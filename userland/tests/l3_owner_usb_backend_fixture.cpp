// SPDX-License-Identifier: GPL-2.0-only
//
// Offline one-path fixture: the real It930xController control transaction is
// executed through L3OwnerTransport -> common owner transfer/op management.
// The USB provider is fake; no hardware is used.

#include "l3_adapter.h"
#include "l3_owner_transport.h"
#include "px4/it930x.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

using namespace px4::userland;

std::uint16_t checksum(const std::uint8_t* data, std::size_t size) noexcept
{
    std::uint16_t sum = 0U;
    std::size_t index = 0U;
    while (index + 1U < size) {
        sum = static_cast<std::uint16_t>(
            sum + static_cast<std::uint16_t>(
                      (static_cast<std::uint16_t>(data[index]) << 8U) |
                      data[index + 1U]));
        index += 2U;
    }
    if (index < size) {
        sum = static_cast<std::uint16_t>(
            sum + static_cast<std::uint16_t>(
                      static_cast<std::uint16_t>(data[index]) << 8U));
    }
    return static_cast<std::uint16_t>(~sum);
}

class FakeIt930xUsb final : public L3UsbPort {
public:
    dtv_l3_claim_result claim(dtv_l3_provider_binding*,
                              std::uint32_t claim_index) noexcept override
    {
        if (claim_index != 0U) {
            return {DTV_L3_INVALID, DTV_L3_CLAIM_NONE};
        }
        return {DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
    }

    dtv_l3_release_result release_claim(
        dtv_l3_provider_binding*, std::uint32_t) noexcept override
    {
        return {DTV_L3_RELEASE_QUIESCED, DTV_L3_CLAIM_NONE,
                DTV_L3_RESTORE_SUCCEEDED, DTV_L3_OK};
    }

    dtv_l3_result submit(dtv_l3_provider_binding* binding,
                         std::uint64_t transfer_id,
                         const dtv_l3_transfer_request* request,
                         dtv_l3_transfer_complete_fn complete,
                         void* callback_context) noexcept override
    {
        ++submit_calls;
        if (request->kind != DTV_L3_TRANSFER_BULK) {
            return DTV_L3_INVALID;
        }
        if (request->out_size != 0U) {
            if (request->endpoint != kCommandOutEndpoint ||
                request->out_size < 7U) {
                return DTV_L3_INVALID;
            }
            sequence = request->out_data[3U];
            if (request->out_data[1U] != 0U ||
                request->out_data[2U] != 0x22U ||
                request->out_data[4U] != 1U) {
                return DTV_L3_INVALID;
            }
            ++write_calls;
            const dtv_l3_transfer_completion completion{
                DTV_L3_OK, nullptr, 0U, 0U, 1U};
            complete(binding, transfer_id, &completion, callback_context);
            ++completion_calls;
            return DTV_L3_ACCEPTED;
        }
        if (request->in_capacity < 9U ||
            request->endpoint != kCommandInEndpoint) {
            return DTV_L3_INVALID;
        }
        std::array<std::uint8_t, 9U> response{};
        response[0U] = 8U;
        response[1U] = sequence;
        response[2U] = 0U;
        response[3U] = 0x00U;
        response[4U] = 0x01U;
        response[5U] = 0x04U;
        response[6U] = 0x00U;
        const std::uint16_t sum = checksum(response.data() + 1U, 6U);
        response[7U] = static_cast<std::uint8_t>(sum >> 8U);
        response[8U] = static_cast<std::uint8_t>(sum);
        ++read_calls;
        const dtv_l3_transfer_completion completion{
            DTV_L3_OK, response.data(), response.size(), response.size(), 1U};
        complete(binding, transfer_id, &completion, callback_context);
        ++completion_calls;
        return DTV_L3_ACCEPTED;
    }

    dtv_l3_result cancel(dtv_l3_provider_binding*, std::uint64_t) noexcept override
    {
        ++cancel_calls;
        return DTV_L3_ACCEPTED;
    }

    dtv_l3_cancel_scope cancel_scope() const noexcept override
    {
        return DTV_L3_CANCEL_SCOPE_TRANSFER;
    }

    std::uint8_t sequence = 0U;
    unsigned int submit_calls = 0U;
    unsigned int write_calls = 0U;
    unsigned int read_calls = 0U;
    unsigned int completion_calls = 0U;
    unsigned int cancel_calls = 0U;
};

class OwnerBackendProduct final : public L3Product {
public:
    dtv_l3_capabilities capabilities() const noexcept override
    {
        dtv_l3_capabilities capabilities{};
        capabilities.receiver_count = 1U;
        capabilities.cancel_scope = DTV_L3_CANCEL_SCOPE_TRANSFER;
        return capabilities;
    }

    dtv_l3_result bind(dtv_l3_context* owner) noexcept override
    {
        owner_ = owner;
        ++bind_calls;
        return owner == nullptr ? DTV_L3_INVALID : DTV_L3_OK;
    }

    dtv_l3_result initialize(dtv_l3_context* owner) noexcept override
    {
        ++initialize_calls;
        if (owner == nullptr) {
            return DTV_L3_INVALID;
        }
        L3OwnerTransport transport(owner, 0U);
        It930xController controller(
            transport, CommandPacingOptions{CommandPacingMode::no_delay});
        const auto version = controller.firmware_version();
        if (!version) {
            version_error = static_cast<int>(version.error());
            return DTV_L3_FAILED;
        }
        firmware_version = version.value();
        return DTV_L3_OK;
    }

    dtv_l3_result scope_query(std::uint32_t,
                              dtv_l3_scope_info* scope) noexcept override
    {
        if (scope == nullptr) {
            return DTV_L3_INVALID;
        }
        scope->bridge_id = 0U;
        scope->compatibility = DTV_L3_LEASE_SHAREABLE;
        return DTV_L3_OK;
    }

    dtv_l3_result tune(std::uint32_t, std::uint64_t,
                       std::uint64_t*) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_result bridge_start(std::uint32_t,
                               std::uint32_t) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_result bridge_stop(std::uint32_t) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_result stream_attach(std::uint64_t,
                                dtv_l3_ts_deliver_fn,
                                void*) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_result stream_stop(std::uint64_t) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_context* owner_ = nullptr;
    unsigned int bind_calls = 0U;
    unsigned int initialize_calls = 0U;
    std::uint32_t firmware_version = 0U;
    int version_error = 0;
};

int wait_open(dtv_l3_op* op, dtv_l3_op_result* result)
{
    for (unsigned int attempt = 0U; attempt < 1000U; ++attempt) {
        if (dtv_l3_op_poll(op, result) != DTV_L3_OK) {
            return 0;
        }
        if (result->state == DTV_L3_OP_DONE ||
            result->state == DTV_L3_OP_FAILED) {
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return 0;
}

bool release_context(dtv_l3_context* owner)
{
    for (unsigned int attempt = 0U; attempt < 500U; ++attempt) {
        const dtv_l3_result result = dtv_l3_context_release(owner);
        if (result == DTV_L3_OK) {
            return true;
        }
        if (result != DTV_L3_BUSY) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

}  // namespace

int main()
{
    FakeIt930xUsb usb_port;
    OwnerBackendProduct product;
    L3ProductAdapter product_adapter(product);
    L3UsbProviderAdapter usb_adapter(usb_port);
    const dtv_l3_usb_provider usb = usb_adapter.provider();
    const dtv_l3_product_adapter adapter = product_adapter.adapter();
    const dtv_l3_open_result opened = dtv_l3_open_ex(&usb, &adapter, 1U);
    dtv_l3_op_result open_result{};
    bool open_ok = false;
    if (opened.result == DTV_L3_ACCEPTED && opened.op != nullptr &&
        wait_open(opened.op, &open_result)) {
        open_ok = open_result.state == DTV_L3_OP_DONE &&
                  open_result.result == DTV_L3_OK;
        (void)dtv_l3_op_release(opened.op);
    }
    bool cleaned = false;
    if (opened.owner != nullptr) {
        (void)dtv_l3_close(opened.owner, 2000U);
        dtv_l3_close_status status{};
        for (unsigned int attempt = 0U; attempt < 500U; ++attempt) {
            if (dtv_l3_close_status_get(opened.owner, &status) == DTV_L3_OK &&
                status.releasable) {
                cleaned = release_context(opened.owner);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const bool ok = open_ok && product.bind_calls == 1U &&
                    product.initialize_calls == 1U &&
                    product.firmware_version == 0x00010400U &&
                    usb_port.write_calls == 1U && usb_port.read_calls == 1U &&
                    usb_port.submit_calls == 2U &&
                    usb_port.completion_calls == 2U && cleaned;
    std::printf(
        "{\"open\":%s,\"bind\":%u,\"initialize\":%u,\"version\":%u,"
        "\"writes\":%u,\"reads\":%u,\"submits\":%u,\"completions\":%u,"
        "\"version_error\":%d,\"cleaned\":%s,\"ok\":%s}\n",
        open_ok ? "true" : "false", product.bind_calls,
        product.initialize_calls, product.firmware_version, usb_port.write_calls,
        usb_port.read_calls, usb_port.submit_calls, usb_port.completion_calls,
        product.version_error, cleaned ? "true" : "false",
        ok ? "true" : "false");
    return ok ? 0 : 1;
}

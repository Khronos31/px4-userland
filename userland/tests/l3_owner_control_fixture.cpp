// SPDX-License-Identifier: GPL-2.0-only
//
// Manual G1/G2 fixture: a product bind/initialize callback performs exactly one
// control transfer through the common owner.  It is not registered with CTest
// until the Core candidate accepts transfer_start from the initialize lane.

#include "l3_adapter.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

using namespace px4::userland;

class FakeUsbPort final : public L3UsbPort {
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
        if (request->kind != DTV_L3_TRANSFER_CONTROL ||
            request->control.request_type != 0xc0U ||
            request->control.request != 0x06U) {
            control_shape_ok = false;
        }
        const dtv_l3_transfer_completion completion{
            DTV_L3_OK, data.data(), data.size(), data.size(), 1U};
        complete(binding, transfer_id, &completion, callback_context);
        return DTV_L3_ACCEPTED;
    }

    dtv_l3_result cancel(dtv_l3_provider_binding*, std::uint64_t) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_cancel_scope cancel_scope() const noexcept override
    {
        return DTV_L3_CANCEL_SCOPE_PIPE;
    }

    std::array<std::uint8_t, 8> data{{0x12U, 0x01U, 0x00U, 0x02U,
                                      0x00U, 0x00U, 0x00U, 0x40U}};
    unsigned int submit_calls = 0U;
    bool control_shape_ok = true;
};

class ControlProduct final : public L3Product {
public:
    dtv_l3_capabilities capabilities() const noexcept override
    {
        dtv_l3_capabilities capabilities{};
        capabilities.receiver_count = 1U;
        capabilities.cancel_scope = DTV_L3_CANCEL_SCOPE_PIPE;
        return capabilities;
    }

    const dtv_l3_capability_catalog* catalog() const noexcept override
    {
        return &catalog_;
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
            initialize_result = DTV_L3_INVALID;
            return initialize_result;
        }
        dtv_l3_transfer_request request{};
        request.kind = DTV_L3_TRANSFER_CONTROL;
        request.endpoint = 0U;
        request.timeout_ms = 100U;
        request.in_capacity = 8U;
        request.claim_index = 0U;
        request.control.request_type = 0xc0U;
        request.control.request = 0x06U;
        request.control.value = 0x0100U;
        request.control.index = 0U;
        dtv_l3_op* op = nullptr;
        initialize_result = dtv_l3_transfer_start(owner_, &request, &op);
        if (initialize_result != DTV_L3_ACCEPTED || op == nullptr) {
            return initialize_result;
        }
        const dtv_l3_result waited = dtv_l3_op_wait(op, 2000U);
        dtv_l3_op_result result{};
        if (waited != DTV_L3_OK ||
            dtv_l3_op_poll(op, &result) != DTV_L3_OK ||
            result.state != DTV_L3_OP_DONE || result.result != DTV_L3_OK ||
            result.data_size != 8U) {
            (void)dtv_l3_op_release(op);
            initialize_result = DTV_L3_FAILED;
            return initialize_result;
        }
        std::memcpy(received.data(), result.data, received.size());
        received_size = result.data_size;
        (void)dtv_l3_op_release(op);
        initialize_result = DTV_L3_OK;
        return initialize_result;
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
    dtv_l3_result initialize_result = DTV_L3_FAILED;
    std::array<std::uint8_t, 8> received{};
    std::size_t received_size = 0U;

private:
    dtv_l3_capability_catalog catalog_{};
};

}  // namespace

int main()
{
    FakeUsbPort usb_port;
    ControlProduct product;
    L3ProductAdapter product_adapter(product);
    L3UsbProviderAdapter usb_adapter(usb_port);
    const dtv_l3_usb_provider usb = usb_adapter.provider();
    const dtv_l3_product_adapter adapter = product_adapter.adapter();
    const dtv_l3_open_result opened = dtv_l3_open_ex(&usb, &adapter, 1U);
    dtv_l3_op_result open_result{};
    dtv_l3_result open_wait = DTV_L3_FAILED;
    if (opened.result == DTV_L3_ACCEPTED && opened.op != nullptr) {
        (void)dtv_l3_op_wait(opened.op, 2000U);
        (void)dtv_l3_op_poll(opened.op, &open_result);
        open_wait = open_result.result;
        (void)dtv_l3_op_release(opened.op);
    }
    const bool ok = opened.result == DTV_L3_ACCEPTED &&
                    opened.owner != nullptr && open_wait == DTV_L3_OK &&
                    product.bind_calls == 1U &&
                    product.initialize_calls == 1U &&
                    product.initialize_result == DTV_L3_OK &&
                    usb_port.submit_calls == 1U &&
                    usb_port.control_shape_ok && product.received_size == 8U;
    std::printf(
        "{\"open_result\":%d,\"open_wait\":%d,\"bind\":%u,"
        "\"initialize\":%u,\"initialize_result\":%d,\"submit\":%u,"
        "\"control_shape\":%s,\"bytes\":%zu,\"ok\":%s}\n",
        static_cast<int>(opened.result), static_cast<int>(open_wait),
        product.bind_calls, product.initialize_calls,
        static_cast<int>(product.initialize_result), usb_port.submit_calls,
        usb_port.control_shape_ok ? "true" : "false",
        product.received_size, ok ? "true" : "false");
    if (opened.owner != nullptr) {
        dtv_l3_close_status status{};
        if (dtv_l3_close(opened.owner, 2000U) == DTV_L3_OK) {
            for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
                if (dtv_l3_close_status_get(opened.owner, &status) ==
                        DTV_L3_OK &&
                    status.releasable) {
                    (void)dtv_l3_context_release(opened.owner);
                    break;
                }
            }
        }
    }
    return ok ? 0 : 1;
}

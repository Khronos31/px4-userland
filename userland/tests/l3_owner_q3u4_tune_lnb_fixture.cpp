// SPDX-License-Identifier: GPL-2.0-only
//
// Offline fixture: the real Q3U4 frontend/LNB coordinator is driven by
// It930xController instances whose Transport is L3OwnerTransport.  USB
// submit/completion is fake; no hardware is used.  Stream is intentionally a
// no-op because it is the next migration unit.

#include "l3_adapter.h"
#include "l3_backend_product.h"
#include "l3_owner_transport.h"
#include "l3_q3u4_owner_backend.h"
#include "px4/it930x.h"
#include "q3u4_frontend.h"
#include "q3u4_lnb_power.h"
#include "q3u4_tuner_backend.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

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

class FakeUsb final : public L3UsbPort {
public:
    dtv_l3_claim_result claim(dtv_l3_provider_binding*,
                              std::uint32_t claim_index) noexcept override
    {
        if (claim_index >= 2U) {
            return {DTV_L3_INVALID, DTV_L3_CLAIM_NONE};
        }
        ++claim_calls;
        return {DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
    }

    dtv_l3_release_result release_claim(
        dtv_l3_provider_binding*, std::uint32_t) noexcept override
    {
        ++release_calls;
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
        if (request->out_size == 0U &&
            (request->endpoint == kTsInEndpoint ||
             request->endpoint == kObservedTsInEndpoint)) {
            const std::size_t packets =
                request->in_capacity >= 188U ? request->in_capacity / 188U : 0U;
            std::vector<std::uint8_t> ts(packets * 188U);
            for (std::size_t packet = 0U; packet < packets; ++packet) {
                std::uint8_t* target = ts.data() + packet * 188U;
                target[0U] = static_cast<std::uint8_t>((3U << 4U) | 0x07U);
                target[1U] = 0x01U;
                target[2U] = 0x01U;
                target[3U] = static_cast<std::uint8_t>(0x10U |
                                                       (stream_cc++ & 0x0fU));
            }
            ++stream_read_calls;
            const dtv_l3_transfer_completion completion{
                DTV_L3_OK, ts.data(), ts.size(), ts.size(), 1U};
            complete(binding, transfer_id, &completion, callback_context);
            ++completion_calls;
            return DTV_L3_ACCEPTED;
        }
        if (request->out_size != 0U) {
            if (request->out_data[1U] != 0U) {
                return DTV_L3_INVALID;
            }
            last_command = request->out_data[2U];
            sequence = request->out_data[3U];
            pending_length = 0U;
            if (last_command == 0x2aU) {
                pending_length = request->out_data[4U];
            } else if (last_command == 0x00U) {
                pending_length = request->out_data[4U];
            } else if (last_command == 0x2bU) {
                last_i2c_register = request->out_size > 7U
                                        ? request->out_data[7U] : 0U;
            }
            ++write_calls;
            const dtv_l3_transfer_completion completion{
                DTV_L3_OK, nullptr, 0U, 0U, 1U};
            complete(binding, transfer_id, &completion, callback_context);
            ++completion_calls;
            return DTV_L3_ACCEPTED;
        }
        if (request->in_capacity < pending_length) {
            return DTV_L3_INVALID;
        }
        std::array<std::uint8_t, 260U> response{};
        const std::size_t payload_length = pending_length;
        if (last_command == 0x2aU) {
            if (payload_length == 1U) {
                if (last_i2c_register == 0xb0U) {
                    response[3U] = 0x0fU;
                } else if (last_i2c_register == 0x80U) {
                    response[3U] = 0x00U;
                } else {
                    response[3U] = 0x98U;
                }
            } else if (payload_length == 3U) {
                response[5U] = 0x02U;
            } else if (payload_length == 4U) {
                response[6U] = 0x70U;
            }
        }
        const std::size_t response_size = payload_length + 5U;
        response[0U] = static_cast<std::uint8_t>(response_size - 1U);
        response[1U] = sequence;
        response[2U] = 0U;
        const std::uint16_t sum =
            checksum(response.data() + 1U, response_size - 3U);
        response[response_size - 2U] = static_cast<std::uint8_t>(sum >> 8U);
        response[response_size - 1U] = static_cast<std::uint8_t>(sum);
        ++read_calls;
        const dtv_l3_transfer_completion completion{
            DTV_L3_OK, response.data(), response_size, response_size, 1U};
        complete(binding, transfer_id, &completion, callback_context);
        ++completion_calls;
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

    unsigned int claim_calls = 0U;
    unsigned int release_calls = 0U;
    unsigned int submit_calls = 0U;
    unsigned int write_calls = 0U;
    unsigned int read_calls = 0U;
    unsigned int completion_calls = 0U;
    unsigned int stream_read_calls = 0U;
    std::uint8_t stream_cc = 0U;
    std::uint8_t last_command = 0U;
    std::uint8_t sequence = 0U;
    std::uint8_t last_i2c_register = 0U;
    std::size_t pending_length = 0U;
};

struct FactoryContext final {
    const dtv_l3_product_adapter* adapter = nullptr;
};

dtv_l3_result factory_create(void* context, const dtv_l3_open_args*,
                             const dtv_l3_product_adapter** adapter_out,
                             uint32_t* claim_count_out,
                             void** owner_out)
{
    auto* factory = static_cast<FactoryContext*>(context);
    if (factory == nullptr || factory->adapter == nullptr ||
        adapter_out == nullptr || claim_count_out == nullptr ||
        owner_out == nullptr) {
        return DTV_L3_INVALID;
    }
    *adapter_out = factory->adapter;
    *claim_count_out = 2U;
    *owner_out = factory;
    return DTV_L3_OK;
}

void factory_release(void*) {}

int wait_open(dtv_l3_op* op, dtv_l3_op_result* result)
{
    for (unsigned int attempt = 0U; attempt < 2000U; ++attempt) {
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
    for (unsigned int attempt = 0U; attempt < 1000U; ++attempt) {
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
    FakeUsb usb_port;
    const DeviceProfile& profile = device_profile(DeviceModel::px_q3u4);
    L3Q3U4OwnerBackend product(profile, true, false, nullptr);
    L3ProductAdapter product_adapter(product);
    L3UsbProviderAdapter usb_adapter(usb_port);
    const dtv_l3_usb_provider usb = usb_adapter.provider();
    const dtv_l3_product_adapter adapter_value = product_adapter.adapter();
    FactoryContext factory_context{&adapter_value};
    const dtv_l3_factory factory{sizeof(dtv_l3_factory), &factory_context,
                                 &factory_create, &factory_release};
    dtv_l3_open_args args{};
    args.struct_size = sizeof(args);
    args.usb_provider = &usb;
    args.factory = &factory;
    args.grants = nullptr;
    args.grant_count = 0U;
    args.firmware = nullptr;
    args.power_permission = 1U;
    const dtv_l3_open_result opened = dtv_l3_open_with_args(&args);
    dtv_l3_op_result open_result{};
    bool open_ok = false;
    if (opened.result == DTV_L3_ACCEPTED && opened.op != nullptr &&
        wait_open(opened.op, &open_result)) {
        open_ok = open_result.state == DTV_L3_OP_DONE &&
                  open_result.result == DTV_L3_OK;
        (void)dtv_l3_op_release(opened.op);
    }

    dtv_l3_lnb_state lnb{};
    dtv_l3_lnb_state after{};
    dtv_l3_lease* lease = nullptr;
    dtv_l3_stream* stream_handle = nullptr;
    std::uint8_t ts_buffer[188U * 64U];
    std::size_t ts_bytes = 0U;
    dtv_l3_read_state read_state = DTV_L3_READ_WAIT;
    dtv_l3_result stream_result = DTV_L3_FAILED;
    dtv_l3_tune_request tune{};
    uint64_t programmed = 0U;
    uint64_t revision = 0U;
    bool worked = false;
    bool cleaned = false;
    dtv_l3_result off_result = DTV_L3_FAILED;
    dtv_l3_result on_result = DTV_L3_FAILED;
    dtv_l3_result tune_result = DTV_L3_FAILED;
    dtv_l3_result start_result = DTV_L3_FAILED;
    if (open_ok) {
        (void)dtv_l3_lnb_status(opened.owner, 0U, &lnb);
        off_result =
            dtv_l3_lnb_set(opened.owner, 0U, DTV_L3_LNB_OFF, 0U, &after);
        on_result =
            dtv_l3_lnb_set(opened.owner, 0U, DTV_L3_LNB_ON_15V, 0U, &after);
        worked = off_result == DTV_L3_OK && on_result == DTV_L3_OK &&
                 after.controlled == DTV_L3_LNB_COMMAND_ON_15V;
        if (worked) {
            worked = dtv_l3_lease_acquire(opened.owner, 2U, &lease) ==
                         DTV_L3_OK &&
                     lease != nullptr;
        }
        if (worked) {
            tune.system = DTV_L3_TUNE_SYSTEM_ISDB_T;
            tune.selection = DTV_L3_TUNE_SELECTION_AUTO;
            tune.requested_hz = 527143000ULL;
            tune_result = dtv_l3_lease_tune_ex(lease, &tune, &programmed,
                                               &revision);
            worked = tune_result == DTV_L3_OK;
        }
        if (worked) {
            start_result = dtv_l3_lease_start(lease);
            worked = start_result == DTV_L3_OK;
        }
        if (worked) {
            worked = dtv_l3_lnb_status(opened.owner, 0U, &lnb) == DTV_L3_OK &&
                     lnb.controlled == DTV_L3_LNB_COMMAND_ON_15V;
        }
        if (worked) {
            worked = dtv_l3_lease_attach(lease, &stream_handle) == DTV_L3_OK &&
                     stream_handle != nullptr;
        }
        if (worked) {
            for (unsigned int attempt = 0U; attempt < 200U; ++attempt) {
                stream_result = dtv_l3_stream_read_ts_wait(
                    stream_handle, ts_buffer, sizeof(ts_buffer), &ts_bytes,
                    &read_state, 1000U);
                if (stream_result == DTV_L3_OK && ts_bytes != 0U) {
                    break;
                }
                if (stream_result != DTV_L3_OK &&
                    stream_result != DTV_L3_WAIT_TIMEOUT) {
                    break;
                }
            }
            worked = stream_result == DTV_L3_OK && ts_bytes != 0U;
        }
        if (stream_handle != nullptr) {
            (void)dtv_l3_stream_stop(stream_handle, 2000U);
            for (unsigned int attempt = 0U; attempt < 500U; ++attempt) {
                const dtv_l3_result released = dtv_l3_stream_release(
                    stream_handle, DTV_L3_STREAM_RELEASE_DISCARD_UNREAD);
                if (released == DTV_L3_OK || released == DTV_L3_STALE) {
                    break;
                }
                if (released != DTV_L3_ACCEPTED && released != DTV_L3_BUSY) {
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            (void)dtv_l3_stream_destroy(stream_handle);
        }
        if (lease != nullptr) {
            (void)dtv_l3_lease_release(lease);
        }
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
    const bool ok = open_ok && worked && cleaned && ts_bytes != 0U &&
                    usb_port.stream_read_calls != 0U &&
                    usb_port.submit_calls > 2U &&
                    usb_port.completion_calls == usb_port.submit_calls;
    std::printf(
        "{\"open\":%s,\"off\":%d,\"on\":%d,\"tune\":%d,"
        "\"start\":%d,\"stream\":%d,\"ts_bytes\":%zu,\"submits\":%u,"
        "\"completions\":%u,\"stream_reads\":%u,\"cleaned\":%s,"
        "\"ok\":%s}\n",
        open_ok ? "true" : "false", static_cast<int>(off_result),
        static_cast<int>(on_result), static_cast<int>(tune_result),
        static_cast<int>(start_result), static_cast<int>(stream_result),
        ts_bytes, usb_port.submit_calls, usb_port.completion_calls,
        usb_port.stream_read_calls, cleaned ? "true" : "false",
        ok ? "true" : "false");
    return ok ? 0 : 1;
}

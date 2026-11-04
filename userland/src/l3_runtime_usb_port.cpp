// SPDX-License-Identifier: GPL-2.0-only
#include "l3_runtime_usb_port.h"

#include <vector>

namespace px4::userland {

namespace {

dtv_l3_result from_error(Error error) noexcept
{
    switch (error) {
    case Error::OK: return DTV_L3_OK;
    case Error::INVALID_ARGUMENT:
    case Error::BUFFER_TOO_SMALL: return DTV_L3_INVALID;
    case Error::BUSY: return DTV_L3_BUSY;
    case Error::NOT_READY: return DTV_L3_FAILED;
    case Error::TIMEOUT: return DTV_L3_TIMEOUT;
    case Error::DISCONNECTED: return DTV_L3_DISCONNECTED;
    case Error::UNSUPPORTED: return DTV_L3_UNSUPPORTED;
    default: return DTV_L3_FAILED;
    }
}

}  // namespace

L3RuntimeUsbPort::L3RuntimeUsbPort(Q3U4Runtime& runtime) noexcept
    : runtime_(&runtime)
{
}

dtv_l3_claim_result L3RuntimeUsbPort::claim(
    dtv_l3_provider_binding*, std::uint32_t claim_index) noexcept
{
    if (claim_index >= 2U) {
        return {DTV_L3_INVALID, DTV_L3_CLAIM_NONE};
    }
    ++claim_calls;
    return {DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
}

dtv_l3_release_result L3RuntimeUsbPort::release_claim(
    dtv_l3_provider_binding*, std::uint32_t claim_index) noexcept
{
    if (claim_index >= 2U) {
        return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN, DTV_L3_CLAIM_UNKNOWN,
                DTV_L3_RESTORE_NOT_ATTEMPTED, DTV_L3_INVALID};
    }
    ++release_calls;
    return {DTV_L3_RELEASE_QUIESCED, DTV_L3_CLAIM_NONE,
            DTV_L3_RESTORE_SUCCEEDED, DTV_L3_OK};
}

dtv_l3_result L3RuntimeUsbPort::submit(
    dtv_l3_provider_binding* binding, std::uint64_t transfer_id,
    const dtv_l3_transfer_request* request,
    dtv_l3_transfer_complete_fn complete,
    void* callback_context) noexcept
{
    if (runtime_ == nullptr || request == nullptr || complete == nullptr ||
        request->kind != DTV_L3_TRANSFER_BULK ||
        request->claim_index >= 2U) {
        return DTV_L3_UNSUPPORTED;
    }
    Transport& transport = request->claim_index == 0U
                               ? runtime_->dev1() : runtime_->dev2();
    const Timeout timeout{request->timeout_ms};
    if (request->out_size != 0U) {
        const auto written = transport.bulk_write(
            static_cast<std::uint8_t>(request->endpoint),
            ByteView{request->out_data, request->out_size}, timeout);
        if (!written) {
            return from_error(written.error());
        }
        ++write_calls;
        const dtv_l3_transfer_completion completion{
            DTV_L3_OK, nullptr, 0U, written.value(), 1U};
        complete(binding, transfer_id, &completion, callback_context);
        ++completion_calls;
        return DTV_L3_ACCEPTED;
    }
    std::vector<std::uint8_t> buffer(request->in_capacity);
    const auto read = transport.bulk_read(
        static_cast<std::uint8_t>(request->endpoint),
        MutableByteView{buffer.data(), buffer.size()}, timeout);
    if (!read) {
        return from_error(read.error());
    }
    ++read_calls;
    const dtv_l3_transfer_completion completion{
        DTV_L3_OK, buffer.data(), read.value(), read.value(), 1U};
    complete(binding, transfer_id, &completion, callback_context);
    ++completion_calls;
    return DTV_L3_ACCEPTED;
}

dtv_l3_result L3RuntimeUsbPort::cancel(
    dtv_l3_provider_binding*, std::uint64_t) noexcept
{
    return DTV_L3_UNSUPPORTED;
}

dtv_l3_cancel_scope L3RuntimeUsbPort::cancel_scope() const noexcept
{
    return DTV_L3_CANCEL_SCOPE_PIPE;
}

}  // namespace px4::userland

// SPDX-License-Identifier: GPL-2.0-only
#include "l3_owner_transport.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace px4::userland {

namespace {

Error from_l3_result(dtv_l3_result result) noexcept
{
    switch (result) {
    case DTV_L3_OK: return Error::OK;
    case DTV_L3_INVALID: return Error::INVALID_ARGUMENT;
    case DTV_L3_BUSY: return Error::BUSY;
    case DTV_L3_TIMEOUT: return Error::TIMEOUT;
    case DTV_L3_UNSUPPORTED: return Error::UNSUPPORTED;
    case DTV_L3_DISCONNECTED: return Error::DISCONNECTED;
    case DTV_L3_CANCELLED:
    case DTV_L3_ACCEPTED:
    case DTV_L3_FAILED:
    case DTV_L3_STALE:
    case DTV_L3_WAIT_TIMEOUT:
    case DTV_L3_CLOSING:
    case DTV_L3_OUTCOME_UNKNOWN:
    case DTV_L3_REJECTED:
        return Error::USB_IO;
    }
    return Error::USB_IO;
}

}  // namespace

L3OwnerTransport::L3OwnerTransport(dtv_l3_context* owner,
                                   std::uint32_t claim_index) noexcept
    : owner_(owner), claim_index_(claim_index)
{
}

Result<std::size_t> L3OwnerTransport::bulk_read(
    std::uint8_t endpoint, MutableByteView output, Timeout timeout,
    BulkReadObservation* observation) noexcept
{
    if (observation != nullptr) {
        *observation = BulkReadObservation{};
    }
    if (owner_ == nullptr || (output.size != 0U && output.data == nullptr)) {
        return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
    }
    const auto result = transfer(DTV_L3_TRANSFER_BULK, endpoint, nullptr, 0U,
                                 output.data, output.size, timeout);
    if (result && observation != nullptr) {
        observation->completion_error = Error::OK;
        observation->transferred = result.value();
    }
    return result;
}

Result<std::size_t> L3OwnerTransport::bulk_write(
    std::uint8_t endpoint, ByteView input, Timeout timeout) noexcept
{
    if (owner_ == nullptr || (input.size != 0U && input.data == nullptr)) {
        return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
    }
    return transfer(DTV_L3_TRANSFER_BULK, endpoint, input.data, input.size,
                    nullptr, 0U, timeout);
}

Result<std::size_t> L3OwnerTransport::transfer(
    std::uint32_t kind, std::uint8_t endpoint, const std::uint8_t* out_data,
    std::size_t out_size, std::uint8_t* in_data, std::size_t in_capacity,
    Timeout timeout) noexcept
{
    dtv_l3_transfer_request request{};
    request.kind = kind;
    request.endpoint = endpoint;
    request.timeout_ms = timeout.milliseconds;
    request.out_data = out_data;
    request.out_size = out_size;
    request.in_capacity = in_capacity;
    request.claim_index = claim_index_;

    dtv_l3_op* op = nullptr;
    const dtv_l3_result started =
        dtv_l3_transfer_start(owner_, &request, &op);
    if (started != DTV_L3_ACCEPTED || op == nullptr) {
        return Result<std::size_t>::failure(from_l3_result(started));
    }

    const std::uint32_t wait_ms =
        timeout.milliseconds == 0U ? DTV_L3_WAIT_FOREVER
                                   : timeout.milliseconds;
    dtv_l3_op_result op_result{};
    dtv_l3_result waited = dtv_l3_op_wait(op, wait_ms);
    if (waited == DTV_L3_OK) {
        waited = dtv_l3_op_poll(op, &op_result);
    }
    if (waited != DTV_L3_OK ||
        (op_result.state != DTV_L3_OP_DONE &&
         op_result.state != DTV_L3_OP_FAILED)) {
        (void)dtv_l3_op_cancel(op);
        (void)dtv_l3_op_wait(op, 100U);
        (void)dtv_l3_op_poll(op, &op_result);
        if (op_result.state == DTV_L3_OP_DONE ||
            op_result.state == DTV_L3_OP_FAILED) {
            (void)dtv_l3_op_release(op);
        }
        return Result<std::size_t>::failure(
            waited == DTV_L3_WAIT_TIMEOUT ? Error::TIMEOUT
                                          : from_l3_result(waited));
    }
    const dtv_l3_result terminal = op_result.result;
    const std::uint8_t* data = op_result.data;
    const std::size_t size = op_result.data_size;
    // Result data is valid until op_release; copy IN data before releasing.
    const bool copy_ok =
        in_capacity == 0U ? true
                          : (size <= in_capacity && (size == 0U || data != nullptr));
    if (copy_ok && in_capacity != 0U && size != 0U) {
        std::copy(data, data + size, in_data);
    }
    const dtv_l3_result released = dtv_l3_op_release(op);
    if (released != DTV_L3_OK) {
        return Result<std::size_t>::failure(from_l3_result(released));
    }
    if (op_result.state != DTV_L3_OP_DONE || terminal != DTV_L3_OK) {
        return Result<std::size_t>::failure(from_l3_result(terminal));
    }
    if (in_capacity == 0U) {
        return Result<std::size_t>::success(out_size);
    }
    if (!copy_ok) {
        return Result<std::size_t>::failure(Error::BUFFER_TOO_SMALL);
    }
    return Result<std::size_t>::success(size);
}

bool L3OwnerTransport::stream_active() const noexcept
{
    std::lock_guard<std::mutex> lock(stream_mutex_);
    return stream_active_;
}

Result<void> L3OwnerTransport::start_stream(
    const StreamConfig& config) noexcept
{
    if (owner_ == nullptr || config.endpoint == 0U ||
        config.transfer_size == 0U) {
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (stream_op_ != nullptr) {
        return Result<void>::failure(Error::BUSY);
    }
    stream_config_ = config;
    stream_active_ = true;
    stream_buffer_.clear();
    return Result<void>::success();
}

Result<StreamEvent> L3OwnerTransport::wait_stream(Timeout timeout) noexcept
{
    std::vector<std::uint8_t> request_buffer;
    std::uint8_t endpoint = 0U;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (owner_ == nullptr || !stream_active_) {
            return Result<StreamEvent>::failure(Error::NOT_READY);
        }
        if (stream_op_ != nullptr) {
            return Result<StreamEvent>::failure(Error::BUSY);
        }
        endpoint = stream_config_.endpoint;
        request_buffer.resize(stream_config_.transfer_size);
        stream_buffer_.assign(stream_config_.transfer_size, 0U);
    }

    dtv_l3_transfer_request request{};
    request.kind = DTV_L3_TRANSFER_BULK;
    request.endpoint = endpoint;
    request.timeout_ms = timeout.milliseconds;
    request.in_capacity = request_buffer.size();
    request.claim_index = claim_index_;
    dtv_l3_op* op = nullptr;
    const dtv_l3_result started =
        dtv_l3_transfer_start(owner_, &request, &op);
    if (started != DTV_L3_ACCEPTED || op == nullptr) {
        return Result<StreamEvent>::failure(from_l3_result(started));
    }
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (!stream_active_) {
            (void)dtv_l3_op_cancel(op);
            (void)dtv_l3_op_release(op);
            return Result<StreamEvent>::failure(Error::DISCONNECTED);
        }
        stream_op_ = op;
    }

    const std::uint32_t wait_ms =
        timeout.milliseconds == 0U ? DTV_L3_WAIT_FOREVER
                                   : timeout.milliseconds;
    dtv_l3_op_result op_result{};
    dtv_l3_result waited = dtv_l3_op_wait(op, wait_ms);
    if (waited == DTV_L3_OK) {
        waited = dtv_l3_op_poll(op, &op_result);
    }
    bool timed_out = false;
    if (waited != DTV_L3_OK ||
        (op_result.state != DTV_L3_OP_DONE &&
         op_result.state != DTV_L3_OP_FAILED)) {
        (void)dtv_l3_op_cancel(op);
        (void)dtv_l3_op_wait(op, 100U);
        (void)dtv_l3_op_poll(op, &op_result);
        timed_out = true;
    }

    const dtv_l3_result terminal = op_result.result;
    const std::size_t size =
        op_result.data_size <= request_buffer.size() ? op_result.data_size : 0U;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (size != 0U && op_result.data != nullptr) {
            std::copy(op_result.data, op_result.data + size,
                      stream_buffer_.begin());
        }
        stream_op_ = nullptr;
    }
    const dtv_l3_result released = dtv_l3_op_release(op);
    if (released != DTV_L3_OK) {
        return Result<StreamEvent>::failure(from_l3_result(released));
    }
    if (timed_out) {
        return Result<StreamEvent>::failure(Error::TIMEOUT);
    }
    if (op_result.state != DTV_L3_OP_DONE || terminal != DTV_L3_OK) {
        return Result<StreamEvent>::failure(from_l3_result(terminal));
    }
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (!stream_active_) {
        return Result<StreamEvent>::failure(Error::DISCONNECTED);
    }
    const StreamEventKind kind =
        size < stream_config_.transfer_size ? StreamEventKind::short_transfer
                                            : StreamEventKind::data;
    return Result<StreamEvent>::success(
        StreamEvent{kind, stream_buffer_.data(), size});
}

Result<void> L3OwnerTransport::cancel_stream() noexcept
{
    std::lock_guard<std::mutex> lock(stream_mutex_);
    if (stream_op_ != nullptr) {
        (void)dtv_l3_op_cancel(stream_op_);
    }
    return Result<void>::success();
}

Result<void> L3OwnerTransport::stop_stream() noexcept
{
    std::lock_guard<std::mutex> lock(stream_mutex_);
    stream_active_ = false;
    if (stream_op_ != nullptr) {
        (void)dtv_l3_op_cancel(stream_op_);
    }
    return Result<void>::success();
}

}  // namespace px4::userland

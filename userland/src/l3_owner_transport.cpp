// SPDX-License-Identifier: GPL-2.0-only
#include "l3_owner_transport.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
#include <cstdio>
#endif

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
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=bulk_read claim=%u endpoint=0x%02x capacity=%zu owner=%u result=error error=INVALID_ARGUMENT\n",
                     static_cast<unsigned>(claim_index_),
                     static_cast<unsigned>(endpoint), output.size,
                     owner_ != nullptr ? 1U : 0U);
#endif
        return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
    }
    const auto result = transfer(DTV_L3_TRANSFER_BULK, endpoint, nullptr, 0U,
                                 output.data, output.size, timeout);
    if (result && observation != nullptr) {
        observation->completion_error = Error::OK;
        observation->transferred = result.value();
    }
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=bulk_read claim=%u endpoint=0x%02x capacity=%zu result=%s bytes=%zu\n",
                 static_cast<unsigned>(claim_index_),
                 static_cast<unsigned>(endpoint), output.size,
                 result ? "ok" : error_string(result.error()),
                 result ? result.value() : 0U);
#endif
    return result;
}

Result<std::size_t> L3OwnerTransport::bulk_write(
    std::uint8_t endpoint, ByteView input, Timeout timeout) noexcept
{
    if (owner_ == nullptr || (input.size != 0U && input.data == nullptr)) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=bulk_write claim=%u endpoint=0x%02x bytes=%zu owner=%u result=error error=INVALID_ARGUMENT\n",
                     static_cast<unsigned>(claim_index_),
                     static_cast<unsigned>(endpoint), input.size,
                     owner_ != nullptr ? 1U : 0U);
#endif
        return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
    }
    const auto result = transfer(DTV_L3_TRANSFER_BULK, endpoint, input.data,
                                 input.size, nullptr, 0U, timeout);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=bulk_write claim=%u endpoint=0x%02x bytes=%zu result=%s written=%zu\n",
                 static_cast<unsigned>(claim_index_),
                 static_cast<unsigned>(endpoint), input.size,
                 result ? "ok" : error_string(result.error()),
                 result ? result.value() : 0U);
#endif
    return result;
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

#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=transfer_start phase=call claim=%u kind=%u endpoint=0x%02x out=%zu in=%zu timeout=%u\n",
                 static_cast<unsigned>(claim_index_), static_cast<unsigned>(kind),
                 static_cast<unsigned>(endpoint), out_size, in_capacity,
                 static_cast<unsigned>(timeout.milliseconds));
#endif
    dtv_l3_op* op = nullptr;
    const dtv_l3_result started =
        dtv_l3_transfer_start(owner_, &request, &op);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=transfer_start phase=return result=%u op-present=%u\n",
                 static_cast<unsigned>(started), op != nullptr ? 1U : 0U);
#endif
    if (started != DTV_L3_ACCEPTED || op == nullptr) {
        return Result<std::size_t>::failure(from_l3_result(started));
    }

    const std::uint32_t wait_ms =
        timeout.milliseconds == 0U ? DTV_L3_WAIT_FOREVER
                                   : timeout.milliseconds;
    dtv_l3_op_result op_result{};
    dtv_l3_result waited = dtv_l3_op_wait(op, wait_ms);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=op_wait phase=return result=%u wait-ms=%u\n",
                 static_cast<unsigned>(waited), static_cast<unsigned>(wait_ms));
#endif
    if (waited == DTV_L3_OK) {
        waited = dtv_l3_op_poll(op, &op_result);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=op_poll phase=return result=%u state=%u terminal=%u bytes=%zu\n",
                     static_cast<unsigned>(waited),
                     static_cast<unsigned>(op_result.state),
                     static_cast<unsigned>(op_result.result), op_result.data_size);
#endif
    }
    if (waited != DTV_L3_OK ||
        (op_result.state != DTV_L3_OP_DONE &&
         op_result.state != DTV_L3_OP_FAILED)) {
        const dtv_l3_result cancelled = dtv_l3_op_cancel(op);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=op_cancel phase=return result=%u\n",
                     static_cast<unsigned>(cancelled));
#endif
        (void)cancelled;
        const dtv_l3_result drained =
            dtv_l3_op_wait(op, DTV_L3_WAIT_FOREVER);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=op_wait-drain phase=return result=%u\n",
                     static_cast<unsigned>(drained));
#endif
        (void)drained;
        if (drained == DTV_L3_OK) {
            const dtv_l3_result drain_poll = dtv_l3_op_poll(op, &op_result);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=l3-owner op=op_poll-drain phase=return result=%u state=%u terminal=%u bytes=%zu\n",
                         static_cast<unsigned>(drain_poll),
                         static_cast<unsigned>(op_result.state),
                         static_cast<unsigned>(op_result.result), op_result.data_size);
#endif
            (void)drain_poll;
        }
        if (op_result.state == DTV_L3_OP_DONE ||
            op_result.state == DTV_L3_OP_FAILED) {
            const dtv_l3_result drain_release = dtv_l3_op_release(op);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=l3-owner op=op_release-drain phase=return result=%u\n",
                         static_cast<unsigned>(drain_release));
#endif
            (void)drain_release;
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
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=op_release phase=return result=%u state=%u terminal=%u bytes=%zu copy-ok=%u\n",
                 static_cast<unsigned>(released),
                 static_cast<unsigned>(op_result.state),
                 static_cast<unsigned>(terminal), size, copy_ok ? 1U : 0U);
#endif
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

Result<dtv_l3_op*> L3OwnerTransport::start_stream_operation() noexcept
{
    dtv_l3_transfer_request request{};
    request.kind = DTV_L3_TRANSFER_BULK;
    request.endpoint = stream_config_.endpoint;
    request.timeout_ms = 0U;
    request.in_capacity = stream_config_.transfer_size;
    request.claim_index = claim_index_;
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=stream-transfer_start phase=call claim=%u kind=%u endpoint=0x%02x out=0 in=%zu timeout=0\n",
                 static_cast<unsigned>(claim_index_),
                 static_cast<unsigned>(request.kind),
                 static_cast<unsigned>(request.endpoint), request.in_capacity);
#endif
    dtv_l3_op* op = nullptr;
    const dtv_l3_result started =
        dtv_l3_transfer_start(owner_, &request, &op);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=stream-transfer_start phase=return result=%u op-present=%u\n",
                 static_cast<unsigned>(started), op != nullptr ? 1U : 0U);
#endif
    if (started != DTV_L3_ACCEPTED || op == nullptr) {
        return Result<dtv_l3_op*>::failure(from_l3_result(started));
    }
    return Result<dtv_l3_op*>::success(op);
}

void L3OwnerTransport::cancel_stream_operations() noexcept
{
    std::array<dtv_l3_op*, 64U> operations{};
    std::size_t count = 0U;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        count = stream_operation_count_;
        for (std::size_t index = 0U; index < count; ++index) {
            operations[index] = stream_operations_[index].op;
        }
    }
    for (std::size_t index = 0U; index < count; ++index) {
        if (operations[index] != nullptr) {
            const dtv_l3_result cancelled = dtv_l3_op_cancel(operations[index]);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=l3-owner op=stream-op_cancel slot=%zu result=%u\n",
                         index, static_cast<unsigned>(cancelled));
#endif
            (void)cancelled;
        }
    }
}

Result<void> L3OwnerTransport::drain_stream_operations() noexcept
{
    std::array<dtv_l3_op*, 64U> operations{};
    std::size_t count = 0U;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        count = stream_operation_count_;
        for (std::size_t index = 0U; index < count; ++index) {
            operations[index] = stream_operations_[index].op;
        }
    }
    Error first_error = Error::OK;
    std::array<bool, 64U> released{};
    for (std::size_t index = 0U; index < count; ++index) {
        dtv_l3_op* op = operations[index];
        if (op == nullptr) {
            continue;
        }
        dtv_l3_op_result result{};
        const dtv_l3_result waited = dtv_l3_op_wait(op, DTV_L3_WAIT_FOREVER);
        const dtv_l3_result polled = dtv_l3_op_poll(op, &result);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=stream-op-drain slot=%zu wait=%u poll=%u state=%u terminal=%u bytes=%zu\n",
                     index, static_cast<unsigned>(waited),
                     static_cast<unsigned>(polled),
                     static_cast<unsigned>(result.state),
                     static_cast<unsigned>(result.result), result.data_size);
#endif
        if (waited == DTV_L3_OK && polled == DTV_L3_OK &&
            (result.state == DTV_L3_OP_DONE ||
             result.state == DTV_L3_OP_FAILED)) {
            const dtv_l3_result released_result = dtv_l3_op_release(op);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=l3-owner op=stream-op-release slot=%zu result=%u\n",
                         index, static_cast<unsigned>(released_result));
#endif
            released[index] = released_result == DTV_L3_OK;
            if (!released[index] && first_error == Error::OK) {
                first_error = from_l3_result(released_result);
            }
        } else if (first_error == Error::OK) {
            first_error = from_l3_result(waited != DTV_L3_OK ? waited : polled);
        }
    }
    std::lock_guard<std::mutex> lock(stream_mutex_);
    bool retained = false;
    for (std::size_t index = 0U; index < count; ++index) {
        if (released[index]) {
            stream_operations_[index].op = nullptr;
        } else if (stream_operations_[index].op != nullptr) {
            retained = true;
        }
    }
    if (!retained) {
        stream_operation_count_ = 0U;
        stream_delivery_index_ = 0U;
    }
    return first_error == Error::OK ? Result<void>::success()
                                    : Result<void>::failure(first_error);
}

Result<void> L3OwnerTransport::start_stream(
    const StreamConfig& config) noexcept
{
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=start_stream phase=call owner=%u endpoint=0x%02x capacity=%zu transfers=%zu\n",
                 owner_ != nullptr ? 1U : 0U,
                 static_cast<unsigned>(config.endpoint), config.transfer_size,
                 config.transfer_count);
#endif
    if (owner_ == nullptr || config.endpoint == 0U ||
        config.transfer_size == 0U || config.transfer_count == 0U ||
        config.transfer_count > stream_operations_.size() ||
        config.endpoint != kTsInEndpoint ||
        config.transfer_size > 1048576U) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_ATTACH_DIAG stage=L3-stream-config owner=%u endpoint=0x%02x capacity=%zu transfers=%zu\n",
                     owner_ != nullptr ? 1U : 0U,
                     static_cast<unsigned>(config.endpoint),
                     config.transfer_size, config.transfer_count);
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=start_stream phase=return result=INVALID_ARGUMENT\n");
#endif
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    bool started_all = false;
    Error start_error = Error::USB_IO;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (stream_active_ || stream_operation_count_ != 0U) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=l3-owner op=start_stream phase=return result=BUSY active=%u operations=%zu\n",
                         stream_active_ ? 1U : 0U, stream_operation_count_);
#endif
            return Result<void>::failure(Error::BUSY);
        }
        stream_config_ = config;
        stream_active_ = true;
        stream_operation_count_ = 0U;
        stream_delivery_index_ = 0U;
        stream_buffer_.assign(config.transfer_size, 0U);
        for (std::size_t index = 0U; index < config.transfer_count; ++index) {
            const auto started = start_stream_operation();
            if (!started) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
                std::fprintf(stderr,
                             "PX4_ATTACH_DIAG L3-transfer-start claim=%u endpoint=0x%02x timeout=0 capacity=%zu slot=%zu error=%s\n",
                             static_cast<unsigned>(claim_index_),
                             static_cast<unsigned>(config.endpoint),
                             config.transfer_size, index,
                             error_string(started.error()));
#endif
                stream_active_ = false;
                start_error = started.error();
                break;
            }
            stream_operations_[stream_operation_count_++].op = started.value();
        }
        started_all = stream_active_;
    }
    if (!started_all) {
        cancel_stream_operations();
        (void)drain_stream_operations();
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=start_stream phase=return result=%s error=%s\n",
                     error_string(start_error), error_string(start_error));
#endif
        return Result<void>::failure(start_error);
    }
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=start_stream phase=return result=OK operations=%zu\n",
                 config.transfer_count);
#endif
    return Result<void>::success();
}

Result<StreamEvent> L3OwnerTransport::wait_stream(Timeout timeout) noexcept
{
    dtv_l3_op* op = nullptr;
    std::size_t operation_index = 0U;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (owner_ == nullptr || !stream_active_) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=NOT_READY owner=%u active=%u\n",
                         owner_ != nullptr ? 1U : 0U,
                         stream_active_ ? 1U : 0U);
#endif
            return Result<StreamEvent>::failure(Error::NOT_READY);
        }
        if (stream_operation_count_ == 0U) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=NOT_READY operations=0\n");
#endif
            return Result<StreamEvent>::failure(Error::NOT_READY);
        }
        operation_index = stream_delivery_index_ % stream_operation_count_;
        op = stream_operations_[operation_index].op;
    }
    if (op == nullptr) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=NOT_READY op-present=0 slot=%zu\n",
                     operation_index);
#endif
        return Result<StreamEvent>::failure(Error::NOT_READY);
    }

    dtv_l3_op_result op_result{};
    dtv_l3_result waited = dtv_l3_op_wait(
        op, timeout.milliseconds == 0U ? DTV_L3_WAIT_FOREVER
                                       : timeout.milliseconds);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=stream-op_wait slot=%zu wait=%u result=%u\n",
                 operation_index,
                 static_cast<unsigned>(timeout.milliseconds == 0U
                                           ? DTV_L3_WAIT_FOREVER
                                           : timeout.milliseconds),
                 static_cast<unsigned>(waited));
#endif
    if (waited == DTV_L3_OK) {
        waited = dtv_l3_op_poll(op, &op_result);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=stream-op_poll slot=%zu result=%u state=%u terminal=%u bytes=%zu\n",
                     operation_index, static_cast<unsigned>(waited),
                     static_cast<unsigned>(op_result.state),
                     static_cast<unsigned>(op_result.result), op_result.data_size);
#endif
    }
    if (waited == DTV_L3_WAIT_TIMEOUT) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=TIMEOUT slot=%zu\n",
                     operation_index);
#endif
        return Result<StreamEvent>::failure(Error::TIMEOUT);
    }
    if (waited != DTV_L3_OK ||
        (op_result.state != DTV_L3_OP_DONE &&
         op_result.state != DTV_L3_OP_FAILED)) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=%u slot=%zu state=%u\n",
                     static_cast<unsigned>(waited), operation_index,
                     static_cast<unsigned>(op_result.state));
#endif
        return Result<StreamEvent>::failure(from_l3_result(waited));
    }
    const dtv_l3_result terminal = op_result.result;
    const std::size_t size = op_result.data_size;
    const bool completed = op_result.state == DTV_L3_OP_DONE &&
                          terminal == DTV_L3_OK;
    const bool data_valid = size <= stream_config_.transfer_size &&
                            (size == 0U || op_result.data != nullptr);
    if (completed && !data_valid) {
        const dtv_l3_result released = dtv_l3_op_release(op);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=stream-op-release slot=%zu result=%u state=%u terminal=%u bytes=%zu data-valid=0\n",
                     operation_index, static_cast<unsigned>(released),
                     static_cast<unsigned>(op_result.state),
                     static_cast<unsigned>(terminal), size);
#endif
        std::lock_guard<std::mutex> lock(stream_mutex_);
        stream_active_ = false;
        if (released == DTV_L3_OK) {
            stream_operations_[operation_index].op = nullptr;
        }
        if (released != DTV_L3_OK) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=%u release=%u slot=%zu\n",
                         static_cast<unsigned>(from_l3_result(released)),
                         static_cast<unsigned>(released), operation_index);
#endif
            return Result<StreamEvent>::failure(from_l3_result(released));
        }
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=BUFFER_TOO_SMALL bytes=%zu capacity=%zu\n",
                     size, stream_config_.transfer_size);
#endif
        return Result<StreamEvent>::failure(Error::BUFFER_TOO_SMALL);
    }
    if (completed && size != 0U) {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        std::copy(op_result.data, op_result.data + size, stream_buffer_.begin());
    }
    const dtv_l3_result released = dtv_l3_op_release(op);
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=stream-op-release slot=%zu result=%u state=%u terminal=%u bytes=%zu data-valid=%u\n",
                 operation_index, static_cast<unsigned>(released),
                 static_cast<unsigned>(op_result.state),
                 static_cast<unsigned>(terminal), size, data_valid ? 1U : 0U);
#endif
    Result<dtv_l3_op*> replacement =
        Result<dtv_l3_op*>::failure(Error::NOT_READY);
    Error replacement_error = Error::OK;
    bool active_after_delivery = false;
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        if (released == DTV_L3_OK) {
            stream_operations_[operation_index].op = nullptr;
            ++stream_delivery_index_;
        } else {
            stream_active_ = false;
        }
        if (released == DTV_L3_OK && completed && stream_active_) {
            replacement = start_stream_operation();
            if (replacement) {
                stream_operations_[operation_index].op = replacement.value();
            } else {
                stream_active_ = false;
                replacement_error = replacement.error();
            }
        } else {
            stream_active_ = false;
        }
        active_after_delivery = stream_active_;
    }
    if (released != DTV_L3_OK) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=%u release=%u slot=%zu\n",
                     static_cast<unsigned>(from_l3_result(released)),
                     static_cast<unsigned>(released), operation_index);
#endif
        return Result<StreamEvent>::failure(from_l3_result(released));
    }
    if (!completed) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=%u terminal=%u slot=%zu\n",
                     static_cast<unsigned>(from_l3_result(terminal)),
                     static_cast<unsigned>(terminal), operation_index);
#endif
        return Result<StreamEvent>::failure(from_l3_result(terminal));
    }
    if (!active_after_delivery && replacement_error != Error::OK) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
        std::fprintf(stderr,
                     "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=%s replacement=error\n",
                     error_string(replacement_error));
#endif
        return Result<StreamEvent>::failure(replacement_error);
    }
    std::lock_guard<std::mutex> lock(stream_mutex_);
    const StreamEventKind kind =
        size < stream_config_.transfer_size ? StreamEventKind::short_transfer
                                            : StreamEventKind::data;
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=wait_stream phase=return result=OK slot=%zu bytes=%zu event=%u\n",
                 operation_index, size, static_cast<unsigned>(kind));
#endif
    return Result<StreamEvent>::success(
        StreamEvent{kind, stream_buffer_.data(), size});
}

Result<void> L3OwnerTransport::cancel_stream() noexcept
{
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        stream_active_ = false;
    }
    cancel_stream_operations();
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=cancel_stream phase=return result=OK\n");
#endif
    return Result<void>::success();
}

Result<void> L3OwnerTransport::stop_stream() noexcept
{
    {
        std::lock_guard<std::mutex> lock(stream_mutex_);
        stream_active_ = false;
    }
    cancel_stream_operations();
    const auto result = drain_stream_operations();
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=l3-owner op=stop_stream phase=return result=%s\n",
                 result ? "OK" : error_string(result.error()));
#endif
    return result;
}

}  // namespace px4::userland

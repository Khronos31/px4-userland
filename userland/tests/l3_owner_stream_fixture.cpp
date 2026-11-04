// SPDX-License-Identifier: GPL-2.0-only
// Offline L3 owner/provider stream fixture. USB operations are fake.

#include "l3_adapter.h"
#include "l3_owner_transport.h"
#include "l3_runtime_usb_port.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <new>
#include <thread>

namespace {

using namespace px4::userland;

class FakeStreamTransport final : public Transport {
public:
    Result<std::size_t> bulk_read(std::uint8_t, MutableByteView,
                                  Timeout,
                                  BulkReadObservation*) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }

    Result<std::size_t> bulk_write(std::uint8_t, ByteView,
                                   Timeout) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }

    Result<void> start_stream(const StreamConfig& config) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ || config.endpoint != kTsInEndpoint ||
            config.transfer_size != packets_.size() ||
            config.transfer_count != 6U) {
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        }
        active_ = true;
        ++start_calls;
        changed_.notify_all();
        return Result<void>::success();
    }

    Result<StreamEvent> wait_stream(Timeout) noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        ++wait_calls;
        wait_entered_ = true;
        changed_.notify_all();
        changed_.wait(lock, [this] { return !active_ || !block_wait_; });
        if (!active_) {
            return Result<StreamEvent>::failure(Error::NOT_READY);
        }
        if (block_wait_) {
            block_wait_ = false;
        }
        packets_[0U] = 0x47U;
        ++completed_calls;
        return Result<StreamEvent>::success(StreamEvent{
            StreamEventKind::short_transfer, packets_.data(), packets_.size()});
    }

    Result<void> cancel_stream() noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        ++cancel_calls;
        changed_.notify_all();
        return Result<void>::success();
    }

    Result<void> stop_stream() noexcept override
    {
        return cancel_stream();
    }

    bool stream_active() const noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

    void block_next_wait() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        block_wait_ = true;
        wait_entered_ = false;
    }

    bool wait_until_entered() noexcept
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(2), [this] {
            return wait_entered_;
        });
    }

    std::atomic<unsigned int> start_calls{0U};
    std::atomic<unsigned int> wait_calls{0U};
    std::atomic<unsigned int> completed_calls{0U};
    std::atomic<unsigned int> cancel_calls{0U};

private:
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::array<std::uint8_t, 188U> packets_{};
    bool active_ = false;
    bool block_wait_ = false;
    bool wait_entered_ = false;
};

class StreamProduct final : public L3Product {
public:
    dtv_l3_capabilities capabilities() const noexcept override
    {
        dtv_l3_capabilities out{};
        out.receiver_count = 1U;
        out.cancel_scope = DTV_L3_CANCEL_SCOPE_PIPE;
        return out;
    }

    dtv_l3_result bind(dtv_l3_context* owner) noexcept override
    {
        return owner == nullptr ? DTV_L3_INVALID : DTV_L3_OK;
    }

    dtv_l3_result initialize(dtv_l3_context* owner) noexcept override
    {
        if (owner == nullptr) {
            return DTV_L3_INVALID;
        }
        transport.reset(new (std::nothrow) L3OwnerTransport(owner, 0U));
        return transport == nullptr ? DTV_L3_FAILED : DTV_L3_OK;
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

    dtv_l3_result bridge_start(std::uint32_t, std::uint32_t) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_result bridge_stop(std::uint32_t) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_result stream_attach(std::uint64_t, dtv_l3_ts_deliver_fn,
                                void*) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_result stream_stop(std::uint64_t) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    std::unique_ptr<L3OwnerTransport> transport;
};

bool wait_open(dtv_l3_op* op, dtv_l3_op_result* result)
{
    if (op == nullptr || dtv_l3_op_wait(op, 2000U) != DTV_L3_OK ||
        dtv_l3_op_poll(op, result) != DTV_L3_OK) {
        return false;
    }
    const bool terminal = result->state == DTV_L3_OP_DONE ||
                         result->state == DTV_L3_OP_FAILED;
    return dtv_l3_op_release(op) == DTV_L3_OK && terminal &&
           result->result == DTV_L3_OK;
}

bool wait_releasable(dtv_l3_context* owner)
{
    dtv_l3_close_status status{};
    for (unsigned int attempt = 0U; attempt < 500U; ++attempt) {
        if (dtv_l3_close_status_get(owner, &status) != DTV_L3_OK) {
            return false;
        }
        if (status.releasable) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
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
    FakeStreamTransport dev1;
    FakeStreamTransport dev2;
    L3RuntimeUsbPort usb_port(dev1, dev2);
    StreamProduct product;
    L3ProductAdapter product_adapter(product);
    L3UsbProviderAdapter usb_adapter(usb_port);
    const dtv_l3_usb_provider usb = usb_adapter.provider();
    const dtv_l3_product_adapter adapter = product_adapter.adapter();
    const dtv_l3_open_result opened = dtv_l3_open_ex(&usb, &adapter, 2U);
    dtv_l3_op_result open_result{};
    const bool opened_ok = opened.result == DTV_L3_ACCEPTED &&
                           opened.owner != nullptr &&
                           wait_open(opened.op, &open_result);

    bool stream_started = false;
    bool stream_read = false;
    bool stream_stopped = false;
    bool cancel_started = false;
    bool cancel_drained = false;
    if (opened_ok && product.transport != nullptr) {
        stream_started = static_cast<bool>(product.transport->start_stream(
            StreamConfig{kTsInEndpoint, 188U, 6U}));
        const auto event = product.transport->wait_stream(Timeout{2000U});
        stream_read = event && event.value().size == 188U &&
                      event.value().data[0U] == 0x47U &&
                      usb_port.submit_calls == 7U;
        stream_stopped = static_cast<bool>(product.transport->stop_stream());

        dev1.block_next_wait();
        cancel_started = static_cast<bool>(product.transport->start_stream(
            StreamConfig{kTsInEndpoint, 188U, 6U}));
        std::thread waiter([&product] {
            if (product.transport != nullptr) {
                (void)product.transport->wait_stream(Timeout{2000U});
            }
        });
        const bool entered = dev1.wait_until_entered();
        const auto cancel_result = product.transport->cancel_stream();
        waiter.join();
        const auto stop_result = product.transport->stop_stream();
        cancel_drained = entered && cancel_result && stop_result &&
                         usb_port.completion_calls == usb_port.submit_calls &&
        dev1.cancel_calls.load() != 0U;
    }

    bool cleaned = false;
    if (opened.owner != nullptr) {
        (void)dtv_l3_close(opened.owner, 2000U);
        cleaned = wait_releasable(opened.owner) &&
                  usb_port.release_calls == 2U &&
                  release_context(opened.owner);
    }
    const bool ok = opened_ok && stream_started && stream_read &&
                    stream_stopped && cancel_started && cancel_drained &&
                    cleaned;
    std::printf(
        "{\"open\":%s,\"start\":%s,\"read\":%s,\"stop\":%s,"
        "\"cancel_start\":%s,\"cancel_drain\":%s,\"submits\":%u,"
        "\"completions\":%u,\"urb_starts\":%u,\"urb_waits\":%u,"
        "\"cleaned\":%s,\"ok\":%s}\n",
        opened_ok ? "true" : "false", stream_started ? "true" : "false",
        stream_read ? "true" : "false", stream_stopped ? "true" : "false",
        cancel_started ? "true" : "false",
        cancel_drained ? "true" : "false", usb_port.submit_calls.load(),
        usb_port.completion_calls.load(), dev1.start_calls.load(),
        dev1.wait_calls.load(),
        cleaned ? "true" : "false", ok ? "true" : "false");
    return ok ? 0 : 1;
}

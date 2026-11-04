// SPDX-License-Identifier: GPL-2.0-only
#include "l3_runtime_usb_port.h"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <new>

namespace px4::userland {

struct L3RuntimeUsbPort::Request final {
    dtv_l3_provider_binding* binding = nullptr;
    std::uint64_t transfer_id = 0U;
    std::uint8_t endpoint = 0U;
    std::uint32_t timeout_ms = 0U;
    std::size_t out_size = 0U;
    std::size_t in_capacity = 0U;
    dtv_l3_transfer_complete_fn complete = nullptr;
    void* callback_context = nullptr;
    std::unique_ptr<std::uint8_t[]> buffer;
    std::uint64_t sequence = 0U;
    bool cancelled = false;
};

struct L3RuntimeUsbPort::WorkerState final {
    std::mutex mutex;
    std::condition_variable changed;
    std::array<std::unique_ptr<Request>, 64U> pending{};
    Request* active = nullptr;
    std::uint64_t next_sequence = 0U;
    bool stopping = false;
    std::unique_ptr<std::thread> thread;
};

namespace {

constexpr std::uint32_t kStreamEventWaitMs = 50U;

}  // namespace

L3RuntimeUsbPort::L3RuntimeUsbPort(Q3U4Runtime& runtime) noexcept
    : L3RuntimeUsbPort(runtime.dev1(), runtime.dev2())
{
}

L3RuntimeUsbPort::L3RuntimeUsbPort(Transport& dev1, Transport& dev2) noexcept
    : transports_{{&dev1, &dev2}}
{
    for (std::uint32_t claim_index = 0U; claim_index < workers_.size();
         ++claim_index) {
        workers_[claim_index].reset(new (std::nothrow) WorkerState);
        if (!workers_[claim_index]) {
            continue;
        }
        workers_[claim_index]->thread.reset(new (std::nothrow) std::thread(
            [this, claim_index]() noexcept { run(claim_index); }));
    }
}

L3RuntimeUsbPort::~L3RuntimeUsbPort() noexcept
{
    for (std::size_t claim_index = 0U; claim_index < workers_.size();
         ++claim_index) {
        WorkerState* state = workers_[claim_index].get();
        if (state == nullptr) {
            continue;
        }
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->stopping = true;
            if (state->active != nullptr) {
                state->active->cancelled = true;
            }
            for (auto& request : state->pending) {
                if (request != nullptr) {
                    request->cancelled = true;
                }
            }
        }
        state->changed.notify_all();
    }
    for (Transport* transport : transports_) {
        if (transport != nullptr && transport->stream_active()) {
            (void)transport->cancel_stream();
        }
    }
    for (auto& state : workers_) {
        if (state != nullptr && state->thread != nullptr &&
            state->thread->joinable()) {
            state->thread->join();
        }
    }
    for (Transport* transport : transports_) {
        if (transport != nullptr && transport->stream_active()) {
            (void)transport->stop_stream();
        }
    }
}

dtv_l3_result L3RuntimeUsbPort::from_error(Error error) noexcept
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

dtv_l3_claim_result L3RuntimeUsbPort::claim(
    dtv_l3_provider_binding*, std::uint32_t claim_index) noexcept
{
    if (claim_index >= workers_.size() || workers_[claim_index] == nullptr ||
        workers_[claim_index]->thread == nullptr) {
        return {DTV_L3_INVALID, DTV_L3_CLAIM_NONE};
    }
    ++claim_calls;
    return {DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
}

dtv_l3_release_result L3RuntimeUsbPort::release_claim(
    dtv_l3_provider_binding*, std::uint32_t claim_index) noexcept
{
    if (claim_index >= workers_.size() || workers_[claim_index] == nullptr) {
        return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN, DTV_L3_CLAIM_UNKNOWN,
                DTV_L3_RESTORE_NOT_ATTEMPTED, DTV_L3_INVALID};
    }
    WorkerState& state = *workers_[claim_index];
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.active != nullptr) {
            return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN, DTV_L3_CLAIM_UNKNOWN,
                    DTV_L3_RESTORE_NOT_ATTEMPTED, DTV_L3_BUSY};
        }
        for (const auto& request : state.pending) {
            if (request != nullptr) {
                return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN,
                        DTV_L3_CLAIM_UNKNOWN,
                        DTV_L3_RESTORE_NOT_ATTEMPTED, DTV_L3_BUSY};
            }
        }
    }
    Transport* transport = transports_[claim_index];
    if (transport != nullptr && transport->stream_active()) {
        const auto cancelled = transport->cancel_stream();
        const auto stopped = transport->stop_stream();
        if (!cancelled || !stopped) {
            const Error error = !cancelled ? cancelled.error() : stopped.error();
            return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN, DTV_L3_CLAIM_UNKNOWN,
                    DTV_L3_RESTORE_NOT_ATTEMPTED, from_error(error)};
        }
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
    if (request == nullptr || complete == nullptr ||
        request->kind != DTV_L3_TRANSFER_BULK || request->claim_index >= 2U ||
        transports_[request->claim_index] == nullptr ||
        request->endpoint > 0xffU ||
        (request->out_size != 0U && request->out_data == nullptr) ||
        (request->out_size != 0U && request->in_capacity != 0U)) {
        return DTV_L3_UNSUPPORTED;
    }
    WorkerState* state = workers_[request->claim_index].get();
    if (state == nullptr || state->thread == nullptr) {
        return DTV_L3_FAILED;
    }
    std::unique_ptr<Request> pending(new (std::nothrow) Request);
    if (!pending) {
        return DTV_L3_FAILED;
    }
    const std::size_t buffer_size =
        request->out_size != 0U ? request->out_size : request->in_capacity;
    if (buffer_size != 0U) {
        pending->buffer.reset(new (std::nothrow) std::uint8_t[buffer_size]);
        if (!pending->buffer) {
            return DTV_L3_FAILED;
        }
        if (request->out_size != 0U) {
            std::copy(request->out_data, request->out_data + request->out_size,
                      pending->buffer.get());
        }
    }
    pending->binding = binding;
    pending->transfer_id = transfer_id;
    pending->endpoint = static_cast<std::uint8_t>(request->endpoint);
    pending->timeout_ms = request->timeout_ms;
    pending->out_size = request->out_size;
    pending->in_capacity = request->in_capacity;
    pending->complete = complete;
    pending->callback_context = callback_context;
    {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->stopping) {
            return DTV_L3_DISCONNECTED;
        }
        std::size_t free_slot = state->pending.size();
        for (std::size_t index = 0U; index < state->pending.size(); ++index) {
            if (state->pending[index] == nullptr) {
                free_slot = index;
                break;
            }
        }
        if (free_slot == state->pending.size()) {
            return DTV_L3_BUSY;
        }
        pending->sequence = state->next_sequence++;
        state->pending[free_slot] = std::move(pending);
    }
    state->changed.notify_one();
    ++submit_calls;
    return DTV_L3_ACCEPTED;
}

dtv_l3_result L3RuntimeUsbPort::cancel(
    dtv_l3_provider_binding*, std::uint64_t transfer_id) noexcept
{
    for (std::uint32_t claim_index = 0U; claim_index < workers_.size();
         ++claim_index) {
        WorkerState* state = workers_[claim_index].get();
        if (state == nullptr) {
            continue;
        }
        std::uint8_t endpoint = 0U;
        bool found = false;
        bool abort_stream = false;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->active != nullptr &&
                state->active->transfer_id == transfer_id) {
                endpoint = state->active->endpoint;
                found = true;
            }
            if (!found) {
                for (const auto& request : state->pending) {
                    if (request != nullptr && request->transfer_id == transfer_id) {
                        endpoint = request->endpoint;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                continue;
            }
            if (state->active != nullptr &&
                state->active->endpoint == endpoint) {
                state->active->cancelled = true;
                abort_stream = endpoint == kTsInEndpoint;
            }
            for (auto& request : state->pending) {
                if (request != nullptr && request->endpoint == endpoint) {
                    request->cancelled = true;
                }
            }
        }
        state->changed.notify_one();
        if (abort_stream && transports_[claim_index] != nullptr) {
            const auto cancelled = transports_[claim_index]->cancel_stream();
            if (!cancelled) {
                return from_error(cancelled.error());
            }
        }
        return DTV_L3_OK;
    }
    return DTV_L3_STALE;
}

dtv_l3_cancel_scope L3RuntimeUsbPort::cancel_scope() const noexcept
{
    return DTV_L3_CANCEL_SCOPE_PIPE;
}

void L3RuntimeUsbPort::run(std::uint32_t claim_index) noexcept
{
    WorkerState& state = *workers_[claim_index];
    for (;;) {
        std::unique_ptr<Request> request;
        {
            std::unique_lock<std::mutex> lock(state.mutex);
            state.changed.wait(lock, [&state] {
                if (state.stopping) {
                    return true;
                }
                for (const auto& pending : state.pending) {
                    if (pending != nullptr) {
                        return true;
                    }
                }
                return false;
            });
            std::size_t selected = state.pending.size();
            std::uint64_t sequence = UINT64_MAX;
            for (std::size_t index = 0U; index < state.pending.size(); ++index) {
                if (state.pending[index] != nullptr &&
                    state.pending[index]->sequence < sequence) {
                    selected = index;
                    sequence = state.pending[index]->sequence;
                }
            }
            if (selected == state.pending.size()) {
                if (state.stopping) {
                    return;
                }
                continue;
            }
            request = std::move(state.pending[selected]);
            state.active = request.get();
        }

        dtv_l3_result result = DTV_L3_FAILED;
        std::size_t transferred = 0U;
        const bool stream_request = request->endpoint == kTsInEndpoint &&
                                    request->out_size == 0U;
        bool cancelled = false;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            cancelled = request->cancelled;
        }
        Transport& transport = *transports_[claim_index];
        if (cancelled) {
            result = DTV_L3_CANCELLED;
        } else if (stream_request) {
            bool ready = true;
            if (!transport.stream_active()) {
                const auto started = transport.start_stream(StreamConfig{
                    request->endpoint, request->in_capacity, 6U});
                if (!started) {
                    result = from_error(started.error());
                    ready = false;
                }
            }
            while (ready) {
                {
                    std::lock_guard<std::mutex> lock(state.mutex);
                    cancelled = request->cancelled;
                }
                if (cancelled) {
                    (void)transport.cancel_stream();
                    result = DTV_L3_CANCELLED;
                    break;
                }
                if (result != DTV_L3_FAILED) {
                    break;
                }
                const auto event = transport.wait_stream(
                    Timeout{kStreamEventWaitMs});
                if (!event) {
                    if (event.error() == Error::TIMEOUT) {
                        continue;
                    }
                    std::lock_guard<std::mutex> lock(state.mutex);
                    result = request->cancelled ? DTV_L3_CANCELLED
                                                : from_error(event.error());
                    break;
                }
                transferred = event.value().size;
                if (transferred > request->in_capacity) {
                    result = DTV_L3_INVALID;
                    break;
                }
                if (transferred != 0U) {
                    std::copy(event.value().data,
                              event.value().data + transferred,
                              request->buffer.get());
                }
                result = DTV_L3_OK;
                break;
            }
        } else if (request->out_size != 0U) {
            const auto written = transport.bulk_write(
                request->endpoint,
                ByteView{request->buffer.get(), request->out_size},
                Timeout{request->timeout_ms});
            result = written ? DTV_L3_OK : from_error(written.error());
            if (written) {
                transferred = written.value();
                ++write_calls;
            }
        } else {
            const auto read = transport.bulk_read(
                request->endpoint,
                MutableByteView{request->buffer.get(), request->in_capacity},
                Timeout{request->timeout_ms});
            result = read ? DTV_L3_OK : from_error(read.error());
            if (read) {
                transferred = read.value();
                ++read_calls;
            }
        }

        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.active == request.get()) {
                state.active = nullptr;
            }
            if (request->cancelled && result != DTV_L3_OK) {
                result = DTV_L3_CANCELLED;
            }
        }
        if (result == DTV_L3_OK && stream_request) {
            ++read_calls;
            ++stream_read_calls;
        }
        const dtv_l3_transfer_completion completion{
            result,
            result == DTV_L3_OK && request->out_size == 0U
                ? request->buffer.get() : nullptr,
            result == DTV_L3_OK && request->out_size == 0U
                ? transferred : 0U,
            transferred,
            1U};
        request->complete(request->binding, request->transfer_id,
                          &completion, request->callback_context);
        ++completion_calls;
    }
}

}  // namespace px4::userland

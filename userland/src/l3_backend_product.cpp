// SPDX-License-Identifier: GPL-2.0-only
#include "l3_backend_product.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <thread>
#include <vector>

namespace px4::userland {

namespace {

constexpr std::size_t kMaxReceivers = 8U;
constexpr std::size_t kMaxBridges = 2U;
constexpr std::size_t kReadPackets = 4096U;
constexpr std::size_t kPacketSize = 188U;
constexpr std::uint32_t kLockPollMs = 10U;
constexpr std::uint32_t kTerrestrialSettleMs = 340U;

dtv_l3_result from_error(Error error) noexcept
{
    switch (error) {
    case Error::OK: return DTV_L3_OK;
    case Error::INVALID_ARGUMENT:
    case Error::BUFFER_TOO_SMALL: return DTV_L3_INVALID;
    case Error::NOT_FOUND: return DTV_L3_STALE;
    case Error::BUSY: return DTV_L3_BUSY;
    case Error::NOT_READY: return DTV_L3_FAILED;
    case Error::TIMEOUT: return DTV_L3_TIMEOUT;
    case Error::USB_IO: return DTV_L3_FAILED;
    case Error::DISCONNECTED: return DTV_L3_DISCONNECTED;
    case Error::UNSUPPORTED: return DTV_L3_UNSUPPORTED;
    case Error::RESOURCE_LIMIT:
    case Error::INTERNAL:
    case Error::VERSION_MISMATCH:
    case Error::PROTOCOL_ERROR:
    case Error::FIRMWARE_REJECTED:
    case Error::NO_CARD:
    case Error::CARD_REMOVED:
    case Error::SLOW_CONSUMER: return DTV_L3_FAILED;
    }
    return DTV_L3_FAILED;
}

}  // namespace

struct L3BackendProduct::Impl final {
    struct ReceiverRecord final {
        bool opened = false;
        bool tuned = false;
        ipc::System system = ipc::System::ISDB_T;
    };

    struct BridgeRecord final {
        bool running = false;
        bool capture_running = false;
        std::uint8_t receiver = 0U;
        ipc::System system = ipc::System::ISDB_T;
        std::uint64_t stream_generation = 0U;
    };

    struct StreamRecord final {
        std::uint64_t generation = 0U;
        std::uint8_t bridge = 0U;
        TunerAttachment attachment{};
        dtv_l3_ts_deliver_fn deliver = nullptr;
        void* callback_context = nullptr;
        std::vector<std::uint8_t> buffer;
        std::atomic<bool> stop{false};
        std::atomic<bool> failed{false};
        std::unique_ptr<std::thread> thread;
    };

    Impl(TunerServiceBackend& tuner_value,
         TunerStreamControl& stream_value,
         L3UsbPort& usb_value,
         const DeviceProfile& profile_value,
         bool has_card_reader) noexcept
        : tuner(tuner_value),
          stream(stream_value),
          usb(usb_value),
          profile(profile_value),
          card_reader(has_card_reader)
    {
    }

    bool valid_receiver(std::uint32_t receiver) const noexcept
    {
        return receiver < profile.receiver_count && receiver < kMaxReceivers;
    }

    bool valid_bridge(std::uint32_t bridge) const noexcept
    {
        return bridge < profile.bridge_count && bridge < kMaxBridges;
    }

    std::uint32_t bridge_for_receiver(std::uint32_t receiver) const noexcept
    {
        if (profile.bridge_count < 2U) {
            return 0U;
        }
        return receiver < 4U ? 0U : 1U;
    }

    dtv_l3_result select_system(std::uint32_t receiver,
                                ipc::System* system) const noexcept
    {
        if (receivers[receiver].tuned) {
            *system = receivers[receiver].system;
            return DTV_L3_OK;
        }
        const bool terrestrial =
            tuner.receiver_supports(static_cast<std::uint8_t>(receiver),
                                    ipc::System::ISDB_T);
        const bool satellite =
            tuner.receiver_supports(static_cast<std::uint8_t>(receiver),
                                    ipc::System::ISDB_S);
        if (terrestrial == satellite) {
            // Both systems means the candidate tune callback cannot carry the
            // requested system; neither means the receiver has no service.
            return DTV_L3_UNSUPPORTED;
        }
        *system = terrestrial ? ipc::System::ISDB_T : ipc::System::ISDB_S;
        return DTV_L3_OK;
    }

    dtv_l3_result open_if_needed(std::uint32_t receiver) noexcept
    {
        if (receivers[receiver].opened) {
            return DTV_L3_OK;
        }
        const auto opened =
            tuner.open_receiver(static_cast<std::uint8_t>(receiver));
        if (!opened) {
            return from_error(opened.error());
        }
        receivers[receiver].opened = true;
        return DTV_L3_OK;
    }

    bool lock_poll(std::uint8_t receiver, ipc::System system,
                   std::uint32_t timeout_ms) noexcept
    {
        const auto start = std::chrono::steady_clock::now();
        for (;;) {
            const auto locked = tuner.is_locked(receiver, system);
            if (!locked) {
                return false;
            }
            if (locked.value()) {
                return true;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start);
            if (elapsed.count() >= static_cast<long>(timeout_ms)) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kLockPollMs));
        }
    }

    void pump(StreamRecord& record) noexcept
    {
        for (;;) {
            if (record.stop.load(std::memory_order_acquire)) {
                return;
            }
            const auto read = stream.read(
                record.attachment,
                MutableByteView{record.buffer.data(), record.buffer.size()},
                Timeout{20U});
            if (!read) {
                record.failed.store(true, std::memory_order_release);
                if (record.deliver != nullptr) {
                    record.deliver(record.callback_context, nullptr, 0U);
                }
                return;
            }
            const TunerStreamReadResult& value = read.value();
            if (value.bytes != 0U && record.deliver != nullptr) {
                record.deliver(record.callback_context, record.buffer.data(),
                               value.bytes);
            }
            if (value.eof || value.terminal != TunerStreamTerminal::none) {
                if (value.terminal != TunerStreamTerminal::none) {
                    record.failed.store(true, std::memory_order_release);
                }
                if (record.deliver != nullptr) {
                    record.deliver(record.callback_context, nullptr, 0U);
                }
                return;
            }
        }
    }

    bool join_stream(StreamRecord& record) noexcept
    {
        record.stop.store(true, std::memory_order_release);
        if (record.thread && record.thread->joinable()) {
            if (record.thread->get_id() == std::this_thread::get_id()) {
                return false;
            }
            record.thread->join();
        }
        return true;
    }

    TunerServiceBackend& tuner;
    TunerStreamControl& stream;
    L3UsbPort& usb;
    DeviceProfile profile;
    bool card_reader;
    std::mutex mutex;
    ReceiverRecord receivers[kMaxReceivers]{};
    BridgeRecord bridges[kMaxBridges]{};
    std::map<std::uint64_t, std::unique_ptr<StreamRecord>> streams;
};

L3BackendProduct::L3BackendProduct(TunerServiceBackend& tuner,
                                   TunerStreamControl& stream,
                                   L3UsbPort& usb,
                                   const DeviceProfile& profile,
                                   bool has_card_reader) noexcept
    : impl_(new (std::nothrow) Impl(tuner, stream, usb, profile,
                                    has_card_reader))
{
}

L3BackendProduct::~L3BackendProduct() noexcept
{
    (void)close_all();
}

L3Capabilities L3BackendProduct::capabilities() const noexcept
{
    if (!impl_) {
        return L3Capabilities{0U, 0U, false, false, false};
    }
    return L3Capabilities{impl_->profile.bridge_count,
                          impl_->profile.receiver_count,
                          impl_->profile.dual_system,
                          impl_->profile.supports_lnb_15v,
                          impl_->card_reader};
}

dtv_l3_cancel_scope L3BackendProduct::cancel_scope() const noexcept
{
    return impl_ ? impl_->usb.cancel_scope() : DTV_L3_CANCEL_SCOPE_TRANSFER;
}

dtv_l3_claim_result L3BackendProduct::claim(
    dtv_l3_provider_binding* binding, std::uint32_t claim_index) noexcept
{
    return impl_ ? impl_->usb.claim(binding, claim_index)
                 : dtv_l3_claim_result{DTV_L3_FAILED, DTV_L3_CLAIM_UNKNOWN};
}

dtv_l3_release_result L3BackendProduct::release_claim(
    dtv_l3_provider_binding* binding, std::uint32_t claim_index) noexcept
{
    return impl_ ? impl_->usb.release_claim(binding, claim_index)
                 : dtv_l3_release_result{DTV_L3_RELEASE_QUIESCENCE_UNKNOWN,
                                         DTV_L3_CLAIM_UNKNOWN,
                                         DTV_L3_RESTORE_NOT_ATTEMPTED,
                                         DTV_L3_FAILED};
}

dtv_l3_result L3BackendProduct::submit(
    dtv_l3_provider_binding* binding, std::uint64_t transfer_id,
    const dtv_l3_transfer_request* request,
    dtv_l3_transfer_complete_fn complete,
    void* callback_context) noexcept
{
    return impl_ ? impl_->usb.submit(binding, transfer_id, request, complete,
                                     callback_context)
                 : DTV_L3_FAILED;
}

dtv_l3_result L3BackendProduct::cancel(
    dtv_l3_provider_binding* binding, std::uint64_t transfer_id) noexcept
{
    return impl_ ? impl_->usb.cancel(binding, transfer_id) : DTV_L3_FAILED;
}

dtv_l3_result L3BackendProduct::claim_cancel(
    dtv_l3_provider_binding* binding, std::uint32_t claim_index) noexcept
{
    return impl_ ? impl_->usb.claim_cancel(binding, claim_index)
                 : DTV_L3_FAILED;
}

dtv_l3_result L3BackendProduct::scope_query(
    std::uint32_t receiver_id, dtv_l3_scope_info* scope) noexcept
{
    if (!impl_ || scope == nullptr) {
        return DTV_L3_INVALID;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->valid_receiver(receiver_id)) {
        return DTV_L3_INVALID;
    }
    scope->bridge_id = impl_->bridge_for_receiver(receiver_id);
    scope->compatibility = DTV_L3_LEASE_SHAREABLE;
    return DTV_L3_OK;
}

dtv_l3_result L3BackendProduct::bridge_start(
    std::uint32_t bridge_id, std::uint32_t receiver_id) noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->valid_bridge(bridge_id) ||
        !impl_->valid_receiver(receiver_id)) {
        return DTV_L3_INVALID;
    }
    Impl::BridgeRecord& bridge = impl_->bridges[bridge_id];
    if (bridge.running) {
        return DTV_L3_BUSY;
    }
    if (impl_->bridge_for_receiver(receiver_id) != bridge_id) {
        return DTV_L3_INVALID;
    }
    ipc::System system = ipc::System::ISDB_T;
    const auto selected = impl_->select_system(receiver_id, &system);
    if (selected != DTV_L3_OK) {
        return selected;
    }
    const auto opened = impl_->open_if_needed(receiver_id);
    if (opened != DTV_L3_OK) {
        return opened;
    }
    const auto started = impl_->tuner.start_capture(
        static_cast<std::uint8_t>(receiver_id), system);
    if (!started) {
        return from_error(started.error());
    }
    bridge.running = true;
    bridge.capture_running = true;
    bridge.receiver = static_cast<std::uint8_t>(receiver_id);
    bridge.system = system;
    return DTV_L3_OK;
}

dtv_l3_result L3BackendProduct::bridge_stop(std::uint32_t bridge_id) noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->valid_bridge(bridge_id)) {
        return DTV_L3_INVALID;
    }
    Impl::BridgeRecord& bridge = impl_->bridges[bridge_id];
    if (bridge.stream_generation != 0U) {
        return DTV_L3_BUSY;
    }
    if (!bridge.running && !bridge.capture_running) {
        return DTV_L3_OK;
    }
    const std::uint8_t receiver = bridge.receiver;
    if (bridge.capture_running) {
        const auto stopped = impl_->tuner.stop_capture(receiver, bridge.system);
        if (!stopped) {
            return from_error(stopped.error());
        }
        bridge.capture_running = false;
    }
    const auto closed = impl_->tuner.close_receiver(receiver);
    if (!closed) {
        return from_error(closed.error());
    }
    impl_->receivers[receiver].opened = false;
    impl_->receivers[receiver].tuned = false;
    bridge.running = false;
    return DTV_L3_OK;
}

dtv_l3_result L3BackendProduct::stream_attach(
    std::uint64_t stream_id, dtv_l3_ts_deliver_fn deliver,
    void* callback_context) noexcept
{
    if (!impl_ || deliver == nullptr) {
        return DTV_L3_INVALID;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->streams.find(stream_id) != impl_->streams.end()) {
        return DTV_L3_BUSY;
    }
    std::uint32_t bridge_index = kMaxBridges;
    for (std::uint32_t index = 0U; index < impl_->profile.bridge_count;
         ++index) {
        if (impl_->bridges[index].running &&
            impl_->bridges[index].stream_generation == 0U) {
            bridge_index = index;
            break;
        }
    }
    if (bridge_index == kMaxBridges) {
        // The candidate stream callback carries no receiver.  A second stream
        // on an already-started bridge cannot be attributed to its receiver.
        return DTV_L3_UNSUPPORTED;
    }
    Impl::BridgeRecord& bridge = impl_->bridges[bridge_index];
    auto record = std::unique_ptr<Impl::StreamRecord>(
        new (std::nothrow) Impl::StreamRecord());
    if (!record) {
        return DTV_L3_FAILED;
    }
    record->generation = stream_id;
    record->bridge = static_cast<std::uint8_t>(bridge_index);
    record->attachment.owner_client_id = 1U;
    record->attachment.lease_id = stream_id;
    record->attachment.attachment_id = stream_id;
    record->attachment.receiver = bridge.receiver;
    record->attachment.system = bridge.system;
    record->attachment.evict_oldest = false;
    record->deliver = deliver;
    record->callback_context = callback_context;
    record->buffer.resize(kReadPackets * kPacketSize);
    const auto attached = impl_->stream.attach(record->attachment);
    if (!attached) {
        return from_error(attached.error());
    }
    Impl* impl = impl_.get();
    Impl::StreamRecord* raw = record.get();
    record->thread.reset(new (std::nothrow) std::thread(
        [impl, raw]() noexcept { impl->pump(*raw); }));
    if (!record->thread) {
        (void)impl_->stream.detach(record->attachment);
        return DTV_L3_FAILED;
    }
    bridge.stream_generation = stream_id;
    impl_->streams.emplace(stream_id, std::move(record));
    return DTV_L3_OK;
}

dtv_l3_result L3BackendProduct::stream_stop(
    std::uint64_t stream_id) noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    std::unique_ptr<Impl::StreamRecord> record;
    std::uint32_t bridge_index = kMaxBridges;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto found = impl_->streams.find(stream_id);
        if (found == impl_->streams.end()) {
            return DTV_L3_STALE;
        }
        record = std::move(found->second);
        impl_->streams.erase(found);
        bridge_index = record->bridge;
        impl_->bridges[bridge_index].stream_generation = 0U;
    }
    if (!impl_->join_stream(*record)) {
        // A provider callback stopped its own stream.  The pump still owns the
        // record, so preserve it rather than freeing storage under the thread.
        (void)record.release();
        return DTV_L3_FAILED;
    }
    const auto detached = impl_->stream.detach(record->attachment);
    if (!detached) {
        return from_error(detached.error());
    }
    return record->failed.load(std::memory_order_acquire) ? DTV_L3_FAILED
                                                          : DTV_L3_OK;
}

dtv_l3_result L3BackendProduct::tune(std::uint32_t receiver_id,
                                     std::uint64_t requested_hz,
                                     std::uint64_t* programmed_hz) noexcept
{
    if (!impl_ || programmed_hz == nullptr) {
        return DTV_L3_INVALID;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->valid_receiver(receiver_id)) {
        return DTV_L3_INVALID;
    }
    ipc::System system = ipc::System::ISDB_T;
    const auto selected = impl_->select_system(receiver_id, &system);
    if (selected != DTV_L3_OK) {
        return selected;
    }
    const auto opened = impl_->open_if_needed(receiver_id);
    if (opened != DTV_L3_OK) {
        return opened;
    }
    const std::uint8_t receiver = static_cast<std::uint8_t>(receiver_id);
    const auto prepared = impl_->tuner.begin_tune_power(receiver, system, 0U);
    if (!prepared) {
        return from_error(prepared.error());
    }
    const auto tuned = system == ipc::System::ISDB_T
        ? impl_->tuner.tune_terrestrial_hz(receiver, requested_hz,
                                           kTuneTimeoutMs)
        : impl_->tuner.tune_satellite_hz(receiver, requested_hz,
                                         kTuneTimeoutMs);
    if (!tuned) {
        (void)impl_->tuner.rollback_tune_power(receiver);
        return from_error(tuned.error());
    }
    if (!impl_->lock_poll(receiver, system, kTuneTimeoutMs)) {
        (void)impl_->tuner.rollback_tune_power(receiver);
        return DTV_L3_TIMEOUT;
    }
    if (system == ipc::System::ISDB_T &&
        impl_->tuner.requires_terrestrial_lock_settle()) {
        std::this_thread::sleep_for(
            std::chrono::milliseconds(kTerrestrialSettleMs));
    }
    const auto committed = impl_->tuner.commit_tune_power(receiver);
    if (!committed) {
        (void)impl_->tuner.rollback_tune_power(receiver);
        return from_error(committed.error());
    }
    impl_->receivers[receiver_id].tuned = true;
    impl_->receivers[receiver_id].system = system;
    *programmed_hz = tuned.value().programmed_hz.value_or(requested_hz);
    return DTV_L3_OK;
}

dtv_l3_result L3BackendProduct::close_all() noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    dtv_l3_result first = DTV_L3_OK;
    std::vector<std::uint64_t> generations;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (const auto& entry : impl_->streams) {
            generations.push_back(entry.first);
        }
    }
    for (std::uint64_t generation : generations) {
        const dtv_l3_result result = stream_stop(generation);
        if (first == DTV_L3_OK && result != DTV_L3_OK) {
            first = result;
        }
    }
    for (std::uint32_t bridge = 0U; bridge < impl_->profile.bridge_count;
         ++bridge) {
        const dtv_l3_result result = bridge_stop(bridge);
        if (first == DTV_L3_OK && result != DTV_L3_OK) {
            first = result;
        }
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        for (std::uint32_t receiver = 0U; receiver < impl_->profile.receiver_count;
             ++receiver) {
            if (!impl_->receivers[receiver].opened) {
                continue;
            }
            const auto closed = impl_->tuner.close_receiver(
                static_cast<std::uint8_t>(receiver));
            if (closed) {
                impl_->receivers[receiver].opened = false;
                impl_->receivers[receiver].tuned = false;
            } else if (first == DTV_L3_OK) {
                first = from_error(closed.error());
            }
        }
    }
    const auto stopped = impl_->tuner.shutdown();
    if (!stopped && first == DTV_L3_OK) {
        first = from_error(stopped.error());
    }
    return first;
}

}  // namespace px4::userland

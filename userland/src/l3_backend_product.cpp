// SPDX-License-Identifier: GPL-2.0-only
#include "l3_backend_product.h"

#include "l3_lnb_port.h"

#include <array>
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
        std::array<bool, kMaxReceivers> capture_running{};
        std::array<ipc::System, kMaxReceivers> capture_system{};
    };

    struct StreamRecord final {
        std::uint64_t generation = 0U;
        std::uint8_t bridge = 0U;
        std::uint8_t receiver = 0U;
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
         const DeviceProfile& profile_value,
         bool has_card_reader,
         dtv_l3_cancel_scope cancel_scope_value,
         bool allow_lnb_15v,
         L3OwnerLifecycle* lifecycle_value) noexcept
        : tuner(tuner_value),
          stream(stream_value),
          profile(profile_value),
          card_reader(has_card_reader),
          cancel_scope(cancel_scope_value),
          lifecycle(lifecycle_value),
          lnb(tuner_value, profile_value.supports_lnb_15v, allow_lnb_15v)
    {
        for (std::uint32_t receiver = 0U; receiver < profile.receiver_count &&
             receiver < kMaxReceivers; ++receiver) {
            const std::uint8_t id = static_cast<std::uint8_t>(receiver);
            receiver_caps[receiver].id = receiver;
            receiver_caps[receiver].systems =
                (tuner.receiver_supports(id, ipc::System::ISDB_T)
                     ? DTV_L3_SYSTEM_ISDB_T : 0U) |
                (tuner.receiver_supports(id, ipc::System::ISDB_S)
                     ? DTV_L3_SYSTEM_ISDB_S : 0U);
            receiver_caps[receiver].pid_filter = DTV_L3_PID_FILTER_SOFTWARE;
            receiver_caps[receiver].support = DTV_L3_SUPPORT_SUPPORTED;
        }
        reader_caps[0].id = 0U;
        reader_caps[0].external_apdu = DTV_L3_SUPPORT_UNSUPPORTED;
        reader_caps[0].support = card_reader ? DTV_L3_SUPPORT_UNSUPPORTED
                                             : DTV_L3_SUPPORT_UNSUPPORTED;
        lnb_caps[0].id = 0U;
        lnb_caps[0].support = profile.supports_lnb_15v
                                  ? DTV_L3_SUPPORT_SUPPORTED
                                  : DTV_L3_SUPPORT_UNSUPPORTED;
        lnb_caps[0].receivers_mask = 0U;
        for (std::uint32_t receiver = 0U;
             receiver < profile.receiver_count && receiver < kMaxReceivers;
             ++receiver) {
            if (tuner.receiver_supports(static_cast<std::uint8_t>(receiver),
                                        ipc::System::ISDB_S)) {
                lnb_caps[0].receivers_mask |= (1U << receiver);
            }
        }
        lnb_caps[0].commands = DTV_L3_LNB_COMMAND_OFF |
                               DTV_L3_LNB_COMMAND_ON_15V;
        lnb_caps[0].readback = DTV_L3_SUPPORT_UNKNOWN;
        for (std::uint32_t bridge = 0U; bridge < profile.bridge_count &&
             bridge < kMaxBridges; ++bridge) {
            shared_caps[bridge].id = bridge;
            shared_caps[bridge].compatibility = DTV_L3_LEASE_SHAREABLE;
            shared_caps[bridge].support = DTV_L3_SUPPORT_SUPPORTED;
        }
        catalog.receivers = receiver_caps.data();
        catalog.receiver_count = profile.receiver_count < kMaxReceivers
                                     ? profile.receiver_count
                                     : kMaxReceivers;
        catalog.readers = reader_caps.data();
        catalog.reader_count = card_reader ? 1U : 0U;
        catalog.lnb_scopes = lnb_caps.data();
        catalog.lnb_scope_count =
            profile.supports_lnb_15v ? 1U : 0U;
        catalog.shared_scopes = shared_caps.data();
        catalog.shared_scope_count = profile.bridge_count < kMaxBridges
                                         ? profile.bridge_count
                                         : kMaxBridges;
        catalog.ts_form = DTV_L3_SUPPORT_SUPPORTED;
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

    dtv_l3_result select_satellite_locked(
        std::uint8_t receiver, std::uint32_t selection,
        std::uint32_t slot, std::uint32_t tsid) noexcept
    {
        Result<void> selected = Result<void>::success();
        if (selection == DTV_L3_TUNE_SELECTION_SLOT) {
            if (slot > 0xffU) {
                return DTV_L3_INVALID;
            }
            selected = tuner.select_satellite_slot(
                receiver, static_cast<std::uint8_t>(slot), kTuneTimeoutMs);
        } else if (selection == DTV_L3_TUNE_SELECTION_TSID) {
            if (tsid > 0xffffU) {
                return DTV_L3_INVALID;
            }
            selected = tuner.select_satellite_tsid(
                receiver, static_cast<std::uint16_t>(tsid), kTuneTimeoutMs);
        } else if (selection != DTV_L3_TUNE_SELECTION_AUTO) {
            return DTV_L3_INVALID;
        }
        return selected ? DTV_L3_OK : from_error(selected.error());
    }

    dtv_l3_result tune_locked(std::uint32_t receiver_id,
                              ipc::System system,
                              std::uint32_t selection,
                              std::uint32_t slot,
                              std::uint32_t tsid,
                              std::uint64_t requested_hz,
                              std::uint64_t* programmed_hz) noexcept
    {
        const std::uint8_t receiver =
            static_cast<std::uint8_t>(receiver_id);
        if (!tuner.receiver_supports(receiver, system)) {
            return DTV_L3_UNSUPPORTED;
        }
        if (system == ipc::System::ISDB_T &&
            selection != DTV_L3_TUNE_SELECTION_AUTO) {
            return DTV_L3_INVALID;
        }
        if (system == ipc::System::ISDB_S &&
            selection != DTV_L3_TUNE_SELECTION_AUTO &&
            selection != DTV_L3_TUNE_SELECTION_SLOT &&
            selection != DTV_L3_TUNE_SELECTION_TSID) {
            return DTV_L3_INVALID;
        }
        const auto opened = open_if_needed(receiver_id);
        if (opened != DTV_L3_OK) {
            return opened;
        }
        const auto prepared = tuner.begin_tune_power(receiver, system, 0U);
        if (!prepared) {
            return from_error(prepared.error());
        }
        const bool select_before =
            system == ipc::System::ISDB_S && selection !=
                    DTV_L3_TUNE_SELECTION_AUTO &&
            tuner.selects_satellite_stream_before_tune();
        if (select_before) {
            const dtv_l3_result selected =
                select_satellite_locked(receiver, selection, slot, tsid);
            if (selected != DTV_L3_OK) {
                (void)tuner.rollback_tune_power(receiver);
                return selected;
            }
        }
        const auto tuned = system == ipc::System::ISDB_T
            ? tuner.tune_terrestrial_hz(receiver, requested_hz,
                                        kTuneTimeoutMs)
            : tuner.tune_satellite_hz(receiver, requested_hz, kTuneTimeoutMs);
        if (!tuned) {
            (void)tuner.rollback_tune_power(receiver);
            return from_error(tuned.error());
        }
        if (!lock_poll(receiver, system, kTuneTimeoutMs)) {
            (void)tuner.rollback_tune_power(receiver);
            return DTV_L3_TIMEOUT;
        }
        if (system == ipc::System::ISDB_S && !select_before &&
            selection != DTV_L3_TUNE_SELECTION_AUTO) {
            const dtv_l3_result selected =
                select_satellite_locked(receiver, selection, slot, tsid);
            if (selected != DTV_L3_OK) {
                (void)tuner.rollback_tune_power(receiver);
                return selected;
            }
        }
        if (system == ipc::System::ISDB_T &&
            tuner.requires_terrestrial_lock_settle()) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(kTerrestrialSettleMs));
        }
        const auto committed = tuner.commit_tune_power(receiver);
        if (!committed) {
            (void)tuner.rollback_tune_power(receiver);
            return from_error(committed.error());
        }
        receivers[receiver_id].tuned = true;
        receivers[receiver_id].system = system;
        *programmed_hz = tuned.value().programmed_hz.value_or(requested_hz);
        return DTV_L3_OK;
    }

    dtv_l3_result attach_stream_locked(
        std::uint64_t stream_id, std::uint32_t receiver_id,
        std::uint32_t bridge_id, dtv_l3_ts_deliver_fn deliver,
        void* callback_context) noexcept
    {
        if (streams.find(stream_id) != streams.end()) {
            return DTV_L3_BUSY;
        }
        if (!valid_receiver(receiver_id) || !valid_bridge(bridge_id) ||
            bridge_for_receiver(receiver_id) != bridge_id) {
            return DTV_L3_INVALID;
        }
        BridgeRecord& bridge = bridges[bridge_id];
        if (!bridge.running) {
            return DTV_L3_BUSY;
        }
        for (const auto& entry : streams) {
            if (entry.second->receiver == receiver_id) {
                return DTV_L3_BUSY;
            }
        }
        const std::uint8_t receiver =
            static_cast<std::uint8_t>(receiver_id);
        ipc::System system = ipc::System::ISDB_T;
        if (bridge.capture_running[receiver]) {
            system = bridge.capture_system[receiver];
        } else {
            const auto selected = select_system(receiver_id, &system);
            if (selected != DTV_L3_OK) {
                return selected;
            }
            const auto opened = open_if_needed(receiver_id);
            if (opened != DTV_L3_OK) {
                return opened;
            }
            const auto started = tuner.start_capture(receiver, system);
            if (!started) {
                return from_error(started.error());
            }
            bridge.capture_running[receiver] = true;
            bridge.capture_system[receiver] = system;
        }
        auto record = std::unique_ptr<StreamRecord>(
            new (std::nothrow) StreamRecord());
        if (!record) {
            return DTV_L3_FAILED;
        }
        record->generation = stream_id;
        record->bridge = static_cast<std::uint8_t>(bridge_id);
        record->receiver = receiver;
        record->attachment.owner_client_id = 1U;
        record->attachment.lease_id = stream_id;
        record->attachment.attachment_id = stream_id;
        record->attachment.receiver = receiver;
        record->attachment.system = system;
        record->attachment.evict_oldest = false;
        record->deliver = deliver;
        record->callback_context = callback_context;
        record->buffer.resize(kReadPackets * kPacketSize);
        const auto attached = stream.attach(record->attachment);
        if (!attached) {
            return from_error(attached.error());
        }
        StreamRecord* raw = record.get();
        record->thread.reset(new (std::nothrow) std::thread(
            [this, raw]() noexcept { pump(*raw); }));
        if (!record->thread) {
            (void)stream.detach(record->attachment);
            return DTV_L3_FAILED;
        }
        streams.emplace(stream_id, std::move(record));
        return DTV_L3_OK;
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
    DeviceProfile profile;
    bool card_reader;
    dtv_l3_cancel_scope cancel_scope;
    L3OwnerLifecycle* lifecycle;
    L3LnbPort lnb;
    std::array<dtv_l3_receiver_capability, kMaxReceivers> receiver_caps{};
    std::array<dtv_l3_reader_capability, 1> reader_caps{};
    std::array<dtv_l3_lnb_scope_capability, 1> lnb_caps{};
    std::array<dtv_l3_shared_scope_capability, kMaxBridges> shared_caps{};
    dtv_l3_capability_catalog catalog{};
    std::mutex mutex;
    ReceiverRecord receivers[kMaxReceivers]{};
    BridgeRecord bridges[kMaxBridges]{};
    std::map<std::uint64_t, std::unique_ptr<StreamRecord>> streams;
};

L3BackendProduct::L3BackendProduct(TunerServiceBackend& tuner,
                                   TunerStreamControl& stream,
                                   const DeviceProfile& profile,
                                   bool has_card_reader,
                                   dtv_l3_cancel_scope cancel_scope,
                                   bool allow_lnb_15v,
                                   L3OwnerLifecycle* lifecycle) noexcept
    : impl_(new (std::nothrow) Impl(tuner, stream, profile, has_card_reader,
                                    cancel_scope, allow_lnb_15v, lifecycle))
{
}

L3BackendProduct::~L3BackendProduct() noexcept
{
    (void)close_all();
}

dtv_l3_capabilities L3BackendProduct::capabilities() const noexcept
{
    dtv_l3_capabilities capabilities{};
    if (!impl_) {
        return capabilities;
    }
    capabilities.receiver_count = impl_->profile.receiver_count;
    capabilities.reader_count = impl_->card_reader ? 1U : 0U;
    capabilities.lnb_power = impl_->profile.supports_lnb_15v ? 1U : 0U;
    capabilities.serial = 0U;
    capabilities.cancel_scope = impl_->cancel_scope;
    return capabilities;
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

const dtv_l3_capability_catalog* L3BackendProduct::catalog() const noexcept
{
    return impl_ == nullptr ? nullptr : &impl_->catalog;
}

dtv_l3_result L3BackendProduct::bind(dtv_l3_context* owner) noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    return impl_->lifecycle == nullptr ? DTV_L3_OK
                                       : impl_->lifecycle->bind(owner);
}

dtv_l3_result L3BackendProduct::initialize(dtv_l3_context* owner) noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    return impl_->lifecycle == nullptr ? DTV_L3_OK
                                       : impl_->lifecycle->initialize(owner);
}

dtv_l3_result L3BackendProduct::bridge_start(
    std::uint32_t bridge_id, std::uint32_t receiver_id) noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->valid_bridge(bridge_id) ||
        !impl_->valid_receiver(receiver_id) ||
        impl_->bridge_for_receiver(receiver_id) != bridge_id) {
        return DTV_L3_INVALID;
    }
    Impl::BridgeRecord& bridge = impl_->bridges[bridge_id];
    const std::uint8_t receiver = static_cast<std::uint8_t>(receiver_id);
    bridge.running = true;
    if (bridge.capture_running[receiver]) {
        return DTV_L3_OK;
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
    const auto started = impl_->tuner.start_capture(receiver, system);
    if (!started) {
        return from_error(started.error());
    }
    bridge.capture_running[receiver] = true;
    bridge.capture_system[receiver] = system;
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
    for (const auto& entry : impl_->streams) {
        if (entry.second->bridge == bridge_id) {
            return DTV_L3_BUSY;
        }
    }
    Impl::BridgeRecord& bridge = impl_->bridges[bridge_id];
    bool any_capture = false;
    for (std::size_t receiver = 0U; receiver < kMaxReceivers; ++receiver) {
        if (bridge.capture_running[receiver]) {
            any_capture = true;
            break;
        }
    }
    if (!bridge.running && !any_capture) {
        return DTV_L3_OK;
    }
    for (std::size_t receiver = 0U; receiver < kMaxReceivers; ++receiver) {
        if (!bridge.capture_running[receiver]) {
            continue;
        }
        const std::uint8_t id = static_cast<std::uint8_t>(receiver);
        const auto stopped =
            impl_->tuner.stop_capture(id, bridge.capture_system[receiver]);
        if (!stopped) {
            return from_error(stopped.error());
        }
        bridge.capture_running[receiver] = false;
        const auto closed = impl_->tuner.close_receiver(id);
        if (!closed) {
            return from_error(closed.error());
        }
        impl_->receivers[receiver].opened = false;
        impl_->receivers[receiver].tuned = false;
    }
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
    for (std::uint32_t bridge = 0U; bridge < impl_->profile.bridge_count;
         ++bridge) {
        if (!impl_->bridges[bridge].running) {
            continue;
        }
        for (std::uint32_t receiver = 0U;
             receiver < impl_->profile.receiver_count; ++receiver) {
            if (!impl_->bridges[bridge].capture_running[receiver]) {
                continue;
            }
            bool used = false;
            for (const auto& entry : impl_->streams) {
                if (entry.second->receiver == receiver) {
                    used = true;
                    break;
                }
            }
            if (!used) {
                return impl_->attach_stream_locked(
                    stream_id, receiver, bridge, deliver, callback_context);
            }
        }
    }
    return DTV_L3_UNSUPPORTED;
}

dtv_l3_result L3BackendProduct::stream_attach_ex(
    std::uint64_t stream_id, std::uint32_t receiver_id,
    std::uint32_t bridge_id, dtv_l3_ts_deliver_fn deliver,
    void* callback_context) noexcept
{
    if (!impl_ || deliver == nullptr) {
        return DTV_L3_INVALID;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->attach_stream_locked(stream_id, receiver_id, bridge_id,
                                       deliver, callback_context);
}

dtv_l3_result L3BackendProduct::stream_stop(
    std::uint64_t stream_id) noexcept
{
    return stop_stream(stream_id, 0U, 0U, false);
}

dtv_l3_result L3BackendProduct::stream_stop_ex(
    std::uint64_t stream_id, std::uint32_t receiver_id,
    std::uint32_t bridge_id) noexcept
{
    return stop_stream(stream_id, receiver_id, bridge_id, true);
}

dtv_l3_result L3BackendProduct::stop_stream(
    std::uint64_t stream_id, std::uint32_t receiver_id,
    std::uint32_t bridge_id, bool check_route) noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    std::unique_ptr<Impl::StreamRecord> record;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        const auto found = impl_->streams.find(stream_id);
        if (found == impl_->streams.end()) {
            return DTV_L3_STALE;
        }
        if (check_route &&
            (found->second->receiver != receiver_id ||
             found->second->bridge != bridge_id)) {
            return DTV_L3_INVALID;
        }
        record = std::move(found->second);
        impl_->streams.erase(found);
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

dtv_l3_result L3BackendProduct::lnb_status(
    std::uint32_t scope_id, dtv_l3_lnb_state* out) noexcept
{
    return impl_ == nullptr ? DTV_L3_FAILED
                            : impl_->lnb.status(scope_id, out);
}

dtv_l3_result L3BackendProduct::lnb_set(
    std::uint32_t scope_id, dtv_l3_lnb_command command,
    std::uint64_t deadline_ns, dtv_l3_lnb_state* out) noexcept
{
    return impl_ == nullptr ? DTV_L3_FAILED
                            : impl_->lnb.set(scope_id, command, deadline_ns,
                                             out);
}

dtv_l3_result L3BackendProduct::lnb_restore(
    std::uint32_t scope_id, dtv_l3_restore_result* out) noexcept
{
    return impl_ == nullptr ? DTV_L3_FAILED
                            : impl_->lnb.restore(scope_id, out);
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
    return impl_->tune_locked(receiver_id, system,
                              DTV_L3_TUNE_SELECTION_AUTO, 0U, 0U,
                              requested_hz, programmed_hz);
}

dtv_l3_result L3BackendProduct::tune_ex(
    std::uint32_t receiver_id, const dtv_l3_tune_request* request,
    std::uint64_t* programmed_hz) noexcept
{
    if (!impl_ || request == nullptr || programmed_hz == nullptr) {
        return DTV_L3_INVALID;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->valid_receiver(receiver_id)) {
        return DTV_L3_INVALID;
    }
    ipc::System system = ipc::System::ISDB_T;
    switch (request->system) {
    case DTV_L3_TUNE_SYSTEM_AUTO: {
        const auto selected = impl_->select_system(receiver_id, &system);
        if (selected != DTV_L3_OK) {
            return selected;
        }
        break;
    }
    case DTV_L3_TUNE_SYSTEM_ISDB_T:
        system = ipc::System::ISDB_T;
        break;
    case DTV_L3_TUNE_SYSTEM_ISDB_S:
        system = ipc::System::ISDB_S;
        break;
    default:
        return DTV_L3_UNSUPPORTED;
    }
    return impl_->tune_locked(receiver_id, system, request->selection,
                              request->slot, request->tsid,
                              request->requested_hz, programmed_hz);
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

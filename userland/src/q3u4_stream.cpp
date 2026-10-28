// SPDX-License-Identifier: GPL-2.0-only
#include "px4/q3u4_stream.h"
#include "px4/ts_continuity.h"
#include "dtv/userland/detail/diagnostic_log.h"

#include "tagged_ts_demux.h"
#include "mlt5pe_frontend.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <thread>

namespace px4::userland {

namespace {

constexpr std::size_t kBridgeCount = 2U;
constexpr std::size_t kPidCount = 8192U;

// Enclosure stream shape: Q3U4 has two bridges with four fixed-system tags;
// the MLT5 family has one bridge with five dual-system tags.
struct StreamLayout final {
    std::size_t bridge_count;
    std::size_t receivers_per_bridge;
    bool dual_system;
    std::uint8_t tag_count;
    std::array<std::int8_t, 5U> tag_to_receiver;
    bool plain_ts = false;
};

constexpr StreamLayout kQ3U4Layout{2U, 4U, false, 4U, {{0, 1, 2, 3, -1}}};
constexpr StreamLayout kW3U4Layout{1U, 4U, false, 4U, {{0, 1, 2, 3, -1}}};
constexpr StreamLayout kMlt5PeLayout{1U, 5U, true, 5U, {{0, 1, 2, 3, 4}}};

constexpr std::uint64_t kDiagnosticSampleIntervalUs = 10000000U;

#if defined(DTV_USERLAND_DIAGNOSTICS) || defined(PX4_Q3U4_STREAM_TEST_ACCESS)
bool diagnostic_sample_due(std::uint64_t now_us,
                           std::uint64_t last_sample_us) noexcept
{
    return now_us >= last_sample_us &&
           now_us - last_sample_us >= kDiagnosticSampleIntervalUs;
}
#endif

struct DiagnosticSample final {
    std::uint8_t bridge = 0U;
    std::uint8_t receiver = 0U;
    StreamTerminal terminal = StreamTerminal::none;
    std::size_t ring_count = 0U;
    std::size_t ring_capacity = 0U;
    std::uint64_t packets = 0U;
    std::uint64_t bytes = 0U;
    std::uint64_t tei_packets = 0U;
    std::uint64_t wire_tei_packets = 0U;
    std::uint64_t mpeg_tei_packets = 0U;
    std::uint64_t continuity_errors = 0U;
    std::uint64_t queue_drops = 0U;
    std::uint64_t usb_errors = 0U;
    std::uint64_t empty_intervals = 0U;
    std::size_t demux_invalid_tags = 0U;
    std::size_t demux_sync_losses = 0U;
    std::size_t demux_input_bytes = 0U;
    std::size_t demux_emitted_packets = 0U;
    std::size_t demux_buffered_bytes = 0U;
};

StreamLayout mlt_stream_layout(DeviceModel model) noexcept
{
    if (model == DeviceModel::px_mlt5pe || model == DeviceModel::dtv02a_5ts_p)
        return kMlt5PeLayout;
    if (model == DeviceModel::px_m1ur || model == DeviceModel::px_s1ur ||
        model == DeviceModel::dtv03a_1tu || model == DeviceModel::dtv02_1t1s_u ||
        model == DeviceModel::dtv02a_1t1s_u)
        return StreamLayout{1U, 1U, device_profile(model).dual_system, 1U,
                            {{0, -1, -1, -1, -1}}, true};
    const MltModelLayout profile = mlt_model_layout(model);
    StreamLayout layout{1U, profile.receiver_count, true, 0U, {{-1, -1, -1, -1, -1}}};
    for (std::size_t receiver = 0U; receiver < profile.receiver_count; ++receiver) {
        const std::uint8_t tag = static_cast<std::uint8_t>(profile.receivers[receiver].port_number + 1U);
        if (tag > 5U) continue;
        layout.tag_to_receiver[tag - 1U] = static_cast<std::int8_t>(receiver);
        if (layout.tag_count < tag) layout.tag_count = tag;
    }
    return layout;
}

// The demodulator can assert lock before its TS output and the IT930x packet
// sync buffer have reached a clean epoch.  Hardware A/B testing with the
// reference kernel driver observed the last startup continuity disturbance at
// packet 10793.  Hold an entire receiver stream private until it has crossed a
// larger observation window and has also supplied a clean tail after the last
// attributable fault.  Once promoted or the startup budget is exhausted, this
// gate is permanently disabled for the attachment epoch: every later TEI,
// continuity gap, and sync loss remains visible through the normal counters.
struct StartupStabilizationPolicy final {
    std::size_t minimum_packets = 16384U;
    std::size_t clean_packets = 4096U;
    std::size_t maximum_packets = 131072U;

    bool enabled() const noexcept
    {
        return minimum_packets != 0U || clean_packets != 0U ||
               maximum_packets != 0U;
    }
};

enum class ContinuityObservation : std::uint8_t {
    boundary,
    clean,
    fault,
};

// IT930x session-boundary publication policy, deliberately outside the shared
// packet checker. A duplicate is not the consecutive transition needed here.
struct StartupContinuity final {
    bool seen = false;
    bool established = false;
    std::uint8_t counter = 0U;
};

// Runtime event handling is single-owner.  A pump either handles events for
// this short interval or waits on its callback condition while a command or
// the other bridge owns the event loop, keeping stop latency bounded.
constexpr std::uint32_t kPumpWaitMs = 1U;

bool same_attachment(const TunerAttachment& left,
                     const TunerAttachment& right) noexcept
{
    return left.owner_client_id == right.owner_client_id &&
           left.lease_id == right.lease_id &&
           left.attachment_id == right.attachment_id &&
           left.receiver == right.receiver && left.system == right.system &&
           left.nonce == right.nonce;
}

StreamTerminal terminal_for_error(Error error) noexcept
{
    return error == Error::DISCONNECTED ? StreamTerminal::disconnected
                                        : StreamTerminal::usb_error;
}

}  // namespace

class Q3U4StreamDataPlane::Impl final {
public:
    struct Session final {
        mutable std::mutex mutex;
        std::condition_variable changed;
        std::unique_ptr<std::uint8_t[]> packets;
        std::size_t capacity = 0U;
        std::size_t head = 0U;
        std::size_t count = 0U;
        bool attached = false;
        bool draining = false;
        TunerAttachment identity{};
        bool final_snapshot_valid = false;
        TunerAttachment final_identity{};
        TunerStreamFinalSnapshot final_snapshot{};
        StreamTerminal terminal = StreamTerminal::none;
        StreamCounters counters{};
#if defined(DTV_USERLAND_DIAGNOSTICS) || defined(PX4_Q3U4_STREAM_TEST_ACCESS)
        std::uint64_t diagnostic_wire_tei_packets = 0U;
#endif
        TsContinuityChecker continuity_checker;
        std::array<StartupContinuity, kPidCount> continuity{};
        bool stabilizing = true;
        bool stabilization_ready = false;
        bool stabilization_boundary_established = false;
        bool startup_budget_fallback = false;
        bool startup_continuity_established = false;
        std::size_t startup_packets = 0U;
        std::size_t startup_clean_packets = 0U;
        StreamCounters startup_faults{};
    };

    struct Bridge final {
        enum class State : std::uint8_t { stopped, starting, running, stopping, fatal };

        Impl& owner;
        Transport& transport;
        std::size_t receiver_base;
        std::mutex lifecycle;
        std::unique_ptr<std::thread> thread;
        bool stop_requested = false;
        bool join_in_progress = false;
        State state = State::stopped;
        bool cleanup_done = false;
        Error cleanup_error = Error::OK;
        std::size_t attached_count = 0U;
        TaggedTsDemux demux;
        TaggedTsDemux::Counters demux_counters{};
#if defined(DTV_USERLAND_DIAGNOSTICS)
        struct BoundaryDiagnostic final {
            struct ReceiverCounts final {
                std::uint64_t packets = 0U;
                std::uint64_t forwarded = 0U;
                std::uint64_t transfer_tei = 0U;
                std::uint64_t demux_tei = 0U;
                std::uint64_t sink_tei = 0U;
                std::uint64_t transfer_adaptation_overruns = 0U;
                std::uint64_t demux_adaptation_overruns = 0U;
                std::uint64_t sink_adaptation_overruns = 0U;
                std::array<std::uint64_t, 4U> transfer_afc{};
                std::array<std::uint64_t, 4U> demux_afc{};
                std::array<std::uint64_t, 4U> sink_afc{};
            };
            struct Example final {
                std::size_t packet_ordinal = 0U;
                std::size_t receiver = 0U;
                std::uint8_t tag = 0U;
                std::uint8_t transfer_copy_mask = 0U;
                std::uint8_t demux_sink_mask = 0U;
                TaggedTsDemux::PacketHeaderScalars transfer;
                TaggedTsDemux::PacketHeaderScalars demux;
                TaggedTsDemux::PacketHeaderScalars sink;
            };
            std::uint64_t transfer_events = 0U;
            std::uint64_t transfer_bytes = 0U;
            std::uint64_t packets = 0U;
            std::uint64_t forwarded = 0U;
            std::uint64_t missing_transfer_input = 0U;
            std::uint64_t transfer_copy_mismatches = 0U;
            std::uint64_t demux_sink_mismatches = 0U;
            std::array<ReceiverCounts, 4U> receivers{};
            std::array<Example, 8U> examples{};
            std::size_t example_count = 0U;
        } boundary_diagnostic;
        std::uint64_t last_diagnostic_sample_us = 0U;
#endif
        std::condition_variable state_changed;

        Bridge(Impl& owner_value, Transport& transport_value,
               std::size_t receiver_base_value) noexcept
            : owner(owner_value), transport(transport_value),
              receiver_base(receiver_base_value),
          demux(static_cast<std::uint8_t>(owner.layout_.tag_count), owner.layout_.plain_ts)
        {
        }

        bool stopping() noexcept
        {
            std::lock_guard<std::mutex> lock(lifecycle);
            return stop_requested || state == State::stopping ||
                   state == State::stopped || state == State::fatal;
        }

        void run() noexcept
        {
#if defined(DTV_USERLAND_DIAGNOSTICS)
            last_diagnostic_sample_us =
                dtv::userland::detail::diagnostic_now_us();
#endif
            while (!stopping()) {
                const auto event = transport.wait_stream(Timeout{kPumpWaitMs});
                if (!event) {
                    if (event.error() == Error::TIMEOUT) {
                        owner.count_empty(receiver_base);
                        owner.maybe_sample_diagnostics(*this);
                        continue;
                    }
                    owner.mark_bridge_terminal(*this, terminal_for_error(event.error()));
                    owner.finish_pump(*this);
                    break;
                }
                if (event.value().size == 0U) {
                    owner.count_empty(receiver_base);
                    owner.maybe_sample_diagnostics(*this, true);
                    continue;
                }
                // short_transfer has no dedicated counter in the wire/API
                // contract.  Its bytes are therefore processed as data;
                // only an actually empty event increments empty_intervals.
                const bool published_before =
                    owner.bridge_has_published_packets(receiver_base);
#if defined(DTV_USERLAND_DIAGNOSTICS)
                if (owner.boundary_diagnostic_enabled()) {
                    ++boundary_diagnostic.transfer_events;
                    boundary_diagnostic.transfer_bytes += event.value().size;
                    demux.observe_transfer_input(
                        ByteView{event.value().data, event.value().size});
                }
#endif
                const auto pushed = demux.push(
                    ByteView{event.value().data, event.value().size},
                    &Impl::packet_sink, this, &Impl::wire_tag_observer, this
#if defined(DTV_USERLAND_DIAGNOSTICS)
                    , owner.boundary_diagnostic_enabled()
                          ? &Impl::packet_boundary_observer
                          : nullptr,
                    this
#endif
                    );
                const auto current = demux.counters();
                owner.count_sync_deltas(receiver_base, demux_counters, current,
                                        published_before);
                demux_counters = current;
                owner.maybe_sample_diagnostics(*this);
                if (!pushed) {
                    owner.mark_bridge_terminal(*this, pushed.error() == Error::SLOW_CONSUMER
                                                    ? StreamTerminal::sync_error
                                                    : StreamTerminal::bridge_fatal);
                    owner.finish_pump(*this);
                    break;
                }
            }
#if defined(DTV_USERLAND_DIAGNOSTICS)
            if (owner.boundary_diagnostic_enabled())
                owner.emit_boundary_diagnostic(*this);
#endif
        }
    };

    // A single-bridge layout passes the same transport twice; the second
    // Bridge is never started or iterated.
    Impl(Transport& dev1, Transport& dev2, std::size_t queue_packets,
         StartupStabilizationPolicy stabilization, StreamLayout layout) noexcept
        : queue_packets_(queue_packets), stabilization_(stabilization), layout_(layout),
          bridges_{Bridge(*this, dev1, 0U),
                   Bridge(*this, dev2, layout.receivers_per_bridge)}
    {
    }

    ~Impl() noexcept
    {
        (void)shutdown();
    }

    static Result<std::unique_ptr<Impl>> create(Transport& dev1, Transport& dev2,
                                                 std::size_t queue_packets,
                                                 StartupStabilizationPolicy stabilization,
                                                 StreamLayout layout) noexcept
    {
        if (queue_packets < Q3U4StreamDataPlane::kMinQueuePackets ||
            queue_packets > Q3U4StreamDataPlane::kMaxQueuePackets ||
            queue_packets > std::numeric_limits<std::size_t>::max() / kPacketSize)
            return Result<std::unique_ptr<Impl>>::failure(Error::INVALID_ARGUMENT);
        if (stabilization.enabled() &&
            (stabilization.minimum_packets == 0U ||
             stabilization.clean_packets == 0U ||
             stabilization.maximum_packets < stabilization.minimum_packets ||
             stabilization.maximum_packets < stabilization.clean_packets))
            return Result<std::unique_ptr<Impl>>::failure(Error::INVALID_ARGUMENT);
        std::unique_ptr<Impl> value(
            new (std::nothrow) Impl(dev1, dev2, queue_packets, stabilization, layout));
        if (!value) return Result<std::unique_ptr<Impl>>::failure(Error::INTERNAL);
        return Result<std::unique_ptr<Impl>>::success(std::move(value));
    }

    static Result<void> packet_sink(void* context, std::size_t local_receiver,
                                    ByteView packet) noexcept
    {
        auto* bridge = static_cast<Bridge*>(context);
        if (local_receiver >= bridge->owner.layout_.tag_count || packet.size != kPacketSize)
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        const std::int8_t mapped = bridge->owner.layout_.tag_to_receiver[local_receiver];
        if (mapped < 0) return Result<void>::success();
        return bridge->owner.enqueue(
            bridge->receiver_base + static_cast<std::size_t>(mapped), packet);
    }

    static void wire_tag_observer(void* context, std::uint8_t wire_sync) noexcept
    {
        auto* bridge = static_cast<Bridge*>(context);
        if ((wire_sync & 0x80U) == 0U) return;
        const std::size_t wire_tag = (wire_sync >> 4U) & 0x07U;
        if (wire_tag == 0U || wire_tag > bridge->owner.layout_.tag_count) return;
        const std::int8_t mapped = bridge->owner.layout_.tag_to_receiver[wire_tag - 1U];
        if (mapped >= 0)
            bridge->owner.count_wire_tei(bridge->receiver_base + static_cast<std::size_t>(mapped));
    }

#if defined(DTV_USERLAND_DIAGNOSTICS)
    bool boundary_diagnostic_enabled() const noexcept
    {
        return layout_.bridge_count == 2U &&
               layout_.receivers_per_bridge == 4U &&
               layout_.tag_count == 4U && !layout_.plain_ts;
    }

    static void packet_boundary_observer(
        void* context,
        const TaggedTsDemux::PacketBoundaryObservation& observation) noexcept
    {
        auto* bridge = static_cast<Bridge*>(context);
        auto& summary = bridge->boundary_diagnostic;
        ++summary.packets;
        const auto add_header = [](const TaggedTsDemux::PacketHeaderScalars& header,
                                   std::array<std::uint64_t, 4U>& afc,
                                   std::uint64_t& tei,
                                   std::uint64_t& overruns) noexcept {
            if (header.tei) ++tei;
            if (header.adaptation_field_control < afc.size())
                ++afc[header.adaptation_field_control];
            if (header.adaptation_length_overrun) ++overruns;
        };
        std::size_t local_receiver = bridge->owner.layout_.receivers_per_bridge;
        if (observation.wire_tag != 0U &&
            observation.wire_tag <= bridge->owner.layout_.tag_count) {
            const std::int8_t mapped = bridge->owner.layout_.tag_to_receiver[
                observation.wire_tag - 1U];
            if (mapped >= 0) local_receiver = static_cast<std::size_t>(mapped);
        }
        Bridge::BoundaryDiagnostic::ReceiverCounts* receiver_counts =
            local_receiver < bridge->owner.layout_.receivers_per_bridge
                ? &summary.receivers[local_receiver]
                : nullptr;
        if (receiver_counts != nullptr) ++receiver_counts->packets;
        if (!observation.transfer_input_available) {
            ++summary.missing_transfer_input;
        } else {
            if (receiver_counts != nullptr)
                add_header(observation.transfer_input,
                           receiver_counts->transfer_afc,
                           receiver_counts->transfer_tei,
                           receiver_counts->transfer_adaptation_overruns);
            if (observation.transfer_copy_mismatch_mask != 0U)
                ++summary.transfer_copy_mismatches;
        }
        if (receiver_counts != nullptr)
            add_header(observation.demux_input, receiver_counts->demux_afc,
                       receiver_counts->demux_tei,
                       receiver_counts->demux_adaptation_overruns);
        if (observation.forwarded) {
            ++summary.forwarded;
            if (receiver_counts != nullptr) ++receiver_counts->forwarded;
            if (receiver_counts != nullptr)
                add_header(observation.sink, receiver_counts->sink_afc,
                           receiver_counts->sink_tei,
                           receiver_counts->sink_adaptation_overruns);
            if (observation.demux_sink_mismatch_mask != 0U)
                ++summary.demux_sink_mismatches;
        }

        if ((!observation.transfer_input_available ||
             observation.transfer_copy_mismatch_mask != 0U ||
             observation.demux_sink_mismatch_mask != 0U) &&
            summary.example_count < summary.examples.size()) {
            auto& example = summary.examples[summary.example_count++];
            example.packet_ordinal = observation.packet_ordinal;
            example.receiver = local_receiver < bridge->owner.layout_.receivers_per_bridge
                                   ? bridge->receiver_base + local_receiver
                                   : bridge->receiver_base +
                                         bridge->owner.layout_.receivers_per_bridge;
            example.tag = observation.wire_tag;
            example.transfer_copy_mask = observation.transfer_copy_mismatch_mask;
            example.demux_sink_mask = observation.demux_sink_mismatch_mask;
            example.transfer = observation.transfer_input;
            example.demux = observation.demux_input;
            example.sink = observation.sink;
        }
    }

    void emit_boundary_diagnostic(const Bridge& bridge) const noexcept
    {
        const auto& summary = bridge.boundary_diagnostic;
        if (summary.transfer_events == 0U) return;
        dtv::userland::detail::diagnostic_log(
            "component=q3_boundary event=summary bridge=%u transfer_events=%llu transfer_bytes=%llu packets=%llu forwarded=%llu missing_transfer=%llu scanner_sample_overflow=%u transfer_copy_mismatches=%llu demux_sink_mismatches=%llu",
            static_cast<unsigned>(bridge.receiver_base / layout_.receivers_per_bridge),
            static_cast<unsigned long long>(summary.transfer_events),
            static_cast<unsigned long long>(summary.transfer_bytes),
            static_cast<unsigned long long>(summary.packets),
            static_cast<unsigned long long>(summary.forwarded),
            static_cast<unsigned long long>(summary.missing_transfer_input),
            bridge.demux.transfer_input_sample_overflowed() ? 1U : 0U,
            static_cast<unsigned long long>(summary.transfer_copy_mismatches),
            static_cast<unsigned long long>(summary.demux_sink_mismatches));
        for (std::size_t receiver = 0U; receiver < summary.receivers.size(); ++receiver) {
            const auto& counts = summary.receivers[receiver];
            dtv::userland::detail::diagnostic_log(
                "component=q3_boundary event=receiver bridge=%u receiver=%u packets=%llu forwarded=%llu transfer_tei=%llu demux_tei=%llu sink_tei=%llu transfer_adaptation_overruns=%llu demux_adaptation_overruns=%llu sink_adaptation_overruns=%llu transfer_afc=%llu/%llu/%llu/%llu demux_afc=%llu/%llu/%llu/%llu sink_afc=%llu/%llu/%llu/%llu",
                static_cast<unsigned>(bridge.receiver_base /
                                      layout_.receivers_per_bridge),
                static_cast<unsigned>(bridge.receiver_base + receiver),
                static_cast<unsigned long long>(counts.packets),
                static_cast<unsigned long long>(counts.forwarded),
                static_cast<unsigned long long>(counts.transfer_tei),
                static_cast<unsigned long long>(counts.demux_tei),
                static_cast<unsigned long long>(counts.sink_tei),
                static_cast<unsigned long long>(counts.transfer_adaptation_overruns),
                static_cast<unsigned long long>(counts.demux_adaptation_overruns),
                static_cast<unsigned long long>(counts.sink_adaptation_overruns),
                static_cast<unsigned long long>(counts.transfer_afc[0U]),
                static_cast<unsigned long long>(counts.transfer_afc[1U]),
                static_cast<unsigned long long>(counts.transfer_afc[2U]),
                static_cast<unsigned long long>(counts.transfer_afc[3U]),
                static_cast<unsigned long long>(counts.demux_afc[0U]),
                static_cast<unsigned long long>(counts.demux_afc[1U]),
                static_cast<unsigned long long>(counts.demux_afc[2U]),
                static_cast<unsigned long long>(counts.demux_afc[3U]),
                static_cast<unsigned long long>(counts.sink_afc[0U]),
                static_cast<unsigned long long>(counts.sink_afc[1U]),
                static_cast<unsigned long long>(counts.sink_afc[2U]),
                static_cast<unsigned long long>(counts.sink_afc[3U]));
        }
        for (std::size_t index = 0U; index < summary.example_count; ++index) {
            const auto& example = summary.examples[index];
            dtv::userland::detail::diagnostic_log(
                "component=q3_boundary event=example bridge=%u ordinal=%zu receiver=%zu tag=%u transfer_copy_mask=%u demux_sink_mask=%u tei=%u/%u/%u afc=%u/%u/%u adaptation_length=%u/%u/%u",
                static_cast<unsigned>(bridge.receiver_base /
                                      layout_.receivers_per_bridge),
                example.packet_ordinal, example.receiver,
                static_cast<unsigned>(example.tag),
                static_cast<unsigned>(example.transfer_copy_mask),
                static_cast<unsigned>(example.demux_sink_mask),
                example.transfer.tei ? 1U : 0U,
                example.demux.tei ? 1U : 0U,
                example.sink.tei ? 1U : 0U,
                static_cast<unsigned>(example.transfer.adaptation_field_control),
                static_cast<unsigned>(example.demux.adaptation_field_control),
                static_cast<unsigned>(example.sink.adaptation_field_control),
                static_cast<unsigned>(example.transfer.adaptation_field_length),
                static_cast<unsigned>(example.demux.adaptation_field_length),
                static_cast<unsigned>(example.sink.adaptation_field_length));
        }
    }
#endif

    bool valid_receiver(std::size_t receiver) const noexcept
    {
        return receiver < layout_.bridge_count * layout_.receivers_per_bridge;
    }

    std::size_t bridge_index(std::size_t receiver) const noexcept
    {
        return receiver / layout_.receivers_per_bridge;
    }

    bool valid_system(std::size_t receiver, ipc::System system) const noexcept
    {
        if (layout_.dual_system)
            return system == ipc::System::ISDB_T || system == ipc::System::ISDB_S;
        if (layout_.plain_ts)
            return system == ipc::System::ISDB_T;
        const bool satellite = (receiver % layout_.receivers_per_bridge) < 2U;
        return system == (satellite ? ipc::System::ISDB_S : ipc::System::ISDB_T);
    }

    // Lock order: bridge.lifecycle -> session.mutex.  Reader methods acquire
    // only session.mutex.  No code takes lifecycle while holding a session
    // mutex, which keeps pump terminalization and cleanup free of inversion.
    Result<void> start_bridge_locked(Bridge& bridge) noexcept
    {
        if (bridge.state != Bridge::State::stopped)
            return Result<void>::failure(Error::BUSY);
        if (bridge.thread != nullptr)
            return Result<void>::failure(Error::BUSY);
        bridge.state = Bridge::State::starting;
        bridge.cleanup_done = false;
        bridge.cleanup_error = Error::OK;
        const auto started = bridge.transport.start_stream(
            StreamConfig{kTsInEndpoint, 188U * 816U, 6U});
        if (!started) {
            bridge.state = Bridge::State::stopped;
            bridge.state_changed.notify_all();
            return started;
        }
        bridge.demux.reset();
        bridge.demux_counters = bridge.demux.counters();
#if defined(DTV_USERLAND_DIAGNOSTICS)
        bridge.boundary_diagnostic = Bridge::BoundaryDiagnostic{};
        bridge.last_diagnostic_sample_us =
            dtv::userland::detail::diagnostic_now_us();
#endif
        bridge.stop_requested = false;
        // This repository builds with exceptions disabled.  The standard
        // thread constructor follows that existing policy: allocation is
        // nothrow, while an OS thread-start failure is process-fatal under
        // the C++ standard library rather than recoverable as a Result.
        bridge.thread.reset(new (std::nothrow) std::thread([&bridge]() noexcept {
            bridge.run();
        }));
        if (!bridge.thread) {
            bridge.state = Bridge::State::stopping;
            bridge.stop_requested = true;
            // There is no worker to join, but transport still needs the same
            // finite cancel/drain cleanup as a normal last detach.
            const auto cancelled = bridge.transport.cancel_stream();
            const auto stopped = bridge.transport.stop_stream();
            bridge.state = (!cancelled || !stopped) ? Bridge::State::fatal
                                                    : Bridge::State::stopped;
            bridge.cleanup_done = true;
            if (!cancelled) bridge.cleanup_error = cancelled.error();
            else if (!stopped) bridge.cleanup_error = stopped.error();
            bridge.state_changed.notify_all();
            if (!cancelled || !stopped) {
                enclosure_fatal_.store(true);
                mark_all_terminal(StreamTerminal::bridge_fatal);
            }
            if (!cancelled) return cancelled;
            if (!stopped) return stopped;
            return Result<void>::failure(Error::INTERNAL);
        }
        bridge.state = Bridge::State::running;
        bridge.state_changed.notify_all();
        return Result<void>::success();
    }

    Result<void> stop_bridge(Bridge& bridge, bool owns_stopping = false) noexcept
    {
        std::unique_ptr<std::thread> thread;
        bool cleanup_needed = false;
        std::unique_lock<std::mutex> lifecycle(bridge.lifecycle);
        if (!owns_stopping) {
            while (bridge.state == Bridge::State::starting ||
                   bridge.state == Bridge::State::stopping ||
                   bridge.join_in_progress)
                bridge.state_changed.wait(lifecycle);
        } else {
            while (bridge.join_in_progress)
                bridge.state_changed.wait(lifecycle);
        }
        const bool join_needed = bridge.thread != nullptr;
        cleanup_needed = !bridge.cleanup_done &&
                         (bridge.state == Bridge::State::running ||
                          (owns_stopping && bridge.state == Bridge::State::stopping));
        if (!join_needed && !cleanup_needed)
            return bridge.cleanup_error == Error::OK
                       ? Result<void>::success()
                       : Result<void>::failure(bridge.cleanup_error);
        // Claim the join object while holding the lifecycle lock.  A second
        // cleanup caller waits for this epoch and then observes cleanup_done;
        // it must never inspect or reset the same std::thread concurrently.
        thread = std::move(bridge.thread);
        bridge.join_in_progress = thread != nullptr;
        bridge.stop_requested = true;
        if (cleanup_needed) bridge.state = Bridge::State::stopping;
        lifecycle.unlock();

        Error first = Error::OK;
        if (cleanup_needed) {
            const auto cancelled = bridge.transport.cancel_stream();
            if (!cancelled) first = cancelled.error();
        }
        if (thread != nullptr && thread->joinable()) {
            thread->join();
        }
        if (cleanup_needed) {
            const auto stopped = bridge.transport.stop_stream();
            if (!stopped && first == Error::OK) first = stopped.error();
        }

        lifecycle.lock();
        if (first == Error::OK) first = bridge.cleanup_error;
        bridge.cleanup_done = true;
        bridge.cleanup_error = first;
        bridge.state = first == Error::OK ? Bridge::State::stopped : Bridge::State::fatal;
        bridge.join_in_progress = false;
        bridge.state_changed.notify_all();
        lifecycle.unlock();
        if (first != Error::OK) {
            enclosure_fatal_.store(true);
            mark_all_terminal(StreamTerminal::bridge_fatal);
            return Result<void>::failure(first);
        }
        return Result<void>::success();
    }

    void finish_pump(Bridge& bridge) noexcept
    {
        std::unique_lock<std::mutex> lifecycle(bridge.lifecycle);
        if (bridge.state != Bridge::State::running || bridge.cleanup_done)
            return;
        bridge.state = Bridge::State::stopping;
        bridge.stop_requested = true;
        lifecycle.unlock();

        Error first = Error::OK;
        const auto cancelled = bridge.transport.cancel_stream();
        if (!cancelled) first = cancelled.error();
        const auto stopped = bridge.transport.stop_stream();
        if (!stopped && first == Error::OK) first = stopped.error();

        lifecycle.lock();
        bridge.cleanup_done = true;
        bridge.cleanup_error = first;
        // A bulk/demux failure ends this pump's epoch.  It is not an
        // enclosure-fatal condition unless finite cancel/stop cleanup fails.
        bridge.state = Bridge::State::fatal;
        if (first != Error::OK) {
            enclosure_fatal_.store(true);
            mark_all_terminal(StreamTerminal::bridge_fatal);
        }
        // The owner claims and joins this thread after cleanup is complete.
        // Nothing may access owner or bridge state after waking waiters.
        bridge.state_changed.notify_all();
        lifecycle.unlock();
    }

    Result<void> attach(const TunerAttachment& attachment) noexcept
    {
        if (attachment.owner_client_id == 0U || attachment.lease_id == 0U ||
            attachment.attachment_id == 0U || !valid_receiver(attachment.receiver))
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        if (!valid_system(attachment.receiver, attachment.system))
            return Result<void>::failure(Error::INVALID_ARGUMENT);

        Bridge& bridge = bridges_[bridge_index(attachment.receiver)];
        std::unique_lock<std::mutex> lifecycle(bridge.lifecycle);
        while (bridge.state == Bridge::State::starting ||
               bridge.state == Bridge::State::stopping)
            bridge.state_changed.wait(lifecycle);
        if (shutting_down_.load() || enclosure_fatal_.load() ||
            bridge.state == Bridge::State::fatal)
            return Result<void>::failure(Error::DISCONNECTED);
        if (bridge.state == Bridge::State::stopped && bridge.attached_count != 0U)
            return Result<void>::failure(Error::INTERNAL);
        Session& session = sessions_[attachment.receiver];
        static_assert(Q3U4StreamDataPlane::kMaxQueuePackets <=
                      std::numeric_limits<std::size_t>::max() / kPacketSize);
        {
            std::lock_guard<std::mutex> lock(session.mutex);
            if (session.attached || session.draining)
                return Result<void>::failure(Error::BUSY);
            if (session.packets == nullptr) {
                if (queue_packets_ > std::numeric_limits<std::size_t>::max() /
                                    kPacketSize)
                    return Result<void>::failure(Error::INTERNAL);
                session.packets.reset(new (std::nothrow)
                                         std::uint8_t[queue_packets_ * kPacketSize]);
                if (!session.packets)
                    return Result<void>::failure(Error::INTERNAL);
                session.capacity = queue_packets_;
            }
            session.head = 0U;
            session.count = 0U;
            session.attached = true;
            session.draining = false;
            session.identity = attachment;
            session.final_snapshot_valid = false;
            session.final_identity = TunerAttachment{};
            session.final_snapshot = TunerStreamFinalSnapshot{};
            session.terminal = StreamTerminal::none;
            session.counters = StreamCounters{};
#if defined(DTV_USERLAND_DIAGNOSTICS) || defined(PX4_Q3U4_STREAM_TEST_ACCESS)
            session.diagnostic_wire_tei_packets = 0U;
#endif
            session.counters.availability.fill(
                dtv::userland::wire::MeasureState::valid);
            session.continuity_checker.reset();
            session.continuity.fill(StartupContinuity{});
            session.stabilizing = stabilization_.enabled();
            session.stabilization_ready = false;
            session.stabilization_boundary_established = false;
            session.startup_budget_fallback = false;
            session.startup_continuity_established = false;
            session.startup_packets = 0U;
            session.startup_clean_packets = 0U;
            session.startup_faults = StreamCounters{};
        }
        if (bridge.attached_count == 0U) {
            const auto started = start_bridge_locked(bridge);
            if (!started) {
                std::lock_guard<std::mutex> lock(session.mutex);
                session.attached = false;
                session.terminal = StreamTerminal::stopped;
                session.count = 0U;
                session.changed.notify_all();
                return started;
            }
        }
        ++bridge.attached_count;
        return Result<void>::success();
    }

    Result<void> detach(const TunerAttachment& attachment) noexcept
    {
        if (!valid_receiver(attachment.receiver))
            return Result<void>::failure(Error::NOT_FOUND);
        Bridge& bridge = bridges_[bridge_index(attachment.receiver)];
        std::unique_lock<std::mutex> lifecycle(bridge.lifecycle);
        while (bridge.state == Bridge::State::starting ||
               bridge.state == Bridge::State::stopping)
            bridge.state_changed.wait(lifecycle);
        Session& session = sessions_[attachment.receiver];
        TunerStreamFinalSnapshot snapshot{};
        {
            std::lock_guard<std::mutex> lock(session.mutex);
            if (!session.attached || !same_attachment(session.identity, attachment))
                return Result<void>::failure(Error::NOT_FOUND);
            const StreamTerminal final_terminal =
                session.terminal == StreamTerminal::none
                    ? StreamTerminal::stopped : session.terminal;
            snapshot.counters = session.counters;
            snapshot.terminal = static_cast<std::uint8_t>(final_terminal);
            session.final_identity = session.identity;
            session.final_snapshot = snapshot;
            session.final_snapshot_valid = true;
            session.attached = false;
            session.terminal = final_terminal;
            // Keep already queued packets readable by the data endpoint.  A
            // later release_final() or the final EOF read retires this
            // drain-only session before the receiver can be reopened.
            session.draining = true;
            session.changed.notify_all();
        }
        if (bridge.attached_count != 0U) --bridge.attached_count;
        if (bridge.attached_count == 0U) {
            const bool owns_stopping = bridge.state == Bridge::State::running;
            if (owns_stopping) {
                bridge.state = Bridge::State::stopping;
                bridge.stop_requested = true;
            }
            lifecycle.unlock();
            const auto stopped = stop_bridge(bridge, owns_stopping);
            if (!stopped) {
                // The logical session was already invalidated above.  Keep
                // its retained final snapshot honest when physical bridge
                // cleanup fails after that linearization point.
                std::lock_guard<std::mutex> lock(session.mutex);
                if (session.final_snapshot_valid &&
                    same_attachment(session.final_identity, attachment)) {
                    session.final_snapshot.terminal = static_cast<std::uint8_t>(
                        StreamTerminal::bridge_fatal);
                }
            } else {
                std::lock_guard<std::mutex> lock(bridge.lifecycle);
                if (bridge.state == Bridge::State::fatal &&
                    bridge.attached_count == 0U)
                    bridge.state = Bridge::State::stopped;
                bridge.state_changed.notify_all();
            }
            return stopped;
        }
        return Result<void>::success();
    }

    Result<StreamReadResult> read(const TunerAttachment& attachment,
                                  MutableByteView output, Timeout timeout) noexcept
    {
        if (!valid_receiver(attachment.receiver) ||
            (output.size != 0U && output.data == nullptr) ||
            output.size == 0U || (output.size % kPacketSize) != 0U ||
            output.size > kMaxReadBytes)
            return Result<StreamReadResult>::failure(Error::INVALID_ARGUMENT);
        Session& session = sessions_[attachment.receiver];
        std::unique_lock<std::mutex> lock(session.mutex);
        const bool identity_active = session.attached &&
                                     same_attachment(session.identity, attachment);
        const bool identity_draining = session.draining &&
                                       same_attachment(session.final_identity, attachment);
        if (!identity_active && !identity_draining)
            return Result<StreamReadResult>::failure(Error::NOT_FOUND);
        if (session.count == 0U && session.terminal == StreamTerminal::none &&
            timeout.milliseconds != 0U) {
#if defined(PX4_Q3U4_STREAM_TEST_ACCESS)
            if (read_wait_observer != nullptr)
                read_wait_observer(read_wait_observer_context, attachment.receiver);
#endif
            (void)session.changed.wait_for(
                lock, std::chrono::milliseconds(timeout.milliseconds), [&]() noexcept {
                    return session.count != 0U || session.terminal != StreamTerminal::none;
                });
        }
        if (session.count == 0U) {
            if (session.terminal != StreamTerminal::none)
            {
                const StreamTerminal terminal = session.terminal;
                session.draining = false;
                session.changed.notify_all();
                return Result<StreamReadResult>::success(
                    StreamReadResult{0U, true, false, terminal});
            }
            return Result<StreamReadResult>::success(
                StreamReadResult{0U, false, true, StreamTerminal::none});
        }
        std::size_t packets = output.size / kPacketSize;
        packets = packets > session.count ? session.count : packets;
        const std::size_t first = session.head;
        const std::size_t first_count =
            packets > session.capacity - first ? session.capacity - first : packets;
        if (first_count != 0U)
            std::memcpy(output.data, session.packets.get() + first * kPacketSize,
                        first_count * kPacketSize);
        if (packets > first_count)
            std::memcpy(output.data + first_count * kPacketSize, session.packets.get(),
                        (packets - first_count) * kPacketSize);
        session.head = (session.head + packets) % session.capacity;
        session.count -= packets;
        return Result<StreamReadResult>::success(
            StreamReadResult{packets * kPacketSize, false, false,
                             StreamTerminal::none});
    }

    Result<StreamCounters> stats(const TunerAttachment& attachment) const noexcept
    {
        if (!valid_receiver(attachment.receiver))
            return Result<StreamCounters>::failure(Error::NOT_FOUND);
        const Session& session = sessions_[attachment.receiver];
        std::lock_guard<std::mutex> lock(session.mutex);
        if (!session.attached || !same_attachment(session.identity, attachment))
            return Result<StreamCounters>::failure(Error::NOT_FOUND);
        return Result<StreamCounters>::success(session.counters);
    }

    Result<TunerStreamFinalSnapshot> final_snapshot(
        const TunerAttachment& attachment) const noexcept
    {
        if (!valid_receiver(attachment.receiver))
            return Result<TunerStreamFinalSnapshot>::failure(Error::NOT_FOUND);
        const Session& session = sessions_[attachment.receiver];
        std::lock_guard<std::mutex> lock(session.mutex);
        if (!session.final_snapshot_valid ||
            !same_attachment(session.final_identity, attachment))
            return Result<TunerStreamFinalSnapshot>::failure(Error::NOT_FOUND);
        return Result<TunerStreamFinalSnapshot>::success(session.final_snapshot);
    }

    Result<void> release_final(const TunerAttachment& attachment) noexcept
    {
        if (!valid_receiver(attachment.receiver))
            return Result<void>::failure(Error::NOT_FOUND);
        Session& session = sessions_[attachment.receiver];
        std::lock_guard<std::mutex> lock(session.mutex);
        if (!session.final_snapshot_valid ||
            !same_attachment(session.final_identity, attachment))
            return Result<void>::failure(Error::NOT_FOUND);
        session.draining = false;
        session.count = 0U;
        session.changed.notify_all();
        return Result<void>::success();
    }

#if defined(PX4_Q3U4_STREAM_TEST_ACCESS)
    void set_read_wait_observer(Q3U4StreamDataPlane::ReadWaitObserver observer,
                                void* context) noexcept
    {
        read_wait_observer = observer;
        read_wait_observer_context = context;
    }

    void set_packet_enqueue_observer(
        Q3U4StreamDataPlane::PacketEnqueueObserver observer,
        void* context) noexcept
    {
        packet_enqueue_observer = observer;
        packet_enqueue_observer_context = context;
    }

    void set_diagnostic_sample_observer(
        Q3U4StreamDataPlane::DiagnosticSampleObserver observer,
        void* context) noexcept
    {
        diagnostic_sample_observer = observer;
        diagnostic_sample_observer_context = context;
    }

    void emit_diagnostic_sample_for_test(std::uint8_t bridge) noexcept
    {
        if (bridge < layout_.bridge_count) emit_diagnostic_sample(bridges_[bridge]);
    }

    std::size_t empty_event_sample_gate_calls_for_test(std::uint8_t bridge) const noexcept
    {
        return bridge < layout_.bridge_count
            ? empty_event_sample_gate_calls[bridge].load(std::memory_order_relaxed) : 0U;
    }
#endif

    Result<StreamTerminal> terminal(const TunerAttachment& attachment) const noexcept
    {
        if (!valid_receiver(attachment.receiver))
            return Result<StreamTerminal>::failure(Error::NOT_FOUND);
        const Session& session = sessions_[attachment.receiver];
        std::lock_guard<std::mutex> lock(session.mutex);
        if (!session.attached || !same_attachment(session.identity, attachment))
            return Result<StreamTerminal>::failure(Error::NOT_FOUND);
        return Result<StreamTerminal>::success(session.terminal);
    }

    void count_empty(std::size_t base) noexcept
    {
        for (std::size_t offset = 0U; offset < layout_.receivers_per_bridge; ++offset) {
            Session& session = sessions_[base + offset];
            std::lock_guard<std::mutex> lock(session.mutex);
            if (session.attached && session.terminal == StreamTerminal::none)
                ++session.counters.empty_intervals;
        }
    }

    bool bridge_has_published_packets(std::size_t base) const noexcept
    {
        for (std::size_t offset = 0U; offset < layout_.receivers_per_bridge; ++offset) {
            const Session& session = sessions_[base + offset];
            std::lock_guard<std::mutex> lock(session.mutex);
            if (session.attached && session.counters.packets != 0U) return true;
        }
        return false;
    }

    void count_sync_deltas(std::size_t base, TaggedTsDemux::Counters before,
                           TaggedTsDemux::Counters after,
                           bool published_before) noexcept
    {
        const std::size_t losses = after.sync_loss_events - before.sync_loss_events;
        if (losses != 0U) {
            bridge_sync_errors_[bridge_index(base)].fetch_add(losses);
            // A loss occurs before a valid local tag is available.  Mirror
            // each bridge-wide observation into the receivers active at the
            // observation point so their lease-level counters are useful;
            // this is intentionally replicated attribution, never a byte sum.
            for (std::size_t offset = 0U; offset < layout_.receivers_per_bridge; ++offset) {
                Session& session = sessions_[base + offset];
                std::lock_guard<std::mutex> lock(session.mutex);
                if (!session.attached || session.terminal != StreamTerminal::none)
                    continue;
                if (session.stabilizing) {
                    session.startup_faults.sync_errors += losses;
                    session.startup_clean_packets = 0U;
                    session.stabilization_ready = false;
                } else if (session.stabilization_boundary_established ||
                           published_before) {
                    session.counters.sync_errors += losses;
                }
            }
        }
        // Promotion is deliberately deferred until the complete USB event has
        // been demultiplexed and its bridge-wide sync observations applied.
        // Otherwise an early clean run and a later resync in the same event
        // could publish a false clean boundary.
        promote_ready_sessions(base);
    }

    void count_wire_tei(std::size_t receiver) noexcept
    {
        Session& session = sessions_[receiver];
        std::lock_guard<std::mutex> lock(session.mutex);
        if (!session.attached || session.terminal != StreamTerminal::none) return;
#if defined(DTV_USERLAND_DIAGNOSTICS) || defined(PX4_Q3U4_STREAM_TEST_ACCESS)
        ++session.diagnostic_wire_tei_packets;
#endif
        session.continuity_checker.observe_untrusted_packet();
        if (session.stabilizing) {
            ++session.startup_packets;
            ++session.startup_faults.tei_packets;
            session.startup_clean_packets = 0U;
            session.stabilization_ready = false;
            finish_startup_packet_locked(session);
        } else {
            ++session.counters.tei_packets;
        }
    }

#if defined(DTV_USERLAND_DIAGNOSTICS) || defined(PX4_Q3U4_STREAM_TEST_ACCESS)
    bool make_diagnostic_sample(Bridge& bridge, std::uint8_t offset,
                                DiagnosticSample& sample) noexcept
    {
        const std::size_t receiver = bridge.receiver_base + offset;
        if (!valid_receiver(receiver)) return false;
        const Session& session = sessions_[receiver];
        std::lock_guard<std::mutex> lock(session.mutex);
        if (!session.attached) return false;
        sample.bridge = static_cast<std::uint8_t>(bridge_index(receiver));
        sample.receiver = static_cast<std::uint8_t>(receiver);
        sample.terminal = session.terminal;
        sample.ring_count = session.count;
        sample.ring_capacity = session.capacity;
        sample.tei_packets = session.stabilizing
            ? session.startup_faults.tei_packets : session.counters.tei_packets;
        sample.wire_tei_packets = session.diagnostic_wire_tei_packets;
        sample.mpeg_tei_packets = sample.tei_packets >= sample.wire_tei_packets
            ? sample.tei_packets - sample.wire_tei_packets : 0U;
        sample.packets = session.stabilizing
            ? session.startup_packets : session.counters.packets;
        sample.bytes = session.counters.bytes;
        sample.continuity_errors = session.stabilizing
            ? session.startup_faults.continuity_errors
            : session.counters.continuity_errors;
        sample.queue_drops = session.counters.queue_drops;
        sample.usb_errors = session.counters.usb_errors;
        sample.empty_intervals = session.counters.empty_intervals;
        const auto demux = bridge.demux_counters;
        sample.demux_invalid_tags = demux.invalid_tag_packets;
        sample.demux_sync_losses = demux.sync_loss_events;
        sample.demux_input_bytes = demux.input_bytes_accepted;
        sample.demux_emitted_packets = demux.emitted_packets;
        sample.demux_buffered_bytes = demux.buffered_bytes;
        return true;
    }

    void emit_diagnostic_sample(Bridge& bridge) noexcept
    {
        for (std::size_t offset = 0U; offset < layout_.receivers_per_bridge; ++offset) {
            DiagnosticSample sample{};
            if (!make_diagnostic_sample(bridge, static_cast<std::uint8_t>(offset), sample))
                continue;
#if defined(PX4_Q3U4_STREAM_TEST_ACCESS)
            if (diagnostic_sample_observer != nullptr) {
                Q3U4StreamDataPlane::DiagnosticSampleForTest test_sample{};
                test_sample.bridge = sample.bridge;
                test_sample.receiver = sample.receiver;
                test_sample.attached = true;
                test_sample.terminal = sample.terminal;
                test_sample.ring_count = sample.ring_count;
                test_sample.ring_capacity = sample.ring_capacity;
                test_sample.packets = sample.packets;
                test_sample.bytes = sample.bytes;
                test_sample.tei_packets = sample.tei_packets;
                test_sample.wire_tei_packets = sample.wire_tei_packets;
                test_sample.mpeg_tei_packets = sample.mpeg_tei_packets;
                test_sample.continuity_errors = sample.continuity_errors;
                test_sample.queue_drops = sample.queue_drops;
                test_sample.usb_errors = sample.usb_errors;
                test_sample.empty_intervals = sample.empty_intervals;
                test_sample.demux_invalid_tags = sample.demux_invalid_tags;
                test_sample.demux_sync_losses = sample.demux_sync_losses;
                test_sample.demux_input_bytes = sample.demux_input_bytes;
                test_sample.demux_emitted_packets = sample.demux_emitted_packets;
                test_sample.demux_buffered_bytes = sample.demux_buffered_bytes;
                diagnostic_sample_observer(diagnostic_sample_observer_context, test_sample);
                continue;
            }
#endif
            dtv::userland::detail::diagnostic_log(
                "component=q3_stream event=sample bridge=%u receiver=%u terminal=%u ring_count=%zu ring_capacity=%zu packets=%llu bytes=%llu tei_packets=%llu wire_tei_dropped=%llu mpeg_tei=%llu continuity_errors=%llu queue_drops=%llu usb_errors=%llu empty_intervals=%llu demux_invalid_tags=%zu demux_sync_losses=%zu demux_input_bytes=%zu demux_emitted_packets=%zu demux_buffered_bytes=%zu",
                static_cast<unsigned>(sample.bridge),
                static_cast<unsigned>(sample.receiver),
                static_cast<unsigned>(sample.terminal), sample.ring_count,
                sample.ring_capacity,
                static_cast<unsigned long long>(sample.packets),
                static_cast<unsigned long long>(sample.bytes),
                static_cast<unsigned long long>(sample.tei_packets),
                static_cast<unsigned long long>(sample.wire_tei_packets),
                static_cast<unsigned long long>(sample.mpeg_tei_packets),
                static_cast<unsigned long long>(sample.continuity_errors),
                static_cast<unsigned long long>(sample.queue_drops),
                static_cast<unsigned long long>(sample.usb_errors),
                static_cast<unsigned long long>(sample.empty_intervals),
                sample.demux_invalid_tags, sample.demux_sync_losses,
                sample.demux_input_bytes, sample.demux_emitted_packets,
                sample.demux_buffered_bytes);
        }
    }
#endif

    void maybe_sample_diagnostics(Bridge& bridge, bool successful_empty_event = false) noexcept
    {
#if defined(PX4_Q3U4_STREAM_TEST_ACCESS)
        if (successful_empty_event) {
            empty_event_sample_gate_calls[
                bridge_index(bridge.receiver_base)].fetch_add(1U, std::memory_order_relaxed);
        }
#else
        (void)successful_empty_event;
#endif
#if defined(DTV_USERLAND_DIAGNOSTICS)
        const std::uint64_t now_us = dtv::userland::detail::diagnostic_now_us();
        if (!diagnostic_sample_due(now_us, bridge.last_diagnostic_sample_us)) return;
        bridge.last_diagnostic_sample_us = now_us;
        emit_diagnostic_sample(bridge);
#else
        (void)bridge;
#endif
    }

    Result<void> enqueue(std::size_t receiver, ByteView packet) noexcept
    {
        Session& session = sessions_[receiver];
        {
            std::lock_guard<std::mutex> lock(session.mutex);
            if (!session.attached || session.terminal != StreamTerminal::none)
                return Result<void>::success();
            if (session.stabilizing) {
                ++session.startup_packets;
                if ((packet.data[1U] & 0x80U) != 0U) {
                    session.continuity_checker.observe_untrusted_packet();
                    ++session.startup_faults.tei_packets;
                    session.startup_clean_packets = 0U;
                    session.stabilization_ready = false;
                    finish_startup_packet_locked(session);
                    return Result<void>::success();
                }
                const ContinuityObservation observation =
                    observe_continuity(session, packet);
                if (observation == ContinuityObservation::fault) {
                    ++session.startup_faults.continuity_errors;
                    session.startup_clean_packets = 0U;
                    session.stabilization_ready = false;
                } else {
                    ++session.startup_clean_packets;
                }
                finish_startup_packet_locked(session);
                return Result<void>::success();
            }
            if ((packet.data[1U] & 0x80U) != 0U)
                ++session.counters.tei_packets;
            const ContinuityObservation observation =
                observe_continuity(session, packet);
            if (observation == ContinuityObservation::boundary &&
                !session.startup_budget_fallback)
                return Result<void>::success();
            if (observation == ContinuityObservation::fault)
                ++session.counters.continuity_errors;
            ++session.counters.packets;
            session.counters.bytes += kPacketSize;
            if (session.count == session.capacity) {
                ++session.counters.queue_drops;
                session.terminal = StreamTerminal::slow_consumer;
                dtv::userland::detail::diagnostic_log(
                    "px4_ring_terminal receiver=%zu terminal=slow_consumer count=%zu capacity=%zu drops=%llu",
                    receiver, session.count, session.capacity,
                    static_cast<unsigned long long>(session.counters.queue_drops));
                session.changed.notify_all();
                return Result<void>::success();
            }
            const std::size_t tail = (session.head + session.count) % session.capacity;
            std::memcpy(session.packets.get() + tail * kPacketSize, packet.data, kPacketSize);
            ++session.count;
            session.changed.notify_all();
        }
#if defined(PX4_Q3U4_STREAM_TEST_ACCESS)
        if (packet_enqueue_observer != nullptr)
            packet_enqueue_observer(packet_enqueue_observer_context,
                                    static_cast<std::uint8_t>(receiver));
#endif
        return Result<void>::success();
    }

    static ContinuityObservation observe_continuity(Session& session,
                                                     ByteView packet) noexcept
    {
        const TsContinuityResult observation =
            session.continuity_checker.observe(packet.data, packet.size);
        if (observation.kind == TsContinuityKind::null_packet ||
            observation.kind == TsContinuityKind::transport_error ||
            observation.kind == TsContinuityKind::invalid_packet)
            return ContinuityObservation::clean;
        StartupContinuity& state = session.continuity[observation.pid];
        const std::uint8_t counter = observation.counter;
        if (observation.discontinuity) {
            // The marker resets the previous expectation, and its own CC is
            // the baseline for the following payload packet.
            state.seen = true;
            state.established = true;
            state.counter = counter;
            session.startup_continuity_established = true;
            return ContinuityObservation::clean;
        }
        if (state.established) {
            return observation.kind == TsContinuityKind::error
                ? ContinuityObservation::fault : ContinuityObservation::clean;
        }
        if (!observation.has_payload) return ContinuityObservation::clean;
        if (!state.seen) {
            // The IT930x PSB can expose one stale packet when a capture pin is
            // enabled, then switch to the live multiplex.  A session boundary
            // has no prior CC continuity contract, so require one consecutive
            // transition before publishing this PID.  This discards the
            // unverified boundary packet rather than hiding an error in the
            // delivered TS.  Once established, every gap remains observable.
            state.seen = true;
            state.counter = counter;
            return ContinuityObservation::boundary;
        }
        const std::uint8_t expected =
            static_cast<std::uint8_t>((state.counter + 1U) & 0x0fU);
        state.counter = counter;
        if (counter != expected) return ContinuityObservation::boundary;
        state.established = true;
        session.startup_continuity_established = true;
        return ContinuityObservation::clean;
    }

    void finish_startup_packet_locked(Session& session) noexcept
    {
        if (!session.stabilizing || session.terminal != StreamTerminal::none) return;
        session.stabilization_ready =
            session.startup_continuity_established &&
            session.startup_packets >= stabilization_.minimum_packets &&
            session.startup_clean_packets >= stabilization_.clean_packets;
        if (session.startup_packets < stabilization_.maximum_packets ||
            session.stabilization_ready)
            return;

        // Preserve the withheld startup faults and publish only later packets.
        session.counters.sync_errors += session.startup_faults.sync_errors;
        session.counters.tei_packets += session.startup_faults.tei_packets;
        session.counters.continuity_errors +=
            session.startup_faults.continuity_errors;
        session.startup_budget_fallback = true;
        publish_session_locked(session);
    }

    void publish_session_locked(Session& session) noexcept
    {
        session.stabilizing = false;
        session.stabilization_ready = false;
        session.stabilization_boundary_established = true;
        session.continuity_checker.reset();
        session.continuity.fill(StartupContinuity{});
        session.changed.notify_all();
    }

    void promote_ready_sessions(std::size_t base) noexcept
    {
        for (std::size_t offset = 0U; offset < layout_.receivers_per_bridge; ++offset) {
            Session& session = sessions_[base + offset];
            std::lock_guard<std::mutex> lock(session.mutex);
            if (!session.attached || session.terminal != StreamTerminal::none ||
                !session.stabilizing || !session.stabilization_ready)
                continue;
            // No startup packet entered the public ring.  Reset PID state at
            // this exact publication boundary so the existing one-transition
            // session-boundary validation still applies.  From the next USB
            // event onward no integrity observation is suppressed.
            publish_session_locked(session);
        }
    }

    void mark_bridge_terminal(Bridge& bridge, StreamTerminal reason) noexcept
    {
        for (std::size_t offset = 0U; offset < layout_.receivers_per_bridge; ++offset) {
            Session& session = sessions_[bridge.receiver_base + offset];
            std::lock_guard<std::mutex> lock(session.mutex);
            if (session.attached && session.terminal == StreamTerminal::none) {
                if (reason == StreamTerminal::usb_error ||
                    reason == StreamTerminal::disconnected)
                    ++session.counters.usb_errors;
                session.terminal = reason;
                session.changed.notify_all();
            }
        }
    }

    void mark_all_terminal(StreamTerminal reason) noexcept
    {
        for (Session& session : sessions_) {
            std::lock_guard<std::mutex> lock(session.mutex);
            if (session.attached && session.terminal == StreamTerminal::none) {
                session.terminal = reason;
                session.changed.notify_all();
            }
        }
    }

    Result<void> shutdown() noexcept
    {
        shutting_down_.store(true);
        Error first = Error::OK;
        // Linearize the metadata transition with attach/detach.  The
        // lifecycle lock is deliberately held while taking each session lock
        // (the documented lifecycle -> session order); no new attach can
        // observe a detached session with an old attached_count.
        for (std::size_t index = 0U; index < layout_.bridge_count; ++index) {
            Bridge& bridge = bridges_[index];
            bool owns_stopping = false;
            {
                std::unique_lock<std::mutex> lifecycle(bridge.lifecycle);
                while (bridge.state == Bridge::State::starting ||
                       bridge.state == Bridge::State::stopping)
                    bridge.state_changed.wait(lifecycle);
                for (std::size_t offset = 0U; offset < layout_.receivers_per_bridge;
                     ++offset) {
                    Session& session = sessions_[bridge.receiver_base + offset];
                    std::lock_guard<std::mutex> session_lock(session.mutex);
                    if (session.attached) {
                        const StreamTerminal final_terminal =
                            session.terminal == StreamTerminal::none
                                ? StreamTerminal::stopped : session.terminal;
                        session.final_identity = session.identity;
                        session.final_snapshot.counters = session.counters;
                        session.final_snapshot.terminal = static_cast<std::uint8_t>(
                            final_terminal);
                        session.final_snapshot_valid = true;
                        session.attached = false;
                        session.terminal = final_terminal;
                        session.draining = false;
                        session.count = 0U;
                        session.changed.notify_all();
                    }
                }
                bridge.attached_count = 0U;
                if (bridge.state == Bridge::State::running) {
                    bridge.state = Bridge::State::stopping;
                    bridge.stop_requested = true;
                    owns_stopping = true;
                }
            }
            const auto stopped = stop_bridge(bridge, owns_stopping);
            if (!stopped) {
                if (first == Error::OK) first = stopped.error();
                // Sessions were logically detached before cleanup.  Preserve
                // their terminal reason unless physical epoch cleanup itself
                // failed, in which case STREAM_END must expose bridge_fatal.
                std::lock_guard<std::mutex> lifecycle(bridge.lifecycle);
                if (bridge.state == Bridge::State::fatal) {
                    for (std::size_t offset = 0U; offset < layout_.receivers_per_bridge;
                         ++offset) {
                        Session& session = sessions_[bridge.receiver_base + offset];
                        std::lock_guard<std::mutex> session_lock(session.mutex);
                        if (session.final_snapshot_valid)
                            session.final_snapshot.terminal = static_cast<std::uint8_t>(
                                StreamTerminal::bridge_fatal);
                    }
                }
            } else {
                std::lock_guard<std::mutex> lifecycle(bridge.lifecycle);
                if (bridge.state == Bridge::State::fatal &&
                    bridge.attached_count == 0U)
                    bridge.state = Bridge::State::stopped;
                bridge.state_changed.notify_all();
            }
        }
        return first == Error::OK ? Result<void>::success()
                                  : Result<void>::failure(first);
    }

    std::size_t queue_packets_;
    StartupStabilizationPolicy stabilization_;
    StreamLayout layout_;
    std::array<Session, ipc::kReceiverCount> sessions_{};
    std::array<Bridge, kBridgeCount> bridges_;
    std::atomic<bool> enclosure_fatal_{false};
    std::atomic<bool> shutting_down_{false};
    std::atomic<std::uint64_t> bridge_sync_errors_[kBridgeCount]{};
#if defined(PX4_Q3U4_STREAM_TEST_ACCESS)
    Q3U4StreamDataPlane::ReadWaitObserver read_wait_observer = nullptr;
    void* read_wait_observer_context = nullptr;
    Q3U4StreamDataPlane::PacketEnqueueObserver packet_enqueue_observer = nullptr;
    void* packet_enqueue_observer_context = nullptr;
    Q3U4StreamDataPlane::DiagnosticSampleObserver diagnostic_sample_observer = nullptr;
    void* diagnostic_sample_observer_context = nullptr;
    std::array<std::atomic<std::size_t>, kBridgeCount> empty_event_sample_gate_calls{};
#endif
};

Q3U4StreamDataPlane::Q3U4StreamDataPlane(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

Result<std::unique_ptr<Q3U4StreamDataPlane>> Q3U4StreamDataPlane::create(
    Transport& dev1, Transport& dev2, std::size_t queue_packets) noexcept
{
    auto impl = Impl::create(dev1, dev2, queue_packets,
                             StartupStabilizationPolicy{}, kQ3U4Layout);
    if (!impl) return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(impl.error());
    std::unique_ptr<Q3U4StreamDataPlane> value(
        new (std::nothrow) Q3U4StreamDataPlane(std::move(impl.value())));
    if (!value) return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INTERNAL);
    return Result<std::unique_ptr<Q3U4StreamDataPlane>>::success(std::move(value));
}

Result<std::unique_ptr<Q3U4StreamDataPlane>> Q3U4StreamDataPlane::create_w3u4(
    Transport& device, std::size_t queue_packets) noexcept
{
    auto impl = Impl::create(device, device, queue_packets,
                             StartupStabilizationPolicy{}, kW3U4Layout);
    if (!impl) return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(impl.error());
    std::unique_ptr<Q3U4StreamDataPlane> value(
        new (std::nothrow) Q3U4StreamDataPlane(std::move(impl.value())));
    if (!value) return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INTERNAL);
    return Result<std::unique_ptr<Q3U4StreamDataPlane>>::success(std::move(value));
}

Result<std::unique_ptr<Q3U4StreamDataPlane>> Q3U4StreamDataPlane::create_mlt5pe(
    Transport& device, std::size_t queue_packets) noexcept
{
    return create_mlt_family(device, DeviceModel::px_mlt5pe, queue_packets);
}

Result<std::unique_ptr<Q3U4StreamDataPlane>> Q3U4StreamDataPlane::create_mlt_family(
    Transport& device, DeviceModel model,
    std::size_t queue_packets) noexcept
{
    const MltModelLayout profile = mlt_model_layout(model);
    const std::uint8_t receiver_count = profile.receiver_count;
    if (receiver_count == 0U || receiver_count > 5U)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INVALID_ARGUMENT);
    const StreamLayout layout = mlt_stream_layout(model);
    auto impl = Impl::create(device, device, queue_packets,
                             StartupStabilizationPolicy{}, layout);
    if (!impl) return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(impl.error());
    std::unique_ptr<Q3U4StreamDataPlane> value(
        new (std::nothrow) Q3U4StreamDataPlane(std::move(impl.value())));
    if (!value) return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INTERNAL);
    return Result<std::unique_ptr<Q3U4StreamDataPlane>>::success(std::move(value));
}

Result<std::unique_ptr<Q3U4StreamDataPlane>> Q3U4StreamDataPlane::create_single_receiver(
    Transport& device, DeviceModel model, std::size_t queue_packets) noexcept
{
    if (model != DeviceModel::px_m1ur && model != DeviceModel::px_s1ur &&
        model != DeviceModel::dtv03a_1tu && model != DeviceModel::dtv02_1t1s_u &&
        model != DeviceModel::dtv02a_1t1s_u)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INVALID_ARGUMENT);
    auto impl = Impl::create(device, device, queue_packets,
                             StartupStabilizationPolicy{}, mlt_stream_layout(model));
    if (!impl) return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(impl.error());
    std::unique_ptr<Q3U4StreamDataPlane> value(
        new (std::nothrow) Q3U4StreamDataPlane(std::move(impl.value())));
    if (!value) return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INTERNAL);
    return Result<std::unique_ptr<Q3U4StreamDataPlane>>::success(std::move(value));
}

#if defined(PX4_Q3U4_STREAM_TEST_ACCESS)
Result<std::unique_ptr<Q3U4StreamDataPlane>>
Q3U4StreamDataPlane::create_for_test(
    Transport& dev1, Transport& dev2, std::size_t queue_packets,
    StartupStabilizationTestConfig stabilization) noexcept
{
    const StartupStabilizationPolicy policy{
        stabilization.minimum_packets,
        stabilization.clean_packets,
        stabilization.maximum_packets,
    };
    auto impl = Impl::create(dev1, dev2, queue_packets, policy, kQ3U4Layout);
    if (!impl)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(impl.error());
    std::unique_ptr<Q3U4StreamDataPlane> value(
        new (std::nothrow) Q3U4StreamDataPlane(std::move(impl.value())));
    if (!value)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INTERNAL);
    return Result<std::unique_ptr<Q3U4StreamDataPlane>>::success(std::move(value));
}

Result<std::unique_ptr<Q3U4StreamDataPlane>>
Q3U4StreamDataPlane::create_mlt5pe_for_test(
    Transport& device, std::size_t queue_packets,
    StartupStabilizationTestConfig stabilization) noexcept
{
    const StartupStabilizationPolicy policy{
        stabilization.minimum_packets,
        stabilization.clean_packets,
        stabilization.maximum_packets,
    };
    auto impl = Impl::create(device, device, queue_packets, policy, kMlt5PeLayout);
    if (!impl)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(impl.error());
    std::unique_ptr<Q3U4StreamDataPlane> value(
        new (std::nothrow) Q3U4StreamDataPlane(std::move(impl.value())));
    if (!value)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INTERNAL);
    return Result<std::unique_ptr<Q3U4StreamDataPlane>>::success(std::move(value));
}

Result<std::unique_ptr<Q3U4StreamDataPlane>>
Q3U4StreamDataPlane::create_mlt_family_for_test(
    Transport& device, DeviceModel model, std::size_t queue_packets,
    StartupStabilizationTestConfig stabilization) noexcept
{
    const MltModelLayout profile = mlt_model_layout(model);
    const std::uint8_t receiver_count = profile.receiver_count;
    if (receiver_count == 0U || receiver_count > 5U)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INVALID_ARGUMENT);
    const StartupStabilizationPolicy policy{
        stabilization.minimum_packets,
        stabilization.clean_packets,
        stabilization.maximum_packets,
    };
    const StreamLayout layout = mlt_stream_layout(model);
    auto impl = Impl::create(device, device, queue_packets, policy, layout);
    if (!impl)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(impl.error());
    std::unique_ptr<Q3U4StreamDataPlane> value(
        new (std::nothrow) Q3U4StreamDataPlane(std::move(impl.value())));
    if (!value)
        return Result<std::unique_ptr<Q3U4StreamDataPlane>>::failure(Error::INTERNAL);
    return Result<std::unique_ptr<Q3U4StreamDataPlane>>::success(std::move(value));
}
#endif

Q3U4StreamDataPlane::~Q3U4StreamDataPlane() noexcept = default;

Result<void> Q3U4StreamDataPlane::attach(const TunerAttachment& attachment) noexcept
{
    return impl_ == nullptr ? Result<void>::failure(Error::INTERNAL)
                            : impl_->attach(attachment);
}

Result<void> Q3U4StreamDataPlane::detach(const TunerAttachment& attachment) noexcept
{
    return impl_ == nullptr ? Result<void>::failure(Error::INTERNAL)
                            : impl_->detach(attachment);
}

Result<TunerStreamFinalSnapshot> Q3U4StreamDataPlane::final_snapshot(
    const TunerAttachment& attachment) const noexcept
{
    return impl_ == nullptr
        ? Result<TunerStreamFinalSnapshot>::failure(Error::INTERNAL)
        : impl_->final_snapshot(attachment);
}

Result<void> Q3U4StreamDataPlane::release_final(
    const TunerAttachment& attachment) noexcept
{
    return impl_ == nullptr ? Result<void>::failure(Error::INTERNAL)
                            : impl_->release_final(attachment);
}

#if defined(PX4_Q3U4_STREAM_TEST_ACCESS)
void Q3U4StreamDataPlane::set_read_wait_observer_for_test(
    ReadWaitObserver observer, void* context) noexcept
{
    if (impl_ != nullptr) impl_->set_read_wait_observer(observer, context);
}

void Q3U4StreamDataPlane::set_packet_enqueue_observer_for_test(
    PacketEnqueueObserver observer, void* context) noexcept
{
    if (impl_ != nullptr) impl_->set_packet_enqueue_observer(observer, context);
}

void Q3U4StreamDataPlane::set_diagnostic_sample_observer_for_test(
    DiagnosticSampleObserver observer, void* context) noexcept
{
    if (impl_ != nullptr) impl_->set_diagnostic_sample_observer(observer, context);
}

std::size_t Q3U4StreamDataPlane::empty_event_sample_gate_calls_for_test(
    std::uint8_t bridge) const noexcept
{
    return impl_ != nullptr ? impl_->empty_event_sample_gate_calls_for_test(bridge) : 0U;
}

void Q3U4StreamDataPlane::emit_diagnostic_sample_for_test(
    std::uint8_t bridge) noexcept
{
    if (impl_ != nullptr) impl_->emit_diagnostic_sample_for_test(bridge);
}

bool Q3U4StreamDataPlane::diagnostic_sample_due_for_test(
    std::uint64_t now_us, std::uint64_t last_sample_us) noexcept
{
    return diagnostic_sample_due(now_us, last_sample_us);
}
#endif

Result<StreamReadResult> Q3U4StreamDataPlane::read(
    const TunerAttachment& attachment, MutableByteView output, Timeout timeout) noexcept
{
    return impl_ == nullptr
        ? Result<StreamReadResult>::failure(Error::INTERNAL)
        : impl_->read(attachment, output, timeout);
}

Result<StreamCounters> Q3U4StreamDataPlane::stats(
    const TunerAttachment& attachment) const noexcept
{
    return impl_ == nullptr ? Result<StreamCounters>::failure(Error::INTERNAL)
                            : impl_->stats(attachment);
}

Result<StreamTerminal> Q3U4StreamDataPlane::terminal(
    const TunerAttachment& attachment) const noexcept
{
    return impl_ == nullptr ? Result<StreamTerminal>::failure(Error::INTERNAL)
                            : impl_->terminal(attachment);
}

Result<std::uint64_t> Q3U4StreamDataPlane::bridge_sync_errors(
    std::uint8_t bridge) const noexcept
{
    if (bridge >= kBridgeCount ||
        (impl_ != nullptr && bridge >= impl_->layout_.bridge_count))
        return Result<std::uint64_t>::failure(Error::INVALID_ARGUMENT);
    return Result<std::uint64_t>::success(
        impl_ == nullptr ? 0U : impl_->bridge_sync_errors_[bridge].load());
}

Result<void> Q3U4StreamDataPlane::shutdown() noexcept
{
    return impl_ == nullptr ? Result<void>::failure(Error::INTERNAL)
                            : impl_->shutdown();
}

bool Q3U4StreamDataPlane::enclosure_fatal() const noexcept
{
    return impl_ != nullptr && impl_->enclosure_fatal_.load();
}

}  // namespace px4::userland

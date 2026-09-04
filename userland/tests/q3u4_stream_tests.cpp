// SPDX-License-Identifier: GPL-2.0-only
#include "px4/q3u4_stream.h"
#include "tagged_ts_demux.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;

#define STREAM_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "q3u4 stream check failed at %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

class FakeTransport final : public Transport {
public:
    Result<std::size_t> bulk_read(std::uint8_t, MutableByteView, Timeout,
                                  BulkReadObservation*) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    Result<std::size_t> bulk_write(std::uint8_t, ByteView, Timeout) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }

    Result<void> start_stream(const StreamConfig& config) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++starts;
        last_config = config;
        if (start_error != Error::OK)
            return Result<void>::failure(start_error);
        active = true;
        cancelled = false;
        return Result<void>::success();
    }

    Result<StreamEvent> wait_stream(Timeout timeout) noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++wait_calls;
        last_wait_timeout_ms = timeout.milliseconds;
        wait_changed.notify_all();
        (void)changed.wait_for(lock, std::chrono::milliseconds(timeout.milliseconds),
                               [&]() noexcept {
                                   return cancelled || wait_error != Error::OK ||
                                          !events.empty();
                               });
        if (cancelled) return Result<StreamEvent>::failure(Error::DISCONNECTED);
        if (wait_error != Error::OK) {
            const Error error = wait_error;
            wait_error = Error::OK;
            return Result<StreamEvent>::failure(error);
        }
        if (events.empty()) return Result<StreamEvent>::failure(Error::TIMEOUT);
        current = std::move(events.front());
        events.pop_front();
        const StreamEventKind kind = current_kind;
        current_kind = StreamEventKind::data;
        return Result<StreamEvent>::success(
            StreamEvent{kind, current.data(), current.size()});
    }

    Result<void> cancel_stream() noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++cancels;
        if (block_cancel) {
            cancel_entered = true;
            changed.notify_all();
            changed.wait(lock, [&]() noexcept { return allow_cancel; });
        }
        cancelled = true;
        changed.notify_all();
        return cancel_error == Error::OK ? Result<void>::success()
                                         : Result<void>::failure(cancel_error);
    }

    Result<void> stop_stream() noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++stops;
        if (block_stop) {
            stop_entered = true;
            changed.notify_all();
            changed.wait(lock, [&]() noexcept { return allow_stop; });
        }
        active = false;
        changed.notify_all();
        return stop_error == Error::OK ? Result<void>::success()
                                       : Result<void>::failure(stop_error);
    }

    bool stream_active() const noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return active;
    }

    void push(std::vector<std::uint8_t> value) noexcept
    {
        push_kind(StreamEventKind::data, std::move(value));
    }

    void push_short(std::vector<std::uint8_t> value) noexcept
    {
        push_kind(StreamEventKind::short_transfer, std::move(value));
    }

    void push_kind(StreamEventKind kind, std::vector<std::uint8_t> value) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        current_kind = kind;
        events.push_back(std::move(value));
        changed.notify_all();
    }

    std::size_t wait_count() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        return wait_calls;
    }

    std::uint32_t last_wait_timeout() const noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        return last_wait_timeout_ms;
    }

    bool wait_for_count(std::size_t count) noexcept
    {
        std::unique_lock<std::mutex> lock(mutex);
        return wait_changed.wait_for(lock, std::chrono::seconds(5),
                                     [&]() noexcept { return wait_calls >= count; });
    }

    bool wait_for_stop_count(std::size_t count) noexcept
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(5), [&]() noexcept {
            return stops >= count;
        });
    }

    bool wait_for_cancel_count(std::size_t count) noexcept
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(5), [&]() noexcept {
            return cancels >= count;
        });
    }

    void fail(Error error) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        wait_error = error;
        changed.notify_all();
    }

    mutable std::mutex mutex;
    std::condition_variable changed;
    std::condition_variable wait_changed;
    std::deque<std::vector<std::uint8_t>> events;
    std::vector<std::uint8_t> current;
    Error start_error = Error::OK;
    Error cancel_error = Error::OK;
    Error stop_error = Error::OK;
    Error wait_error = Error::OK;
    std::size_t starts = 0U;
    std::size_t cancels = 0U;
    std::size_t stops = 0U;
    std::size_t wait_calls = 0U;
    std::uint32_t last_wait_timeout_ms = std::numeric_limits<std::uint32_t>::max();
    bool active = false;
    bool cancelled = false;
    bool block_cancel = false;
    bool cancel_entered = false;
    bool allow_cancel = false;
    bool block_stop = false;
    bool stop_entered = false;
    bool allow_stop = false;
    StreamEventKind current_kind = StreamEventKind::data;
    StreamConfig last_config{};
};

TunerAttachment attachment(std::uint8_t receiver, std::uint64_t id) noexcept
{
    TunerAttachment value{};
    value.owner_client_id = 1U;
    value.lease_id = id;
    value.attachment_id = id + 100U;
    value.receiver = receiver;
    value.system = receiver < 2U || (receiver >= 4U && receiver < 6U)
        ? ipc::System::ISDB_S : ipc::System::ISDB_T;
    value.nonce[0U] = static_cast<std::uint8_t>(id);
    return value;
}

std::array<std::uint8_t, TaggedTsDemux::kPacketSize> wire_packet(
    std::uint8_t tag, std::uint16_t pid, std::uint8_t cc,
    std::uint8_t adaptation = 1U, bool discontinuity = false,
    bool tei = false) noexcept
{
    std::array<std::uint8_t, TaggedTsDemux::kPacketSize> packet{};
    packet[0U] = static_cast<std::uint8_t>((tag << 4U) | 0x07U);
    packet[1U] = static_cast<std::uint8_t>((pid >> 8U) & 0x1fU);
    if (tei) packet[1U] = static_cast<std::uint8_t>(packet[1U] | 0x80U);
    packet[2U] = static_cast<std::uint8_t>(pid);
    packet[3U] = static_cast<std::uint8_t>((adaptation << 4U) | (cc & 0x0fU));
    if (adaptation == 3U) {
        packet[4U] = 1U;
        packet[5U] = discontinuity ? 0x80U : 0U;
    }
    return packet;
}

std::vector<std::uint8_t> stream(std::uint8_t tag, std::size_t count,
                                 std::uint16_t pid = 0x101U) noexcept
{
    std::vector<std::uint8_t> result;
    // The production stream establishes PID continuity across the session
    // boundary before publishing packets.  Supply one baseline packet so
    // callers still receive exactly count packets from this helper.
    result.reserve((count + 1U) * TaggedTsDemux::kPacketSize);
    for (std::size_t index = 0U; index <= count; ++index) {
        const auto packet = wire_packet(tag, pid, static_cast<std::uint8_t>(index));
        result.insert(result.end(), packet.begin(), packet.end());
    }
    return result;
}

Result<std::unique_ptr<Q3U4StreamDataPlane>> create_test_plane(
    FakeTransport& dev1, FakeTransport& dev2,
    std::size_t queue_packets = Q3U4StreamDataPlane::kDefaultQueuePackets) noexcept
{
    // Existing lifecycle/golden tests exercise their original packet counts.
    // Startup stabilization has dedicated tests below with explicit compact
    // thresholds; the release build always uses the production policy.
    return Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, queue_packets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
}

bool test_mapping_and_bridge_lifecycle()
{
    FakeTransport dev1;
    FakeTransport dev2;
    const auto created = create_test_plane(dev1, dev2,
                                                      Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto first = attachment(0U, 1U);
    const auto second = attachment(1U, 2U);
    const auto other_bridge = attachment(4U, 3U);
    STREAM_CHECK(plane.attach(first));
    STREAM_CHECK(dev1.starts == 1U);
    STREAM_CHECK(dev1.wait_for_count(1U));
    STREAM_CHECK(dev1.last_wait_timeout() == 1U);
    STREAM_CHECK(dev1.last_config.endpoint == kTsInEndpoint &&
                 dev1.last_config.transfer_size == 188U * 816U &&
                 dev1.last_config.transfer_count == 6U);
    STREAM_CHECK(plane.attach(second));
    STREAM_CHECK(dev1.starts == 1U);
    STREAM_CHECK(plane.attach(other_bridge));
    STREAM_CHECK(dev2.starts == 1U);

    dev1.push(stream(1U, 5U));
    std::array<std::uint8_t, TaggedTsDemux::kPacketSize> output{};
    std::size_t bytes = 0U;
    for (std::size_t index = 0U; index < 5U; ++index) {
        const auto read = plane.read(first, MutableByteView{output.data(), output.size()},
                                     Timeout{1000U});
        STREAM_CHECK(read && read.value().bytes == output.size());
        bytes += read.value().bytes;
    }
    STREAM_CHECK(bytes == TaggedTsDemux::kPacketSize * 5U);
    STREAM_CHECK(output[0U] == 0x47U);

    STREAM_CHECK(dev2.wait_for_count(1U));
    const std::size_t dev2_baseline = dev2.wait_count();
    dev2.push(stream(1U, 4U));
    STREAM_CHECK(dev2.wait_for_count(dev2_baseline + 1U));
    const auto other_read = plane.read(other_bridge,
                                       MutableByteView{output.data(), output.size()},
                                       Timeout{1000U});
    STREAM_CHECK(other_read && other_read.value().bytes == output.size());
    STREAM_CHECK(output[0U] == 0x47U);

    auto stale = first;
    stale.attachment_id++;
    STREAM_CHECK(plane.detach(stale).error() == Error::NOT_FOUND);
    STREAM_CHECK(plane.detach(first));
    const auto final = plane.final_snapshot(first);
    STREAM_CHECK(final && final.value().counters.packets >= 5U &&
                 final.value().terminal == static_cast<std::uint8_t>(StreamTerminal::stopped));
    STREAM_CHECK(dev1.stops == 0U);
    STREAM_CHECK(plane.detach(second));
    STREAM_CHECK(dev1.stops == 1U);
    STREAM_CHECK(plane.detach(other_bridge));
    STREAM_CHECK(dev2.stops == 1U);

    const auto reopened = attachment(2U, 4U);
    STREAM_CHECK(plane.attach(reopened));
    STREAM_CHECK(dev1.starts == 2U);
    STREAM_CHECK(plane.detach(reopened));
    return true;
}

struct PacketEnqueueBarrier final {
    static void observe(void* context, std::uint8_t) noexcept
    {
        auto& barrier = *static_cast<PacketEnqueueBarrier*>(context);
        std::lock_guard<std::mutex> lock(barrier.mutex);
        ++barrier.count;
        barrier.changed.notify_all();
    }

    bool wait_for(std::size_t expected) noexcept
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(5), [&]() noexcept {
            return count >= expected;
        });
    }

    std::mutex mutex;
    std::condition_variable changed;
    std::size_t count = 0U;
};

bool test_detach_drain_and_final_release()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = create_test_plane(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto old = attachment(2U, 200U);
    STREAM_CHECK(plane.attach(old) && dev1.wait_for_count(1U));
    PacketEnqueueBarrier enqueued;
    plane.set_packet_enqueue_observer_for_test(&PacketEnqueueBarrier::observe,
                                               &enqueued);
    dev1.push(stream(3U, 4U));
    const bool enqueued_ok = enqueued.wait_for(4U);
    plane.set_packet_enqueue_observer_for_test(nullptr, nullptr);
    STREAM_CHECK(enqueued_ok);

    STREAM_CHECK(plane.detach(old));
    const auto final = plane.final_snapshot(old);
    STREAM_CHECK(final && final.value().counters.packets == 4U &&
                 final.value().terminal ==
                     static_cast<std::uint8_t>(StreamTerminal::stopped));
    const auto replacement_busy = plane.attach(attachment(2U, 201U));
    STREAM_CHECK(!replacement_busy && replacement_busy.error() == Error::BUSY);

    std::array<std::uint8_t, 4U * TaggedTsDemux::kPacketSize> output{};
    const auto drained = plane.read(
        old, MutableByteView{output.data(), output.size()}, Timeout{0U});
    STREAM_CHECK(drained && drained.value().bytes == output.size() &&
                 output[0U] == 0x47U);
    const auto eof = plane.read(old, MutableByteView{output.data(), output.size()},
                                Timeout{0U});
    STREAM_CHECK(eof && eof.value().eof &&
                 eof.value().terminal == StreamTerminal::stopped);
    STREAM_CHECK(plane.release_final(old));

    const auto replacement = attachment(2U, 201U);
    STREAM_CHECK(plane.attach(replacement));
    const auto stale_release = plane.release_final(old);
    STREAM_CHECK(!stale_release && stale_release.error() == Error::NOT_FOUND);
    std::array<std::uint8_t, TaggedTsDemux::kPacketSize> one{};
    const auto replacement_read = plane.read(
        replacement, MutableByteView{one.data(), one.size()}, Timeout{0U});
    STREAM_CHECK(replacement_read && replacement_read.value().timed_out);
    STREAM_CHECK(plane.detach(replacement));

    const auto empty = attachment(2U, 202U);
    STREAM_CHECK(plane.attach(empty) && plane.detach(empty));
    const auto empty_final = plane.final_snapshot(empty);
    STREAM_CHECK(empty_final && empty_final.value().counters.packets == 0U &&
                 plane.release_final(empty));

    FakeTransport failing_dev1;
    FakeTransport failing_dev2;
    auto failing = create_test_plane(
        failing_dev1, failing_dev2, Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(failing);
    auto& failing_plane = *failing.value();
    const auto failed = attachment(2U, 203U);
    STREAM_CHECK(failing_plane.attach(failed));
    failing_dev1.cancel_error = Error::USB_IO;
    const auto failed_detach = failing_plane.detach(failed);
    STREAM_CHECK(!failed_detach && failed_detach.error() == Error::USB_IO);
    const auto failed_final = failing_plane.final_snapshot(failed);
    STREAM_CHECK(failed_final && failed_final.value().terminal ==
                 static_cast<std::uint8_t>(StreamTerminal::bridge_fatal));
    STREAM_CHECK(failing_plane.release_final(failed));
    return true;
}

bool test_queue_counters_and_terminal()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = create_test_plane(dev1, dev2,
                                                Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto value = attachment(0U, 7U);
    const auto sibling = attachment(1U, 71U);
    STREAM_CHECK(plane.attach(value));
    STREAM_CHECK(plane.attach(sibling));
    STREAM_CHECK(dev1.wait_for_count(1U));
    const std::size_t baseline_waits = dev1.wait_count();
    dev1.push(stream(1U, Q3U4StreamDataPlane::kMinQueuePackets + 4U));
    // The next wait call is entered only after the complete event has been
    // demultiplexed, so this condition is a deterministic processing barrier.
    STREAM_CHECK(dev1.wait_for_count(baseline_waits + 1U));
    const std::size_t sibling_baseline = dev1.wait_count();
    dev1.push(stream(2U, 4U));
    STREAM_CHECK(dev1.wait_for_count(sibling_baseline + 1U));
    std::array<std::uint8_t, TaggedTsDemux::kPacketSize> sibling_output{};
    const auto sibling_read = plane.read(
        sibling, MutableByteView{sibling_output.data(), sibling_output.size()},
        Timeout{1000U});
    STREAM_CHECK(sibling_read && sibling_read.value().bytes == sibling_output.size());
    STREAM_CHECK(sibling_output[0U] == 0x47U);
    std::vector<std::uint8_t> output(Q3U4StreamDataPlane::kMaxReadBytes);
    STREAM_CHECK(plane.read(value, MutableByteView{nullptr, 188U}, Timeout{0U})
                     .error() == Error::INVALID_ARGUMENT);
    STREAM_CHECK(plane.read(value, MutableByteView{output.data(), 187U}, Timeout{0U})
                     .error() == Error::INVALID_ARGUMENT);
    STREAM_CHECK(plane.read(value, MutableByteView{output.data(),
                                                   output.size() + 188U}, Timeout{0U})
                     .error() == Error::INVALID_ARGUMENT);
    bool eof = false;
    for (std::size_t attempt = 0U; attempt < 2U && !eof; ++attempt) {
        const auto read = plane.read(value, MutableByteView{output.data(), output.size()},
                                     Timeout{1000U});
        STREAM_CHECK(read);
        eof = read.value().eof;
    }
    STREAM_CHECK(eof);
    const auto stats = plane.stats(value);
    STREAM_CHECK(stats && stats.value().packets >= Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(stats.value().queue_drops == 1U);
    STREAM_CHECK(plane.terminal(value).value() == StreamTerminal::slow_consumer);
    STREAM_CHECK(plane.detach(value));
    STREAM_CHECK(plane.detach(sibling));
    return true;
}

void append_packet(std::vector<std::uint8_t>& bytes,
                   const std::array<std::uint8_t, TaggedTsDemux::kPacketSize>& packet)
{
    bytes.insert(bytes.end(), packet.begin(), packet.end());
}

bool test_continuity_and_epoch_reset()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = create_test_plane(dev1, dev2,
                                                Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    auto run_case = [&](std::uint64_t id, std::vector<std::uint8_t> input,
                        std::size_t expected_packets, std::uint64_t expected_errors,
                        bool fragmented) noexcept {
        const auto value = attachment(2U, id);
        if (!plane.attach(value)) return false;
        const std::size_t expected_bytes =
            expected_packets * TaggedTsDemux::kPacketSize;
        if (!dev1.wait_for_count(dev1.wait_count() + 1U)) return false;
        if (fragmented) {
            const std::size_t split = 2U * TaggedTsDemux::kPacketSize;
            const std::size_t first_wait = dev1.wait_count();
            dev1.push(std::vector<std::uint8_t>(input.begin(), input.begin() + split));
            if (!dev1.wait_for_count(first_wait + 1U)) return false;
            const std::size_t second_wait = dev1.wait_count();
            dev1.push(std::vector<std::uint8_t>(input.begin() + split, input.end()));
            if (!dev1.wait_for_count(second_wait + 1U)) return false;
        } else {
            const std::size_t baseline = dev1.wait_count();
            dev1.push(std::move(input));
            if (!dev1.wait_for_count(baseline + 1U)) return false;
        }
        std::vector<std::uint8_t> output(Q3U4StreamDataPlane::kMaxReadBytes);
        const auto read = plane.read(value,
                                     MutableByteView{output.data(), output.size()},
                                     Timeout{1000U});
        const auto counters = plane.stats(value);
        const bool good = read && read.value().bytes == expected_bytes && counters &&
                          counters.value().continuity_errors == expected_errors &&
                          plane.detach(value);
        if (!good) (void)plane.detach(value);
        return good;
    };

    std::vector<std::uint8_t> normal;
    append_packet(normal, wire_packet(3U, 0x101U, 14U));
    append_packet(normal, wire_packet(3U, 0x101U, 15U));
    append_packet(normal, wire_packet(3U, 0x101U, 0U));
    append_packet(normal, wire_packet(3U, 0x101U, 1U));
    STREAM_CHECK(run_case(90U, normal, 3U, 0U, false));

    std::vector<std::uint8_t> duplicate_gap;
    append_packet(duplicate_gap, wire_packet(3U, 0x101U, 15U));
    append_packet(duplicate_gap, wire_packet(3U, 0x101U, 0U));
    append_packet(duplicate_gap, wire_packet(3U, 0x101U, 1U));
    append_packet(duplicate_gap, wire_packet(3U, 0x101U, 1U));
    append_packet(duplicate_gap, wire_packet(3U, 0x101U, 3U));
    append_packet(duplicate_gap, wire_packet(3U, 0x101U, 4U));
    STREAM_CHECK(run_case(91U, duplicate_gap, 5U, 2U, false));

    std::vector<std::uint8_t> adaptation_only;
    append_packet(adaptation_only, wire_packet(3U, 0x101U, 1U));
    append_packet(adaptation_only, wire_packet(3U, 0x101U, 9U, 2U));
    append_packet(adaptation_only, wire_packet(3U, 0x101U, 2U));
    append_packet(adaptation_only, wire_packet(3U, 0x101U, 3U));
    STREAM_CHECK(run_case(92U, adaptation_only, 3U, 0U, false));

    std::vector<std::uint8_t> discontinuity;
    append_packet(discontinuity, wire_packet(3U, 0x101U, 1U));
    append_packet(discontinuity, wire_packet(3U, 0x101U, 10U, 3U, true));
    append_packet(discontinuity, wire_packet(3U, 0x101U, 11U));
    append_packet(discontinuity, wire_packet(3U, 0x101U, 12U));
    STREAM_CHECK(run_case(93U, discontinuity, 3U, 0U, false));

    std::vector<std::uint8_t> discontinuity_gap;
    append_packet(discontinuity_gap, wire_packet(3U, 0x101U, 1U));
    append_packet(discontinuity_gap, wire_packet(3U, 0x101U, 10U, 3U, true));
    append_packet(discontinuity_gap, wire_packet(3U, 0x101U, 4U));
    append_packet(discontinuity_gap, wire_packet(3U, 0x101U, 5U));
    STREAM_CHECK(run_case(96U, discontinuity_gap, 3U, 1U, false));

    std::vector<std::uint8_t> separate_pids;
    append_packet(separate_pids, wire_packet(3U, 0x101U, 7U));
    append_packet(separate_pids, wire_packet(3U, 0x102U, 3U));
    append_packet(separate_pids, wire_packet(3U, 0x101U, 8U));
    append_packet(separate_pids, wire_packet(3U, 0x102U, 4U));
    STREAM_CHECK(run_case(94U, separate_pids, 2U, 0U, true));

    // A stale PSB boundary packet followed by the live multiplex is not
    // emitted.  Continuity becomes established only at the first consecutive
    // transition; later gaps are still counted normally.
    std::vector<std::uint8_t> stale_boundary;
    append_packet(stale_boundary, wire_packet(3U, 0x101U, 6U));
    append_packet(stale_boundary, wire_packet(3U, 0x101U, 1U));
    append_packet(stale_boundary, wire_packet(3U, 0x101U, 2U));
    append_packet(stale_boundary, wire_packet(3U, 0x101U, 4U));
    STREAM_CHECK(run_case(97U, stale_boundary, 2U, 1U, false));

    std::vector<std::uint8_t> null_packets;
    append_packet(null_packets, wire_packet(3U, 0x1fffU, 0U));
    append_packet(null_packets, wire_packet(3U, 0x1fffU, 0U));
    append_packet(null_packets, wire_packet(3U, 0x1fffU, 9U));
    append_packet(null_packets, wire_packet(3U, 0x1fffU, 3U));
    STREAM_CHECK(run_case(98U, null_packets, 4U, 0U, false));

    const auto reset = attachment(2U, 95U);
    STREAM_CHECK(plane.attach(reset));
    const auto reset_stats = plane.stats(reset);
    STREAM_CHECK(reset_stats && reset_stats.value().packets == 0U &&
                 reset_stats.value().continuity_errors == 0U);
    STREAM_CHECK(plane.detach(reset));
    return true;
}

bool test_queue_bounds_and_reader_wakeup()
{
    FakeTransport dev1;
    FakeTransport dev2;
    const auto defaults = create_test_plane(dev1, dev2);
    STREAM_CHECK(defaults);
    const auto maximum = create_test_plane(
        dev1, dev2, Q3U4StreamDataPlane::kMaxQueuePackets);
    STREAM_CHECK(maximum);
    STREAM_CHECK(!create_test_plane(dev1, dev2,
                                               Q3U4StreamDataPlane::kMinQueuePackets - 1U));
    STREAM_CHECK(!create_test_plane(dev1, dev2,
                                               Q3U4StreamDataPlane::kMaxQueuePackets + 1U));

    auto created = create_test_plane(dev1, dev2,
                                                Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto value = attachment(0U, 70U);
    STREAM_CHECK(plane.attach(value));

    return true;
}

struct ReadWaitBarrier final {
    static void observe(void* context, std::uint8_t) noexcept
    {
        auto& barrier = *static_cast<ReadWaitBarrier*>(context);
        std::unique_lock<std::mutex> lock(barrier.mutex);
        barrier.entered = true;
        barrier.changed.notify_all();
        barrier.changed.wait(lock, [&]() noexcept { return barrier.allow; });
        barrier.returned = true;
        barrier.changed.notify_all();
    }

    bool wait_until_entered() noexcept
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(5), [&]() noexcept {
            return entered;
        });
    }

    bool wait_until_returned() noexcept
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(5), [&]() noexcept {
            return returned;
        });
    }

    void release() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        allow = true;
        changed.notify_all();
    }

    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool returned = false;
    bool allow = false;
};

bool test_reader_data_and_shutdown_wakeup()
{
    auto run_case = [](std::uint64_t id, std::uint8_t action) noexcept {
        FakeTransport dev1;
        FakeTransport dev2;
        auto created = create_test_plane(
            dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets);
        if (!created) return false;
        auto& plane = *created.value();
        const auto value = attachment(0U, id);
        if (!plane.attach(value) || !dev1.wait_for_count(1U)) return false;
        ReadWaitBarrier barrier;
        plane.set_read_wait_observer_for_test(&ReadWaitBarrier::observe, &barrier);
        std::array<std::uint8_t, 188U> output{};
        StreamReadResult result{};
        bool read_ok = false;
        std::thread reader([&]() {
            const auto read = plane.read(
                value, MutableByteView{output.data(), output.size()}, Timeout{5000U});
            if (read) {
                result = read.value();
                read_ok = true;
            }
        });
        if (!barrier.wait_until_entered()) {
            barrier.release();
            (void)plane.detach(value);
            reader.join();
            return false;
        }
        barrier.release();
        if (!barrier.wait_until_returned()) {
            (void)plane.detach(value);
            reader.join();
            return false;
        }
        bool operation_ok = true;
        if (action == 0U) {
            dev1.push(stream(1U, 4U));
        } else if (action == 1U) {
            operation_ok = static_cast<bool>(plane.detach(value));
        } else if (action == 2U) {
            dev1.fail(Error::USB_IO);
        } else {
            operation_ok = static_cast<bool>(plane.shutdown());
        }
        reader.join();
        plane.set_read_wait_observer_for_test(nullptr, nullptr);
        if (!operation_ok || !read_ok) return false;
        if (action == 0U)
            return result.bytes == output.size() && !result.eof && !result.timed_out;
        if (action == 1U)
            return result.eof && !result.timed_out &&
                   result.terminal == StreamTerminal::stopped;
        if (action == 2U) {
            const auto stats = plane.stats(value);
            const bool counters_ok = stats && stats.value().usb_errors >= 1U;
            const auto detached = plane.detach(value);
            return result.eof && result.terminal == StreamTerminal::usb_error &&
                   counters_ok && detached;
        }
        return result.eof && !result.timed_out &&
               result.terminal == StreamTerminal::stopped && plane.shutdown();
    };

    STREAM_CHECK(run_case(72U, 0U));  // data wake
    STREAM_CHECK(run_case(73U, 1U));  // detach wake
    STREAM_CHECK(run_case(74U, 2U));  // USB error wake
    STREAM_CHECK(run_case(75U, 3U));  // shutdown wake
    return true;
}

bool test_start_failure_and_stale_replacement()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = create_test_plane(dev1, dev2,
                                                Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto first = attachment(0U, 74U);
    dev1.start_error = Error::USB_IO;
    STREAM_CHECK(plane.attach(first).error() == Error::USB_IO);
    STREAM_CHECK(dev1.starts == 1U && dev1.cancels == 0U && dev1.stops == 0U);

    dev1.start_error = Error::OK;
    STREAM_CHECK(plane.attach(first));
    STREAM_CHECK(plane.detach(first));
    const auto replacement = attachment(0U, 75U);
    STREAM_CHECK(plane.attach(replacement));
    std::array<std::uint8_t, 188U> output{};
    STREAM_CHECK(plane.read(first, MutableByteView{output.data(), output.size()},
                            Timeout{0U}).error() == Error::NOT_FOUND);
    STREAM_CHECK(plane.detach(first).error() == Error::NOT_FOUND);
    STREAM_CHECK(plane.detach(replacement));
    return true;
}

bool test_wire_tei_and_bridge_sync_counter()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = create_test_plane(dev1, dev2,
                                                Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto value = attachment(0U, 9U);
    const auto sibling = attachment(1U, 19U);
    STREAM_CHECK(plane.attach(value));
    STREAM_CHECK(plane.attach(sibling));

    const std::size_t timeout_baseline = dev1.wait_count();
    dev1.fail(Error::TIMEOUT);
    // The second wait entry is the processing barrier: the first call has
    // returned TIMEOUT and the pump has already accounted for it.
    STREAM_CHECK(dev1.wait_for_count(timeout_baseline + 2U));
    const auto timeout_stats = plane.stats(value);
    STREAM_CHECK(timeout_stats && timeout_stats.value().empty_intervals >= 1U);
    const std::size_t zero_baseline = dev1.wait_count();
    dev1.push(std::vector<std::uint8_t>{});
    STREAM_CHECK(dev1.wait_for_count(zero_baseline + 2U));
    const auto zero_stats = plane.stats(value);
    STREAM_CHECK(zero_stats && zero_stats.value().empty_intervals >= 2U);

    auto tagged = stream(1U, 4U);
    const auto wire_tei = wire_packet(9U, 0x101U, 4U);
    tagged.insert(tagged.end(), wire_tei.begin(), wire_tei.end());
    const auto standard_tei = wire_packet(1U, 0x101U, 5U, 1U, false, true);
    tagged.insert(tagged.end(), standard_tei.begin(), standard_tei.end());
    const std::size_t first_processed_call = dev1.wait_count() + 2U;
    dev1.push(std::move(tagged));
    STREAM_CHECK(dev1.wait_for_count(first_processed_call));
    std::array<std::uint8_t, 188U> output{};
    const auto first_read = plane.read(value, MutableByteView{output.data(), output.size()},
                                       Timeout{1000U});
    STREAM_CHECK(first_read);
    const auto before = plane.stats(value);
    STREAM_CHECK(before && before.value().tei_packets >= 2U);

    const std::size_t short_baseline = dev1.wait_count();
    dev1.push_short(stream(2U, 2U));
    STREAM_CHECK(dev1.wait_for_count(short_baseline + 1U));
    const auto sibling_read = plane.read(
        sibling, MutableByteView{output.data(), output.size()}, Timeout{1000U});
    STREAM_CHECK(sibling_read && sibling_read.value().bytes == output.size());
    const auto sibling_stats = plane.stats(sibling);
    STREAM_CHECK(sibling_stats && sibling_stats.value().packets == 2U);

    const std::size_t empty_baseline = dev1.wait_count();
    dev1.push(std::vector<std::uint8_t>{});
    STREAM_CHECK(dev1.wait_for_count(empty_baseline + 1U));
    const auto empty_stats = plane.stats(value);
    STREAM_CHECK(empty_stats && empty_stats.value().empty_intervals >= 1U);

    auto resync = stream(1U, 5U);
    resync[0U] = 0x00U;
    const std::size_t second_processed_call = dev1.wait_count() + 2U;
    dev1.push(std::move(resync));
    STREAM_CHECK(dev1.wait_for_count(second_processed_call));
    const auto second_read = plane.read(value, MutableByteView{output.data(), output.size()},
                                        Timeout{1000U});
    STREAM_CHECK(second_read);
    const auto after = plane.stats(value);
    STREAM_CHECK(after && after.value().sync_errors >= 1U);
    const auto sibling_after = plane.stats(sibling);
    STREAM_CHECK(sibling_after && sibling_after.value().sync_errors >= 1U);
    const auto sync_errors = plane.bridge_sync_errors(0U);
    STREAM_CHECK(sync_errors && sync_errors.value() >= 1U);
    STREAM_CHECK(plane.detach(value));
    STREAM_CHECK(plane.detach(sibling));
    return true;
}

bool test_initial_sync_recovery_is_session_boundary()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = create_test_plane(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto value = attachment(0U, 109U);
    STREAM_CHECK(plane.attach(value));

    std::vector<std::uint8_t> initial;
    for (std::uint8_t counter = 0U; counter < 4U; ++counter)
        append_packet(initial, wire_packet(5U, 0x101U, counter));
    initial.push_back(0U);
    auto valid = stream(1U, 4U);
    initial.insert(initial.end(), valid.begin(), valid.end());
    const std::size_t initial_barrier = dev1.wait_count() + 2U;
    dev1.push(std::move(initial));
    STREAM_CHECK(dev1.wait_for_count(initial_barrier));
    const auto initial_stats = plane.stats(value);
    STREAM_CHECK(initial_stats && initial_stats.value().packets == 4U &&
                 initial_stats.value().sync_errors == 0U);
    const auto bridge_initial = plane.bridge_sync_errors(0U);
    STREAM_CHECK(bridge_initial && bridge_initial.value() == 1U);

    auto damaged = stream(1U, 5U);
    damaged[0U] = 0U;
    const std::size_t damaged_barrier = dev1.wait_count() + 2U;
    dev1.push(std::move(damaged));
    STREAM_CHECK(dev1.wait_for_count(damaged_barrier));
    const auto damaged_stats = plane.stats(value);
    STREAM_CHECK(damaged_stats && damaged_stats.value().sync_errors == 1U);
    STREAM_CHECK(plane.detach(value));
    return true;
}

bool test_capture_startup_stabilization()
{
    FakeTransport dev1;
    FakeTransport dev2;
    // Use the release policy, not the disabled legacy-test policy.  The
    // observed reference-driver fault tail ended at packet 10793; production
    // must still be withholding output at packet 12000 and promote only after
    // the larger clean boundary has been demonstrated.
    auto created = Q3U4StreamDataPlane::create(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto value = attachment(2U, 110U);
    STREAM_CHECK(plane.attach(value));
    STREAM_CHECK(dev1.wait_for_count(1U));

    constexpr std::size_t kFaultTail = 10793U;
    constexpr std::size_t kFirstObservation = 12000U;
    constexpr std::size_t kProductionBoundary = 16384U;
    std::size_t packet_index = 0U;
    std::uint8_t next_cc = 0U;
    auto push_until = [&](std::size_t target) noexcept {
        while (packet_index < target) {
            const std::size_t remaining = target - packet_index;
            const std::size_t count = remaining > 800U ? 800U : remaining;
            std::vector<std::uint8_t> bytes;
            bytes.reserve(count * TaggedTsDemux::kPacketSize);
            for (std::size_t item = 0U; item < count; ++item, ++packet_index) {
                const std::size_t index = packet_index;
                std::uint8_t wire_tag = 3U;
                bool standard_tei = false;
                if (index == 500U) wire_tag = 11U;  // bit-7 wire TEI for local tag 3
                if (index == 700U) standard_tei = true;
                if (index == kFaultTail)
                    next_cc = static_cast<std::uint8_t>((next_cc + 2U) & 0x0fU);
                append_packet(bytes, wire_packet(wire_tag, 0x101U, next_cc,
                                                 1U, false, standard_tei));
                next_cc = static_cast<std::uint8_t>((next_cc + 1U) & 0x0fU);
            }
            const std::size_t barrier = dev1.wait_count() + 1U;
            dev1.push(std::move(bytes));
            if (!dev1.wait_for_count(barrier)) return false;
        }
        return true;
    };

    STREAM_CHECK(push_until(kFirstObservation));
    std::array<std::uint8_t, 4U * TaggedTsDemux::kPacketSize> output{};
    const auto withheld = plane.read(
        value, MutableByteView{output.data(), output.size()}, Timeout{0U});
    STREAM_CHECK(withheld && withheld.value().bytes == 0U &&
                 withheld.value().timed_out && !withheld.value().eof);
    const auto startup_stats = plane.stats(value);
    STREAM_CHECK(startup_stats && startup_stats.value().packets == 0U &&
                 startup_stats.value().tei_packets == 0U &&
                 startup_stats.value().continuity_errors == 0U &&
                 startup_stats.value().sync_errors == 0U);

    STREAM_CHECK(push_until(kProductionBoundary));
    const auto boundary = plane.read(
        value, MutableByteView{output.data(), output.size()}, Timeout{0U});
    STREAM_CHECK(boundary && boundary.value().bytes == 0U &&
                 boundary.value().timed_out);

    // Publication begins only after the stable boundary.  The first packet is
    // the ordinary session baseline; the following packets are delivered.
    std::vector<std::uint8_t> published;
    for (std::size_t index = 0U; index < 3U; ++index) {
        append_packet(published, wire_packet(3U, 0x101U, next_cc));
        next_cc = static_cast<std::uint8_t>((next_cc + 1U) & 0x0fU);
    }
    std::size_t barrier = dev1.wait_count() + 1U;
    dev1.push(std::move(published));
    STREAM_CHECK(dev1.wait_for_count(barrier));
    const auto clean_read = plane.read(
        value, MutableByteView{output.data(), output.size()}, Timeout{1000U});
    STREAM_CHECK(clean_read && clean_read.value().bytes == 2U * 188U);
    auto clean_stats = plane.stats(value);
    STREAM_CHECK(clean_stats && clean_stats.value().packets == 2U &&
                 clean_stats.value().tei_packets == 0U &&
                 clean_stats.value().continuity_errors == 0U &&
                 clean_stats.value().sync_errors == 0U);

    // Once the publication boundary exists, every integrity observation is
    // preserved.  Standard TEI remains in the TS, wire TEI is discarded but
    // counted, and a CC gap plus bridge resync are both reported.
    std::vector<std::uint8_t> damaged;
    append_packet(damaged, wire_packet(3U, 0x101U, next_cc, 1U, false, true));
    next_cc = static_cast<std::uint8_t>((next_cc + 1U) & 0x0fU);
    append_packet(damaged, wire_packet(11U, 0x101U, next_cc));
    next_cc = static_cast<std::uint8_t>((next_cc + 1U) & 0x0fU);
    next_cc = static_cast<std::uint8_t>((next_cc + 1U) & 0x0fU);
    append_packet(damaged, wire_packet(3U, 0x101U, next_cc));
    next_cc = static_cast<std::uint8_t>((next_cc + 1U) & 0x0fU);
    barrier = dev1.wait_count() + 1U;
    dev1.push(std::move(damaged));
    STREAM_CHECK(dev1.wait_for_count(barrier));

    std::vector<std::uint8_t> resync;
    for (std::size_t index = 0U; index < 4U; ++index)
        append_packet(resync, wire_packet(5U, 0x120U,
                                          static_cast<std::uint8_t>(index)));
    resync.push_back(0U);
    for (std::size_t index = 0U; index < 5U; ++index) {
        append_packet(resync, wire_packet(3U, 0x101U, next_cc));
        next_cc = static_cast<std::uint8_t>((next_cc + 1U) & 0x0fU);
    }
    barrier = dev1.wait_count() + 1U;
    dev1.push(std::move(resync));
    STREAM_CHECK(dev1.wait_for_count(barrier));
    const auto damaged_stats = plane.stats(value);
    STREAM_CHECK(damaged_stats && damaged_stats.value().tei_packets == 2U &&
                 damaged_stats.value().continuity_errors >= 1U &&
                 damaged_stats.value().sync_errors == 1U);
    STREAM_CHECK(plane.detach(value));
    return true;
}

bool test_startup_sync_deferral_and_failure_bound()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{8U, 4U, 32U});
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto value = attachment(0U, 111U);
    STREAM_CHECK(plane.attach(value));
    STREAM_CHECK(dev1.wait_for_count(1U));

    // The target receiver becomes tentatively ready in this USB event, but a
    // later bridge resync in the same event resets its clean tail before the
    // deferred promotion point.
    std::vector<std::uint8_t> initial;
    for (std::size_t index = 0U; index < 4U; ++index)
        append_packet(initial, wire_packet(5U, 0x120U,
                                           static_cast<std::uint8_t>(index)));
    initial.push_back(0U);
    for (std::size_t index = 0U; index < 8U; ++index)
        append_packet(initial, wire_packet(1U, 0x101U,
                                           static_cast<std::uint8_t>(index)));
    std::size_t barrier = dev1.wait_count() + 1U;
    dev1.push(std::move(initial));
    STREAM_CHECK(dev1.wait_for_count(barrier));
    std::array<std::uint8_t, 188U> output{};
    auto read = plane.read(value, MutableByteView{output.data(), output.size()},
                           Timeout{0U});
    STREAM_CHECK(read && read.value().timed_out);

    std::vector<std::uint8_t> clean;
    for (std::size_t index = 8U; index < 12U; ++index)
        append_packet(clean, wire_packet(1U, 0x101U,
                                         static_cast<std::uint8_t>(index)));
    barrier = dev1.wait_count() + 1U;
    dev1.push(std::move(clean));
    STREAM_CHECK(dev1.wait_for_count(barrier));
    const auto promoted_stats = plane.stats(value);
    STREAM_CHECK(promoted_stats && promoted_stats.value().sync_errors == 0U);
    STREAM_CHECK(plane.detach(value));

    FakeTransport bad_dev1;
    FakeTransport bad_dev2;
    auto failed = Q3U4StreamDataPlane::create_for_test(
        bad_dev1, bad_dev2, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{4U, 2U, 8U});
    STREAM_CHECK(failed);
    auto& bad_plane = *failed.value();
    const auto bad = attachment(0U, 112U);
    STREAM_CHECK(bad_plane.attach(bad));
    STREAM_CHECK(bad_dev1.wait_for_count(1U));
    std::vector<std::uint8_t> never_stable;
    for (std::size_t index = 0U; index < 8U; ++index)
        append_packet(never_stable, wire_packet(
            1U, 0x101U, static_cast<std::uint8_t>((index * 2U) & 0x0fU)));
    barrier = bad_dev1.wait_count() + 1U;
    bad_dev1.push(std::move(never_stable));
    STREAM_CHECK(bad_dev1.wait_for_count(barrier));
    read = bad_plane.read(bad, MutableByteView{output.data(), output.size()},
                          Timeout{1000U});
    STREAM_CHECK(read && read.value().eof &&
                 read.value().terminal == StreamTerminal::sync_error);
    const auto failed_stats = bad_plane.stats(bad);
    STREAM_CHECK(failed_stats && failed_stats.value().sync_errors == 1U &&
                 failed_stats.value().packets == 0U);
    STREAM_CHECK(bad_plane.detach(bad));
    return true;
}

bool test_fatal_wakeup_and_validation()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = create_test_plane(dev1, dev2,
                                                Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto value = attachment(2U, 8U);
    const auto other = attachment(4U, 84U);
    STREAM_CHECK(plane.attach(value));
    STREAM_CHECK(plane.attach(other));
    std::array<std::uint8_t, 188U> output{};
    STREAM_CHECK(plane.read(value, MutableByteView{output.data(), 0U}, Timeout{0U}).error() ==
                 Error::INVALID_ARGUMENT);
    dev1.fail(Error::USB_IO);
    const auto result = plane.read(value, MutableByteView{output.data(), output.size()},
                                   Timeout{1000U});
    STREAM_CHECK(result && result.value().eof);
    STREAM_CHECK(result.value().terminal == StreamTerminal::usb_error);
    STREAM_CHECK(dev2.wait_for_count(1U));
    const std::size_t other_baseline = dev2.wait_count();
    dev2.push(stream(1U, 4U));
    STREAM_CHECK(dev2.wait_for_count(other_baseline + 1U));
    const auto other_read = plane.read(other,
                                       MutableByteView{output.data(), output.size()},
                                       Timeout{1000U});
    STREAM_CHECK(other_read && other_read.value().bytes == output.size());
    STREAM_CHECK(plane.detach(value));
    STREAM_CHECK(plane.detach(other));
    STREAM_CHECK(dev1.stops == 1U);
    return true;
}

bool test_fatal_epoch_and_cleanup_ownership()
{
    FakeTransport dev1;
    FakeTransport dev2;
    auto created = create_test_plane(dev1, dev2,
                                                Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(created);
    auto& plane = *created.value();
    const auto first = attachment(0U, 80U);
    const auto sibling = attachment(1U, 81U);
    const auto later = attachment(2U, 82U);
    STREAM_CHECK(plane.attach(first));
    STREAM_CHECK(plane.attach(sibling));
    dev1.fail(Error::USB_IO);
    std::array<std::uint8_t, 188U> output{};
    const auto result = plane.read(first, MutableByteView{output.data(), output.size()},
                                   Timeout{1000U});
    STREAM_CHECK(result && result.value().eof);
    STREAM_CHECK(!plane.enclosure_fatal());
    STREAM_CHECK(dev1.wait_for_stop_count(1U));
    const auto rejected = plane.attach(later);
    STREAM_CHECK(!rejected && rejected.error() == Error::DISCONNECTED);
    STREAM_CHECK(plane.detach(first));
    const auto first_final = plane.final_snapshot(first);
    STREAM_CHECK(first_final && first_final.value().terminal ==
                 static_cast<std::uint8_t>(StreamTerminal::usb_error));
    STREAM_CHECK(plane.detach(sibling));
    STREAM_CHECK(plane.attach(later));
    STREAM_CHECK(plane.detach(later));

    FakeTransport failing_dev1;
    FakeTransport failing_dev2;
    auto failing = create_test_plane(failing_dev1, failing_dev2,
                                                Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(failing);
    auto& failing_plane = *failing.value();
    const auto failing_attachment = attachment(0U, 83U);
    STREAM_CHECK(failing_plane.attach(failing_attachment));
    failing_dev1.cancel_error = Error::USB_IO;
    const auto stopped = failing_plane.detach(failing_attachment);
    STREAM_CHECK(!stopped && stopped.error() == Error::USB_IO);
    STREAM_CHECK(failing_plane.enclosure_fatal());
    STREAM_CHECK(failing_dev1.cancels == 1U && failing_dev1.stops == 1U);
    const auto failed_final = failing_plane.final_snapshot(failing_attachment);
    STREAM_CHECK(failed_final && failed_final.value().terminal ==
                 static_cast<std::uint8_t>(StreamTerminal::bridge_fatal));

    FakeTransport shutdown_dev1;
    FakeTransport shutdown_dev2;
    auto shutdown_created = create_test_plane(
        shutdown_dev1, shutdown_dev2, Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(shutdown_created);
    auto& shutdown_plane = *shutdown_created.value();
    const auto shutdown_attachment = attachment(0U, 84U);
    STREAM_CHECK(shutdown_plane.attach(shutdown_attachment));
    shutdown_dev1.fail(Error::USB_IO);
    const auto shutdown_read = shutdown_plane.read(
        shutdown_attachment, MutableByteView{output.data(), output.size()}, Timeout{1000U});
    STREAM_CHECK(shutdown_read && shutdown_read.value().terminal ==
                 StreamTerminal::usb_error);
    STREAM_CHECK(shutdown_plane.shutdown());
    const auto shutdown_final = shutdown_plane.final_snapshot(shutdown_attachment);
    STREAM_CHECK(shutdown_final && shutdown_final.value().terminal ==
                 static_cast<std::uint8_t>(StreamTerminal::usb_error));

    FakeTransport cleanup_dev1;
    FakeTransport cleanup_dev2;
    auto cleanup_created = create_test_plane(
        cleanup_dev1, cleanup_dev2, Q3U4StreamDataPlane::kMinQueuePackets);
    STREAM_CHECK(cleanup_created);
    auto& cleanup_plane = *cleanup_created.value();
    const auto cleanup_attachment = attachment(0U, 85U);
    STREAM_CHECK(cleanup_plane.attach(cleanup_attachment));
    cleanup_dev1.cancel_error = Error::USB_IO;
    const auto cleanup_shutdown = cleanup_plane.shutdown();
    STREAM_CHECK(!cleanup_shutdown && cleanup_shutdown.error() == Error::USB_IO);
    const auto cleanup_final = cleanup_plane.final_snapshot(cleanup_attachment);
    STREAM_CHECK(cleanup_final && cleanup_final.value().terminal ==
                 static_cast<std::uint8_t>(StreamTerminal::bridge_fatal));
    return true;
}

bool test_lifecycle_barrier_races()
{
    {
        FakeTransport dev1;
        FakeTransport dev2;
        auto created = create_test_plane(
            dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets);
        if (!created) return false;
        auto& plane = *created.value();
        const auto first = attachment(0U, 101U);
        const auto second = attachment(1U, 102U);
        if (!plane.attach(first) || !dev1.wait_for_count(1U)) return false;
        {
            std::unique_lock<std::mutex> lock(dev1.mutex);
            dev1.block_cancel = true;
        }
        std::atomic<bool> detach_done{false};
        std::thread detach([&]() {
            (void)plane.detach(first);
            detach_done.store(true);
        });
        {
            std::unique_lock<std::mutex> lock(dev1.mutex);
            if (!dev1.changed.wait_for(lock, std::chrono::seconds(5), [&]() noexcept {
                    return dev1.cancel_entered;
                })) {
                dev1.allow_cancel = true;
                dev1.changed.notify_all();
                lock.unlock();
                detach.join();
                return false;
            }
            if (dev1.starts != 1U || detach_done.load()) {
                dev1.allow_cancel = true;
                dev1.changed.notify_all();
                lock.unlock();
                detach.join();
                return false;
            }
        }
        std::atomic<bool> attach_done{false};
        std::thread attach([&]() {
            (void)plane.attach(second);
            attach_done.store(true);
        });
        // The stopping lifecycle state, not a timing assumption, prevents a
        // new epoch from starting while the old cancel/join is blocked.
        {
            std::unique_lock<std::mutex> lock(dev1.mutex);
            if (attach_done.load() || dev1.starts != 1U) {
                dev1.allow_cancel = true;
                dev1.changed.notify_all();
                lock.unlock();
                detach.join();
                attach.join();
                return false;
            }
            dev1.allow_cancel = true;
            dev1.changed.notify_all();
        }
        detach.join();
        attach.join();
        bool good = detach_done.load() && attach_done.load();
        {
            std::lock_guard<std::mutex> lock(dev1.mutex);
            good = good && dev1.starts == 2U && dev1.cancels == 1U &&
                   dev1.stops == 1U;
        }
        if (!good) return false;
        if (!plane.detach(second)) return false;
        {
            std::lock_guard<std::mutex> lock(dev1.mutex);
            if (dev1.cancels != 2U || dev1.stops != 2U) return false;
        }
    }

    {
        FakeTransport dev1;
        FakeTransport dev2;
        auto created = create_test_plane(
            dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets);
        if (!created) return false;
        auto& plane = *created.value();
        const auto value = attachment(0U, 103U);
        if (!plane.attach(value) || !dev1.wait_for_count(1U)) return false;
        {
            std::lock_guard<std::mutex> lock(dev1.mutex);
            dev1.block_cancel = true;
        }
        dev1.fail(Error::USB_IO);
        if (!dev1.wait_for_cancel_count(1U)) {
            std::lock_guard<std::mutex> lock(dev1.mutex);
            dev1.allow_cancel = true;
            dev1.changed.notify_all();
            return false;
        }
        std::thread detach([&]() { (void)plane.detach(value); });
        std::thread shutdown([&]() { (void)plane.shutdown(); });
        {
            std::lock_guard<std::mutex> lock(dev1.mutex);
            dev1.allow_cancel = true;
            dev1.changed.notify_all();
        }
        detach.join();
        shutdown.join();
        bool good = true;
        {
            std::lock_guard<std::mutex> lock(dev1.mutex);
            good = dev1.cancels == 1U && dev1.stops == 1U;
        }
        if (!good) return false;
    }
    return true;
}

}  // namespace

bool run_q3u4_stream_tests()
{
    const bool mapping = test_mapping_and_bridge_lifecycle();
    if (!mapping) std::fprintf(stderr, "q3u4_stream: mapping failed\n");
    const bool drain = mapping && test_detach_drain_and_final_release();
    if (mapping && !drain) std::fprintf(stderr, "q3u4_stream: drain failed\n");
    const bool queue = drain && test_queue_counters_and_terminal();
    if (mapping && !queue) std::fprintf(stderr, "q3u4_stream: queue failed\n");
    const bool continuity = queue && test_continuity_and_epoch_reset();
    if (queue && !continuity) std::fprintf(stderr, "q3u4_stream: continuity failed\n");
    const bool reader = continuity && test_reader_data_and_shutdown_wakeup();
    if (continuity && !reader) std::fprintf(stderr, "q3u4_stream: reader failed\n");
    const bool replacement = reader && test_start_failure_and_stale_replacement();
    if (reader && !replacement)
        std::fprintf(stderr, "q3u4_stream: replacement failed\n");
    const bool wake = replacement && test_queue_bounds_and_reader_wakeup();
    if (replacement && !wake) std::fprintf(stderr, "q3u4_stream: wake failed\n");
    const bool tei = wake && test_wire_tei_and_bridge_sync_counter();
    if (wake && !tei) std::fprintf(stderr, "q3u4_stream: tei failed\n");
    const bool initial_sync = tei && test_initial_sync_recovery_is_session_boundary();
    if (tei && !initial_sync)
        std::fprintf(stderr, "q3u4_stream: initial sync boundary failed\n");
    const bool stabilization = initial_sync && test_capture_startup_stabilization();
    if (initial_sync && !stabilization)
        std::fprintf(stderr, "q3u4_stream: startup stabilization failed\n");
    const bool stabilization_bound =
        stabilization && test_startup_sync_deferral_and_failure_bound();
    if (stabilization && !stabilization_bound)
        std::fprintf(stderr, "q3u4_stream: startup bound failed\n");
    const bool fatal = stabilization_bound && test_fatal_wakeup_and_validation();
    if (stabilization_bound && !fatal)
        std::fprintf(stderr, "q3u4_stream: fatal wake failed\n");
    const bool epoch = fatal && test_fatal_epoch_and_cleanup_ownership();
    if (fatal && !epoch) std::fprintf(stderr, "q3u4_stream: epoch failed\n");
    const bool races = epoch && test_lifecycle_barrier_races();
    if (epoch && !races) std::fprintf(stderr, "q3u4_stream: lifecycle races failed\n");
    return races;
}

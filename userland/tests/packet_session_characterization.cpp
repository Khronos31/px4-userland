// SPDX-License-Identifier: GPL-2.0-only
//
// D2a packet-session ordinary characterization harness.
//
// This file is the SAME harness source compiled twice: once against the fixed
// old data plane copy (userland/tests/packet_session_fixed_old.cpp, a
// source-only instrumented copy of the P 90f08a59 q3u4_stream.cpp) and once
// against the extracted common component through the real q3u4_stream.cpp.
// It uses the actual Transport interface with a fake transport and the real
// startup test hooks; it never substitutes a mock queue for the data plane.
//
// The ordinary trace must be byte-identical between the two builds.  The
// separate cross-session blocked-reader race is proven by the independent
// instrumented-old evidence (reports/packet-session-old-race-proof.md and
// reports/capture-new-proof-luna.md); it is deliberately not duplicated here.

#include "px4/q3u4_stream.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;

constexpr std::size_t kPacketSize = 188U;

// Deterministic output accumulator: every emitted line feeds an FNV-1a digest
// so old and new builds can be compared by digest and by raw text.
std::uint64_t g_digest = 1469598103934665603ULL;

void emit(const char* format, ...) noexcept
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    std::printf("%s\n", buffer);
    const int length = written < 0
        ? 0
        : (written < static_cast<int>(sizeof(buffer)) ? written
                                                       : static_cast<int>(sizeof(buffer)) - 1);
    for (int index = 0; index < length; ++index) {
        g_digest ^= static_cast<std::uint8_t>(buffer[index]);
        g_digest *= 1099511628211ULL;
    }
    g_digest ^= '\n';
    g_digest *= 1099511628211ULL;
}

std::uint64_t bytes_digest(const std::uint8_t* data, std::size_t size) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0U; index < size; ++index) {
        hash ^= data[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

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
        if (start_error != Error::OK) return Result<void>::failure(start_error);
        active = true;
        cancelled = false;
        return Result<void>::success();
    }

    Result<StreamEvent> wait_stream(Timeout timeout) noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex);
        ++wait_calls;
        wait_changed.notify_all();
        (void)changed.wait_for(
            lock, std::chrono::milliseconds(timeout.milliseconds),
            [&]() noexcept { return cancelled || wait_error != Error::OK || !events.empty(); });
        if (cancelled) return Result<StreamEvent>::failure(Error::DISCONNECTED);
        if (wait_error != Error::OK) {
            const Error error = wait_error;
            wait_error = Error::OK;
            return Result<StreamEvent>::failure(error);
        }
        if (events.empty()) return Result<StreamEvent>::failure(Error::TIMEOUT);
        current = std::move(events.front());
        events.pop_front();
        return Result<StreamEvent>::success(
            StreamEvent{StreamEventKind::data, current.data(), current.size()});
    }

    Result<void> cancel_stream() noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++cancels;
        cancelled = true;
        changed.notify_all();
        return cancel_error == Error::OK ? Result<void>::success()
                                         : Result<void>::failure(cancel_error);
    }

    Result<void> stop_stream() noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++stops;
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
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(std::move(value));
        changed.notify_all();
    }

    void fail(Error error) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        wait_error = error;
        changed.notify_all();
    }

    bool wait_for_count(std::size_t count) noexcept
    {
        std::unique_lock<std::mutex> lock(mutex);
        return wait_changed.wait_for(lock, std::chrono::seconds(5),
                                     [&]() noexcept { return wait_calls >= count; });
    }

    // Synchronized snapshot of the wait counter.  The pump increments
    // wait_calls under this same mutex, so the observer must read it here
    // rather than through a plain unsynchronized member read.
    std::size_t wait_count() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        return wait_calls;
    }

    mutable std::mutex mutex;
    std::condition_variable changed;
    std::condition_variable wait_changed;
    std::deque<std::vector<std::uint8_t>> events;
    std::vector<std::uint8_t> current;
    StreamConfig last_config{};
    Error start_error = Error::OK;
    Error cancel_error = Error::OK;
    Error stop_error = Error::OK;
    Error wait_error = Error::OK;
    std::size_t starts = 0U;
    std::size_t cancels = 0U;
    std::size_t stops = 0U;
    std::size_t wait_calls = 0U;
    bool active = false;
    bool cancelled = false;
};

TunerAttachment attachment(std::uint8_t receiver, std::uint64_t id) noexcept
{
    TunerAttachment value{};
    value.owner_client_id = 1U;
    value.lease_id = id;
    value.attachment_id = id + 100U;
    value.receiver = receiver;
    value.system = receiver < 2U ? ipc::System::ISDB_S : ipc::System::ISDB_T;
    value.nonce[0U] = static_cast<std::uint8_t>(id);
    return value;
}

std::array<std::uint8_t, kPacketSize> wire_packet(std::uint8_t tag, std::uint16_t pid,
                                                  std::uint8_t cc) noexcept
{
    std::array<std::uint8_t, kPacketSize> packet{};
    packet[0U] = static_cast<std::uint8_t>((tag << 4U) | 0x07U);
    packet[1U] = static_cast<std::uint8_t>((pid >> 8U) & 0x1fU);
    packet[2U] = static_cast<std::uint8_t>(pid);
    packet[3U] = static_cast<std::uint8_t>((1U << 4U) | (cc & 0x0fU));
    packet[4U] = static_cast<std::uint8_t>(tag);
    return packet;
}

std::vector<std::uint8_t> stream(std::uint8_t tag, std::size_t count,
                                 std::uint16_t pid = 0x101U) noexcept
{
    std::vector<std::uint8_t> result;
    result.reserve((count + 1U) * kPacketSize);
    for (std::size_t index = 0U; index <= count; ++index) {
        const auto packet = wire_packet(tag, pid, static_cast<std::uint8_t>(index));
        result.insert(result.end(), packet.begin(), packet.end());
    }
    return result;
}

Result<std::unique_ptr<Q3U4StreamDataPlane>> create_test_plane(
    FakeTransport& dev1, FakeTransport& dev2,
    std::size_t queue_packets = Q3U4StreamDataPlane::kMinQueuePackets) noexcept
{
    return Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, queue_packets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
}

// Deterministic count of packets the producer enqueued.  The enqueue test seam
// fires after each accounting/append decision, so it counts stored packets and
// overflow drops alike.  It only observes; it never touches the queue.
struct EnqueueGate final {
    std::atomic<std::size_t> count{0U};
};

void enqueue_observer(void* context, std::uint8_t) noexcept
{
    auto& gate = *static_cast<EnqueueGate*>(context);
    gate.count.fetch_add(1U, std::memory_order_relaxed);
}

bool wait_for_enqueues(EnqueueGate& gate, std::size_t target) noexcept
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (gate.count.load(std::memory_order_acquire) < target) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// A packet as published into the ring: the demux rewrites the wire sync byte to
// the plain 0x47 header before the session stores it.
std::array<std::uint8_t, kPacketSize> published_packet(std::uint8_t tag,
                                                       std::uint16_t pid,
                                                       std::uint8_t cc) noexcept
{
    auto packet = wire_packet(tag, pid, cc);
    packet[0U] = 0x47U;
    return packet;
}

// Case 1: two receivers sharing one bridge, read round trip, stats, final,
// stale-identity rejection.
bool case_shared_bridge_and_final()
{
    FakeTransport dev1;
    FakeTransport dev2;
    const auto created = create_test_plane(dev1, dev2);
    if (!created) return false;
    auto& plane = *created.value();

    const auto first = attachment(0U, 1U);
    const auto second = attachment(1U, 2U);
    if (!plane.attach(first) || !plane.attach(second)) return false;
    emit("shared attach: starts=%zu", dev1.starts);

    dev1.push(stream(1U, 4U));
    std::array<std::uint8_t, kPacketSize> output{};
    std::uint64_t payload = 0U;
    std::size_t total = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        const auto read = plane.read(first, MutableByteView{output.data(), output.size()},
                                     Timeout{1000U});
        if (!read || read.value().bytes != output.size()) return false;
        total += read.value().bytes;
        payload ^= bytes_digest(output.data(), output.size()) + index;
    }
    emit("shared read: bytes=%zu payload=%016llx", total,
         static_cast<unsigned long long>(payload));

    const auto stats = plane.stats(first);
    if (!stats) return false;
    emit("shared stats: packets=%llu bytes=%llu",
         static_cast<unsigned long long>(stats.value().packets),
         static_cast<unsigned long long>(stats.value().bytes));

    if (!plane.detach(first)) return false;
    const auto final_snapshot = plane.final_snapshot(first);
    if (!final_snapshot) return false;
    emit("shared final: packets=%llu terminal=%u",
         static_cast<unsigned long long>(final_snapshot.value().counters.packets),
         static_cast<unsigned>(final_snapshot.value().terminal));

    auto stale = first;
    stale.attachment_id += 1U;
    const auto stale_final = plane.final_snapshot(stale);
    const auto stale_stats = plane.stats(stale);
    const auto stale_release = plane.release_final(stale);
    emit("shared stale: final=%u stats=%u release=%u",
         static_cast<unsigned>(stale_final.error()),
         static_cast<unsigned>(stale_stats.error()),
         static_cast<unsigned>(stale_release.error()));
    if (stale_final.error() != Error::NOT_FOUND ||
        stale_stats.error() != Error::NOT_FOUND ||
        stale_release.error() != Error::NOT_FOUND)
        return false;
    if (!plane.release_final(first)) return false;
    if (!plane.detach(second)) return false;
    emit("shared teardown: starts=%zu cancels=%zu stops=%zu", dev1.starts,
         dev1.cancels, dev1.stops);
    return true;
}

// Case 2: full-ring accounting before rejection, read validation precedence,
// slow-consumer terminal, active read then EOF.
bool case_queue_full_validation_and_terminal()
{
    FakeTransport dev1;
    FakeTransport dev2;
    const auto created = create_test_plane(dev1, dev2);
    if (!created) return false;
    auto& plane = *created.value();

    const auto value = attachment(0U, 7U);
    const auto sibling = attachment(1U, 71U);
    if (!plane.attach(value) || !plane.attach(sibling)) return false;
    if (!dev1.wait_for_count(1U)) return false;
    const std::size_t baseline = dev1.wait_count();
    dev1.push(stream(1U, Q3U4StreamDataPlane::kMinQueuePackets + 4U));
    if (!dev1.wait_for_count(baseline + 1U)) return false;
    const std::size_t sibling_baseline = dev1.wait_count();
    dev1.push(stream(2U, 4U));
    if (!dev1.wait_for_count(sibling_baseline + 1U)) return false;

    std::array<std::uint8_t, kPacketSize> sibling_output{};
    const auto sibling_read =
        plane.read(sibling, MutableByteView{sibling_output.data(), sibling_output.size()},
                   Timeout{1000U});
    if (!sibling_read || sibling_read.value().bytes != sibling_output.size()) return false;
    emit("full sibling: bytes=%zu marker=%u", sibling_read.value().bytes,
         static_cast<unsigned>(sibling_output[4U]));

    std::vector<std::uint8_t> output(Q3U4StreamDataPlane::kMaxReadBytes);
    const auto null_read =
        plane.read(value, MutableByteView{nullptr, kPacketSize}, Timeout{0U});
    const auto unaligned_read =
        plane.read(value, MutableByteView{output.data(), 187U}, Timeout{0U});
    const auto large_read = plane.read(
        value, MutableByteView{output.data(), output.size() + kPacketSize}, Timeout{0U});
    emit("full validation: null=%u unaligned=%u large=%u",
         static_cast<unsigned>(null_read.error()),
         static_cast<unsigned>(unaligned_read.error()),
         static_cast<unsigned>(large_read.error()));
    if (null_read.error() != Error::INVALID_ARGUMENT ||
        unaligned_read.error() != Error::INVALID_ARGUMENT ||
        large_read.error() != Error::INVALID_ARGUMENT)
        return false;

    bool eof = false;
    std::size_t drained = 0U;
    for (std::size_t attempt = 0U; attempt < 4U && !eof; ++attempt) {
        const auto read = plane.read(value, MutableByteView{output.data(), output.size()},
                                     Timeout{1000U});
        if (!read) return false;
        drained += read.value().bytes;
        eof = read.value().eof;
    }
    const auto stats = plane.stats(value);
    if (!stats || !eof) return false;
    emit("full drained: bytes=%zu packets=%llu drops=%llu terminal=%u", drained,
         static_cast<unsigned long long>(stats.value().packets),
         static_cast<unsigned long long>(stats.value().queue_drops),
         static_cast<unsigned>(plane.terminal(value).value()));
    if (stats.value().queue_drops != 1U) return false;
    if (!plane.detach(value) || !plane.detach(sibling)) return false;
    return true;
}

// Case 3: empty detach final, drain reads, final EOF retirement, release.
bool case_drain_and_eof()
{
    FakeTransport dev1;
    FakeTransport dev2;
    const auto created = create_test_plane(dev1, dev2);
    if (!created) return false;
    auto& plane = *created.value();

    const auto empty = attachment(2U, 202U);
    if (!plane.attach(empty) || !plane.detach(empty)) return false;
    const auto empty_final = plane.final_snapshot(empty);
    if (!empty_final) return false;
    emit("drain empty final: packets=%llu terminal=%u",
         static_cast<unsigned long long>(empty_final.value().counters.packets),
         static_cast<unsigned>(empty_final.value().terminal));
    if (!plane.release_final(empty)) return false;

    const auto value = attachment(2U, 203U);
    if (!plane.attach(value)) return false;
    if (!dev1.wait_for_count(dev1.wait_count() + 1U)) return false;
    const std::size_t baseline = dev1.wait_count();
    dev1.push(stream(3U, 3U));
    if (!dev1.wait_for_count(baseline + 1U)) return false;
    if (!plane.detach(value)) return false;

    std::array<std::uint8_t, kPacketSize> output{};
    std::size_t drained = 0U;
    bool eof = false;
    for (std::size_t attempt = 0U; attempt < 8U && !eof; ++attempt) {
        const auto read = plane.read(value, MutableByteView{output.data(), output.size()},
                                     Timeout{200U});
        if (!read) return false;
        drained += read.value().bytes;
        eof = read.value().eof;
    }
    const auto final_snapshot = plane.final_snapshot(value);
    if (!final_snapshot) return false;
    emit("drain retired: bytes=%zu final_packets=%llu final_terminal=%u", drained,
         static_cast<unsigned long long>(final_snapshot.value().counters.packets),
         static_cast<unsigned>(final_snapshot.value().terminal));
    if (!plane.release_final(value)) return false;
    return true;
}

// Case 4: failed physical cleanup amends the retained final for the same
// identity to bridge_fatal.
bool case_failed_cleanup_amendment()
{
    FakeTransport dev1;
    FakeTransport dev2;
    const auto created = create_test_plane(dev1, dev2);
    if (!created) return false;
    auto& plane = *created.value();

    const auto failed = attachment(2U, 303U);
    if (!plane.attach(failed)) return false;
    dev1.cancel_error = Error::USB_IO;
    const auto detach = plane.detach(failed);
    const auto final_snapshot = plane.final_snapshot(failed);
    if (!final_snapshot) return false;
    emit("cleanup fail: detach=%u final_terminal=%u",
         static_cast<unsigned>(detach.error()),
         static_cast<unsigned>(final_snapshot.value().terminal));
    if (detach.error() != Error::USB_IO) return false;
    if (final_snapshot.value().terminal !=
        static_cast<std::uint8_t>(StreamTerminal::bridge_fatal))
        return false;
    if (!plane.release_final(failed)) return false;
    return true;
}

// Case 5: producer-originated transport failure terminates the attached
// session and the owner detach/join still completes.
bool case_producer_failure()
{
    FakeTransport dev1;
    FakeTransport dev2;
    const auto created = create_test_plane(dev1, dev2);
    if (!created) return false;
    auto& plane = *created.value();

    const auto value = attachment(2U, 404U);
    if (!plane.attach(value)) return false;
    if (!dev1.wait_for_count(1U)) return false;
    dev1.fail(Error::USB_IO);

    bool eof = false;
    StreamTerminal terminal = StreamTerminal::none;
    for (std::size_t attempt = 0U; attempt < 400U && !eof; ++attempt) {
        std::array<std::uint8_t, kPacketSize> output{};
        const auto read = plane.read(value, MutableByteView{output.data(), output.size()},
                                     Timeout{10U});
        if (!read) return false;
        eof = read.value().eof;
        terminal = read.value().terminal;
        if (!eof) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto stats = plane.stats(value);
    const auto detach = plane.detach(value);
    const auto final_snapshot = plane.final_snapshot(value);
    if (!final_snapshot) return false;
    emit("producer fail: eof=%d terminal=%u usb_errors=%llu detach=%u final_terminal=%u",
         eof ? 1 : 0, static_cast<unsigned>(terminal),
         static_cast<unsigned long long>(stats ? stats.value().usb_errors : 0ULL),
         static_cast<unsigned>(detach.error()),
         static_cast<unsigned>(final_snapshot.value().terminal));
    if (!eof || terminal != StreamTerminal::usb_error) return false;
    if (!stats || stats.value().usb_errors < 1U) return false;
    if (final_snapshot.value().terminal !=
        static_cast<std::uint8_t>(StreamTerminal::usb_error))
        return false;
    return true;
}

// Case 6: deterministic blocked-reader wake on detach.
struct WaitGate final {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
};

void wait_observer(void* context, std::uint8_t) noexcept
{
    auto& gate = *static_cast<WaitGate*>(context);
    std::lock_guard<std::mutex> lock(gate.mutex);
    gate.entered = true;
    gate.changed.notify_all();
}

bool case_blocked_reader_wake()
{
    FakeTransport dev1;
    FakeTransport dev2;
    const auto created = create_test_plane(dev1, dev2);
    if (!created) return false;
    auto& plane = *created.value();

    const auto value = attachment(2U, 505U);
    if (!plane.attach(value)) return false;

    WaitGate gate;
    plane.set_read_wait_observer_for_test(&wait_observer, &gate);

    std::atomic<bool> done{false};
    TunerStreamReadResult result{};
    Error error = Error::OK;
    bool detach_ok = false;
    bool entered = false;
    std::thread reader([&]() noexcept {
        std::array<std::uint8_t, kPacketSize> output{};
        const auto read = plane.read(value, MutableByteView{output.data(), output.size()},
                                     Timeout{5000U});
        if (read) result = read.value();
        else error = read.error();
        done.store(true);
    });
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        entered = gate.changed.wait_for(lock, std::chrono::seconds(5),
                                        [&]() noexcept { return gate.entered; });
    }
    // Always wake and join the reader before returning, even on a failed gate
    // rendezvous, so a joinable thread is never destroyed.
    if (entered) detach_ok = static_cast<bool>(plane.detach(value));
    else (void)plane.detach(value);
    reader.join();
    plane.set_read_wait_observer_for_test(nullptr, nullptr);
    emit("blocked wake: entered=%d detach=%d done=%d error=%u eof=%d terminal=%u",
         entered ? 1 : 0, detach_ok ? 1 : 0, done.load() ? 1 : 0,
         static_cast<unsigned>(error), result.eof ? 1 : 0,
         static_cast<unsigned>(result.terminal));
    if (!entered || !detach_ok) return false;
    if (!done.load() || error != Error::OK || !result.eof) return false;
    if (result.terminal != StreamTerminal::stopped) return false;
    (void)plane.release_final(value);
    return true;
}

// Case 7: two-span wrap read through the real ring.  The consumer reads the
// front of a full ring so head is nonzero, then more packets are appended so the
// write index crosses the physical end of the queue.  A single read then copies
// the first span up to the physical end and the wrapped span from the front.
bool case_two_span_wrap_read()
{
    FakeTransport dev1;
    FakeTransport dev2;
    // The enqueue observer context must outlive the plane.  Declaring the gate
    // before the owning plane means stack unwinding on any early return joins
    // the pump (plane destruction) first and only then destroys the gate, so a
    // delayed pump can never call the observer against dead storage.
    EnqueueGate gate;
    const std::size_t capacity = Q3U4StreamDataPlane::kMinQueuePackets;
    const auto created = create_test_plane(dev1, dev2, capacity);
    if (!created) return false;
    auto& plane = *created.value();

    const auto value = attachment(2U, 606U);
    if (!plane.attach(value)) return false;

    plane.set_packet_enqueue_observer_for_test(&enqueue_observer, &gate);

    // Fill the whole ring.  stream(3, n) emits n+1 packets; the first packet of
    // a new PID is the continuity boundary and is not published.
    dev1.push(stream(3U, capacity - 1U, 0x101U));
    if (!wait_for_enqueues(gate, capacity - 1U)) return false;

    // A single front read advances head to a nonzero index.
    std::array<std::uint8_t, kPacketSize> front{};
    const auto front_read = plane.read(value, MutableByteView{front.data(), front.size()},
                                       Timeout{1000U});
    if (!front_read || front_read.value().bytes != front.size()) return false;

    // Append two more packets: they land at the physical tail and then wrap to
    // the front of the ring, ahead of the current head.
    dev1.push(stream(3U, 2U, 0x201U));
    if (!wait_for_enqueues(gate, capacity + 1U)) return false;

    std::vector<std::uint8_t> output(Q3U4StreamDataPlane::kMaxReadBytes);
    const auto read = plane.read(value, MutableByteView{output.data(), output.size()},
                                 Timeout{1000U});
    if (!read) return false;

    const auto expected_front = published_packet(3U, 0x101U, 1U);
    bool ordered = std::memcmp(front.data(), expected_front.data(), kPacketSize) == 0;
    ordered = ordered && read.value().bytes == capacity * kPacketSize;
    // Compare every complete 188-byte packet of both spans in order: the first
    // span holds the remaining first-stream packets (cc 2..capacity), and the
    // two wrapped packets are the last two blocks.
    if (ordered) {
        for (std::size_t index = 0U; index < capacity; ++index) {
            std::uint16_t pid = 0x101U;
            std::uint8_t cc = static_cast<std::uint8_t>(index + 2U);
            if (index == capacity - 2U) {
                pid = 0x201U;
                cc = 1U;
            } else if (index == capacity - 1U) {
                pid = 0x201U;
                cc = 2U;
            }
            const auto expected = published_packet(3U, pid, cc);
            if (std::memcmp(output.data() + index * kPacketSize, expected.data(),
                            kPacketSize) != 0) {
                ordered = false;
                break;
            }
        }
    }

    const auto stats = plane.stats(value);
    emit("wrap read: bytes=%zu packets=%llu drops=%llu ordered=%d",
         read.value().bytes,
         static_cast<unsigned long long>(stats ? stats.value().packets : 0ULL),
         static_cast<unsigned long long>(stats ? stats.value().queue_drops : 0ULL),
         ordered ? 1 : 0);
    if (!ordered) return false;
    if (!stats || stats.value().packets != capacity + 1U) return false;
    if (stats.value().bytes != (capacity + 1U) * kPacketSize) return false;
    if (stats.value().queue_drops != 0U) return false;
    if (!plane.detach(value)) return false;
    return true;
}

}  // namespace

int main()
{
    const bool ok = case_shared_bridge_and_final() &&
                    case_queue_full_validation_and_terminal() &&
                    case_drain_and_eof() && case_failed_cleanup_amendment() &&
                    case_producer_failure() && case_blocked_reader_wake() &&
                    case_two_span_wrap_read();
    std::printf("digest=%016llx ok=%d\n",
                static_cast<unsigned long long>(g_digest), ok ? 1 : 0);
    return ok ? 0 : 1;
}

// SPDX-License-Identifier: GPL-2.0-only
// Characterization of the observable TunerService lifecycle: the ordered
// backend and stream callbacks, results, error precedence, receiver states,
// and the status generation for scripted scenarios.  These expectations were
// written against the service before its lifecycle moved into the common
// receiver engine and must stay unchanged across that move.
//
// `--dump` additionally prints every recorded trace so the output of two
// builds can be compared line by line.
#include "px4/tuner_service.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;

#define TRACE_CHECK(condition)                                                        \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "tuner trace check failed at line %d: %s\n",         \
                         __LINE__, #condition);                                       \
            return false;                                                              \
        }                                                                              \
    } while (false)

bool g_dump = false;

// Bound for every cross-thread wait.  Gates order the threads; a wait that
// runs out records a failure and lets its thread continue, so a regression
// ends the test instead of hanging it.  CTest's TIMEOUT is the outer bound.
constexpr std::chrono::milliseconds kWaitBound{10000};
// Shorter bound for the deliberately serialized experiment, which is expected
// to run out.
constexpr std::chrono::milliseconds kExpectedTimeout{200};

constexpr int kNoReceiver = -1;

std::string num(std::uint64_t value)
{
    return std::to_string(value);
}

std::string sys(System system)
{
    return std::to_string(static_cast<unsigned>(system));
}

class Trace final {
public:
    void add(int receiver, std::string text)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.push_back(Entry{receiver, std::move(text)});
    }

    std::vector<std::string> take()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> texts;
        for (const Entry& entry : entries_) texts.push_back(entry.text);
        entries_.clear();
        return texts;
    }

    std::vector<std::string> for_receiver(int receiver)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::string> texts;
        for (const Entry& entry : entries_) {
            if (entry.receiver == receiver) texts.push_back(entry.text);
        }
        return texts;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
    }

private:
    struct Entry final {
        int receiver;
        std::string text;
    };

    std::mutex mutex_;
    std::vector<Entry> entries_;
};

bool expect_trace(const char* scenario, const std::vector<std::string>& actual,
                  const std::vector<std::string>& expected)
{
    if (g_dump) {
        std::printf("[%s]\n", scenario);
        for (const std::string& entry : actual) std::printf("  %s\n", entry.c_str());
    }
    if (actual == expected) return true;
    std::fprintf(stderr, "trace mismatch in %s\n  expected:\n", scenario);
    for (const std::string& entry : expected) std::fprintf(stderr, "    %s\n", entry.c_str());
    std::fprintf(stderr, "  actual:\n");
    for (const std::string& entry : actual) std::fprintf(stderr, "    %s\n", entry.c_str());
    return false;
}

Result<void> take_result(std::deque<Error>& failures) noexcept
{
    if (failures.empty()) return Result<void>::success();
    const Error error = failures.front();
    failures.pop_front();
    return error == Error::OK ? Result<void>::success() : Result<void>::failure(error);
}

// A one-shot rendezvous: the service thread enters and waits until the test
// opens the gate or the bound runs out.
class Gate final {
public:
    void arm(int receiver)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        receiver_ = receiver;
        entered_ = false;
        open_ = false;
        timed_out_ = false;
    }

    void pass(int receiver)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (receiver_ != receiver) return;
        receiver_ = kNoReceiver;
        entered_ = true;
        changed_.notify_all();
        if (!changed_.wait_for(lock, kWaitBound, [&]() noexcept { return open_; }))
            timed_out_ = true;
    }

    bool wait_entered()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, kWaitBound, [&]() noexcept { return entered_; });
    }

    void open()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = true;
        receiver_ = kNoReceiver;
        changed_.notify_all();
    }

    bool timed_out()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return timed_out_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    int receiver_ = kNoReceiver;
    bool entered_ = false;
    bool open_ = false;
    bool timed_out_ = false;
};

class Done final {
public:
    void set()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        done_ = true;
        changed_.notify_all();
    }

    bool wait(std::chrono::milliseconds bound)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, bound, [&]() noexcept { return done_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool done_ = false;
};

// Runs `held` on one thread until it reaches the armed gate, then `probe` on a
// second thread, and waits a bounded time for the probe to finish while the
// first operation is still held.  The gate is always opened and both threads
// joined, so a probe that cannot finish (for example because the operations
// were serialized) fails the check instead of hanging the process.
template <typename Held, typename Probe>
bool probe_while_held(Gate& gate, int receiver, Held held, Probe probe,
                      std::chrono::milliseconds bound = kWaitBound)
{
    gate.arm(receiver);
    std::thread holder(held);
    const bool entered = gate.wait_entered();
    Done probed;
    std::thread prober([&]() {
        probe();
        probed.set();
    });
    const bool finished = probed.wait(bound);
    gate.open();
    prober.join();
    holder.join();
    if (!entered) std::fprintf(stderr, "held operation did not reach its gate\n");
    if (!finished) std::fprintf(stderr, "probe did not finish while the operation was held\n");
    return entered && finished && !gate.timed_out();
}

class TraceBackend final : public TunerServiceBackend {
public:
    explicit TraceBackend(Trace& trace) noexcept : trace_(trace) {}

    std::uint8_t receiver_count() const noexcept override { return count; }

    bool receiver_supports(std::uint8_t receiver, System system) const noexcept override
    {
        trace_.add(receiver, "supports " + num(receiver) + " " + sys(system));
        std::unique_lock<std::mutex> serial(serial_, std::defer_lock);
        if (serialize) serial.lock();
        supports_gate.pass(receiver);
        return TunerServiceBackend::receiver_supports(receiver, system);
    }

    bool selects_satellite_stream_before_tune() const noexcept override
    {
        return select_before_tune;
    }

    bool requires_terrestrial_lock_settle() const noexcept override
    {
        return settle;
    }

    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        trace_.add(receiver, "open " + num(receiver));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(open_failures);
    }

    Result<void> tune_terrestrial(std::uint8_t receiver, std::uint32_t frequency_khz,
                                  std::uint32_t timeout_ms) noexcept override
    {
        trace_.add(receiver, "tune_t " + num(receiver) + " " + num(frequency_khz) + " " +
                                 num(timeout_ms));
        tune_gate.pass(receiver);
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(tune_failures);
    }

    Result<void> tune_satellite(std::uint8_t receiver, std::uint32_t frequency_khz,
                                std::uint32_t timeout_ms) noexcept override
    {
        trace_.add(receiver, "tune_s " + num(receiver) + " " + num(frequency_khz) + " " +
                                 num(timeout_ms));
        tune_gate.pass(receiver);
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(tune_failures);
    }

    Result<bool> is_locked(std::uint8_t receiver, System system) noexcept override
    {
        trace_.add(receiver, "lock " + num(receiver) + " " + sys(system));
        std::lock_guard<std::mutex> lock(mutex_);
        if (!lock_failures.empty()) {
            const Error error = lock_failures.front();
            lock_failures.pop_front();
            return Result<bool>::failure(error);
        }
        return Result<bool>::success(lock_value);
    }

    Result<void> select_satellite_slot(std::uint8_t receiver, std::uint8_t slot,
                                       std::uint32_t timeout_ms) noexcept override
    {
        trace_.add(receiver, "slot " + num(receiver) + " " + num(slot) + " " +
                                 num(timeout_ms));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(slot_failures);
    }

    Result<void> select_satellite_tsid(std::uint8_t receiver, std::uint16_t tsid,
                                       std::uint32_t timeout_ms) noexcept override
    {
        trace_.add(receiver, "tsid " + num(receiver) + " " + num(tsid) + " " +
                                 num(timeout_ms));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(tsid_failures);
    }

    Result<void> close_receiver(std::uint8_t receiver) noexcept override
    {
        trace_.add(receiver, "close " + num(receiver));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(close_failures);
    }

    Result<void> begin_tune_power(std::uint8_t receiver, System system,
                                  std::uint8_t lnb_voltage) noexcept override
    {
        trace_.add(receiver, "begin_power " + num(receiver) + " " + sys(system) + " " +
                                 num(lnb_voltage));
        if (lnb_voltage == 15U && !allow_lnb_15v)
            return Result<void>::failure(Error::UNSUPPORTED);
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(power_begin_failures);
    }

    Result<void> commit_tune_power(std::uint8_t receiver) noexcept override
    {
        trace_.add(receiver, "commit " + num(receiver));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(power_commit_failures);
    }

    Result<void> rollback_tune_power(std::uint8_t receiver) noexcept override
    {
        trace_.add(receiver, "rollback " + num(receiver));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(power_rollback_failures);
    }

    void mark_receiver_disconnected(std::uint8_t receiver) noexcept override
    {
        trace_.add(receiver, "mark_disconnected " + num(receiver));
    }

    Result<void> shutdown() noexcept override
    {
        trace_.add(kNoReceiver, "shutdown");
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(shutdown_failures);
    }

    Result<void> start_capture(std::uint8_t receiver, System system) noexcept override
    {
        trace_.add(receiver, "start_capture " + num(receiver) + " " + sys(system));
        capture_gate.pass(receiver);
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(capture_start_failures);
    }

    Result<void> stop_capture(std::uint8_t receiver, System system) noexcept override
    {
        trace_.add(receiver, "stop_capture " + num(receiver) + " " + sys(system));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(capture_stop_failures);
    }

    std::uint8_t count = static_cast<std::uint8_t>(kReceiverCount);
    bool select_before_tune = false;
    bool settle = false;
    bool allow_lnb_15v = false;
    bool lock_value = true;
    std::deque<Error> open_failures;
    std::deque<Error> tune_failures;
    std::deque<Error> lock_failures;
    std::deque<Error> slot_failures;
    std::deque<Error> tsid_failures;
    std::deque<Error> close_failures;
    std::deque<Error> power_begin_failures;
    std::deque<Error> power_commit_failures;
    std::deque<Error> power_rollback_failures;
    std::deque<Error> shutdown_failures;
    std::deque<Error> capture_start_failures;
    std::deque<Error> capture_stop_failures;
    // Test-only regression model: one lock across every receiver's support
    // query, as if separate receivers were serialized.
    bool serialize = false;
    mutable Gate supports_gate;
    Gate tune_gate;
    Gate capture_gate;

private:
    Trace& trace_;
    std::mutex mutex_;
    mutable std::mutex serial_;
};

class TraceStream final : public TunerStreamControl {
public:
    explicit TraceStream(Trace& trace) noexcept : trace_(trace) {}

    Result<void> attach(const TunerAttachment& attachment) noexcept override
    {
        trace_.add(attachment.receiver, "attach " + identity(attachment));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(attach_failures);
    }

    Result<void> detach(const TunerAttachment& attachment) noexcept override
    {
        trace_.add(attachment.receiver, "detach " + identity(attachment));
        std::lock_guard<std::mutex> lock(mutex_);
        return take_result(detach_failures);
    }

    Result<TunerStreamCounters> stats(
        const TunerAttachment& attachment) const noexcept override
    {
        trace_.add(attachment.receiver, "stats " + identity(attachment));
        std::lock_guard<std::mutex> lock(mutex_);
        return Result<TunerStreamCounters>::success(counters);
    }

    Result<TunerStreamFinalSnapshot> final_snapshot(
        const TunerAttachment& attachment) const noexcept override
    {
        trace_.add(attachment.receiver, "final " + identity(attachment));
        std::lock_guard<std::mutex> lock(mutex_);
        if (!final_failures.empty()) {
            const Error error = final_failures.front();
            final_failures.pop_front();
            return Result<TunerStreamFinalSnapshot>::failure(error);
        }
        return Result<TunerStreamFinalSnapshot>::success(snapshot);
    }

    static std::string identity(const TunerAttachment& attachment)
    {
        return num(attachment.receiver) + " " + num(attachment.lease_id) + " " +
               num(attachment.attachment_id);
    }

    std::deque<Error> attach_failures;
    std::deque<Error> detach_failures;
    mutable std::deque<Error> final_failures;
    TunerStreamCounters counters{};
    TunerStreamFinalSnapshot snapshot{};

private:
    Trace& trace_;
    mutable std::mutex mutex_;
};

class TraceClock final : public TunerServiceTime {
public:
    explicit TraceClock(Trace& trace) noexcept : trace_(trace) {}

    std::uint64_t monotonic_ms() noexcept override { return now.load(); }

    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        trace_.add(kNoReceiver, "sleep " + num(milliseconds));
        now.fetch_add(milliseconds);
    }

    std::atomic<std::uint64_t> now{0U};

private:
    Trace& trace_;
};

class CountingNonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, kNonceLength>> generate() noexcept override
    {
        std::array<std::uint8_t, kNonceLength> value{};
        value[0U] = next.fetch_add(1U);
        return Result<std::array<std::uint8_t, kNonceLength>>::success(value);
    }

    std::atomic<std::uint8_t> next{1U};
};

struct Fixture final {
    Trace trace;
    TraceBackend backend{trace};
    TraceStream stream{trace};
    TraceClock clock{trace};
    CountingNonce nonce;
};

TuneRequestPayload terrestrial(std::uint64_t lease_id, std::uint32_t timeout_ms = 1000U)
{
    return TuneRequestPayload{lease_id, System::ISDB_T, 557000U, 0xffffU, 0xffffU,
                              6000000U, 0U, timeout_ms};
}

TuneRequestPayload satellite_slot(std::uint64_t lease_id, std::uint16_t slot,
                                  std::uint8_t lnb_voltage)
{
    return TuneRequestPayload{lease_id, System::ISDB_S, 1049480U, 0xffffU, slot, 0U,
                              lnb_voltage, 1000U};
}

TuneRequestPayload satellite_tsid(std::uint64_t lease_id, std::uint16_t tsid)
{
    return TuneRequestPayload{lease_id, System::ISDB_S, 1049480U, tsid, 0xffffU, 0U, 0U,
                              1000U};
}

bool state_is(const TunerService& service, std::uint8_t receiver, ReceiverState state)
{
    const auto status = service.status();
    return status && status.value().receiver_states[receiver] == state;
}

std::uint64_t generation(const TunerService& service)
{
    const auto status = service.status();
    return status ? status.value().generation : 0U;
}

bool failed_with(Error actual_error, bool succeeded, Error expected)
{
    return !succeeded && actual_error == expected;
}

template <typename T>
bool failed(const Result<T>& result, Error expected)
{
    return failed_with(result.error(), static_cast<bool>(result), expected);
}

bool test_terrestrial_lifecycle_trace()
{
    Fixture f;
    TunerService service(f.backend, f.nonce, f.clock, nullptr, nullptr, &f.stream);

    const auto lease = service.acquire(1U, 2U);
    TRACE_CHECK(lease && lease.value().lease_id == 1U && lease.value().nonce[0U] == 1U);
    TRACE_CHECK(expect_trace("acquire", f.trace.take(), {"open 2", "supports 2 2"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::leased) && generation(service) == 2U);

    const auto tuned = service.tune(1U, terrestrial(1U));
    TRACE_CHECK(tuned && tuned.value().locked == 1U &&
                tuned.value().cnr_mdb == std::numeric_limits<std::int32_t>::min());
    TRACE_CHECK(expect_trace("tune", f.trace.take(),
                             {"supports 2 1", "begin_power 2 1 0",
                              "tune_t 2 557000 1000", "lock 2 1", "commit 2"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::tuned) && generation(service) == 3U);

    TRACE_CHECK(service.start_stream(1U, 1U));
    const auto attached = service.attach_stream(1U, lease.value().nonce);
    TRACE_CHECK(attached && attached.value().owner_client_id == 1U &&
                attached.value().lease_id == 1U && attached.value().attachment_id == 1U &&
                attached.value().receiver == 2U &&
                attached.value().system == System::ISDB_T &&
                attached.value().nonce == lease.value().nonce);
    TRACE_CHECK(expect_trace("attach", f.trace.take(),
                             {"start_capture 2 1", "attach 2 1 1"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::streaming) &&
                generation(service) == 4U);

    f.stream.counters.packets = 11U;
    const auto stats = service.stats(1U, 1U);
    TRACE_CHECK(stats && stats.value().packets == 11U);
    TRACE_CHECK(expect_trace("stats", f.trace.take(), {"stats 2 1 1"}));

    f.stream.snapshot.counters.packets = 7U;
    f.stream.snapshot.terminal = static_cast<std::uint8_t>(TunerStreamTerminal::stopped);
    const auto stopped = service.stop_stream(1U, 1U);
    TRACE_CHECK(stopped && stopped.value().counters.packets == 7U &&
                stopped.value().terminal ==
                    static_cast<std::uint8_t>(TunerStreamTerminal::stopped));
    TRACE_CHECK(expect_trace("stop", f.trace.take(),
                             {"detach 2 1 1", "stop_capture 2 1", "final 2 1 1"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::tuned) && generation(service) == 5U);

    TRACE_CHECK(service.release(1U, 1U));
    TRACE_CHECK(expect_trace("release", f.trace.take(), {"close 2"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::free) && generation(service) == 6U);
    return true;
}

bool test_satellite_selection_traces()
{
    {
        Fixture f;
        f.backend.allow_lnb_15v = true;
        TunerService service(f.backend, f.nonce, f.clock);
        TRACE_CHECK(service.acquire(1U, 0U));
        TRACE_CHECK(expect_trace("satellite acquire", f.trace.take(),
                                 {"open 0", "supports 0 2", "supports 0 1"}));
        // Q3U4 order: the slot is selected after lock with the remaining budget.
        TRACE_CHECK(service.tune(1U, satellite_slot(1U, 3U, 15U)));
        TRACE_CHECK(expect_trace("satellite slot", f.trace.take(),
                                 {"supports 0 2", "begin_power 0 2 15",
                                  "tune_s 0 1049480 1000", "lock 0 2",
                                  "slot 0 3 1000", "commit 0"}));
        TRACE_CHECK(state_is(service, 0U, ReceiverState::tuned));
    }
    {
        Fixture f;
        f.backend.select_before_tune = true;
        TunerService service(f.backend, f.nonce, f.clock);
        TRACE_CHECK(service.acquire(1U, 1U));
        f.trace.clear();
        // PX-MLT order: the TSID is selected before the frontend tune.
        TRACE_CHECK(service.tune(1U, satellite_tsid(1U, 0x4010U)));
        TRACE_CHECK(expect_trace("satellite tsid before tune", f.trace.take(),
                                 {"supports 1 2", "begin_power 1 2 0",
                                  "tsid 1 16400 1000", "tune_s 1 1049480 1000",
                                  "lock 1 2", "commit 1"}));
    }
    return true;
}

bool test_tune_error_precedence()
{
    Fixture f;
    TunerService service(f.backend, f.nonce, f.clock, nullptr, nullptr, &f.stream);
    TRACE_CHECK(service.acquire(1U, 2U));
    TRACE_CHECK(service.acquire(1U, 3U));
    f.trace.clear();
    const std::uint64_t base_generation = generation(service);

    // An unknown lease or a foreign client precedes every request check.
    TRACE_CHECK(failed(service.tune(2U, terrestrial(1U, 99U)), Error::NOT_FOUND));
    TRACE_CHECK(failed(service.tune(1U, terrestrial(99U, 99U)), Error::NOT_FOUND));
    TRACE_CHECK(expect_trace("not found", f.trace.take(), {}));

    // Receiver support is queried before the request itself is validated.
    TRACE_CHECK(failed(service.tune(1U, satellite_slot(1U, 0U, 0U)),
                       Error::INVALID_ARGUMENT));
    TRACE_CHECK(expect_trace("unsupported system", f.trace.take(), {"supports 2 2"}));
    TRACE_CHECK(failed(service.tune(1U, terrestrial(1U, 99U)), Error::INVALID_ARGUMENT));
    TRACE_CHECK(expect_trace("short timeout", f.trace.take(), {"supports 2 1"}));
    TuneRequestPayload either = terrestrial(1U);
    either.system = System::ISDB_T_OR_S;
    TRACE_CHECK(failed(service.tune(1U, either), Error::INVALID_ARGUMENT));
    TRACE_CHECK(expect_trace("capability-only system", f.trace.take(), {"supports 2 3"}));
    // Wider than the frontend's 32-bit kHz parameter: its low bits equal a
    // valid frequency, but the legacy request is invalid.
    TuneRequestPayload wide = terrestrial(1U);
    wide.frequency_khz += std::uint64_t{1U} << 32U;
    TRACE_CHECK(failed(service.tune(1U, wide), Error::INVALID_ARGUMENT));
    TRACE_CHECK(expect_trace("wide frequency", f.trace.take(), {"supports 2 1"}));
    TuneRequestPayload powered = terrestrial(1U);
    powered.lnb_voltage = 15U;
    TRACE_CHECK(failed(service.tune(1U, powered), Error::INVALID_ARGUMENT));
    TRACE_CHECK(expect_trace("terrestrial lnb", f.trace.take(), {"supports 2 1"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::leased) &&
                generation(service) == base_generation);

    // An armed stream (BUSY) precedes support and validation.
    TRACE_CHECK(service.tune(1U, terrestrial(1U)));
    TRACE_CHECK(service.start_stream(1U, 1U));
    f.trace.clear();
    TRACE_CHECK(failed(service.tune(1U, terrestrial(1U, 99U)), Error::BUSY));
    TRACE_CHECK(expect_trace("busy", f.trace.take(), {}));

    // A failed rollback quarantines the receiver; NOT_READY then precedes
    // request validation.
    f.backend.tune_failures = {Error::USB_IO};
    f.backend.power_rollback_failures = {Error::USB_IO};
    TRACE_CHECK(failed(service.tune(1U, terrestrial(2U)), Error::USB_IO));
    TRACE_CHECK(expect_trace("rollback failure", f.trace.take(),
                             {"supports 3 1", "begin_power 3 1 0",
                              "tune_t 3 557000 1000", "rollback 3"}));
    TRACE_CHECK(state_is(service, 3U, ReceiverState::error));
    TRACE_CHECK(failed(service.tune(1U, terrestrial(2U, 99U)), Error::NOT_READY));
    TRACE_CHECK(expect_trace("not ready", f.trace.take(), {}));

    // RELEASE closes the quarantined receiver and frees it.
    TRACE_CHECK(service.release(1U, 2U));
    TRACE_CHECK(expect_trace("release quarantined", f.trace.take(), {"close 3"}));
    TRACE_CHECK(state_is(service, 3U, ReceiverState::free));
    return true;
}

bool test_tune_rollback_traces()
{
    Fixture f;
    f.backend.allow_lnb_15v = false;
    TunerService service(f.backend, f.nonce, f.clock);
    TRACE_CHECK(service.acquire(1U, 2U));
    TRACE_CHECK(service.acquire(1U, 0U));
    f.trace.clear();

    // No lock: ten 10 ms polls inside a 100 ms budget, then rollback.
    f.backend.lock_value = false;
    TRACE_CHECK(failed(service.tune(1U, terrestrial(1U, 100U)), Error::TIMEOUT));
    std::vector<std::string> expected{"supports 2 1", "begin_power 2 1 0",
                                      "tune_t 2 557000 100"};
    for (int poll = 0; poll < 10; ++poll) {
        expected.push_back("lock 2 1");
        expected.push_back("sleep 10");
    }
    expected.push_back("rollback 2");
    TRACE_CHECK(expect_trace("lock timeout", f.trace.take(), expected));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::leased));
    f.backend.lock_value = true;

    // Transport loss is marked before the power rollback.
    f.backend.tune_failures = {Error::DISCONNECTED};
    TRACE_CHECK(failed(service.tune(1U, terrestrial(1U)), Error::DISCONNECTED));
    TRACE_CHECK(expect_trace("tune disconnected", f.trace.take(),
                             {"supports 2 1", "begin_power 2 1 0",
                              "tune_t 2 557000 1000", "mark_disconnected 2",
                              "rollback 2"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::leased));

    // A failed power preparation has nothing to roll back.
    f.backend.power_begin_failures = {Error::DISCONNECTED};
    TRACE_CHECK(failed(service.tune(1U, terrestrial(1U)), Error::DISCONNECTED));
    TRACE_CHECK(expect_trace("power disconnected", f.trace.take(),
                             {"supports 2 1", "begin_power 2 1 0",
                              "mark_disconnected 2"}));
    TRACE_CHECK(failed(service.tune(1U, satellite_slot(2U, 0U, 15U)), Error::UNSUPPORTED));
    TRACE_CHECK(expect_trace("lnb unsupported", f.trace.take(),
                             {"supports 0 2", "begin_power 0 2 15"}));

    // A failing lock query and a failing commit both roll back.
    f.backend.lock_failures = {Error::USB_IO};
    TRACE_CHECK(failed(service.tune(1U, terrestrial(1U)), Error::USB_IO));
    TRACE_CHECK(expect_trace("lock failure", f.trace.take(),
                             {"supports 2 1", "begin_power 2 1 0",
                              "tune_t 2 557000 1000", "lock 2 1", "rollback 2"}));
    f.backend.power_commit_failures = {Error::USB_IO};
    TRACE_CHECK(failed(service.tune(1U, terrestrial(1U)), Error::USB_IO));
    TRACE_CHECK(expect_trace("commit failure", f.trace.take(),
                             {"supports 2 1", "begin_power 2 1 0",
                              "tune_t 2 557000 1000", "lock 2 1", "commit 2",
                              "rollback 2"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::leased));

    // Post-lock slot selection failure.
    f.backend.slot_failures = {Error::TIMEOUT};
    TRACE_CHECK(failed(service.tune(1U, satellite_slot(2U, 0U, 0U)), Error::TIMEOUT));
    TRACE_CHECK(expect_trace("slot failure", f.trace.take(),
                             {"supports 0 2", "begin_power 0 2 0",
                              "tune_s 0 1049480 1000", "lock 0 2", "slot 0 0 1000",
                              "rollback 0"}));
    TRACE_CHECK(state_is(service, 0U, ReceiverState::leased));
    return true;
}

bool test_disconnect_client_traces()
{
    {
        Fixture f;
        TunerService service(f.backend, f.nonce, f.clock, nullptr, nullptr, &f.stream);
        const auto streaming = service.acquire(1U, 2U);
        TRACE_CHECK(streaming && service.acquire(1U, 5U) && service.acquire(2U, 6U));
        TRACE_CHECK(service.tune(1U, terrestrial(1U)) && service.start_stream(1U, 1U));
        const auto attached = service.attach_stream(1U, streaming.value().nonce);
        TRACE_CHECK(attached);
        f.trace.clear();

        // Receiver order; the stream detaches before capture stop and close.
        TRACE_CHECK(service.disconnect_client(1U));
        TRACE_CHECK(expect_trace("disconnect", f.trace.take(),
                                 {"detach 2 1 1", "stop_capture 2 1", "close 2",
                                  "close 5"}));
        TRACE_CHECK(state_is(service, 2U, ReceiverState::free) &&
                    state_is(service, 5U, ReceiverState::free) &&
                    state_is(service, 6U, ReceiverState::leased));
        TRACE_CHECK(failed(service.detach_stream(attached.value()), Error::NOT_FOUND));
        TRACE_CHECK(expect_trace("stale detach", f.trace.take(), {}));
    }
    {
        Fixture f;
        TunerService service(f.backend, f.nonce, f.clock, nullptr, nullptr, &f.stream);
        const auto streaming = service.acquire(1U, 2U);
        TRACE_CHECK(streaming && service.acquire(1U, 4U));
        TRACE_CHECK(service.tune(1U, terrestrial(1U)) && service.start_stream(1U, 1U));
        TRACE_CHECK(service.attach_stream(1U, streaming.value().nonce));
        f.trace.clear();

        // A lost data plane makes the transport terminal: no stop or close.
        f.stream.detach_failures = {Error::DISCONNECTED};
        TRACE_CHECK(failed(service.disconnect_client(1U), Error::DISCONNECTED));
        TRACE_CHECK(expect_trace("disconnect lost", f.trace.take(),
                                 {"detach 2 1 1", "mark_disconnected 2", "close 4"}));
        TRACE_CHECK(state_is(service, 2U, ReceiverState::error) &&
                    state_is(service, 4U, ReceiverState::free));
        TRACE_CHECK(failed(service.acquire(3U, 2U), Error::NOT_READY));
        TRACE_CHECK(expect_trace("quarantined acquire", f.trace.take(), {}));
    }
    return true;
}

bool test_final_stream_cleanup_traces()
{
    {
        Fixture f;
        TunerService service(f.backend, f.nonce, f.clock, nullptr, nullptr, &f.stream);
        const auto lease = service.acquire(1U, 2U);
        TRACE_CHECK(lease && service.tune(1U, terrestrial(1U)) && service.start_stream(1U, 1U));
        TRACE_CHECK(service.attach_stream(1U, lease.value().nonce));
        f.trace.clear();

        // A final snapshot reporting transport loss quarantines the receiver.
        f.stream.snapshot.counters.packets = 3U;
        f.stream.snapshot.terminal =
            static_cast<std::uint8_t>(TunerStreamTerminal::disconnected);
        const auto stopped = service.stop_stream(1U, 1U);
        TRACE_CHECK(stopped && stopped.value().counters.packets == 3U);
        TRACE_CHECK(expect_trace("stop disconnected", f.trace.take(),
                                 {"detach 2 1 1", "stop_capture 2 1", "final 2 1 1",
                                  "mark_disconnected 2"}));
        TRACE_CHECK(state_is(service, 2U, ReceiverState::error));
        TRACE_CHECK(failed(service.release(1U, 1U), Error::DISCONNECTED));
        TRACE_CHECK(expect_trace("release disconnected", f.trace.take(), {}));
        TRACE_CHECK(state_is(service, 2U, ReceiverState::error));
    }
    {
        Fixture f;
        TunerService service(f.backend, f.nonce, f.clock, nullptr, nullptr, &f.stream);
        const auto lease = service.acquire(1U, 2U);
        TRACE_CHECK(lease && service.tune(1U, terrestrial(1U)) && service.start_stream(1U, 1U));
        const auto first = service.attach_stream(1U, lease.value().nonce);
        TRACE_CHECK(first);
        f.trace.clear();

        // A missing final snapshot fails STOP but the receiver stays tuned.
        f.stream.final_failures = {Error::UNSUPPORTED};
        TRACE_CHECK(failed(service.stop_stream(1U, 1U), Error::UNSUPPORTED));
        TRACE_CHECK(expect_trace("stop without snapshot", f.trace.take(),
                                 {"detach 2 1 1", "stop_capture 2 1", "final 2 1 1"}));
        TRACE_CHECK(state_is(service, 2U, ReceiverState::tuned));
        TRACE_CHECK(failed(service.stop_stream(1U, 1U), Error::NOT_READY));

        // A new tune re-arms the lease with a fresh attachment identity; the
        // stale identity cannot detach it.
        TRACE_CHECK(service.tune(1U, terrestrial(1U)) && service.start_stream(1U, 1U));
        const auto second = service.attach_stream(1U, lease.value().nonce);
        TRACE_CHECK(second && second.value().attachment_id == 2U);
        TRACE_CHECK(expect_trace("re-arm", f.trace.take(),
                                 {"supports 2 1", "begin_power 2 1 0",
                                  "tune_t 2 557000 1000", "lock 2 1", "commit 2",
                                  "start_capture 2 1", "attach 2 1 2"}));
        TRACE_CHECK(failed(service.detach_stream(first.value()), Error::NOT_FOUND));
        TRACE_CHECK(expect_trace("stale identity", f.trace.take(), {}));
        TRACE_CHECK(service.detach_stream(second.value()));
        TRACE_CHECK(expect_trace("current identity", f.trace.take(),
                                 {"detach 2 1 2", "stop_capture 2 1"}));
        TRACE_CHECK(state_is(service, 2U, ReceiverState::tuned));
    }
    return true;
}

bool test_revoke_during_capture_start()
{
    Fixture f;
    TunerService service(f.backend, f.nonce, f.clock, nullptr, nullptr, &f.stream);
    const auto lease = service.acquire(1U, 2U);
    TRACE_CHECK(lease && service.tune(1U, terrestrial(1U)) && service.start_stream(1U, 1U));
    f.trace.clear();

    Result<TunerAttachment> attached = Result<TunerAttachment>::failure(Error::INTERNAL);
    bool revoked = false;
    // Metadata-only revoke wins while the physical start is in flight.
    const bool proceeded = probe_while_held(
        f.backend.capture_gate, 2,
        [&]() { attached = service.attach_stream(1U, lease.value().nonce); },
        [&]() { revoked = static_cast<bool>(service.revoke_stream(1U, 1U)); });

    TRACE_CHECK(proceeded && revoked);
    TRACE_CHECK(failed(attached, Error::NOT_FOUND));
    TRACE_CHECK(expect_trace("revoked attach", f.trace.take(),
                             {"start_capture 2 1", "attach 2 1 1", "detach 2 1 1",
                              "stop_capture 2 1"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::tuned));
    return true;
}

struct HeldValidRequest final {
    bool proceeded = false;
    bool observed = false;
    Result<TuneResponsePayload> held = Result<TuneResponsePayload>::failure(Error::INTERNAL);
    Result<TuneResponsePayload> invalid =
        Result<TuneResponsePayload>::failure(Error::INTERNAL);
    Result<TuneResponsePayload> other = Result<TuneResponsePayload>::failure(Error::INTERNAL);
};

// A valid request on receiver 2 (lease 1) waits at its validation point, the
// support query.  An invalid request on receiver 3 (lease 2) with the same
// 32-bit frequency and a valid request on receiver 6 (lease 3) must complete
// meanwhile.
HeldValidRequest tune_others_while_valid_request_held(Fixture& f, TunerService& service,
                                                      std::chrono::milliseconds bound)
{
    TuneRequestPayload wide = terrestrial(2U);
    wide.frequency_khz += std::uint64_t{1U} << 32U;
    HeldValidRequest result;
    result.proceeded = probe_while_held(
        f.backend.supports_gate, 2,
        [&]() { result.held = service.tune(1U, terrestrial(1U)); },
        [&]() {
            result.invalid = service.tune(1U, wide);
            result.other = service.tune(1U, terrestrial(3U));
            result.observed = state_is(service, 2U, ReceiverState::leased) &&
                              state_is(service, 6U, ReceiverState::tuned);
        },
        bound);
    return result;
}

bool test_separate_receivers_and_per_request_validation()
{
    Fixture f;
    TunerService service(f.backend, f.nonce, f.clock);
    TRACE_CHECK(service.acquire(1U, 2U) && service.acquire(1U, 3U) && service.acquire(1U, 6U));
    f.trace.clear();

    const HeldValidRequest first = tune_others_while_valid_request_held(f, service, kWaitBound);
    TRACE_CHECK(first.proceeded && first.observed);
    TRACE_CHECK(first.held && first.other);
    TRACE_CHECK(failed(first.invalid, Error::INVALID_ARGUMENT));
    TRACE_CHECK(expect_trace("held valid", f.trace.for_receiver(2),
                             {"supports 2 1", "begin_power 2 1 0",
                              "tune_t 2 557000 1000", "lock 2 1", "commit 2"}));
    TRACE_CHECK(expect_trace("concurrent invalid", f.trace.for_receiver(3), {"supports 3 1"}));
    TRACE_CHECK(expect_trace("concurrent valid", f.trace.for_receiver(6),
                             {"supports 6 1", "begin_power 6 1 0",
                              "tune_t 6 557000 1000", "lock 6 1", "commit 6"}));
    f.trace.clear();

    // The reverse: the invalid request waits at the same point while a valid
    // request with the same 32-bit frequency completes.
    TuneRequestPayload wide = terrestrial(1U);
    wide.frequency_khz += std::uint64_t{1U} << 32U;
    Result<TuneResponsePayload> held = Result<TuneResponsePayload>::failure(Error::INTERNAL);
    Result<TuneResponsePayload> other = Result<TuneResponsePayload>::failure(Error::INTERNAL);
    const bool reproceeded = probe_while_held(
        f.backend.supports_gate, 2, [&]() { held = service.tune(1U, wide); },
        [&]() { other = service.tune(1U, terrestrial(2U)); });

    TRACE_CHECK(reproceeded && other);
    TRACE_CHECK(failed(held, Error::INVALID_ARGUMENT));
    TRACE_CHECK(expect_trace("held invalid", f.trace.for_receiver(2), {"supports 2 1"}));
    TRACE_CHECK(expect_trace("concurrent valid same frequency", f.trace.for_receiver(3),
                             {"supports 3 1", "begin_power 3 1 0",
                              "tune_t 3 557000 1000", "lock 3 1", "commit 3"}));
    TRACE_CHECK(state_is(service, 2U, ReceiverState::tuned) &&
                state_is(service, 3U, ReceiverState::tuned));
    return true;
}

bool test_serialized_regression_fails_without_hanging()
{
    // Failure experiment, not a characterization: with every support query
    // behind one lock, the other receivers cannot proceed while receiver 2 is
    // held.  The check must report that and return; releasing the gate then
    // lets all requests complete.  No trace is dumped for this scenario.
    Fixture f;
    f.backend.serialize = true;
    TunerService service(f.backend, f.nonce, f.clock);
    TRACE_CHECK(service.acquire(1U, 2U) && service.acquire(1U, 3U) && service.acquire(1U, 6U));

    const auto started = std::chrono::steady_clock::now();
    const HeldValidRequest result =
        tune_others_while_valid_request_held(f, service, kExpectedTimeout);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    TRACE_CHECK(!result.proceeded);
    TRACE_CHECK(elapsed < kWaitBound);
    TRACE_CHECK(result.held && result.other);
    TRACE_CHECK(failed(result.invalid, Error::INVALID_ARGUMENT));
    return true;
}

bool test_receiver_count_and_absent_slots()
{
    Fixture f;
    f.backend.count = 4U;
    TunerService service(f.backend, f.nonce, f.clock);
    const auto status = service.status();
    TRACE_CHECK(status && status.value().generation == 1U);
    for (std::uint8_t receiver = 0U; receiver < kReceiverCount; ++receiver) {
        TRACE_CHECK(status.value().receiver_states[receiver] ==
                    (receiver < 4U ? ReceiverState::free : ReceiverState::absent));
    }
    TRACE_CHECK(failed(service.acquire(1U, 4U), Error::INVALID_ARGUMENT));
    TRACE_CHECK(failed(service.acquire(0U, 1U), Error::INVALID_ARGUMENT));
    TRACE_CHECK(failed(service.acquire(1U, 200U), Error::INVALID_ARGUMENT));
    TRACE_CHECK(expect_trace("invalid acquire", f.trace.take(), {}));
    TRACE_CHECK(service.acquire(1U, 3U));
    TRACE_CHECK(expect_trace("last receiver", f.trace.take(), {"open 3", "supports 3 2"}));
    TRACE_CHECK(state_is(service, 4U, ReceiverState::absent) && generation(service) == 2U);
    return true;
}

bool test_shutdown_trace()
{
    Fixture f;
    {
        TunerService service(f.backend, f.nonce, f.clock, nullptr, nullptr, &f.stream);
        const auto lease = service.acquire(1U, 2U);
        TRACE_CHECK(lease && service.acquire(2U, 6U));
        TRACE_CHECK(service.tune(1U, terrestrial(1U)) && service.start_stream(1U, 1U));
        TRACE_CHECK(service.attach_stream(1U, lease.value().nonce));
        f.trace.clear();

        TRACE_CHECK(service.shutdown());
        TRACE_CHECK(expect_trace("shutdown", f.trace.take(),
                                 {"detach 2 1 1", "stop_capture 2 1", "close 2",
                                  "close 6", "shutdown"}));
        TRACE_CHECK(state_is(service, 2U, ReceiverState::free) &&
                    state_is(service, 6U, ReceiverState::free));
    }
    // Destruction shuts down again: one more backend shutdown, no lease work.
    TRACE_CHECK(expect_trace("destructor", f.trace.take(), {"shutdown"}));
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    g_dump = argc > 1 && std::strcmp(argv[1], "--dump") == 0;
    return test_terrestrial_lifecycle_trace() && test_satellite_selection_traces() &&
                   test_tune_error_precedence() && test_tune_rollback_traces() &&
                   test_disconnect_client_traces() && test_final_stream_cleanup_traces() &&
                   test_revoke_during_capture_start() &&
                   test_separate_receivers_and_per_request_validation() &&
                   test_serialized_regression_fails_without_hanging() &&
                   test_receiver_count_and_absent_slots() && test_shutdown_trace()
               ? 0
               : 1;
}

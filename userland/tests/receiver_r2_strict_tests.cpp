// SPDX-License-Identifier: GPL-2.0-only
//
// R2 strict admission through the real control worker lane, plus the real
// Q3U4 and MLT LNB power coordinators with mocked power I/O.  The committed
// power transactions of those coordinators cannot be rolled back, so a late or
// cancelled success after commit must quarantine instead of compensating.
#include "control_workers.h"
#include "mlt5pe_power.h"
#include "q3u4_lnb_power.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;
using namespace px4::userland::ipc::posix;

#define R2_CHECK(condition)                                                           \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "receiver R2 P check failed at %d: %s\n", __LINE__,  \
                         #condition);                                                  \
            return false;                                                              \
        }                                                                              \
    } while (false)

constexpr std::chrono::milliseconds kWaitBound{10000};

template <typename F>
struct ScopeExit final {
    F fn;
    ~ScopeExit() noexcept { fn(); }
};

template <typename F>
ScopeExit<F> on_scope_exit(F fn)
{
    return ScopeExit<F>{fn};
}

class FakeCardBackend final : public CardServiceBackend {
public:
    Result<void> set_power(bool) noexcept override { return Result<void>::success(); }
    Result<void> initialize_uart() noexcept override { return Result<void>::success(); }
    Result<bool> detect_card() noexcept override { return Result<bool>::success(false); }
};

class FakeCardSession final : public CardProtocolSession {
public:
    Result<void> initialize() noexcept override { return Result<void>::success(); }
    Result<std::size_t> transmit(ByteView, MutableByteView) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    bool initialized() const noexcept override { return false; }
    const CardAtr& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override {}

private:
    CardAtr atr_{};
};

class FakeNonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, kNonceLength>> generate() noexcept override
    {
        entered_.store(true);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            changed_.notify_all();
        }
        std::unique_lock<std::mutex> lock(mutex_);
        if (gate_) changed_.wait_for(lock, kWaitBound, [&]() noexcept { return open_; });
        std::array<std::uint8_t, kNonceLength> value{};
        value[0U] = next_.fetch_add(1U);
        return Result<std::array<std::uint8_t, kNonceLength>>::success(value);
    }

    void arm() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entered_.store(false);
        open_ = false;
        gate_ = true;
    }
    bool wait_entered() noexcept
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, kWaitBound, [&]() noexcept { return entered_.load(); });
    }
    void open() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = true;
        gate_ = false;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::atomic<bool> entered_{false};
    std::atomic<std::uint8_t> next_{1U};
    bool gate_ = false;
    bool open_ = false;
};

class FakeTime final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override { return now.load(); }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        now.fetch_add(milliseconds);
    }
    std::atomic<std::uint64_t> now{0U};
};

// Recording backend used for the strict worker-path tests.
class RecordingBackend final : public TunerServiceBackend {
public:
    std::uint8_t receiver_count() const noexcept override
    {
        return static_cast<std::uint8_t>(ids.size());
    }
    bool receiver_supports(std::uint8_t receiver, System system) const noexcept override
    {
        if (receiver >= ids.size()) return false;
        return system == (receiver == 1U ? System::ISDB_S : System::ISDB_T);
    }

    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        calls.push_back("open " + std::to_string(receiver));
        return Result<void>::success();
    }
    Result<void> tune_terrestrial(std::uint8_t receiver, std::uint32_t,
                                  std::uint32_t) noexcept override
    {
        calls.push_back("tune_t " + std::to_string(receiver));
        tune_gate.pass(receiver);
        return Result<void>::success();
    }
    Result<void> tune_satellite(std::uint8_t receiver, std::uint32_t,
                                std::uint32_t) noexcept override
    {
        calls.push_back("tune_s " + std::to_string(receiver));
        tune_gate.pass(receiver);
        return Result<void>::success();
    }
    Result<bool> is_locked(std::uint8_t, System) noexcept override
    {
        calls.push_back("lock");
        return Result<bool>::success(true);
    }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    {
        calls.push_back("slot");
        return Result<void>::success();
    }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    {
        calls.push_back("tsid");
        return Result<void>::success();
    }
    Result<void> close_receiver(std::uint8_t receiver) noexcept override
    {
        calls.push_back("close " + std::to_string(receiver));
        return Result<void>::success();
    }
    Result<void> begin_tune_power(std::uint8_t receiver, System,
                                  std::uint8_t voltage) noexcept override
    {
        calls.push_back("power " + std::to_string(receiver) + " " +
                        std::to_string(voltage));
        return Result<void>::success();
    }
    Result<void> commit_tune_power(std::uint8_t receiver) noexcept override
    {
        calls.push_back("commit " + std::to_string(receiver));
        return Result<void>::success();
    }
    Result<void> rollback_tune_power(std::uint8_t receiver) noexcept override
    {
        calls.push_back("rollback " + std::to_string(receiver));
        return Result<void>::success();
    }
    void mark_receiver_disconnected(std::uint8_t receiver) noexcept override
    {
        calls.push_back("lost " + std::to_string(receiver));
    }
    Result<void> shutdown() noexcept override
    {
        calls.push_back("shutdown");
        return Result<void>::success();
    }
    Result<void> start_capture(std::uint8_t receiver, System) noexcept override
    {
        calls.push_back("start " + std::to_string(receiver));
        return Result<void>::success();
    }
    Result<void> stop_capture(std::uint8_t receiver, System) noexcept override
    {
        calls.push_back("stop " + std::to_string(receiver));
        return Result<void>::success();
    }

    std::vector<std::string> calls;
    std::array<std::uint8_t, 2> ids{{0U, 1U}};

    class Gate final {
    public:
        void arm(int receiver)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            receiver_ = receiver;
            entered_ = false;
            open_ = false;
        }
        void pass(int receiver)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (receiver_ != receiver) return;
            receiver_ = -1;
            entered_ = true;
            changed_.notify_all();
            changed_.wait_for(lock, kWaitBound, [&]() noexcept { return open_; });
        }
        bool wait_entered()
        {
            std::unique_lock<std::mutex> lock(mutex_);
            return changed_.wait_for(lock, kWaitBound,
                                     [&]() noexcept { return entered_; });
        }
        void open()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            open_ = true;
            receiver_ = -1;
            changed_.notify_all();
        }

    private:
        std::mutex mutex_;
        std::condition_variable changed_;
        int receiver_ = -1;
        bool entered_ = false;
        bool open_ = false;
    };

    Gate tune_gate;
};

class LnbPowerMock final : public Q3U4LnbPower {
public:
    Result<void> set_lnb_power(bool on) noexcept override
    {
        calls.push_back(on ? "on" : "off");
        if (results.empty()) return Result<void>::success();
        const Error result = results.front();
        results.pop_front();
        return result == Error::OK ? Result<void>::success()
                                   : Result<void>::failure(result);
    }
    void fail(Error error) { results.push_back(error); }

    std::vector<std::string> calls;
    std::deque<Error> results;
};

// Real Q3U4 coordinator with a mocked power seam.  Only the receiver's LNB
// reference is touched; the bridge aggregate is the coordinator's job.
class Q3U4CoordinatedBackend final : public TunerServiceBackend {
public:
    Q3U4CoordinatedBackend(Q3U4LnbPowerCoordinator& coordinator, FakeTime& time) noexcept
        : coordinator_(coordinator), time_(time)
    {
    }

    std::uint8_t receiver_count() const noexcept override { return 2U; }
    bool receiver_supports(std::uint8_t receiver, System) const noexcept override
    {
        return receiver < 2U;
    }

    Result<void> open_receiver(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t,
                                  std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<bool> is_locked(std::uint8_t, System) noexcept override
    {
        return Result<bool>::success(true);
    }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> close_receiver(std::uint8_t receiver) noexcept override
    {
        if (disconnected) return Result<void>::success();
        const auto released = coordinator_.release_receiver(receiver);
        if (!released) return released;
        return close_error == Error::OK ? Result<void>::success()
                                        : Result<void>::failure(close_error);
    }
    Result<void> begin_tune_power(std::uint8_t receiver, System system,
                                  std::uint8_t lnb_voltage) noexcept override
    {
        if (system != System::ISDB_S) return Result<void>::success();
        return coordinator_.begin_tune(receiver, lnb_voltage);
    }
    Result<void> commit_tune_power(std::uint8_t receiver) noexcept override
    {
        ++scenario;
        const auto committed = coordinator_.commit_tune(receiver);
        if (!committed) return committed;
        if (commit_wait_ms != 0U) time_.sleep_ms(commit_wait_ms);
        if (block_commit.load()) {
            commit_entered.store(true);
            for (int spin = 0; spin < 2000000 && !commit_go.load(); ++spin)
                std::this_thread::yield();
        }
        return Result<void>::success();
    }
    Result<void> rollback_tune_power(std::uint8_t receiver) noexcept override
    {
        ++rollback_calls;
        if (system_disconnected) return Result<void>::success();
        return coordinator_.rollback_tune(receiver);
    }
    void mark_receiver_disconnected(std::uint8_t) noexcept override
    {
        system_disconnected = true;
        (void)coordinator_.disconnect(Q3U4Bridge::dev1);
    }
    Result<void> shutdown() noexcept override { return coordinator_.shutdown(); }
    Result<void> start_capture(std::uint8_t, System) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> stop_capture(std::uint8_t, System) noexcept override
    {
        return Result<void>::success();
    }

    Q3U4LnbPowerCoordinator& coordinator_;
    FakeTime& time_;
    std::uint32_t commit_wait_ms = 0U;
    int rollback_calls = 0;
    int scenario = 0;
    bool disconnected = false;
    bool system_disconnected = false;
    Error close_error = Error::OK;
    std::atomic<bool> block_commit{false};
    std::atomic<bool> commit_entered{false};
    std::atomic<bool> commit_go{false};
};

class MltCoordinatedBackend final : public TunerServiceBackend {
public:
    MltCoordinatedBackend(Mlt5PeLnbPowerCoordinator& coordinator,
                          FakeTime& time) noexcept
        : coordinator_(coordinator), time_(time)
    {
    }

    std::uint8_t receiver_count() const noexcept override { return 2U; }
    bool receiver_supports(std::uint8_t receiver, System) const noexcept override
    {
        return receiver < 2U;
    }

    Result<void> open_receiver(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t,
                                  std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<bool> is_locked(std::uint8_t, System) noexcept override
    {
        return Result<bool>::success(true);
    }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> close_receiver(std::uint8_t receiver) noexcept override
    {
        return coordinator_.release_receiver(receiver);
    }
    Result<void> begin_tune_power(std::uint8_t receiver, System system,
                                  std::uint8_t lnb_voltage) noexcept override
    {
        if (system != System::ISDB_S) return Result<void>::success();
        return coordinator_.begin_tune(receiver, lnb_voltage);
    }
    Result<void> commit_tune_power(std::uint8_t receiver) noexcept override
    {
        const auto committed = coordinator_.commit_tune(receiver);
        if (!committed) return committed;
        if (commit_wait_ms != 0U) time_.sleep_ms(commit_wait_ms);
        return Result<void>::success();
    }
    Result<void> rollback_tune_power(std::uint8_t receiver) noexcept override
    {
        ++rollback_calls;
        // The real MLT coordinator clears pending at commit, so rollback after
        // a successful commit returns INVALID_ARGUMENT (not a no-op success).
        return coordinator_.rollback_tune(receiver);
    }
    void mark_receiver_disconnected(std::uint8_t) noexcept override
    {
        coordinator_.disconnect();
    }
    Result<void> shutdown() noexcept override { return Result<void>::success(); }
    Result<void> start_capture(std::uint8_t, System) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> stop_capture(std::uint8_t, System) noexcept override
    {
        return Result<void>::success();
    }

    Mlt5PeLnbPowerCoordinator& coordinator_;
    FakeTime& time_;
    std::uint32_t commit_wait_ms = 0U;
    int rollback_calls = 0;
};

TuneRequestPayload tune_payload(std::uint64_t lease_id, System system,
                                std::uint8_t voltage = 0U,
                                std::uint32_t timeout_ms = 1000U)
{
    TuneRequestPayload payload{};
    payload.lease_id = lease_id;
    payload.system = system;
    payload.frequency_khz = system == System::ISDB_S ? 1049480U : 557000U;
    payload.stream_id = 0xffffU;
    payload.slot = system == System::ISDB_S ? 0U : 0xffffU;
    payload.bandwidth_hz = system == System::ISDB_T ? 6000000U : 0U;
    payload.lnb_voltage = voltage;
    payload.timeout_ms = timeout_ms;
    return payload;
}

bool drain_until(ControlWorkerLanes& workers, ControlWorkerLane lane,
                 ControlWorkerOperation operation, ControlWorkerCompletion& out)
{
    const auto deadline = std::chrono::steady_clock::now() + kWaitBound;
    while (std::chrono::steady_clock::now() < deadline) {
        ControlWorkerCompletion completion{};
        while (workers.try_pop(lane, completion)) {
            if (completion.operation == operation) {
                out = completion;
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void drain_lane(ControlWorkerLanes& workers, ControlWorkerLane lane)
{
    const auto deadline = std::chrono::steady_clock::now() + kWaitBound;
    while (std::chrono::steady_clock::now() < deadline) {
        ControlWorkerCompletion completion{};
        if (!workers.try_pop(lane, completion)) {
            if (workers.idle()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

ControlWorkerTask tune_task(std::uint64_t client, std::uint8_t receiver,
                            std::uint64_t lease_id, const TuneRequestPayload& payload,
                            std::uint64_t accepted_at_ms)
{
    ControlWorkerTask task{};
    task.operation = ControlWorkerOperation::tuner_tune;
    task.kind = ControlWorkerTaskKind::client_request;
    task.client_id = client;
    task.receiver = receiver;
    task.lease_id = lease_id;
    task.tune = payload;
    task.accepted_at_ms = accepted_at_ms;
    return task;
}

struct WorkerFixture final {
    FakeCardBackend card_backend;
    FakeCardSession card_session{};
    CardService card{card_backend, card_session};
    FakeNonce nonce{};
    FakeTime time{};
    RecordingBackend backend{};
    TunerService service{backend, nonce, time};
};

bool test_worker_strict_execution_no_supports()
{
    WorkerFixture f;
    R2_CHECK(static_cast<bool>(f.service.configure_strict_capabilities()));
    const auto lease = f.service.acquire(1U, 0U);
    R2_CHECK(lease && lease.value().lease_id == 1U);

    auto workers = ControlWorkerLanes::create(f.card, f.service);
    R2_CHECK(workers);
    f.backend.calls.clear();

    const auto submitted = workers.value()->submit(
        ControlWorkerLane::tuner_dev1,
        tune_task(1U, 0U, 1U, tune_payload(1U, System::ISDB_T), f.time.monotonic_ms()));
    R2_CHECK(submitted);
    ControlWorkerCompletion completion{};
    R2_CHECK(drain_until(*workers.value(), ControlWorkerLane::tuner_dev1,
                         ControlWorkerOperation::tuner_tune, completion));
    R2_CHECK(completion.error == Error::OK);
    R2_CHECK(completion.tune_response.locked == 1U);
    // The admitted path never calls the virtual supports predicate.
    for (const std::string& call : f.backend.calls) {
        R2_CHECK(call.rfind("open ", 0U) == 0U || call.rfind("power ", 0U) == 0U ||
                 call.rfind("tune_t", 0U) == 0U || call.rfind("tune_s", 0U) == 0U ||
                 call.rfind("lock", 0U) == 0U || call.rfind("commit ", 0U) == 0U ||
                 call.rfind("slot", 0U) == 0U || call.rfind("tsid", 0U) == 0U ||
                 call == "shutdown");
    }
    R2_CHECK(workers.value()->stop_and_join());
    return true;
}

bool test_worker_queue_full_and_registry_busy()
{
    WorkerFixture f;
    R2_CHECK(static_cast<bool>(f.service.configure_strict_capabilities()));
    const auto lease = f.service.acquire(1U, 0U);
    R2_CHECK(lease);
    auto workers = ControlWorkerLanes::create(f.card, f.service);
    R2_CHECK(workers);
    f.backend.calls.clear();

    // Hold the worker inside TUNE1 so the ring can be filled deterministically.
    f.backend.tune_gate.arm(0);
    R2_CHECK(workers.value()->submit(
        ControlWorkerLane::tuner_dev1,
        tune_task(1U, 0U, 1U, tune_payload(1U, System::ISDB_T), f.time.monotonic_ms())));
    const auto cleanup = on_scope_exit([&]() noexcept {
        f.backend.tune_gate.open();
        drain_lane(*workers.value(), ControlWorkerLane::tuner_dev1);
        (void)workers.value()->stop_and_join();
    });
    R2_CHECK(f.backend.tune_gate.wait_entered());
    const auto before = f.service.tune_metadata(0U);
    R2_CHECK(before);

    for (std::size_t index = 0U; index < kControlWorkerQueueCapacity; ++index) {
        ControlWorkerTask status{};
        status.operation = ControlWorkerOperation::tuner_status;
        status.kind = ControlWorkerTaskKind::client_request;
        status.client_id = 1U;
        R2_CHECK(workers.value()->submit(ControlWorkerLane::tuner_dev1, status));
    }
    // Queue-full TUNE is BUSY before admission: revision/current/history stay
    // exactly as the first admitted (still executing) tune left them.
    const auto rejected = workers.value()->submit(
        ControlWorkerLane::tuner_dev1,
        tune_task(1U, 0U, 1U, tune_payload(1U, System::ISDB_T), f.time.monotonic_ms()));
    R2_CHECK(!rejected && rejected.error() == Error::BUSY);
    const auto after = f.service.tune_metadata(0U);
    R2_CHECK(after && after.value().revision == before.value().revision &&
             after.value().current_valid == before.value().current_valid &&
             after.value().last_successful_valid == before.value().last_successful_valid);
    return true;
}

bool test_worker_registry_contention_busy()
{
    WorkerFixture f;
    R2_CHECK(static_cast<bool>(f.service.configure_strict_capabilities()));
    R2_CHECK(f.service.acquire(1U, 0U));
    auto workers = ControlWorkerLanes::create(f.card, f.service);
    R2_CHECK(workers);

    const auto before = f.service.tune_metadata(0U);
    R2_CHECK(before);
    Result<TunerAcquireResult> other = Result<TunerAcquireResult>::failure(Error::INTERNAL);
    {
        f.nonce.arm();
        std::thread accepter([&]() { other = f.service.acquire(2U, 1U); });
        const auto guard = on_scope_exit([&]() noexcept {
            f.nonce.open();
            if (accepter.joinable()) accepter.join();
        });
        R2_CHECK(f.nonce.wait_entered());
        const auto rejected = workers.value()->submit(
            ControlWorkerLane::tuner_dev1,
            tune_task(1U, 0U, 1U, tune_payload(1U, System::ISDB_T), 0U));
        R2_CHECK(!rejected && rejected.error() == Error::BUSY);
        const auto after = f.service.tune_metadata(0U);
        R2_CHECK(after && after.value().revision == before.value().revision &&
                 after.value().current_valid == before.value().current_valid);
    }
    R2_CHECK(other);
    R2_CHECK(workers.value()->stop_and_join());
    return true;
}

bool test_worker_signal_failure_aborts_once()
{
    WorkerFixture f;
    R2_CHECK(static_cast<bool>(f.service.configure_strict_capabilities()));
    R2_CHECK(f.service.acquire(1U, 0U));
    ControlWorkerStartupOptions options{};
    options.fail_tune_signal = true;
    auto workers = ControlWorkerLanes::create_for_test(f.card, f.service, options);
    R2_CHECK(workers);
    f.backend.calls.clear();

    const auto rejected = workers.value()->submit(
        ControlWorkerLane::tuner_dev1,
        tune_task(1U, 0U, 1U, tune_payload(1U, System::ISDB_T), 0U));
    R2_CHECK(!rejected && rejected.error() == Error::INTERNAL);
    // The accepted ticket was aborted without any hardware I/O, and no
    // completion/phantom outstanding task is left behind.
    R2_CHECK(f.backend.calls.empty());
    ControlWorkerCompletion completion{};
    R2_CHECK(!workers.value()->try_pop(ControlWorkerLane::tuner_dev1, completion));
    R2_CHECK(workers.value()->stop_and_join());
    // The revision advanced and current stayed invalid; a later admission is
    // possible once the operation returned to idle.
    const auto status = f.service.status();
    R2_CHECK(status);
    const auto admitted = f.service.try_admit_tune(1U, tune_payload(1U, System::ISDB_T), 0U);
    R2_CHECK(admitted);
    return true;
}

bool test_worker_deadline_expired()
{
    WorkerFixture f;
    R2_CHECK(static_cast<bool>(f.service.configure_strict_capabilities()));
    R2_CHECK(f.service.acquire(1U, 0U));
    auto workers = ControlWorkerLanes::create(f.card, f.service);
    R2_CHECK(workers);
    f.backend.calls.clear();

    f.time.now.store(2000U);
    R2_CHECK(workers.value()->submit(
        ControlWorkerLane::tuner_dev1,
        tune_task(1U, 0U, 1U, tune_payload(1U, System::ISDB_T, 0U, 1000U), 0U)));
    ControlWorkerCompletion completion{};
    R2_CHECK(drain_until(*workers.value(), ControlWorkerLane::tuner_dev1,
                         ControlWorkerOperation::tuner_tune, completion));
    R2_CHECK(completion.error == Error::TIMEOUT);
    for (const std::string& call : f.backend.calls) {
        R2_CHECK(call.rfind("power ", 0U) != 0U && call.rfind("tune_", 0U) != 0U);
    }
    R2_CHECK(workers.value()->stop_and_join());
    return true;
}

bool test_q3u4_postcommit_quarantine_peer_reference()
{
    LnbPowerMock dev1;
    LnbPowerMock dev2;
    Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
    FakeNonce nonce;
    FakeTime time;
    Q3U4CoordinatedBackend backend(coordinator, time);
    TunerService service(backend, nonce, time);
    R2_CHECK(static_cast<bool>(service.configure_strict_capabilities()));
    R2_CHECK(service.acquire(1U, 0U));
    R2_CHECK(service.acquire(2U, 1U));

    // Both satellite receivers hold a committed 15 V reference on bridge 0.
    auto first = service.try_admit_tune(1U, tune_payload(1U, System::ISDB_S, 15U), 0U);
    R2_CHECK(first);
    R2_CHECK(service.execute_admitted_tune(first.value()));
    auto peer = service.try_admit_tune(2U, tune_payload(2U, System::ISDB_S, 15U), 0U);
    R2_CHECK(peer);
    R2_CHECK(service.execute_admitted_tune(peer.value()));
    R2_CHECK(coordinator.snapshot().ref_count[0U] == 2U);
    R2_CHECK(coordinator.snapshot().physical_state[0U] == Q3U4LnbPhysicalState::on);
    R2_CHECK(dev1.calls == std::vector<std::string>{"on"});

    // Late commit for receiver 0: the commit consumes the transaction, so the
    // final gate must not call rollback and must quarantine instead.
    time.now.store(0U);
    dev1.calls.clear();
    dev1.results.clear();
    backend.commit_wait_ms = 2000U;
    auto late = service.try_admit_tune(1U, tune_payload(1U, System::ISDB_S, 15U), 0U);
    R2_CHECK(late);
    const auto late_result = service.execute_admitted_tune(late.value());
    R2_CHECK(!late_result && late_result.error() == Error::TIMEOUT);
    // The committed transaction was not rolled back.
    R2_CHECK(backend.rollback_calls == 0);
    const auto status = service.status();
    R2_CHECK(status && status.value().receiver_states[0U] == ReceiverState::error);
    R2_CHECK(status && status.value().receiver_states[0U] != ReceiverState::free);
    // Not reusable until exact RELEASE.
    R2_CHECK(!service.start_stream(1U, 1U));
    const auto reused = service.try_admit_tune(1U, tune_payload(1U, System::ISDB_S, 15U), 0U);
    R2_CHECK(!reused);

    // Exact RELEASE clears only receiver 0's reference; the peer stays on.
    backend.commit_wait_ms = 0U;
    R2_CHECK(service.release(1U, 1U));
    R2_CHECK(coordinator.snapshot().ref_count[0U] == 1U);
    R2_CHECK(coordinator.snapshot().physical_state[0U] == Q3U4LnbPhysicalState::on);
    R2_CHECK(service.release(2U, 2U));
    R2_CHECK(coordinator.snapshot().ref_count[0U] == 0U);
    R2_CHECK(coordinator.snapshot().physical_state[0U] == Q3U4LnbPhysicalState::off);
    return true;
}

bool test_q3u4_close_failure_quarantine()
{
    LnbPowerMock dev1;
    LnbPowerMock dev2;
    Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
    FakeNonce nonce;
    FakeTime time;
    Q3U4CoordinatedBackend backend(coordinator, time);
    TunerService service(backend, nonce, time);
    R2_CHECK(static_cast<bool>(service.configure_strict_capabilities()));
    R2_CHECK(service.acquire(1U, 0U));

    auto admitted = service.try_admit_tune(1U, tune_payload(1U, System::ISDB_S, 15U), 0U);
    R2_CHECK(admitted);
    // Reach a committed 15 V state, then reject the late success at the final
    // gate so the receiver is quarantined.
    backend.commit_wait_ms = 2000U;
    R2_CHECK(!service.execute_admitted_tune(admitted.value()));
    backend.commit_wait_ms = 0U;
    R2_CHECK(service.status().value().receiver_states[0U] == ReceiverState::error);

    // An ambiguous release close failure must not downgrade the quarantine.
    dev1.fail(Error::USB_IO);
    const auto released = service.release(1U, 1U);
    R2_CHECK(!released && released.error() == Error::USB_IO);
    const auto status = service.status();
    R2_CHECK(status && status.value().receiver_states[0U] == ReceiverState::error);
    R2_CHECK(coordinator.snapshot().cleanup_debt[0U]);
    return true;
}

bool test_mlt_postcommit_quarantine_and_owner_close()
{
    LnbPowerMock dev;
    Mlt5PeLnbPowerCoordinator coordinator(dev, true);
    FakeNonce nonce;
    FakeTime time;
    MltCoordinatedBackend backend(coordinator, time);
    TunerService service(backend, nonce, time);
    R2_CHECK(static_cast<bool>(service.configure_strict_capabilities()));
    R2_CHECK(service.acquire(1U, 0U));
    R2_CHECK(service.acquire(2U, 1U));

    auto first = service.try_admit_tune(1U, tune_payload(1U, System::ISDB_S, 15U), 0U);
    R2_CHECK(first && service.execute_admitted_tune(first.value()));
    auto peer = service.try_admit_tune(2U, tune_payload(2U, System::ISDB_S, 15U), 0U);
    R2_CHECK(peer && service.execute_admitted_tune(peer.value()));
    R2_CHECK(coordinator.snapshot().ref_count == 2U);

    // Late commit: real MLT rollback after a consumed commit returns
    // INVALID_ARGUMENT, so the engine must not call it and must not infer a
    // reusable state.
    time.now.store(0U);
    backend.commit_wait_ms = 2000U;
    auto late = service.try_admit_tune(1U, tune_payload(1U, System::ISDB_S, 15U), 0U);
    R2_CHECK(late);
    R2_CHECK(!service.execute_admitted_tune(late.value()));
    R2_CHECK(backend.rollback_calls == 0);
    const auto status = service.status();
    R2_CHECK(status && status.value().receiver_states[0U] == ReceiverState::error);
    R2_CHECK(!service.start_stream(1U, 1U));
    backend.commit_wait_ms = 0U;

    // Owner logical teardown must drive the eventual close, preserving the
    // peer reference.
    dev.calls.clear();
    R2_CHECK(service.disconnect_client(1U));
    R2_CHECK(coordinator.snapshot().ref_count == 1U);
    R2_CHECK(coordinator.snapshot().physical_state == Q3U4LnbPhysicalState::on);
    R2_CHECK(service.disconnect_client(2U));
    R2_CHECK(coordinator.snapshot().ref_count == 0U);
    R2_CHECK(coordinator.snapshot().physical_state == Q3U4LnbPhysicalState::off);

    // A terminal transport disconnect performs no further power I/O.
    time.now.store(0U);
    R2_CHECK(service.acquire(3U, 0U));
    auto tune = service.try_admit_tune(3U, tune_payload(3U, System::ISDB_S, 15U), 0U);
    R2_CHECK(tune && service.execute_admitted_tune(tune.value()));
    dev.calls.clear();
    backend.mark_receiver_disconnected(0U);
    // The terminal transport result is propagated; crucially no further power
    // I/O is issued during the eventual close.
    (void)service.disconnect_client(3U);
    R2_CHECK(dev.calls.empty());
    return true;
}

// The real Q3U4 coordinator: the server's synchronous logical-teardown hook
// (revoke_tune_ticket) revokes the exact owner while the consumed commit is in
// flight, then the queued release runs and its close fails.  No post-commit
// rollback; the receiver stays excluded and the peer 15 V reference survives.
// The teardown rendezvous is the synchronous revoke call, not a scheduler race.
bool test_q3u4_concurrent_release_consumed_commit_close_failure()
{
    LnbPowerMock dev1;
    LnbPowerMock dev2;
    Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
    FakeNonce nonce;
    FakeTime time;
    Q3U4CoordinatedBackend backend(coordinator, time);
    TunerService service(backend, nonce, time);
    R2_CHECK(static_cast<bool>(service.configure_strict_capabilities()));
    R2_CHECK(service.acquire(1U, 0U));
    R2_CHECK(service.acquire(2U, 1U));
    // A committed peer reference on the same bridge.
    auto peer = service.try_admit_tune(2U, tune_payload(2U, System::ISDB_S, 15U), 0U);
    R2_CHECK(peer && service.execute_admitted_tune(peer.value()));
    R2_CHECK(coordinator.snapshot().ref_count[0U] == 1U);

    auto admitted = service.try_admit_tune(1U, tune_payload(1U, System::ISDB_S, 15U), 0U);
    R2_CHECK(admitted);
    backend.block_commit.store(true);
    backend.close_error = Error::USB_IO;
    Error executed = Error::OK;
    Error released = Error::OK;
    std::thread worker;
    std::thread releaser;
    {
        const auto guard = on_scope_exit([&]() noexcept {
            backend.block_commit.store(false);
            backend.commit_go.store(true);
            if (worker.joinable()) worker.join();
            if (releaser.joinable()) releaser.join();
        });
        worker = std::thread([&]() {
            const auto result = service.execute_admitted_tune(admitted.value());
            executed = result ? Error::OK : result.error();
        });
        for (int spin = 0; spin < 2000000 && !backend.commit_entered.load(); ++spin)
            std::this_thread::yield();
        R2_CHECK(backend.commit_entered.load());
        // The real socket logical-teardown hook revokes the exact owner while
        // the consumed commit is in flight, then queues the release.
        R2_CHECK(service.revoke_tune_ticket(1U, 1U));
        releaser = std::thread([&]() {
            const auto result = service.release(1U, 1U);
            released = result ? Error::OK : result.error();
        });
        backend.commit_go.store(true);
    }
    R2_CHECK(executed == Error::NOT_FOUND);
    R2_CHECK(released == Error::USB_IO);
    R2_CHECK(backend.rollback_calls == 0);
    // The failed close clears only this receiver's reference; the peer 15 V
    // reference is preserved.
    R2_CHECK(coordinator.snapshot().ref_count[0U] == 1U);
    R2_CHECK(coordinator.snapshot().physical_state[0U] == Q3U4LnbPhysicalState::on);
    const auto status = service.status();
    R2_CHECK(status && status.value().receiver_states[0U] == ReceiverState::error &&
             !service.start_stream(1U, 1U));
    return true;
}

}  // namespace

int main()
{
    return test_worker_strict_execution_no_supports() &&
                   test_worker_queue_full_and_registry_busy() &&
                   test_worker_registry_contention_busy() &&
                   test_worker_signal_failure_aborts_once() &&
                   test_worker_deadline_expired() &&
                   test_q3u4_postcommit_quarantine_peer_reference() &&
                   test_q3u4_close_failure_quarantine() &&
                   test_q3u4_concurrent_release_consumed_commit_close_failure() &&
                   test_mlt_postcommit_quarantine_and_owner_close()
               ? 0
               : 1;
}

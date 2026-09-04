// SPDX-License-Identifier: GPL-2.0-only
#include "q3u4_lnb_power.h"
#include "q3u4_tuner_backend.h"

#include <array>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;

#define LNB_CHECK(condition)                                                        \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "q3u4 lnb check failed at line %d: %s\n",       \
                         __LINE__, #condition);                                     \
            return false;                                                           \
        }                                                                           \
    } while (false)

class FakeLnbPower final : public Q3U4LnbPower {
public:
    Result<void> set_lnb_power(bool on) noexcept override
    {
        calls.push_back(on);
        if (results.empty()) return Result<void>::success();
        const Error result = results.front();
        results.pop_front();
        return result == Error::OK ? Result<void>::success()
                                   : Result<void>::failure(result);
    }

    void fail(Error error) { results.push_back(error); }

    std::vector<bool> calls;
    std::deque<Error> results;
};

class NoopBridge final : public BridgeI2cMaster {
public:
    Result<void> request(BridgeI2cRequest*, std::size_t) noexcept override
    {
        return Result<void>::success();
    }
};

class NoopBackendPower final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool, Q3U4Delay&) noexcept override
    {
        return Result<void>::success();
    }
};

class NoopDelay final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t) noexcept override {}
};

bool snapshot_is(const Q3U4LnbPowerCoordinator& coordinator,
                 std::uint8_t dev1_refs, std::uint8_t dev2_refs,
                 Q3U4LnbPhysicalState dev1, Q3U4LnbPhysicalState dev2,
                 bool dev1_debt = false, bool dev2_debt = false)
{
    const auto value = coordinator.snapshot();
    return value.ref_count[0U] == dev1_refs && value.ref_count[1U] == dev2_refs &&
           value.physical_state[0U] == dev1 && value.physical_state[1U] == dev2 &&
           value.cleanup_debt[0U] == dev1_debt &&
           value.cleanup_debt[1U] == dev2_debt;
}

bool test_validation_and_opt_in_gate()
{
    FakeLnbPower dev1;
    FakeLnbPower dev2;
    Q3U4LnbPowerCoordinator denied(dev1, dev2, false);
    for (const std::uint8_t invalid : std::array<std::uint8_t, 4U>{2U, 3U, 6U, 7U}) {
        const auto result = denied.begin_tune(invalid, 0U);
        LNB_CHECK(!result && result.error() == Error::INVALID_ARGUMENT);
    }
    LNB_CHECK(denied.begin_tune(0U, 0U));
    LNB_CHECK(denied.commit_tune(0U));
    const auto denied_15 = denied.begin_tune(0U, 15U);
    LNB_CHECK(!denied_15 && denied_15.error() == Error::UNSUPPORTED);
    LNB_CHECK(dev1.calls.empty() && dev2.calls.empty());
    const auto invalid_voltage = denied.begin_tune(1U, 1U);
    LNB_CHECK(!invalid_voltage && invalid_voltage.error() == Error::INVALID_ARGUMENT);
    LNB_CHECK(dev1.calls.empty() && dev2.calls.empty());
    return true;
}

bool test_bridge_reference_matrix_and_retune()
{
    FakeLnbPower dev1;
    FakeLnbPower dev2;
    Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);

    LNB_CHECK(coordinator.begin_tune(0U, 15U));
    LNB_CHECK(coordinator.commit_tune(0U));
    LNB_CHECK((dev1.calls == std::vector<bool>{true}));
    LNB_CHECK(snapshot_is(coordinator, 1U, 0U, Q3U4LnbPhysicalState::on,
                          Q3U4LnbPhysicalState::off));

    LNB_CHECK(coordinator.begin_tune(1U, 15U));
    LNB_CHECK(coordinator.commit_tune(1U));
    LNB_CHECK((dev1.calls == std::vector<bool>{true}));
    LNB_CHECK(snapshot_is(coordinator, 2U, 0U, Q3U4LnbPhysicalState::on,
                          Q3U4LnbPhysicalState::off));

    LNB_CHECK(coordinator.release_receiver(0U));
    LNB_CHECK((dev1.calls == std::vector<bool>{true}));
    LNB_CHECK(coordinator.release_receiver(1U));
    LNB_CHECK((dev1.calls == std::vector<bool>{true, false}));

    LNB_CHECK(coordinator.begin_tune(4U, 15U));
    LNB_CHECK(coordinator.commit_tune(4U));
    LNB_CHECK((dev2.calls == std::vector<bool>{true}));
    LNB_CHECK(snapshot_is(coordinator, 0U, 1U, Q3U4LnbPhysicalState::off,
                          Q3U4LnbPhysicalState::on));

    // Same-voltage retune is idempotent.
    LNB_CHECK(coordinator.begin_tune(4U, 15U));
    LNB_CHECK(coordinator.commit_tune(4U));
    LNB_CHECK((dev2.calls == std::vector<bool>{true}));
    // 15 -> 0 removes the final reference before frontend tuning.
    LNB_CHECK(coordinator.begin_tune(4U, 0U));
    LNB_CHECK((dev2.calls == std::vector<bool>{true, false}));
    LNB_CHECK(coordinator.commit_tune(4U));
    // 0 -> 15 restores it.
    LNB_CHECK(coordinator.begin_tune(4U, 15U));
    LNB_CHECK((dev2.calls == std::vector<bool>{true, false, true}));
    LNB_CHECK(coordinator.commit_tune(4U));
    LNB_CHECK(coordinator.shutdown());
    LNB_CHECK((dev2.calls == std::vector<bool>{true, false, true, false}));
    return true;
}

bool test_concurrent_same_bridge_references()
{
    FakeLnbPower dev1;
    FakeLnbPower dev2;
    Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
    std::mutex gate_mutex;
    std::condition_variable gate_changed;
    std::size_t ready = 0U;
    bool go = false;
    std::array<Error, 2U> results{{Error::INTERNAL, Error::INTERNAL}};
    std::array<std::thread, 2U> threads;
    for (std::size_t index = 0U; index < threads.size(); ++index) {
        threads[index] = std::thread([&, index]() {
            {
                std::unique_lock<std::mutex> gate(gate_mutex);
                ++ready;
                gate_changed.notify_all();
                gate_changed.wait(gate, [&]() noexcept { return go; });
            }
            const std::uint8_t receiver = static_cast<std::uint8_t>(index);
            const auto begun = coordinator.begin_tune(receiver, 15U);
            if (!begun) {
                results[index] = begun.error();
                return;
            }
            results[index] = coordinator.commit_tune(receiver).error();
        });
    }
    {
        std::unique_lock<std::mutex> gate(gate_mutex);
        gate_changed.wait(gate, [&]() noexcept { return ready == threads.size(); });
        go = true;
        gate_changed.notify_all();
    }
    for (std::thread& thread : threads) thread.join();
    LNB_CHECK(results[0U] == Error::OK && results[1U] == Error::OK);
    LNB_CHECK((dev1.calls == std::vector<bool>{true}));
    LNB_CHECK(snapshot_is(coordinator, 2U, 0U, Q3U4LnbPhysicalState::on,
                          Q3U4LnbPhysicalState::off));

    ready = 0U;
    go = false;
    results = {Error::INTERNAL, Error::INTERNAL};
    for (std::size_t index = 0U; index < threads.size(); ++index) {
        threads[index] = std::thread([&, index]() {
            {
                std::unique_lock<std::mutex> gate(gate_mutex);
                ++ready;
                gate_changed.notify_all();
                gate_changed.wait(gate, [&]() noexcept { return go; });
            }
            results[index] = coordinator.release_receiver(
                static_cast<std::uint8_t>(index)).error();
        });
    }
    {
        std::unique_lock<std::mutex> gate(gate_mutex);
        gate_changed.wait(gate, [&]() noexcept { return ready == threads.size(); });
        go = true;
        gate_changed.notify_all();
    }
    for (std::thread& thread : threads) thread.join();
    LNB_CHECK(results[0U] == Error::OK && results[1U] == Error::OK);
    LNB_CHECK((dev1.calls == std::vector<bool>{true, false}));
    return true;
}

bool test_transaction_rollback()
{
    FakeLnbPower dev1;
    FakeLnbPower dev2;
    Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);

    // A post-LNB tune failure rolls 0 -> 15 back to 0.
    LNB_CHECK(coordinator.begin_tune(0U, 15U));
    LNB_CHECK(coordinator.rollback_tune(0U));
    LNB_CHECK((dev1.calls == std::vector<bool>{true, false}));
    LNB_CHECK(snapshot_is(coordinator, 0U, 0U, Q3U4LnbPhysicalState::off,
                          Q3U4LnbPhysicalState::off));

    LNB_CHECK(coordinator.begin_tune(0U, 15U));
    LNB_CHECK(coordinator.commit_tune(0U));
    // A failed 15 -> 0 retune restores the previous 15 V request.
    LNB_CHECK(coordinator.begin_tune(0U, 0U));
    LNB_CHECK(coordinator.rollback_tune(0U));
    LNB_CHECK((dev1.calls == std::vector<bool>{true, false, true, false, true}));
    LNB_CHECK(snapshot_is(coordinator, 1U, 0U, Q3U4LnbPhysicalState::on,
                          Q3U4LnbPhysicalState::off));
    return true;
}

bool test_ambiguous_transition_and_cleanup_debt()
{
    {
        FakeLnbPower dev1;
        FakeLnbPower dev2;
        Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
        // Enable may have reached hardware, then response was lost. begin_tune
        // restores the prior zero-ref state with an explicit low write.
        dev1.fail(Error::TIMEOUT);
        const auto result = coordinator.begin_tune(0U, 15U);
        LNB_CHECK(!result && result.error() == Error::TIMEOUT);
        LNB_CHECK((dev1.calls == std::vector<bool>{true, false}));
        LNB_CHECK(snapshot_is(coordinator, 0U, 0U, Q3U4LnbPhysicalState::off,
                              Q3U4LnbPhysicalState::off));
    }
    {
        FakeLnbPower dev1;
        FakeLnbPower dev2;
        Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
        dev1.fail(Error::USB_IO);
        dev1.fail(Error::TIMEOUT);
        const auto result = coordinator.begin_tune(0U, 15U);
        LNB_CHECK(!result && result.error() == Error::USB_IO);
        LNB_CHECK(snapshot_is(coordinator, 0U, 0U, Q3U4LnbPhysicalState::unknown,
                              Q3U4LnbPhysicalState::off, true, false));
        LNB_CHECK(coordinator.reconcile());
        LNB_CHECK((dev1.calls == std::vector<bool>{true, false, false}));
        LNB_CHECK(snapshot_is(coordinator, 0U, 0U, Q3U4LnbPhysicalState::off,
                              Q3U4LnbPhysicalState::off));
    }
    {
        FakeLnbPower dev1;
        FakeLnbPower dev2;
        Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
        LNB_CHECK(coordinator.begin_tune(1U, 15U));
        LNB_CHECK(coordinator.commit_tune(1U));
        dev1.fail(Error::USB_IO);
        const auto released = coordinator.release_receiver(1U);
        LNB_CHECK(!released && released.error() == Error::USB_IO);
        LNB_CHECK(snapshot_is(coordinator, 0U, 0U, Q3U4LnbPhysicalState::unknown,
                              Q3U4LnbPhysicalState::off, true, false));
        LNB_CHECK(coordinator.shutdown());
        LNB_CHECK((dev1.calls == std::vector<bool>{true, false, false}));
    }
    {
        FakeLnbPower dev1;
        FakeLnbPower dev2;
        Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
        LNB_CHECK(coordinator.begin_tune(0U, 15U));
        LNB_CHECK(coordinator.commit_tune(0U));
        // A lost response while lowering may already have disabled the rail;
        // restore the previous successful 15 V request explicitly.
        dev1.fail(Error::TIMEOUT);
        const auto result = coordinator.begin_tune(0U, 0U);
        LNB_CHECK(!result && result.error() == Error::TIMEOUT);
        LNB_CHECK((dev1.calls == std::vector<bool>{true, false, true}));
        LNB_CHECK(snapshot_is(coordinator, 1U, 0U, Q3U4LnbPhysicalState::on,
                              Q3U4LnbPhysicalState::off));
    }
    return true;
}

bool test_disconnect_and_surviving_bridge_cleanup()
{
    FakeLnbPower dev1;
    FakeLnbPower dev2;
    Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
    LNB_CHECK(coordinator.begin_tune(0U, 15U));
    LNB_CHECK(coordinator.commit_tune(0U));
    LNB_CHECK(coordinator.begin_tune(4U, 15U));
    LNB_CHECK(coordinator.commit_tune(4U));
    LNB_CHECK(coordinator.disconnect(Q3U4Bridge::dev1));

    const auto future = coordinator.begin_tune(5U, 15U);
    LNB_CHECK(!future && future.error() == Error::DISCONNECTED);
    const auto shutdown = coordinator.shutdown();
    LNB_CHECK(!shutdown && shutdown.error() == Error::DISCONNECTED);
    // No post-disconnect write on dev1; surviving dev2 is lowered.
    LNB_CHECK((dev1.calls == std::vector<bool>{true}));
    LNB_CHECK((dev2.calls == std::vector<bool>{true, false}));
    LNB_CHECK(snapshot_is(coordinator, 0U, 0U,
                          Q3U4LnbPhysicalState::disconnected,
                          Q3U4LnbPhysicalState::off));

    FakeLnbPower lost;
    FakeLnbPower survivor;
    Q3U4LnbPowerCoordinator operation_disconnect(lost, survivor, true);
    lost.fail(Error::DISCONNECTED);
    const auto failed = operation_disconnect.begin_tune(0U, 15U);
    LNB_CHECK(!failed && failed.error() == Error::DISCONNECTED);
    LNB_CHECK((lost.calls == std::vector<bool>{true}));
    LNB_CHECK(!operation_disconnect.reconcile());
    LNB_CHECK((lost.calls == std::vector<bool>{true}));

    FakeLnbPower failing_dev1;
    FakeLnbPower cleaned_dev2;
    Q3U4LnbPowerCoordinator attempt_both(failing_dev1, cleaned_dev2, true);
    LNB_CHECK(attempt_both.begin_tune(0U, 15U));
    LNB_CHECK(attempt_both.commit_tune(0U));
    LNB_CHECK(attempt_both.begin_tune(4U, 15U));
    LNB_CHECK(attempt_both.commit_tune(4U));
    failing_dev1.fail(Error::USB_IO);
    const auto both_shutdown = attempt_both.shutdown();
    LNB_CHECK(!both_shutdown && both_shutdown.error() == Error::USB_IO);
    LNB_CHECK((failing_dev1.calls == std::vector<bool>{true, false}));
    LNB_CHECK((cleaned_dev2.calls == std::vector<bool>{true, false}));
    LNB_CHECK(snapshot_is(attempt_both, 0U, 0U,
                          Q3U4LnbPhysicalState::unknown,
                          Q3U4LnbPhysicalState::off, true, false));
    return true;
}

bool test_production_tuner_adapter_uses_narrow_authority()
{
    {
        NoopBridge bridge1;
        NoopBridge bridge2;
        NoopBackendPower backend_power1;
        NoopBackendPower backend_power2;
        NoopDelay delay;
        Q3U4FrontendEnclosure enclosure(bridge1, bridge2, backend_power1,
                                        backend_power2, delay);
        FakeLnbPower dev1;
        FakeLnbPower dev2;
        Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
        Q3U4FrontendTunerBackend backend(enclosure, coordinator);

        LNB_CHECK(backend.begin_tune_power(0U, ipc::System::ISDB_S, 15U));
        LNB_CHECK(backend.commit_tune_power(0U));
        LNB_CHECK((dev1.calls == std::vector<bool>{true}));
        LNB_CHECK(backend.begin_tune_power(2U, ipc::System::ISDB_T, 0U));
        const auto invalid_t = backend.begin_tune_power(
            2U, ipc::System::ISDB_T, 15U);
        LNB_CHECK(!invalid_t && invalid_t.error() == Error::INVALID_ARGUMENT);
        const auto wrong_system = backend.begin_tune_power(
            0U, ipc::System::ISDB_T, 0U);
        LNB_CHECK(!wrong_system && wrong_system.error() == Error::INVALID_ARGUMENT);
        LNB_CHECK(backend.shutdown());
        LNB_CHECK((dev1.calls == std::vector<bool>{true, false}));
        LNB_CHECK(dev2.calls.empty());
    }

    // The production metadata notification maps a lost receiver to its bridge.
    // Shutdown must never touch that bridge again, but it must still lower the
    // surviving bridge before its transport is destroyed.
    {
        NoopBridge bridge1;
        NoopBridge bridge2;
        NoopBackendPower backend_power1;
        NoopBackendPower backend_power2;
        NoopDelay delay;
        Q3U4FrontendEnclosure enclosure(bridge1, bridge2, backend_power1,
                                        backend_power2, delay);
        FakeLnbPower dev1;
        FakeLnbPower dev2;
        Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
        Q3U4FrontendTunerBackend backend(enclosure, coordinator);
        LNB_CHECK(backend.begin_tune_power(0U, ipc::System::ISDB_S, 15U));
        LNB_CHECK(backend.commit_tune_power(0U));
        LNB_CHECK(backend.begin_tune_power(4U, ipc::System::ISDB_S, 15U));
        LNB_CHECK(backend.commit_tune_power(4U));

        backend.mark_receiver_disconnected(0U);
        const auto shutdown = backend.shutdown();
        LNB_CHECK(!shutdown && shutdown.error() == Error::DISCONNECTED);
        LNB_CHECK((dev1.calls == std::vector<bool>{true}));
        LNB_CHECK((dev2.calls == std::vector<bool>{true, false}));
    }
    return true;
}

}  // namespace

bool run_q3u4_lnb_power_tests()
{
    const bool validation = test_validation_and_opt_in_gate();
    const bool matrix = test_bridge_reference_matrix_and_retune();
    const bool concurrency = test_concurrent_same_bridge_references();
    const bool rollback = test_transaction_rollback();
    const bool ambiguity = test_ambiguous_transition_and_cleanup_debt();
    const bool disconnect = test_disconnect_and_surviving_bridge_cleanup();
    const bool adapter = test_production_tuner_adapter_uses_narrow_authority();
    return validation && matrix && concurrency && rollback && ambiguity &&
           disconnect && adapter;
}

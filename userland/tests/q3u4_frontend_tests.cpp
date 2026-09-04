// Modified/ported for px4-userland on 2026-09-03.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: driver/px4_device.c,
// winusb/src/DriverHost_PX4/px4_device.cpp.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only

#include "q3u4_frontend.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <initializer_list>
#include <mutex>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace px4::userland;

#define FRONTEND_LIFECYCLE_CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "q3u4 frontend check failed at %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

class Bridge final : public BridgeI2cMaster {
public:
    Result<void> request(BridgeI2cRequest* requests, std::size_t count) noexcept override
    {
        ++requests_seen;
        batches.emplace_back();
        for (std::size_t i = 0U; i < count; ++i) {
            batches.back().push_back({requests[i].address,
                                      requests[i].type == BridgeI2cRequestType::write
                                          ? std::vector<std::uint8_t>(requests[i].write_data.data,
                                                                       requests[i].write_data.data + requests[i].write_data.size)
                                          : std::vector<std::uint8_t>{}});
            if (requests[i].type == BridgeI2cRequestType::read) {
                for (std::size_t j = 0U; j < requests[i].read_data.size; ++j)
                    requests[i].read_data.data[j] = 0U;
                if (requests[i].read_data.size == 1U && count == 3U)
                    requests[i].read_data.data[0] = 0x98U;
                if (requests[i].read_data.size == 1U && count == 2U &&
                    requests[0].write_data.size == 1U && requests[0].write_data.data[0] == 0xb0U)
                    requests[i].read_data.data[0] = 0x0fU;
                if (requests[i].read_data.size == 3U) requests[i].read_data.data[2] = 0x02U;
                if (requests[i].read_data.size == 4U) requests[i].read_data.data[3] = 0x70U;
                if (pll_script_enabled && requests[i].read_data.size == 3U && !pll_script.empty()) {
                    const int scripted = pll_script.front();
                    pll_script.pop_front();
                    if (scripted < 0) {
                        batches.back()[i].data.assign(requests[i].read_data.data,
                                                      requests[i].read_data.data + requests[i].read_data.size);
                        return Result<void>::failure(failure);
                    }
                    requests[i].read_data.data[2] = scripted == 1 ? 0x02U : 0U;
                }
                if (!read_payloads.empty() && read_payloads.front().size() == requests[i].read_data.size) {
                    const auto payload = read_payloads.front();
                    read_payloads.pop_front();
                    for (std::size_t j = 0U; j < payload.size(); ++j)
                        requests[i].read_data.data[j] = payload[j];
                }
                batches.back()[i].data.assign(requests[i].read_data.data,
                                              requests[i].read_data.data + requests[i].read_data.size);
            }
        }
        if (fail_at != static_cast<std::size_t>(-1) && requests_seen - 1U == fail_at)
            return Result<void>::failure(failure);
        return Result<void>::success();
    }

    void queue_read(std::vector<std::uint8_t> payload) { read_payloads.push_back(std::move(payload)); }

    std::size_t requests_seen = 0U;
    struct Operation { std::uint8_t address; std::vector<std::uint8_t> data; };
    std::vector<std::vector<Operation>> batches;
    std::deque<std::vector<std::uint8_t>> read_payloads;
    std::size_t fail_at = static_cast<std::size_t>(-1);
    Error failure = Error::USB_IO;
    bool pll_script_enabled = false;
    std::deque<int> pll_script;
};

class Delay final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        delays.push_back(milliseconds);
    }

    std::vector<std::uint32_t> delays;
    std::mutex mutex;
};

class Power final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool on, Q3U4Delay&) noexcept override
    {
        states.push_back(on);
        if (!outcomes.empty()) {
            const auto outcome = outcomes.front();
            outcomes.pop_front();
            if (outcome != Error::OK) return Result<void>::failure(outcome);
        }
        return Result<void>::success();
    }

    std::vector<bool> states;
    std::deque<Error> outcomes;
};

class CoordinatorBackend final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool on, Q3U4Delay&) noexcept override
    {
        states.push_back(on);
        if (!outcomes.empty()) {
            const auto outcome = outcomes.front();
            outcomes.pop_front();
            if (outcome != Error::OK) return Result<void>::failure(outcome);
        }
        return Result<void>::success();
    }

    std::vector<bool> states;
    std::deque<Error> outcomes;
};

class Purger final : public Q3U4PsbPurger {
public:
    Result<void> purge() noexcept override
    {
        ++calls;
        if (outcomes.empty()) return Result<void>::success();
        const Error outcome = outcomes.front();
        outcomes.pop_front();
        return outcome == Error::OK ? Result<void>::success()
                                    : Result<void>::failure(outcome);
    }

    std::size_t calls = 0U;
    std::deque<Error> outcomes;
};

bool check_enclosure_power(const Q3U4FrontendEnclosure& enclosure,
                           std::uint8_t receiver_mask,
                           Q3U4PowerState dev1,
                           Q3U4PowerState dev2)
{
    const auto snapshot = enclosure.power_snapshot();
    return snapshot.receiver_mask == receiver_mask &&
           !snapshot.card_acquired &&
           snapshot.backend_state[0] == dev1 &&
           snapshot.backend_state[1] == dev2;
}

bool check_enclosure_power_and_card(const Q3U4FrontendEnclosure& enclosure,
                                    std::uint8_t receiver_mask,
                                    bool card,
                                    Q3U4PowerState dev1,
                                    Q3U4PowerState dev2)
{
    const auto snapshot = enclosure.power_snapshot();
    return snapshot.receiver_mask == receiver_mask &&
           snapshot.card_acquired == card &&
           snapshot.backend_state[0] == dev1 &&
           snapshot.backend_state[1] == dev2;
}

bool no_bridge_address_since(const Bridge& bridge, std::size_t first_batch,
                             std::uint8_t address)
{
    for (std::size_t index = first_batch; index < bridge.batches.size(); ++index) {
        for (const auto& operation : bridge.batches[index]) {
            if (operation.address == address) return false;
        }
    }
    return true;
}

std::size_t find_batch_index(
    const Bridge& bridge, std::uint8_t address,
    std::initializer_list<std::initializer_list<std::uint8_t>> expected)
{
    for (std::size_t batch_index = 0U; batch_index < bridge.batches.size(); ++batch_index) {
        const auto& batch = bridge.batches[batch_index];
        if (batch.size() != expected.size()) continue;
        std::size_t operation_index = 0U;
        bool match = true;
        for (const auto values : expected) {
            if (batch[operation_index].address != address ||
                batch[operation_index].data != std::vector<std::uint8_t>(values)) {
                match = false;
            }
            ++operation_index;
        }
        if (match) return batch_index;
    }
    return static_cast<std::size_t>(-1);
}

std::size_t find_batch_index_after(
    const Bridge& bridge, std::size_t after, std::uint8_t address,
    std::initializer_list<std::initializer_list<std::uint8_t>> expected)
{
    for (std::size_t batch_index = after + 1U; batch_index < bridge.batches.size(); ++batch_index) {
        const auto& batch = bridge.batches[batch_index];
        if (batch.size() != expected.size()) continue;
        std::size_t operation_index = 0U;
        bool match = true;
        for (const auto values : expected) {
            if (batch[operation_index].address != address ||
                batch[operation_index].data != std::vector<std::uint8_t>(values)) {
                match = false;
            }
            ++operation_index;
        }
        if (match) return batch_index;
    }
    return static_cast<std::size_t>(-1);
}

bool test_q3u4_frontend_lifecycle_contract()
{
    Bridge bridge;
    Delay delay;
    Power power;
    {
        Q3U4Frontend frontend(bridge, power, delay);
        FRONTEND_LIFECYCLE_CHECK(!frontend.open_terrestrial(0U));
        FRONTEND_LIFECYCLE_CHECK(!frontend.open_terrestrial(1U));
        FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == 0U);
        FRONTEND_LIFECYCLE_CHECK(power.states.empty());
        FRONTEND_LIFECYCLE_CHECK(frontend.open_terrestrial(2U));
        FRONTEND_LIFECYCLE_CHECK(frontend.open_receiver() == 2);
        FRONTEND_LIFECYCLE_CHECK(frontend.terrestrial_loop_through(2U).value());
        FRONTEND_LIFECYCLE_CHECK(!frontend.terrestrial_loop_through(3U).value());
        const auto tc_init = find_batch_index(
            bridge, 0x10U, {{0xb0U, 0xa0U}, {0xb2U, 0x3dU}, {0xb3U, 0x25U},
                            {0xb4U, 0x8bU}, {0xb5U, 0x4bU}, {0xb6U, 0x3fU},
                            {0xb7U, 0xffU}, {0xb8U, 0xc0U}, {0x1fU, 0x00U},
                            {0x75U, 0x00U}});
        const auto ts_disable = find_batch_index(bridge, 0x10U, {{0x1dU, 0xa8U}});
        const auto wake = find_batch_index(bridge, 0x10U, {{0x03U, 0x00U}});
        const auto shared_s0 =
            find_batch_index(bridge, 0x11U, {{0x07U, 0x31U}, {0x08U, 0x77U}});
        const auto shared_t0 =
            find_batch_index(bridge, 0x10U, {{0x0eU, 0x77U}, {0x0fU, 0x13U}});
        // R850 wake/set_system are intentionally state-only in the accepted
        // portable driver and occur between the observable wake and S0 batches.
        FRONTEND_LIFECYCLE_CHECK(tc_init < ts_disable);
        FRONTEND_LIFECYCLE_CHECK(ts_disable < wake);
        FRONTEND_LIFECYCLE_CHECK(wake < shared_s0);
        FRONTEND_LIFECYCLE_CHECK(shared_s0 < shared_t0);

        const auto requests_after_open = bridge.requests_seen;
        FRONTEND_LIFECYCLE_CHECK(!frontend.open_terrestrial(3U));
        FRONTEND_LIFECYCLE_CHECK(power.states.size() == 1U);
        FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == requests_after_open);
        const auto tuned = frontend.tune_terrestrial(527143U);
        if (!tuned) {
            std::fprintf(stderr, "tune error: %s\n", error_string(tuned.error()));
            return false;
        }
        const auto locked = frontend.is_terrestrial_locked();
        FRONTEND_LIFECYCLE_CHECK(locked && locked.value());
        bridge.queue_read(std::vector<std::uint8_t>{0x28U});
        const auto unlocked = frontend.is_terrestrial_locked();
        FRONTEND_LIFECYCLE_CHECK(unlocked && !unlocked.value());
        const auto requests_before_close = bridge.requests_seen;
        FRONTEND_LIFECYCLE_CHECK(frontend.close());
        FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == requests_before_close);
        FRONTEND_LIFECYCLE_CHECK(power.states.size() == 2U && !power.states.back());
        const auto requests_after_close = bridge.requests_seen;
        FRONTEND_LIFECYCLE_CHECK(frontend.close());
        FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == requests_after_close);
    }
    FRONTEND_LIFECYCLE_CHECK(power.states.size() == 2U);
    return true;
}

bool test_q3u4_local3_and_preopen_contract()
{
    Bridge bridge;
    Delay delay;
    Power power;
    Q3U4Frontend frontend(bridge, power, delay);
    FRONTEND_LIFECYCLE_CHECK(!frontend.tune_terrestrial(527143U));
    FRONTEND_LIFECYCLE_CHECK(!frontend.is_terrestrial_locked());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == 0U && power.states.empty());
    FRONTEND_LIFECYCLE_CHECK(frontend.open_terrestrial(3U));
    FRONTEND_LIFECYCLE_CHECK(!frontend.terrestrial_loop_through(3U).value());
    FRONTEND_LIFECYCLE_CHECK(frontend.close());
    FRONTEND_LIFECYCLE_CHECK(power.states.size() == 2U && !power.states.back());
    return true;
}

bool test_q3u4_frontend_power_and_i2c_failures()
{
    Bridge bridge;
    Delay delay;
    Power power;
    power.outcomes = {Error::USB_IO, Error::OK};
    Q3U4Frontend frontend(bridge, power, delay);
    FRONTEND_LIFECYCLE_CHECK(!frontend.open_terrestrial(2U));
    FRONTEND_LIFECYCLE_CHECK(power.states.size() == 2U && !power.states.back());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == 0U);

    bridge.fail_at = 0U;
    const auto failed = frontend.open_terrestrial(2U);
    FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::USB_IO);
    FRONTEND_LIFECYCLE_CHECK(std::string_view(frontend.diagnostic_stage()) ==
                             "tuner_initialize_rt710_0");
    FRONTEND_LIFECYCLE_CHECK(power.states.size() == 4U && !power.states.back());
    FRONTEND_LIFECYCLE_CHECK(frontend.close());
    return true;
}

bool check_late_open_failure(std::size_t fail_at, bool fail_cleanup,
                             const char* expected_stage)
{
    Bridge bridge;
    Delay delay;
    Power power;
    if (fail_cleanup) {
        power.outcomes = {Error::OK, Error::DISCONNECTED, Error::OK};
    }
    bridge.fail_at = fail_at;
    Q3U4Frontend frontend(bridge, power, delay);
    const auto failed = frontend.open_terrestrial(2U);
    FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::USB_IO);
    FRONTEND_LIFECYCLE_CHECK(std::string_view(frontend.diagnostic_stage()) == expected_stage);
    FRONTEND_LIFECYCLE_CHECK(!frontend.open_state());
    FRONTEND_LIFECYCLE_CHECK(power.states.size() == 2U);
    FRONTEND_LIFECYCLE_CHECK(power.states[0] && !power.states[1]);
    const auto requests_after_failure = bridge.requests_seen;
    const auto power_after_failure = power.states.size();
    FRONTEND_LIFECYCLE_CHECK(frontend.close());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == requests_after_failure);
    FRONTEND_LIFECYCLE_CHECK(power.states.size() == power_after_failure + (fail_cleanup ? 1U : 0U));
    if (fail_cleanup) FRONTEND_LIFECYCLE_CHECK(!power.states.back());
    FRONTEND_LIFECYCLE_CHECK(frontend.close());
    return true;
}

bool test_q3u4_late_open_rollbacks()
{
    Bridge reference_bridge;
    Delay reference_delay;
    Power reference_power;
    Q3U4Frontend reference(reference_bridge, reference_power, reference_delay);
    FRONTEND_LIFECYCLE_CHECK(reference.open_terrestrial(2U));
    const auto prepare = find_batch_index(
        reference_bridge, 0x10U,
        {{0xb0U, 0xa0U}, {0xb2U, 0x3dU}, {0xb3U, 0x25U}, {0xb4U, 0x8bU},
         {0xb5U, 0x4bU}, {0xb6U, 0x3fU}, {0xb7U, 0xffU}, {0xb8U, 0xc0U},
         {0x1fU, 0x00U}, {0x75U, 0x00U}});
    const auto shared_t0 = find_batch_index(
        reference_bridge, 0x10U, {{0x0eU, 0x77U}, {0x0fU, 0x13U}});
    FRONTEND_LIFECYCLE_CHECK(prepare != static_cast<std::size_t>(-1));
    FRONTEND_LIFECYCLE_CHECK(shared_t0 != static_cast<std::size_t>(-1));
    FRONTEND_LIFECYCLE_CHECK(reference.close());
    FRONTEND_LIFECYCLE_CHECK(check_late_open_failure(prepare, false, "selected_tc_init_writes"));
    FRONTEND_LIFECYCLE_CHECK(check_late_open_failure(shared_t0, false, "shared_t0"));
    // The open USB_IO remains the reported error even when cleanup OFF returns
    // DISCONNECTED; the following close retries OFF and succeeds.
    FRONTEND_LIFECYCLE_CHECK(check_late_open_failure(prepare, true, "selected_tc_init_writes"));
    return true;
}

bool test_q3u4_pll_polling_contract()
{
    const auto run = [](std::deque<int> script, std::size_t expected_delays,
                        Error expected_error, const char* expected_stage) {
        Bridge bridge;
        Delay delay;
        Power power;
        Q3U4Frontend frontend(bridge, power, delay);
        if (!frontend.open_terrestrial(2U)) return false;
        const auto before = delay.delays.size();
        bridge.pll_script_enabled = true;
        bridge.pll_script = std::move(script);
        const auto tuned = frontend.tune_terrestrial(527143U);
        if (expected_error == Error::OK) {
            if (!tuned) return false;
        } else if (tuned || tuned.error() != expected_error) {
            return false;
        }
        if (std::string_view(frontend.diagnostic_stage()) != expected_stage) return false;
        if (delay.delays.size() - before != expected_delays) {
            return false;
        }
        if (!bridge.pll_script.empty()) return false;
        if (expected_error == Error::OK) {
            const auto demod_lock = frontend.is_terrestrial_locked();
            if (!demod_lock || !demod_lock.value()) return false;
        }
        return static_cast<bool>(frontend.close());
    };
    // Every count includes one delay from R850 PLL programming. Each failed or
    // unlocked legacy poll contributes one additional 10 ms delay.
    if (!run({1}, 1U, Error::OK, "tuned")) return false;
    if (!run({-1, 1}, 2U, Error::OK, "tuned")) return false;
    if (!run(std::deque<int>(50U, 0), 51U, Error::TIMEOUT, "pll_poll")) return false;
    if (!run(std::deque<int>(50U, -1), 51U, Error::USB_IO, "pll_poll")) return false;
    return true;
}

bool test_q3u4_cleanup_power_stage()
{
    Bridge bridge;
    Delay delay;
    Power power;
    Q3U4Frontend frontend(bridge, power, delay);
    FRONTEND_LIFECYCLE_CHECK(frontend.open_terrestrial(2U));
    power.outcomes.push_back(Error::DISCONNECTED);
    const auto closed = frontend.close();
    FRONTEND_LIFECYCLE_CHECK(!closed && closed.error() == Error::DISCONNECTED);
    FRONTEND_LIFECYCLE_CHECK(std::string_view(frontend.diagnostic_stage()) == "cleanup_power_off");
    return true;
}

bool test_q3u4_capture_lifecycle()
{
    Bridge bridge;
    Delay delay;
    Power power;
    Q3U4Frontend frontend(bridge, power, delay);

    const auto before_start = bridge.requests_seen;
    FRONTEND_LIFECYCLE_CHECK(!frontend.start_terrestrial_capture());
    FRONTEND_LIFECYCLE_CHECK(frontend.diagnostic_stage() != nullptr);
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == before_start);
    FRONTEND_LIFECYCLE_CHECK(!frontend.capture_active());

    FRONTEND_LIFECYCLE_CHECK(frontend.open_terrestrial(2U));
    const auto before_tune_start = bridge.requests_seen;
    FRONTEND_LIFECYCLE_CHECK(!frontend.start_terrestrial_capture());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == before_tune_start);
    FRONTEND_LIFECYCLE_CHECK(frontend.tune_terrestrial(527143U));
    const auto before_enable = bridge.requests_seen;
    FRONTEND_LIFECYCLE_CHECK(frontend.start_terrestrial_capture());
    FRONTEND_LIFECYCLE_CHECK(frontend.capture_active());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == before_enable + 1U);
    FRONTEND_LIFECYCLE_CHECK(find_batch_index(bridge, 0x10U, {{0x1dU, 0x00U}}) !=
                             static_cast<std::size_t>(-1));

    const auto before_duplicate = bridge.requests_seen;
    const auto duplicate = frontend.start_terrestrial_capture();
    FRONTEND_LIFECYCLE_CHECK(!duplicate && duplicate.error() == Error::BUSY);
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == before_duplicate);

    const auto before_stop = bridge.requests_seen;
    FRONTEND_LIFECYCLE_CHECK(frontend.stop_terrestrial_capture());
    FRONTEND_LIFECYCLE_CHECK(!frontend.capture_active());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == before_stop + 1U);
    FRONTEND_LIFECYCLE_CHECK(frontend.stop_terrestrial_capture());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == before_stop + 1U);
    FRONTEND_LIFECYCLE_CHECK(frontend.close());
    return true;
}

bool test_q3u4_capture_stop_retry_and_cleanup()
{
    {
        Bridge bridge;
        Delay delay;
        Power power;
        Q3U4Frontend frontend(bridge, power, delay);
        FRONTEND_LIFECYCLE_CHECK(frontend.open_terrestrial(2U));
        FRONTEND_LIFECYCLE_CHECK(frontend.tune_terrestrial(527143U));
        FRONTEND_LIFECYCLE_CHECK(frontend.start_terrestrial_capture());
        bridge.fail_at = bridge.requests_seen;
        const auto failed = frontend.stop_terrestrial_capture();
        FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::USB_IO);
        FRONTEND_LIFECYCLE_CHECK(frontend.capture_active());
        const auto after_failed_stop = bridge.requests_seen;
        bridge.fail_at = static_cast<std::size_t>(-1);
        FRONTEND_LIFECYCLE_CHECK(frontend.stop_terrestrial_capture());
        FRONTEND_LIFECYCLE_CHECK(!frontend.capture_active());
        FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == after_failed_stop + 1U);
        FRONTEND_LIFECYCLE_CHECK(frontend.close());
    }

    Bridge bridge;
    Delay delay;
    Power power;
    Q3U4Frontend frontend(bridge, power, delay);
    FRONTEND_LIFECYCLE_CHECK(frontend.open_terrestrial(2U));
    FRONTEND_LIFECYCLE_CHECK(frontend.tune_terrestrial(527143U));
    FRONTEND_LIFECYCLE_CHECK(frontend.start_terrestrial_capture());
    const auto before_close = bridge.requests_seen;
    bridge.fail_at = before_close;
    const auto closed = frontend.close();
    FRONTEND_LIFECYCLE_CHECK(!closed && closed.error() == Error::USB_IO);
    FRONTEND_LIFECYCLE_CHECK(std::string_view(frontend.diagnostic_stage()) ==
                             "cleanup_ts_pin_disable");
    FRONTEND_LIFECYCLE_CHECK(!frontend.capture_active());
    FRONTEND_LIFECYCLE_CHECK(!frontend.open_state());
    FRONTEND_LIFECYCLE_CHECK(power.states.size() == 2U && !power.states.back());
    const auto after_close = bridge.requests_seen;
    FRONTEND_LIFECYCLE_CHECK(frontend.close());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == after_close);
    return true;
}

bool test_q3u4_satellite_open_tune_slot_and_capture()
{
    Bridge bridge;
    Delay delay;
    Power power;
    Q3U4Frontend frontend(bridge, power, delay);
    FRONTEND_LIFECYCLE_CHECK(frontend.open_satellite(0U));
    FRONTEND_LIFECYCLE_CHECK(frontend.open_receiver() == 0);
    const auto init = find_batch_index(bridge, 0x11U,
                                       {{0x15U, 0x00U}, {0x1dU, 0x00U}, {0x04U, 0x02U}});
    const auto pins_off = find_batch_index(bridge, 0x11U,
                                           {{0x1cU, 0x80U}, {0x1fU, 0x22U}});
    const auto wake = find_batch_index(bridge, 0x11U,
                                       {{0x13U, 0x00U}, {0x17U, 0x00U}});
    const auto shared_s0 = find_batch_index(bridge, 0x11U,
                                            {{0x07U, 0x31U}, {0x08U, 0x77U}});
    const auto shared_t0 = find_batch_index(bridge, 0x10U,
                                            {{0x0eU, 0x77U}, {0x0fU, 0x13U}});
    FRONTEND_LIFECYCLE_CHECK(init != static_cast<std::size_t>(-1));
    FRONTEND_LIFECYCLE_CHECK(pins_off != static_cast<std::size_t>(-1));
    FRONTEND_LIFECYCLE_CHECK(wake != static_cast<std::size_t>(-1));
    FRONTEND_LIFECYCLE_CHECK(init < pins_off && pins_off < wake && wake < shared_s0 &&
                             shared_s0 < shared_t0);

    // The TC90522 bridge exposes the RT710 PLL status as a three-byte
    // downstream read; the final raw byte is bit-reversed by Rt710.
    bridge.queue_read({0U, 0U, 0x01U});
    FRONTEND_LIFECYCLE_CHECK(frontend.tune_satellite(1049480U));
    FRONTEND_LIFECYCLE_CHECK(frontend.is_satellite_locked().value());
    bridge.queue_read({0x12U, 0x34U});
    bridge.queue_read({0x12U, 0x34U});
    FRONTEND_LIFECYCLE_CHECK(frontend.select_satellite_slot(3U));
    FRONTEND_LIFECYCLE_CHECK(frontend.selected_tsid() == 0x1234U);
    FRONTEND_LIFECYCLE_CHECK(frontend.start_satellite_capture());
    FRONTEND_LIFECYCLE_CHECK(frontend.capture_active());
    const auto pins_on = find_batch_index(bridge, 0x11U,
                                          {{0x1cU, 0x00U}, {0x1fU, 0x00U}});
    FRONTEND_LIFECYCLE_CHECK(pins_on != static_cast<std::size_t>(-1));
    FRONTEND_LIFECYCLE_CHECK(frontend.close());
    FRONTEND_LIFECYCLE_CHECK(!frontend.capture_active());
    FRONTEND_LIFECYCLE_CHECK(power.states.size() == 2U && !power.states.back());
    const auto cleanup_pins = find_batch_index_after(
        bridge, pins_on, 0x11U, {{0x1cU, 0x80U}, {0x1fU, 0x22U}});
    FRONTEND_LIFECYCLE_CHECK(cleanup_pins != pins_off);
    const auto after_close = bridge.requests_seen;
    FRONTEND_LIFECYCLE_CHECK(frontend.close());
    FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == after_close);

    // TSID zero is a valid direct selection. Capture readiness must use an
    // explicit selection flag rather than treating the value as a sentinel.
    Bridge direct_bridge;
    Bridge unused_bridge;
    CoordinatorBackend direct_power;
    CoordinatorBackend unused_power;
    Q3U4FrontendEnclosure enclosure(direct_bridge, unused_bridge, direct_power,
                                    unused_power, delay);
    FRONTEND_LIFECYCLE_CHECK(enclosure.open_satellite(0U));
    direct_bridge.queue_read({0U, 0U, 0x01U});
    FRONTEND_LIFECYCLE_CHECK(enclosure.tune_satellite(0U, 1049480U));
    FRONTEND_LIFECYCLE_CHECK(
        enclosure.select_satellite_tsid_with_timeout(0U, 0U, 100U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.start_satellite_capture(0U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(0U));
    return true;
}

bool test_q3u4_satellite_failure_stages_and_cleanup()
{
    const auto run_pll = [](bool io_failure) {
        Bridge bridge;
        Delay delay;
        Power power;
        Q3U4Frontend frontend(bridge, power, delay);
        if (!frontend.open_satellite(0U)) return false;
        if (io_failure) {
            bridge.pll_script_enabled = true;
            bridge.pll_script = std::deque<int>(50U, -1);
        }
        const auto result = frontend.tune_satellite(1049480U);
        if (result || result.error() != (io_failure ? Error::USB_IO : Error::TIMEOUT)) return false;
        if (std::string_view(frontend.diagnostic_stage()) != "satellite_pll_poll") return false;
        return static_cast<bool>(frontend.close());
    };
    FRONTEND_LIFECYCLE_CHECK(run_pll(false));
    FRONTEND_LIFECYCLE_CHECK(run_pll(true));

    {
        Bridge bridge;
        Delay delay;
        Power power;
        Q3U4Frontend frontend(bridge, power, delay);
        FRONTEND_LIFECYCLE_CHECK(frontend.open_satellite(0U));
        bridge.queue_read({0U, 0U, 0x01U});
        FRONTEND_LIFECYCLE_CHECK(frontend.tune_satellite(1049480U));
        const auto before = bridge.requests_seen;
        const auto failed = frontend.select_satellite_slot(0U);
        FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::TIMEOUT);
        FRONTEND_LIFECYCLE_CHECK(std::string_view(frontend.diagnostic_stage()) ==
                                 "satellite_slot_tmcc");
        FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen > before);
        FRONTEND_LIFECYCLE_CHECK(frontend.close());
    }

    {
        Bridge bridge;
        Bridge unused_bridge;
        CoordinatorBackend power;
        CoordinatorBackend unused_power;
        Delay delay;
        Q3U4FrontendEnclosure enclosure(bridge, unused_bridge, power,
                                        unused_power, delay);
        FRONTEND_LIFECYCLE_CHECK(enclosure.open_satellite(0U));
        bridge.queue_read({0U, 0U, 0x01U});
        FRONTEND_LIFECYCLE_CHECK(enclosure.tune_satellite(0U, 1049480U));
        for (std::size_t count = 0U; count < 5U; ++count)
            bridge.queue_read({0U, 0U});
        bridge.queue_read({0x12U, 0x34U});
        const auto before_select = delay.delays.size();
        const auto failed = enclosure.select_satellite_slot_with_timeout(0U, 3U, 100U);
        FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::TIMEOUT);
        // Five TMCC waits plus five TSID read-back waits share the 100 ms
        // budget; the verification stage must not receive another 100 ms.
        FRONTEND_LIFECYCLE_CHECK(delay.delays.size() - before_select == 10U);
        FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(0U));
    }

    {
        Bridge bridge;
        Delay delay;
        Power power;
        Q3U4Frontend frontend(bridge, power, delay);
        FRONTEND_LIFECYCLE_CHECK(frontend.open_satellite(0U));
        bridge.queue_read({0U, 0U, 0x01U});
        FRONTEND_LIFECYCLE_CHECK(frontend.tune_satellite(1049480U));
        bridge.queue_read({0x22U, 0x11U});
        const auto failed = frontend.select_satellite_slot(0U);
        FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::TIMEOUT);
        FRONTEND_LIFECYCLE_CHECK(std::string_view(frontend.diagnostic_stage()) ==
                                 "satellite_slot_verify");
        FRONTEND_LIFECYCLE_CHECK(frontend.close());
    }

    {
        Bridge bridge;
        Delay delay;
        Power power;
        Q3U4Frontend frontend(bridge, power, delay);
        FRONTEND_LIFECYCLE_CHECK(frontend.open_satellite(0U));
        bridge.queue_read({0U, 0U, 0x01U});
        FRONTEND_LIFECYCLE_CHECK(frontend.tune_satellite(1049480U));
        bridge.queue_read({0x22U, 0x11U});
        bridge.queue_read({0x22U, 0x11U});
        FRONTEND_LIFECYCLE_CHECK(frontend.select_satellite_slot(0U));
        FRONTEND_LIFECYCLE_CHECK(frontend.start_satellite_capture());
        bridge.fail_at = bridge.requests_seen;
        const auto failed = frontend.close();
        FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::USB_IO);
        FRONTEND_LIFECYCLE_CHECK(std::string_view(frontend.diagnostic_stage()) ==
                                 "cleanup_satellite_ts_pin_disable");
        FRONTEND_LIFECYCLE_CHECK(!frontend.capture_active() && !frontend.open_state());
        const auto after_close = bridge.requests_seen;
        FRONTEND_LIFECYCLE_CHECK(frontend.close());
        FRONTEND_LIFECYCLE_CHECK(bridge.requests_seen == after_close);
    }

    {
        Bridge bridge;
        Delay delay;
        Power power;
        Q3U4Frontend frontend(bridge, power, delay);
        FRONTEND_LIFECYCLE_CHECK(frontend.open_satellite(0U));
        bridge.queue_read({0U, 0U, 0x01U});
        FRONTEND_LIFECYCLE_CHECK(frontend.tune_satellite(1049480U));
        bridge.queue_read({0x33U, 0x44U});
        bridge.queue_read({0x33U, 0x44U});
        FRONTEND_LIFECYCLE_CHECK(frontend.select_satellite_slot(0U));
        bridge.fail_at = bridge.requests_seen;
        const auto failed = frontend.start_satellite_capture();
        FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::USB_IO);
        FRONTEND_LIFECYCLE_CHECK(frontend.capture_active());
        bridge.fail_at = static_cast<std::size_t>(-1);
        FRONTEND_LIFECYCLE_CHECK(frontend.close());
        FRONTEND_LIFECYCLE_CHECK(!frontend.capture_active());
    }
    return true;
}

bool test_q3u4_enclosure_mapping_and_state_contract()
{
    for (std::uint8_t global = 0U; global < 8U; ++global) {
        const auto mapping = Q3U4FrontendEnclosure::map_receiver(global);
        FRONTEND_LIFECYCLE_CHECK(mapping);
        FRONTEND_LIFECYCLE_CHECK(mapping.value().global_receiver == global);
        FRONTEND_LIFECYCLE_CHECK(mapping.value().local_receiver == global % 4U);
        FRONTEND_LIFECYCLE_CHECK(mapping.value().bridge ==
                                 (global < 4U ? Q3U4Bridge::dev1 : Q3U4Bridge::dev2));
        FRONTEND_LIFECYCLE_CHECK(mapping.value().system ==
                                 (global % 4U < 2U ? Tc90522System::isdb_s
                                                   : Tc90522System::isdb_t));
    }
    const auto invalid = Q3U4FrontendEnclosure::map_receiver(8U);
    FRONTEND_LIFECYCLE_CHECK(!invalid && invalid.error() == Error::INVALID_ARGUMENT);

    Bridge dev1_bridge;
    Bridge dev2_bridge;
    CoordinatorBackend dev1_power;
    CoordinatorBackend dev2_power;
    Delay delay;
    Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                     dev2_power, delay);

    // Exercise the production card-power seam without opening a frontend:
    // card-only powers device 1, a receiver couples both bridges, and closing
    // either side never removes the other logical user.
    FRONTEND_LIFECYCLE_CHECK(enclosure.acquire_card());
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power_and_card(
        enclosure, 0U, true, Q3U4PowerState::on, Q3U4PowerState::off));
    FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(2U));
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power_and_card(
        enclosure, 0x04U, true, Q3U4PowerState::on, Q3U4PowerState::on));
    FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(2U));
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power_and_card(
        enclosure, 0U, true, Q3U4PowerState::on, Q3U4PowerState::off));
    FRONTEND_LIFECYCLE_CHECK(enclosure.release_card());
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0U,
                                                   Q3U4PowerState::off,
                                                   Q3U4PowerState::off));

    FRONTEND_LIFECYCLE_CHECK(!enclosure.tune_terrestrial(2U, 527143U));
    FRONTEND_LIFECYCLE_CHECK(!enclosure.open_satellite(2U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(2U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::open);
    const auto duplicate = enclosure.open_terrestrial(2U);
    FRONTEND_LIFECYCLE_CHECK(!duplicate && duplicate.error() == Error::BUSY);
    const auto wrong_system = enclosure.tune_satellite(2U, 1049480U);
    FRONTEND_LIFECYCLE_CHECK(!wrong_system && wrong_system.error() == Error::UNSUPPORTED);
    const auto early_capture = enclosure.start_terrestrial_capture(2U);
    FRONTEND_LIFECYCLE_CHECK(!early_capture && early_capture.error() == Error::NOT_READY);
    const auto early_stop = enclosure.stop_terrestrial_capture(2U);
    FRONTEND_LIFECYCLE_CHECK(!early_stop && early_stop.error() == Error::NOT_READY);
    FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(2U));
    const auto duplicate_close = enclosure.close_receiver(2U);
    FRONTEND_LIFECYCLE_CHECK(!duplicate_close && duplicate_close.error() == Error::INVALID_ARGUMENT);
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0U,
                                                   Q3U4PowerState::off,
                                                   Q3U4PowerState::off));
    return true;
}

bool test_q3u4_same_bank_multi_open_and_reopen()
{
    Bridge dev1_bridge;
    Bridge dev2_bridge;
    CoordinatorBackend dev1_power;
    CoordinatorBackend dev2_power;
    Delay delay;
    Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                     dev2_power, delay);

    FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(2U));
    const auto after_first_open = dev1_bridge.batches.size();
    FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(3U));
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0x0cU,
                                                   Q3U4PowerState::on,
                                                   Q3U4PowerState::on));
    FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::open);
    FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(3U) == Q3U4ReceiverState::open);
    // A second open wakes/configures only local receiver 3; local receiver 2
    // and the bank-wide S0/T0 setup are untouched.
    FRONTEND_LIFECYCLE_CHECK(no_bridge_address_since(dev1_bridge, after_first_open,
                                                     0x10U));

    const auto before_close = dev1_bridge.batches.size();
    FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(3U));
    FRONTEND_LIFECYCLE_CHECK(no_bridge_address_since(dev1_bridge, before_close,
                                                     0x10U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::open);
    FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(3U) == Q3U4ReceiverState::closed);
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0x04U,
                                                   Q3U4PowerState::on,
                                                   Q3U4PowerState::on));
    FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(2U));
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0U,
                                                   Q3U4PowerState::off,
                                                   Q3U4PowerState::off));

    const auto before_reopen = dev1_bridge.batches.size();
    FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(3U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(3U) == Q3U4ReceiverState::open);
    FRONTEND_LIFECYCLE_CHECK(find_batch_index_after(
        dev1_bridge, before_reopen, 0x11U,
        {{0x07U, 0x31U}, {0x08U, 0x77U}}) != static_cast<std::size_t>(-1));
    FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(3U));
    return true;
}

bool test_q3u4_all_close_orders()
{
    std::array<std::uint8_t, 4U> order{{0U, 1U, 2U, 3U}};
    do {
        Bridge dev1_bridge;
        Bridge dev2_bridge;
        CoordinatorBackend dev1_power;
        CoordinatorBackend dev2_power;
        Delay delay;
        Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                         dev2_power, delay);
        FRONTEND_LIFECYCLE_CHECK(enclosure.open_satellite(0U));
        FRONTEND_LIFECYCLE_CHECK(enclosure.open_satellite(1U));
        FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(2U));
        FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(3U));
        FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0x0fU,
                                                       Q3U4PowerState::on,
                                                       Q3U4PowerState::on));
        std::array<bool, 4U> closed{};
        for (const auto global : order) {
            FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(global));
            closed[global] = true;
            for (std::uint8_t survivor = 0U; survivor < 4U; ++survivor) {
                FRONTEND_LIFECYCLE_CHECK(
                    enclosure.receiver_state(survivor) ==
                    (closed[survivor] ? Q3U4ReceiverState::closed
                                      : Q3U4ReceiverState::open));
            }
        }
        FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0U,
                                                       Q3U4PowerState::off,
                                                       Q3U4PowerState::off));
    } while (std::next_permutation(order.begin(), order.end()));
    return true;
}

bool test_q3u4_cross_bank_eight_open_and_close()
{
    Bridge dev1_bridge;
    Bridge dev2_bridge;
    CoordinatorBackend dev1_power;
    CoordinatorBackend dev2_power;
    Delay delay;
    Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                     dev2_power, delay);
    std::array<std::thread, 8U> workers;
    std::array<Error, 8U> errors{};
    for (std::size_t index = 0U; index < workers.size(); ++index) {
        workers[index] = std::thread([&, index]() {
            const auto global = static_cast<std::uint8_t>(index);
            errors[index] = global % 4U < 2U
                ? enclosure.open_satellite(global).error()
                : enclosure.open_terrestrial(global).error();
        });
    }
    for (auto& worker : workers) worker.join();
    for (const auto error : errors) FRONTEND_LIFECYCLE_CHECK(error == Error::OK);
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0xffU,
                                                   Q3U4PowerState::on,
                                                   Q3U4PowerState::on));
    for (std::uint8_t global = 0U; global < 8U; ++global)
        FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(global) == Q3U4ReceiverState::open);

    for (std::uint8_t index = 0U; index < 8U; ++index) {
        const auto global = static_cast<std::uint8_t>((index * 5U) % 8U);
        FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(global));
    }
    FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0U,
                                                   Q3U4PowerState::off,
                                                   Q3U4PowerState::off));
    return true;
}

bool test_q3u4_bank_failure_boundaries()
{
    {
        Bridge dev1_bridge;
        Bridge dev2_bridge;
        CoordinatorBackend dev1_power;
        CoordinatorBackend dev2_power;
        Delay delay;
        Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                         dev2_power, delay);
        dev1_power.outcomes.push_back(Error::TIMEOUT);
        const auto failed = enclosure.open_terrestrial(2U);
        FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::TIMEOUT);
        FRONTEND_LIFECYCLE_CHECK(dev1_bridge.batches.empty() && dev2_bridge.batches.empty());
        FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0U,
                                                       Q3U4PowerState::off,
                                                       Q3U4PowerState::off));
    }

    {
        Bridge dev1_bridge;
        Bridge dev2_bridge;
        CoordinatorBackend dev1_power;
        CoordinatorBackend dev2_power;
        Delay delay;
        Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                         dev2_power, delay);
        dev1_bridge.fail_at = 0U;
        const auto failed = enclosure.open_terrestrial(2U);
        FRONTEND_LIFECYCLE_CHECK(!failed && failed.error() == Error::USB_IO);
        FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::closed);
        FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0U,
                                                       Q3U4PowerState::off,
                                                       Q3U4PowerState::off));
    }

    {
        Bridge dev1_bridge;
        Bridge dev2_bridge;
        CoordinatorBackend dev1_power;
        CoordinatorBackend dev2_power;
        Delay delay;
        Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                         dev2_power, delay);
        FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(2U));
        const auto wake = find_batch_index(
            dev1_bridge, 0x10U,
            {{0xb0U, 0xa0U}, {0xb2U, 0x3dU}, {0xb3U, 0x25U}, {0xb4U, 0x8bU},
             {0xb5U, 0x4bU}, {0xb6U, 0x3fU}, {0xb7U, 0xffU}, {0xb8U, 0xc0U},
             {0x1fU, 0x00U}, {0x75U, 0x00U}});
        FRONTEND_LIFECYCLE_CHECK(wake != static_cast<std::size_t>(-1));
        FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(2U));
        const auto before = dev1_bridge.batches.size();
        dev1_bridge.fail_at = before;
        const auto reopened = enclosure.open_terrestrial(2U);
        FRONTEND_LIFECYCLE_CHECK(!reopened && reopened.error() == Error::USB_IO);
        FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::closed);
        FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(2U).error() ==
                                 Error::INVALID_ARGUMENT);
    }

    {
        Bridge dev1_bridge;
        Bridge dev2_bridge;
        CoordinatorBackend dev1_power;
        CoordinatorBackend dev2_power;
        Delay delay;
        Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                         dev2_power, delay);
        FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(2U));
        FRONTEND_LIFECYCLE_CHECK(enclosure.tune_terrestrial(2U, 527143U));
        dev1_bridge.fail_at = dev1_bridge.batches.size();
        const auto pins = enclosure.start_terrestrial_capture(2U);
        FRONTEND_LIFECYCLE_CHECK(!pins && pins.error() == Error::USB_IO);
        FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::tuned);
        dev1_bridge.fail_at = static_cast<std::size_t>(-1);
        FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(2U));
    }

    {
        Bridge dev1_bridge;
        Bridge dev2_bridge;
        CoordinatorBackend dev1_power;
        CoordinatorBackend dev2_power;
        Delay delay;
        Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                         dev2_power, delay);
        FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(2U));
        FRONTEND_LIFECYCLE_CHECK(enclosure.tune_terrestrial(2U, 527143U));
        FRONTEND_LIFECYCLE_CHECK(enclosure.start_terrestrial_capture(2U));
        dev1_bridge.fail_at = dev1_bridge.batches.size();
        const auto closed = enclosure.close_receiver(2U);
        FRONTEND_LIFECYCLE_CHECK(!closed && closed.error() == Error::USB_IO);
        FRONTEND_LIFECYCLE_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::closed);
        FRONTEND_LIFECYCLE_CHECK(check_enclosure_power(enclosure, 0U,
                                                       Q3U4PowerState::off,
                                                       Q3U4PowerState::off));
    }
    return true;
}

bool test_q3u4_first_capture_purges_psb()
{
    Bridge dev1_bridge;
    Bridge dev2_bridge;
    CoordinatorBackend dev1_power;
    CoordinatorBackend dev2_power;
    Delay delay;
    Purger dev1_purger;
    Purger dev2_purger;
    Q3U4FrontendEnclosure enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                     dev2_power, delay, &dev1_purger,
                                     &dev2_purger);

    FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(2U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.open_terrestrial(3U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.tune_terrestrial(2U, 527143U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.tune_terrestrial(3U, 527143U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.start_terrestrial_capture(2U));
    FRONTEND_LIFECYCLE_CHECK(dev1_purger.calls == 1U && dev2_purger.calls == 0U);
    FRONTEND_LIFECYCLE_CHECK(enclosure.start_terrestrial_capture(3U));
    FRONTEND_LIFECYCLE_CHECK(dev1_purger.calls == 1U);
    FRONTEND_LIFECYCLE_CHECK(enclosure.stop_terrestrial_capture(2U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.stop_terrestrial_capture(3U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.start_terrestrial_capture(2U));
    FRONTEND_LIFECYCLE_CHECK(dev1_purger.calls == 2U);
    FRONTEND_LIFECYCLE_CHECK(enclosure.stop_terrestrial_capture(2U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(2U));
    FRONTEND_LIFECYCLE_CHECK(enclosure.close_receiver(3U));

    Bridge failed_dev1_bridge;
    Bridge failed_dev2_bridge;
    CoordinatorBackend failed_dev1_power;
    CoordinatorBackend failed_dev2_power;
    Purger failed_purger;
    failed_purger.outcomes.push_back(Error::USB_IO);
    Q3U4FrontendEnclosure failed(failed_dev1_bridge, failed_dev2_bridge,
                                 failed_dev1_power, failed_dev2_power, delay,
                                 &failed_purger, nullptr);
    FRONTEND_LIFECYCLE_CHECK(failed.open_terrestrial(2U));
    FRONTEND_LIFECYCLE_CHECK(failed.tune_terrestrial(2U, 527143U));
    const std::size_t before_pin = failed_dev1_bridge.requests_seen;
    const auto start = failed.start_terrestrial_capture(2U);
    FRONTEND_LIFECYCLE_CHECK(!start && start.error() == Error::USB_IO);
    FRONTEND_LIFECYCLE_CHECK(failed_dev1_bridge.requests_seen == before_pin);
    FRONTEND_LIFECYCLE_CHECK(failed.receiver_state(2U) == Q3U4ReceiverState::tuned);
    FRONTEND_LIFECYCLE_CHECK(std::string_view(failed.bank(Q3U4Bridge::dev1)
                                                  .diagnostic_stage()) == "psb_purge");
    FRONTEND_LIFECYCLE_CHECK(failed.close_receiver(2U));
    return true;
}

}  // namespace

bool run_q3u4_frontend_tests()
{
    return test_q3u4_frontend_lifecycle_contract() &&
           test_q3u4_local3_and_preopen_contract() &&
           test_q3u4_frontend_power_and_i2c_failures() &&
           test_q3u4_late_open_rollbacks() &&
           test_q3u4_pll_polling_contract() &&
           test_q3u4_cleanup_power_stage() &&
           test_q3u4_capture_lifecycle() &&
           test_q3u4_capture_stop_retry_and_cleanup() &&
           test_q3u4_satellite_open_tune_slot_and_capture() &&
           test_q3u4_satellite_failure_stages_and_cleanup() &&
           test_q3u4_enclosure_mapping_and_state_contract() &&
           test_q3u4_same_bank_multi_open_and_reopen() &&
           test_q3u4_all_close_orders() &&
           test_q3u4_cross_bank_eight_open_and_close() &&
           test_q3u4_bank_failure_boundaries() &&
           test_q3u4_first_capture_purges_psb();
}

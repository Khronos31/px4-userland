// Modified/ported for px4-userland on 2026-09-02.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin path: driver/r850.c.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only

#include "r850.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

using namespace px4::userland;

#define R850_CHECK(condition)                                                       \
    do {                                                                            \
        if (!(condition)) {                                                         \
            std::fprintf(stderr, "r850 check failed at %s:%d: %s\n",              \
                         __FILE__, __LINE__, #condition);                           \
            return false;                                                           \
        }                                                                           \
    } while (false)

struct Operation final {
    BridgeI2cRequestType type;
    std::uint8_t address;
    std::vector<std::uint8_t> data;
};

class RecordingDelay final : public R850Delay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        delays.push_back(milliseconds);
    }

    std::vector<std::uint32_t> delays;
};

class RecordingBridge final : public BridgeI2cMaster {
public:
    Result<void> request(BridgeI2cRequest* requests,
                         std::size_t count) noexcept override
    {
        if (requests == nullptr || count == 0U) {
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        }
        if (!failures.empty()) {
            const Error error = failures.front();
            failures.pop_front();
            if (error != Error::OK) {
                return Result<void>::failure(error);
            }
        }
        std::vector<Operation> batch;
        batch.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const auto& request = requests[index];
            if (request.type == BridgeI2cRequestType::write) {
                batch.push_back(Operation{request.type, request.address,
                    std::vector<std::uint8_t>(request.write_data.data,
                                               request.write_data.data + request.write_data.size)});
            } else if (request.type == BridgeI2cRequestType::read) {
                if (read_values.empty() || request.read_data.data == nullptr ||
                    request.read_data.size == 0U) {
                    return Result<void>::failure(Error::PROTOCOL_ERROR);
                }
                const auto value = std::move(read_values.front());
                read_values.pop_front();
                if (value.size() != request.read_data.size) {
                    return Result<void>::failure(Error::PROTOCOL_ERROR);
                }
                std::memcpy(request.read_data.data, value.data(), value.size());
                batch.push_back(Operation{request.type, request.address, value});
            } else {
                return Result<void>::failure(Error::INVALID_ARGUMENT);
            }
        }
        batches.push_back(std::move(batch));
        return Result<void>::success();
    }

    void queue_read(std::vector<std::uint8_t> value)
    {
        read_values.push_back(std::move(value));
    }

    void queue_read(std::initializer_list<std::uint8_t> value)
    {
        read_values.emplace_back(value);
    }

    void fail(Error error)
    {
        failures.push_back(error);
    }

    std::vector<std::vector<Operation>> batches;
    std::deque<std::vector<std::uint8_t>> read_values;
    std::deque<Error> failures;
};

std::uint8_t reverse_bits(std::uint8_t value) noexcept
{
    value = static_cast<std::uint8_t>(((value & 0x55U) << 1U) |
                                      ((value & 0xaaU) >> 1U));
    value = static_cast<std::uint8_t>(((value & 0x33U) << 2U) |
                                      ((value & 0xccU) >> 2U));
    return static_cast<std::uint8_t>((value << 4U) | (value >> 4U));
}

std::vector<std::uint8_t> reversed_range(std::size_t length,
                                          std::uint8_t start)
{
    std::vector<std::uint8_t> raw(length + 8U, 0U);
    for (std::size_t index = 8U; index < raw.size(); ++index) {
        raw[index] = reverse_bits(static_cast<std::uint8_t>(start + index - 8U));
    }
    return raw;
}

bool check_tune_write(const RecordingBridge& bridge, std::size_t first_batch,
                      const std::vector<std::uint8_t>& registers,
                      std::uint8_t trigger_value = 0x42U,
                      std::uint8_t receiver_address = 0x10U)
{
    if (first_batch + 2U > bridge.batches.size() ||
        bridge.batches[first_batch].size() != 1U ||
        bridge.batches[first_batch + 1U].size() != 1U) {
        return false;
    }
    const auto& write = bridge.batches[first_batch][0];
    const auto& trigger = bridge.batches[first_batch + 1U][0];
    return write.type == BridgeI2cRequestType::write &&
           write.address == receiver_address &&
           write.data == registers &&
           trigger.type == BridgeI2cRequestType::write &&
           trigger.address == receiver_address &&
           trigger.data == std::vector<std::uint8_t>{0xfeU, 0xf8U, 0x2fU,
                                                     trigger_value};
}

void queue_init_reads(RecordingBridge& bridge, std::uint8_t probe_value,
                      std::uint8_t saved_start, std::uint8_t xtal_sample)
{
    bridge.queue_read({reverse_bits(probe_value)});
    bridge.queue_read(reversed_range(40U, saved_start));
    bridge.queue_read({0U, 0U, reverse_bits(xtal_sample)});
}

bool test_frequency_table_selection()
{
    const struct Case final {
        std::uint32_t if_frequency;
        std::uint32_t rf_frequency;
        std::uint8_t row;
    } cases[] = {
        {4063U, 340000U, 0U}, {4063U, 470000U, 1U},
        {4063U, 487999U, 1U}, {4063U, 680000U, 2U},
        {4063U, 691999U, 2U}, {4063U, 692000U, 3U},
        {4063U, 697999U, 3U}, {4063U, 527143U, 4U},
        {0U, 340000U, 5U}, {0U, 470000U, 6U},
        {0U, 680000U, 7U}, {0U, 692000U, 8U},
        {0U, 1002000U, 9U},
    };
    for (const auto& test : cases) {
        const auto selected = r850_select_isdb_t_frequency(
            test.if_frequency, test.rf_frequency);
        R850_CHECK(selected && selected.value().row_index == test.row);
    }
    R850_CHECK(r850_select_isdb_t_frequency(4063U, 469999U).value().row_index == 4U);
    R850_CHECK(r850_select_isdb_t_frequency(4063U, 1002001U).value().row_index == 4U);
    return true;
}

bool test_initialize_retry_save_restore()
{
    RecordingBridge bridge;
    RecordingDelay delay;
    const auto mapping = q3u4_receiver_mapping(2U);
    R850_CHECK(mapping);
    Tc90522 demod(bridge, mapping.value());
    R850 tuner(demod, delay, true);
    bridge.fail(Error::TIMEOUT);
    queue_init_reads(bridge, 0x98U, 0xa0U, 0x71U);
    R850_CHECK(tuner.initialize());
    R850_CHECK(tuner.initialized());
    R850_CHECK(tuner.chip_variant());
    R850_CHECK(tuner.xtal_power() == 1U);
    R850_CHECK(bridge.batches.size() == 6U);
    R850_CHECK(bridge.batches[1].size() == 3U);
    const auto expected_saved = reversed_range(40U, 0xa0U);
    R850_CHECK(bridge.batches[1][2].type == BridgeI2cRequestType::read &&
               bridge.batches[1][2].address == 0x10U);
    R850_CHECK(bridge.batches[1][2].data == expected_saved);
    R850_CHECK(bridge.batches[2].size() == 1U);
    R850_CHECK(bridge.batches[4].size() == 3U);
    R850_CHECK(bridge.batches[5].size() == 1U);
    R850_CHECK(bridge.batches[5][0].data[0] == 0xfeU);
    R850_CHECK(bridge.batches[5][0].data[1] == 0xf8U);
    R850_CHECK(bridge.batches[5][0].data[2] == 0x08U);
    R850_CHECK(tuner.set_system(R850SystemConfig{
        R850System::isdb_t, R850Bandwidth::mhz_6, 4063U}));
    const std::size_t variant_tune = bridge.batches.size();
    R850_CHECK(tuner.set_frequency(527143U));
    R850_CHECK(check_tune_write(
        bridge, variant_tune,
        std::vector<std::uint8_t>{0xfeU, 0xf8U, 0x08U, 0xcaU, 0xc0U, 0x72U,
                                  0x50U, 0x00U, 0xe4U, 0x00U, 0x30U, 0x97U,
                                  0xbbU, 0xb8U, 0xb7U, 0xd2U, 0x10U, 0xcdU,
                                  0x65U, 0xa7U, 0x42U, 0x81U, 0x92U, 0x16U,
                                  0x2eU, 0x0aU, 0x23U, 0x21U, 0xf1U, 0x0cU,
                                  0x5fU, 0xc4U, 0x20U, 0x9aU, 0x5aU, 0xc1U,
                                  0x99U, 0x6bU, 0x44U, 0x53U, 0x57U, 0x7eU,
                                  0x45U},
        0x47U));
    const std::size_t before_sleep = bridge.batches.size();
    R850_CHECK(tuner.sleep());
    R850_CHECK(tuner.wakeup());
    R850_CHECK(bridge.batches.size() == before_sleep);
    const std::vector<std::uint32_t> expected_variant_delays{10U};
    R850_CHECK(delay.delays == expected_variant_delays);
    return true;
}

bool test_initialize_error_propagation()
{
    {
        RecordingBridge bridge;
        RecordingDelay delay;
        const auto mapping = q3u4_receiver_mapping(2U);
        R850_CHECK(mapping);
        Tc90522 demod(bridge, mapping.value());
        R850 tuner(demod, delay, true);
        for (std::size_t attempt = 0U; attempt < 4U; ++attempt) {
            bridge.fail(Error::TIMEOUT);
        }
        const auto result = tuner.initialize();
        R850_CHECK(!result && result.error() == Error::TIMEOUT);
        R850_CHECK(bridge.batches.empty());
    }
    {
        RecordingBridge bridge;
        RecordingDelay delay;
        const auto mapping = q3u4_receiver_mapping(2U);
        R850_CHECK(mapping);
        Tc90522 demod(bridge, mapping.value());
        R850 tuner(demod, delay, true);
        bridge.queue_read({reverse_bits(0x98U)});
        bridge.fail(Error::OK);
        bridge.fail(Error::USB_IO);
        const auto result = tuner.initialize();
        R850_CHECK(!result && result.error() == Error::USB_IO);
        R850_CHECK(!tuner.initialized());
    }
    {
        RecordingBridge bridge;
        RecordingDelay delay;
        const auto mapping = q3u4_receiver_mapping(2U);
        R850_CHECK(mapping);
        Tc90522 demod(bridge, mapping.value());
        R850 tuner(demod, delay, true);
        queue_init_reads(bridge, 0x98U, 0x30U, 0x71U);
        bridge.fail(Error::OK);
        bridge.fail(Error::OK);
        bridge.fail(Error::OK);
        bridge.fail(Error::OK);
        bridge.fail(Error::USB_IO);
        const auto result = tuner.initialize();
        R850_CHECK(!result && result.error() == Error::USB_IO);
        R850_CHECK(!tuner.initialized());
    }
    return true;
}

bool test_state_frequency_and_lock()
{
    RecordingBridge bridge;
    RecordingDelay delay;
    const auto mapping = q3u4_receiver_mapping(2U);
    R850_CHECK(mapping);
    Tc90522 demod(bridge, mapping.value());
    R850 tuner(demod, delay, true);
    R850_CHECK(tuner.loop_through());
    for (std::size_t attempt = 0U; attempt < 4U; ++attempt) {
        bridge.queue_read({0U});
    }
    bridge.queue_read(reversed_range(40U, 0x10U));
    bridge.queue_read({0U, 0U, reverse_bits(0x71U)});
    R850_CHECK(tuner.initialize());
    R850_CHECK(tuner.set_system(R850SystemConfig{
        R850System::isdb_t, R850Bandwidth::mhz_6, 4063U}));
    const std::size_t before_first_tune = bridge.batches.size();
    R850_CHECK(tuner.set_frequency(527143U));
    const std::size_t after_first_tune = bridge.batches.size();
    R850_CHECK(check_tune_write(
        bridge, before_first_tune,
        std::vector<std::uint8_t>{0xfeU, 0xf8U, 0x08U, 0xcaU, 0xc0U, 0x72U,
                                  0x50U, 0x00U, 0xe4U, 0x00U, 0x30U, 0x97U,
                                  0xbbU, 0xb8U, 0xb7U, 0xd2U, 0x10U, 0xcdU,
                                  0x65U, 0x87U, 0x42U, 0x81U, 0x92U, 0x16U,
                                  0x2eU, 0x0aU, 0x23U, 0x21U, 0xf1U, 0x0cU,
                                  0x5fU, 0xc4U, 0x20U, 0x9aU, 0x5aU, 0xc1U,
                                  0x99U, 0x6bU, 0x44U, 0x53U, 0x57U, 0x6eU,
                                  0x40U}));
    R850_CHECK(after_first_tune == before_first_tune + 2U);
    const std::size_t before_low = bridge.batches.size();
    R850_CHECK(tuner.set_frequency(40000U));
    R850_CHECK(check_tune_write(
        bridge, before_low,
        std::vector<std::uint8_t>{0xfeU, 0xf8U, 0x08U, 0xcaU, 0xc0U, 0x72U,
                                  0x50U, 0x00U, 0xe4U, 0x88U, 0x3aU, 0x9fU,
                                  0x3bU, 0xbaU, 0xb7U, 0xd2U, 0x10U, 0xcdU,
                                  0x65U, 0x84U, 0x40U, 0x81U, 0x88U, 0x7eU,
                                  0xeaU, 0x16U, 0x23U, 0x21U, 0xf1U, 0x2cU,
                                  0x5fU, 0xc4U, 0x20U, 0xaaU, 0x6bU, 0x30U,
                                  0x99U, 0x4aU, 0x44U, 0x53U, 0x5fU, 0x6aU,
                                  0x40U}));
    const std::size_t before_high = bridge.batches.size();
    R850_CHECK(tuner.set_frequency(1002000U));
    R850_CHECK(check_tune_write(
        bridge, before_high,
        std::vector<std::uint8_t>{0xfeU, 0xf8U, 0x08U, 0xcaU, 0xc0U, 0x72U,
                                  0x50U, 0x00U, 0xe4U, 0x00U, 0x30U, 0x80U,
                                  0x3bU, 0xbbU, 0xb7U, 0xd2U, 0x10U, 0xcdU,
                                  0x65U, 0x87U, 0x42U, 0x81U, 0x91U, 0x56U,
                                  0x29U, 0x06U, 0x23U, 0x20U, 0xf1U, 0x0cU,
                                  0x5fU, 0xc4U, 0x20U, 0x9aU, 0x5aU, 0xc1U,
                                  0x99U, 0x6bU, 0x44U, 0x53U, 0x53U, 0x6dU,
                                  0x40U}));
    const std::size_t before_if = bridge.batches.size();
    R850_CHECK(tuner.set_frequency(480000U));
    R850_CHECK(check_tune_write(
        bridge, before_if,
        std::vector<std::uint8_t>{0xfeU, 0xf8U, 0x08U, 0xcaU, 0xc0U, 0x72U,
                                  0x50U, 0x00U, 0xe4U, 0x00U, 0x30U, 0x9fU,
                                  0xbbU, 0xb8U, 0xb7U, 0xd2U, 0x10U, 0xcdU,
                                  0x65U, 0x87U, 0x42U, 0x81U, 0xccU, 0x9eU,
                                  0x4aU, 0x08U, 0x23U, 0x21U, 0xf1U, 0x0fU,
                                  0x5fU, 0xc4U, 0x20U, 0xa9U, 0x8cU, 0xc1U,
                                  0x99U, 0x6bU, 0x44U, 0x53U, 0x57U, 0x6eU,
                                  0x40U}));
    const std::vector<std::uint32_t> expected_delays{10U, 10U, 10U, 40U};
    R850_CHECK(delay.delays == expected_delays);
    R850_CHECK(tuner.set_frequency(650000U));
    R850_CHECK(bridge.batches.size() == before_if + 4U);
    R850_CHECK(delay.delays.back() == 10U);
    bridge.queue_read({0U, 0U, reverse_bits(0x40U)});
    const auto locked = tuner.is_pll_locked();
    R850_CHECK(locked && locked.value());
    bridge.queue_read({0U, 0U, reverse_bits(0x00U)});
    const auto unlocked = tuner.is_pll_locked();
    R850_CHECK(unlocked && !unlocked.value());
    bridge.fail(Error::DISCONNECTED);
    const auto failed = tuner.is_pll_locked();
    R850_CHECK(!failed && failed.error() == Error::DISCONNECTED);
    R850_CHECK(!tuner.set_frequency(39999U));
    R850_CHECK(!tuner.set_frequency(1002001U));
    R850_CHECK(tuner.terminate());
    R850_CHECK(!tuner.initialized());
    R850_CHECK(!tuner.set_frequency(527143U));
    R850_CHECK(!tuner.is_pll_locked());
    return true;
}

bool test_loop_through_and_failures()
{
    RecordingBridge bridge;
    RecordingDelay delay;
    const auto mapping = q3u4_receiver_mapping(3U);
    R850_CHECK(mapping);
    Tc90522 demod(bridge, mapping.value());
    R850 tuner(demod, delay, false);
    for (std::size_t attempt = 0U; attempt < 4U; ++attempt) {
        bridge.queue_read({0U});
    }
    bridge.queue_read(reversed_range(40U, 0x20U));
    for (std::size_t attempt = 0U; attempt < 4U; ++attempt) {
        bridge.queue_read({0U, 0U, reverse_bits(0x00U)});
    }
    R850_CHECK(tuner.initialize());
    R850_CHECK(!tuner.loop_through());
    R850_CHECK(tuner.xtal_power() == 3U);
    R850_CHECK(tuner.set_system(R850SystemConfig{
        R850System::isdb_t, R850Bandwidth::mhz_6, 4063U}));
    const std::size_t false_variant_tune = bridge.batches.size();
    R850_CHECK(tuner.set_frequency(527143U));
    R850_CHECK(check_tune_write(
        bridge, false_variant_tune,
        std::vector<std::uint8_t>{0xfeU, 0xf8U, 0x08U, 0x4aU, 0xc0U, 0x70U,
                                  0x50U, 0x00U, 0xe4U, 0x00U, 0x30U, 0x97U,
                                  0xbbU, 0xb8U, 0xb7U, 0xd2U, 0x10U, 0xcdU,
                                  0x65U, 0x87U, 0x42U, 0x81U, 0x92U, 0x16U,
                                  0x2eU, 0x0aU, 0x23U, 0x21U, 0xf1U, 0x0cU,
                                  0x5fU, 0xc4U, 0x20U, 0x9aU, 0x5aU, 0xc1U,
                                  0x99U, 0x6bU, 0x44U, 0x53U, 0x57U, 0x6eU,
                                  0x40U},
        0x42U, 0x12U));
    bridge.fail(Error::USB_IO);
    R850_CHECK(!tuner.set_frequency(527143U));
    return true;
}

}  // namespace

bool run_r850_tests()
{
    return test_frequency_table_selection() &&
           test_initialize_retry_save_restore() &&
           test_initialize_error_propagation() &&
           test_state_frequency_and_lock() && test_loop_through_and_failures();
}

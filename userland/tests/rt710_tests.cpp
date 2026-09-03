// Modified/ported for px4-userland on 2026-09-03.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin path: driver/rt710.c.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only

#include "rt710.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <limits>
#include <utility>
#include <vector>

namespace {

using namespace px4::userland;

#define RT710_CHECK(condition)                                                       \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::fprintf(stderr, "rt710 check failed at %s:%d: %s\n",              \
                         __FILE__, __LINE__, #condition);                            \
            return false;                                                            \
        }                                                                            \
    } while (false)

struct Op final {
    BridgeI2cRequestType type;
    std::uint8_t address;
    std::vector<std::uint8_t> data;
};

class Delay final : public Rt710Delay {
public:
    void sleep_ms(std::uint32_t value) noexcept override { delays.push_back(value); }
    std::vector<std::uint32_t> delays;
};

class Bridge final : public BridgeI2cMaster {
public:
    Result<void> request(BridgeI2cRequest* requests, std::size_t count) noexcept override
    {
        if (requests == nullptr || count == 0U) return Result<void>::failure(Error::INVALID_ARGUMENT);
        if (!failures.empty()) {
            const Error error = failures.front();
            failures.pop_front();
            if (error != Error::OK) return Result<void>::failure(error);
        }
        std::vector<Op> batch;
        for (std::size_t index = 0U; index < count; ++index) {
            const auto& request = requests[index];
            if (request.type == BridgeI2cRequestType::write) {
                batch.push_back(Op{request.type, request.address,
                                   std::vector<std::uint8_t>(request.write_data.data,
                                                              request.write_data.data + request.write_data.size)});
            } else if (request.type == BridgeI2cRequestType::read) {
                if (reads.empty() || request.read_data.data == nullptr ||
                    reads.front().size() != request.read_data.size) return Result<void>::failure(Error::PROTOCOL_ERROR);
                const auto value = std::move(reads.front());
                reads.pop_front();
                std::memcpy(request.read_data.data, value.data(), value.size());
                batch.push_back(Op{request.type, request.address, value});
            } else return Result<void>::failure(Error::INVALID_ARGUMENT);
        }
        batches.push_back(std::move(batch));
        return Result<void>::success();
    }

    void read(std::initializer_list<std::uint8_t> value) { reads.emplace_back(value); }
    void fail(Error error) { failures.push_back(error); }

    std::vector<std::vector<Op>> batches;
    std::deque<std::vector<std::uint8_t>> reads;
    std::deque<Error> failures;
};

std::uint8_t reverse_bits(std::uint8_t value) noexcept
{
    value = static_cast<std::uint8_t>(((value & 0x55U) << 1U) | ((value & 0xaaU) >> 1U));
    value = static_cast<std::uint8_t>(((value & 0x33U) << 2U) | ((value & 0xccU) >> 2U));
    return static_cast<std::uint8_t>((value << 4U) | (value >> 4U));
}

void probe(Bridge& bridge, std::uint8_t register_value)
{
    bridge.read({0U, 0U, 0U, reverse_bits(register_value)});
}

// The test bridge records translated repeater operations, so test each request
// explicitly rather than comparing a value produced by the implementation.
bool check_downstream_write(const Bridge& bridge, std::size_t index, std::uint8_t reg,
                            const std::vector<std::uint8_t>& values,
                            std::uint8_t demod = 0x10U)
{
    if (index >= bridge.batches.size() || bridge.batches[index].size() != 1U) return false;
    const auto& op = bridge.batches[index][0];
    std::vector<std::uint8_t> expected{0xfeU,
                                       static_cast<std::uint8_t>(0x7aU << 1U), reg};
    expected.insert(expected.end(), values.begin(), values.end());
    if (!(op.type == BridgeI2cRequestType::write && op.address == demod && op.data == expected)) {
        std::fprintf(stderr, "unexpected write %zu:", index);
        for (const auto value : op.data) std::fprintf(stderr, " %02x", value);
        std::fprintf(stderr, "\n");
        return false;
    }
    return true;
}

bool test_init_sleep_and_lock()
{
    Bridge bridge;
    Delay delay;
    probe(bridge, 0x70U);
    const auto mapping = q3u4_receiver_mapping(0U);
    RT710_CHECK(mapping && mapping.value().demod_address == 0x11U);
    Tc90522 demod(bridge, mapping.value());
    Rt710 tuner(demod, delay);
    RT710_CHECK(tuner.initialize() && tuner.chip() == Rt710Chip::rt710);
    RT710_CHECK(bridge.batches.size() == 1U && bridge.batches[0].size() == 3U);
    const std::vector<std::uint8_t> init_pointer{0xfeU, 0xf4U, 0U};
    const std::vector<std::uint8_t> init_read{0xfeU, 0xf5U};
    RT710_CHECK(bridge.batches[0][0].data == init_pointer);
    RT710_CHECK(bridge.batches[0][1].data == init_read);
    bridge.batches.clear();
    RT710_CHECK(tuner.sleep());
    RT710_CHECK(check_downstream_write(bridge, 0U, 0U,
        {0xffU, 0x5cU, 0x88U, 0x30U, 0x41U, 0xc8U, 0xedU, 0x25U,
         0x47U, 0xfcU, 0x48U, 0xa2U, 0x08U, 0x0fU, 0xf3U, 0x59U}, 0x11U));
    bridge.batches.clear();
    bridge.read({0U, 0U, reverse_bits(0x80U)});
    const auto locked = tuner.is_pll_locked();
    RT710_CHECK(locked && locked.value());
    RT710_CHECK(tuner.terminate());
    RT710_CHECK(!tuner.is_pll_locked() && tuner.is_pll_locked().error() == Error::NOT_READY);
    return true;
}

bool test_rt710_set_params_golden_and_boundaries()
{
    Bridge bridge;
    Delay delay;
    probe(bridge, 0x70U);
    const auto mapping = q3u4_receiver_mapping(1U);
    RT710_CHECK(mapping && mapping.value().demod_address == 0x13U);
    Tc90522 demod(bridge, mapping.value());
    Rt710 tuner(demod, delay);
    RT710_CHECK(tuner.initialize());
    bridge.batches.clear();
    RT710_CHECK(tuner.set_params(1049480U, 28860U, 4U));
    RT710_CHECK(delay.delays == std::vector<std::uint32_t>{10U});
    // Independent golden values from driver/rt710.c for BS-1.
    RT710_CHECK(check_downstream_write(bridge, 0U, 0U,
        {0x40U, 0x1dU, 0x20U, 0x10U, 0x41U, 0x50U, 0xedU, 0x25U,
         0x07U, 0x58U, 0x39U, 0x64U, 0x38U, 0xf7U, 0x90U, 0x35U}, 0x13U));
    RT710_CHECK(bridge.batches.size() == 11U);
    RT710_CHECK(check_downstream_write(bridge, 1U, 0x04U, {0x40U}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 2U, 0x05U, {0x92U}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 3U, 0x04U, {0x40U}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 4U, 0x07U, {0x74U}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 5U, 0x06U, {0xecU}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 6U, 0x0aU, {0x39U}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 7U, 0x02U, {0x20U}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 8U, 0x08U, {0x07U}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 9U, 0x0eU, {0x90U}, 0x13U));
    RT710_CHECK(check_downstream_write(bridge, 10U, 0x0fU, {0x45U}, 0x13U));
    return true;
}

bool test_rt720_and_gain_error_paths()
{
    Bridge bridge;
    Delay delay;
    probe(bridge, 0x20U);
    const auto mapping = q3u4_receiver_mapping(0U);
    RT710_CHECK(mapping);
    Tc90522 demod(bridge, mapping.value());
    Rt710 tuner(demod, delay);
    RT710_CHECK(tuner.initialize() && tuner.chip() == Rt710Chip::rt720);
    bridge.batches.clear();
    RT710_CHECK(tuner.set_params(1049480U, 28860U, 4U));
    RT710_CHECK(bridge.batches.size() == 11U);
    RT710_CHECK(check_downstream_write(bridge, 0U, 0U,
        {0x00U, 0x1cU, 0x00U, 0x10U, 0x41U, 0x48U, 0xdaU, 0x4bU,
         0x07U, 0x58U, 0x38U, 0x40U, 0x37U, 0xf7U, 0x4cU, 0x59U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 1U, 0x04U, {0x40U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 2U, 0x08U, {0x07U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 3U, 0x04U, {0x40U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 4U, 0x0cU, {0x37U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 5U, 0x05U, {0x92U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 6U, 0x04U, {0x40U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 7U, 0x07U, {0x74U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 8U, 0x06U, {0xecU}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 9U, 0x0bU, {0x40U}, 0x11U));
    RT710_CHECK(check_downstream_write(bridge, 10U, 0x0fU, {0x45U}, 0x11U));
    bridge.batches.clear();
    RT710_CHECK(tuner.sleep());
    RT710_CHECK(check_downstream_write(bridge, 0U, 0U,
        {0xffU, 0x5eU, 0x88U, 0x30U, 0x41U, 0xc8U, 0xedU, 0x25U,
         0x47U, 0xfcU, 0x48U, 0xa2U, 0x08U, 0x0fU, 0xf3U, 0x59U}, 0x11U));
    bridge.batches.clear();
    bridge.read({0U, reverse_bits(0xa0U)});
    const auto gain = tuner.get_rf_gain();
    RT710_CHECK(gain && gain.value() == 10U);
    bridge.read({0U, reverse_bits(0xa0U)});
    const auto strength = tuner.get_rf_signal_strength();
    RT710_CHECK(strength && strength.value() == -28100);
    bridge.read({0U, reverse_bits(0x00U)});
    const auto gain_zero = tuner.get_rf_gain();
    RT710_CHECK(gain_zero && gain_zero.value() == 0U);
    bridge.read({0U, reverse_bits(0xf1U)});
    const auto gain_max = tuner.get_rf_gain();
    RT710_CHECK(gain_max && gain_max.value() == 31U);
    bridge.read({0U, reverse_bits(0xf1U)});
    const auto strength_max = tuner.get_rf_signal_strength();
    RT710_CHECK(strength_max && strength_max.value() == -57700);
    bridge.fail(Error::DISCONNECTED);
    const auto failed_gain = tuner.get_rf_gain();
    RT710_CHECK(!failed_gain && failed_gain.error() == Error::DISCONNECTED);
    RT710_CHECK(tuner.terminate());
    const auto sleeping = tuner.sleep();
    RT710_CHECK(!sleeping && sleeping.error() == Error::NOT_READY);
    return true;
}

bool test_rt710_gain_golden_values()
{
    const struct GainCase final {
        std::uint8_t raw;
        std::uint8_t gain;
    } cases[] = {{0U, 0U}, {2U, 0U}, {3U, 1U}, {9U, 7U}, {10U, 7U},
                 {12U, 7U}, {13U, 8U}, {22U, 17U}, {23U, 18U}, {31U, 18U}};
    for (const auto& test : cases) {
        Bridge bridge;
        Delay delay;
        probe(bridge, 0x70U);
        const auto mapping = q3u4_receiver_mapping(0U);
        RT710_CHECK(mapping);
        Tc90522 demod(bridge, mapping.value());
        Rt710 tuner(demod, delay);
        RT710_CHECK(tuner.initialize());
        bridge.batches.clear();
        bridge.read({0U, reverse_bits(static_cast<std::uint8_t>(
                                      (test.raw << 4U) | ((test.raw >> 4U) & 1U)))});
        const auto gain = tuner.get_rf_gain();
        RT710_CHECK(gain && gain.value() == test.gain);
        RT710_CHECK(bridge.batches[0U][0].address == 0x11U);
    }

    const struct StrengthCase final {
        std::uint32_t frequency;
        std::int32_t strength;
    } strengths[] = {{1049480U, -21600}, {1613000U, -19600}, {1950000U, -16600}};
    for (const auto& test : strengths) {
        Bridge bridge;
        Delay delay;
        probe(bridge, 0x70U);
        const auto mapping = q3u4_receiver_mapping(1U);
        RT710_CHECK(mapping && mapping.value().demod_address == 0x13U);
        Tc90522 demod(bridge, mapping.value());
        Rt710 tuner(demod, delay);
        RT710_CHECK(tuner.initialize());
        bridge.batches.clear();
        RT710_CHECK(tuner.set_params(test.frequency, 28860U, 4U));
        bridge.batches.clear();
        const std::uint8_t logical_register = static_cast<std::uint8_t>((3U << 4U) | 0U);
        bridge.read({0U, reverse_bits(logical_register)});
        const auto strength = tuner.get_rf_signal_strength();
        RT710_CHECK(strength && strength.value() == test.strength);
        RT710_CHECK(bridge.batches[0U].size() == 3U);
        RT710_CHECK(bridge.batches[0U][0].address == 0x13U);
        const std::vector<std::uint8_t> expected_pointer{0xfeU, 0xf4U, 0x00U};
        const std::vector<std::uint8_t> expected_read{0xfeU, 0xf5U};
        RT710_CHECK(bridge.batches[0U][0].data == expected_pointer);
        RT710_CHECK(bridge.batches[0U][1].data == expected_read);
    }
    return true;
}

bool test_preflight_validation_and_pll_state()
{
    constexpr std::array<std::uint8_t, 2U> probe_values{0x70U, 0x20U};
    for (const std::uint8_t probe_value : probe_values) {
        Bridge bridge;
        Delay delay;
        probe(bridge, probe_value);
        const auto mapping = q3u4_receiver_mapping(0U);
        RT710_CHECK(mapping);
        Tc90522 demod(bridge, mapping.value());
        Rt710 tuner(demod, delay);
        RT710_CHECK(tuner.initialize());
        const std::size_t batches_before = bridge.batches.size();
        const std::size_t delays_before = delay.delays.size();
        const struct InvalidCase final {
            std::uint32_t frequency;
            std::uint32_t symbol_rate;
            std::uint32_t rolloff;
        } invalid[] = {
            {146874U, 28860U, 4U},
            {2350001U, 28860U, 4U},
            {1049480U, 28860U, 6U},
            {1049480U, 0U, 4U},
            {1049480U, std::numeric_limits<std::uint32_t>::max(), 4U},
        };
        for (const auto& test : invalid) {
            const auto result = tuner.set_params(test.frequency, test.symbol_rate, test.rolloff);
            RT710_CHECK(!result && result.error() == Error::INVALID_ARGUMENT);
            RT710_CHECK(bridge.batches.size() == batches_before);
            RT710_CHECK(delay.delays.size() == delays_before);
        }
        if (probe_value == 0x70U) {
            const auto coarse_overflow = tuner.set_params(1049480U, 10000000U, 0U);
            RT710_CHECK(!coarse_overflow && coarse_overflow.error() == Error::INVALID_ARGUMENT);
            RT710_CHECK(bridge.batches.size() == batches_before);
            RT710_CHECK(delay.delays.size() == delays_before);
        }
    }

    const struct FailureCase final {
        std::uint8_t probe_value;
        std::size_t pll_writes;
    } chips[] = {{0x70U, 5U}, {0x20U, 8U}};
    for (const auto& chip : chips) {
        for (std::size_t position = 0U; position < chip.pll_writes; ++position) {
            Bridge bridge;
            Delay delay;
            probe(bridge, chip.probe_value);
            const auto mapping = q3u4_receiver_mapping(0U);
            RT710_CHECK(mapping);
            Tc90522 demod(bridge, mapping.value());
            Rt710 tuner(demod, delay);
            RT710_CHECK(tuner.initialize());
            bridge.batches.clear();
            RT710_CHECK(tuner.set_params(1049480U, 28860U, 4U));
            RT710_CHECK(tuner.frequency_khz() == 1049480U);
            bridge.batches.clear();
            delay.delays.clear();
            // The initial image and each earlier PLL write succeed; the
            // selected PLL write fails.  The legacy driver clears freq at
            // entry to rt710_set_pll, not at entry to set_params.
            for (std::size_t index = 0U; index <= position; ++index) bridge.fail(Error::OK);
            bridge.fail(Error::USB_IO);
            const auto result = tuner.set_params(1049480U, 28860U, 4U);
            RT710_CHECK(!result && result.error() == Error::USB_IO);
            RT710_CHECK(tuner.frequency_khz() == 0U);
            RT710_CHECK(delay.delays.empty());
            RT710_CHECK(bridge.batches.size() == position + 1U);
        }

        Bridge bridge;
        Delay delay;
        probe(bridge, chip.probe_value);
        const auto mapping = q3u4_receiver_mapping(0U);
        RT710_CHECK(mapping);
        Tc90522 demod(bridge, mapping.value());
        Rt710 tuner(demod, delay);
        RT710_CHECK(tuner.initialize());
        bridge.batches.clear();
        RT710_CHECK(tuner.set_params(1049480U, 28860U, 4U));
        const std::uint32_t previous = tuner.frequency_khz();
        bridge.batches.clear();
        delay.delays.clear();
        bridge.fail(Error::USB_IO);
        const auto initial_failure = tuner.set_params(1049480U, 28860U, 4U);
        RT710_CHECK(!initial_failure && initial_failure.error() == Error::USB_IO);
        RT710_CHECK(tuner.frequency_khz() == previous);
        RT710_CHECK(delay.delays.empty());
        RT710_CHECK(bridge.batches.empty());
    }
    return true;
}

bool test_rt720_lock_and_io_errors()
{
    {
        Bridge bridge;
        Delay delay;
        probe(bridge, 0x20U);
        bridge.fail(Error::USB_IO);
        const auto mapping = q3u4_receiver_mapping(0U);
        RT710_CHECK(mapping);
        Tc90522 demod(bridge, mapping.value());
        Rt710 tuner(demod, delay);
        const auto init_error = tuner.initialize();
        RT710_CHECK(!init_error && init_error.error() == Error::USB_IO);
        RT710_CHECK(!tuner.initialized() && bridge.batches.empty());
    }
    Bridge bridge;
    Delay delay;
    probe(bridge, 0x20U);
    const auto mapping = q3u4_receiver_mapping(1U);
    RT710_CHECK(mapping);
    Tc90522 demod(bridge, mapping.value());
    Rt710 tuner(demod, delay);
    RT710_CHECK(tuner.initialize());
    bridge.batches.clear();
    bridge.read({0U, 0U, reverse_bits(0x80U)});
    const auto locked = tuner.is_pll_locked();
    RT710_CHECK(locked && locked.value());
    bridge.read({0U, 0U, reverse_bits(0x00U)});
    const auto unlocked = tuner.is_pll_locked();
    RT710_CHECK(unlocked && !unlocked.value());
    bridge.fail(Error::DISCONNECTED);
    const auto lock_error = tuner.is_pll_locked();
    RT710_CHECK(!lock_error && lock_error.error() == Error::DISCONNECTED);

    bridge.fail(Error::USB_IO);
    const auto sleep_error = tuner.sleep();
    RT710_CHECK(!sleep_error && sleep_error.error() == Error::USB_IO);
    const std::size_t batches_before = bridge.batches.size();
    const std::size_t delays_before = delay.delays.size();
    bridge.fail(Error::USB_IO);
    const auto tune_error = tuner.set_params(1049480U, 28860U, 4U);
    RT710_CHECK(!tune_error && tune_error.error() == Error::USB_IO);
    RT710_CHECK(bridge.batches.size() == batches_before);
    RT710_CHECK(delay.delays.size() == delays_before);
    return true;
}

Result<std::uint8_t> tune_final_bandwidth(std::uint32_t symbol_rate)
{
    Bridge bridge;
    Delay delay;
    probe(bridge, 0x70U);
    const auto mapping = q3u4_receiver_mapping(0U);
    if (!mapping) return Result<std::uint8_t>::failure(mapping.error());
    Tc90522 demod(bridge, mapping.value());
    Rt710 tuner(demod, delay);
    const auto initialized = tuner.initialize();
    const auto tuned = initialized ? tuner.set_params(1049480U, symbol_rate, 0U)
                                   : Result<void>::failure(initialized.error());
    if (!initialized || !tuned || bridge.batches.size() != 12U || bridge.batches.back().empty()) {
        return Result<std::uint8_t>::failure(Error::INTERNAL);
    }
    return Result<std::uint8_t>::success(bridge.batches.back()[0].data.back());
}

std::uint8_t independent_rt720_reg0f(std::uint32_t symbol_rate,
                                      std::uint32_t rolloff)
{
    // Independently transcribed from driver/rt710.c.  RT720's final register
    // is derived from s; the later symbol-rate compensation is legacy-dead
    // for this result and is intentionally not used here.
    std::uint32_t legacy_symbol_rate = symbol_rate;
    if (legacy_symbol_rate >= 15000U) legacy_symbol_rate += 6000U;
    const std::uint32_t s = legacy_symbol_rate * 12U;

    const std::uint32_t range = rolloff > 1U ? 20000U : 0U;
    std::uint32_t coarse = 0U;
    if (s <= 88000U + range) {
        coarse = 0U;
    } else if (s <= 368000U + range) {
        const std::uint32_t delta = s - 88000U - range;
        coarse = delta / 20000U + (delta % 20000U != 0U);
        if (coarse > 6U) ++coarse;
    } else if (s <= 764000U + range) {
        const std::uint32_t delta = s - 368000U - range;
        coarse = delta / 20000U + 15U;
        if ((s + 25216U - range) % 20000U != 0U) ++coarse;
        if (coarse >= 33U) coarse += 3U;
        else if (coarse >= 29U) coarse += 2U;
        else if (coarse >= 27U) coarse += 3U;
        else if (coarse >= 24U) coarse += 2U;
        else if (coarse >= 19U) ++coarse;
    } else {
        coarse = 42U;
    }
    return static_cast<std::uint8_t>((coarse << 2U) | (rolloff > 1U ? 1U : 0U));
}

Result<std::uint8_t> tune_rt720_final(std::uint32_t symbol_rate,
                                      std::uint32_t rolloff)
{
    Bridge bridge;
    Delay delay;
    probe(bridge, 0x20U);
    const auto mapping = q3u4_receiver_mapping(0U);
    if (!mapping) return Result<std::uint8_t>::failure(mapping.error());
    Tc90522 demod(bridge, mapping.value());
    Rt710 tuner(demod, delay);
    if (!tuner.initialize()) return Result<std::uint8_t>::failure(Error::INTERNAL);
    bridge.batches.clear();
    const auto tuned = tuner.set_params(1049480U, symbol_rate, rolloff);
    if (!tuned || bridge.batches.empty() || bridge.batches.back().size() != 1U ||
        bridge.batches.back()[0].address != mapping.value().demod_address ||
        bridge.batches.back()[0].data.size() != 4U ||
        bridge.batches.back()[0].data[2U] != 0x0fU) {
        return Result<std::uint8_t>::failure(tuned ? Error::INTERNAL : tuned.error());
    }
    return Result<std::uint8_t>::success(bridge.batches.back()[0].data.back());
}

bool test_rt720_low_symbol_rate_legacy_values()
{
    // 14,000 follows the <=15,000 legacy compensation path.  15,000 receives
    // the initial +6,000, then reaches the <=30,000 later-compensation path;
    // that later adjustment is not consumed by RT720's final register math.
    for (const std::uint32_t symbol_rate : {14000U, 15000U}) {
        const auto actual = tune_rt720_final(symbol_rate, 4U);
        RT710_CHECK(actual &&
                    actual.value() == independent_rt720_reg0f(symbol_rate, 4U));
    }
    return true;
}

bool test_rt710_bandwidth_and_frequency_boundaries()
{
    // Independent copy of the 26 legacy table outputs (coarse << 2 | fine).
    constexpr std::array<std::uint32_t, 26U> limits{
        50000U, 73000U, 96000U, 104000U, 116000U, 126000U, 134000U,
        146000U, 158000U, 170000U, 178000U, 190000U, 202000U, 212000U,
        218000U, 234000U, 244000U, 246000U, 262000U, 266000U, 282000U,
        298000U, 318000U, 340000U, 358000U, 379999U};
    constexpr std::array<std::uint8_t, 26U> outputs{
        0x00U, 0x01U, 0x04U, 0x05U, 0x08U, 0x09U, 0x0cU, 0x0dU,
        0x10U, 0x11U, 0x14U, 0x15U, 0x18U, 0x19U, 0x1cU, 0x1dU,
        0x25U, 0x28U, 0x29U, 0x2cU, 0x2dU, 0x31U, 0x35U, 0x39U,
        0x3dU, 0x41U};
    for (std::size_t index = 0U; index < limits.size(); ++index) {
        // With rolloff 0, bandwidth=floor(symbol_rate*115/10).  The
        // predecessor of each boundary is still above the preceding limit;
        // +1 crosses the current boundary.  Exact integer preimages do not
        // exist for every decimal table limit, so these are the meaningful
        // below/above tests for the integer API.
        const std::uint32_t symbol = limits[index] * 10U / 115U;
        const auto below = tune_final_bandwidth(symbol);
        RT710_CHECK(below && below.value() == outputs[index]);
        std::uint32_t above_symbol = symbol + 1U;
        while ((above_symbol * 115U) / 10U <= limits[index]) ++above_symbol;
        const auto above = tune_final_bandwidth(above_symbol);
        const std::uint8_t expected = index + 1U < outputs.size() ? outputs[index + 1U] : 0x45U;
        RT710_CHECK(above && above.value() == expected);
    }

    const struct Branch final {
        std::uint32_t frequency;
        std::uint8_t mixer;
        std::uint8_t reg2;
        std::uint8_t reg8;
        std::uint8_t reg_e;
    } branches[] = {
        {1599999U, 0x39U, 0x20U, 0x07U, 0x90U},
        {1600000U, 0x39U, 0x60U, 0x87U, 0x90U},
        {1949999U, 0x39U, 0x60U, 0x87U, 0x90U},
        {1950000U, 0x38U, 0x20U, 0x07U, 0x90U},
        {1999999U, 0x38U, 0x20U, 0x07U, 0x90U},
        {2000000U, 0x38U, 0x20U, 0x07U, 0x98U},
    };
    for (const auto& branch : branches) {
        Bridge bridge;
        Delay delay;
        probe(bridge, 0x70U);
        const auto mapping = q3u4_receiver_mapping(0U);
        RT710_CHECK(mapping);
        Tc90522 demod(bridge, mapping.value());
        Rt710 tuner(demod, delay);
        RT710_CHECK(tuner.initialize());
        bridge.batches.clear();
        RT710_CHECK(tuner.set_params(branch.frequency, 28860U, 4U));
        RT710_CHECK(bridge.batches[6U][0].data.back() == branch.mixer);
        RT710_CHECK(bridge.batches[7U][0].data.back() == branch.reg2);
        RT710_CHECK(bridge.batches[8U][0].data.back() == branch.reg8);
        RT710_CHECK(bridge.batches[9U][0].data.back() == branch.reg_e);
    }
    Bridge bridge;
    Delay delay;
    probe(bridge, 0x70U);
    const auto mapping = q3u4_receiver_mapping(0U);
    RT710_CHECK(mapping);
    Tc90522 demod(bridge, mapping.value());
    Rt710 tuner(demod, delay);
    RT710_CHECK(tuner.initialize());
    const auto too_low = tuner.set_params(146874U, 28860U, 4U);
    RT710_CHECK(!too_low && too_low.error() == Error::INVALID_ARGUMENT);
    RT710_CHECK(tuner.set_params(146875U, 28860U, 4U));
    const auto too_high = tuner.set_params(2350001U, 28860U, 4U);
    RT710_CHECK(!too_high && too_high.error() == Error::INVALID_ARGUMENT);
    const auto invalid_rolloff = tuner.set_params(1049480U, 28860U, 6U);
    RT710_CHECK(!invalid_rolloff && invalid_rolloff.error() == Error::INVALID_ARGUMENT);
    return true;
}

}  // namespace

bool run_rt710_tests()
{
    return test_init_sleep_and_lock() &&
           test_rt710_set_params_golden_and_boundaries() &&
           test_rt720_and_gain_error_paths() &&
           test_rt710_gain_golden_values() &&
           test_preflight_validation_and_pll_state() &&
           test_rt720_lock_and_io_errors() &&
           test_rt720_low_symbol_rate_legacy_values() &&
           test_rt710_bandwidth_and_frequency_boundaries();
}

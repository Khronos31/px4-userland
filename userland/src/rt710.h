// Modified/ported for px4-userland on 2026-09-02.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: driver/rt710.c, driver/rt710.h.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_RT710_H
#define PX4_USERLAND_RT710_H

#include "tc90522.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace px4::userland {

class Rt710Delay {
public:
    virtual ~Rt710Delay() noexcept = default;
    virtual void sleep_ms(std::uint32_t milliseconds) noexcept = 0;
};

enum class Rt710Chip : std::uint8_t { unknown = 0, rt710, rt720 };

class Rt710 final {
public:
    Rt710(Tc90522& demod, Rt710Delay& delay) noexcept
        : demod_(demod), delay_(delay)
    {
    }

    Result<void> initialize() noexcept;
    Result<void> terminate() noexcept;
    Result<void> sleep() noexcept;
    // The original kHz entrance is a wrapper over set_params_hz() at
    // frequency_khz * 1000.  Symbol rate and rolloff keep their own units.
    Result<void> set_params(std::uint32_t frequency_khz,
                            std::uint32_t symbol_rate,
                            std::uint32_t rolloff) noexcept;
    // R3 native-Hz entrance; the full requested Hz reaches the PLL planner.
    Result<void> set_params_hz(std::uint64_t frequency_hz,
                               std::uint32_t symbol_rate,
                               std::uint32_t rolloff) noexcept;
    Result<bool> is_pll_locked() noexcept;
    Result<std::uint8_t> get_rf_gain() noexcept;
    Result<std::int32_t> get_rf_signal_strength() noexcept;

    bool initialized() const noexcept { return initialized_; }
    Rt710Chip chip() const noexcept { return chip_; }
    // Compatibility projection of the last programmed request (floor kHz).
    std::uint32_t frequency_khz() const noexcept
    {
        return static_cast<std::uint32_t>(frequency_hz_ / 1000U);
    }
    std::uint64_t frequency_hz() const noexcept { return frequency_hz_; }

private:
    Result<std::vector<std::uint8_t>> read_regs(std::uint8_t reg,
                                                  std::size_t length) noexcept;
    Result<void> write_regs(std::uint8_t reg, ByteView values) noexcept;
    Result<void> set_pll(std::array<std::uint8_t, 0x10U>& regs,
                         std::uint64_t frequency_hz) noexcept;
    Result<void> validate_params(std::uint64_t frequency_hz,
                                 std::uint32_t symbol_rate,
                                 std::uint32_t rolloff) const noexcept;

    Tc90522& demod_;
    Rt710Delay& delay_;
    Rt710Chip chip_ = Rt710Chip::unknown;
    bool initialized_ = false;
    std::uint64_t frequency_hz_ = 0U;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_RT710_H

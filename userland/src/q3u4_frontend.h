// Modified/ported for px4-userland on 2026-09-03.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: driver/px4_device.c, driver/px4_device.h,
// winusb/src/DriverHost_PX4/px4_device.cpp,
// winusb/src/DriverHost_PX4/px4_device.hpp.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_Q3U4_FRONTEND_H
#define PX4_USERLAND_Q3U4_FRONTEND_H

#include "px4/error.h"
#include "px4/it930x.h"
#include "q3u4_power.h"
#include "r850.h"
#include "rt710.h"
#include "tc90522.h"

#include <array>
#include <cstdint>

namespace px4::userland {

// Deliberately narrower than the controller API: a frontend may request only
// the shared backend state, never an arbitrary GPIO.
class Q3U4BackendPower {
public:
    virtual ~Q3U4BackendPower() noexcept = default;
    virtual Result<void> set_backend_power(bool on, Q3U4Delay& delay) noexcept = 0;
};

// The frontend injects one timing object into all three users.  It is kept
// private to this header and does not alter the individual accepted drivers.
class Q3U4FrontendDelay : public Q3U4Delay, public R850Delay, public Rt710Delay {
public:
    ~Q3U4FrontendDelay() noexcept override = default;
    void sleep_ms(std::uint32_t milliseconds) noexcept override = 0;
};

class It930xBackendPower final : public Q3U4BackendPower {
public:
    explicit It930xBackendPower(It930xController& controller) noexcept
        : controller_(controller)
    {
    }

    Result<void> set_backend_power(bool on, Q3U4Delay& delay) noexcept override;

private:
    It930xController& controller_;
};

class Q3U4Frontend final {
public:
    enum class DiagnosticStage : std::uint8_t {
        none,
        backend_power_on,
        tuner_initialize_rt710_0,
        tuner_initialize_rt710_1,
        tuner_initialize_r850_0,
        tuner_initialize_r850_1,
        frontend_sleep_rt710_0,
        frontend_sleep_rt710_1,
        frontend_sleep_r850_0,
        frontend_sleep_r850_1,
        selected_tc_init_writes,
        satellite_selected_tc_init_writes,
        ts_pin_disable,
        satellite_ts_pin_disable,
        ts_pin_enable,
        satellite_ts_pin_enable,
        cleanup_ts_pin_disable,
        cleanup_satellite_ts_pin_disable,
        demod_wake,
        satellite_demod_wake,
        selected_r850_wake,
        r850_system_set,
        shared_s0,
        shared_t0,
        tune_tc47,
        agc_off,
        tc76,
        r850_set_frequency,
        pll_poll,
        satellite_agc_off,
        satellite_mode_registers,
        satellite_set_params,
        satellite_pll_poll,
        satellite_agc_on,
        satellite_demod_lock,
        satellite_slot_tmcc,
        satellite_slot_set,
        satellite_slot_verify,
        agc_on,
        tc71_72_75,
        cleanup_tuner_terminate,
        cleanup_power_off,
        open,
        tuned,
        closed,
    };

    Q3U4Frontend(BridgeI2cMaster& bridge, Q3U4BackendPower& power,
                 Q3U4FrontendDelay& delay) noexcept;
    ~Q3U4Frontend() noexcept;

    Q3U4Frontend(const Q3U4Frontend&) = delete;
    Q3U4Frontend& operator=(const Q3U4Frontend&) = delete;

    Result<void> open_terrestrial(std::uint8_t local_receiver) noexcept;
    Result<void> open_satellite(std::uint8_t local_receiver) noexcept;
    Result<void> tune_terrestrial(std::uint32_t frequency_khz) noexcept;
    Result<void> tune_satellite(std::uint32_t frequency_khz) noexcept;
    Result<void> select_satellite_slot(std::uint8_t slot) noexcept;
    Result<void> start_terrestrial_capture() noexcept;
    Result<void> start_satellite_capture() noexcept;
    Result<void> stop_terrestrial_capture() noexcept;
    Result<void> stop_satellite_capture() noexcept;
    bool capture_active() const noexcept { return capture_active_; }
    Result<bool> is_terrestrial_locked() noexcept;
    Result<bool> is_satellite_locked() noexcept;
    std::uint16_t selected_tsid() const noexcept { return selected_tsid_; }
    Result<void> close() noexcept;
    Result<void> terminate() noexcept;

    bool open_state() const noexcept { return open_receiver_ >= 0; }
    std::int8_t open_receiver() const noexcept { return open_receiver_; }
    const char* diagnostic_stage() const noexcept;
    // Used by the probe's final external power-off guard.  It does not alter
    // the frontend transaction; it only records a failure stage.
    void record_cleanup_power_off_failure() noexcept;
    Result<bool> terrestrial_loop_through(std::uint8_t local_receiver) const noexcept;

private:
    Result<void> cleanup() noexcept;
    Result<void> sleep_frontend(std::uint8_t index) noexcept;
    Result<void> terminate_initialized() noexcept;
    void remember_first(Error& first, const Result<void>& result) noexcept;
    void begin_operation() noexcept;
    void attempt(DiagnosticStage stage) noexcept { current_stage_ = stage; }
    void remember_failure(const Result<void>& result) noexcept;

    Q3U4BackendPower& power_;
    Q3U4FrontendDelay& delay_;
    std::array<Tc90522, 4U> tc_;
    std::array<Rt710, 2U> rt710_;
    std::array<R850, 2U> r850_;
    std::array<bool, 4U> tuner_initialized_{};
    bool backend_on_ = false;
    std::int8_t open_receiver_ = -1;
    bool tuned_ = false;
    bool capture_active_ = false;
    Tc90522System open_system_ = Tc90522System::isdb_t;
    std::uint16_t selected_tsid_ = 0U;
    DiagnosticStage current_stage_ = DiagnosticStage::none;
    DiagnosticStage failure_stage_ = DiagnosticStage::none;
    bool operation_failed_ = false;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_Q3U4_FRONTEND_H

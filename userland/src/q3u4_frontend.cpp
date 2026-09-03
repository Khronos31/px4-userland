// Modified/ported for px4-userland on 2026-09-03.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: driver/px4_device.c, driver/px4_device.h,
// winusb/src/DriverHost_PX4/px4_device.cpp,
// winusb/src/DriverHost_PX4/px4_device.hpp.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only

#include "q3u4_frontend.h"

#include <array>
#include <vector>

namespace px4::userland {
namespace {

constexpr std::array<std::array<std::uint8_t, 2U>, 10U> kTcInitTValues{{
    {{0xb0U, 0xa0U}}, {{0xb2U, 0x3dU}}, {{0xb3U, 0x25U}}, {{0xb4U, 0x8bU}},
    {{0xb5U, 0x4bU}}, {{0xb6U, 0x3fU}}, {{0xb7U, 0xffU}}, {{0xb8U, 0xc0U}},
    {{0x1fU, 0x00U}}, {{0x75U, 0x00U}},
}};
constexpr std::array<std::array<std::uint8_t, 2U>, 3U> kTcInitSValues{{
    {{0x15U, 0x00U}}, {{0x1dU, 0x00U}}, {{0x04U, 0x02U}},
}};

constexpr std::array<std::array<std::uint8_t, 2U>, 2U> kSharedS0{{
    {{0x07U, 0x31U}}, {{0x08U, 0x77U}},
}};
constexpr std::array<std::array<std::uint8_t, 2U>, 2U> kSharedT0{{
    {{0x0eU, 0x77U}}, {{0x0fU, 0x13U}},
}};

template <std::size_t N>
Result<void> write_fixed(Tc90522& demod,
                         const std::array<std::array<std::uint8_t, 2U>, N>& values) noexcept
{
    std::vector<Tc90522RegisterWrite> writes;
    writes.reserve(N);
    for (std::size_t i = 0U; i < N; ++i) {
        writes.push_back(Tc90522RegisterWrite{values[i][0], ByteView{&values[i][1], 1U}});
    }
    return demod.write_multiple_regs(writes);
}

}  // namespace

Result<void> It930xBackendPower::set_backend_power(bool on, Q3U4Delay& delay) noexcept
{
    return controller_.set_q3u4_backend_power(on, delay);
}

Q3U4Frontend::Q3U4Frontend(BridgeI2cMaster& bridge, Q3U4BackendPower& power,
                           Q3U4FrontendDelay& delay) noexcept
    : power_(power), delay_(delay),
      tc_{Tc90522(bridge, Q3U4ReceiverMapping{0U, 0x11U, false, Tc90522System::isdb_s}),
          Tc90522(bridge, Q3U4ReceiverMapping{1U, 0x13U, true, Tc90522System::isdb_s}),
          Tc90522(bridge, Q3U4ReceiverMapping{2U, 0x10U, false, Tc90522System::isdb_t}),
          Tc90522(bridge, Q3U4ReceiverMapping{3U, 0x12U, true, Tc90522System::isdb_t})},
      rt710_{Rt710(tc_[0], delay), Rt710(tc_[1], delay)},
      r850_{R850(tc_[2], delay, true), R850(tc_[3], delay, false)}
{
}

Q3U4Frontend::~Q3U4Frontend() noexcept
{
    (void)cleanup();
}

const char* Q3U4Frontend::diagnostic_stage() const noexcept
{
    const auto stage = operation_failed_ ? failure_stage_ : current_stage_;
    switch (stage) {
    case DiagnosticStage::backend_power_on: return "backend_power_on";
    case DiagnosticStage::tuner_initialize_rt710_0: return "tuner_initialize_rt710_0";
    case DiagnosticStage::tuner_initialize_rt710_1: return "tuner_initialize_rt710_1";
    case DiagnosticStage::tuner_initialize_r850_0: return "tuner_initialize_r850_0";
    case DiagnosticStage::tuner_initialize_r850_1: return "tuner_initialize_r850_1";
    case DiagnosticStage::frontend_sleep_rt710_0: return "frontend_sleep_rt710_0";
    case DiagnosticStage::frontend_sleep_rt710_1: return "frontend_sleep_rt710_1";
    case DiagnosticStage::frontend_sleep_r850_0: return "frontend_sleep_r850_0";
    case DiagnosticStage::frontend_sleep_r850_1: return "frontend_sleep_r850_1";
    case DiagnosticStage::selected_tc_init_writes: return "selected_tc_init_writes";
    case DiagnosticStage::satellite_selected_tc_init_writes: return "satellite_selected_tc_init_writes";
    case DiagnosticStage::ts_pin_disable: return "ts_pin_disable";
    case DiagnosticStage::satellite_ts_pin_disable: return "satellite_ts_pin_disable";
    case DiagnosticStage::ts_pin_enable: return "ts_pin_enable";
    case DiagnosticStage::satellite_ts_pin_enable: return "satellite_ts_pin_enable";
    case DiagnosticStage::cleanup_ts_pin_disable: return "cleanup_ts_pin_disable";
    case DiagnosticStage::cleanup_satellite_ts_pin_disable: return "cleanup_satellite_ts_pin_disable";
    case DiagnosticStage::demod_wake: return "demod_wake";
    case DiagnosticStage::satellite_demod_wake: return "satellite_demod_wake";
    case DiagnosticStage::selected_r850_wake: return "selected_r850_wake";
    case DiagnosticStage::r850_system_set: return "r850_system_set";
    case DiagnosticStage::shared_s0: return "shared_s0";
    case DiagnosticStage::shared_t0: return "shared_t0";
    case DiagnosticStage::tune_tc47: return "tune_tc47";
    case DiagnosticStage::agc_off: return "agc_off";
    case DiagnosticStage::tc76: return "tc76";
    case DiagnosticStage::r850_set_frequency: return "r850_set_frequency";
    case DiagnosticStage::pll_poll: return "pll_poll";
    case DiagnosticStage::satellite_agc_off: return "satellite_agc_off";
    case DiagnosticStage::satellite_mode_registers: return "satellite_mode_registers";
    case DiagnosticStage::satellite_set_params: return "satellite_set_params";
    case DiagnosticStage::satellite_pll_poll: return "satellite_pll_poll";
    case DiagnosticStage::satellite_agc_on: return "satellite_agc_on";
    case DiagnosticStage::satellite_demod_lock: return "satellite_demod_lock";
    case DiagnosticStage::satellite_slot_tmcc: return "satellite_slot_tmcc";
    case DiagnosticStage::satellite_slot_set: return "satellite_slot_set";
    case DiagnosticStage::satellite_slot_verify: return "satellite_slot_verify";
    case DiagnosticStage::agc_on: return "agc_on";
    case DiagnosticStage::tc71_72_75: return "tc71_72_75";
    case DiagnosticStage::cleanup_tuner_terminate: return "cleanup_tuner_terminate";
    case DiagnosticStage::cleanup_power_off: return "cleanup_power_off";
    case DiagnosticStage::open: return "open";
    case DiagnosticStage::tuned: return "tuned";
    case DiagnosticStage::closed: return "closed";
    case DiagnosticStage::none: return "none";
    }
    return "none";
}

void Q3U4Frontend::begin_operation() noexcept
{
    operation_failed_ = false;
    failure_stage_ = DiagnosticStage::none;
}

void Q3U4Frontend::remember_failure(const Result<void>& result) noexcept
{
    if (!result && !operation_failed_) {
        failure_stage_ = current_stage_;
        operation_failed_ = true;
    }
}

void Q3U4Frontend::record_cleanup_power_off_failure() noexcept
{
    attempt(DiagnosticStage::cleanup_power_off);
    if (!operation_failed_) {
        failure_stage_ = current_stage_;
        operation_failed_ = true;
    }
}

void Q3U4Frontend::remember_first(Error& first, const Result<void>& result) noexcept
{
    if (first == Error::OK && !result) first = result.error();
}

Result<void> Q3U4Frontend::sleep_frontend(std::uint8_t index) noexcept
{
    attempt(static_cast<DiagnosticStage>(static_cast<std::uint8_t>(
        DiagnosticStage::frontend_sleep_rt710_0) + index));
    if (index < 2U) {
        const auto tuner = rt710_[index].sleep();
        if (!tuner) { remember_failure(tuner); return tuner; }
        const auto result = tc_[index].sleep_s(true);
        remember_failure(result);
        return result;
    }
    const auto tuner = r850_[index - 2U].sleep();
    if (!tuner) { remember_failure(tuner); return tuner; }
    const auto result = tc_[index].sleep_t(true);
    remember_failure(result);
    return result;
}

Result<void> Q3U4Frontend::terminate_initialized() noexcept
{
    Error first = Error::OK;
    for (int i = 3; i >= 0; --i) {
        if (!tuner_initialized_[static_cast<std::size_t>(i)]) continue;
        attempt(DiagnosticStage::cleanup_tuner_terminate);
        const auto result = i < 2 ? rt710_[static_cast<std::size_t>(i)].terminate()
                                  : r850_[static_cast<std::size_t>(i - 2)].terminate();
        remember_first(first, result);
        remember_failure(result);
        tuner_initialized_[static_cast<std::size_t>(i)] = false;
    }
    return first == Error::OK ? Result<void>::success() : Result<void>::failure(first);
}

Result<void> Q3U4Frontend::cleanup() noexcept
{
    Error first = Error::OK;
    // With one permitted open receiver this is always the legacy last-close
    // path: backend termination does not sleep the selected receiver first.
    bool pins_disabled = !capture_active_;
    if (capture_active_ && backend_on_ && open_receiver_ >= 0) {
        const bool satellite = open_system_ == Tc90522System::isdb_s;
        attempt(satellite ? DiagnosticStage::cleanup_satellite_ts_pin_disable
                          : DiagnosticStage::cleanup_ts_pin_disable);
        const auto result = satellite
            ? tc_[static_cast<std::size_t>(open_receiver_)].enable_ts_pins_s(false)
            : tc_[static_cast<std::size_t>(open_receiver_)].enable_ts_pins_t(false);
        remember_first(first, result);
        remember_failure(result);
        pins_disabled = static_cast<bool>(result);
        if (pins_disabled) capture_active_ = false;
    } else if (capture_active_) {
        // A successful power-off makes the demod inaccessible.  Do not issue
        // a retrying I2C disable against that powered-off device.
        capture_active_ = false;
        pins_disabled = true;
    }
    remember_first(first, terminate_initialized());
    if (backend_on_) {
        attempt(DiagnosticStage::cleanup_power_off);
        const auto off = power_.set_backend_power(false, delay_);
        remember_first(first, off);
        remember_failure(off);
        if (off) {
            backend_on_ = false;
            // If pin cleanup failed, the successful backend power-off is the
            // final safe boundary: forget the active capture and avoid I2C on
            // future idempotent close calls.
            capture_active_ = false;
            open_receiver_ = -1;
        } else if (pins_disabled) {
            open_receiver_ = -1;
        }
    } else {
        open_receiver_ = -1;
    }
    tuned_ = false;
    selected_tsid_ = 0U;
    if (first == Error::OK && !operation_failed_) current_stage_ = DiagnosticStage::closed;
    return first == Error::OK ? Result<void>::success() : Result<void>::failure(first);
}

Result<void> Q3U4Frontend::open_terrestrial(std::uint8_t local_receiver) noexcept
{
    begin_operation();
    if (local_receiver < 2U || local_receiver > 3U) {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    if (open_receiver_ >= 0 || backend_on_) return Result<void>::failure(Error::BUSY);

    attempt(DiagnosticStage::backend_power_on);
    auto power = power_.set_backend_power(true, delay_);
    if (!power) {
        remember_failure(power);
        (void)power_.set_backend_power(false, delay_);
        return power;
    }
    backend_on_ = true;

    Error failure = Error::OK;
    for (std::uint8_t i = 0U; i < 4U; ++i) {
        attempt(static_cast<DiagnosticStage>(static_cast<std::uint8_t>(
            DiagnosticStage::tuner_initialize_rt710_0) + i));
        const auto result = i < 2U ? rt710_[i].initialize() : r850_[i - 2U].initialize();
        if (!result) { remember_failure(result); failure = result.error(); break; }
        tuner_initialized_[i] = true;
    }
    if (failure != Error::OK) {
        (void)cleanup();
        return Result<void>::failure(failure);
    }
    for (std::uint8_t i = 0U; i < 4U; ++i) {
        if (i == local_receiver) continue;
        const auto result = sleep_frontend(i);
        if (!result) { failure = result.error(); break; }
    }
    if (failure != Error::OK) { (void)cleanup(); return Result<void>::failure(failure); }

    attempt(DiagnosticStage::selected_tc_init_writes);
    auto result = write_fixed(tc_[local_receiver], kTcInitTValues);
    if (!result) { remember_failure(result); }
    if (result) { attempt(DiagnosticStage::ts_pin_disable); result = tc_[local_receiver].enable_ts_pins_t(false); }
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::demod_wake); result = tc_[local_receiver].sleep_t(false); }
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::selected_r850_wake); result = r850_[local_receiver - 2U].wakeup(); }
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::r850_system_set); result = r850_[local_receiver - 2U].set_system(
        R850SystemConfig{R850System::isdb_t, R850Bandwidth::mhz_6, 4063U});
    }
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::shared_s0); result = write_fixed(tc_[0], kSharedS0); }
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::shared_t0); result = write_fixed(tc_[2], kSharedT0); }
    if (!result) remember_failure(result);
    if (!result) { (void)cleanup(); return result; }
    open_receiver_ = static_cast<std::int8_t>(local_receiver);
    tuned_ = false;
    capture_active_ = false;
    current_stage_ = DiagnosticStage::open;
    open_system_ = Tc90522System::isdb_t;
    return Result<void>::success();
}

Result<void> Q3U4Frontend::open_satellite(std::uint8_t local_receiver) noexcept
{
    begin_operation();
    if (local_receiver >= 2U) return Result<void>::failure(Error::UNSUPPORTED);
    if (open_receiver_ >= 0 || backend_on_) return Result<void>::failure(Error::BUSY);

    attempt(DiagnosticStage::backend_power_on);
    auto power = power_.set_backend_power(true, delay_);
    if (!power) {
        remember_failure(power);
        (void)power_.set_backend_power(false, delay_);
        return power;
    }
    backend_on_ = true;

    Error failure = Error::OK;
    for (std::uint8_t i = 0U; i < 4U; ++i) {
        attempt(static_cast<DiagnosticStage>(static_cast<std::uint8_t>(
            DiagnosticStage::tuner_initialize_rt710_0) + i));
        const auto result = i < 2U ? rt710_[i].initialize() : r850_[i - 2U].initialize();
        if (!result) { remember_failure(result); failure = result.error(); break; }
        tuner_initialized_[i] = true;
    }
    if (failure != Error::OK) { (void)cleanup(); return Result<void>::failure(failure); }

    for (std::uint8_t i = 0U; i < 4U; ++i) {
        if (i == local_receiver) continue;
        const auto result = sleep_frontend(i);
        if (!result) { failure = result.error(); break; }
    }
    if (failure != Error::OK) { (void)cleanup(); return Result<void>::failure(failure); }

    attempt(DiagnosticStage::satellite_selected_tc_init_writes);
    auto result = write_fixed(tc_[local_receiver], kTcInitSValues);
    if (!result) remember_failure(result);
    if (result) {
        attempt(DiagnosticStage::satellite_ts_pin_disable);
        result = tc_[local_receiver].enable_ts_pins_s(false);
    }
    if (!result) remember_failure(result);
    if (result) {
        attempt(DiagnosticStage::satellite_demod_wake);
        result = tc_[local_receiver].sleep_s(false);
    }
    if (!result) remember_failure(result);
    if (!result) { (void)cleanup(); return result; }

    attempt(DiagnosticStage::shared_s0);
    result = write_fixed(tc_[0], kSharedS0);
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::shared_t0); result = write_fixed(tc_[2], kSharedT0); }
    if (!result) remember_failure(result);
    if (!result) { (void)cleanup(); return result; }

    open_receiver_ = static_cast<std::int8_t>(local_receiver);
    open_system_ = Tc90522System::isdb_s;
    tuned_ = false;
    capture_active_ = false;
    selected_tsid_ = 0U;
    current_stage_ = DiagnosticStage::open;
    return Result<void>::success();
}

Result<void> Q3U4Frontend::tune_terrestrial(std::uint32_t frequency_khz) noexcept
{
    begin_operation();
    if (open_receiver_ < 2) return Result<void>::failure(Error::NOT_READY);
    attempt(DiagnosticStage::tune_tc47);
    auto result = tc_[static_cast<std::size_t>(open_receiver_)].write_reg(0x47U, 0x30U);
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::agc_off); result = tc_[static_cast<std::size_t>(open_receiver_)].set_agc_t(false); }
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::tc76); result = tc_[static_cast<std::size_t>(open_receiver_)].write_reg(0x76U, 0x0cU); }
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::r850_set_frequency); result = r850_[static_cast<std::size_t>(open_receiver_ - 2)].set_frequency(frequency_khz); }
    if (!result) { remember_failure(result); return result; }
    attempt(DiagnosticStage::pll_poll);
    bool locked = false;
    Error final_error = Error::OK;
    for (std::size_t attempt = 0U; attempt < 50U; ++attempt) {
        const auto status = r850_[static_cast<std::size_t>(open_receiver_ - 2)].is_pll_locked();
        if (!status) {
            final_error = status.error();
        } else if (status.value()) {
            locked = true;
            break;
        } else {
            final_error = Error::OK;
        }
        delay_.sleep_ms(10U);
    }
    if (final_error != Error::OK && !locked) {
        remember_failure(Result<void>::failure(final_error));
        return Result<void>::failure(final_error);
    }
    if (!locked) { const auto timeout = Result<void>::failure(Error::TIMEOUT); remember_failure(timeout); return timeout; }
    attempt(DiagnosticStage::agc_on);
    result = tc_[static_cast<std::size_t>(open_receiver_)].set_agc_t(true);
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::tc71_72_75); result = tc_[static_cast<std::size_t>(open_receiver_)].write_reg(0x71U, 0x21U); }
    if (!result) remember_failure(result);
    if (result) result = tc_[static_cast<std::size_t>(open_receiver_)].write_reg(0x72U, 0x25U);
    if (!result) remember_failure(result);
    if (result) result = tc_[static_cast<std::size_t>(open_receiver_)].write_reg(0x75U, 0x08U);
    if (!result) remember_failure(result);
    if (result) current_stage_ = DiagnosticStage::tuned;
    if (result) tuned_ = true;
    return result;
}

Result<void> Q3U4Frontend::tune_satellite(std::uint32_t frequency_khz) noexcept
{
    begin_operation();
    if (open_receiver_ < 0 || open_system_ != Tc90522System::isdb_s)
        return Result<void>::failure(Error::NOT_READY);
    auto& demod = tc_[static_cast<std::size_t>(open_receiver_)];
    auto& tuner = rt710_[static_cast<std::size_t>(open_receiver_)];

    attempt(DiagnosticStage::satellite_agc_off);
    auto result = demod.set_agc_s(false);
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::satellite_mode_registers); result = demod.write_reg(0x8eU, 0x06U); }
    if (!result) remember_failure(result);
    if (result) result = demod.write_reg(0xa3U, 0xf7U);
    if (!result) remember_failure(result);
    if (result) { attempt(DiagnosticStage::satellite_set_params); result = tuner.set_params(frequency_khz, 28860U, 4U); }
    if (!result) { remember_failure(result); return result; }

    attempt(DiagnosticStage::satellite_pll_poll);
    bool locked = false;
    Error last_error = Error::OK;
    for (std::size_t attempt_count = 0U; attempt_count < 50U; ++attempt_count) {
        const auto status = tuner.is_pll_locked();
        if (status && status.value()) { locked = true; break; }
        last_error = status ? Error::OK : status.error();
        delay_.sleep_ms(10U);
    }
    if (!locked) {
        const auto failed = Result<void>::failure(last_error == Error::OK ? Error::TIMEOUT : last_error);
        remember_failure(failed);
        return failed;
    }
    attempt(DiagnosticStage::satellite_agc_on);
    result = demod.set_agc_s(true);
    if (!result) remember_failure(result);
    if (result) tuned_ = true;
    if (result) current_stage_ = DiagnosticStage::tuned;
    return result;
}

Result<void> Q3U4Frontend::select_satellite_slot(std::uint8_t slot) noexcept
{
    begin_operation();
    if (slot >= 12U) return Result<void>::failure(Error::INVALID_ARGUMENT);
    if (open_receiver_ < 0 || open_system_ != Tc90522System::isdb_s || !tuned_)
        return Result<void>::failure(Error::NOT_READY);
    auto& demod = tc_[static_cast<std::size_t>(open_receiver_)];

    attempt(DiagnosticStage::satellite_slot_tmcc);
    std::uint16_t tsid = 0U;
    Error last_error = Error::OK;
    bool found = false;
    for (std::size_t attempt_count = 0U; attempt_count < 100U; ++attempt_count) {
        const auto value = demod.tmcc_get_tsid_s(slot);
        if (value && value.value() != 0U) { tsid = value.value(); found = true; break; }
        last_error = value ? Error::OK : value.error();
        delay_.sleep_ms(10U);
    }
    if (!found) {
        const auto failed = Result<void>::failure(last_error == Error::OK ? Error::TIMEOUT : last_error);
        remember_failure(failed);
        return failed;
    }

    attempt(DiagnosticStage::satellite_slot_set);
    auto result = demod.set_tsid_s(tsid);
    if (!result) { remember_failure(result); return result; }

    attempt(DiagnosticStage::satellite_slot_verify);
    bool verified = false;
    last_error = Error::OK;
    for (std::size_t attempt_count = 0U; attempt_count < 100U; ++attempt_count) {
        const auto value = demod.get_tsid_s();
        if (value && value.value() == tsid) { verified = true; break; }
        last_error = value ? Error::OK : value.error();
        delay_.sleep_ms(10U);
    }
    if (!verified) {
        const auto failed = Result<void>::failure(last_error == Error::OK ? Error::TIMEOUT : last_error);
        remember_failure(failed);
        return failed;
    }
    selected_tsid_ = tsid;
    current_stage_ = DiagnosticStage::satellite_demod_lock;
    return Result<void>::success();
}

Result<void> Q3U4Frontend::start_terrestrial_capture() noexcept
{
    begin_operation();
    if (open_receiver_ < 2 || !tuned_) return Result<void>::failure(Error::NOT_READY);
    if (capture_active_) return Result<void>::failure(Error::BUSY);

    attempt(DiagnosticStage::ts_pin_enable);
    const auto result = tc_[static_cast<std::size_t>(open_receiver_)].enable_ts_pins_t(true);
    if (!result) remember_failure(result);
    if (result) capture_active_ = true;
    return result;
}

Result<void> Q3U4Frontend::start_satellite_capture() noexcept
{
    begin_operation();
    if (open_receiver_ < 0 || open_system_ != Tc90522System::isdb_s ||
        !tuned_ || selected_tsid_ == 0U)
        return Result<void>::failure(Error::NOT_READY);
    if (capture_active_) return Result<void>::failure(Error::BUSY);
    // Mark the pins as requiring cleanup before the I2C write.  A bridge
    // failure can occur after a device has partially applied the write, so
    // cleanup must retry the satellite disable before backend power-off.
    capture_active_ = true;
    attempt(DiagnosticStage::satellite_ts_pin_enable);
    const auto result = tc_[static_cast<std::size_t>(open_receiver_)].enable_ts_pins_s(true);
    if (!result) remember_failure(result);
    return result;
}

Result<void> Q3U4Frontend::stop_terrestrial_capture() noexcept
{
    begin_operation();
    if (!capture_active_) return Result<void>::success();

    attempt(DiagnosticStage::ts_pin_disable);
    const auto result = tc_[static_cast<std::size_t>(open_receiver_)].enable_ts_pins_t(false);
    if (!result) {
        remember_failure(result);
        return result;
    }
    capture_active_ = false;
    return Result<void>::success();
}

Result<void> Q3U4Frontend::stop_satellite_capture() noexcept
{
    begin_operation();
    if (!capture_active_) return Result<void>::success();
    if (open_receiver_ < 0 || open_system_ != Tc90522System::isdb_s)
        return Result<void>::failure(Error::NOT_READY);
    attempt(DiagnosticStage::satellite_ts_pin_disable);
    const auto result = tc_[static_cast<std::size_t>(open_receiver_)].enable_ts_pins_s(false);
    if (!result) { remember_failure(result); return result; }
    capture_active_ = false;
    return Result<void>::success();
}

Result<bool> Q3U4Frontend::is_terrestrial_locked() noexcept
{
    if (open_receiver_ < 2) return Result<bool>::failure(Error::NOT_READY);
    return tc_[static_cast<std::size_t>(open_receiver_)].is_signal_locked_t();
}

Result<bool> Q3U4Frontend::is_satellite_locked() noexcept
{
    if (open_receiver_ < 0 || open_system_ != Tc90522System::isdb_s)
        return Result<bool>::failure(Error::NOT_READY);
    attempt(DiagnosticStage::satellite_demod_lock);
    return tc_[static_cast<std::size_t>(open_receiver_)].is_signal_locked_s();
}

Result<bool> Q3U4Frontend::terrestrial_loop_through(std::uint8_t local_receiver) const noexcept
{
    if (local_receiver < 2U || local_receiver > 3U)
        return Result<bool>::failure(Error::INVALID_ARGUMENT);
    return Result<bool>::success(r850_[local_receiver - 2U].loop_through());
}

Result<void> Q3U4Frontend::close() noexcept { begin_operation(); return cleanup(); }
Result<void> Q3U4Frontend::terminate() noexcept { begin_operation(); return cleanup(); }

}  // namespace px4::userland

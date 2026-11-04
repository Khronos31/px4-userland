// SPDX-License-Identifier: GPL-2.0-only
#include "q3u4_tuner_backend.h"
#include "px4_tuner_hz.h"

#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
#include <cstdio>
#endif

namespace px4::userland {

namespace {

bool valid_receiver(std::uint8_t receiver) noexcept
{
    return receiver < 8U;
}

bool is_satellite(std::uint8_t receiver) noexcept
{
    return receiver == 0U || receiver == 1U || receiver == 4U || receiver == 5U;
}

}  // namespace

Result<void> Q3U4FrontendTunerBackend::open_receiver(
    std::uint8_t receiver) noexcept
{
    if (!valid_receiver(receiver)) return Result<void>::failure(Error::INVALID_ARGUMENT);
    return is_satellite(receiver) ? enclosure_.open_satellite(receiver)
                                   : enclosure_.open_terrestrial(receiver);
}

Result<void> Q3U4FrontendTunerBackend::tune_terrestrial(
    std::uint8_t receiver, std::uint32_t frequency_khz,
    std::uint32_t timeout_ms) noexcept
{
    return enclosure_.tune_terrestrial_with_timeout(receiver, frequency_khz,
                                                    timeout_ms);
}

Result<void> Q3U4FrontendTunerBackend::tune_satellite(
    std::uint8_t receiver, std::uint32_t frequency_khz,
    std::uint32_t timeout_ms) noexcept
{
    return enclosure_.tune_satellite_with_timeout(receiver, frequency_khz,
                                                  timeout_ms);
}

Result<TunerHzProgramming> Q3U4FrontendTunerBackend::tune_terrestrial_hz(
    std::uint8_t receiver, std::uint64_t frequency_hz,
    std::uint32_t timeout_ms) noexcept
{
    const auto frequency = px4_frequency_khz(frequency_hz, ipc::System::ISDB_T);
    if (!frequency || !receiver_supports(receiver, ipc::System::ISDB_T))
        return Result<TunerHzProgramming>::failure(
            frequency ? Error::UNSUPPORTED : frequency.error());
    const auto tuned = tune_terrestrial(receiver, frequency.value(), timeout_ms);
    if (!tuned) return Result<TunerHzProgramming>::failure(tuned.error());
    return Result<TunerHzProgramming>::success(px4_programming(frequency.value()));
}

Result<TunerHzProgramming> Q3U4FrontendTunerBackend::tune_satellite_hz(
    std::uint8_t receiver, std::uint64_t frequency_hz,
    std::uint32_t timeout_ms) noexcept
{
    const auto frequency = px4_frequency_khz(frequency_hz, ipc::System::ISDB_S);
    if (!frequency || !receiver_supports(receiver, ipc::System::ISDB_S))
        return Result<TunerHzProgramming>::failure(
            frequency ? Error::UNSUPPORTED : frequency.error());
    const auto tuned = tune_satellite(receiver, frequency.value(), timeout_ms);
    if (!tuned) return Result<TunerHzProgramming>::failure(tuned.error());
    return Result<TunerHzProgramming>::success(px4_programming(frequency.value()));
}

TunerStrictProfile Q3U4FrontendTunerBackend::strict_profile(
    std::uint8_t receiver, ipc::System system) const noexcept
{
    if (!receiver_supports(receiver, system)) return {};
    return px4_strict_profile(system, true);
}

Result<bool> Q3U4FrontendTunerBackend::is_locked(
    std::uint8_t receiver, ipc::System system) noexcept
{
    if (!valid_receiver(receiver)) return Result<bool>::failure(Error::INVALID_ARGUMENT);
    if (system == ipc::System::ISDB_T)
        return enclosure_.is_terrestrial_locked(receiver);
    if (system == ipc::System::ISDB_S)
        return enclosure_.is_satellite_locked(receiver);
    return Result<bool>::failure(Error::INVALID_ARGUMENT);
}

Result<void> Q3U4FrontendTunerBackend::select_satellite_slot(
    std::uint8_t receiver, std::uint8_t slot,
    std::uint32_t timeout_ms) noexcept
{
    return enclosure_.select_satellite_slot_with_timeout(receiver, slot, timeout_ms);
}

Result<void> Q3U4FrontendTunerBackend::select_satellite_tsid(
    std::uint8_t receiver, std::uint16_t tsid,
    std::uint32_t timeout_ms) noexcept
{
    return enclosure_.select_satellite_tsid_with_timeout(receiver, tsid, timeout_ms);
}

Result<void> Q3U4FrontendTunerBackend::close_receiver(
    std::uint8_t receiver) noexcept
{
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=tuner-backend op=close_receiver phase=call receiver=%u valid=%u satellite=%u\n",
                 static_cast<unsigned>(receiver), valid_receiver(receiver) ? 1U : 0U,
                 is_satellite(receiver) ? 1U : 0U);
#endif
    Error first = Error::OK;
    if (is_satellite(receiver)) {
        const auto released = lnb_power_.release_receiver(receiver);
        if (!released) first = released.error();
        if (released.error() == Error::DISCONNECTED) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            std::fprintf(stderr,
                         "PX4_OWNER_DIAG layer=tuner-backend op=close_receiver phase=return error=%s stage=lnb-release\n",
                         error_string(first));
#endif
            return Result<void>::failure(first);
        }
    }
    const auto closed = enclosure_.close_receiver(receiver);
    if (!closed && first == Error::OK) first = closed.error();
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=tuner-backend op=close_receiver phase=return error=%s stage=enclosure-close\n",
                 error_string(first));
#endif
    return first == Error::OK ? Result<void>::success()
                              : Result<void>::failure(first);
}

Result<void> Q3U4FrontendTunerBackend::begin_tune_power(
    std::uint8_t receiver, ipc::System system,
    std::uint8_t lnb_voltage) noexcept
{
    if (!valid_receiver(receiver) || system !=
            (is_satellite(receiver) ? ipc::System::ISDB_S : ipc::System::ISDB_T)) {
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    if (system == ipc::System::ISDB_T) {
        return lnb_voltage == 0U ? Result<void>::success()
                                 : Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    return lnb_power_.begin_tune(receiver, lnb_voltage);
}

Result<void> Q3U4FrontendTunerBackend::commit_tune_power(
    std::uint8_t receiver) noexcept
{
    return is_satellite(receiver) ? lnb_power_.commit_tune(receiver)
                                  : Result<void>::success();
}

Result<void> Q3U4FrontendTunerBackend::rollback_tune_power(
    std::uint8_t receiver) noexcept
{
    return is_satellite(receiver) ? lnb_power_.rollback_tune(receiver)
                                  : Result<void>::success();
}

Result<void> Q3U4FrontendTunerBackend::set_independent_lnb_power(
    bool enabled) noexcept
{
    return lnb_power_.set_independent_power(enabled);
}

dtv::userland::wire::Optional<bool>
Q3U4FrontendTunerBackend::independent_lnb_power_state() noexcept
{
    const Q3U4LnbPowerSnapshot state = lnb_power_.snapshot();
    if (state.physical_state[0U] == Q3U4LnbPhysicalState::on &&
        state.physical_state[1U] == Q3U4LnbPhysicalState::on) {
        return {true, true};
    }
    if (state.physical_state[0U] == Q3U4LnbPhysicalState::off &&
        state.physical_state[1U] == Q3U4LnbPhysicalState::off) {
        return {true, false};
    }
    return {};
}

void Q3U4FrontendTunerBackend::mark_receiver_disconnected(
    std::uint8_t receiver) noexcept
{
    if (!valid_receiver(receiver)) return;
    const Q3U4Bridge bridge = receiver < 4U ? Q3U4Bridge::dev1
                                            : Q3U4Bridge::dev2;
    (void)lnb_power_.disconnect(bridge);
    (void)enclosure_.mark_bridge_disconnected(bridge);
}

Result<void> Q3U4FrontendTunerBackend::shutdown() noexcept
{
    return lnb_power_.shutdown();
}

Result<void> Q3U4FrontendTunerBackend::start_capture(
    std::uint8_t receiver, ipc::System system) noexcept
{
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    const auto mapping = Q3U4FrontendEnclosure::map_receiver(receiver);
    const char* route = "invalid";
#endif
    Result<void> result = Result<void>::failure(Error::INVALID_ARGUMENT);
    if (valid_receiver(receiver)) {
        if (system == ipc::System::ISDB_T) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            route = "terrestrial";
#endif
            result = enclosure_.start_terrestrial_capture(receiver);
        } else if (system == ipc::System::ISDB_S) {
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
            route = "satellite";
#endif
            result = enclosure_.start_satellite_capture(receiver);
        }
    }
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=tuner-backend op=start_capture receiver=%u system=%u receiver-valid=%u route=%s mapping-valid=%u bridge=%u local=%u mapped-system=%u result=%s error=%s\n",
                 static_cast<unsigned>(receiver), static_cast<unsigned>(system),
                 valid_receiver(receiver) ? 1U : 0U, route,
                 mapping ? 1U : 0U,
                 mapping ? static_cast<unsigned>(mapping.value().bridge) : 255U,
                 mapping ? static_cast<unsigned>(mapping.value().local_receiver) : 255U,
                 mapping ? static_cast<unsigned>(mapping.value().system) : 255U,
                 result ? "ok" : "error",
                 result ? "OK" : error_string(result.error()));
#endif
    return result;
}

Result<void> Q3U4FrontendTunerBackend::stop_capture(
    std::uint8_t receiver, ipc::System system) noexcept
{
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=tuner-backend op=stop_capture phase=call receiver=%u system=%u valid=%u\n",
                 static_cast<unsigned>(receiver), static_cast<unsigned>(system),
                 valid_receiver(receiver) ? 1U : 0U);
#endif
    Result<void> result = Result<void>::failure(Error::INVALID_ARGUMENT);
    if (valid_receiver(receiver)) {
        if (system == ipc::System::ISDB_T)
            result = enclosure_.stop_terrestrial_capture(receiver);
        else if (system == ipc::System::ISDB_S)
            result = enclosure_.stop_satellite_capture(receiver);
    }
#if defined(DTV_USERLAND_IPC_DIAGNOSTICS)
    std::fprintf(stderr,
                 "PX4_OWNER_DIAG layer=tuner-backend op=stop_capture phase=return result=%s error=%s\n",
                 result ? "ok" : "error",
                 result ? "OK" : error_string(result.error()));
#endif
    return result;
}

}  // namespace px4::userland

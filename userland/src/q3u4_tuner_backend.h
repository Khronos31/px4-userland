// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_Q3U4_TUNER_BACKEND_H
#define PX4_USERLAND_Q3U4_TUNER_BACKEND_H

#include "px4/tuner_service.h"
#include "q3u4_frontend.h"
#include "q3u4_lnb_power.h"

namespace px4::userland {

// Production bridge from the portable tuner seam to the Q3U4 enclosure.
class Q3U4FrontendTunerBackend final : public TunerServiceBackend {
public:
    Q3U4FrontendTunerBackend(Q3U4FrontendEnclosure& enclosure,
                             Q3U4LnbPowerCoordinator& lnb_power) noexcept
        : enclosure_(enclosure), lnb_power_(lnb_power)
    {
    }

    Result<void> open_receiver(std::uint8_t receiver) noexcept override;
    Result<void> tune_terrestrial(std::uint8_t receiver,
                                  std::uint32_t frequency_khz,
                                  std::uint32_t timeout_ms) noexcept override;
    Result<void> tune_satellite(std::uint8_t receiver,
                                std::uint32_t frequency_khz,
                                std::uint32_t timeout_ms) noexcept override;
    // Native-Hz R850/RT710 programming through the same enclosure routing.
    // The fractional PLL exposes no honest final command scalar, so
    // programmed_hz stays absent.
    Result<TunerHzProgramming> tune_terrestrial_hz(std::uint8_t receiver,
                                                   std::uint64_t frequency_hz,
                                                   std::uint32_t timeout_ms) noexcept override;
    Result<TunerHzProgramming> tune_satellite_hz(std::uint8_t receiver,
                                                 std::uint64_t frequency_hz,
                                                 std::uint32_t timeout_ms) noexcept override;
    TunerStrictProfile strict_profile(std::uint8_t receiver,
                                      ipc::System system) const noexcept override;
    Result<bool> is_locked(std::uint8_t receiver,
                           ipc::System system) noexcept override;
    bool requires_terrestrial_lock_settle() const noexcept override
    {
        return true;
    }
    Result<void> select_satellite_slot(std::uint8_t receiver,
                                       std::uint8_t slot,
                                       std::uint32_t timeout_ms) noexcept override;
    Result<void> select_satellite_tsid(std::uint8_t receiver,
                                       std::uint16_t tsid,
                                       std::uint32_t timeout_ms) noexcept override;
    Result<void> close_receiver(std::uint8_t receiver) noexcept override;
    Result<void> start_capture(std::uint8_t receiver,
                               ipc::System system) noexcept override;
    Result<void> stop_capture(std::uint8_t receiver,
                              ipc::System system) noexcept override;
    Result<void> begin_tune_power(std::uint8_t receiver, ipc::System system,
                                  std::uint8_t lnb_voltage) noexcept override;
    Result<void> commit_tune_power(std::uint8_t receiver) noexcept override;
    Result<void> rollback_tune_power(std::uint8_t receiver) noexcept override;
    Result<void> set_independent_lnb_power(bool enabled) noexcept override;
    dtv::userland::wire::Optional<bool> independent_lnb_power_state() noexcept override;
    void mark_receiver_disconnected(std::uint8_t receiver) noexcept override;
    Result<void> shutdown() noexcept override;

private:
    Q3U4FrontendEnclosure& enclosure_;
    Q3U4LnbPowerCoordinator& lnb_power_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_Q3U4_TUNER_BACKEND_H

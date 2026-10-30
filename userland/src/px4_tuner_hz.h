// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_TUNER_HZ_H
#define PX4_USERLAND_TUNER_HZ_H

#include "px4/tuner_service.h"

#include <cstdint>

namespace px4::userland {

inline TunerStrictProfile px4_strict_profile(ipc::System system,
                                             bool supports_lnb_15v) noexcept
{
    TunerStrictProfile profile;
    if (system != ipc::System::ISDB_T && system != ipc::System::ISDB_S) return profile;
    profile.native_hz = true;
    if (system == ipc::System::ISDB_T) {
        profile.min_hz = 40000000U;
        profile.max_hz = 1002000000U;
    } else {
        profile.min_hz = 146875000U;
        profile.max_hz = 2350000000U;
        profile.lnb_voltage_mask = supports_lnb_15v ? 0x03U : 0x01U;
        profile.slot_count = 12U;
    }
    return profile;
}

inline Result<std::uint32_t> px4_frequency_khz(std::uint64_t frequency_hz,
                                               ipc::System system) noexcept
{
    const std::uint64_t minimum = system == ipc::System::ISDB_T ? 40000000U :
                                  system == ipc::System::ISDB_S ? 146875000U : 0U;
    const std::uint64_t maximum = system == ipc::System::ISDB_T ? 1002000000U :
                                  system == ipc::System::ISDB_S ? 2350000000U : 0U;
    if (minimum == 0U || frequency_hz < minimum || frequency_hz > maximum)
        return Result<std::uint32_t>::failure(Error::UNSUPPORTED);
    return Result<std::uint32_t>::success(
        static_cast<std::uint32_t>(frequency_hz / 1000U));
}

inline TunerHzProgramming px4_programming(std::uint32_t frequency_khz) noexcept
{
    TunerHzProgramming programming;
    programming.programmed_hz = static_cast<std::uint64_t>(frequency_khz) * 1000U;
    return programming;
}

}  // namespace px4::userland

#endif  // PX4_USERLAND_TUNER_HZ_H

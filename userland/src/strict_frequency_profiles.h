// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_STRICT_FREQUENCY_PROFILES_H
#define PX4_USERLAND_STRICT_FREQUENCY_PROFILES_H

#include "px4/tuner_service.h"

#include <cstdint>

namespace px4::userland {

// Reviewed R3 strict requested-Hz domains (receiver-hz-profile-table.md).
// They are software admission domains derived from the actual frontend
// expressions, not measured hardware bands.  Satellite values are IF Hz.

// R850 ISDB-T: 40 MHz..1002 MHz inclusive, valid for both chip variants and
// every reachable crystal-power class.
inline TunerStrictProfile r850_terrestrial_strict_profile() noexcept
{
    TunerStrictProfile profile{};
    profile.native_hz = true;
    profile.min_hz = 40000000U;
    profile.max_hz = 1002000000U;
    return profile;
}

// RT710/RT720 ISDB-S IF: 146.875 MHz..2350 MHz inclusive (mixers 2..16 cover
// the 2350..4700 MHz VCO range without a gap), TC90522 slots 0..11.
inline TunerStrictProfile rt710_satellite_strict_profile() noexcept
{
    TunerStrictProfile profile{};
    profile.native_hz = true;
    profile.min_hz = 146875000U;
    profile.max_hz = 2350000000U;
    profile.slot_count = 12U;
    profile.lnb_voltage_mask = 0x03U;
    return profile;
}

// CXD2858ER ISDB-T: same request domain; field = floor(Hz / 1000).
inline TunerStrictProfile mlt_terrestrial_strict_profile() noexcept
{
    return r850_terrestrial_strict_profile();
}

// CXD2858ER ISDB-S IF: same request domain; field = floor((Hz + 2000) / 4000).
// The CXD2856ER rejects slot 8 and above before the tune.
inline TunerStrictProfile mlt_satellite_strict_profile() noexcept
{
    TunerStrictProfile profile = rt710_satellite_strict_profile();
    profile.slot_count = 8U;
    return profile;
}

}  // namespace px4::userland

#endif  // PX4_USERLAND_STRICT_FREQUENCY_PROFILES_H

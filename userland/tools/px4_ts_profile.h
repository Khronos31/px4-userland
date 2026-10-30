// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_PX4_TS_PROFILE_H
#define PX4_USERLAND_PX4_TS_PROFILE_H

namespace px4::userland::cli {

// Boolean fields of the px4-ts product profile. The front ends and the CLI
// test share these names so a positional aggregate mistake cannot silently
// change the instance contract.
inline constexpr bool kPx4TsSerialSelectorSupported = true;
inline constexpr bool kPx4TsPreserveInstanceCase = true;

}  // namespace px4::userland::cli

#endif  // PX4_USERLAND_PX4_TS_PROFILE_H

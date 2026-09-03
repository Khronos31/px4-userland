// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_Q3U4_POWER_H
#define PX4_USERLAND_Q3U4_POWER_H

#include <cstdint>

namespace px4::userland {

// Portable timing seam for the narrow Q3U4 backend power sequence.
class Q3U4Delay {
public:
    virtual ~Q3U4Delay() noexcept = default;
    virtual void sleep_ms(std::uint32_t milliseconds) noexcept = 0;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_Q3U4_POWER_H

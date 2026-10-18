// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_CONTROL_CONTEXT_H
#define PX4_USERLAND_CONTROL_CONTEXT_H

#include "px4/control_server.h"
#include "px4/identity.h"
#include "px4/tuner_service.h"

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

namespace px4::userland::ipc::posix {

// Facts of the opened enclosure that a px4d context is built from.  bridges
// holds the selected USB observation of each bridge in bridge order (the
// observation the runtime claimed), or null when it is not available.
struct Px4ContextInput final {
    DeviceModel model = DeviceModel::px_q3u4;
    std::string_view base_serial;
    std::array<const DeviceObservation*, 2U> bridges{};
    std::string_view product_version;
    std::string_view instance;
    dtv::userland::wire::Uuid epoch{};
    std::optional<bool> serial_unique_in_scan;
};

// Builds the canonical context from the profile, the bridge observations and
// the tuner service's fixed receiver set and owned strict profiles.  Public
// receiver IDs are the existing receiver numbers 0..N-1, bound to the bridge
// that carries them; the card reader is reader 0 on the first bridge.  No
// value is padded or guessed: an observation that is missing or not
// representable is reported as such, and a profile/service mismatch fails.
Result<void> build_px4_control_context(const Px4ContextInput& input,
                                       const TunerService& tuner,
                                       ControlContext& out) noexcept;

}  // namespace px4::userland::ipc::posix

#endif  // PX4_USERLAND_CONTROL_CONTEXT_H

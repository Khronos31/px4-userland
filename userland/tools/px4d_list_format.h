// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "px4/identity.h"

#include <string>

namespace px4::userland::tools {

// Formats `px4d --list` (SPEC 4.6).  For each enclosure of a supported model:
// one "serial=... model=... usb=... status=... receivers=..." line followed by
// the model's fixed receiver table in the `px4ctl list` receiver format.  Then
// one "rejected ..." line per supported-model USB device that could not be
// grouped.  USB devices of other models are not printed.
std::string format_device_list(const GroupingResult& grouping);

} // namespace px4::userland::tools

// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_PX4D_LISTENER_ERROR_H
#define PX4_USERLAND_PX4D_LISTENER_ERROR_H

#include <dtv/userland/host_listener_context/enclosure_context.h>
#include <dtv/userland/error.h>

#include <string>

namespace px4::userland::tools {

std::string format_listener_error(
    dtv::userland::Error error,
    const dtv::userland::host_listener_context::ListenerDiagnostic& diagnostic,
    const char* operation);
std::string format_usb_selection_error(dtv::userland::Error error);
std::string format_firmware_error(dtv::userland::Error error);

}  // namespace px4::userland::tools

#endif  // PX4_USERLAND_PX4D_LISTENER_ERROR_H

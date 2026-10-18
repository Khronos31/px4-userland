// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_RUNTIME_IDENTITY_H
#define PX4_USERLAND_RUNTIME_IDENTITY_H

#include "px4/identity.h"

#include <dtv/userland/identity.h>

#include <array>
#include <charconv>
#include <string>
#include <system_error>

namespace px4::userland {

inline bool append_runtime_usb_number(std::string& output, unsigned int value) noexcept
{
    std::array<char, 16U> bytes{};
    const auto converted = std::to_chars(bytes.data(), bytes.data() + bytes.size(), value);
    if (converted.ec != std::errc{}) return false;
    output.append(bytes.data(), static_cast<std::size_t>(converted.ptr - bytes.data()));
    return true;
}

inline Result<dtv::userland::identity::ResolvedIdentity> automatic_px4_identity(
    DeviceModel model, std::string_view base_serial,
    const std::array<const DeviceObservation*, 2U>& functions,
    std::size_t function_count, bool serial_unique_in_scan) noexcept
{
    namespace identity = dtv::userland::identity;
    if (device_profile(model).serial_identity_supported && serial_unique_in_scan) {
        const auto serial = identity::make_serial_selector(base_serial);
        if (serial) {
            identity::Selector selector{};
            selector.kind = identity::SelectorKind::serial;
            selector.serial = serial.value();
            const auto resolved = identity::resolve_selector(identity::Product::px4, selector);
            if (resolved) return Result<identity::ResolvedIdentity>::success(resolved.value());
        }
    }

    if (function_count == 0U || function_count > functions.size())
        return Result<identity::ResolvedIdentity>::failure(Error::INVALID_ARGUMENT);
    std::string selector_text("usb:");
    for (std::size_t index = 0U; index < function_count; ++index) {
        const DeviceObservation* observation = functions[index];
        if (observation == nullptr || !observation->location.has_bus ||
            observation->location.port_count == 0U)
            return Result<identity::ResolvedIdentity>::failure(Error::INVALID_ARGUMENT);
        if (index != 0U) selector_text.push_back(',');
        if (!append_runtime_usb_number(selector_text, observation->location.bus))
            return Result<identity::ResolvedIdentity>::failure(Error::INVALID_ARGUMENT);
        selector_text.push_back('-');
        for (std::size_t port = 0U; port < observation->location.port_count; ++port) {
            if (port != 0U) selector_text.push_back('.');
            if (!append_runtime_usb_number(selector_text,
                                           observation->location.port_path[port]))
                return Result<identity::ResolvedIdentity>::failure(Error::INVALID_ARGUMENT);
        }
    }
    const auto selector = identity::parse_device_selector(selector_text);
    if (!selector) return Result<identity::ResolvedIdentity>::failure(selector.error());
    return identity::resolve_selector(identity::Product::px4, selector.value());
}

}  // namespace px4::userland

#endif  // PX4_USERLAND_RUNTIME_IDENTITY_H

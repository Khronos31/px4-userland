// SPDX-License-Identifier: GPL-2.0-only
#include "px4/identity.h"

#include "host_ctl.h"

#if defined(_WIN32)
#include "px4_windows_args.h"
#endif

#include <cstring>
#include <string>
#include <vector>

namespace {

namespace wire = dtv::userland::wire;
namespace identity = dtv::userland::identity;

bool matches_px4_serial(const wire::ListResponse& list,
                        const identity::ResolvedIdentity& expected) noexcept
{
    if (!list.enclosure.present || expected.kind != identity::IdentityKind::serial)
        return false;
    const wire::Identity& observed = list.enclosure.value.identity;
    if (observed.uniqueness != wire::Uniqueness::unique_in_scan ||
        list.functions.count == 0U)
        return false;
    const px4::userland::DeviceProfile* profile =
        px4::userland::device_profile_for_usb_id(
            list.functions.items[0U].vid, list.functions.items[0U].pid);
    if (profile == nullptr || !profile->serial_identity_supported) return false;
    for (std::size_t index = 1U; index < list.functions.count; ++index) {
        const px4::userland::DeviceProfile* other =
            px4::userland::device_profile_for_usb_id(
                list.functions.items[index].vid, list.functions.items[index].pid);
        if (other == nullptr || other->model != profile->model) return false;
    }
    const auto& serial = list.enclosure.value.identity.serial;
    return observed.serial_state == wire::IdentitySerialState::present && serial.present &&
           serial.value.length == expected.serial.length &&
           std::memcmp(serial.value.bytes.data(), expected.serial.bytes.data(),
                       expected.serial.length) == 0;
}

const dtv::userland::host_ctl::Profile& profile() noexcept
{
    static const dtv::userland::host_ctl::Profile value{
        "px4ctl", "px4-userland", PX4_USERLAND_PRODUCT_VERSION,
        identity::Product::px4, true, true, nullptr, &matches_px4_serial};
    return value;
}

}  // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    const std::vector<std::string> owned_argv =
        px4::userland::cli::windows_argv_utf8(argc, argv);
    std::vector<const char*> argv_views;
    argv_views.reserve(owned_argv.size());
    for (const std::string& value : owned_argv) argv_views.push_back(value.c_str());
    return dtv::userland::host_ctl::run_control_cli(
        static_cast<int>(argv_views.size()), argv_views.data(), profile());
#else
    return dtv::userland::host_ctl::run_control_cli(
        argc, const_cast<const char* const*>(argv), profile());
#endif
}

// SPDX-License-Identifier: GPL-2.0-only
#include "px4/identity.h"
#include "px4/libusb_transport.h"

#include <array>
#include <cstdio>
#include <string_view>
#include <vector>

namespace {

using namespace px4::userland;

const char* speed_string(UsbSpeed speed) noexcept
{
    switch (speed) {
    case UsbSpeed::low:
        return "low";
    case UsbSpeed::full:
        return "full";
    case UsbSpeed::high:
        return "high";
    case UsbSpeed::super:
        return "super";
    case UsbSpeed::super_plus:
        return "super-plus";
    case UsbSpeed::super_plus_x2:
        return "super-plus-x2";
    case UsbSpeed::unknown:
        return "unknown";
    }
    return "unknown";
}

const char* topology_string(const DeviceObservation& observation) noexcept
{
    return q3u4_topology_is_usable(observation.topology) ? "usable" : "invalid";
}

void print_member(const std::optional<DeviceObservation>& member, std::uint8_t dev_id) noexcept
{
    if (!member.has_value()) {
        std::printf(" dev%u=missing", static_cast<unsigned int>(dev_id));
        return;
    }
    std::printf(" dev%u=%s speed=%s topology=%s", static_cast<unsigned int>(dev_id),
                member->serial.c_str(), speed_string(member->speed), topology_string(*member));
}

void print_usage() noexcept
{
    std::printf("usage: px4-usb-probe [--claim] [--base SERIAL]\n");
    std::printf("default: read-only native enumeration and grouping\n");
    std::printf("--claim: open and claim interface 0 for the selected ready enclosure\n");
    std::printf("--base SERIAL: select a 14-digit enclosure base serial\n");
}

}  // namespace

int main(int argc, char** argv)
{
    bool claim = false;
    std::string_view base_serial;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            if (argc != 2) {
                std::fprintf(stderr, "--help cannot be combined with other arguments\n");
                return 2;
            }
            print_usage();
            return 0;
        }
        if (argument == "--claim") {
            if (claim) {
                std::fprintf(stderr, "duplicate --claim\n");
                return 2;
            }
            claim = true;
            continue;
        }
        if (argument == "--base") {
            if (index + 1 >= argc || !base_serial.empty()) {
                std::fprintf(stderr, "--base requires exactly one serial\n");
                return 2;
            }
            base_serial = std::string_view(argv[++index]);
            continue;
        }
        std::fprintf(stderr, "unknown argument: %s\n", argv[index]);
        return 2;
    }

    const auto grouping = Q3U4Runtime::enumerate_native();
    if (!grouping) {
        std::fprintf(stderr, "enumeration failed: %s\n", error_string(grouping.error()));
        return 1;
    }
    for (const Q3U4Group& group : grouping.value().groups) {
        std::printf("group base=%s status=%s", group.base_serial.c_str(),
                    group_status_string(group.status));
        print_member(group.devices[0U], 1U);
        print_member(group.devices[1U], 2U);
        std::printf("\n");
    }
    for (const RejectedObservation& rejected : grouping.value().rejected) {
        std::printf("rejected status=%s vid=%04x pid=%04x\n",
                    observation_status_string(rejected.status), rejected.observation.vendor_id,
                    rejected.observation.product_id);
    }
    if (!claim) {
        return 0;
    }
    const auto runtime = Q3U4Runtime::open_native(base_serial);
    if (!runtime) {
        std::fprintf(stderr, "claim failed: %s\n", error_string(runtime.error()));
        return 1;
    }
    std::printf("claimed base=%s dev1=%s dev2=%s\n",
                std::string(runtime.value()->base_serial()).c_str(),
                runtime.value()->dev1().stream_active() ? "active" : "ready",
                runtime.value()->dev2().stream_active() ? "active" : "ready");
    return 0;
}

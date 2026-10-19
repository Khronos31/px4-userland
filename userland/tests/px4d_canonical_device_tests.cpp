// SPDX-License-Identifier: GPL-2.0-only
#include "px4/identity.h"
#include "px4d_args.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace px4::userland;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::fprintf(stderr, "canonical device check failed at %s:%d: %s\n", \
                         __FILE__, __LINE__, #condition);                       \
            return false;                                                       \
        }                                                                       \
    } while (false)

Px4dArguments parse(std::vector<std::string> values)
{
    std::vector<const char*> pointers;
    pointers.reserve(values.size());
    for (const std::string& value : values) pointers.push_back(value.c_str());
    return parse_px4d_arguments(static_cast<int>(pointers.size()), pointers.data());
}

DeviceObservation q3_observation(const char* base, std::uint8_t device,
                                 std::uint8_t port)
{
    DeviceObservation result;
    result.vendor_id = kQ3U4VendorId;
    result.product_id = kQ3U4ProductId;
    result.serial = std::string(base) + static_cast<char>('0' + device);
    result.speed = UsbSpeed::high;
    result.location.has_bus = true;
    result.location.has_address = true;
    result.location.bus = 1U;
    result.location.address = static_cast<std::uint8_t>(10U + port);
    result.location.port_path[0U] = port;
    result.location.port_count = 1U;
    UsbInterfaceObservation interface;
    interface.number = 0U;
    interface.alternate_setting = 0U;
    interface.endpoints = {
        {0x81U, EndpointType::bulk, 512U},
        {0x02U, EndpointType::bulk, 512U},
        {0x84U, EndpointType::bulk, 512U},
        {0x85U, EndpointType::bulk, 512U},
    };
    result.topology.interfaces.push_back(std::move(interface));
    return result;
}

bool test_canonical_parser()
{
    const auto version = parse({"px4d", "--version"});
    CHECK(version.valid && version.version && !version.help && !version.list_json);
    CHECK(!parse({"px4d", "--version", "--list-json"}).valid);
    const auto serial = parse({"px4d", "--device", "serial:00000000000010",
                               "--firmware", "firmware.bin"});
    CHECK(serial.valid && serial.device == "00000000000010");
    const auto short_serial = parse({"px4d", "-d", "serial:00000000000010",
                                     "--firmware", "firmware.bin"});
    CHECK(short_serial.valid && short_serial.device == serial.device);
    CHECK(!parse({"px4d", "-d", "serial:00000000000010", "--device",
                  "serial:00000000000020", "--firmware", "firmware.bin"}).valid);
    CHECK(!parse({"px4d", "--device", "00000000000010", "--firmware",
                  "firmware.bin"}).valid);
    CHECK(!parse({"px4d", "--usb-path", "1-2", "--firmware",
                  "firmware.bin"}).valid);

    const auto topology = parse({"px4d", "--device", "usb:1-3,1-2", "--instance",
                                 "receiver", "--firmware", "firmware.bin"});
    CHECK(topology.valid && topology.device.empty());
    CHECK(topology.usb_path_count == 2U && topology.usb_paths[0U] == "1-2" &&
          topology.usb_paths[1U] == "1-3");
    CHECK(!parse({"px4d", "--device", "usb:1-2,1-3,1-4", "--instance",
                  "receiver", "--firmware", "firmware.bin"}).valid);
    const auto fd_serial = parse({"px4d", "--fd", "10", "--device",
                                  "serial:00000000000010", "--firmware",
                                  "firmware.bin"});
    CHECK(fd_serial.valid && fd_serial.device == "00000000000010" &&
          px4d_open_mode(fd_serial) == Px4dOpenMode::file_descriptors);
    CHECK(!parse({"px4d", "--fd", "10", "--device", "usb:1-2", "--instance",
                  "receiver", "--firmware", "firmware.bin"}).valid);
    return true;
}

bool test_enclosure_selection()
{
    const std::vector<DeviceObservation> ready_observations{
        q3_observation("00000000000010", 1U, 2U),
        q3_observation("00000000000010", 2U, 4U),
    };
    const auto ready = group_q3u4_devices(ready_observations);
    CHECK(ready && ready.value().groups.size() == 1U);
    const auto one_path = select_q3u4_group_by_usb_paths(
        ready.value(), {}, {"1-2"});
    CHECK(one_path && one_path.value().candidate_indices[0U] == 0U &&
          one_path.value().candidate_indices[1U] == 1U);

    const std::vector<DeviceObservation> duplicated_observations{
        q3_observation("00000000000010", 1U, 2U),
        q3_observation("00000000000010", 2U, 4U),
        q3_observation("00000000000010", 1U, 6U),
        q3_observation("00000000000010", 2U, 8U),
    };
    const auto duplicated = group_q3u4_devices(duplicated_observations);
    CHECK(duplicated && duplicated.value().groups.size() == 1U &&
          duplicated.value().groups[0U].status == GroupStatus::duplicate);
    CHECK(select_ready_q3u4_group(duplicated.value(), "00000000000010").error() ==
          Error::INVALID_ARGUMENT);
    CHECK(!select_q3u4_group_by_usb_paths(duplicated.value(), "00000000000010",
                                           {"1-2"}));

    const std::vector<DeviceObservation> separate_observations{
        q3_observation("00000000000010", 1U, 2U),
        q3_observation("00000000000010", 2U, 4U),
        q3_observation("00000000000020", 1U, 6U),
        q3_observation("00000000000020", 2U, 8U),
    };
    const auto separate = group_q3u4_devices(separate_observations);
    CHECK(separate && separate.value().groups.size() == 2U);
    CHECK(select_q3u4_group_by_usb_paths(separate.value(), {}, {"1-2", "1-4"}));
    CHECK(!select_q3u4_group_by_usb_paths(separate.value(), {}, {"1-2", "1-8"}));
    return true;
}

}  // namespace

int main()
{
    return test_canonical_parser() && test_enclosure_selection() ? 0 : 1;
}

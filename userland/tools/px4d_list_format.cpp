// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_list_format.h"

#include "px4ctl_format.h"
#include "px4/ipc.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace px4::userland::tools {
namespace {

void append_usb_id(std::string& output, std::uint16_t vendor_id, std::uint16_t product_id)
{
    char text[16]{};
    const int length = std::snprintf(text, sizeof(text), "%04x:%04x",
                                     static_cast<unsigned int>(vendor_id),
                                     static_cast<unsigned int>(product_id));
    if (length > 0) output.append(text, static_cast<std::size_t>(length));
}

// A rejected device's serial descriptor is untrusted.  Keep each record on one
// line of space-separated key=value fields.
void append_field_value(std::string& output, const std::string& value)
{
    for (const char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        output.push_back(byte > 0x20U && byte < 0x7fU ? character : '?');
    }
}

} // namespace

std::string format_device_list(const GroupingResult& grouping)
{
    std::string output;
    char line[64]{};
    for (const Q3U4Group& group : grouping.groups) {
        const DeviceProfile& profile = device_profile(group.model);
        output += "serial=";
        append_field_value(output, group.base_serial);
        output += " model=";
        output += profile.name;
        output += " usb=";
        append_usb_id(output, kQ3U4VendorId, profile.product_id);
        output += " status=";
        output += group_status_string(group.status);
        const int length = std::snprintf(line, sizeof(line), " receivers=%u\n",
                                         static_cast<unsigned int>(profile.receiver_count));
        if (length > 0) output.append(line, static_cast<std::size_t>(length));
        const auto records = ipc::receiver_records(profile.receiver_count);
        if (records) {
            output += format_receiver_records(records.value().data(), profile.receiver_count);
        }
    }
    for (const RejectedObservation& rejected : grouping.rejected) {
        const DeviceObservation& observation = rejected.observation;
        const DeviceProfile* profile =
            device_profile_for_usb_id(observation.vendor_id, observation.product_id);
        if (profile == nullptr) continue;
        output += "rejected serial=";
        append_field_value(output, observation.serial);
        output += " model=";
        output += profile->name;
        output += " usb=";
        append_usb_id(output, observation.vendor_id, observation.product_id);
        output += " status=";
        output += observation_status_string(rejected.status);
        output += "\n";
    }
    return output;
}

} // namespace px4::userland::tools

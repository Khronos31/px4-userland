// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_list_format.h"

#include "px4/ipc.h"
#include "px4d_receiver_records.h"

#include <dtv/userland/discovery_json.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace px4::userland::tools {
namespace {

void append_usb_id(std::string& out, std::uint16_t vid, std::uint16_t pid)
{
    char buffer[16]{};
    const int count = std::snprintf(buffer, sizeof(buffer), "%04x:%04x",
                                    static_cast<unsigned int>(vid),
                                    static_cast<unsigned int>(pid));
    if (count > 0) out.append(buffer, static_cast<std::size_t>(count));
}

void append_number(std::string& out, unsigned int number)
{
    out += std::to_string(number);
}

const char* boolean(bool value) noexcept { return value ? "true" : "false"; }

// Serial descriptors are untrusted. Keep text records on one line.
void append_text_value(std::string& out, std::string_view value)
{
    for (char character : value) {
        const auto byte = static_cast<unsigned char>(character);
        out.push_back(byte > 0x20U && byte < 0x7fU ? character : '?');
    }
}

std::string port_string(const UsbLocation& location)
{
    if (!location.has_bus || location.port_count == 0U) return {};
    std::string value = std::to_string(location.bus);
    for (std::size_t i = 0U; i < location.port_count && i < location.port_path.size(); ++i) {
        value.push_back(i == 0U ? '-' : '.');
        append_number(value, location.port_path[i]);
    }
    return value;
}

void append_text_location(std::string& out, std::string_view prefix,
                          const UsbLocation& location)
{
    if (location.has_bus) {
        out.push_back(' ');
        out.append(prefix.data(), prefix.size());
        out += "bus=";
        append_number(out, location.bus);
    }
    if (location.has_address) {
        out.push_back(' ');
        out.append(prefix.data(), prefix.size());
        out += "address=";
        append_number(out, location.address);
    }
    const std::string port = port_string(location);
    if (!port.empty()) {
        out.push_back(' ');
        out.append(prefix.data(), prefix.size());
        out += "port=";
        out += port;
    }
}

bool serial_unique(const GroupingResult& grouping, const Q3U4Group& group) noexcept
{
    if (group.status != GroupStatus::ready) return false;
    std::size_t matching_count = 0U;
    for (const Q3U4Group& other : grouping.groups) {
        if (other.base_serial == group.base_serial) ++matching_count;
    }
    return matching_count == 1U;
}

const char* system_name(ipc::System system) noexcept
{
    switch (system) {
    case ipc::System::ISDB_T: return "ISDB-T";
    case ipc::System::ISDB_S: return "ISDB-S";
    case ipc::System::ISDB_T_OR_S: return "ISDB-T/S";
    }
    return "";
}

bool lnb_supported(const DeviceProfile& profile, ipc::System system) noexcept
{
    return profile.supports_lnb_15v && system != ipc::System::ISDB_T;
}

void append_receiver_text(std::string& out, const ipc::ReceiverRecord& receiver,
                          const DeviceProfile& profile)
{
    out += "receiver=";
    append_number(out, receiver.global_id);
    out += " device=";
    append_number(out, receiver.dev_id);
    out += " local=";
    append_number(out, receiver.local_id);
    out += " system=";
    out += system_name(receiver.system);
    out += " lnb_15v_supported=";
    out += boolean(lnb_supported(profile, receiver.system));
    out.push_back('\n');
}

namespace wire = dtv::userland::wire;

template <std::size_t N>
bool set_text(wire::WString<N>& target, std::string_view value) noexcept
{
    if (value.size() > N || value.find('\0') != std::string_view::npos ||
        !dtv::userland::detail::strict_utf8(
            reinterpret_cast<const std::uint8_t*>(value.data()), value.size())) return false;
    target.length = static_cast<std::uint16_t>(value.size());
    std::copy(value.begin(), value.end(), target.bytes.begin());
    return true;
}

template <std::size_t N>
bool set_optional_text(wire::Optional<wire::WString<N>>& target,
                        std::string_view value) noexcept
{
    target.present = !value.empty() && set_text(target.value, value);
    return target.present;
}

wire::DiscoveryUsbLocation discovery_location(const UsbLocation& location)
{
    wire::DiscoveryUsbLocation result;
    result.bus = {location.has_bus, location.bus};
    result.address = {location.has_address, location.address};
    (void)set_optional_text(result.port, port_string(location));
    return result;
}

struct DiscoveryInventory final {
    std::array<wire::DiscoveryDevice, 2U> devices{};
    std::array<std::array<wire::DiscoveryReceiver, 8U>, 2U> receivers{};
    std::array<wire::DiscoveryReader, 1U> readers{};
    std::vector<wire::DiscoveryCandidate> candidates;
};

} // namespace

std::string format_device_list(const GroupingResult& grouping)
{
    std::string out;
    for (const Q3U4Group& group : grouping.groups) {
        const DeviceProfile& profile = device_profile(group.model);
        out += "serial=";
        append_text_value(out, group.base_serial);
        out += " model=";
        out += profile.name;
        out += " usb=";
        append_usb_id(out, kQ3U4VendorId, profile.product_id);
        out += " status=";
        out += group_status_string(group.status);
        out += " receivers=";
        append_number(out, profile.receiver_count);
        out += " serial_unique=";
        out += boolean(serial_unique(grouping, group));
        if (group.status == GroupStatus::duplicate) {
            for (std::size_t i = 0U; i < group.candidates.size(); ++i) {
                const Q3U4Group::Candidate& candidate = group.candidates[i];
                const std::string prefix = "candidate" + std::to_string(i + 1U) + "_";
                out.push_back(' ');
                out += prefix;
                out += "device=";
                append_number(out, candidate.device);
                append_text_location(out, prefix, candidate.observation.location);
            }
        } else {
            for (std::size_t i = 0U; i < profile.bridge_count; ++i) {
                if (!group.devices[i]) continue;
                const std::string prefix = "dev" + std::to_string(i + 1U) + "_";
                append_text_location(out, prefix, group.devices[i]->location);
            }
        }
        out.push_back('\n');
        const auto records = px4_product::receiver_records(profile.receiver_count,
                                                            profile.dual_system);
        if (records) {
            for (std::size_t i = 0U; i < profile.receiver_count; ++i) {
                append_receiver_text(out, records.value()[i], profile);
            }
        }
    }
    for (const RejectedObservation& rejected : grouping.rejected) {
        const DeviceObservation& observation = rejected.observation;
        const DeviceProfile* profile =
            device_profile_for_usb_id(observation.vendor_id, observation.product_id);
        if (profile == nullptr) continue;
        out += "rejected serial=";
        append_text_value(out, observation.serial);
        out += " model=";
        out += profile->name;
        out += " usb=";
        append_usb_id(out, observation.vendor_id, observation.product_id);
        out += " status=";
        out += observation_status_string(rejected.status);
        append_text_location(out, "", observation.location);
        out.push_back('\n');
    }
    return out;
}

std::string format_model_list()
{
    std::string out;
    for (std::size_t i = 0U; const DeviceProfile* profile = device_profile_at(i); ++i) {
        out += profile->name;
        out.push_back(' ');
        append_usb_id(out, kQ3U4VendorId, profile->product_id);
        out.push_back('\n');
    }
    return out;
}

Result<std::string> format_device_list_json(const GroupingResult& grouping,
                                           std::string_view version)
{
    wire::DiscoveryScan scan;
    scan.complete = grouping.complete;
    if (!set_text(scan.product, "px4-userland") || !set_text(scan.version, version))
        return Result<std::string>::failure(Error::INVALID_ARGUMENT);
    std::vector<wire::DiscoveryEnclosure> enclosures(grouping.groups.size());
    std::vector<DiscoveryInventory> inventories(grouping.groups.size());
    std::vector<wire::DiscoveryUsbObservation> ungrouped;
    std::vector<wire::DiscoveryScanIssue> issues;
    auto issue = [&scan, &issues](const char* code, const char* detail) {
        scan.complete = false;
        wire::DiscoveryScanIssue value;
        (void)set_text(value.code, code);
        (void)set_text(value.detail, detail);
        issues.push_back(value);
    };
    if (!grouping.complete)
        issue("usb_descriptor_unavailable", "Some USB device descriptors could not be read");
    for (std::size_t index = 0U; index < grouping.groups.size(); ++index) {
        if (index > UINT16_MAX) return Result<std::string>::failure(Error::RESOURCE_LIMIT);
        const Q3U4Group& group = grouping.groups[index];
        const DeviceProfile& profile = device_profile(group.model);
        auto& enclosure = enclosures[index];
        auto& inventory = inventories[index];
        enclosure.enclosure = static_cast<std::uint16_t>(index);
        enclosure.status = static_cast<wire::DiscoveryEnclosureStatus>(group.status);
        (void)set_optional_text(enclosure.model, profile.name);
        if (!set_optional_text(enclosure.serial, group.base_serial))
            issue("serial_unavailable", "An enclosure serial could not be represented");
        enclosure.lnb_15v_supported = {true, profile.supports_lnb_15v};
        if (group.status == GroupStatus::duplicate) {
            for (const Q3U4Group::Candidate& candidate : group.candidates) {
                wire::DiscoveryCandidate value;
                value.device = candidate.device;
                if (!set_optional_text(value.serial, candidate.observation.serial))
                    issue("serial_unavailable", "A USB candidate serial could not be represented");
                value.location = discovery_location(candidate.observation.location);
                inventory.candidates.push_back(value);
            }
            enclosure.candidate_count = inventory.candidates.size();
            enclosure.candidates = inventory.candidates.data();
            continue;
        }
        const auto records = px4_product::receiver_records(profile.receiver_count,
                                                            profile.dual_system);
        if (!records) return Result<std::string>::failure(records.error());
        for (std::size_t slot = 0U; slot < profile.bridge_count; ++slot) {
            if (!group.devices[slot]) continue;
            const DeviceObservation& observation = *group.devices[slot];
            auto& device = inventory.devices[enclosure.device_count++];
            device.device = static_cast<std::uint16_t>(slot + 1U);
            device.vid = observation.vendor_id;
            device.pid = observation.product_id;
            if (!set_optional_text(device.serial, observation.serial))
                issue("serial_unavailable", "A USB device serial could not be represented");
            device.location = discovery_location(observation.location);
            device.receivers_known = true;
            for (std::size_t receiver = 0U; receiver < profile.receiver_count; ++receiver) {
                const auto& record = records.value()[receiver];
                if (record.dev_id != device.device) continue;
                auto& value = inventory.receivers[slot][device.receiver_count++];
                value.receiver = record.global_id;
                value.device = record.dev_id;
                value.local = record.local_id;
                value.systems.mask = record.system == ipc::System::ISDB_T ? 1U :
                                     record.system == ipc::System::ISDB_S ? 2U : 3U;
            }
            device.receivers = inventory.receivers[slot].data();
            // The existing runtime exposes its shared reader on bridge 1.
            if (slot == 0U) {
                inventory.readers[0U] = {0U, device.device};
                enclosure.readers_known = true;
                enclosure.reader_count = 1U;
                enclosure.readers = inventory.readers.data();
            }
        }
        enclosure.devices = inventory.devices.data();
    }
    for (const RejectedObservation& rejected : grouping.rejected) {
        const DeviceObservation& observation = rejected.observation;
        const DeviceProfile* profile =
            device_profile_for_usb_id(observation.vendor_id, observation.product_id);
        if (profile == nullptr) continue;
        wire::DiscoveryUsbObservation value;
        value.vid = observation.vendor_id;
        value.pid = observation.product_id;
        value.status = static_cast<wire::DiscoveryObservationStatus>(rejected.status);
        (void)set_optional_text(value.model, profile->name);
        if (rejected.status != ObservationStatus::open_failed)
            (void)set_optional_text(value.serial, observation.serial);
        value.location = discovery_location(observation.location);
        ungrouped.push_back(value);
        issue(observation_status_string(rejected.status),
              "A supported USB device could not be assigned to an enclosure");
    }
    scan.issue_count = issues.size();
    scan.issues = issues.data();
    scan.enclosure_count = enclosures.size();
    scan.enclosures = enclosures.data();
    scan.ungrouped_usb_device_count = ungrouped.size();
    scan.ungrouped_usb_devices = ungrouped.data();
    const auto size = wire::discovery_json_size(scan);
    if (!size) return Result<std::string>::failure(size.error());
    std::string output(size.value(), '\0');
    const auto encoded = wire::encode_discovery_json(scan,
        {reinterpret_cast<std::uint8_t*>(output.data()), output.size()});
    if (!encoded) return Result<std::string>::failure(encoded.error());
    return Result<std::string>::success(std::move(output));
}

} // namespace px4::userland::tools

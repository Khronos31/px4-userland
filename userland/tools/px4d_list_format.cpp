// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_list_format.h"

#include "px4/ipc.h"

#include <dtv/userland/json.h>
#include <dtv/userland/identity.h>

#include <array>
#include <limits>
#include <random>
#include <vector>
#include <algorithm>
#include <cstring>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

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

void append_json_string(std::string& out, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (std::size_t i = 0U; i < value.size();) {
        const char character = value[i];
        const auto byte = static_cast<unsigned char>(character);
        if (character == '"' || character == '\\') {
            out.push_back('\\');
            out.push_back(character);
            ++i;
        } else if (byte < 0x20U) {
            out += "\\u00";
            out.push_back(hex[byte >> 4U]);
            out.push_back(hex[byte & 0x0fU]);
            ++i;
        } else if (byte < 0x80U) {
            out.push_back(character);
            ++i;
        } else {
            // libusb normally supplies UTF-8, but an invalid descriptor must
            // not turn the entire JSON document into invalid UTF-8.
            std::size_t width = 0U;
            if (byte >= 0xc2U && byte <= 0xdfU) width = 2U;
            else if (byte >= 0xe0U && byte <= 0xefU) width = 3U;
            else if (byte >= 0xf0U && byte <= 0xf4U) width = 4U;
            bool valid = width != 0U && i + width <= value.size();
            if (valid) {
                for (std::size_t j = 1U; j < width; ++j) {
                    const auto next = static_cast<unsigned char>(value[i + j]);
                    if (next < 0x80U || next > 0xbfU) valid = false;
                }
                const auto next = static_cast<unsigned char>(value[i + 1U]);
                if (byte == 0xe0U && next < 0xa0U) valid = false;
                if (byte == 0xedU && next > 0x9fU) valid = false;
                if (byte == 0xf0U && next < 0x90U) valid = false;
                if (byte == 0xf4U && next > 0x8fU) valid = false;
            }
            if (valid) {
                out.append(value.data() + i, width);
                i += width;
            } else {
                out += "\\ufffd";
                ++i;
            }
        }
    }
    out.push_back('"');
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

void append_json_location(std::string& out, const UsbLocation& location)
{
    out += ",\"bus\":";
    if (location.has_bus) append_number(out, location.bus);
    else out += "null";
    out += ",\"address\":";
    if (location.has_address) append_number(out, location.address);
    else out += "null";
    out += ",\"port\":";
    const std::string port = port_string(location);
    if (port.empty()) out += "null";
    else append_json_string(out, port);
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

void append_receiver_json(std::string& out, const ipc::ReceiverRecord& receiver,
                          const DeviceProfile& profile)
{
    out += "{\"receiver\":";
    append_number(out, receiver.global_id);
    out += ",\"device\":";
    append_number(out, receiver.dev_id);
    out += ",\"local\":";
    append_number(out, receiver.local_id);
    out += ",\"system\":";
    append_json_string(out, system_name(receiver.system));
    out += ",\"lnb_15v_supported\":";
    out += boolean(lnb_supported(profile, receiver.system));
    out.push_back('}');
}

void append_json_observation(std::string& out, unsigned int device,
                             const DeviceObservation& observation)
{
    out += "{\"device\":";
    append_number(out, device);
    out += ",\"serial\":";
    append_json_string(out, observation.serial);
    append_json_location(out, observation.location);
    out.push_back('}');
}

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
        const auto records = ipc::receiver_records(profile.receiver_count, profile.dual_system);
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

std::string format_device_list_json(const GroupingResult& grouping)
{
    std::string out{"{\"enclosures\":["};
    bool first_group = true;
    for (const Q3U4Group& group : grouping.groups) {
        if (!first_group) out.push_back(',');
        first_group = false;
        const DeviceProfile& profile = device_profile(group.model);
        out += "{\"serial\":";
        append_json_string(out, group.base_serial);
        out += ",\"model\":";
        append_json_string(out, profile.name);
        out += ",\"usb\":";
        std::string usb;
        append_usb_id(usb, kQ3U4VendorId, profile.product_id);
        append_json_string(out, usb);
        out += ",\"status\":";
        append_json_string(out, group_status_string(group.status));
        out += ",\"serial_unique\":";
        out += boolean(serial_unique(grouping, group));
        out += ",\"devices\":[";
        bool first_device = true;
        if (group.status != GroupStatus::duplicate) {
            for (std::size_t i = 0U; i < profile.bridge_count; ++i) {
                if (!group.devices[i]) continue;
                if (!first_device) out.push_back(',');
                first_device = false;
                append_json_observation(out, static_cast<unsigned int>(i + 1U), *group.devices[i]);
            }
        }
        out += "],\"candidates\":[";
        if (group.status == GroupStatus::duplicate) {
            for (std::size_t i = 0U; i < group.candidates.size(); ++i) {
                if (i != 0U) out.push_back(',');
                append_json_observation(out, group.candidates[i].device,
                                        group.candidates[i].observation);
            }
        }
        out += "],\"receivers\":[";
        const auto records = ipc::receiver_records(profile.receiver_count, profile.dual_system);
        if (records) {
            for (std::size_t i = 0U; i < profile.receiver_count; ++i) {
                if (i != 0U) out.push_back(',');
                append_receiver_json(out, records.value()[i], profile);
            }
        }
        out += "]}";
    }
    out += "],\"ungrouped_usb_devices\":[";
    bool first_rejected = true;
    for (const RejectedObservation& rejected : grouping.rejected) {
        const DeviceObservation& observation = rejected.observation;
        const DeviceProfile* profile =
            device_profile_for_usb_id(observation.vendor_id, observation.product_id);
        if (profile == nullptr) continue;
        if (!first_rejected) out.push_back(',');
        first_rejected = false;
        out += "{\"serial\":";
        if (observation.serial.empty() || rejected.status == ObservationStatus::open_failed)
            out += "null";
        else append_json_string(out, observation.serial);
        out += ",\"model\":";
        append_json_string(out, profile->name);
        out += ",\"usb\":";
        std::string usb;
        append_usb_id(usb, observation.vendor_id, observation.product_id);
        append_json_string(out, usb);
        out += ",\"status\":";
        append_json_string(out, observation_status_string(rejected.status));
        append_json_location(out, observation.location);
        out.push_back('}');
    }
    out += "]}\n";
    return out;
}



namespace {
// Only the actual static profile and USB observations populate the shared
// discovery document; no firmware, card or tuner operation is performed.
namespace wire = dtv::userland::wire;
template <std::size_t N>
bool wire_text(wire::WString<N>& dest, std::string_view text)
{
    if (text.empty() || text.size() > dest.bytes.size()) return false;
    dest.length = static_cast<std::uint16_t>(text.size());
    std::memcpy(dest.bytes.data(), text.data(), text.size());
    return true;
}
bool ascii_serial(std::string_view text)
{
    if (text.empty()) return false;
    for (const unsigned char ch : text)
        if (ch < 0x21U || ch > 0x7eU) return false;
    return true;
}
bool add_candidate(wire::BArray<wire::DiscoverySelectorCandidate, wire::kMaxFunctions>& out,
                   const std::string& value, wire::DiscoveryCandidateState state)
{
    if (!dtv::userland::identity::parse_device_selector(value) ||
        out.count == out.items.size()) return false;
    auto& candidate = out.items[out.count++];
    return wire_text(candidate.value, value) &&
           ((candidate.state = state), true);
}
std::uint8_t wire_system(ipc::System system)
{
    switch (system) {
    case ipc::System::ISDB_T: return 1U;
    case ipc::System::ISDB_S: return 2U;
    case ipc::System::ISDB_T_OR_S: return 3U;
    }
    return 0U;
}
bool add_usb_function(std::vector<wire::DiscoveryFunction>& output,
                      const DeviceObservation& source, ObservationStatus state,
                      wire::DiscoveryGroupingState grouping,
                      std::uint16_t enclosure_id)
{
    const DeviceProfile* profile =
        device_profile_for_usb_id(source.vendor_id, source.product_id);
    if (profile == nullptr ||
        output.size() >= std::numeric_limits<std::uint16_t>::max()) return false;
    wire::DiscoveryFunction f{};
    auto& observed = f.observation;
    observed.id = static_cast<std::uint16_t>(output.size());
    observed.vid = source.vendor_id;
    observed.pid = source.product_id;
    observed.mode = state == ObservationStatus::open_failed
        ? wire::Mode::unknown : wire::Mode::runtime;
    observed.model.present = true;
    if (!wire_text(observed.model.value.id, profile->name) ||
        !wire_text(observed.model.value.name, profile->name)) return false;
    observed.identification_reason = wire::IdentificationReason::none;
    // A failed descriptor read is unknown, not an absent device.
    observed.present = state != ObservationStatus::open_failed;
    if (source.location.has_bus) {
        observed.location.bus.present = true;
        observed.location.bus.value = source.location.bus;
    }
    if (source.location.has_address) {
        observed.location.address.present = true;
        observed.location.address.value = source.location.address;
    }
    const std::string port = port_string(source.location);
    if (!port.empty() &&
        dtv::userland::identity::parse_device_selector("usb:" + port)) {
        observed.location.usb_path.present =
            wire_text(observed.location.usb_path.value, port);
    }
    if (state == ObservationStatus::invalid_serial) {
        observed.serial_state = wire::SerialState::invalid;
    } else if (state == ObservationStatus::open_failed) {
        observed.serial_state = wire::SerialState::unreadable;
    } else if (ascii_serial(source.serial)) {
        observed.serial_state = wire::SerialState::present;
        observed.serial.present = wire_text(observed.serial.value, source.serial);
    } else if (source.serial.empty()) {
        observed.serial_state = wire::SerialState::not_queried;
    } else {
        observed.serial_state = wire::SerialState::invalid;
    }
    f.grouping = grouping;
    if (grouping == wire::DiscoveryGroupingState::bound) {
        f.enclosure_id.present = true;
        f.enclosure_id.value = enclosure_id;
    } else {
        f.grouping_reason.present = wire_text(f.grouping_reason.value,
            grouping == wire::DiscoveryGroupingState::ambiguous
                ? "duplicate_or_ambiguous" : "not_bound");
    }
    if (observed.location.usb_path.present) {
        const std::string selector = "usb:" + port;
        const auto candidate_state =
            grouping == wire::DiscoveryGroupingState::bound
                ? wire::DiscoveryCandidateState::usable
                : grouping == wire::DiscoveryGroupingState::ambiguous
                    ? wire::DiscoveryCandidateState::ambiguous
                    : wire::DiscoveryCandidateState::requires_resolution;
        if (!add_candidate(f.selector_candidates, selector, candidate_state))
            return false;
    }
    output.push_back(std::move(f));
    return true;
}
} // namespace

std::string format_discovery_json(const GroupingResult& grouping, const char* version)
{
    if (version == nullptr) return {};
    wire::Producer producer{};
    if (!wire_text(producer.product, "px4-userland") ||
        !wire_text(producer.version, version)) return {};
    std::vector<wire::DiscoveryFunction> functions;
    std::vector<wire::DiscoveryEnclosure> enclosures;
    functions.reserve(grouping.rejected.size() + (grouping.groups.size() * 2U));
    enclosures.reserve(grouping.groups.size());

    for (const Q3U4Group& group : grouping.groups) {
        const DeviceProfile& profile = device_profile(group.model);
        const bool resolved = group.status == GroupStatus::ready ||
                              group.status == GroupStatus::incomplete;
        std::array<std::uint16_t, 2U> slot_ids{};
        slot_ids.fill(std::numeric_limits<std::uint16_t>::max());
        const auto enclosure_id = static_cast<std::uint16_t>(enclosures.size());
        if (enclosures.size() >= std::numeric_limits<std::uint16_t>::max()) return {};
        // Only functions validated by the existing grouping belong to an
        // enclosure. Additional candidates retain their unresolved identity.
        if (resolved) {
            for (std::size_t slot = 0U; slot < profile.bridge_count; ++slot) {
                if (!group.devices[slot]) continue;
                if (!add_usb_function(functions, *group.devices[slot],
                                      ObservationStatus::usable,
                                      wire::DiscoveryGroupingState::bound,
                                      enclosure_id)) return {};
                slot_ids[slot] = functions.back().observation.id;
            }
        }
        for (const auto& candidate : group.candidates) {
            bool already_bound = false;
            for (std::size_t slot = 0U; slot < profile.bridge_count; ++slot) {
                if (resolved && group.candidate_indices[slot].has_value() &&
                    *group.candidate_indices[slot] == candidate.observation_index &&
                    slot_ids[slot] != std::numeric_limits<std::uint16_t>::max())
                    already_bound = true;
            }
            if (already_bound) continue;
            if (!add_usb_function(functions, candidate.observation,
                                  ObservationStatus::usable,
                                  group.status == GroupStatus::duplicate
                                    ? wire::DiscoveryGroupingState::ambiguous
                                    : wire::DiscoveryGroupingState::unresolved,
                                  0U)) return {};
        }
        std::size_t present_count = 0U;
        for (std::size_t slot = 0U; slot < profile.bridge_count; ++slot)
            if (slot_ids[slot] != std::numeric_limits<std::uint16_t>::max())
                ++present_count;
        if (!resolved || present_count == 0U) continue;
        wire::DiscoveryEnclosure enclosure{};
        auto& descriptor = enclosure.descriptor;
        descriptor.id = enclosure_id;
        descriptor.model.present = true;
        if (!wire_text(descriptor.model.value.id, profile.name) ||
            !wire_text(descriptor.model.value.name, profile.name)) return {};
        if (ascii_serial(group.base_serial)) {
            descriptor.identity.serial_state = wire::IdentitySerialState::present;
            descriptor.identity.serial.present =
                wire_text(descriptor.identity.serial.value, group.base_serial);
            descriptor.identity.uniqueness =
                !profile.serial_identity_supported ? wire::Uniqueness::unknown :
                group.status == GroupStatus::ready
                    ? (serial_unique(grouping, group)
                        ? wire::Uniqueness::unique_in_scan
                        : wire::Uniqueness::duplicate)
                    : wire::Uniqueness::unknown;
        } else {
            descriptor.identity.serial_state = wire::IdentitySerialState::unknown;
            descriptor.identity.uniqueness = wire::Uniqueness::unknown;
        }
        enclosure.topology.state =
            group.status == GroupStatus::ready &&
            present_count == profile.bridge_count
                ? wire::TopologyState::complete : wire::TopologyState::partial;
        enclosure.topology.expected_function_count.present = true;
        enclosure.topology.expected_function_count.value = profile.bridge_count;
        for (std::size_t slot = 0U; slot < profile.bridge_count; ++slot) {
            if (slot_ids[slot] == std::numeric_limits<std::uint16_t>::max()) continue;
            enclosure.usb_function_ids.items[enclosure.usb_function_ids.count++] =
                slot_ids[slot];
            const auto& candidates = functions[slot_ids[slot]].selector_candidates;
            for (std::size_t i = 0U; i < candidates.count; ++i)
                enclosure.selector_candidates.items[enclosure.selector_candidates.count++] =
                    candidates.items[i];
        }
        // A serial selector must not be advertised for repeated hardware
        // serials, or for a partially observed enclosure.
        if (descriptor.identity.uniqueness == wire::Uniqueness::unique_in_scan) {
            (void)add_candidate(enclosure.selector_candidates,
                "serial:" + group.base_serial, wire::DiscoveryCandidateState::usable);
        }
        const auto records = ipc::receiver_records(profile.receiver_count,
                                                    profile.dual_system);
        if (!records) return {};
        enclosure.receivers.present = true;
        enclosure.receivers.value.count = profile.receiver_count;
        descriptor.limits.max_concurrent_streams.present = true;
        descriptor.limits.max_concurrent_streams.value = profile.receiver_count;
        for (std::size_t i = 0U; i < profile.receiver_count; ++i) {
            const auto& source = records.value()[i];
            auto& receiver = enclosure.receivers.value.items[i];
            receiver.id = source.global_id;
            receiver.systems.mask = wire_system(source.system);
            const std::size_t slot = static_cast<std::size_t>(source.dev_id - 1U);
            if (slot < profile.bridge_count &&
                slot_ids[slot] != std::numeric_limits<std::uint16_t>::max()) {
                receiver.binding.present = true;
                receiver.binding.value.function_id = slot_ids[slot];
                receiver.binding.value.local_id = source.local_id;
            }
            if (lnb_supported(profile, source.system)) {
                receiver.capabilities.controls |= wire::kControlBitLnb;
                receiver.capabilities.lnb.values.count = 2U;
                receiver.capabilities.lnb.values.items[0] = 0U;
                receiver.capabilities.lnb.values.items[1] = 15U;
            }
        }
        enclosure.readers.present = true;
        enclosure.readers.value.count = 1U;
        enclosure.readers.value.items[0].id = 0U;
        if (slot_ids[0] != std::numeric_limits<std::uint16_t>::max()) {
            auto& binding = enclosure.readers.value.items[0].binding;
            binding.present = true;
            binding.value.function_id = slot_ids[0];
            binding.value.local_id = 0U;
        }
        enclosures.push_back(std::move(enclosure));
    }
    for (const auto& rejected : grouping.rejected) {
        if (device_profile_for_usb_id(rejected.observation.vendor_id,
                                      rejected.observation.product_id) == nullptr) continue;
        if (!add_usb_function(functions, rejected.observation, rejected.status,
                              wire::DiscoveryGroupingState::unresolved, 0U)) return {};
    }

    wire::DiscoveryResponse document{};
    document.producer = producer;
    // Every invocation is a new read-only snapshot, not a stable USB identity.
    std::random_device source;
    for (auto& byte : document.scan_id)
        byte = static_cast<std::uint8_t>(source());
    document.usb_functions = {functions.data(), functions.size()};
    document.enclosures = {enclosures.data(), enclosures.size()};
    const auto size = wire::discovery_json_size(document);
    if (!size) return {};
    std::string output(size.value(), '\0');
    const auto written = wire::encode_discovery_json(document,
        {reinterpret_cast<std::uint8_t*>(output.data()), output.size()});
    if (!written || written.value() != output.size()) return {};
    return output;
}

std::string format_supported_models_text()
{
    namespace w = dtv::userland::wire;
    constexpr std::size_t count =
        static_cast<std::size_t>(DeviceModel::dtv02a_1t1s_u) + 1U;
    std::array<w::SupportedModel, count> supported{};
    for (std::size_t i = 0U; i < supported.size(); ++i) {
        const DeviceProfile& p = device_profile(static_cast<DeviceModel>(i));
        const std::size_t length = std::strlen(p.name);
        if (length == 0U || length > supported[i].name.bytes.size()) return {};
        supported[i].name.length = static_cast<std::uint16_t>(length);
        std::memcpy(supported[i].name.bytes.data(), p.name, length);
        supported[i].usb_ids.count = 1U;
        supported[i].usb_ids.items[0] = {kQ3U4VendorId, p.product_id};
    }
    const w::SupportedModels models{{supported.data(), supported.size()}};
    const auto size = w::models_text_size(models);
    if (!size) return {};
    std::string output(size.value(), '\0');
    const auto encoded = w::encode_models_text(models, {
        reinterpret_cast<std::uint8_t*>(output.data()), output.size()});
    return encoded && encoded.value() == output.size() ? output : std::string{};
}

} // namespace px4::userland::tools

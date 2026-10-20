// SPDX-License-Identifier: GPL-2.0-only
#include "px4/control_context.h"

#include <dtv/userland/identity.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <string>

namespace px4::userland::ipc::posix {
namespace {

namespace wire = dtv::userland::wire;

constexpr const char* kProductName = "px4-userland";

template <std::size_t N>
bool copy_text(wire::WString<N>& out, std::string_view text) noexcept
{
    if (text.size() > N) return false;
    out.length = static_cast<std::uint16_t>(text.size());
    for (std::size_t index = 0U; index < text.size(); ++index) out.bytes[index] = text[index];
    return true;
}

// Bus-port path in the form px4d --usb-path accepts (BUS-PORT.PORT...).
bool usb_path_text(const UsbLocation& location, wire::WString<64>& out) noexcept
{
    if (!location.has_bus || location.port_count == 0U ||
        location.port_count > location.port_path.size())
        return false;
    std::string value = std::to_string(location.bus);
    for (std::size_t index = 0U; index < location.port_count; ++index) {
        value.push_back(index == 0U ? '-' : '.');
        value += std::to_string(location.port_path[index]);
    }
    return copy_text(out, value);
}

// The record codec is the validator: a field it would refuse is reported as
// not representable instead of being shortened or sanitized.
bool encodable(const wire::UsbFunction& function) noexcept
{
    std::unique_ptr<std::array<std::uint8_t, wire::kMaxControlPayload>> scratch(
        new (std::nothrow) std::array<std::uint8_t, wire::kMaxControlPayload>());
    if (!scratch) return false;
    return static_cast<bool>(wire::encode_usb_function(
        function, MutableByteView{scratch->data(), scratch->size()}));
}

wire::Model profile_model(const DeviceProfile& profile) noexcept
{
    // The existing stable profile name is the model ID until a catalog fixes
    // separate IDs.
    wire::Model model;
    (void)copy_text(model.id, profile.name);
    (void)copy_text(model.name, profile.name);
    return model;
}

void fill_function(wire::UsbFunction& function, std::uint16_t id, const DeviceProfile& profile,
                   const DeviceObservation* observation) noexcept
{
    function = wire::UsbFunction{};
    function.id = id;
    function.vid = kQ3U4VendorId;
    function.pid = profile.product_id;
    function.mode = wire::Mode::runtime;
    function.model.present = true;
    function.model.value = profile_model(profile);
    function.identification_reason = wire::IdentificationReason::none;
    function.present = true;
    if (observation == nullptr) {
        function.serial_state = wire::SerialState::not_queried;
        return;
    }
    function.vid = observation->vendor_id;
    function.pid = observation->product_id;
    if (observation->location.has_bus) {
        function.location.bus.present = true;
        function.location.bus.value = observation->location.bus;
    }
    if (observation->location.has_address) {
        function.location.address.present = true;
        function.location.address.value = observation->location.address;
    }
    wire::WString<64> path;
    if (usb_path_text(observation->location, path)) {
        function.location.usb_path.present = true;
        function.location.usb_path.value = path;
    }
    if (observation->serial.empty()) {
        function.serial_state = wire::SerialState::absent;
        return;
    }
    function.serial_state = wire::SerialState::present;
    function.serial.present = copy_text(function.serial.value, observation->serial);
    if (!function.serial.present || !encodable(function)) {
        function.serial_state = wire::SerialState::invalid;
        function.serial.present = false;
    }
}

}  // namespace

Result<void> build_px4_control_context(const Px4ContextInput& input,
                                       const TunerService& tuner,
                                       ControlContext& out) noexcept
{
    const DeviceProfile& profile = device_profile(input.model);
    const std::uint8_t receivers = tuner.receiver_count();
    if (profile.bridge_count == 0U || profile.bridge_count > 2U || receivers == 0U ||
        receivers > kReceiverCount || receivers != profile.receiver_count ||
        input.base_serial.empty())
        return Result<void>::failure(Error::INVALID_ARGUMENT);

    out = ControlContext{};
    out.epoch = input.epoch;
    const auto canonical_instance = dtv::userland::identity::canonicalize_instance(
        input.instance);
    if (!canonical_instance) return Result<void>::failure(Error::INVALID_ARGUMENT);
    out.instance.present = copy_text(
        out.instance.value,
        std::string_view(canonical_instance.value().bytes.data(),
                         canonical_instance.value().length));
    if (!out.instance.present) return Result<void>::failure(Error::INVALID_ARGUMENT);

    wire::ListResponse& list = out.list;
    list.epoch = input.epoch;
    if (!copy_text(list.producer.product, kProductName) ||
        input.product_version.empty() ||
        !copy_text(list.producer.version, input.product_version))
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    list.topology.state = wire::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = profile.bridge_count;

    list.enclosure.present = true;
    wire::Enclosure& enclosure = list.enclosure.value;
    enclosure.id = 0U;
    enclosure.model.present = true;
    enclosure.model.value = profile_model(profile);
    enclosure.identity.serial_state = wire::IdentitySerialState::present;
    enclosure.identity.serial.present = copy_text(enclosure.identity.serial.value,
                                                  input.base_serial);
    enclosure.identity.uniqueness = !profile.serial_identity_supported ||
            !input.serial_unique_in_scan.has_value() ? wire::Uniqueness::unknown :
            *input.serial_unique_in_scan ? wire::Uniqueness::unique_in_scan :
                                           wire::Uniqueness::duplicate;
    if (!enclosure.identity.serial.present)
        return Result<void>::failure(Error::INVALID_ARGUMENT);

    list.functions.count = profile.bridge_count;
    for (std::uint16_t bridge = 0U; bridge < profile.bridge_count; ++bridge) {
        fill_function(list.functions.items[bridge], bridge, profile, input.bridges[bridge]);
    }

    list.receivers.present = true;
    list.receivers.value.count = receivers;
    out.execution.receiver_count = receivers;
    for (std::uint8_t receiver = 0U; receiver < receivers; ++receiver) {
        wire::ReceiverDescriptor& descriptor = list.receivers.value.items[receiver];
        descriptor = wire::ReceiverDescriptor{};
        descriptor.id = receiver;
        const bool terrestrial = tuner.receiver_supports(receiver, ipc::System::ISDB_T);
        const bool satellite = tuner.receiver_supports(receiver, ipc::System::ISDB_S);
        descriptor.systems.mask = static_cast<std::uint8_t>((terrestrial ? 0x01U : 0U) |
                                                            (satellite ? 0x02U : 0U));
        if (descriptor.systems.mask == 0U) return Result<void>::failure(Error::INVALID_ARGUMENT);
        // Paired bridges carry receivers 0..3 and 4..7; a single bridge carries
        // every receiver.
        descriptor.binding.present = true;
        descriptor.binding.value.function_id =
            profile.bridge_count == 2U ? static_cast<std::uint16_t>(receiver / 4U) : 0U;
        descriptor.binding.value.local_id =
            profile.bridge_count == 2U ? static_cast<std::uint16_t>(receiver % 4U) : receiver;
        descriptor.capabilities.ts_scope = wire::TsScope::multiplex;
        if (satellite) {
            const TunerStrictProfile strict = tuner.strict_profile(receiver, ipc::System::ISDB_S);
            wire::LnbValues& values = descriptor.capabilities.lnb;
            if ((strict.lnb_voltage_mask & 0x01U) != 0U)
                values.values.items[values.values.count++] = 0U;
            if ((strict.lnb_voltage_mask & 0x02U) != 0U)
                values.values.items[values.values.count++] = 15U;
            if (values.values.count != 0U)
                descriptor.capabilities.controls |= wire::kControlBitLnb;
        }
        out.receiver_index[receiver] = receiver;
        out.execution.receiver_lane[receiver] =
            profile.bridge_count == 2U && receiver >= 4U
                ? ControlWorkerLane::tuner_dev2 : ControlWorkerLane::tuner_dev1;
    }

    list.readers.present = true;
    list.readers.value.count = 1U;
    list.readers.value.items[0].id = 0U;
    list.readers.value.items[0].binding.present = true;
    list.readers.value.items[0].binding.value = wire::Binding{0U, 0U};

    if (wire::validate_list_response(list) != Error::OK)
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    return Result<void>::success();
}

}  // namespace px4::userland::ipc::posix

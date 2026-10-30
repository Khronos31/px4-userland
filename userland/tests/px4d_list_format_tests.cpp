// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_list_format.h"
#include "px4d_receiver_records.h"

#include <cstdio>
#include <initializer_list>
#include <string>

namespace {

using namespace px4::userland;

#define CHECK(condition)                                                                     \
    do {                                                                                     \
        if (!(condition)) {                                                                  \
            std::fprintf(stderr, "px4d list format CHECK failed at %s:%d: %s\n", __FILE__,  \
                         __LINE__, #condition);                                               \
            return false;                                                                    \
        }                                                                                    \
    } while (false)

Q3U4Group group(const char* base_serial, DeviceModel model, GroupStatus status)
{
    Q3U4Group result;
    result.base_serial = base_serial;
    result.model = model;
    result.status = status;
    return result;
}

RejectedObservation rejected(std::uint16_t product_id, const char* serial,
                             ObservationStatus status)
{
    RejectedObservation result;
    result.status = status;
    result.observation.vendor_id = kQ3U4VendorId;
    result.observation.product_id = product_id;
    result.observation.serial = serial;
    return result;
}

bool test_enclosures_carry_the_px4ctl_list_receiver_table()
{
    GroupingResult grouping;
    grouping.groups.push_back(group("00001205000960", DeviceModel::px_q3u4, GroupStatus::ready));
    grouping.groups.push_back(
        group("000020263901491", DeviceModel::dtv02a_5ts_p, GroupStatus::ready));

    // Receiver identity and system fields match `px4ctl list`; LNB capability is appended.
    const std::string expected =
        "serial=00001205000960 model=PX-Q3U4 usb=0511:084a status=ready receivers=8 serial_unique=true\n"
        "receiver=0 device=1 local=0 system=ISDB-S lnb_15v_supported=true\n"
        "receiver=1 device=1 local=1 system=ISDB-S lnb_15v_supported=true\n"
        "receiver=2 device=1 local=2 system=ISDB-T lnb_15v_supported=false\n"
        "receiver=3 device=1 local=3 system=ISDB-T lnb_15v_supported=false\n"
        "receiver=4 device=2 local=0 system=ISDB-S lnb_15v_supported=true\n"
        "receiver=5 device=2 local=1 system=ISDB-S lnb_15v_supported=true\n"
        "receiver=6 device=2 local=2 system=ISDB-T lnb_15v_supported=false\n"
        "receiver=7 device=2 local=3 system=ISDB-T lnb_15v_supported=false\n"
        "serial=000020263901491 model=DTV02A-5TS-P usb=0511:924e status=ready receivers=5 serial_unique=true\n"
        "receiver=0 device=1 local=0 system=ISDB-T/S lnb_15v_supported=true\n"
        "receiver=1 device=1 local=1 system=ISDB-T/S lnb_15v_supported=true\n"
        "receiver=2 device=1 local=2 system=ISDB-T/S lnb_15v_supported=true\n"
        "receiver=3 device=1 local=3 system=ISDB-T/S lnb_15v_supported=true\n"
        "receiver=4 device=1 local=4 system=ISDB-T/S lnb_15v_supported=true\n";
    CHECK(tools::format_device_list(grouping) == expected);
    return true;
}

bool test_px4_receiver_table_preserves_product_mapping()
{
    using namespace px4::userland;

    const auto single = px4_product::receiver_records(1U);
    CHECK(single && single.value()[0U].global_id == 0U &&
          single.value()[0U].dev_id == 1U && single.value()[0U].local_id == 0U &&
          single.value()[0U].system == ipc::System::ISDB_T);

    const auto single_dual = px4_product::receiver_records(1U, true);
    CHECK(single_dual && single_dual.value()[0U].system == ipc::System::ISDB_T_OR_S);
    const auto dual_four = px4_product::receiver_records(4U, true);
    CHECK(dual_four && dual_four.value()[3U].system == ipc::System::ISDB_T_OR_S);

    for (const std::uint8_t count : {3U, 5U}) {
        const auto records = px4_product::receiver_records(count);
        CHECK(records);
        for (std::uint8_t index = 0U; index < count; ++index) {
            CHECK(records.value()[index].global_id == index &&
                  records.value()[index].dev_id == 1U &&
                  records.value()[index].local_id == index &&
                  records.value()[index].system == ipc::System::ISDB_T_OR_S);
        }
    }

    const auto four = px4_product::receiver_records(4U);
    CHECK(four && four.value()[0U].system == ipc::System::ISDB_S &&
          four.value()[1U].system == ipc::System::ISDB_S &&
          four.value()[2U].system == ipc::System::ISDB_T &&
          four.value()[3U].system == ipc::System::ISDB_T);
    const auto eight = px4_product::receiver_records(8U);
    CHECK(eight);
    for (std::uint8_t index = 0U; index < 8U; ++index) {
        const auto local = static_cast<std::uint8_t>(index % 4U);
        CHECK(eight.value()[index].global_id == index &&
              eight.value()[index].dev_id == static_cast<std::uint8_t>(index / 4U) + 1U &&
              eight.value()[index].local_id == local &&
              eight.value()[index].system ==
                  (local < 2U ? ipc::System::ISDB_S : ipc::System::ISDB_T));
    }

    CHECK(!px4_product::receiver_records(0U) &&
          !px4_product::receiver_records(2U) &&
          !px4_product::receiver_records(6U) &&
          !px4_product::receiver_records(9U));
    return true;
}

bool test_incomplete_enclosure_keeps_its_status()
{
    GroupingResult grouping;
    grouping.groups.push_back(
        group("00001205000123", DeviceModel::px_q3u4, GroupStatus::incomplete));
    const std::string output = tools::format_device_list(grouping);
    CHECK(output.rfind(
              "serial=00001205000123 model=PX-Q3U4 usb=0511:084a status=incomplete receivers=8 serial_unique=false\n",
              0U) == 0U);
    return true;
}

bool test_rejected_devices_of_supported_models_only()
{
    GroupingResult grouping;
    grouping.rejected.push_back(
        rejected(kPxMlt5PeProductId, "", ObservationStatus::open_failed));
    // A serial descriptor is untrusted: it must not split or add a line.
    grouping.rejected.push_back(
        rejected(kQ3U4ProductId, "bad serial\nx", ObservationStatus::invalid_serial));
    // Other USB IDs are not this product's devices.
    grouping.rejected.push_back(
        rejected(0x084eU, "000012050009991", ObservationStatus::unsupported));

    const std::string expected =
        "rejected serial= model=PX-MLT5PE usb=0511:024e status=open_failed\n"
        "rejected serial=bad?serial?x model=PX-Q3U4 usb=0511:084a status=invalid_serial\n";
    CHECK(tools::format_device_list(grouping) == expected);
    return true;
}

bool test_nothing_connected_prints_nothing()
{
    CHECK(tools::format_device_list(GroupingResult{}).empty());
    const auto json = tools::format_device_list_json(GroupingResult{}, "fixture-version");
    CHECK(json);
    CHECK(json.value() ==
          "{\"schema\":{\"name\":\"userland.discovery\",\"major\":1,\"minor\":0},"
          "\"producer\":{\"product\":\"px4-userland\",\"version\":\"fixture-version\"},"
          "\"scan\":{\"complete\":true,\"issues\":[]},"
          "\"enclosures\":[],\"ungrouped_usb_devices\":[]}\n");
    return true;
}

DeviceObservation observation(std::uint16_t product_id, const char* serial,
                              std::uint8_t bus, std::uint8_t address,
                              std::initializer_list<std::uint8_t> ports)
{
    DeviceObservation result;
    result.vendor_id = kQ3U4VendorId;
    result.product_id = product_id;
    result.serial = serial;
    result.location.has_bus = true;
    result.location.bus = bus;
    result.location.has_address = true;
    result.location.address = address;
    for (std::uint8_t port : ports) result.location.port_path[result.location.port_count++] = port;
    return result;
}

bool test_location_collision_and_duplicate_candidates()
{
    GroupingResult grouping;
    Q3U4Group m1 = group("000000000000001", DeviceModel::px_m1ur, GroupStatus::ready);
    m1.devices[0] = observation(kPxM1UrProductId, "000000000000001", 1U, 6U, {2U, 3U});
    Q3U4Group s1 = group("000000000000001", DeviceModel::px_s1ur, GroupStatus::ready);
    s1.devices[0] = observation(kPxS1UrProductId, "000000000000001", 1U, 7U, {2U, 4U});
    grouping.groups.push_back(m1);
    grouping.groups.push_back(s1);

    Q3U4Group duplicate = group("00001205000960", DeviceModel::px_q3u4,
                                GroupStatus::duplicate);
    duplicate.candidates.push_back({0U, 1U,
        observation(kQ3U4ProductId, "000012050009601", 1U, 8U, {3U, 1U})});
    duplicate.candidates.push_back({1U, 1U,
        observation(kQ3U4ProductId, "000012050009601", 1U, 9U, {3U, 2U})});
    duplicate.candidates.push_back({2U, 2U,
        observation(kQ3U4ProductId, "000012050009602", 1U, 10U, {3U, 3U})});
    grouping.groups.push_back(duplicate);

    const std::string text = tools::format_device_list(grouping);
    CHECK(text.find("model=PX-M1UR usb=0511:0854 status=ready receivers=1 serial_unique=false"
                    " dev1_bus=1 dev1_address=6 dev1_port=1-2.3\n") != std::string::npos);
    CHECK(text.find("model=PX-S1UR usb=0511:0855 status=ready receivers=1 serial_unique=false"
                    " dev1_bus=1 dev1_address=7 dev1_port=1-2.4\n") != std::string::npos);
    CHECK(text.find("receiver=0 device=1 local=0 system=ISDB-T/S lnb_15v_supported=false\n")
          != std::string::npos);
    CHECK(text.find("candidate1_device=1 candidate1_bus=1 candidate1_address=8 "
                    "candidate1_port=1-3.1 candidate2_device=1") != std::string::npos);
    CHECK(text.find("candidate3_device=2 candidate3_bus=1 candidate3_address=10 "
                    "candidate3_port=1-3.3") != std::string::npos);

    const auto formatted = tools::format_device_list_json(grouping, "fixture-version");
    CHECK(formatted);
    const std::string& json = formatted.value();
    CHECK(json.find("\"serial_unique\":false,\"status\":\"ready\"") != std::string::npos);
    CHECK(json.find("\"devices\":[{\"id\":1,\"usb\":\"0511:0854\","
                    "\"serial\":\"000000000000001\",\"bus\":1,\"address\":6,"
                    "\"port\":\"1-2.3\",\"receivers\":[{\"receiver\":0,\"device\":1,"
                    "\"local\":0,\"systems\":[\"ISDB-T\",\"ISDB-S\"]}]}],\"candidates\":[]")
          != std::string::npos);
    CHECK(json.find("\"serial_unique\":false,\"status\":\"duplicate\","
                    "\"lnb_15v_supported\":true,\"devices\":[],"
                    "\"candidates\":[{\"device\":1") != std::string::npos);
    CHECK(json.find("\"system\":") == std::string::npos);
    CHECK(json.back() == '\n' && json.find('\n') == json.size() - 1U);
    return true;
}

bool test_ready_serial_colliding_with_incomplete_is_not_unique()
{
    GroupingResult grouping;
    grouping.groups.push_back(
        group("000000000000001", DeviceModel::px_m1ur, GroupStatus::ready));
    grouping.groups.push_back(
        group("000000000000001", DeviceModel::px_q3u4, GroupStatus::incomplete));
    const std::string text = tools::format_device_list(grouping);
    CHECK(text.find("model=PX-M1UR usb=0511:0854 status=ready receivers=1 "
                    "serial_unique=false\n") != std::string::npos);
    CHECK(text.find("model=PX-Q3U4 usb=0511:084a status=incomplete receivers=8 "
                    "serial_unique=false\n") != std::string::npos);
    const auto formatted = tools::format_device_list_json(grouping, "fixture-version");
    CHECK(formatted);
    const std::string& json = formatted.value();
    CHECK(json.find("\"serial_unique\":false,\"status\":\"ready\"") != std::string::npos);
    CHECK(json.find("\"serial_unique\":false,\"status\":\"incomplete\"") != std::string::npos);
    return true;
}

bool test_json_rejected_null_and_escaping()
{
    GroupingResult grouping;
    RejectedObservation bad = rejected(kQ3U4ProductId, "x\"\\\n", ObservationStatus::invalid_serial);
    bad.observation.location.has_bus = true;
    bad.observation.location.bus = 2U;
    grouping.rejected.push_back(bad);
    grouping.rejected.push_back(
        rejected(kQ3U4ProductId, "bad\xff", ObservationStatus::invalid_serial));
    grouping.rejected.push_back(
        rejected(kQ3U4ProductId, "\xc3\xa9", ObservationStatus::invalid_serial));
    grouping.rejected.push_back(rejected(kPxMlt5PeProductId, "", ObservationStatus::open_failed));
    grouping.rejected.push_back(rejected(0x084eU, "ignored", ObservationStatus::unsupported));
    const auto formatted = tools::format_device_list_json(grouping, "fixture-version");
    CHECK(formatted);
    const std::string& json = formatted.value();
    CHECK(json.find("\"serial\":\"x\\\"\\\\\\n\"") != std::string::npos);
    CHECK(json.find("\"bus\":2,\"address\":null,\"port\":null") != std::string::npos);
    CHECK(json.find("\"serial\":null,\"model\":\"PX-MLT5PE\"") != std::string::npos);
    CHECK(json.find("bad") == std::string::npos);
    CHECK(json.find("\"serial\":null,\"model\":\"PX-Q3U4\"") != std::string::npos);
    CHECK(json.find("\"serial\":\"\xc3\xa9\"") != std::string::npos);
    CHECK(json.find("ignored") == std::string::npos);
    CHECK(json.find("\"complete\":false") != std::string::npos);
    CHECK(json.find("\"readers\":") == std::string::npos);
    CHECK(json.find("\"receivers\":") == std::string::npos);
    return true;
}

bool test_json_observed_devices_and_unknown_scan()
{
    GroupingResult grouping;
    Q3U4Group paired = group("00001205000960", DeviceModel::px_q3u4, GroupStatus::ready);
    paired.devices[0U] = observation(kQ3U4ProductId, "000012050009601", 1U, 8U, {3U, 1U});
    paired.devices[1U] = observation(kQ3U4ProductId, "000012050009602", 1U, 9U, {3U, 2U});
    grouping.groups.push_back(paired);
    auto formatted = tools::format_device_list_json(grouping, "fixture-version");
    CHECK(formatted);
    const std::string& json = formatted.value();
    CHECK(json.find("\"serial_unique\":true,\"status\":\"ready\"") != std::string::npos);
    CHECK(json.find("\"lnb_15v_supported\":true,\"readers\":[{\"reader\":0,\"device\":1}]")
          != std::string::npos);
    CHECK(json.find("\"receiver\":3,\"device\":1,\"local\":3,\"systems\":[\"ISDB-T\"]}]}"
                    ",{\"id\":2,\"usb\":\"0511:084a\"") != std::string::npos);
    CHECK(json.find("\"receiver\":4,\"device\":2,\"local\":0,\"systems\":[\"ISDB-S\"]")
          != std::string::npos);
    CHECK(json.find("\"receiver\":7,\"device\":2,\"local\":3,\"systems\":[\"ISDB-T\"]}]}]"
                    ",\"candidates\":[]") != std::string::npos);

    grouping.groups[0U].devices[0U].reset();
    grouping.groups[0U].status = GroupStatus::incomplete;
    formatted = tools::format_device_list_json(grouping, "fixture-version");
    CHECK(formatted);
    CHECK(formatted.value().find("\"serial_unique\":true,\"status\":\"incomplete\"")
          != std::string::npos);
    CHECK(formatted.value().find("\"devices\":[{\"id\":2") != std::string::npos);
    CHECK(formatted.value().find("\"receiver\":0") == std::string::npos);
    CHECK(formatted.value().find("\"readers\":") == std::string::npos);

    grouping.complete = false;
    formatted = tools::format_device_list_json(grouping, "fixture-version");
    CHECK(formatted);
    CHECK(formatted.value().find("\"complete\":false") != std::string::npos);
    CHECK(formatted.value().find("\"serial_unique\":null") != std::string::npos);
    grouping.groups.push_back(grouping.groups[0U]);
    formatted = tools::format_device_list_json(grouping, "fixture-version");
    CHECK(formatted);
    CHECK(formatted.value().find("\"serial_unique\":false") != std::string::npos);
    CHECK(formatted.value().find("\"serial_unique\":null") == std::string::npos);
    CHECK(!tools::format_device_list_json(grouping, std::string(65U, 'v')));
    return true;
}

// Every supported model appears as `MODEL VID:PID`, in table order, with no
// connected device and no catalog fields (receivers, LNB, status, JSON, CSV).
bool test_models_are_a_static_name_and_usb_id_table()
{
    const std::string expected =
        "PX-Q3U4 0511:084a\n"
        "PX-W3U4 0511:083f\n"
        "PX-MLT5PE 0511:024e\n"
        "DTV02A-5TS-P 0511:924e\n"
        "PX-W3PE4 0511:023f\n"
        "PX-W3PE5 0511:073f\n"
        "PX-Q3PE4 0511:024a\n"
        "PX-Q3PE5 0511:074a\n"
        "PX-MLT8PE3 0511:0252\n"
        "PX-MLT8PE5 0511:0253\n"
        "DTV02A-4TS-P 0511:0254\n"
        "PX-M1UR 0511:0854\n"
        "PX-S1UR 0511:0855\n"
        "DTV03A-1TU 0511:0052\n"
        "DTV02-1T1S-U 0511:004b\n"
        "DTV02A-1T1S-U 0511:084b\n";
    CHECK(tools::format_model_list() == expected);
    return true;
}

}  // namespace

bool run_px4d_list_format_tests()
{
    return test_enclosures_carry_the_px4ctl_list_receiver_table() &&
           test_px4_receiver_table_preserves_product_mapping() &&
           test_incomplete_enclosure_keeps_its_status() &&
           test_rejected_devices_of_supported_models_only() &&
           test_nothing_connected_prints_nothing() &&
           test_location_collision_and_duplicate_candidates() &&
           test_ready_serial_colliding_with_incomplete_is_not_unique() &&
           test_json_rejected_null_and_escaping() &&
           test_json_observed_devices_and_unknown_scan() &&
           test_models_are_a_static_name_and_usb_id_table();
}

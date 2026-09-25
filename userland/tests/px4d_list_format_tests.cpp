// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_list_format.h"

#include <cstdio>
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

    // The receiver lines are byte-identical to `px4ctl list` for the same model.
    const std::string expected =
        "serial=00001205000960 model=PX-Q3U4 usb=0511:084a status=ready receivers=8\n"
        "receiver=0 device=1 local=0 system=ISDB-S\n"
        "receiver=1 device=1 local=1 system=ISDB-S\n"
        "receiver=2 device=1 local=2 system=ISDB-T\n"
        "receiver=3 device=1 local=3 system=ISDB-T\n"
        "receiver=4 device=2 local=0 system=ISDB-S\n"
        "receiver=5 device=2 local=1 system=ISDB-S\n"
        "receiver=6 device=2 local=2 system=ISDB-T\n"
        "receiver=7 device=2 local=3 system=ISDB-T\n"
        "serial=000020263901491 model=DTV02A-5TS-P usb=0511:924e status=ready receivers=5\n"
        "receiver=0 device=1 local=0 system=ISDB-T/S\n"
        "receiver=1 device=1 local=1 system=ISDB-T/S\n"
        "receiver=2 device=1 local=2 system=ISDB-T/S\n"
        "receiver=3 device=1 local=3 system=ISDB-T/S\n"
        "receiver=4 device=1 local=4 system=ISDB-T/S\n";
    CHECK(tools::format_device_list(grouping) == expected);
    return true;
}

bool test_incomplete_enclosure_keeps_its_status()
{
    GroupingResult grouping;
    grouping.groups.push_back(
        group("00001205000123", DeviceModel::px_q3u4, GroupStatus::incomplete));
    const std::string output = tools::format_device_list(grouping);
    CHECK(output.rfind(
              "serial=00001205000123 model=PX-Q3U4 usb=0511:084a status=incomplete receivers=8\n",
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
    return true;
}

}  // namespace

bool run_px4d_list_format_tests()
{
    return test_enclosures_carry_the_px4ctl_list_receiver_table() &&
           test_incomplete_enclosure_keeps_its_status() &&
           test_rejected_devices_of_supported_models_only() &&
           test_nothing_connected_prints_nothing();
}

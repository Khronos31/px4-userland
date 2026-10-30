// SPDX-License-Identifier: GPL-2.0-only
#include <dtv/host_ts/px4_ts_core.h>
#include <dtv/userland/host_runtime/control_socket.h>

#include "px4_ts_profile.h"
#include "px4d_args.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using px4::userland::cli::Px4TsArguments;
using px4::userland::cli::TsClientProfile;
using px4::userland::cli::parse_px4_ts_product_arguments;

// Uses the product profile booleans from px4_ts.cpp and px4_ts_windows.cpp.
// The instance spelling is part of the daemon contract, so it must be
// preserved.
const TsClientProfile kProductProfile{
    "px4-ts", "px4-userland", dtv::userland::identity::Product::px4,
    px4::userland::cli::kPx4TsSerialSelectorSupported,
    px4::userland::cli::kPx4TsPreserveInstanceCase, nullptr};

Px4TsArguments parse(std::vector<std::string> values)
{
    std::vector<const char*> argv;
    for (const auto& value : values) argv.push_back(value.c_str());
    return parse_px4_ts_product_arguments(static_cast<int>(argv.size()), argv.data());
}

Px4TsArguments parse_product(std::vector<std::string> values)
{
    std::vector<const char*> argv;
    for (const auto& value : values) argv.push_back(value.c_str());
    return parse_px4_ts_product_arguments(static_cast<int>(argv.size()), argv.data(),
                                          kProductProfile);
}

bool check(bool condition, const char* message)
{
    if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
    return condition;
}

}  // namespace

int main()
{
    bool ok = true;
    const auto version = parse({"px4-ts", "--version"});
    ok &= check(version.valid && version.version && version.product_cli && !version.help,
                "--version is standalone");
    ok &= check(!parse({"px4-ts", "--version", "--receiver", "0"}).valid,
                "--version cannot be combined");

    const auto t13 = parse({"px4-ts", "--device", "serial:00001205000960",
                            "--receiver", "0", "-c", "T13"});
    ok &= check(t13.valid && !t13.duration_set && t13.frequency_hz == 473142857U,
                "runtime-dir is optional and T13 resolves to canonical Hz");
    const auto t27 = parse({"px4-ts", "--device", "serial:00001205000960",
                            "--receiver", "0", "-f", "557142857", "-s", "isdb-t",
                            "--runtime-dir", "/tmp/px4-ts-runtime"});
    ok &= check(t27.valid && t27.frequency_hz == 557142857U,
                "explicit runtime-dir and Hz input are retained");

    for (const auto* spelling : {"isdb-t", "ISDB-T", "IsDb-T"}) {
        const auto matched = parse({"px4-ts", "--device", "serial:00001205000960",
                                    "--receiver", "0", "--freq", "557142857",
                                    "--system", spelling});
        ok &= check(matched.valid, "terrestrial system names are case-insensitive");
    }
    ok &= check(!parse({"px4-ts", "--device", "serial:00001205000960",
                         "--receiver", "0", "--freq", "557142857",
                         "--system", "ISDB-T2"}).valid,
                "extended system names are rejected");
    ok &= check(!parse({"px4-ts", "--device", "serial:00001205000960",
                         "--receiver", "0", "--frequency-khz", "557142"}).valid,
                "legacy frequency alias is rejected");

    for (const char* spelling : {"RoomA", "rooma", "ROOMA", "receiver.left"}) {
        const auto matched = parse_product({"px4-ts", "--instance", spelling,
                                            "--receiver", "0", "-c", "T13"});
        ok &= check(matched.valid && matched.instance == spelling,
                    "product profile preserves the explicit instance spelling");
    }
    for (const char* rejected : {".", "..", "room/a", "room:a", "00001205000960"}) {
        ok &= check(!parse_product({"px4-ts", "--instance", rejected,
                                    "--receiver", "0", "-c", "T13"}).valid,
                    "product profile rejects an invalid instance token");
    }
    const std::string too_long(81U, 'a');
    ok &= check(!parse_product({"px4-ts", "--instance", too_long,
                                "--receiver", "0", "-c", "T13"}).valid,
                "product profile rejects an over-long instance token");

    const auto ts_arguments = parse_product({"px4-ts", "--instance", "RoomA",
                                             "--receiver", "0", "-c", "T13",
                                             "--runtime-dir", "/tmp/px4-ts-case"});
    ok &= check(ts_arguments.valid && ts_arguments.instance == "RoomA",
                "TS keeps RoomA for the endpoint");
    const char* daemon_argv[] = {"px4d", "--device", "usb:1-2",
                                 "--firmware", "firmware.bin",
                                 "--instance", "RoomA",
                                 "--runtime-dir", "/tmp/px4-ts-case"};
    const auto daemon_arguments =
        px4::userland::parse_px4d_arguments(9, daemon_argv);
    ok &= check(daemon_arguments.valid && daemon_arguments.instance == "RoomA",
                "px4d keeps RoomA for the endpoint");
    const auto ts_endpoint = dtv::userland::host_runtime::control_endpoint_path(
        {ts_arguments.runtime_directory, "px4-userland", ts_arguments.instance});
    const auto daemon_endpoint = dtv::userland::host_runtime::control_endpoint_path(
        {daemon_arguments.runtime_directory, "px4-userland", daemon_arguments.instance});
    ok &= check(ts_endpoint && daemon_endpoint &&
                    ts_endpoint.value() == daemon_endpoint.value(),
                "TS and px4d derive the same control endpoint for RoomA");
    ok &= check(ts_endpoint &&
                    ts_endpoint.value().find("/RoomA/control.sock") != std::string::npos,
                "control endpoint keeps the uppercase instance component");
    return ok ? 0 : 1;
}

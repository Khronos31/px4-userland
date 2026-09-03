// SPDX-License-Identifier: GPL-2.0-only
#include "it930x_probe_args.h"

#include "px4/firmware.h"
#include "px4/it930x.h"
#include "px4/libusb_transport.h"

#include <cstdio>
#include <optional>
#include <string_view>
#include <utility>

namespace {

using namespace px4::userland;

constexpr int kArgumentFailure = 2;
constexpr int kDiscoveryOpenFailure = 3;
constexpr int kFirmwareFailure = 4;
constexpr int kUsbProtocolFailure = 5;

void print_usage() noexcept
{
    std::printf("usage: px4-it930x-probe --base BASE --device 1|2 [--command-delay-ms 0|1]\n");
    std::printf("       px4-it930x-probe --base BASE --device 1|2 --initialize --firmware PATH\n");
    std::printf("       [--require-cold] [--command-delay-ms 0|1]\n");
    std::printf("default: read-only firmware version query; command pacing is 1 ms\n");
}

int print_failure(const char* operation, Error error, int status) noexcept
{
    std::fprintf(stderr, "%s failed: %s\n", operation, error_string(error));
    return status;
}

}  // namespace

int main(int argc, char** argv)
{
    const It930xProbeArguments arguments = parse_it930x_probe_arguments(
        argc, const_cast<const char* const*>(argv));
    if (!arguments.valid) {
        std::fprintf(stderr, "argument error: %.*s\n",
                     static_cast<int>(arguments.error.size()), arguments.error.data());
        return kArgumentFailure;
    }
    if (arguments.help) {
        print_usage();
        return 0;
    }

    std::optional<FirmwareImage> firmware;
    if (arguments.initialize) {
        FirmwareProvider provider(arguments.firmware_path);
        const auto loaded = provider.load();
        if (!loaded) {
            return print_failure("firmware", loaded.error(), kFirmwareFailure);
        }
        firmware.emplace(std::move(loaded.value()));
    }

    const auto runtime = Q3U4Runtime::open_native(arguments.base_serial);
    if (!runtime) {
        return print_failure("discovery/open", runtime.error(), kDiscoveryOpenFailure);
    }
    Transport& transport = arguments.device == 1U ? runtime.value()->dev1()
                                                  : runtime.value()->dev2();
    const CommandPacingOptions pacing{arguments.pacing_mode};
    It930xController controller(transport, pacing);

    if (!arguments.initialize) {
        const auto version = controller.firmware_version();
        if (!version) {
            return print_failure("USB/protocol query", version.error(), kUsbProtocolFailure);
        }
        std::printf("base=%.*s dev=%u version=0x%08x\n",
                    static_cast<int>(arguments.base_serial.size()), arguments.base_serial.data(),
                    static_cast<unsigned int>(arguments.device),
                    static_cast<unsigned int>(version.value()));
        return 0;
    }

    const auto initialized = controller.initialize_q3u4(
        *firmware, arguments.require_cold ? InitializationPolicy::require_cold
                                          : InitializationPolicy::accept_cold_or_warm);
    if (!initialized) {
        return print_failure("USB/protocol initialize", initialized.error(),
                             initialized.error() == Error::FIRMWARE_REJECTED ? kFirmwareFailure
                                                                              : kUsbProtocolFailure);
    }
    std::printf("base=%.*s dev=%u firmware=%s version=0x%08x\n",
                static_cast<int>(arguments.base_serial.size()), arguments.base_serial.data(),
                static_cast<unsigned int>(arguments.device),
                initialized.value().already_loaded ? "already-loaded" : "newly-loaded",
                static_cast<unsigned int>(initialized.value().firmware_version));
    std::printf("verified=yes\n");
    return 0;
}

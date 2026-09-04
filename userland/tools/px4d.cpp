// SPDX-License-Identifier: GPL-2.0-only
#include "px4/card.h"
#include "px4/card_service.h"
#include "px4/control_server.h"
#include "px4/firmware.h"
#include "px4/it930x.h"
#include "px4/libusb_transport.h"
#include "px4/posix_tuner_nonce.h"
#include "px4/q3u4_stream.h"
#include "px4/tuner_service.h"

#include "q3u4_frontend.h"
#include "q3u4_lnb_power.h"
#include "q3u4_card_backend.h"
#include "q3u4_tuner_backend.h"
#include "q3u4_power.h"
#include "px4d_args.h"

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>
#include <signal.h>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc::posix;

volatile std::sig_atomic_t stop_requested = 0;

void stop_signal_handler(int) noexcept
{
    stop_requested = 1;
}

void usage() noexcept
{
    std::printf(
        "usage:\n"
        "  px4d --device BASE_SERIAL --firmware PATH "
        "[--runtime-dir PATH] [--group] [--allow-lnb-power]\n"
        "  px4d --fd FD --fd FD [--device BASE_SERIAL] --firmware PATH "
        "[--runtime-dir PATH] [--group] [--allow-lnb-power]\n"
        "\n"
        "  --allow-lnb-power  permit explicit ISDB-S 15 V requests; default off\n");
}

int exit_status(Error error) noexcept
{
    switch (error) {
    case Error::OK: return 0;
    case Error::INVALID_ARGUMENT: return 2;
    case Error::NOT_FOUND:
    case Error::NOT_READY:
    case Error::NO_CARD:
    case Error::UNSUPPORTED: return 3;
    case Error::BUSY: return 4;
    case Error::TIMEOUT: return 5;
    case Error::VERSION_MISMATCH:
    case Error::PROTOCOL_ERROR: return 6;
    case Error::USB_IO:
    case Error::DISCONNECTED: return 7;
    case Error::SLOW_CONSUMER: return 8;
    case Error::CARD_REMOVED:
    case Error::BUFFER_TOO_SMALL: return 9;
    case Error::FIRMWARE_REJECTED: return 10;
    case Error::INTERNAL: return 70;
    }
    return 70;
}

class DaemonTime final : public CardTime,
                         public Q3U4FrontendDelay,
                         public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

bool install_signal_handlers() noexcept
{
    struct sigaction action {};
    action.sa_handler = stop_signal_handler;
    if (sigemptyset(&action.sa_mask) != 0) return false;
    action.sa_flags = 0;
    return ::sigaction(SIGINT, &action, nullptr) == 0 &&
           ::sigaction(SIGTERM, &action, nullptr) == 0;
}

}  // namespace

int main(int argc, char** argv)
{
    const Px4dArguments arguments =
        parse_px4d_arguments(argc, const_cast<const char* const*>(argv));
    if (!arguments.valid) {
        std::fprintf(stderr, "argument error: %.*s\n",
                     static_cast<int>(arguments.error.size()), arguments.error.data());
        usage();
        return 2;
    }
    if (arguments.help) {
        usage();
        return 0;
    }

    FirmwareProvider firmware_provider(arguments.firmware);
    const auto firmware = firmware_provider.load();
    if (!firmware) {
        std::fprintf(stderr, "firmware: %s\n", error_string(firmware.error()));
        return exit_status(firmware.error());
    }
    Result<std::unique_ptr<Q3U4Runtime>> runtime =
        Result<std::unique_ptr<Q3U4Runtime>>::failure(Error::INTERNAL);
    if (px4d_open_mode(arguments) == Px4dOpenMode::file_descriptors) {
        const std::vector<int> file_descriptors(
            arguments.file_descriptors.begin(), arguments.file_descriptors.end());
        // open_fds duplicates these caller-owned descriptors.  The runtime
        // owns and closes only its duplicates; termux-usb/UsbManager owners
        // remain responsible for the originals.
        runtime = Q3U4Runtime::open_fds(file_descriptors, arguments.device);
    } else {
        runtime = Q3U4Runtime::open_native(arguments.device);
    }
    if (!runtime) {
        std::fprintf(stderr, "device open: %s\n", error_string(runtime.error()));
        return exit_status(runtime.error());
    }
    const std::string base_serial(runtime.value()->base_serial());
    if (!valid_px4d_base_serial(base_serial)) {
        std::fprintf(stderr, "device open: invalid observed base serial\n");
        return exit_status(Error::INVALID_ARGUMENT);
    }

    It930xController dev1(runtime.value()->dev1());
    It930xController dev2(runtime.value()->dev2());
    const auto initialized1 = dev1.initialize_q3u4(firmware.value());
    if (!initialized1) {
        std::fprintf(stderr, "device 1 initialize: %s\n",
                     error_string(initialized1.error()));
        return exit_status(initialized1.error());
    }
    const auto initialized2 = dev2.initialize_q3u4(firmware.value());
    if (!initialized2) {
        std::fprintf(stderr, "device 2 initialize: %s\n",
                     error_string(initialized2.error()));
        return exit_status(initialized2.error());
    }

    DaemonTime time;
    It930xBridgeI2cMaster dev1_i2c(dev1);
    It930xBridgeI2cMaster dev2_i2c(dev2);
    It930xBackendPower dev1_power(dev1);
    It930xBackendPower dev2_power(dev2);
    It930xPsbPurger dev1_purger(dev1);
    It930xPsbPurger dev2_purger(dev2);
    Q3U4FrontendEnclosure enclosure(dev1_i2c, dev2_i2c, dev1_power,
                                    dev2_power, time, &dev1_purger, &dev2_purger);
    Q3U4CardBackend backend(dev1, enclosure);
    It930xCardHardware card_hardware(dev1);
    CardSession card_session(card_hardware, time);
    NativeCardProtocolSession protocol(card_session);
    CardService card_service(backend, protocol);
    It930xLnbPower dev1_lnb(dev1);
    It930xLnbPower dev2_lnb(dev2);
    Q3U4LnbPowerCoordinator lnb_power(
        dev1_lnb, dev2_lnb, arguments.allow_lnb_power);
    Q3U4FrontendTunerBackend tuner_backend(enclosure, lnb_power);
    PosixTunerNonceSource nonce_source;
    const auto stream = Q3U4StreamDataPlane::create(runtime.value()->dev1(),
                                                    runtime.value()->dev2());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    TunerService tuner_service(tuner_backend, nonce_source, time, nullptr, nullptr,
                               stream.value().get());

    const char* runtime_directory = arguments.runtime_directory.empty() ?
                                        nullptr : arguments.runtime_directory.c_str();
    const EndpointConfig endpoint{
        runtime_directory, base_serial.c_str(), kControlEndpointName,
        arguments.group ? EndpointAccess::shared_group : EndpointAccess::private_user};
    auto server = PosixControlServer::create(
        endpoint, card_service, tuner_service, base_serial, true, 0x03U,
        stream.value().get());
    if (!server) {
        std::fprintf(stderr, "control endpoint: %s\n", error_string(server.error()));
        return exit_status(server.error());
    }
    if (!install_signal_handlers()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }

    std::fprintf(stderr, "px4d ready: device=%s endpoint=%s\n",
                 base_serial.c_str(), server.value()->endpoint_path());
    Error loop_error = Error::OK;
    while (stop_requested == 0) {
        const auto polled = server.value()->poll_once(Timeout{100U});
        if (!polled) {
            loop_error = polled.error();
            break;
        }
    }
    const auto cleanup = server.value()->shutdown();
    if (!cleanup) {
        std::fprintf(stderr, "shutdown cleanup: %s\n", error_string(cleanup.error()));
        if (loop_error == Error::OK) loop_error = cleanup.error();
    }
    if (loop_error != Error::OK) {
        std::fprintf(stderr, "px4d stopped: %s\n", error_string(loop_error));
    }
    return exit_status(loop_error);
}

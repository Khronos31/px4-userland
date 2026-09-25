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

#include "mlt5pe_backend.h"
#include "mlt5pe_frontend.h"
#include "mlt5pe_power.h"
#include "q3u4_frontend.h"
#include "q3u4_lnb_power.h"
#include "q3u4_card_backend.h"
#include "q3u4_tuner_backend.h"
#include "q3u4_power.h"
#include "px4d_args.h"
#include "px4d_signals.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc::posix;

void usage() noexcept
{
    std::printf(
        "usage:\n"
        "  px4d --device BASE_SERIAL --firmware PATH "
        "[--runtime-dir PATH] [--group] [--allow-lnb-power]\n"
        "  px4d --fd FD [--fd FD] [--device BASE_SERIAL] --firmware PATH "
        "[--runtime-dir PATH] [--group] [--allow-lnb-power]\n"
        "\n"
        "  BASE_SERIAL is the 14-digit PX-Q3U4 base serial or the 15-digit\n"
        "  PX-MLT5PE/DTV02A-5TS-P serial.  Pass one --fd per USB device: two\n"
        "  for PX-Q3U4, one for PX-MLT5PE/DTV02A-5TS-P.\n"
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
                         public Mlt5PeDelay,
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

// Publishes the control/data endpoints and runs the foreground loop until a
// stop signal or loop failure.  Returns the process exit status.
int serve(const Px4dArguments& arguments, const std::string& base_serial,
          CardService& card_service, TunerService& tuner_service,
          Q3U4StreamDataPlane& stream, std::uint8_t usb_present_mask,
          std::uint8_t receiver_count) noexcept
{
    const char* runtime_directory = arguments.runtime_directory.empty() ?
                                        nullptr : arguments.runtime_directory.c_str();
    const EndpointConfig endpoint{
        runtime_directory, base_serial.c_str(), kControlEndpointName,
        arguments.group ? EndpointAccess::shared_group : EndpointAccess::private_user};
    auto server = PosixControlServer::create(
        endpoint, card_service, tuner_service, base_serial, true, usb_present_mask,
        &stream, receiver_count);
    if (!server) {
        std::fprintf(stderr, "control endpoint: %s\n", error_string(server.error()));
        return exit_status(server.error());
    }
    if (!px4::userland::px4d::install_signal_handlers()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }

    std::fprintf(stderr, "px4d ready: device=%s endpoint=%s\n",
                 base_serial.c_str(), server.value()->endpoint_path());
    Error loop_error = Error::OK;
    while (!px4::userland::px4d::stop_requested()) {
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

int run_q3u4(const Px4dArguments& arguments, Q3U4Runtime& runtime,
             const FirmwareImage& firmware, const std::string& base_serial) noexcept
{
    It930xController dev1(runtime.dev1());
    It930xController dev2(runtime.dev2());
    const auto initialized1 = dev1.initialize_q3u4(firmware);
    if (!initialized1) {
        std::fprintf(stderr, "device 1 initialize: %s\n",
                     error_string(initialized1.error()));
        return exit_status(initialized1.error());
    }
    const auto initialized2 = dev2.initialize_q3u4(firmware);
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
    const auto stream = Q3U4StreamDataPlane::create(runtime.dev1(), runtime.dev2());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    TunerService tuner_service(tuner_backend, nonce_source, time, nullptr, nullptr,
                               stream.value().get());
    return serve(arguments, base_serial, card_service, tuner_service, *stream.value(),
                 0x03U, ipc::kQ3U4ReceiverCount);
}

// PX-W3U4 has no second bridge.  The Q3U4 coordinator still describes two
// bridges; power and LNB calls for receivers 0..3 stay on the first, and the
// second answers success without a USB write.
class AbsentBridgePower final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool, Q3U4Delay&) noexcept override
    {
        return Result<void>::success();
    }
};

class W3U4TunerBackend final : public TunerServiceBackend {
public:
    W3U4TunerBackend(Q3U4FrontendEnclosure& enclosure,
                     Q3U4LnbPowerCoordinator& lnb_power) noexcept
        : inner_(enclosure, lnb_power)
    {
    }

    std::uint8_t receiver_count() const noexcept override
    {
        return ipc::kW3U4ReceiverCount;
    }

    bool receiver_supports(std::uint8_t receiver, ipc::System system) const noexcept override
    {
        if (receiver >= ipc::kW3U4ReceiverCount) return false;
        const bool satellite = receiver < 2U;
        return system == (satellite ? ipc::System::ISDB_S : ipc::System::ISDB_T);
    }

    bool requires_terrestrial_lock_settle() const noexcept override { return true; }

    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        return inner_.open_receiver(receiver);
    }
    Result<void> tune_terrestrial(std::uint8_t receiver, std::uint32_t frequency_khz,
                                  std::uint32_t timeout_ms) noexcept override
    {
        return inner_.tune_terrestrial(receiver, frequency_khz, timeout_ms);
    }
    Result<void> tune_satellite(std::uint8_t receiver, std::uint32_t frequency_khz,
                                std::uint32_t timeout_ms) noexcept override
    {
        return inner_.tune_satellite(receiver, frequency_khz, timeout_ms);
    }
    Result<bool> is_locked(std::uint8_t receiver, ipc::System system) noexcept override
    {
        return inner_.is_locked(receiver, system);
    }
    Result<void> select_satellite_slot(std::uint8_t receiver, std::uint8_t slot,
                                       std::uint32_t timeout_ms) noexcept override
    {
        return inner_.select_satellite_slot(receiver, slot, timeout_ms);
    }
    Result<void> select_satellite_tsid(std::uint8_t receiver, std::uint16_t tsid,
                                       std::uint32_t timeout_ms) noexcept override
    {
        return inner_.select_satellite_tsid(receiver, tsid, timeout_ms);
    }
    Result<void> close_receiver(std::uint8_t receiver) noexcept override
    {
        return inner_.close_receiver(receiver);
    }
    Result<void> start_capture(std::uint8_t receiver, ipc::System system) noexcept override
    {
        return inner_.start_capture(receiver, system);
    }
    Result<void> stop_capture(std::uint8_t receiver, ipc::System system) noexcept override
    {
        return inner_.stop_capture(receiver, system);
    }
    Result<void> begin_tune_power(std::uint8_t receiver, ipc::System system,
                                  std::uint8_t lnb_voltage) noexcept override
    {
        return inner_.begin_tune_power(receiver, system, lnb_voltage);
    }
    Result<void> commit_tune_power(std::uint8_t receiver) noexcept override
    {
        return inner_.commit_tune_power(receiver);
    }
    Result<void> rollback_tune_power(std::uint8_t receiver) noexcept override
    {
        return inner_.rollback_tune_power(receiver);
    }
    void mark_receiver_disconnected(std::uint8_t receiver) noexcept override
    {
        inner_.mark_receiver_disconnected(receiver);
    }
    Result<void> shutdown() noexcept override { return inner_.shutdown(); }

private:
    Q3U4FrontendTunerBackend inner_;
};

int run_w3u4(const Px4dArguments& arguments, Q3U4Runtime& runtime,
             const FirmwareImage& firmware, const std::string& base_serial) noexcept
{
    It930xController device(runtime.dev1());
    const auto initialized = device.initialize_q3u4(firmware);
    if (!initialized) {
        std::fprintf(stderr, "device initialize: %s\n", error_string(initialized.error()));
        return exit_status(initialized.error());
    }

    DaemonTime time;
    It930xBridgeI2cMaster bridge_i2c(device);
    It930xBackendPower power(device);
    AbsentBridgePower absent_power;
    It930xPsbPurger purger(device);
    Q3U4FrontendEnclosure enclosure(bridge_i2c, bridge_i2c, power, absent_power,
                                    time, &purger, nullptr);
    Q3U4CardBackend backend(device, enclosure);
    It930xCardHardware card_hardware(device);
    CardSession card_session(card_hardware, time);
    NativeCardProtocolSession protocol(card_session);
    CardService card_service(backend, protocol);
    It930xLnbPower lnb(device);
    Q3U4LnbPowerCoordinator lnb_power(lnb, lnb, arguments.allow_lnb_power);
    W3U4TunerBackend tuner_backend(enclosure, lnb_power);
    PosixTunerNonceSource nonce_source;
    const auto stream = Q3U4StreamDataPlane::create_w3u4(runtime.dev1());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    TunerService tuner_service(tuner_backend, nonce_source, time, nullptr, nullptr,
                               stream.value().get());
    return serve(arguments, base_serial, card_service, tuner_service, *stream.value(),
                 0x01U, ipc::kW3U4ReceiverCount);
}

int run_mlt5pe(const Px4dArguments& arguments, Q3U4Runtime& runtime,
               const FirmwareImage& firmware, const std::string& base_serial) noexcept
{
    It930xController device(runtime.dev1());
    const auto initialized = device.initialize_mlt5pe(firmware);
    if (!initialized) {
        std::fprintf(stderr, "device initialize: %s\n", error_string(initialized.error()));
        return exit_status(initialized.error());
    }

    DaemonTime time;
    It930xBridgeI2cMaster bus1(device, 1U);
    It930xBridgeI2cMaster bus3(device, 3U);
    It930xBackendPower power(device);
    It930xPsbPurger purger(device);
    Mlt5PeFrontend frontend(bus1, bus3, power, time, &purger);
    Mlt5PeCardBackend backend(device, frontend);
    It930xCardHardware card_hardware(device);
    CardSession card_session(card_hardware, time);
    NativeCardProtocolSession protocol(card_session);
    CardService card_service(backend, protocol);
    It930xLnbPower lnb(device);
    Mlt5PeLnbPowerCoordinator lnb_power(lnb, arguments.allow_lnb_power);
    Mlt5PeTunerBackend tuner_backend(frontend, lnb_power);
    PosixTunerNonceSource nonce_source;
    const auto stream = Q3U4StreamDataPlane::create_mlt5pe(runtime.dev1());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    TunerService tuner_service(tuner_backend, nonce_source, time, nullptr, nullptr,
                               stream.value().get());
    return serve(arguments, base_serial, card_service, tuner_service, *stream.value(),
                 0x01U, ipc::kMlt5PeReceiverCount);
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
    const Px4dOpenMode mode = px4d_open_mode(arguments);
    if (mode == Px4dOpenMode::file_descriptors) {
        const std::vector<int> file_descriptors(
            arguments.file_descriptors.begin(),
            arguments.file_descriptors.begin() +
                static_cast<std::ptrdiff_t>(arguments.file_descriptor_count));
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
    const DeviceModel model = runtime.value()->model();
    const bool single_bridge = runtime.value()->bridge_count() == 1U;
    // Every --fd must belong to the selected enclosure: an extra descriptor
    // that selection ignored is an argument error, not a silent leftover.
    if (mode == Px4dOpenMode::file_descriptors &&
        arguments.file_descriptor_count != runtime.value()->bridge_count()) {
        std::fprintf(stderr, "device open: descriptor count does not match %s\n",
                     device_profile(runtime.value()->model()).name);
        return exit_status(Error::INVALID_ARGUMENT);
    }
    const std::string base_serial(runtime.value()->base_serial());
    if (!valid_px4d_base_serial(base_serial)) {
        std::fprintf(stderr, "device open: invalid observed base serial\n");
        return exit_status(Error::INVALID_ARGUMENT);
    }
    std::fprintf(stderr, "px4d device: %s\n", device_profile(model).name);
    if (model == DeviceModel::px_w3u4) {
        return run_w3u4(arguments, *runtime.value(), firmware.value(), base_serial);
    }
    return single_bridge
        ? run_mlt5pe(arguments, *runtime.value(), firmware.value(), base_serial)
        : run_q3u4(arguments, *runtime.value(), firmware.value(), base_serial);
}

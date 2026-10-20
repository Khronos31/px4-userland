// SPDX-License-Identifier: GPL-2.0-only
#include "px4/card.h"
#include "px4/card_service.h"
#include "px4/native_card_protocol_session.h"
#include "px4/control_context.h"
#include "px4/control_server.h"
#include "px4/enclosure_context.h"
#include "px4/firmware.h"
#include "px4/it930x.h"
#include "px4/libusb_transport.h"
#include "px4/platform_sleep.h"
#include "px4/q3u4_stream.h"
#include "px4/tuner_service.h"

#include <dtv/userland/identity.h>

#if defined(_WIN32)
#include "px4/windows_tuner_nonce.h"
#include "px4_windows_args.h"
#include "px4d_windows_stdin.h"
#else
#include "px4/posix_tuner_nonce.h"
#endif

#include "mlt5pe_backend.h"
#include "mlt5pe_frontend.h"
#include "mlt5pe_power.h"
#include "q3u4_frontend.h"
#include "q3u4_lnb_power.h"
#include "q3u4_card_backend.h"
#include "q3u4_tuner_backend.h"
#include "single_receiver_frontend.h"
#include "q3u4_power.h"
#include "px4d_args.h"
#include "px4d_list_format.h"
#include "px4d_signals.h"
#include "runtime_identity.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc::posix;

#if defined(_WIN32)
using PlatformTunerNonceSource = px4::userland::ipc::windows::WindowsTunerNonceSource;
#else
using PlatformTunerNonceSource = PosixTunerNonceSource;
#endif

void usage() noexcept
{
    std::printf(
        "usage:\n"
        "  px4d -d|--device serial:VALUE|usb:BUS-PORT[,BUS-PORT] -F|--firmware PATH "
        "[--instance TOKEN] "
        "[--runtime-dir PATH] [--group] [--allow-lnb-power]\n"
        "  px4d --fd FD [--fd FD] [-d|--device serial:VALUE] -F|--firmware PATH "
        "[--instance TOKEN] [--runtime-dir PATH] [--group] [--allow-lnb-power]\n"
        "  px4d --list-json\n"
        "  px4d --models\n"
        "  px4d --version\n"
        "\n"
        "  serial:VALUE is the 14-digit serial base for paired devices or the\n"
        "  15-digit USB serial for single-device models.  Pass one --fd per\n"
        "  USB device: two for paired models, one for single-device models.\n"
        "\n"
        "  --allow-lnb-power  permit explicit ISDB-S 15 V requests; default off\n"
#if defined(_WIN32)
        "  --exit-on-stdin-eof  exit with cleanup when stdin reaches EOF (Windows\n"
        "                     cooperative parent stop)\n"
#endif
        "  -h, --help        print this help and exit\n"
        "  -F, --firmware PATH  load the tuner firmware image\n"
        "  -v, --verbose     log control request types to stderr\n"
        "  --list-json        print read-only connected USB inventory as JSON\n"
        "  --models          print static supported models and USB IDs (text)\n"
        "  --version         print current build version and exit\n"
        "                     read-only, needs neither firmware nor a daemon\n");
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
    case Error::RESOURCE_LIMIT: return 4;
    case Error::INTERNAL: return 70;
    }
    return 70;
}

// `px4d --list` (SPEC 4.6).  Native enumeration only reads descriptors and
// the serial string; it never claims an interface, so it is safe while other
// px4d instances own their enclosures.
int list_devices(bool json) noexcept
{
    const auto grouping = Q3U4Runtime::enumerate_native();
    if (!grouping) {
        std::fprintf(stderr, "enumeration failed: %s\n", error_string(grouping.error()));
        return exit_status(grouping.error());
    }
    const std::string output = json ? tools::format_discovery_json(
        grouping.value(), PX4_USERLAND_PRODUCT_VERSION)
        : tools::format_device_list(grouping.value());
    if (json && output.empty()) {
        std::fprintf(stderr, "list-json: unable to represent device inventory\n");
        return exit_status(Error::INVALID_ARGUMENT);
    }
    if ((!output.empty() &&
         std::fwrite(output.data(), 1U, output.size(), stdout) != output.size()) ||
        std::fflush(stdout) != 0) {
        std::fprintf(stderr, "list: write to stdout failed\n");
        return exit_status(Error::INTERNAL);
    }
    return 0;
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
#if defined(_WIN32)
        (void)platform::sleep_milliseconds(milliseconds);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
    }
};

class It930xSingleReceiverPower final : public Q3U4BackendPower {
public:
    explicit It930xSingleReceiverPower(It930xController& controller) noexcept : controller_(controller) {}
    Result<void> set_backend_power(bool on, Q3U4Delay& delay) noexcept override
    { return controller_.set_single_receiver_backend_power(on, delay); }
private:
    It930xController& controller_;
};

#if defined(_WIN32)
void on_stdin_eof(void*) noexcept
{
    px4::userland::px4d::request_stop();
}
#endif

// Whole-context cleanup budget for the standalone daemon.  It is the adopted
// candidate value for a full close, not a per-model measured duration.  A
// cleanup that has not quiesced by then ends the process abnormally.
constexpr std::chrono::milliseconds kWholeContextCloseBudget{15000};
constexpr int kCloseDeadlineExitStatus = 70;

// Base of the four product families: the runtime that owns the claimed USB
// transports (and their internal descriptor duplicates) and the clock every
// family object shares.  Declared first, they are released last.
class Px4Family : public EnclosureFamily {
public:
    explicit Px4Family(std::unique_ptr<Q3U4Runtime> runtime) noexcept
        : runtime_(std::move(runtime))
    {
    }
    const Q3U4Runtime& runtime() const noexcept { return *runtime_; }
    Transport& runtime_dev1() noexcept { return runtime_->dev1(); }
    Transport& runtime_dev2() noexcept { return runtime_->dev2(); }
    TunerServiceTime& time() noexcept override { return time_; }

protected:
    std::unique_ptr<Q3U4Runtime> runtime_;
    DaemonTime time_;
};

// PX-Q3U4 paired bridges.  Members follow the original construction order;
// the stream data plane is declared last so it is released first.
class Q3U4Family final : public Px4Family {
public:
    Q3U4Family(std::unique_ptr<Q3U4Runtime> runtime, bool allow_lnb_15v) noexcept
        : Px4Family(std::move(runtime)),
          dev1(runtime_->dev1()), dev2(runtime_->dev2()),
          dev1_i2c(dev1), dev2_i2c(dev2), dev1_power(dev1), dev2_power(dev2),
          dev1_purger(dev1), dev2_purger(dev2),
          enclosure(dev1_i2c, dev2_i2c, dev1_power, dev2_power, time_, &dev1_purger,
                    &dev2_purger),
          card(dev1, enclosure), card_hardware(dev1),
          card_session(card_hardware, time_), protocol(card_session),
          dev1_lnb(dev1), dev2_lnb(dev2), lnb_power(dev1_lnb, dev2_lnb, allow_lnb_15v),
          tuner(enclosure, lnb_power)
    {
    }
    TunerServiceBackend& tuner_backend() noexcept override { return tuner; }
    CardServiceBackend& card_backend() noexcept override { return card; }
    CardProtocolSession& card_protocol() noexcept override { return protocol; }
    TunerStreamControl* stream_control() noexcept override { return stream.get(); }

    It930xController dev1;
    It930xController dev2;
    It930xBridgeI2cMaster dev1_i2c;
    It930xBridgeI2cMaster dev2_i2c;
    It930xBackendPower dev1_power;
    It930xBackendPower dev2_power;
    It930xPsbPurger dev1_purger;
    It930xPsbPurger dev2_purger;
    Q3U4FrontendEnclosure enclosure;
    Q3U4CardBackend card;
    It930xCardHardware card_hardware;
    CardSession card_session;
    NativeCardProtocolSession protocol;
    It930xLnbPower dev1_lnb;
    It930xLnbPower dev2_lnb;
    Q3U4LnbPowerCoordinator lnb_power;
    Q3U4FrontendTunerBackend tuner;
    std::unique_ptr<Q3U4StreamDataPlane> stream;
};

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
    Result<TunerHzProgramming> tune_terrestrial_hz(std::uint8_t receiver,
                                                   std::uint64_t frequency_hz,
                                                   std::uint32_t timeout_ms) noexcept override
    {
        return inner_.tune_terrestrial_hz(receiver, frequency_hz, timeout_ms);
    }
    Result<TunerHzProgramming> tune_satellite_hz(std::uint8_t receiver,
                                                 std::uint64_t frequency_hz,
                                                 std::uint32_t timeout_ms) noexcept override
    {
        return inner_.tune_satellite_hz(receiver, frequency_hz, timeout_ms);
    }
    TunerStrictProfile strict_profile(std::uint8_t receiver,
                                      ipc::System system) const noexcept override
    {
        if (!receiver_supports(receiver, system)) return TunerStrictProfile{};
        return inner_.strict_profile(receiver, system);
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

class W3U4Family final : public Px4Family {
public:
    W3U4Family(std::unique_ptr<Q3U4Runtime> runtime, bool allow_lnb_15v) noexcept
        : Px4Family(std::move(runtime)),
          device(runtime_->dev1()), bridge_i2c(device), power(device), purger(device),
          enclosure(bridge_i2c, bridge_i2c, power, absent_power, time_, &purger, nullptr),
          card(device, enclosure), card_hardware(device),
          card_session(card_hardware, time_), protocol(card_session), lnb(device),
          lnb_power(lnb, lnb, allow_lnb_15v), tuner(enclosure, lnb_power)
    {
    }
    TunerServiceBackend& tuner_backend() noexcept override { return tuner; }
    CardServiceBackend& card_backend() noexcept override { return card; }
    CardProtocolSession& card_protocol() noexcept override { return protocol; }
    TunerStreamControl* stream_control() noexcept override { return stream.get(); }

    It930xController device;
    It930xBridgeI2cMaster bridge_i2c;
    It930xBackendPower power;
    AbsentBridgePower absent_power;
    It930xPsbPurger purger;
    Q3U4FrontendEnclosure enclosure;
    Q3U4CardBackend card;
    It930xCardHardware card_hardware;
    CardSession card_session;
    NativeCardProtocolSession protocol;
    It930xLnbPower lnb;
    Q3U4LnbPowerCoordinator lnb_power;
    W3U4TunerBackend tuner;
    std::unique_ptr<Q3U4StreamDataPlane> stream;
};

class Mlt5PeFamily final : public Px4Family {
public:
    Mlt5PeFamily(std::unique_ptr<Q3U4Runtime> runtime, bool allow_lnb_15v,
                 std::uint8_t receiver_count, bool satellite_supported) noexcept
        : Px4Family(std::move(runtime)),
          device(runtime_->dev1()), bus1(device, 1U), bus3(device, 3U), power(device),
          purger(device),
          frontend(bus1, bus3, power, time_, &purger, receiver_count, runtime_->model()),
          card(device, frontend), card_hardware(device),
          card_session(card_hardware, time_), protocol(card_session), lnb(device),
          lnb_power(lnb, allow_lnb_15v),
          tuner(frontend, lnb_power, receiver_count, satellite_supported, runtime_->model())
    {
    }
    TunerServiceBackend& tuner_backend() noexcept override { return tuner; }
    CardServiceBackend& card_backend() noexcept override { return card; }
    CardProtocolSession& card_protocol() noexcept override { return protocol; }
    TunerStreamControl* stream_control() noexcept override { return stream.get(); }

    It930xController device;
    It930xBridgeI2cMaster bus1;
    It930xBridgeI2cMaster bus3;
    It930xBackendPower power;
    It930xPsbPurger purger;
    Mlt5PeFrontend frontend;
    Mlt5PeCardBackend card;
    It930xCardHardware card_hardware;
    CardSession card_session;
    NativeCardProtocolSession protocol;
    It930xLnbPower lnb;
    Mlt5PeLnbPowerCoordinator lnb_power;
    Mlt5PeTunerBackend tuner;
    std::unique_ptr<Q3U4StreamDataPlane> stream;
};

class SingleReceiverFamily final : public Px4Family {
public:
    SingleReceiverFamily(std::unique_ptr<Q3U4Runtime> runtime, bool allow_lnb_power) noexcept
        : Px4Family(std::move(runtime)),
          device(runtime_->dev1()), bridge(device, 3U), power(device),
          frontend(bridge, device, power, time_, runtime_->model(), allow_lnb_power),
          card_hardware(device), card_session(card_hardware, time_), protocol(card_session)
    {
    }
    TunerServiceBackend& tuner_backend() noexcept override { return frontend; }
    CardServiceBackend& card_backend() noexcept override { return frontend; }
    CardProtocolSession& card_protocol() noexcept override { return protocol; }
    TunerStreamControl* stream_control() noexcept override { return stream.get(); }

    It930xController device;
    It930xBridgeI2cMaster bridge;
    It930xSingleReceiverPower power;
    SingleReceiverFrontend frontend;
    It930xCardHardware card_hardware;
    CardSession card_session;
    NativeCardProtocolSession protocol;
    std::unique_ptr<Q3U4StreamDataPlane> stream;
};

// Publishes the control/data endpoints of the owned context and runs the
// foreground loop until a stop signal or loop failure.  Returns the process
// exit status, or ends the process when the whole-context cleanup misses its
// budget.
int serve(const Px4dArguments& arguments, std::unique_ptr<Px4Family> family) noexcept
{
    // The runtime stays owned by the family, which the context owns until its
    // cleanup has quiesced; it is not used after close().
    const Q3U4Runtime& runtime = family->runtime();
    auto assembled = EnclosureContext::assemble(
        std::move(family),
        std::unique_ptr<TunerNonceSource>(new (std::nothrow) PlatformTunerNonceSource()));
    if (!assembled) {
        std::fprintf(stderr, "strict capabilities: %s\n", error_string(assembled.error()));
        return exit_status(assembled.error());
    }
    EnclosureContext& owned = *assembled.value();
    const std::string base_serial(runtime.base_serial());
    std::string resolved_instance;
    const auto physical_identity = automatic_px4_identity(
        runtime.model(), runtime.base_serial(),
        {runtime.bridge_observation(0U), runtime.bridge_observation(1U)},
        runtime.bridge_count(), runtime.serial_unique_in_scan().value_or(false));
    if (arguments.instance.empty()) {
        if (!physical_identity) return exit_status(physical_identity.error());
        const auto automatic = dtv::userland::identity::auto_instance(physical_identity.value());
        if (!automatic) return exit_status(automatic.error());
        resolved_instance.assign(automatic.value().bytes.data(), automatic.value().length);
    } else {
        const auto canonical = dtv::userland::identity::canonicalize_instance(
            arguments.instance);
        if (!canonical) return exit_status(Error::INVALID_ARGUMENT);
        resolved_instance.assign(canonical.value().bytes.data(), canonical.value().length);
    }
    const char* runtime_directory = arguments.runtime_directory.empty() ?
                                        nullptr : arguments.runtime_directory.c_str();
#if !defined(_WIN32)
    std::array<char, kStoredPathCapacity> automatic_runtime_directory{};
    if (runtime_directory == nullptr) {
        const auto selected = select_publish_runtime_root();
        if (!selected) return exit_status(selected.error());
        automatic_runtime_directory = selected.value();
        runtime_directory = automatic_runtime_directory.data();
    }
#endif
    const char* instance = resolved_instance.c_str();
    std::string lease_identity = base_serial;
    if (physical_identity &&
        physical_identity.value().kind == dtv::userland::identity::IdentityKind::usb_topology) {
        const auto automatic = dtv::userland::identity::auto_instance(physical_identity.value());
        if (!automatic) return exit_status(automatic.error());
        lease_identity.assign(automatic.value().bytes.data(), automatic.value().length);
    } else if (physical_identity &&
               physical_identity.value().kind == dtv::userland::identity::IdentityKind::serial) {
        const auto& serial = physical_identity.value().serial;
        lease_identity.assign(serial.bytes.data(), serial.length);
    }
    EnclosurePublication publication{};
    if (runtime_directory != nullptr) publication.runtime_directory = runtime_directory;
    publication.instance = instance;
    publication.lease_identity = lease_identity;
    publication.access =
        arguments.group ? EndpointAccess::shared_group : EndpointAccess::private_user;
    const auto reserved = owned.reserve_namespace(publication);
    if (!reserved) {
        std::fprintf(stderr, "serial endpoint: %s\n", error_string(reserved.error()));
        return exit_status(reserved.error());
    }
    // One fresh random epoch per daemon context.
    PlatformTunerNonceSource epoch_source;
    const auto epoch = epoch_source.generate();
    if (!epoch) {
        std::fprintf(stderr, "context epoch: %s\n", error_string(epoch.error()));
        return exit_status(epoch.error());
    }
    Px4ContextInput context_input{};
    context_input.model = runtime.model();
    context_input.base_serial = base_serial;
    context_input.bridges = {runtime.bridge_observation(0U), runtime.bridge_observation(1U)};
    context_input.product_version = PX4_USERLAND_PRODUCT_VERSION;
    context_input.instance = instance;
    context_input.epoch = epoch.value();
    context_input.serial_unique_in_scan = runtime.serial_unique_in_scan();
    std::unique_ptr<ControlContext> context(new (std::nothrow) ControlContext());
    if (!context) {
        std::fprintf(stderr, "control context: %s\n", error_string(Error::INTERNAL));
        return exit_status(Error::INTERNAL);
    }
    const auto built = build_px4_control_context(context_input, owned.tuner_service(), *context);
    if (!built) {
        std::fprintf(stderr, "control context: %s\n", error_string(built.error()));
        return exit_status(built.error());
    }
    context->verbose = arguments.verbose;
    const auto published = owned.publish(*context);
    if (!published) {
        std::fprintf(stderr, "control endpoint: %s\n", error_string(published.error()));
        return exit_status(published.error());
    }
    if (!px4::userland::px4d::install_signal_handlers()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }
#if defined(_WIN32)
    px4::userland::cli::StdinEofMonitor eof_monitor;
    if (arguments.exit_on_stdin_eof &&
        !eof_monitor.start(::GetStdHandle(STD_INPUT_HANDLE), &on_stdin_eof, nullptr)) {
        std::fprintf(stderr, "stdin monitor setup failed\n");
        return 70;
    }
#endif

#if defined(_WIN32)
    std::fprintf(stderr, "px4d ready: device=%s endpoint=%ls\n",
                 base_serial.c_str(), owned.endpoint_path());
#else
    std::fprintf(stderr, "px4d ready: device=%s endpoint=%s\n",
                 base_serial.c_str(), owned.endpoint_path());
#endif
    Error loop_error = Error::OK;
    while (!px4::userland::px4d::stop_requested()) {
        const auto polled = owned.poll_once(Timeout{100U});
        if (!polled) {
            loop_error = polled.error();
            break;
        }
    }
    // Whole-context cleanup: the decision below takes no cleanup lock and does
    // no I/O.  On an expired budget the graph is still held by the cleanup
    // thread and the process ends here, without diagnostics or destructors.
    const ContextCloseResult closed = owned.close(kWholeContextCloseBudget);
    if (!closed.quiesced) px4::userland::px4d::exit_after_close_deadline(kCloseDeadlineExitStatus);
    if (closed.error != Error::OK) {
        std::fprintf(stderr, "shutdown cleanup: %s\n", error_string(closed.error));
        if (loop_error == Error::OK) loop_error = closed.error;
    }
#if defined(_WIN32)
    eof_monitor.stop();
    // The serial namespace lease was released by the quiesced cleanup, so a
    // console/termination handler cannot let the process exit while the
    // private endpoint namespace is still held.
    px4::userland::px4d::notify_cleanup_complete();
#endif
    if (loop_error != Error::OK) {
        std::fprintf(stderr, "px4d stopped: %s\n", error_string(loop_error));
    }
    return exit_status(loop_error);
}

int run_q3u4(const Px4dArguments& arguments, std::unique_ptr<Q3U4Runtime> runtime,
             const FirmwareImage& firmware) noexcept
{
    const DeviceModel model = runtime->model();
    std::unique_ptr<Q3U4Family> family(new (std::nothrow) Q3U4Family(
        std::move(runtime),
        arguments.allow_lnb_power && device_profile(model).supports_lnb_15v));
    if (!family) return exit_status(Error::INTERNAL);
    const auto initialized1 = family->dev1.initialize_q3u4(
        firmware, InitializationPolicy::accept_cold_or_warm, model);
    if (!initialized1) {
        std::fprintf(stderr, "device 1 initialize: %s\n",
                     error_string(initialized1.error()));
        return exit_status(initialized1.error());
    }
    const auto initialized2 = family->dev2.initialize_q3u4(
        firmware, InitializationPolicy::accept_cold_or_warm, model);
    if (!initialized2) {
        std::fprintf(stderr, "device 2 initialize: %s\n",
                     error_string(initialized2.error()));
        return exit_status(initialized2.error());
    }
    auto stream = Q3U4StreamDataPlane::create(family->runtime_dev1(), family->runtime_dev2());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    family->stream = std::move(stream.value());
    return serve(arguments, std::move(family));
}

int run_w3u4(const Px4dArguments& arguments, std::unique_ptr<Q3U4Runtime> runtime,
             const FirmwareImage& firmware) noexcept
{
    const DeviceModel model = runtime->model();
    std::unique_ptr<W3U4Family> family(new (std::nothrow) W3U4Family(
        std::move(runtime),
        arguments.allow_lnb_power && device_profile(model).supports_lnb_15v));
    if (!family) return exit_status(Error::INTERNAL);
    const auto initialized = family->device.initialize_q3u4(
        firmware, InitializationPolicy::accept_cold_or_warm, model);
    if (!initialized) {
        std::fprintf(stderr, "device initialize: %s\n", error_string(initialized.error()));
        return exit_status(initialized.error());
    }
    auto stream = Q3U4StreamDataPlane::create_w3u4(family->runtime_dev1());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    family->stream = std::move(stream.value());
    return serve(arguments, std::move(family));
}

int run_mlt5pe(const Px4dArguments& arguments, std::unique_ptr<Q3U4Runtime> runtime,
               const FirmwareImage& firmware,
               std::uint8_t receiver_count = ipc::kMlt5PeReceiverCount,
               bool satellite_supported = true) noexcept
{
    const DeviceModel model = runtime->model();
    std::unique_ptr<Mlt5PeFamily> family(new (std::nothrow) Mlt5PeFamily(
        std::move(runtime),
        arguments.allow_lnb_power && device_profile(model).supports_lnb_15v, receiver_count,
        satellite_supported));
    if (!family) return exit_status(Error::INTERNAL);
    const auto initialized = family->device.initialize_mlt_family(firmware, model);
    if (!initialized) {
        std::fprintf(stderr, "device initialize: %s\n", error_string(initialized.error()));
        return exit_status(initialized.error());
    }
    auto stream = Q3U4StreamDataPlane::create_mlt_family(family->runtime_dev1(), model);
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    family->stream = std::move(stream.value());
    return serve(arguments, std::move(family));
}

int run_single_receiver(const Px4dArguments& arguments, std::unique_ptr<Q3U4Runtime> runtime,
                        const FirmwareImage& firmware) noexcept
{
    const DeviceModel model = runtime->model();
    std::unique_ptr<SingleReceiverFamily> family(new (std::nothrow) SingleReceiverFamily(
        std::move(runtime), arguments.allow_lnb_power));
    if (!family) return exit_status(Error::INTERNAL);
    const auto initialized = family->device.initialize_single_receiver(firmware, model);
    if (!initialized) {
        std::fprintf(stderr, "device initialize: %s\n", error_string(initialized.error()));
        return exit_status(initialized.error());
    }
    auto stream = Q3U4StreamDataPlane::create_single_receiver(family->runtime_dev1(), model);
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    family->stream = std::move(stream.value());
    return serve(arguments, std::move(family));
}

}  // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    const std::vector<std::string> owned_argv =
        px4::userland::cli::windows_argv_utf8(argc, argv);
    std::vector<const char*> argv_views;
    argv_views.reserve(owned_argv.size());
    for (const std::string& value : owned_argv) argv_views.push_back(value.c_str());
    const Px4dArguments arguments =
        parse_px4d_arguments(static_cast<int>(argv_views.size()), argv_views.data());
#else
    const Px4dArguments arguments =
        parse_px4d_arguments(argc, const_cast<const char* const*>(argv));
#endif
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
    if (arguments.version) {
        std::printf("px4d %s\n", PX4_USERLAND_PRODUCT_VERSION);
        return 0;
    }
    if (arguments.models) {
        const std::string output = tools::format_supported_models_text();
        if (output.empty() ||
            std::fwrite(output.data(), 1U, output.size(), stdout) != output.size() ||
            std::fflush(stdout) != 0) {
            std::fprintf(stderr, "models: catalog output failed\n");
            return 70;
        }
        return 0;
    }
    if (arguments.list_json) return list_devices(true);

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
        const std::vector<std::string> usb_paths(
            arguments.usb_paths.begin(),
            arguments.usb_paths.begin() +
                static_cast<std::ptrdiff_t>(arguments.usb_path_count));
        runtime = Q3U4Runtime::open_native(arguments.device, usb_paths);
    }
    if (!runtime) {
        std::fprintf(stderr, "device open: %s\n", error_string(runtime.error()));
        bool ambiguous_serial = false;
        if (mode == Px4dOpenMode::native && arguments.usb_path_count == 0U &&
            runtime.error() == Error::INVALID_ARGUMENT) {
            const auto grouping = Q3U4Runtime::enumerate_native();
            if (grouping) {
                GroupingResult matches;
                for (const Q3U4Group& group : grouping.value().groups) {
                    if (group.base_serial == arguments.device) matches.groups.push_back(group);
                }
                ambiguous_serial = matches.groups.size() > 1U ||
                    std::any_of(matches.groups.begin(), matches.groups.end(),
                                [](const Q3U4Group& group) {
                                    return group.status == GroupStatus::duplicate;
                                });
                if (ambiguous_serial) {
                    const std::string candidates = tools::format_device_list(matches);
                    std::fprintf(stderr,
                                 "serial is not uniquely selectable; specify --device usb:... "
                                 "and --instance. Candidates:\n%s",
                                 candidates.c_str());
                }
            }
        }
        if (ambiguous_serial ||
            (mode == Px4dOpenMode::native && arguments.usb_path_count != 0U &&
             runtime.error() == Error::INVALID_ARGUMENT))
            return exit_status(Error::NOT_FOUND);
        return exit_status(runtime.error());
    }
    const DeviceModel model = runtime.value()->model();
    if (arguments.verbose) {
        std::fprintf(stderr, "px4d verbose: USB enclosure opened model=%s functions=%u\n",
                     device_profile(model).name,
                     static_cast<unsigned int>(runtime.value()->bridge_count()));
    }
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
    if (device_profile(model).bridge_count == 1U &&
        !device_profile(model).dual_system &&
        device_profile(model).receiver_count == ipc::kW3U4ReceiverCount) {
        return run_w3u4(arguments, std::move(runtime.value()), firmware.value());
    }
    if (!single_bridge) return run_q3u4(arguments, std::move(runtime.value()), firmware.value());
    const DeviceProfile& profile = device_profile(model);
    if (profile.receiver_count == 1U)
        return run_single_receiver(arguments, std::move(runtime.value()), firmware.value());
    return run_mlt5pe(arguments, std::move(runtime.value()), firmware.value(),
                      profile.receiver_count, profile.dual_system);
}

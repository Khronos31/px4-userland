// SPDX-License-Identifier: GPL-2.0-only

#include "px4/card.h"
#include "px4/card_service.h"
#include "px4/card_t1_adapter.h"
#include "px4/firmware.h"
#include "px4/it930x.h"
#include "px4/libusb_transport.h"
#include "px4/logging.h"
#include "px4/platform_sleep.h"
#include "px4/q3u4_stream.h"
#include "px4/tuner_service.h"

#if defined(_WIN32)
#include "px4/windows_tuner_nonce.h"
#include "px4_windows_args.h"
#include "dtv/userland/host_facade/windows_stdin.h"
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
#include "px4d_listener_error.h"
#include "px4d_receiver_records.h"
#include "px4d_signals.h"

#include <dtv/userland/host_listener_context/enclosure_context.h>
#include <dtv/userland/host_runtime/family.h>
#include <dtv/userland/records.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;

#if defined(_WIN32)
using PlatformTunerNonceSource = px4::userland::ipc::windows::WindowsTunerNonceSource;
#else
using PlatformTunerNonceSource = px4::userland::ipc::posix::PosixTunerNonceSource;
#endif

void usage() noexcept
{
    std::printf(
        "usage:\n"
        "  px4d --device serial:SERIAL|usb:BUS-PORT[,BUS-PORT...] --firmware PATH "
        "[--instance TOKEN] "
        "[--runtime-dir PATH] [--group] [--allow-lnb-power]\n"
        "  px4d --fd FD [--fd FD] [--device serial:SERIAL] --firmware PATH "
        "[--instance TOKEN] [--runtime-dir PATH] [--group] [--allow-lnb-power]\n"
        "  px4d --list-json\n"
        "  px4d --models\n"
        "  px4d --version\n"
        "\n"
        "  serial: selects an observed PX4 serial; usb: selects one or two\n"
        "  current USB topology paths.  Topology selection requires --instance.\n"
        "  Pass one --fd per USB device: two for paired models, one for\n"
        "  single-device models.\n"
        "\n"
        "  -h, --help         print this help and exit\n"
        "  -F, --firmware PATH  load firmware from PATH\n"
        "  -v, --verbose      print debug startup diagnostics to stderr\n"
        "  --allow-lnb-power  permit explicit ISDB-S 15 V requests; default off\n"
#if defined(_WIN32)
        "  --exit-on-stdin-eof  exit with cleanup when stdin reaches EOF (Windows\n"
        "                     cooperative parent stop)\n"
#endif
        "  --list-json        print connected supported enclosures as JSON and exit;\n"
        "                     read-only, needs neither firmware nor a daemon\n"
        "  --models           print supported models and their USB IDs, one per\n"
        "                     line, and exit; static, no USB access\n"
        "  --version          print this program's version and exit\n");
}

void log_to_stderr(const LogRecord& record, void*) noexcept
{
    std::fprintf(stderr, "px4d %s: %.*s\n", log_level_string(record.level),
                 static_cast<int>(record.message.size()), record.message.data());
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
    case Error::BUSY:
    case Error::RESOURCE_LIMIT: return 4;
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

// `px4d --models`: static supported-model table; touches no USB device.
int list_models() noexcept
{
    const std::string output = tools::format_model_list();
    if (std::fwrite(output.data(), 1U, output.size(), stdout) != output.size() ||
        std::fflush(stdout) != 0) {
        std::fprintf(stderr, "models: write to stdout failed\n");
        return exit_status(Error::INTERNAL);
    }
    return 0;
}

// `px4d --list-json` (SPEC 4.6).  Native enumeration only reads descriptors and
// the serial string; it never claims an interface, so it is safe while other
// px4d instances own their enclosures.
int list_devices() noexcept
{
    const auto grouping = Q3U4Runtime::enumerate_native();
    if (!grouping) {
        std::fprintf(stderr, "enumeration failed: %s\n", error_string(grouping.error()));
        return exit_status(grouping.error());
    }
    const auto formatted = tools::format_device_list_json(
        grouping.value(), PX4_USERLAND_PRODUCT_VERSION);
    if (!formatted) {
        std::fprintf(stderr, "discovery JSON: %s\n", error_string(formatted.error()));
        return exit_status(formatted.error());
    }
    const std::string& output = formatted.value();
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

class Px4EnclosureFamily final : public dtv::userland::host_runtime::EnclosureFamily {
public:
    Px4EnclosureFamily(TunerServiceBackend& tuner_backend,
                       CardServiceBackend& card_backend,
                       CardProtocolSession& card_protocol,
                       TunerServiceTime& time,
                       Q3U4StreamDataPlane& stream) noexcept
        : tuner_backend_(tuner_backend), card_backend_(card_backend),
          card_protocol_(card_protocol), time_(time), stream_(stream)
    {
    }

    Result<void> shutdown() noexcept override { return stream_.shutdown(); }
    TunerServiceBackend& tuner_backend() noexcept override { return tuner_backend_; }
    CardServiceBackend& card_backend() noexcept override { return card_backend_; }
    CardProtocolSession& card_protocol() noexcept override { return card_protocol_; }
    TunerServiceTime& time() noexcept override { return time_; }
    TunerStreamControl* stream_control() noexcept override { return &stream_; }

private:
    TunerServiceBackend& tuner_backend_;
    CardServiceBackend& card_backend_;
    CardProtocolSession& card_protocol_;
    TunerServiceTime& time_;
    Q3U4StreamDataPlane& stream_;
};

template <std::size_t N>
bool set_wire_text(dtv::userland::wire::WString<N>& target,
                   std::string_view value) noexcept
{
    if (value.size() > N) return false;
    target.length = static_cast<std::uint16_t>(value.size());
    std::copy(value.begin(), value.end(), target.bytes.begin());
    return true;
}

std::string usb_path(const UsbLocation& location)
{
    if (!location.has_bus || location.port_count == 0U) return {};
    std::string value = std::to_string(location.bus);
    for (std::size_t index = 0U; index < location.port_count; ++index) {
        value.push_back(index == 0U ? '-' : '.');
        value += std::to_string(location.port_path[index]);
    }
    return value;
}

dtv::userland::wire::ListResponse make_canonical_list(
    const DeviceProfile& profile, std::string_view base_serial,
    std::uint8_t bridge_count, bool dual_system, bool serial_unique,
    const Q3U4Runtime& runtime,
    const dtv::userland::wire::Epoch& epoch,
    const dtv::userland::wire::Producer& producer) noexcept
{
    namespace wire = dtv::userland::wire;
    wire::ListResponse list;
    list.epoch = epoch;
    list.producer = producer;
    list.snapshot.sequence = 1U;
    list.topology.state = wire::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = bridge_count;
    list.enclosure.present = true;
    list.enclosure.value.id = 0U;
    list.enclosure.value.model.present = true;
    (void)set_wire_text(list.enclosure.value.model.value.name, profile.name);
    list.enclosure.value.identity.serial_state = wire::IdentitySerialState::present;
    list.enclosure.value.identity.serial.present = true;
    (void)set_wire_text(list.enclosure.value.identity.serial.value, base_serial);
    list.enclosure.value.identity.uniqueness = serial_unique
        ? wire::Uniqueness::unique_in_scan : wire::Uniqueness::unknown;
    list.enclosure.value.limits.max_concurrent_streams.present = true;
    list.enclosure.value.limits.max_concurrent_streams.value = profile.receiver_count;

    list.functions.count = bridge_count;
    for (std::uint8_t index = 0U; index < bridge_count; ++index) {
        auto& function = list.functions.items[index];
        function.id = static_cast<std::uint16_t>(index + 1U);
        function.vid = kQ3U4VendorId;
        function.pid = profile.product_id;
        function.mode = wire::Mode::runtime;
        function.serial_state = wire::SerialState::present;
        std::string function_serial(base_serial);
        if (bridge_count == 2U) function_serial.push_back(static_cast<char>('1' + index));
        function.serial.present = true;
        (void)set_wire_text(function.serial.value, function_serial);
        function.model.present = true;
        (void)set_wire_text(function.model.value.name, profile.name);
        function.identification_reason = wire::IdentificationReason::none;
        function.present = true;
        const UsbLocation location = runtime.usb_location(index);
        if (location.has_bus) {
            function.location.bus.present = true;
            function.location.bus.value = location.bus;
        }
        if (location.has_address) {
            function.location.address.present = true;
            function.location.address.value = location.address;
        }
        const std::string path = usb_path(location);
        if (!path.empty()) {
            function.location.usb_path.present = true;
            (void)set_wire_text(function.location.usb_path.value, path);
        }
    }

    const auto records = px4_product::receiver_records(profile.receiver_count, dual_system);
    if (records) {
        list.receivers.present = true;
        list.receivers.value.count = profile.receiver_count;
        for (std::uint8_t index = 0U; index < profile.receiver_count; ++index) {
            const auto& record = records.value()[index];
            auto& receiver = list.receivers.value.items[index];
            receiver.id = record.global_id;
            receiver.systems.mask = record.system == ipc::System::ISDB_T ? 1U :
                                    record.system == ipc::System::ISDB_S ? 2U : 3U;
            receiver.binding.present = true;
            receiver.binding.value.function_id = record.dev_id;
            receiver.binding.value.local_id = record.local_id;
            receiver.capabilities.measurement_mask = wire::kMetricMaskLock;
            if (profile.supports_lnb_15v && record.system != ipc::System::ISDB_T) {
                receiver.capabilities.controls |= wire::kControlBitLnb;
                receiver.capabilities.lnb.values.count = 2U;
                receiver.capabilities.lnb.values.items[0U] = 0U;
                receiver.capabilities.lnb.values.items[1U] = 15U;
            }
        }
    }
    list.readers.present = true;
    list.readers.value.count = 1U;
    list.readers.value.items[0U].id = 0U;
    list.readers.value.items[0U].binding.present = true;
    list.readers.value.items[0U].binding.value.function_id = 1U;
    list.readers.value.items[0U].binding.value.local_id = 0U;
    return list;
}

int serve_core(const Px4dArguments& arguments, const std::string& base_serial,
               DeviceModel model, const Q3U4Runtime& runtime,
               TunerServiceBackend& tuner_backend,
               CardServiceBackend& card_backend, CardProtocolSession& card_protocol,
               TunerServiceTime& time, Q3U4StreamDataPlane& stream,
               std::uint8_t bridge_count, bool dual_system = false) noexcept
{
    namespace listener = dtv::userland::host_listener_context;
    namespace host = dtv::userland::host_runtime;
    namespace wire = dtv::userland::wire;
    auto nonce_source = std::make_unique<PlatformTunerNonceSource>();
    const auto epoch_seed = nonce_source->generate();
    if (!epoch_seed) return exit_status(epoch_seed.error());
    const wire::Epoch epoch = epoch_seed.value();
    wire::Producer producer;
    (void)set_wire_text(producer.product, "px4-userland");
    (void)set_wire_text(producer.version, PX4_USERLAND_PRODUCT_VERSION);
    producer.core_version.present = true;
    (void)set_wire_text(producer.core_version.value, DTV_USERLAND_CORE_VERSION);

    listener::ContextConfig config;
    config.runtime_directory = arguments.runtime_directory;
    config.product = "px4-userland";
    config.instance = px4d_selected_instance(arguments, base_serial);
    config.access = arguments.group ? host::EndpointAccess::shared_group
                                    : host::EndpointAccess::private_user;
    config.server.epoch = epoch;
    config.server.producer = producer;
    config.server.features = wire::kFeatureStateEvent | wire::kFeatureCardService |
                             wire::kFeaturePidFilter;
    config.canonical_list = make_canonical_list(
        device_profile(model), base_serial, bridge_count, dual_system,
        px4d_open_mode(arguments) == Px4dOpenMode::native &&
            arguments.usb_path_count == 0U,
        runtime,
        epoch, producer);
    config.context.epoch = epoch;
    config.context.state = wire::ContextState::ready;
    config.context.instance.present = true;
    (void)set_wire_text(config.context.instance.value, config.instance);
    for (std::size_t index = 0U; index < 8U; ++index)
        config.stream_engine_epoch |= static_cast<std::uint64_t>(epoch[index]) << (index * 8U);
    if (config.stream_engine_epoch == 0U) config.stream_engine_epoch = 1U;
    config.stream_arm_budget_ms = 5000U;

    auto family = std::make_unique<Px4EnclosureFamily>(
        tuner_backend, card_backend, card_protocol, time, stream);
    auto context = listener::EnclosureContext::assemble(
        std::move(family), std::move(nonce_source), config);
    if (!context) {
        std::fprintf(stderr, "Core enclosure context: %s\n", error_string(context.error()));
        return exit_status(context.error());
    }
    listener::ListenerDiagnostic listener_diagnostic{};
    const auto reserved = context.value()->reserve_namespace(&listener_diagnostic);
    if (!reserved) {
        std::fprintf(stderr, "%s\n", tools::format_listener_error(
            reserved.error(), listener_diagnostic, "reservation").c_str());
        return exit_status(reserved.error());
    }
    const auto published = context.value()->publish();
    if (!published) {
        std::fprintf(stderr, "Core endpoint publication: %s\n",
                     error_string(published.error()));
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
    std::fprintf(stderr, "px4d ready: device=%s\n", base_serial.c_str());
    Error loop_error = Error::OK;
    while (!px4::userland::px4d::stop_requested()) {
        const auto polled = context.value()->poll_once(Timeout{100U});
        if (!polled) {
            std::fprintf(stderr, "Core context poll: %s\n", error_string(polled.error()));
            loop_error = polled.error();
            break;
        }
    }
    for (;;) {
        const auto closed = context.value()->close(Timeout{100U});
        if (!closed) {
            std::fprintf(stderr, "Core context cleanup: %s\n",
                         error_string(closed.error()));
            if (loop_error == Error::OK) loop_error = closed.error();
            break;
        }
        if (closed.value() == listener::CloseState::quiesced) break;
    }
#if defined(_WIN32)
    eof_monitor.stop();
    px4::userland::px4d::notify_cleanup_complete();
#endif
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
    const auto initialized1 = dev1.initialize_q3u4(
        firmware, InitializationPolicy::accept_cold_or_warm, runtime.model());
    if (!initialized1) {
        std::fprintf(stderr, "device 1 initialize: %s\n",
                     error_string(initialized1.error()));
        return exit_status(initialized1.error());
    }
    const auto initialized2 = dev2.initialize_q3u4(
        firmware, InitializationPolicy::accept_cold_or_warm, runtime.model());
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
    CardT1HardwareAdapter card_hardware_adapter(card_hardware);
    CardT1TimeAdapter card_time_adapter(time);
    CardT1Session card_session(card_hardware_adapter, card_time_adapter);
    CardT1ProtocolSession protocol(card_session);
    It930xLnbPower dev1_lnb(dev1);
    It930xLnbPower dev2_lnb(dev2);
    Q3U4LnbPowerCoordinator lnb_power(
        dev1_lnb, dev2_lnb,
        arguments.allow_lnb_power && device_profile(runtime.model()).supports_lnb_15v);
    Q3U4FrontendTunerBackend tuner_backend(enclosure, lnb_power);
    const auto stream = Q3U4StreamDataPlane::create(runtime.dev1(), runtime.dev2());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    return serve_core(arguments, base_serial, runtime.model(), runtime, tuner_backend,
                      backend, protocol, time, *stream.value(), 2U);
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
    Result<TunerHzProgramming> tune_terrestrial_hz(
        std::uint8_t receiver, std::uint64_t frequency_hz,
        std::uint32_t timeout_ms) noexcept override
    {
        return inner_.tune_terrestrial_hz(receiver, frequency_hz, timeout_ms);
    }
    Result<TunerHzProgramming> tune_satellite_hz(
        std::uint8_t receiver, std::uint64_t frequency_hz,
        std::uint32_t timeout_ms) noexcept override
    {
        return inner_.tune_satellite_hz(receiver, frequency_hz, timeout_ms);
    }
    TunerStrictProfile strict_profile(std::uint8_t receiver,
                                      ipc::System system) const noexcept override
    {
        return receiver_supports(receiver, system)
            ? inner_.strict_profile(receiver, system) : TunerStrictProfile{};
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
    Result<void> set_independent_lnb_power(bool enabled) noexcept override
    {
        return inner_.set_independent_lnb_power(enabled);
    }
    dtv::userland::wire::Optional<bool> independent_lnb_power_state() noexcept override
    {
        return inner_.independent_lnb_power_state();
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
    const auto initialized = device.initialize_q3u4(
        firmware, InitializationPolicy::accept_cold_or_warm, runtime.model());
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
    CardT1HardwareAdapter card_hardware_adapter(card_hardware);
    CardT1TimeAdapter card_time_adapter(time);
    CardT1Session card_session(card_hardware_adapter, card_time_adapter);
    CardT1ProtocolSession protocol(card_session);
    It930xLnbPower lnb(device);
    Q3U4LnbPowerCoordinator lnb_power(
        lnb, lnb,
        arguments.allow_lnb_power && device_profile(runtime.model()).supports_lnb_15v);
    W3U4TunerBackend tuner_backend(enclosure, lnb_power);
    const auto stream = Q3U4StreamDataPlane::create_w3u4(runtime.dev1());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    return serve_core(arguments, base_serial, runtime.model(), runtime, tuner_backend,
                      backend, protocol, time, *stream.value(), 1U);
}

int run_mlt5pe(const Px4dArguments& arguments, Q3U4Runtime& runtime,
               const FirmwareImage& firmware, const std::string& base_serial,
               std::uint8_t receiver_count = ipc::kMlt5PeReceiverCount,
               bool satellite_supported = true) noexcept
{
    It930xController device(runtime.dev1());
    const auto initialized = device.initialize_mlt_family(firmware, runtime.model());
    if (!initialized) {
        std::fprintf(stderr, "device initialize: %s\n", error_string(initialized.error()));
        return exit_status(initialized.error());
    }

    DaemonTime time;
    It930xBridgeI2cMaster bus1(device, 1U);
    It930xBridgeI2cMaster bus3(device, 3U);
    It930xBackendPower power(device);
    It930xPsbPurger purger(device);
    Mlt5PeFrontend frontend(bus1, bus3, power, time, &purger, receiver_count,
                           runtime.model());
    Mlt5PeCardBackend backend(device, frontend);
    It930xCardHardware card_hardware(device);
    CardT1HardwareAdapter card_hardware_adapter(card_hardware);
    CardT1TimeAdapter card_time_adapter(time);
    CardT1Session card_session(card_hardware_adapter, card_time_adapter);
    CardT1ProtocolSession protocol(card_session);
    It930xLnbPower lnb(device);
    Mlt5PeLnbPowerCoordinator lnb_power(
        lnb, arguments.allow_lnb_power && device_profile(runtime.model()).supports_lnb_15v);
    Mlt5PeTunerBackend tuner_backend(frontend, lnb_power, receiver_count,
                                     satellite_supported, runtime.model());
    const auto stream = Q3U4StreamDataPlane::create_mlt_family(runtime.dev1(), runtime.model());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    return serve_core(arguments, base_serial, runtime.model(), runtime, tuner_backend,
                      backend, protocol, time, *stream.value(), 1U,
                      satellite_supported || receiver_count > 1U);
}

int run_single_receiver(const Px4dArguments& arguments, Q3U4Runtime& runtime,
                        const FirmwareImage& firmware, const std::string& base_serial) noexcept
{
    It930xController device(runtime.dev1());
    const auto initialized = device.initialize_single_receiver(firmware, runtime.model());
    if (!initialized) {
        std::fprintf(stderr, "device initialize: %s\n", error_string(initialized.error()));
        return exit_status(initialized.error());
    }
    DaemonTime time;
    It930xBridgeI2cMaster bridge(device, 3U);
    It930xSingleReceiverPower power(device);
    SingleReceiverFrontend frontend(bridge, device, power, time, runtime.model(),
                                    arguments.allow_lnb_power);
    It930xCardHardware card_hardware(device);
    CardT1HardwareAdapter card_hardware_adapter(card_hardware);
    CardT1TimeAdapter card_time_adapter(time);
    CardT1Session card_session(card_hardware_adapter, card_time_adapter);
    CardT1ProtocolSession protocol(card_session);
    const auto stream = Q3U4StreamDataPlane::create_single_receiver(runtime.dev1(), runtime.model());
    if (!stream) {
        std::fprintf(stderr, "stream data plane: %s\n", error_string(stream.error()));
        return exit_status(stream.error());
    }
    const bool dual = device_profile(runtime.model()).dual_system;
    return serve_core(arguments, base_serial, runtime.model(), runtime, frontend,
                      frontend, protocol, time, *stream.value(), 1U, dual);
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
    if (arguments.models) return list_models();
    if (arguments.version) {
        std::printf("px4d %s\n", PX4_USERLAND_PRODUCT_VERSION);
        return 0;
    }
    if (arguments.list_json) return list_devices();

    const std::string_view requested_base_serial = arguments.device.empty() || arguments.usb_selector
        ? std::string_view{} : std::string_view(arguments.device).substr(7U);

    Logger logger(arguments.verbose ? LogLevel::debug : LogLevel::info, log_to_stderr);
    logger.log(LogLevel::debug, "loading firmware");
    FirmwareProvider firmware_provider(arguments.firmware);
    const auto firmware = firmware_provider.load();
    if (!firmware) {
        std::fprintf(stderr, "%s\n",
                     tools::format_firmware_error(firmware.error()).c_str());
        return exit_status(firmware.error());
    }
    Result<std::unique_ptr<Q3U4Runtime>> runtime =
        Result<std::unique_ptr<Q3U4Runtime>>::failure(Error::INTERNAL);
    const Px4dOpenMode mode = px4d_open_mode(arguments);
    logger.log(LogLevel::debug, "opening USB enclosure");
    if (mode == Px4dOpenMode::file_descriptors) {
        const std::vector<int> file_descriptors(
            arguments.file_descriptors.begin(),
            arguments.file_descriptors.begin() +
                static_cast<std::ptrdiff_t>(arguments.file_descriptor_count));
        // open_fds duplicates these caller-owned descriptors.  The runtime
        // owns and closes only its duplicates; termux-usb/UsbManager owners
        // remain responsible for the originals.
        runtime = Q3U4Runtime::open_fds(file_descriptors, requested_base_serial);
    } else {
        const std::vector<std::string> usb_paths(
            arguments.usb_paths.begin(),
            arguments.usb_paths.begin() +
                static_cast<std::ptrdiff_t>(arguments.usb_path_count));
        runtime = Q3U4Runtime::open_native(requested_base_serial, usb_paths);
    }
    if (!runtime) {
        if (arguments.usb_selector && runtime.error() == Error::INVALID_ARGUMENT) {
            std::fprintf(stderr, "%s\n",
                         tools::format_usb_selection_error(runtime.error()).c_str());
        } else {
            std::fprintf(stderr, "device open: %s\n", error_string(runtime.error()));
        }
        if (mode == Px4dOpenMode::native && !arguments.usb_selector &&
            runtime.error() == Error::INVALID_ARGUMENT) {
            const auto grouping = Q3U4Runtime::enumerate_native();
            if (grouping) {
                GroupingResult matches;
                for (const Q3U4Group& group : grouping.value().groups) {
                    if (group.base_serial == requested_base_serial) matches.groups.push_back(group);
                }
                if (!matches.groups.empty()) {
                    const std::string candidates = tools::format_device_list(matches);
                    std::fprintf(stderr,
                                 "serial is not uniquely selectable; specify "
                                 "--device usb:BUS-PORT[,BUS-PORT] and --instance. "
                                 "Candidates:\n%s",
                                 candidates.c_str());
                }
            }
        }
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
    logger.log(LogLevel::debug, "starting receiver and card services");
    if (device_profile(model).bridge_count == 1U &&
        !device_profile(model).dual_system &&
        device_profile(model).receiver_count == ipc::kW3U4ReceiverCount) {
        return run_w3u4(arguments, *runtime.value(), firmware.value(), base_serial);
    }
    if (!single_bridge) return run_q3u4(arguments, *runtime.value(), firmware.value(), base_serial);
    const DeviceProfile& profile = device_profile(model);
    if (profile.receiver_count == 1U)
        return run_single_receiver(arguments, *runtime.value(), firmware.value(), base_serial);
    return run_mlt5pe(arguments, *runtime.value(), firmware.value(), base_serial,
                      profile.receiver_count, profile.dual_system);
}

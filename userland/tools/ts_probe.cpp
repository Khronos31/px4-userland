// SPDX-License-Identifier: GPL-2.0-only
#include "frontend_probe_support.h"
#include "bridge_i2c.h"
#include "px4/firmware.h"
#include "px4/it930x.h"
#include "px4/libusb_transport.h"
#include "tagged_ts_demux.h"
#include "ts_probe_support.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;

constexpr int kArgumentFailure = 2;
constexpr int kFirmwareFailure = 3;
constexpr int kOpenFailure = 4;
constexpr int kInitializeFailure = 5;
constexpr int kFrontendFailure = 6;
constexpr int kStreamFailure = 7;
constexpr int kFileFailure = 8;
constexpr int kStopRequested = 9;
constexpr int kSignalInstallFailure = 10;

volatile std::sig_atomic_t stop_requested_flag = 0;

void request_stop(int) noexcept
{
    stop_requested_flag = 1;
}

bool stop_requested() noexcept
{
    return stop_requested_flag != 0;
}

bool install_stop_handlers() noexcept
{
    if (std::signal(SIGINT, request_stop) == SIG_ERR) return false;
    if (std::signal(SIGTERM, request_stop) == SIG_ERR) {
        (void)std::signal(SIGINT, SIG_DFL);
        return false;
    }
    return true;
}

class RealDelay final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

Result<bool> demod_lock(void* context) noexcept
{
    return static_cast<Q3U4Frontend*>(context)->is_terrestrial_locked();
}

Result<bool> satellite_demod_lock(void* context) noexcept
{
    return static_cast<Q3U4Frontend*>(context)->is_satellite_locked();
}

std::size_t stdio_write(void* context, const void* data, std::size_t size) noexcept
{
    return std::fwrite(data, 1U, size, static_cast<std::FILE*>(context));
}

std::FILE* open_output_file(const char* path) noexcept
{
#if defined(_MSC_VER)
    std::FILE* file = nullptr;
    return ::fopen_s(&file, path, "wb") == 0 ? file : nullptr;
#else
    return std::fopen(path, "wb");
#endif
}

void usage() noexcept
{
    std::printf("usage: px4-ts-probe {--base BASE | --fd FD --fd FD [--base BASE]} "
                "--firmware PATH --device 1 "
                "--receiver 2 --frequency-khz F --seconds N --output PATH "
                "--tune-terrestrial\n"
                "   or: px4-ts-probe {--base BASE | --fd FD --fd FD [--base BASE]} "
                "--firmware PATH --device 1 "
                "--receiver 0 --frequency-khz F --slot N --symbol-rate 28860 "
                "--rolloff 4 --lnb-voltage 0 --seconds N --output PATH "
                "--tune-satellite\n");
}

void print_i2c_failure(const BridgeI2cFailureDetail& detail) noexcept
{
    if (!detail.valid) return;
    const char* type = detail.type == BridgeI2cRequestType::write ? "write" : "read";
    std::fprintf(stderr,
                 "i2c-failure index=%zu type=%s address=0x%02x write_len=%zu read_len=%zu\n",
                 detail.request_index, type, detail.address, detail.write_length,
                 detail.read_length);
}

void report_failure(const char* stage, Error error,
                    const It930xBridgeI2cMaster* bridge = nullptr,
                    const char* diagnostic = nullptr) noexcept
{
    std::fprintf(stderr, "%s failed: %s", stage, error_string(error));
    if (diagnostic != nullptr) std::fprintf(stderr, " stage=%s", diagnostic);
    std::fputc('\n', stderr);
    if (bridge != nullptr) print_i2c_failure(bridge->failure_detail());
}

void remember_error(Error& first, const Result<void>& result) noexcept
{
    if (first == Error::OK && !result) first = result.error();
}

Result<void> close_frontend(void* context) noexcept
{
    return static_cast<Q3U4Frontend*>(context)->close();
}

struct PowerOffContext final {
    CoupledProbePower* power;
    Q3U4FrontendDelay* delay;
    Q3U4Frontend* frontend;
};

Result<void> power_off(void* context) noexcept
{
    auto& values = *static_cast<PowerOffContext*>(context);
    const auto result = values.power->set_backend_power(false, *values.delay);
    if (!result) values.frontend->record_cleanup_power_off_failure();
    return result;
}

}  // namespace

int main(int argc, char** argv)
{
    const TsProbeArguments arguments = parse_ts_probe_arguments(
        argc, const_cast<const char* const*>(argv));
    if (!arguments.valid) {
        std::fprintf(stderr, "argument error: %.*s\n",
                     static_cast<int>(arguments.error.size()), arguments.error.data());
        return kArgumentFailure;
    }
    if (arguments.help) {
        usage();
        return 0;
    }
    if (!install_stop_handlers()) {
        std::fprintf(stderr, "signal handler install failed\n");
        return kSignalInstallFailure;
    }
    if (stop_requested()) return kStopRequested;

    FirmwareProvider provider(arguments.firmware_path);
    const auto image = provider.load();
    if (!image) {
        report_failure("firmware", image.error());
        return kFirmwareFailure;
    }
    std::printf("firmware loaded\n");
    if (stop_requested()) return kStopRequested;

    const auto opened = [&arguments]() noexcept {
        if (ts_probe_open_mode(arguments) == TsProbeOpenMode::native) {
            return Q3U4Runtime::open_native(arguments.base_serial);
        }
        const std::vector<int> fds(arguments.file_descriptors.begin(),
                                   arguments.file_descriptors.end());
        return Q3U4Runtime::open_fds(fds, arguments.base_serial);
    }();
    if (!opened) {
        report_failure("runtime open", opened.error());
        return kOpenFailure;
    }
    std::printf("runtime opened\n");
    if (stop_requested()) return kStopRequested;

    std::FILE* output = open_output_file(arguments.output_path.c_str());
    if (output == nullptr) {
        std::fprintf(stderr, "output open failed\n");
        return kFileFailure;
    }

    Q3U4Runtime& runtime = *opened.value();
    Transport& transport = runtime.dev1();
    RealDelay delay;
    It930xController dev1(runtime.dev1());
    It930xController dev2(runtime.dev2());
    It930xBackendPower dev1_power(dev1);
    It930xBackendPower dev2_power(dev2);
    CoupledProbePower power(dev1_power, dev2_power);
    It930xBridgeI2cMaster bridge(dev1);
    Q3U4Frontend frontend(bridge, power, delay);
    TsProbeSink sink{stdio_write, output, {}, arguments.receiver};
    TaggedTsDemux demux;

    Error cleanup_error = Error::OK;
    int primary_status = 0;
    PowerOffContext power_context{&power, &delay, &frontend};
    {
        ProbeCleanupGuard cleanup(close_frontend, &frontend, power_off, &power_context,
                                  cleanup_error);
        bool stream_started = false;

        do {
            if (stop_requested()) {
                if (primary_status == 0) primary_status = kStopRequested;
                break;
            }
            const auto init1 = dev1.initialize_q3u4(image.value());
            if (!init1) {
                report_failure("dev1 initialize", init1.error());
                primary_status = kInitializeFailure;
                break;
            }
            if (stop_requested()) {
                if (primary_status == 0) primary_status = kStopRequested;
                break;
            }
            const auto init2 = dev2.initialize_q3u4(image.value());
            if (!init2) {
                report_failure("dev2 initialize", init2.error());
                primary_status = kInitializeFailure;
                break;
            }
            std::printf("dev1/dev2 initialized\n");
            if (stop_requested()) {
                if (primary_status == 0) primary_status = kStopRequested;
                break;
            }
            const auto opened_frontend = arguments.tune_satellite
                ? frontend.open_satellite(arguments.receiver)
                : frontend.open_terrestrial(arguments.receiver);
            if (!opened_frontend) {
                report_failure("frontend open", opened_frontend.error(), &bridge,
                               frontend.diagnostic_stage());
                primary_status = kFrontendFailure;
                break;
            }
            std::printf("frontend opened\n");
            if (stop_requested()) {
                if (primary_status == 0) primary_status = kStopRequested;
                break;
            }
            const auto tuned = arguments.tune_satellite
                ? frontend.tune_satellite(arguments.frequency_khz)
                : frontend.tune_terrestrial(arguments.frequency_khz);
            if (!tuned) {
                report_failure(arguments.tune_satellite ? "satellite tune" : "terrestrial tune",
                               tuned.error(), &bridge, frontend.diagnostic_stage());
                primary_status = kFrontendFailure;
                break;
            }
            std::printf(arguments.tune_satellite ? "satellite tuner locked\n"
                                                 : "terrestrial tuner locked\n");
            if (stop_requested()) {
                if (primary_status == 0) primary_status = kStopRequested;
                break;
            }
            const auto lock = poll_frontend_probe_lock(
                arguments.tune_satellite ? satellite_demod_lock : demod_lock,
                &frontend, delay);
            if (!lock.locked) {
                report_failure("demod lock", lock.error, &bridge,
                               frontend.diagnostic_stage());
                primary_status = kFrontendFailure;
                break;
            }
            std::printf("demod locked\n");
            if (stop_requested()) {
                if (primary_status == 0) primary_status = kStopRequested;
                break;
            }
            if (arguments.tune_satellite) {
                const auto selected = frontend.select_satellite_slot(arguments.slot);
                if (!selected) {
                    report_failure("satellite slot selection", selected.error(), &bridge,
                                   frontend.diagnostic_stage());
                    primary_status = kFrontendFailure;
                    break;
                }
                std::printf("satellite system=ISDB-S receiver=%u frequency-khz=%u slot=%u "
                            "tsid=0x%04x symbol-rate=%u rolloff=%u lnb=0\n",
                            arguments.receiver, arguments.frequency_khz, arguments.slot,
                            frontend.selected_tsid(), arguments.symbol_rate, arguments.rolloff);
            } else {
                std::printf("terrestrial system=ISDB-T receiver=%u frequency-khz=%u\n",
                            arguments.receiver, arguments.frequency_khz);
            }
            PsbPurgeObservation purge_observation;
            const auto purge = dev1.purge_psb(Timeout{2000U}, &purge_observation);
            if (!purge) {
                std::fprintf(stderr,
                             "PSB purge failed: %s attempted=%d completion=%s transferred=%zu\n",
                             error_string(purge.error()),
                             purge_observation.read_attempted ? 1 : 0,
                             error_string(purge_observation.completion_error),
                             purge_observation.transferred);
                primary_status = kStreamFailure;
                break;
            }
            std::printf("PSB purge complete attempted=%d completion=%s transferred=%zu\n",
                        purge_observation.read_attempted ? 1 : 0,
                        error_string(purge_observation.completion_error),
                        purge_observation.transferred);
            if (stop_requested()) {
                if (primary_status == 0) primary_status = kStopRequested;
                break;
            }
            demux.reset();
            const auto pins = arguments.tune_satellite
                ? frontend.start_satellite_capture()
                : frontend.start_terrestrial_capture();
            if (!pins) {
                report_failure("TS pins enable", pins.error(), &bridge,
                               frontend.diagnostic_stage());
                primary_status = kStreamFailure;
                break;
            }
            std::printf("TS pins enabled\n");
            if (stop_requested()) {
                if (primary_status == 0) primary_status = kStopRequested;
                break;
            }
            const StreamConfig config{kTsInEndpoint, 188U * 816U, 6U};
            const auto started = transport.start_stream(config);
            if (!started) {
                report_failure("stream start", started.error());
                primary_status = kStreamFailure;
                break;
            }
            stream_started = true;
            std::printf("stream started\n");
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(arguments.seconds);
            while (std::chrono::steady_clock::now() < deadline) {
                if (stop_requested()) {
                    if (primary_status == 0) primary_status = kStopRequested;
                    break;
                }
                const auto event = transport.wait_stream(Timeout{250U});
                if (event) {
                    if (event.value().size != 0U) {
                        const auto fed = demux.push(
                            ByteView{event.value().data, event.value().size},
                            write_ts_probe_packet, &sink);
                        if (!fed) {
                            report_failure("demux/write", fed.error());
                            primary_status = kStreamFailure;
                            break;
                        }
                    }
                    continue;
                }
                const bool before_deadline = std::chrono::steady_clock::now() < deadline;
                const TsProbeWaitDisposition disposition =
                    classify_ts_probe_wait(event.error(), before_deadline);
                if (disposition == TsProbeWaitDisposition::retry) continue;
                if (disposition == TsProbeWaitDisposition::deadline) break;
                report_failure("stream receive", event.error());
                primary_status = kStreamFailure;
                break;
            }
            if (stop_requested() && primary_status == 0) primary_status = kStopRequested;
            if (primary_status != kStopRequested) std::printf("capture complete\n");
        } while (false);

        if (stop_requested() && primary_status == 0) primary_status = kStopRequested;
        if (stream_started || transport.stream_active()) {
            std::printf("stopping stream\n");
            (void)std::fflush(stdout);
            const auto stopped = transport.stop_stream();
            stream_started = false;
            remember_error(cleanup_error, stopped);
            if (!stopped) report_failure("stream stop", stopped.error());
        }
        const auto pins_stopped = arguments.tune_satellite
            ? frontend.stop_satellite_capture()
            : frontend.stop_terrestrial_capture();
        remember_error(cleanup_error, pins_stopped);
        if (!pins_stopped) report_failure("TS pins disable", pins_stopped.error(), &bridge,
                                          frontend.diagnostic_stage());
        const auto closed_frontend = frontend.close();
        remember_error(cleanup_error, closed_frontend);
        if (!closed_frontend) report_failure("frontend close", closed_frontend.error(), &bridge,
                                             frontend.diagnostic_stage());
        const auto powered_off = power.set_backend_power(false, delay);
        if (!powered_off) frontend.record_cleanup_power_off_failure();
        remember_error(cleanup_error, powered_off);
        if (!powered_off) report_failure("power off", powered_off.error());
    }

    if (stop_requested() && primary_status == 0) primary_status = kStopRequested;

    if (std::fflush(output) == EOF) {
        std::fprintf(stderr, "output flush failed\n");
        if (primary_status == 0) primary_status = kFileFailure;
    }
    if (std::fclose(output) == EOF) {
        std::fprintf(stderr, "output close failed\n");
        if (primary_status == 0) primary_status = kFileFailure;
    }
    if (stop_requested() && primary_status == 0) primary_status = kStopRequested;

    const TsProbeSinkCounters counters = sink.counters;
    const TaggedTsDemux::Counters demux_counters = demux.counters();
    if (primary_status == 0) {
        const TsProbeAcceptanceResult acceptance =
            evaluate_ts_probe_acceptance(arguments.seconds, counters, demux_counters,
                                         arguments.receiver);
        if (!acceptance.accepted) {
            for (std::size_t index = 0U; index < acceptance.failure_count; ++index) {
                std::fprintf(stderr, "capture rejected: %s\n",
                             ts_probe_acceptance_failure_string(acceptance.failures[index]));
            }
            primary_status = kStreamFailure;
        }
    }
    if (cleanup_error != Error::OK) {
        std::fprintf(stderr, "cleanup failed: %s\n", error_string(cleanup_error));
    }
    std::printf("selected packets=%zu bytes=%zu output=%zu observed=[%zu,%zu,%zu,%zu] "
                "demux accepted=%zu emitted=%zu discarded=%zu invalid=%zu buffered=%zu\n",
                counters.selected_packets, counters.selected_bytes,
                counters.output_bytes, counters.observed_packets[0U],
                counters.observed_packets[1U], counters.observed_packets[2U],
                counters.observed_packets[3U],
                demux_counters.input_bytes_accepted, demux_counters.emitted_packets,
                demux_counters.discarded_sync_search_bytes, demux_counters.invalid_tag_packets,
                demux_counters.buffered_bytes);
    return ts_probe_cleanup_status(primary_status, cleanup_error);
}

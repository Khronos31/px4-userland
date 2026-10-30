// SPDX-License-Identifier: GPL-2.0-only
// Windows px4-ts front end using the shared Core client and output adapter.
#include <dtv/host_ts/px4_ts_core.h>
#include <dtv/host_ts/px4_ts_windows_output.h>
#include "px4/identity.h"
#include "px4_ts_profile.h"
#include "px4_windows_args.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::cli;

bool matches_px4_serial_identity(
    const dtv::userland::wire::ListResponse& list,
    const dtv::userland::identity::ResolvedIdentity& expected) noexcept
{
    if (!list.enclosure.present || expected.kind !=
            dtv::userland::identity::IdentityKind::serial ||
        list.enclosure.value.identity.uniqueness !=
            dtv::userland::wire::Uniqueness::unique_in_scan ||
        list.functions.count == 0U)
        return false;
    const auto& observed = list.enclosure.value.identity;
    const px4::userland::DeviceProfile* profile =
        px4::userland::device_profile_for_usb_id(
            list.functions.items[0U].vid, list.functions.items[0U].pid);
    if (profile == nullptr) return false;
    for (std::size_t index = 1U; index < list.functions.count; ++index) {
        const px4::userland::DeviceProfile* other =
            px4::userland::device_profile_for_usb_id(
                list.functions.items[index].vid, list.functions.items[index].pid);
        if (other == nullptr || other->model != profile->model) return false;
    }
    return observed.serial_state == dtv::userland::wire::IdentitySerialState::present &&
           observed.serial.present &&
           observed.serial.value.length == expected.serial.length &&
           std::memcmp(observed.serial.value.bytes.data(), expected.serial.bytes.data(),
                       expected.serial.length) == 0;
}

std::atomic<bool> stop_flag{false};
HANDLE cleanup_event = nullptr;

BOOL WINAPI stop_handler(DWORD control_type) noexcept
{
    switch (control_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        stop_flag.store(true, std::memory_order_release);
        if (cleanup_event != nullptr) {
            (void)::WaitForSingleObject(cleanup_event, 5000U);
        }
        return TRUE;
    default:
        return FALSE;
    }
}

class WindowsClock final : public Px4TsClock {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        ::Sleep(milliseconds);
    }
};

class WindowsSignal final : public Px4TsSignal {
public:
    bool stop_requested() const noexcept override
    {
        return stop_flag.load(std::memory_order_acquire);
    }
};

bool install_handlers() noexcept
{
    cleanup_event = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (cleanup_event == nullptr) {
        return false;
    }
    return ::SetConsoleCtrlHandler(stop_handler, TRUE) != 0;
}

void print_counters(const char* label, const ipc::CountersPayload& counters) noexcept
{
    const auto print = [](const char* name, std::uint64_t value,
                          dtv::userland::wire::MeasureState state) noexcept {
        if (state == dtv::userland::wire::MeasureState::valid)
            std::fprintf(stderr, " %s=%llu", name, static_cast<unsigned long long>(value));
        else if (state == dtv::userland::wire::MeasureState::unknown)
            std::fprintf(stderr, " %s=unknown", name);
    };
    std::fprintf(stderr, "%s", label);
    print("packets", counters.packets, counters.availability[0U]);
    print("bytes", counters.bytes, counters.availability[1U]);
    print("sync-errors", counters.sync_errors, counters.availability[2U]);
    print("tei", counters.tei_packets, counters.availability[3U]);
    print("continuity-errors", counters.continuity_errors, counters.availability[4U]);
    print("queue-drops", counters.queue_drops, counters.availability[5U]);
    print("usb-errors", counters.usb_errors, counters.availability[6U]);
    print("empty-intervals", counters.empty_intervals, counters.availability[7U]);
    std::fputc('\n', stderr);
}

}  // namespace

int main(int argc, char** argv)
{
    static const TsClientProfile profile{
        "px4-ts", "px4-userland", dtv::userland::identity::Product::px4,
        kPx4TsSerialSelectorSupported, kPx4TsPreserveInstanceCase,
        &matches_px4_serial_identity};
    const std::vector<std::string> owned =
        px4::userland::cli::windows_argv_utf8(argc, argv);
    std::vector<const char*> views;
    views.reserve(owned.size());
    for (const std::string& value : owned) {
        views.push_back(value.c_str());
    }
    const Px4TsArguments arguments =
        parse_px4_ts_product_arguments(static_cast<int>(views.size()), views.data(), profile);
    if (!arguments.valid) {
        std::fprintf(stderr, "argument error: %s\n", arguments.error.c_str());
        print_px4_ts_usage(profile, stderr);
        return 2;
    }
    if (arguments.help) {
        print_px4_ts_usage(profile, stdout);
        return 0;
    }
    if (arguments.version) {
        std::printf("px4-ts %s\n", PX4_USERLAND_PRODUCT_VERSION);
        return 0;
    }
    if (!install_handlers()) {
        std::fprintf(stderr, "signal setup failed\n");
        return 70;
    }
    const bool stdout_output = arguments.output == "-";
    HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "output: stdout is not available\n");
        return 70;
    }
    if (!stdout_output) {
        const std::vector<wchar_t> wide =
            px4::userland::cli::utf8_to_wide_path(arguments.output);
        if (wide.empty()) {
            std::fprintf(stderr, "output: invalid path\n");
            return 70;
        }
        handle = ::CreateFileW(wide.data(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            std::fprintf(stderr, "output: error %lu\n",
                         static_cast<unsigned long>(::GetLastError()));
            return 70;
        }
    }
    auto sink_result = Px4TsWindowsOutput::create(handle, !stdout_output);
    if (!sink_result) {
        std::fprintf(stderr, "output setup failed\n");
        return 70;
    }
    std::unique_ptr<Px4TsWindowsOutput> output = std::move(sink_result.value());
    WindowsClock clock;
    WindowsSignal signal;
    Px4TsFailureKind failure_kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics;
    const auto result = Px4TsRunner::run_wire(arguments, profile, *output, clock, signal,
                                              &failure_kind, &diagnostics);
    const auto finished = output->finish();
    const std::uint64_t completed = output->completed_packets();
    // Release a blocked console/termination handler only when the worker has
    // actually stopped; an abandoned uncancellable writer must not advertise
    // cleanup completion.
    if (cleanup_event != nullptr && output->worker_stopped()) {
        (void)::SetEvent(cleanup_event);
    }

    Error error = result ? Error::OK : result.error();
    if (!finished && error == Error::OK) {
        error = finished.error();
        failure_kind = Px4TsFailureKind::output;
    }
    if (error != Error::OK) {
        std::fprintf(stderr, "px4-ts: %s\n", error_string(error));
        if (!result)
            print_px4_ts_run_diagnostic(stderr, "px4-ts", result.error(), diagnostics);
        std::fprintf(stderr, "written packets=%llu bytes=%llu\n",
                     static_cast<unsigned long long>(completed),
                     static_cast<unsigned long long>(completed * 188U));
        if (diagnostics.stop_counters_valid)
            print_counters("STOP_STREAM", diagnostics.stop_counters);
        if (diagnostics.stream_end_valid) {
            print_counters("STREAM_END", diagnostics.stream_end.counters);
            std::fprintf(stderr, "STREAM_END error=%u\n",
                         static_cast<unsigned int>(diagnostics.stream_end.error_code));
        }
        return px4_ts_exit_status(error, failure_kind);
    }
    print_counters("stream", result.value().counters);
    return 0;
}

#endif  // _WIN32

// SPDX-License-Identifier: GPL-2.0-only
//
// Manual one-receiver probe for the experimental L3 product adapter.  It opens
// a caller-selected enclosure, uses the existing Q3U4 backend, and never runs
// under CTest.  Run it only on a host where the enclosure is assigned to this
// work; the L3 Core owner/claim path is covered by the fake contract test.

#include "l3_backend_product.h"
#include "px4/firmware.h"
#include "px4/it930x.h"
#include "px4/libusb_transport.h"
#include "px4/q3u4_stream.h"
#include "q3u4_frontend.h"
#include "q3u4_lnb_power.h"
#include "q3u4_tuner_backend.h"
#include "bridge_i2c.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

namespace {

using namespace px4::userland;

class ProbeTime final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

class UnusedUsbPort final : public L3UsbPort {
public:
    dtv_l3_claim_result claim(dtv_l3_provider_binding*,
                              std::uint32_t) noexcept override
    {
        return {DTV_L3_FAILED, DTV_L3_CLAIM_RETAINED};
    }

    dtv_l3_release_result release_claim(
        dtv_l3_provider_binding*, std::uint32_t) noexcept override
    {
        return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN, DTV_L3_CLAIM_UNKNOWN,
                DTV_L3_RESTORE_NOT_ATTEMPTED, DTV_L3_FAILED};
    }

    dtv_l3_result submit(dtv_l3_provider_binding*, std::uint64_t,
                         const dtv_l3_transfer_request*,
                         dtv_l3_transfer_complete_fn, void*) noexcept override
    {
        return DTV_L3_FAILED;
    }

    dtv_l3_result cancel(dtv_l3_provider_binding*, std::uint64_t) noexcept override
    {
        return DTV_L3_FAILED;
    }
};

struct PumpState final {
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t bytes = 0U;
    bool eof = false;
};

void deliver(void* context, const std::uint8_t* data, std::size_t size)
{
    auto* state = static_cast<PumpState*>(context);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (data == nullptr && size == 0U) {
        state->eof = true;
    } else {
        state->bytes += size;
    }
    state->changed.notify_all();
}

void usage()
{
    std::fprintf(
        stderr,
        "usage: px4_l3_real_probe --firmware PATH --serial SERIAL "
        "--receiver N --hz HZ [--wait-seconds N]\n");
}

bool parse_u64(const char* text, std::uint64_t* value)
{
    if (text == nullptr || value == nullptr || *text == '\0') {
        return false;
    }
    std::uint64_t result = 0U;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        result = result * 10U + static_cast<std::uint64_t>(*cursor - '0');
    }
    *value = result;
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    const char* firmware_path = nullptr;
    const char* serial = nullptr;
    std::uint64_t receiver_value = 0U;
    std::uint64_t frequency_hz = 0U;
    std::uint64_t wait_seconds = 15U;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        const bool has_value = index + 1 < argc;
        if (argument == "--firmware" && has_value) {
            firmware_path = argv[++index];
        } else if (argument == "--serial" && has_value) {
            serial = argv[++index];
        } else if (argument == "--receiver" && has_value) {
            if (!parse_u64(argv[++index], &receiver_value)) {
                usage();
                return 2;
            }
        } else if (argument == "--hz" && has_value) {
            if (!parse_u64(argv[++index], &frequency_hz)) {
                usage();
                return 2;
            }
        } else if (argument == "--wait-seconds" && has_value) {
            if (!parse_u64(argv[++index], &wait_seconds)) {
                usage();
                return 2;
            }
        } else {
            usage();
            return 2;
        }
    }
    if (firmware_path == nullptr || serial == nullptr ||
        receiver_value > 7U || frequency_hz == 0U) {
        usage();
        return 2;
    }

    const auto firmware = FirmwareProvider(firmware_path).load();
    if (!firmware) {
        std::fprintf(stderr, "firmware: %s\n", error_string(firmware.error()));
        return 3;
    }
    const auto runtime = Q3U4Runtime::open_native(serial);
    if (!runtime) {
        std::fprintf(stderr, "open: %s\n", error_string(runtime.error()));
        return 3;
    }
    Q3U4Runtime& device = *runtime.value();
    if (device.bridge_count() != 2U ||
        device.model() != DeviceModel::px_q3u4) {
        std::fprintf(stderr, "probe currently expects PX-Q3U4\n");
        return 4;
    }

    It930xController dev1(device.dev1());
    It930xController dev2(device.dev2());
    const auto initialized1 = dev1.initialize_q3u4(
        firmware.value(), InitializationPolicy::accept_cold_or_warm,
        device.model());
    const auto initialized2 = initialized1
        ? dev2.initialize_q3u4(firmware.value(),
                               InitializationPolicy::accept_cold_or_warm,
                               device.model())
        : Result<FirmwareLoadResult>::failure(initialized1.error());
    if (!initialized1 || !initialized2) {
        std::fprintf(stderr, "initialize: %s\n",
                     error_string(!initialized1 ? initialized1.error()
                                                : initialized2.error()));
        return 5;
    }

    ProbeTime time;
    It930xBridgeI2cMaster dev1_i2c(dev1);
    It930xBridgeI2cMaster dev2_i2c(dev2);
    It930xBackendPower dev1_power(dev1);
    It930xBackendPower dev2_power(dev2);
    It930xPsbPurger dev1_purger(dev1);
    It930xPsbPurger dev2_purger(dev2);
    Q3U4FrontendEnclosure enclosure(dev1_i2c, dev2_i2c, dev1_power,
                                    dev2_power, time, &dev1_purger,
                                    &dev2_purger);
    It930xLnbPower dev1_lnb(dev1);
    It930xLnbPower dev2_lnb(dev2);
    Q3U4LnbPowerCoordinator lnb_power(dev1_lnb, dev2_lnb, false);
    Q3U4FrontendTunerBackend tuner(enclosure, lnb_power);
    const auto stream = Q3U4StreamDataPlane::create(device.dev1(),
                                                    device.dev2());
    if (!stream) {
        std::fprintf(stderr, "stream: %s\n", error_string(stream.error()));
        return 6;
    }

    UnusedUsbPort usb;
    L3BackendProduct product(tuner, *stream.value(), usb,
                             device_profile(device.model()), true);
    std::uint64_t programmed = 0U;
    const auto tuned = product.tune(static_cast<std::uint32_t>(receiver_value),
                                    frequency_hz, &programmed);
    if (tuned != DTV_L3_OK) {
        std::fprintf(stderr, "tune: %d\n", static_cast<int>(tuned));
        (void)product.close_all();
        return 7;
    }
    const std::uint32_t bridge =
        receiver_value < 4U ? 0U : 1U;
    const auto started = product.bridge_start(
        bridge, static_cast<std::uint32_t>(receiver_value));
    if (started != DTV_L3_OK) {
        std::fprintf(stderr, "bridge_start: %d\n", static_cast<int>(started));
        (void)product.close_all();
        return 8;
    }

    PumpState pump;
    const auto attached = product.stream_attach(1U, &deliver, &pump);
    if (attached != DTV_L3_OK) {
        std::fprintf(stderr, "stream_attach: %d\n", static_cast<int>(attached));
        (void)product.bridge_stop(bridge);
        (void)product.close_all();
        return 9;
    }

    bool got_ts = false;
    {
        std::unique_lock<std::mutex> lock(pump.mutex);
        got_ts = pump.changed.wait_for(
            lock, std::chrono::seconds(wait_seconds),
            [&pump] { return pump.bytes >= 188U || pump.eof; });
        got_ts = got_ts && pump.bytes >= 188U;
    }
    const auto stopped = product.stream_stop(1U);
    const auto bridge_stopped = product.bridge_stop(bridge);
    const auto closed = product.close_all();
    std::printf(
        "{\"receiver\":%llu,\"hz\":%llu,\"programmed_hz\":%llu,"
        "\"ts_bytes\":%zu,\"got_ts\":%s,\"stream_stop\":%d,"
        "\"bridge_stop\":%d,\"close\":%d}\n",
        static_cast<unsigned long long>(receiver_value),
        static_cast<unsigned long long>(frequency_hz),
        static_cast<unsigned long long>(programmed), pump.bytes,
        got_ts ? "true" : "false", static_cast<int>(stopped),
        static_cast<int>(bridge_stopped), static_cast<int>(closed));
    return got_ts && stopped == DTV_L3_OK &&
                   bridge_stopped == DTV_L3_OK && closed == DTV_L3_OK
               ? 0
               : 10;
}

// SPDX-License-Identifier: GPL-2.0-only
#include "frontend_probe_support.h"

#include "bridge_i2c.h"
#include "px4/firmware.h"
#include "px4/it930x.h"
#include "px4/libusb_transport.h"

#include <chrono>
#include <cstdio>
#include <utility>
#include <thread>

namespace {
using namespace px4::userland;
constexpr int kArgs = 2;
constexpr int kOpen = 3;
constexpr int kInit = 4;
constexpr int kFrontend = 5;
constexpr int kCleanup = 6;

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

void usage() noexcept
{
    std::printf("usage: px4-frontend-probe --base BASE --firmware PATH --device 1 "
                "--receiver 2 --frequency-khz F --tune-terrestrial\n");
}

void print_i2c_failure(const BridgeI2cFailureDetail& detail) noexcept
{
    if (!detail.valid) return;
    const char* type = detail.type == BridgeI2cRequestType::write ? "write" : "read";
    std::fprintf(stderr, "i2c-failure index=%zu type=%s address=0x%02x write_len=%zu read_len=%zu data=",
                 detail.request_index, type, detail.address, detail.write_length,
                 detail.read_length);
    for (std::size_t index = 0U; index < detail.captured_byte_count; ++index) {
        std::fprintf(stderr, "%s%02x", index == 0U ? "" : ":", detail.write_data[index]);
    }
    std::fputc('\n', stderr);
}

int failure(const char* stage, Error error, int code, const char* diagnostic = nullptr,
            const It930xBridgeI2cMaster* bridge = nullptr) noexcept
{
    std::fprintf(stderr, "%s failed: %s", stage, error_string(error));
    if (diagnostic != nullptr) std::fprintf(stderr, " stage=%s", diagnostic);
    std::fputc('\n', stderr);
    if (bridge != nullptr) print_i2c_failure(bridge->failure_detail());
    return code;
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
    auto* values = static_cast<PowerOffContext*>(context);
    const auto result = values->power->set_backend_power(false, *values->delay);
    if (!result) values->frontend->record_cleanup_power_off_failure();
    return result;
}
}  // namespace

int main(int argc, char** argv)
{
    const auto args = parse_frontend_probe_arguments(argc, const_cast<const char* const*>(argv));
    if (!args.valid) {
        std::fprintf(stderr, "argument error: %.*s\n", static_cast<int>(args.error.size()), args.error.data());
        return kArgs;
    }
    if (args.help) { usage(); return 0; }

    FirmwareProvider provider(args.firmware_path);
    const auto image = provider.load();
    if (!image) return failure("firmware", image.error(), kInit);
    const auto runtime = Q3U4Runtime::open_native(args.base_serial);
    if (!runtime) return failure("discovery/open", runtime.error(), kOpen);

    RealDelay delay;
    It930xController dev1(runtime.value()->dev1());
    It930xController dev2(runtime.value()->dev2());
    It930xBackendPower dev1_power(dev1);
    It930xBackendPower dev2_power(dev2);
    CoupledProbePower power(dev1_power, dev2_power);
    It930xBridgeI2cMaster bridge(dev1);
    Q3U4Frontend frontend(bridge, power, delay);
    Error cleanup_error = Error::OK;
    int primary = 0;
    PowerOffContext power_context{&power, &delay, &frontend};
    {
        ProbeCleanupGuard cleanup(close_frontend, &frontend, power_off, &power_context,
                                 cleanup_error);
        const auto init1 = dev1.initialize_q3u4(image.value());
        if (!init1) {
            primary = failure("dev1 firmware/init", init1.error(), kInit);
        } else {
            const auto init2 = dev2.initialize_q3u4(image.value());
            if (!init2) {
                primary = failure("dev2 firmware/init", init2.error(), kInit);
            } else {
                std::printf("dev1 firmware version=0x%08x verified=%s\n", init1.value().firmware_version,
                            init1.value().verified ? "yes" : "no");
                std::printf("dev2 firmware version=0x%08x verified=%s\n", init2.value().firmware_version,
                            init2.value().verified ? "yes" : "no");
                const auto opened = frontend.open_terrestrial(args.receiver);
                if (!opened) {
                    primary = failure("frontend open", opened.error(), kFrontend,
                                      frontend.diagnostic_stage(), &bridge);
                } else {
                    std::printf("frontend open\n");
                    const auto tuned = frontend.tune_terrestrial(args.frequency_khz);
                    if (!tuned) {
                        primary = failure("tuner PLL lock", tuned.error(), kFrontend,
                                          frontend.diagnostic_stage(), &bridge);
                    } else {
                        std::printf("tuner PLL lock\n");
                        const auto lock = poll_frontend_probe_lock(demod_lock, &frontend, delay);
                        if (!lock.locked) {
                            primary = failure("demod lock", lock.error, kFrontend, nullptr, &bridge);
                        } else {
                            std::printf("demod lock\n");
                            const auto closed = frontend.close();
                            if (!closed) primary = failure("cleanup", closed.error(), kCleanup,
                                                           frontend.diagnostic_stage(), &bridge);
                        }
                    }
                }
            }
        }
    }
    if (cleanup_error != Error::OK) {
        std::fprintf(stderr, "cleanup failed: %s stage=%s\n", error_string(cleanup_error),
                     frontend.diagnostic_stage());
        print_i2c_failure(bridge.failure_detail());
    }
    return frontend_probe_cleanup_status(primary, cleanup_error);
}

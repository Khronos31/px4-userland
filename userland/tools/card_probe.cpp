// SPDX-License-Identifier: GPL-2.0-only
#include "frontend_probe_support.h"

#include "px4/card.h"
#include "px4/firmware.h"
#include "px4/it930x.h"
#include "px4/libusb_transport.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;

constexpr int kArgumentFailure = 2;
constexpr int kFirmwareFailure = 3;
constexpr int kOpenFailure = 4;
constexpr int kInitializeFailure = 5;
constexpr int kPowerFailure = 6;
constexpr int kCardFailure = 7;
constexpr int kCleanupFailure = 8;

struct Arguments final {
    bool valid = false;
    bool help = false;
    bool detect = false;
    bool atr = false;
    bool apdu = false;
    bool repeat_specified = false;
    std::uint32_t repeat = 1U;
    std::string base_serial;
    std::string firmware_path;
    std::vector<std::uint8_t> apdu_bytes;
    std::string_view error;
};

Arguments invalid_arguments(std::string_view error) noexcept
{
    Arguments result;
    result.error = error;
    return result;
}

bool valid_base_serial(std::string_view serial) noexcept
{
    if (serial.size() != 14U) {
        return false;
    }
    for (const char character : serial) {
        if (character < '0' || character > '9') {
            return false;
        }
    }
    return true;
}

int hex_nibble(char character) noexcept
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

bool parse_apdu_hex(std::string_view text, std::vector<std::uint8_t>& output) noexcept
{
    constexpr std::size_t kMaxApduLength = 65535U;
    output.clear();
    if (text.empty()) {
        return false;
    }

    std::size_t offset = 0U;
    while (offset < text.size()) {
        if (output.size() == kMaxApduLength || offset + 2U > text.size()) {
            output.clear();
            return false;
        }
        const int high = hex_nibble(text[offset]);
        const int low = hex_nibble(text[offset + 1U]);
        if (high < 0 || low < 0) {
            output.clear();
            return false;
        }
        output.push_back(static_cast<std::uint8_t>((high << 4) | low));
        offset += 2U;
        if (offset == text.size()) {
            break;
        }
        if (text[offset] != ':') {
            output.clear();
            return false;
        }
        ++offset;
        if (offset == text.size()) {
            output.clear();
            return false;
        }
    }
    return !output.empty();
}

bool parse_repeat(std::string_view text, std::uint32_t& output) noexcept
{
    if (text.empty()) {
        return false;
    }
    std::uint32_t parsed = 0U;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        parsed < 1U || parsed > 100000U) {
        return false;
    }
    output = parsed;
    return true;
}

Arguments parse_arguments(int argc, const char* const* argv) noexcept
{
    if (argc < 1 || argv == nullptr) {
        return invalid_arguments("invalid argument vector");
    }

    Arguments result;
    bool have_base = false;
    bool have_firmware = false;
    bool have_apdu = false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) {
            return invalid_arguments("null argument");
        }
        const std::string_view argument(argv[index]);
        if (argument == "--help") {
            if (argc != 2) {
                return invalid_arguments("--help cannot be combined with other arguments");
            }
            result.valid = true;
            result.help = true;
            return result;
        }
        if (argument == "--detect") {
            if (result.detect) {
                return invalid_arguments("duplicate --detect");
            }
            result.detect = true;
            continue;
        }
        if (argument == "--atr") {
            if (result.atr) {
                return invalid_arguments("duplicate --atr");
            }
            result.atr = true;
            continue;
        }
        if (argument == "--apdu") {
            if (have_apdu || index + 1 >= argc || argv[index + 1] == nullptr) {
                return invalid_arguments("--apdu requires exactly one value");
            }
            have_apdu = true;
            result.apdu = true;
            if (!parse_apdu_hex(argv[++index], result.apdu_bytes)) {
                return invalid_arguments(
                    "--apdu must be 1..65535 colon-separated hexadecimal bytes");
            }
            continue;
        }
        if (argument == "--repeat") {
            if (result.repeat_specified || index + 1 >= argc || argv[index + 1] == nullptr) {
                return invalid_arguments("--repeat requires exactly one value");
            }
            result.repeat_specified = true;
            if (!parse_repeat(argv[++index], result.repeat)) {
                return invalid_arguments("--repeat must be a decimal integer in 1..100000");
            }
            continue;
        }
        if (argument == "--base") {
            if (have_base || index + 1 >= argc || argv[index + 1] == nullptr) {
                return invalid_arguments("--base requires exactly one value");
            }
            have_base = true;
            result.base_serial = argv[++index];
            continue;
        }
        if (argument == "--firmware") {
            if (have_firmware || index + 1 >= argc || argv[index + 1] == nullptr) {
                return invalid_arguments("--firmware requires exactly one value");
            }
            have_firmware = true;
            result.firmware_path = argv[++index];
            continue;
        }
        return invalid_arguments("unknown argument");
    }

    if (!have_base) {
        return invalid_arguments("--base is required");
    }
    if (!valid_base_serial(result.base_serial)) {
        return invalid_arguments("--base must be a 14-digit base serial");
    }
    if (!have_firmware || result.firmware_path.empty()) {
        return invalid_arguments("--firmware is required and must not be empty");
    }
    const unsigned int mode_count = static_cast<unsigned int>(result.detect) +
                                    static_cast<unsigned int>(result.atr) +
                                    static_cast<unsigned int>(result.apdu);
    if (mode_count != 1U) {
        return invalid_arguments("exactly one of --detect, --atr or --apdu is required");
    }
    if (result.repeat_specified && !result.apdu) {
        return invalid_arguments("--repeat requires --apdu");
    }
    result.valid = true;
    return result;
}

void print_usage() noexcept
{
    std::printf(
        "usage: px4-card-probe --base BASE --firmware PATH (--detect|--atr|--apdu HEX)\n");
    std::printf("--detect checks Q3U4 device 1 without resetting the card\n");
    std::printf("--atr resets the card and reads its ATR without starting T=1\n");
    std::printf("--apdu initializes T=1 and transmits colon-separated hexadecimal bytes\n");
    std::printf("--repeat N transmits the APDU 1..100000 times in the same session\n");
}

class ProbeTime final : public CardTime, public Q3U4Delay {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }

    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

int failure(const char* stage, Error error, int status) noexcept
{
    std::fprintf(stderr, "%s failed: %s\n", stage, error_string(error));
    return status;
}

unsigned int baud_rate_value(It930xCardBaudRate baud_rate) noexcept
{
    switch (baud_rate) {
    case It930xCardBaudRate::baud_9600:
        return 9600U;
    case It930xCardBaudRate::baud_19200:
        return 19200U;
    case It930xCardBaudRate::baud_38400:
        return 38400U;
    }
    return 0U;
}

void print_atr(const CardAtr& atr) noexcept
{
    std::printf("atr=");
    for (std::size_t index = 0U; index < atr.length; ++index) {
        std::printf("%s%02x", index == 0U ? "" : ":",
                    static_cast<unsigned int>(atr.bytes[index]));
    }
    std::printf("\n");
    std::printf("baud=%u ifsc=%u edc=%s block-timeout-ms=%u\n",
                baud_rate_value(atr.baud_rate), static_cast<unsigned int>(atr.ifsc),
                atr.edc == CardEdc::crc ? "crc" : "lrc",
                static_cast<unsigned int>(atr.block_timeout_ms));
}

void print_response(ByteView response) noexcept
{
    std::printf("response=");
    for (std::size_t index = 0U; index < response.size; ++index) {
        std::printf("%s%02x", index == 0U ? "" : ":",
                    static_cast<unsigned int>(response.data[index]));
    }
    std::printf("\nresponse-length=%zu\n", response.size);
}

}  // namespace

int main(int argc, char** argv)
{
    const auto arguments = parse_arguments(argc, const_cast<const char* const*>(argv));
    if (!arguments.valid) {
        std::fprintf(stderr, "argument error: %.*s\n",
                     static_cast<int>(arguments.error.size()), arguments.error.data());
        print_usage();
        return kArgumentFailure;
    }
    if (arguments.help) {
        print_usage();
        return 0;
    }

    FirmwareProvider provider(arguments.firmware_path);
    const auto image = provider.load();
    if (!image) {
        return failure("firmware", image.error(), kFirmwareFailure);
    }

    const auto opened = Q3U4Runtime::open_native(arguments.base_serial);
    if (!opened) {
        return failure("discovery/open", opened.error(), kOpenFailure);
    }

    It930xController dev1(opened.value()->dev1());
    It930xController dev2(opened.value()->dev2());
    const auto init1 = dev1.initialize_q3u4(image.value());
    if (!init1) {
        return failure("dev1 firmware/init", init1.error(), kInitializeFailure);
    }
    const auto init2 = dev2.initialize_q3u4(image.value());
    if (!init2) {
        return failure("dev2 firmware/init", init2.error(), kInitializeFailure);
    }

    ProbeTime delay;
    It930xBackendPower dev1_power(dev1);

    int primary_status = 0;
    const auto powered = dev1_power.set_backend_power(true, delay);
    if (!powered) {
        primary_status = failure("backend power on", powered.error(), kPowerFailure);
    } else {
        const auto card_uart = dev1.initialize_card_uart();
        if (!card_uart) {
            primary_status = failure("dev1 card UART initialize", card_uart.error(), kCardFailure);
        } else {
            if (arguments.detect) {
                const auto detected = dev1.detect_card();
                if (!detected) {
                    primary_status = failure("dev1 card detect", detected.error(), kCardFailure);
                } else {
                    std::printf("card detected=%s\n", detected.value() ? "yes" : "no");
                }
            } else if (arguments.atr) {
                It930xCardHardware card(dev1);
                const auto atr = reset_and_read_card_atr(card, delay);
                if (!atr) {
                    if (atr.error() == Error::NO_CARD) {
                        std::printf("card detected=no\n");
                    }
                    primary_status = failure("dev1 card ATR", atr.error(), kCardFailure);
                } else {
                    std::printf("card detected=yes\n");
                    print_atr(atr.value());
                }
            } else {
                It930xCardHardware card(dev1);
                CardSession session(card, delay);
                const auto initialized = session.initialize();
                if (!initialized) {
                    if (initialized.error() == Error::NO_CARD) {
                        std::printf("card detected=no\n");
                    }
                    primary_status = failure("dev1 card T=1 initialize",
                                             initialized.error(), kCardFailure);
                } else {
                    std::printf("card detected=yes\n");
                    print_atr(session.atr());
                    std::array<std::uint8_t, 4096U> response{};
                    std::size_t response_length = 0U;
                    for (std::uint32_t iteration = 1U; iteration <= arguments.repeat;
                         ++iteration) {
                        const auto transmitted = session.transmit(
                            ByteView{arguments.apdu_bytes.data(),
                                     arguments.apdu_bytes.size()},
                            MutableByteView{response.data(), response.size()});
                        if (!transmitted) {
                            std::fprintf(stderr,
                                         "dev1 card APDU failed at transmit %u/%u: %s\n",
                                         static_cast<unsigned int>(iteration),
                                         static_cast<unsigned int>(arguments.repeat),
                                         error_string(transmitted.error()));
                            primary_status = kCardFailure;
                            break;
                        }
                        response_length = transmitted.value();
                    }
                    if (primary_status == 0) {
                        print_response(ByteView{response.data(), response_length});
                        std::printf("transmit-count=%u\n",
                                    static_cast<unsigned int>(arguments.repeat));
                    }
                }
            }
        }
    }

    // This is intentionally unconditional once power-on was attempted. A
    // failed transaction may still have reached device 1. Card-only operation
    // never changes device 2 backend power.
    const auto powered_off = dev1_power.set_backend_power(false, delay);
    if (!powered_off) {
        std::fprintf(stderr, "backend power off cleanup failed: %s\n",
                     error_string(powered_off.error()));
        if (primary_status == 0) {
            return kCleanupFailure;
        }
    }
    return primary_status;
}

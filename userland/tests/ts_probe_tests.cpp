// SPDX-License-Identifier: GPL-2.0-only
#include "tagged_ts_demux.h"
#include "ts_probe_support.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

using namespace px4::userland;

#define TS_PROBE_CHECK(condition)                                                        \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::fprintf(stderr, "ts probe check failed at %s:%d: %s\n",              \
                         __FILE__, __LINE__, #condition);                               \
            return false;                                                                \
        }                                                                                \
    } while (false)

using Arguments = std::vector<std::string>;

TsProbeArguments parse(const Arguments& arguments)
{
    std::vector<const char*> pointers;
    pointers.reserve(arguments.size());
    for (const std::string& argument : arguments) pointers.push_back(argument.c_str());
    return parse_ts_probe_arguments(static_cast<int>(pointers.size()), pointers.data());
}

Arguments valid_arguments()
{
    return {"px4-ts-probe", "--base", "BASE", "--firmware", "firmware.bin", "--device", "1",
            "--receiver", "2", "--frequency-khz", "500000", "--seconds", "5", "--output",
            "capture.ts", "--tune-terrestrial"};
}

Arguments valid_satellite_arguments()
{
    return {"px4-ts-probe", "--base", "BASE", "--firmware", "firmware.bin", "--device", "1",
            "--receiver", "0", "--frequency-khz", "1049480", "--slot", "3",
            "--symbol-rate", "28860", "--rolloff", "4", "--lnb-voltage", "0",
            "--seconds", "5", "--output", "satellite.ts", "--tune-satellite"};
}

Arguments valid_fd_arguments()
{
    Arguments arguments = valid_arguments();
    arguments.erase(arguments.begin() + 1, arguments.begin() + 3);
    arguments.insert(arguments.begin() + 1, {"--fd", "10", "--fd", "11"});
    return arguments;
}

bool test_arguments()
{
    const TsProbeArguments valid = parse(valid_arguments());
    TS_PROBE_CHECK(valid.valid && !valid.help && valid.base_serial == "BASE");
    TS_PROBE_CHECK(valid.firmware_path == "firmware.bin" && valid.output_path == "capture.ts");
    TS_PROBE_CHECK(valid.device == 1U && valid.receiver == 2U);
    TS_PROBE_CHECK(valid.frequency_khz == 500000U && valid.seconds == 5U);
    TS_PROBE_CHECK(valid.tune_terrestrial);
    TS_PROBE_CHECK(ts_probe_open_mode(valid) == TsProbeOpenMode::native);

    const TsProbeArguments fd_mode = parse(valid_fd_arguments());
    TS_PROBE_CHECK(fd_mode.valid && fd_mode.base_serial.empty());
    TS_PROBE_CHECK(fd_mode.file_descriptor_count == 2U);
    TS_PROBE_CHECK(fd_mode.file_descriptors[0U] == 10 &&
                   fd_mode.file_descriptors[1U] == 11);
    TS_PROBE_CHECK(ts_probe_open_mode(fd_mode) == TsProbeOpenMode::file_descriptors);

    Arguments fd_mode_with_base = valid_fd_arguments();
    fd_mode_with_base.insert(fd_mode_with_base.end(), {"--base", "BASE"});
    const TsProbeArguments fd_with_base = parse(fd_mode_with_base);
    TS_PROBE_CHECK(fd_with_base.valid && fd_with_base.base_serial == "BASE");
    TS_PROBE_CHECK(ts_probe_open_mode(fd_with_base) ==
                   TsProbeOpenMode::file_descriptors);

    Arguments one_fd = valid_fd_arguments();
    one_fd.erase(one_fd.begin() + 3, one_fd.begin() + 5);
    TS_PROBE_CHECK(!parse(one_fd).valid);
    Arguments duplicate_fd = valid_fd_arguments();
    duplicate_fd[4U] = "10";
    TS_PROBE_CHECK(!parse(duplicate_fd).valid);
    Arguments excess_fd = valid_fd_arguments();
    excess_fd.insert(excess_fd.end(), {"--fd", "12"});
    TS_PROBE_CHECK(!parse(excess_fd).valid);
    Arguments negative_fd = valid_fd_arguments();
    negative_fd[2U] = "-1";
    TS_PROBE_CHECK(!parse(negative_fd).valid);
    Arguments malformed_fd = valid_fd_arguments();
    malformed_fd[2U] = "10x";
    TS_PROBE_CHECK(!parse(malformed_fd).valid);
    Arguments oversized_fd = valid_fd_arguments();
    oversized_fd[2U] = std::to_string(
        static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1U);
    TS_PROBE_CHECK(!parse(oversized_fd).valid);
    Arguments missing_fd_value = valid_fd_arguments();
    missing_fd_value.erase(missing_fd_value.begin() + 2);
    TS_PROBE_CHECK(!parse(missing_fd_value).valid);

    Arguments zero_fd = valid_fd_arguments();
    zero_fd[2U] = "0";
    TS_PROBE_CHECK(parse(zero_fd).valid);

    const TsProbeArguments satellite = parse(valid_satellite_arguments());
    TS_PROBE_CHECK(satellite.valid && satellite.tune_satellite && !satellite.tune_terrestrial);
    TS_PROBE_CHECK(satellite.device == 1U && satellite.receiver == 0U && satellite.slot == 3U);
    TS_PROBE_CHECK(satellite.frequency_khz == 1049480U && satellite.symbol_rate == 28860U &&
                   satellite.rolloff == 4U && satellite.lnb_voltage == 0U);

    const Arguments help{"px4-ts-probe", "--help"};
    TS_PROBE_CHECK(parse(help).valid && parse(help).help);
    Arguments help_combined = help;
    help_combined.push_back("--base");
    TS_PROBE_CHECK(!parse(help_combined).valid);
    Arguments help_with_fd = help;
    help_with_fd.insert(help_with_fd.end(), {"--fd", "10", "--fd", "11"});
    TS_PROBE_CHECK(!parse(help_with_fd).valid);

    Arguments mutually_exclusive = valid_arguments();
    mutually_exclusive.push_back("--tune-satellite");
    TS_PROBE_CHECK(!parse(mutually_exclusive).valid);
    Arguments duplicate_satellite_mode = valid_satellite_arguments();
    duplicate_satellite_mode.push_back("--tune-satellite");
    TS_PROBE_CHECK(!parse(duplicate_satellite_mode).valid);

    const std::array<std::string, 8U> options{
        "--base", "--firmware", "--device", "--receiver", "--frequency-khz", "--seconds",
        "--output", "--tune-terrestrial"};
    for (const std::string& option : options) {
        Arguments missing = valid_arguments();
        for (std::size_t index = 0U; index < missing.size(); ++index) {
            if (missing[index] != option) continue;
            missing.erase(missing.begin() + static_cast<std::ptrdiff_t>(index));
            if (option != "--tune-terrestrial") missing.erase(missing.begin() + static_cast<std::ptrdiff_t>(index));
            break;
        }
        TS_PROBE_CHECK(!parse(missing).valid);
    }

    for (const std::string& option : options) {
        Arguments duplicate = valid_arguments();
        duplicate.push_back(option);
        if (option != "--tune-terrestrial") {
            if (option == "--base") duplicate.push_back("BASE2");
            if (option == "--firmware") duplicate.push_back("firmware2.bin");
            if (option == "--device") duplicate.push_back("1");
            if (option == "--receiver") duplicate.push_back("2");
            if (option == "--frequency-khz") duplicate.push_back("500000");
            if (option == "--seconds") duplicate.push_back("5");
            if (option == "--output") duplicate.push_back("capture2.ts");
        }
        TS_PROBE_CHECK(!parse(duplicate).valid);
    }

    Arguments unknown = valid_arguments();
    unknown.push_back("--unknown");
    TS_PROBE_CHECK(!parse(unknown).valid);

    const std::array<std::string, 7U> malformed{
        "--device", "0", "--receiver", "3", "--frequency-khz", "not-a-number", "--seconds"};
    Arguments malformed_device = valid_arguments();
    malformed_device[6] = malformed[1];
    TS_PROBE_CHECK(!parse(malformed_device).valid);
    Arguments malformed_receiver = valid_arguments();
    malformed_receiver[8] = malformed[3];
    TS_PROBE_CHECK(!parse(malformed_receiver).valid);
    Arguments malformed_frequency = valid_arguments();
    malformed_frequency[10] = malformed[5];
    TS_PROBE_CHECK(!parse(malformed_frequency).valid);
    Arguments malformed_seconds = valid_arguments();
    malformed_seconds[12] = "5x";
    TS_PROBE_CHECK(!parse(malformed_seconds).valid);
    Arguments empty_output = valid_arguments();
    empty_output[14] = "";
    TS_PROBE_CHECK(!parse(empty_output).valid);
    Arguments empty_base = valid_arguments();
    empty_base[2] = "";
    TS_PROBE_CHECK(!parse(empty_base).valid);
    Arguments empty_firmware = valid_arguments();
    empty_firmware[4] = "";
    TS_PROBE_CHECK(!parse(empty_firmware).valid);

    Arguments null_value = valid_arguments();
    null_value[2] = "";
    TS_PROBE_CHECK(!parse(null_value).valid);
    const char* null_option_value[] = {"px4-ts-probe", "--base", nullptr};
    TS_PROBE_CHECK(!parse_ts_probe_arguments(3, null_option_value).valid);
    const char* null_argv[] = {"px4-ts-probe", nullptr};
    TS_PROBE_CHECK(!parse_ts_probe_arguments(2, null_argv).valid);
    TS_PROBE_CHECK(!parse_ts_probe_arguments(1, nullptr).valid);
    const char* null_program[] = {nullptr};
    TS_PROBE_CHECK(!parse_ts_probe_arguments(1, null_program).valid);

    for (const std::uint32_t frequency : {40000U, 1002000U}) {
        Arguments boundary = valid_arguments();
        boundary[10] = std::to_string(frequency);
        TS_PROBE_CHECK(parse(boundary).valid);
    }
    for (const std::uint32_t frequency : {39999U, 1002001U}) {
        Arguments boundary = valid_arguments();
        boundary[10] = std::to_string(frequency);
        TS_PROBE_CHECK(!parse(boundary).valid);
    }
    for (const std::uint32_t seconds : {1U, 30U}) {
        Arguments boundary = valid_arguments();
        boundary[12] = std::to_string(seconds);
        TS_PROBE_CHECK(parse(boundary).valid);
    }
    for (const std::uint32_t seconds : {0U, 31U}) {
        Arguments boundary = valid_arguments();
        boundary[12] = std::to_string(seconds);
        TS_PROBE_CHECK(!parse(boundary).valid);
    }

    const std::array<std::string, 4U> satellite_options{
        "--slot", "--symbol-rate", "--rolloff", "--lnb-voltage"};
    for (const std::string& option : satellite_options) {
        Arguments missing = valid_satellite_arguments();
        for (std::size_t index = 0U; index < missing.size(); ++index) {
            if (missing[index] != option) continue;
            missing.erase(missing.begin() + static_cast<std::ptrdiff_t>(index));
            missing.erase(missing.begin() + static_cast<std::ptrdiff_t>(index));
            break;
        }
        TS_PROBE_CHECK(!parse(missing).valid);

        Arguments duplicate = valid_satellite_arguments();
        duplicate.push_back(option);
        duplicate.push_back(option == "--slot" ? "3" :
                             option == "--symbol-rate" ? "28860" :
                             option == "--rolloff" ? "4" : "0");
        TS_PROBE_CHECK(!parse(duplicate).valid);
    }
    for (const std::uint32_t slot : {12U, 255U}) {
        Arguments invalid_slot = valid_satellite_arguments();
        invalid_slot[12] = std::to_string(slot);
        TS_PROBE_CHECK(!parse(invalid_slot).valid);
    }
    for (const std::uint32_t symbol_rate : {28859U, 28861U}) {
        Arguments invalid_symbol = valid_satellite_arguments();
        invalid_symbol[14] = std::to_string(symbol_rate);
        TS_PROBE_CHECK(!parse(invalid_symbol).valid);
    }
    for (const std::uint32_t rolloff : {3U, 5U}) {
        Arguments invalid_rolloff = valid_satellite_arguments();
        invalid_rolloff[16] = std::to_string(rolloff);
        TS_PROBE_CHECK(!parse(invalid_rolloff).valid);
    }
    Arguments invalid_lnb = valid_satellite_arguments();
    invalid_lnb[18] = "15";
    TS_PROBE_CHECK(!parse(invalid_lnb).valid);
    Arguments terrestrial_satellite_option = valid_arguments();
    terrestrial_satellite_option.insert(terrestrial_satellite_option.end(),
                                        {"--slot", "3"});
    TS_PROBE_CHECK(!parse(terrestrial_satellite_option).valid);
    Arguments satellite_wrong_receiver = valid_satellite_arguments();
    satellite_wrong_receiver[8] = "2";
    TS_PROBE_CHECK(!parse(satellite_wrong_receiver).valid);
    Arguments satellite_wrong_device = valid_satellite_arguments();
    satellite_wrong_device[6] = "0";
    TS_PROBE_CHECK(!parse(satellite_wrong_device).valid);
    for (const std::uint32_t frequency : {146874U, 2350001U}) {
        Arguments invalid_frequency = valid_satellite_arguments();
        invalid_frequency[10] = std::to_string(frequency);
        TS_PROBE_CHECK(!parse(invalid_frequency).valid);
    }
    for (const std::uint32_t frequency : {146875U, 2350000U}) {
        Arguments valid_frequency = valid_satellite_arguments();
        valid_frequency[10] = std::to_string(frequency);
        TS_PROBE_CHECK(parse(valid_frequency).valid);
    }
    return true;
}

struct Writer final {
    std::vector<std::uint8_t> bytes;
    std::size_t forced_result = static_cast<std::size_t>(-1);
};

std::size_t write_bytes(void* context, const void* data, std::size_t size) noexcept
{
    auto& writer = *static_cast<Writer*>(context);
    const std::size_t result = writer.forced_result == static_cast<std::size_t>(-1)
                                   ? size : writer.forced_result;
    const std::size_t copied = result < size ? result : size;
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    writer.bytes.insert(writer.bytes.end(), bytes, bytes + copied);
    return result;
}

std::array<std::uint8_t, TaggedTsDemux::kPacketSize> packet(std::uint8_t tag)
{
    std::array<std::uint8_t, TaggedTsDemux::kPacketSize> result{};
    result[0U] = static_cast<std::uint8_t>((tag << 4U) | 0x07U);
    result[1U] = 0x40U;
    return result;
}

bool test_sink()
{
    Writer writer;
    TsProbeSink sink{write_bytes, &writer, {}};
    TaggedTsDemux demux;
    std::array<std::uint8_t, TaggedTsDemux::kPacketSize * 4U> stream{};
    for (std::uint8_t tag = 1U; tag <= 4U; ++tag) {
        const auto value = packet(tag);
        std::memcpy(stream.data() + ((tag - 1U) * TaggedTsDemux::kPacketSize),
                    value.data(), value.size());
    }
    TS_PROBE_CHECK(demux.push(ByteView{stream.data(), stream.size()}, write_ts_probe_packet,
                              &sink));
    TS_PROBE_CHECK(demux.counters().emitted_packets == 4U);
    TS_PROBE_CHECK((sink.counters.observed_packets ==
                    std::array<std::size_t, 4U>{1U, 1U, 1U, 1U}));
    TS_PROBE_CHECK(sink.counters.selected_packets == 1U &&
                   sink.counters.selected_bytes == TaggedTsDemux::kPacketSize);
    TS_PROBE_CHECK(sink.counters.output_bytes == TaggedTsDemux::kPacketSize);
    TS_PROBE_CHECK(writer.bytes.size() == TaggedTsDemux::kPacketSize);

    const std::array<std::uint8_t, 187U> short_packet{};
    TS_PROBE_CHECK(!write_ts_probe_packet(&sink, 2U,
                                          ByteView{short_packet.data(), short_packet.size()}));
    TS_PROBE_CHECK(sink.counters.selected_packets == 1U);

    TS_PROBE_CHECK(write_ts_probe_packet(&sink, 4U,
                                         ByteView{stream.data(), TaggedTsDemux::kPacketSize})
                       .error() == Error::INVALID_ARGUMENT);
    TS_PROBE_CHECK((sink.counters.observed_packets ==
                    std::array<std::size_t, 4U>{1U, 1U, 1U, 1U}));

    Writer failing_writer;
    failing_writer.forced_result = 0U;
    TsProbeSink failing{write_bytes, &failing_writer, {}};
    const auto selected = packet(3U);
    const auto failed = write_ts_probe_packet(&failing, 2U,
                                              ByteView{selected.data(), selected.size()});
    TS_PROBE_CHECK(!failed && failed.error() == Error::USB_IO);
    TS_PROBE_CHECK(failing.counters.selected_packets == 0U &&
                   failing.counters.selected_bytes == 0U);
    TS_PROBE_CHECK(failing.counters.output_bytes == 0U);
    TS_PROBE_CHECK(failing.counters.observed_packets[2U] == 1U);
    return true;
}

bool test_acceptance()
{
    TsProbeSinkCounters sink;
    sink.observed_packets[2U] = 2000U;
    sink.selected_packets = 2000U;
    sink.selected_bytes = 2000U * TaggedTsDemux::kPacketSize;
    sink.output_bytes = sink.selected_bytes;
    TaggedTsDemux::Counters demux{sink.output_bytes, 2000U, 17U, 0U, 0U, 751U};
    const auto accepted = evaluate_ts_probe_acceptance(2U, sink, demux);
    TS_PROBE_CHECK(accepted.accepted && accepted.failure_count == 0U);

    TsProbeSinkCounters satellite_sink;
    satellite_sink.observed_packets[0U] = 2000U;
    satellite_sink.selected_packets = 2000U;
    satellite_sink.selected_bytes = 2000U * TaggedTsDemux::kPacketSize;
    satellite_sink.output_bytes = satellite_sink.selected_bytes;
    const auto satellite_accepted = evaluate_ts_probe_acceptance(
        2U, satellite_sink,
        TaggedTsDemux::Counters{satellite_sink.output_bytes, 2000U, 0U, 0U, 0U, 0U}, 0U);
    TS_PROBE_CHECK(satellite_accepted.accepted);

    Writer satellite_writer;
    TsProbeSink satellite_sink_with_writer{write_bytes, &satellite_writer, {}, 0U};
    const auto satellite_packet = packet(1U);
    TS_PROBE_CHECK(write_ts_probe_packet(
        &satellite_sink_with_writer, 0U,
        ByteView{satellite_packet.data(), satellite_packet.size()}));
    TS_PROBE_CHECK(satellite_sink_with_writer.counters.selected_packets == 1U &&
                   satellite_sink_with_writer.counters.observed_packets[0U] == 1U);

    sink.observed_packets[2U] = 2001U;
    const auto observed_mismatch = evaluate_ts_probe_acceptance(2U, sink, demux);
    TS_PROBE_CHECK(!observed_mismatch.accepted && observed_mismatch.failure_count == 1U);
    TS_PROBE_CHECK(observed_mismatch.failures[0U] ==
                   TsProbeAcceptanceFailure::selected_observed_packets_mismatch);
    TS_PROBE_CHECK(std::string(ts_probe_acceptance_failure_string(
                       TsProbeAcceptanceFailure::selected_observed_packets_mismatch)) ==
                   "selected observed packet count mismatch");

    sink.observed_packets[2U] = 1999U;
    sink.selected_packets = 1999U;
    sink.selected_bytes = 1999U * TaggedTsDemux::kPacketSize;
    sink.output_bytes = sink.selected_bytes;
    demux.input_bytes_accepted = sink.output_bytes;
    demux.buffered_bytes = 751U;
    const auto below_floor = evaluate_ts_probe_acceptance(2U, sink, demux);
    TS_PROBE_CHECK(!below_floor.accepted && below_floor.failure_count == 1U);
    TS_PROBE_CHECK(below_floor.failures[0U] ==
                   TsProbeAcceptanceFailure::insufficient_selected_packets);

    sink.selected_packets = 2000U;
    sink.observed_packets[2U] = 2000U;
    sink.selected_bytes = 1U;
    sink.output_bytes = 3U;
    sink.observed_packets[0U] = 1U;
    demux.invalid_tag_packets = 1U;
    demux.buffered_bytes = 4U * TaggedTsDemux::kPacketSize;
    const auto rejected = evaluate_ts_probe_acceptance(2U, sink, demux);
    TS_PROBE_CHECK(!rejected.accepted && rejected.failure_count == 6U);
    TS_PROBE_CHECK(rejected.failures[0U] == TsProbeAcceptanceFailure::invalid_tag_packets);
    TS_PROBE_CHECK(rejected.failures[1U] == TsProbeAcceptanceFailure::unexpected_receiver_packets);
    TS_PROBE_CHECK(rejected.failures[2U] == TsProbeAcceptanceFailure::selected_bytes_mismatch);
    TS_PROBE_CHECK(rejected.failures[3U] == TsProbeAcceptanceFailure::output_bytes_mismatch);
    TS_PROBE_CHECK(rejected.failures[4U] == TsProbeAcceptanceFailure::output_not_packet_aligned);
    TS_PROBE_CHECK(rejected.failures[5U] == TsProbeAcceptanceFailure::demux_buffered_bytes);
    TS_PROBE_CHECK(std::string(ts_probe_acceptance_failure_string(
                       TsProbeAcceptanceFailure::output_bytes_mismatch)) ==
                   "output byte count mismatch");
    return true;
}

bool test_wait_and_cleanup_helpers()
{
    TS_PROBE_CHECK(classify_ts_probe_wait(Error::TIMEOUT, true) ==
                   TsProbeWaitDisposition::retry);
    TS_PROBE_CHECK(classify_ts_probe_wait(Error::TIMEOUT, false) ==
                   TsProbeWaitDisposition::deadline);
    TS_PROBE_CHECK(classify_ts_probe_wait(Error::USB_IO, true) ==
                   TsProbeWaitDisposition::fatal);
    TS_PROBE_CHECK(classify_ts_probe_wait(Error::NOT_READY, false) ==
                   TsProbeWaitDisposition::fatal);
    TS_PROBE_CHECK(ts_probe_cleanup_status(5, Error::USB_IO) == 5);
    TS_PROBE_CHECK(ts_probe_cleanup_status(0, Error::USB_IO) == 6);
    TS_PROBE_CHECK(ts_probe_cleanup_status(0, Error::OK) == 0);
    return true;
}

}  // namespace

bool run_ts_probe_tests()
{
    return test_arguments() && test_sink() && test_acceptance() && test_wait_and_cleanup_helpers();
}

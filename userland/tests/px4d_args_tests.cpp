// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_args.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace px4::userland;
using Arguments = std::vector<std::string>;

#define PX4D_CHECK(condition)                                                        \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::fprintf(stderr, "px4d args check failed at %s:%d: %s\n",         \
                         __FILE__, __LINE__, #condition);                            \
            return false;                                                            \
        }                                                                            \
    } while (false)

Px4dArguments parse(const Arguments& arguments)
{
    std::vector<const char*> pointers;
    pointers.reserve(arguments.size());
    for (const std::string& argument : arguments) {
        pointers.push_back(argument.c_str());
    }
    return parse_px4d_arguments(static_cast<int>(pointers.size()), pointers.data());
}

Arguments native_arguments()
{
    return {"px4d", "--device", "serial:00001205000960", "--firmware",
            "firmware.bin", "--runtime-dir", "/tmp/px4d"};
}

Arguments fd_arguments()
{
    return {"px4d", "--fd", "10", "--fd", "11", "--firmware",
            "firmware.bin"};
}

bool test_valid_modes()
{
    const Px4dArguments native = parse(native_arguments());
    PX4D_CHECK(native.valid && !native.help && !native.group);
    PX4D_CHECK(!native.allow_lnb_power);
    PX4D_CHECK(native.device == "serial:00001205000960");
    PX4D_CHECK(native.firmware == "firmware.bin" &&
               native.runtime_directory == "/tmp/px4d");
    PX4D_CHECK(native.file_descriptor_count == 0U);
    PX4D_CHECK(!native.usb_selector && native.usb_path_count == 0U &&
               native.instance.empty());
    PX4D_CHECK(px4d_open_mode(native) == Px4dOpenMode::native);
    const std::string base_serial = "00001205000960";
    PX4D_CHECK(px4d_selected_instance(native, base_serial) == base_serial);

    const Px4dArguments descriptors = parse(fd_arguments());
    PX4D_CHECK(descriptors.valid && descriptors.device.empty());
    PX4D_CHECK(descriptors.file_descriptor_count == 2U &&
               descriptors.file_descriptors[0U] == 10 &&
               descriptors.file_descriptors[1U] == 11);
    PX4D_CHECK(px4d_open_mode(descriptors) == Px4dOpenMode::file_descriptors);

    Arguments filtered = fd_arguments();
    filtered.insert(filtered.end(), {"--device", "serial:00001205000960", "--group"});
    const Px4dArguments filtered_result = parse(filtered);
    PX4D_CHECK(filtered_result.valid && filtered_result.group &&
               filtered_result.device == "serial:00001205000960");

    Arguments lnb = native_arguments();
    lnb.push_back("--allow-lnb-power");
    const Px4dArguments lnb_result = parse(lnb);
    PX4D_CHECK(lnb_result.valid && lnb_result.allow_lnb_power);

    Arguments selected{"px4d", "--device", "usb:1-2.3,2-42", "--firmware",
                        "firmware.bin", "--instance", "q3u4.left"};
    const Px4dArguments selected_result = parse(selected);
    PX4D_CHECK(selected_result.valid && selected_result.usb_selector &&
               selected_result.device == "usb:1-2.3,2-42" &&
               selected_result.usb_path_count == 2U &&
               selected_result.usb_paths[0U] == "1-2.3" &&
               selected_result.usb_paths[1U] == "2-42" &&
               selected_result.instance == "q3u4.left");
    PX4D_CHECK(px4d_selected_instance(selected_result, base_serial) == "q3u4.left");

    const Px4dArguments single_path = parse(
        {"px4d", "--device", "usb:1-2.3", "--firmware", "firmware.bin",
         "--instance", "q3u4.left"});
    PX4D_CHECK(single_path.valid && single_path.usb_selector &&
               single_path.usb_path_count == 1U &&
               single_path.usb_paths[0U] == "1-2.3");

    Arguments named_fd = fd_arguments();
    named_fd.insert(named_fd.end(), {"--instance", "fd_device-1"});
    PX4D_CHECK(parse(named_fd).valid);
    Arguments numeric_instance = fd_arguments();
    numeric_instance.insert(numeric_instance.end(), {"--instance", "123"});
    const Px4dArguments numeric_instance_result = parse(numeric_instance);
    PX4D_CHECK(numeric_instance_result.valid &&
               numeric_instance_result.instance == "123");
    Arguments lowercase_instance = fd_arguments();
    lowercase_instance.insert(lowercase_instance.end(), {"--instance", "rooma"});
    const Px4dArguments lowercase_instance_result = parse(lowercase_instance);
    PX4D_CHECK(lowercase_instance_result.valid &&
               lowercase_instance_result.instance == "rooma");

    Arguments limits = fd_arguments();
    limits[2U] = "0";
    limits[4U] = std::to_string(std::numeric_limits<int>::max());
    const Px4dArguments limit_result = parse(limits);
    PX4D_CHECK(limit_result.valid && limit_result.file_descriptors[0U] == 0 &&
               limit_result.file_descriptors[1U] == std::numeric_limits<int>::max());
    return true;
}

bool test_fd_rejections()
{
    // v0.16: one --fd is the PX-MLT5PE/DTV02A-5TS-P form; zero --fd without
    // --device is still rejected below.
    Arguments one_fd = fd_arguments();
    one_fd.erase(one_fd.begin() + 3, one_fd.begin() + 5);
    const Px4dArguments one_fd_result = parse(one_fd);
    PX4D_CHECK(one_fd_result.valid && one_fd_result.file_descriptor_count == 1U &&
               one_fd_result.file_descriptors[0U] == 10);
    PX4D_CHECK(px4d_open_mode(one_fd_result) == Px4dOpenMode::file_descriptors);

    Arguments three_fds = fd_arguments();
    three_fds.insert(three_fds.end(), {"--fd", "12"});
    PX4D_CHECK(!parse(three_fds).valid);

    Arguments duplicate = fd_arguments();
    duplicate[4U] = "10";
    PX4D_CHECK(!parse(duplicate).valid);

    Arguments negative = fd_arguments();
    negative[2U] = "-1";
    PX4D_CHECK(!parse(negative).valid);

    Arguments overflow = fd_arguments();
    overflow[2U] = std::to_string(
        static_cast<std::uint64_t>(std::numeric_limits<int>::max()) + 1U);
    PX4D_CHECK(!parse(overflow).valid);

    Arguments huge = fd_arguments();
    huge[2U] = "999999999999999999999999999999999999";
    PX4D_CHECK(!parse(huge).valid);

    Arguments malformed = fd_arguments();
    malformed[2U] = "10x";
    PX4D_CHECK(!parse(malformed).valid);

    Arguments signed_value = fd_arguments();
    signed_value[2U] = "+10";
    PX4D_CHECK(!parse(signed_value).valid);

    Arguments missing_value = fd_arguments();
    missing_value.erase(missing_value.begin() + 2);
    PX4D_CHECK(!parse(missing_value).valid);
    return true;
}

bool test_device_and_general_rejections()
{
    const Arguments no_device{"px4d", "--firmware", "firmware.bin"};
    PX4D_CHECK(!parse(no_device).valid);

    Arguments bad_native = native_arguments();
    bad_native[2U] = "serial:0000120500096";
    PX4D_CHECK(!parse(bad_native).valid);
    bad_native[2U] = "serial:0000120500096x";
    PX4D_CHECK(!parse(bad_native).valid);
    bad_native[2U] = "00001205000960";
    PX4D_CHECK(!parse(bad_native).valid);

    Arguments bad_fd_filter = fd_arguments();
    bad_fd_filter.insert(bad_fd_filter.end(), {"--device", "wrong"});
    PX4D_CHECK(!parse(bad_fd_filter).valid);

    Arguments no_firmware = native_arguments();
    no_firmware.erase(no_firmware.begin() + 3, no_firmware.begin() + 5);
    PX4D_CHECK(!parse(no_firmware).valid);

    Arguments duplicate_device = native_arguments();
    duplicate_device.insert(duplicate_device.end(),
                            {"--device", "serial:00001205000960"});
    PX4D_CHECK(!parse(duplicate_device).valid);

    Arguments unknown = native_arguments();
    unknown.push_back("--unknown");
    PX4D_CHECK(!parse(unknown).valid);

    Arguments duplicate_lnb = native_arguments();
    duplicate_lnb.insert(duplicate_lnb.end(),
                         {"--allow-lnb-power", "--allow-lnb-power"});
    PX4D_CHECK(!parse(duplicate_lnb).valid);

#if defined(_WIN32)
    Arguments exit_on_eof = native_arguments();
    exit_on_eof.push_back("--exit-on-stdin-eof");
    const auto parsed_exit_on_eof = parse(exit_on_eof);
    PX4D_CHECK(parsed_exit_on_eof.valid && parsed_exit_on_eof.exit_on_stdin_eof);

    Arguments duplicate_exit_on_eof = native_arguments();
    duplicate_exit_on_eof.insert(duplicate_exit_on_eof.end(),
                                 {"--exit-on-stdin-eof", "--exit-on-stdin-eof"});
    PX4D_CHECK(!parse(duplicate_exit_on_eof).valid);
#else
    // The cooperative parent-stop flag is a Windows-only contract; every other
    // platform must reject it rather than accept a silent no-op.
    Arguments posix_exit_on_eof = native_arguments();
    posix_exit_on_eof.push_back("--exit-on-stdin-eof");
    PX4D_CHECK(!parse(posix_exit_on_eof).valid);
#endif

    const Arguments usb_without_instance{"px4d", "--device", "usb:1-2.3",
                                         "--firmware", "firmware.bin"};
    PX4D_CHECK(!parse(usb_without_instance).valid);
    Arguments fd_and_usb = fd_arguments();
    fd_and_usb.insert(fd_and_usb.end(), {"--device", "usb:1-2.3",
                                         "--instance", "fd"});
    PX4D_CHECK(!parse(fd_and_usb).valid);
    Arguments old_usb_path = native_arguments();
    old_usb_path.insert(old_usb_path.end(), {"--usb-path", "1-2"});
    PX4D_CHECK(!parse(old_usb_path).valid);
    for (const char* selector : {"", "serial:", "serial:bad", "usb:", "usb:1:2",
                                 "usb:01-2", "usb:1-0", "usb:1-2.",
                                 "usb:1-1,", "usb:1-1,1-1"}) {
        const Arguments invalid_selector{"px4d", "--device", selector,
                                         "--firmware", "firmware.bin",
                                         "--instance", "named"};
        const Px4dArguments parsed = parse(invalid_selector);
        PX4D_CHECK(!parsed.valid);
        if (std::string_view(selector) == "usb:") {
            PX4D_CHECK(parsed.error.find("usb:BUS-PORT[,BUS-PORT]") !=
                       std::string_view::npos);
        }
    }
    const Arguments three_paths{"px4d", "--device", "usb:1-1,1-2,1-3",
                                "--firmware", "firmware.bin", "--instance", "named"};
    PX4D_CHECK(!parse(three_paths).valid);
    const Arguments address_selector{"px4d", "--device", "usb:1:2",
                                     "--firmware", "firmware.bin",
                                     "--instance", "named"};
    PX4D_CHECK(!parse(address_selector).valid);
    Arguments bad_instance = native_arguments();
    bad_instance.insert(bad_instance.end(), {"--instance", "../bad"});
    const Px4dArguments bad_instance_result = parse(bad_instance);
    PX4D_CHECK(!bad_instance_result.valid);
    PX4D_CHECK(bad_instance_result.error.find("1-64 ASCII") !=
               std::string_view::npos);
    for (const char* invalid : {".", "..", ".a", "RoomA.", "CON", "room/name",
                                "room:name"}) {
        Arguments invalid_instance = native_arguments();
        invalid_instance.insert(invalid_instance.end(), {"--instance", invalid});
        PX4D_CHECK(!parse(invalid_instance).valid);
    }
    const std::string long_instance(65U, 'a');
    Arguments overlong_instance = native_arguments();
    overlong_instance.insert(overlong_instance.end(), {"--instance", long_instance});
    PX4D_CHECK(!parse(overlong_instance).valid);
    Arguments serial_instance = native_arguments();
    serial_instance.insert(serial_instance.end(), {"--instance", "999999999999999"});
    PX4D_CHECK(parse(serial_instance).valid);
    const Arguments same_instance{"px4d", "--device", "usb:1-2",
                                  "--firmware", "firmware.bin", "--instance",
                                  "00001205000960"};
    PX4D_CHECK(parse(same_instance).valid);

    const Arguments help{"px4d", "--help"};
    const Px4dArguments help_result = parse(help);
    PX4D_CHECK(help_result.valid && help_result.help);
    Arguments combined_help = help;
    combined_help.insert(combined_help.end(), {"--fd", "10", "--fd", "11"});
    PX4D_CHECK(parse(combined_help).valid && parse(combined_help).help);
    PX4D_CHECK(parse({"px4d", "--help", "--unknown"}).help);
    PX4D_CHECK(parse({"px4d", "--version", "--unknown"}).version);
    PX4D_CHECK(parse({"px4d", "--version", "--help"}).help);
    const auto firmware_named_help = parse({"px4d", "--device",
        "serial:00001205000960", "--firmware", "-h"});
    PX4D_CHECK(firmware_named_help.valid && firmware_named_help.firmware == "-h");

    PX4D_CHECK(!parse_px4d_arguments(0, nullptr).valid);
    const char* const null_program[]{nullptr};
    PX4D_CHECK(!parse_px4d_arguments(1, null_program).valid);
    return true;
}

// `--list-json` needs neither firmware nor a device and must stand alone, so a
// listing can never be mistaken for starting a daemon.
bool test_list_mode()
{
    PX4D_CHECK(!parse({"px4d", "--list"}).valid);
    PX4D_CHECK(!parse({"px4d", "-l"}).valid);

    Arguments combined = native_arguments();
    combined.push_back("--list");
    PX4D_CHECK(!parse(combined).valid);
    PX4D_CHECK(!parse({"px4d", "--list", "--list"}).valid);
    PX4D_CHECK(parse({"px4d", "--list", "--help"}).help);
    const Px4dArguments json = parse({"px4d", "--list-json"});
    PX4D_CHECK(json.valid && json.list_json && json.device.empty());
    PX4D_CHECK(!parse({"px4d", "--list", "--list-json"}).valid);
    PX4D_CHECK(!parse({"px4d", "--list-json", "--list"}).valid);
    PX4D_CHECK(!parse({"px4d", "--list-json", "--list-json"}).valid);
    combined = native_arguments();
    combined.push_back("--list-json");
    PX4D_CHECK(!parse(combined).valid);
    return true;
}

// `--models` is a static table print: it stands alone like `--list-json`.
bool test_models_mode()
{
    const Px4dArguments models = parse({"px4d", "--models"});
    PX4D_CHECK(models.valid && models.models && !models.list_json);
    PX4D_CHECK(models.device.empty() && models.firmware.empty());
    PX4D_CHECK(!parse(native_arguments()).models);
    PX4D_CHECK(!parse({"px4d", "--models", "--models"}).valid);
    PX4D_CHECK(!parse({"px4d", "--models", "--list"}).valid);
    PX4D_CHECK(!parse({"px4d", "--list-json", "--models"}).valid);
    PX4D_CHECK(parse({"px4d", "--models", "--help"}).help);
    PX4D_CHECK(parse({"px4d", "--help", "--models"}).help);
    Arguments combined = native_arguments();
    combined.push_back("--models");
    PX4D_CHECK(!parse(combined).valid);
    return true;
}

bool test_version_mode()
{
    const Px4dArguments version = parse({"px4d", "--version"});
    PX4D_CHECK(version.valid && version.version && !version.help);
    PX4D_CHECK(version.device.empty() && version.firmware.empty());
    PX4D_CHECK(!parse(native_arguments()).version);
    for (const char* option : {"--version", "--help", "--models", "--list-json",
                               "--device", "--unknown"}) {
        const auto after = parse({"px4d", "--version", option});
        const auto before = parse({"px4d", option, "--version"});
        PX4D_CHECK(after.valid && (option == std::string_view("--help")
            ? after.help : after.version));
        PX4D_CHECK(before.valid && (option == std::string_view("--help")
            ? before.help : before.version));
    }
    return true;
}

bool test_short_options_and_verbose()
{
    const auto help = parse({"px4d", "-h"});
    PX4D_CHECK(help.valid && help.help);
    for (const char* option : {"-h", "--help", "--version", "--models", "--list-json"}) {
        const auto with_version = parse({"px4d", "-v", option});
        if (option == std::string_view("-h") || option == std::string_view("--help"))
            PX4D_CHECK(with_version.valid && with_version.help);
        else if (option == std::string_view("--version"))
            PX4D_CHECK(with_version.valid && with_version.version);
        else
            PX4D_CHECK(!with_version.valid);
        const auto with_verbose = parse({"px4d", option, "--verbose"});
        if (option == std::string_view("-h") || option == std::string_view("--help"))
            PX4D_CHECK(with_verbose.valid && with_verbose.help);
        else if (option == std::string_view("--version"))
            PX4D_CHECK(with_verbose.valid && with_verbose.version);
        else
            PX4D_CHECK(!with_verbose.valid);
        PX4D_CHECK(parse({"px4d", "-h", option}).valid &&
                   parse({"px4d", "-h", option}).help);
    }
    Arguments short_firmware = native_arguments();
    short_firmware[3U] = "-F";
    PX4D_CHECK(!parse(short_firmware).valid);
    PX4D_CHECK(parse(short_firmware).error == "unknown argument");
    Arguments short_version = native_arguments();
    short_version.push_back("-V");
    PX4D_CHECK(!parse(short_version).valid &&
               parse(short_version).error == "unknown argument");
    for (const char* option : {"-v", "--verbose"}) {
        Arguments verbose = native_arguments();
        verbose.push_back(option);
        const auto parsed = parse(verbose);
        PX4D_CHECK(parsed.valid && parsed.verbose && parsed.firmware == "firmware.bin");
    }
    Arguments repeated_verbose = native_arguments();
    repeated_verbose.insert(repeated_verbose.end(), {"-v", "--verbose", "-v"});
    PX4D_CHECK(parse(repeated_verbose).valid && parse(repeated_verbose).verbose);
    PX4D_CHECK(!parse({"px4d", "--device", "serial:00001205000960", "--firmware"}).valid);
    PX4D_CHECK(!parse({"px4d", "--device", "serial:00001205000960", "--firmware", ""}).valid);
    PX4D_CHECK(parse({"px4d", "--device", "serial:00001205000960", "--firmware", "--help"}).help);
    Arguments duplicate = native_arguments();
    duplicate.insert(duplicate.end(), {"--firmware", "other.bin"});
    PX4D_CHECK(!parse(duplicate).valid);
    PX4D_CHECK(!parse(native_arguments()).verbose);
    return true;
}

}  // namespace

bool run_px4d_args_tests()
{
    return test_valid_modes() && test_fd_rejections() &&
           test_device_and_general_rejections() && test_list_mode() &&
           test_models_mode() && test_version_mode() && test_short_options_and_verbose();
}

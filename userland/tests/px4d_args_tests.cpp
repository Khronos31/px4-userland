// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_args.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>
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
    return {"px4d", "--device", "00001205000960", "--firmware",
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
    PX4D_CHECK(native.device == "00001205000960");
    PX4D_CHECK(native.firmware == "firmware.bin" &&
               native.runtime_directory == "/tmp/px4d");
    PX4D_CHECK(native.file_descriptor_count == 0U);
    PX4D_CHECK(px4d_open_mode(native) == Px4dOpenMode::native);

    const Px4dArguments descriptors = parse(fd_arguments());
    PX4D_CHECK(descriptors.valid && descriptors.device.empty());
    PX4D_CHECK(descriptors.file_descriptor_count == 2U &&
               descriptors.file_descriptors[0U] == 10 &&
               descriptors.file_descriptors[1U] == 11);
    PX4D_CHECK(px4d_open_mode(descriptors) == Px4dOpenMode::file_descriptors);

    Arguments filtered = fd_arguments();
    filtered.insert(filtered.end(), {"--device", "00001205000960", "--group"});
    const Px4dArguments filtered_result = parse(filtered);
    PX4D_CHECK(filtered_result.valid && filtered_result.group &&
               filtered_result.device == "00001205000960");

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
    Arguments one_fd = fd_arguments();
    one_fd.erase(one_fd.begin() + 3, one_fd.begin() + 5);
    PX4D_CHECK(!parse(one_fd).valid);

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
    bad_native[2U] = "0000120500096";
    PX4D_CHECK(!parse(bad_native).valid);
    bad_native[2U] = "0000120500096x";
    PX4D_CHECK(!parse(bad_native).valid);

    Arguments bad_fd_filter = fd_arguments();
    bad_fd_filter.insert(bad_fd_filter.end(), {"--device", "wrong"});
    PX4D_CHECK(!parse(bad_fd_filter).valid);

    Arguments no_firmware = native_arguments();
    no_firmware.erase(no_firmware.begin() + 3, no_firmware.begin() + 5);
    PX4D_CHECK(!parse(no_firmware).valid);

    Arguments duplicate_device = native_arguments();
    duplicate_device.insert(duplicate_device.end(),
                            {"--device", "00001205000960"});
    PX4D_CHECK(!parse(duplicate_device).valid);

    Arguments unknown = native_arguments();
    unknown.push_back("--unknown");
    PX4D_CHECK(!parse(unknown).valid);

    const Arguments help{"px4d", "--help"};
    const Px4dArguments help_result = parse(help);
    PX4D_CHECK(help_result.valid && help_result.help);
    Arguments combined_help = help;
    combined_help.insert(combined_help.end(), {"--fd", "10", "--fd", "11"});
    PX4D_CHECK(!parse(combined_help).valid);

    PX4D_CHECK(!parse_px4d_arguments(0, nullptr).valid);
    const char* const null_program[]{nullptr};
    PX4D_CHECK(!parse_px4d_arguments(1, null_program).valid);
    return true;
}

}  // namespace

bool run_px4d_args_tests()
{
    return test_valid_modes() && test_fd_rejections() &&
           test_device_and_general_rejections();
}

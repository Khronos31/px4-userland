// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_PX4D_ARGS_H
#define PX4_USERLAND_PX4D_ARGS_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace px4::userland {

enum class Px4dOpenMode : std::uint8_t {
    native,
    // One --fd per USB device of the enclosure: two for a PX-Q3U4, one for a
    // PX-MLT5PE/DTV02A-5TS-P.
    file_descriptors,
};

struct Px4dArguments final {
    bool valid = false;
    bool help = false;
    bool version = false;
    bool list_json = false;
    // `px4d --models`: print the supported-model table and exit.
    bool models = false;
    bool group = false;
    bool verbose = false;
    bool allow_lnb_power = false;
#if defined(_WIN32)
    // Windows Phase 1 cooperative parent-stop contract: exit with normal
    // cleanup when standard input reaches EOF. Other platforms reject the flag.
    bool exit_on_stdin_eof = false;
#endif
    std::string device;
    std::string instance;
    std::string firmware;
    std::string runtime_directory;
    std::array<int, 2U> file_descriptors{{-1, -1}};
    std::size_t file_descriptor_count = 0U;
    bool usb_selector = false;
    std::array<std::string, 2U> usb_paths{};
    std::size_t usb_path_count = 0U;
    std::string_view error;
};

Px4dArguments parse_px4d_arguments(int argc,
                                   const char* const* argv) noexcept;
Px4dOpenMode px4d_open_mode(const Px4dArguments& arguments) noexcept;
const std::string& px4d_selected_instance(const Px4dArguments& arguments,
                                          const std::string& base_serial) noexcept;
bool valid_px4d_base_serial(std::string_view value) noexcept;

}  // namespace px4::userland

#endif  // PX4_USERLAND_PX4D_ARGS_H

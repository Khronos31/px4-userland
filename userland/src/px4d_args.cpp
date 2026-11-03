// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_args.h"

#include "px4/identity.h"

#include <dtv/userland/identity.h>

#include <charconv>
#include <cstdint>
#include <limits>

namespace px4::userland {
namespace {

Px4dArguments invalid(std::string_view message) noexcept
{
    Px4dArguments result;
    result.error = message;
    return result;
}

bool take_value(int& index, int argc, const char* const* argv,
                std::string_view& value) noexcept
{
    if (index + 1 >= argc || argv[index + 1] == nullptr) return false;
    value = std::string_view(argv[++index]);
    return !value.empty() && value.rfind("--", 0U) != 0U;
}

bool parse_fd(std::string_view value, int& output) noexcept
{
    if (value.empty()) return false;
    std::uint64_t parsed = 0U;
    const auto result =
        std::from_chars(value.data(), value.data() + value.size(), parsed, 10);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() ||
        parsed > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    output = static_cast<int>(parsed);
    return true;
}

}  // namespace

bool valid_px4d_base_serial(std::string_view value) noexcept
{
    return valid_device_instance(value);
}

Px4dArguments parse_px4d_arguments(int argc,
                                   const char* const* argv) noexcept
{
    if (argc < 1 || argv == nullptr || argv[0] == nullptr) {
        return invalid("invalid argument vector");
    }

    bool request_help = false;
    bool request_version = false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) continue;
        const std::string_view option(argv[index]);
        if (option == "--help" || option == "-h") request_help = true;
        else if (option == "--version") request_version = true;
        if ((option == "--firmware" || option == "--runtime-dir") &&
            index + 1 < argc && argv[index + 1] != nullptr &&
            std::string_view(argv[index + 1]).rfind("--", 0U) != 0U) {
            ++index;
        } else if ((option == "--device" || option == "--instance") &&
                   index + 1 < argc && argv[index + 1] != nullptr &&
                   std::string_view(argv[index + 1]).rfind("-", 0U) != 0U) {
            ++index;
        } else if (option == "--fd" && index + 1 < argc &&
                   argv[index + 1] != nullptr &&
                   std::string_view(argv[index + 1]) != "-h" &&
                   std::string_view(argv[index + 1]) != "--help" &&
                   std::string_view(argv[index + 1]) != "--version") {
            ++index;
        }
    }
    if (request_help || request_version) {
        Px4dArguments result;
        result.valid = true;
        result.help = request_help;
        result.version = !request_help && request_version;
        return result;
    }

    Px4dArguments result;
    bool have_device = false;
    bool have_firmware = false;
    bool have_runtime = false;
    bool have_instance = false;
    bool have_allow_lnb_power = false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) return invalid("null argument");
        std::string_view option(argv[index]);
        if (option == "-h") option = "--help";
        else if (option == "-v") option = "--verbose";
        if (option == "--help" || option == "--version") return invalid("metadata scan failed");
        if (option == "--list-json") {
            if (argc != 2) return invalid("--list-json cannot be combined");
            result.valid = true;
            result.list_json = true;
            return result;
        }
        if (option == "--models") {
            if (argc != 2) return invalid("--models cannot be combined");
            result.valid = true;
            result.models = true;
            return result;
        }
        if (option == "--group") {
            if (result.group) return invalid("duplicate --group");
            result.group = true;
            continue;
        }
        if (option == "--verbose") {
            result.verbose = true;
            continue;
        }
        if (option == "--allow-lnb-power") {
            if (have_allow_lnb_power)
                return invalid("duplicate --allow-lnb-power");
            have_allow_lnb_power = true;
            result.allow_lnb_power = true;
            continue;
        }
#if defined(_WIN32)
        if (option == "--exit-on-stdin-eof") {
            if (result.exit_on_stdin_eof)
                return invalid("duplicate --exit-on-stdin-eof");
            result.exit_on_stdin_eof = true;
            continue;
        }
#endif
        if (option != "--device" && option != "--firmware" &&
            option != "--runtime-dir" && option != "--fd" &&
            option != "--instance") {
            return invalid("unknown argument");
        }

        std::string_view value;
        if (!take_value(index, argc, argv, value)) {
            return invalid("option requires a value");
        }
        if (option == "--device") {
            if (have_device) return invalid("duplicate --device");
            have_device = true;
            result.device = value;
        } else if (option == "--instance") {
            if (have_instance) return invalid("duplicate --instance");
            have_instance = true;
            result.instance = value;
        } else if (option == "--firmware") {
            if (have_firmware) return invalid("duplicate --firmware");
            have_firmware = true;
            result.firmware = value;
        } else if (option == "--runtime-dir") {
            if (have_runtime) return invalid("duplicate --runtime-dir");
            have_runtime = true;
            result.runtime_directory = value;
        } else {
            if (result.file_descriptor_count >= result.file_descriptors.size()) {
                return invalid("at most two --fd values are supported");
            }
            int fd = -1;
            if (!parse_fd(value, fd)) return invalid("--fd is invalid");
            for (std::size_t fd_index = 0U;
                 fd_index < result.file_descriptor_count; ++fd_index) {
                if (result.file_descriptors[fd_index] == fd) {
                    return invalid("--fd values must be distinct");
                }
            }
            result.file_descriptors[result.file_descriptor_count++] = fd;
        }
    }

    if (result.file_descriptor_count == 0U && !have_device) {
        return invalid("native mode requires --device");
    }
    if (have_device) {
        const auto selector = dtv::userland::identity::parse_device_selector(result.device);
        if (!selector)
            return invalid("--device requires serial: plus a 14- or 15-digit PX4 serial, "
                           "or usb:BUS-PORT[,BUS-PORT] with one or two current paths");
        if (selector.value().kind == dtv::userland::identity::SelectorKind::serial) {
            const std::string_view serial(selector.value().serial.bytes.data(),
                                          selector.value().serial.length);
            if (!valid_px4d_base_serial(serial))
                return invalid("PX4 serial selector must contain a 14- or 15-digit number");
        } else {
            if (selector.value().topology.count > result.usb_paths.size())
                return invalid("PX4 USB selector supports at most two paths");
            result.usb_selector = true;
            for (std::size_t index = 0U; index < selector.value().topology.count; ++index) {
                const auto& path = selector.value().topology.paths[index];
                result.usb_paths[result.usb_path_count++].assign(path.bytes.data(), path.length);
            }
        }
    }
    if (have_instance &&
        !dtv::userland::identity::canonicalize_instance(result.instance).has_value())
        return invalid("--instance must be a 1-64 ASCII connection token");
    if (result.usb_selector && result.file_descriptor_count != 0U)
        return invalid("USB topology selector cannot be used with --fd");
    if (result.usb_selector && !have_instance)
        return invalid("USB topology selector requires --instance");
    if (!have_firmware || result.firmware.empty()) {
        return invalid("--firmware is required");
    }
    if (have_runtime && result.runtime_directory.empty()) {
        return invalid("--runtime-dir must not be empty");
    }
    if (result.group && !have_runtime) {
        return invalid("--group requires --runtime-dir");
    }
    result.valid = true;
    return result;
}

Px4dOpenMode px4d_open_mode(const Px4dArguments& arguments) noexcept
{
    return arguments.file_descriptor_count != 0U ? Px4dOpenMode::file_descriptors
                                                 : Px4dOpenMode::native;
}

const std::string& px4d_selected_instance(const Px4dArguments& arguments,
                                          const std::string& base_serial) noexcept
{
    return arguments.instance.empty() ? base_serial : arguments.instance;
}

}  // namespace px4::userland

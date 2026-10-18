// SPDX-License-Identifier: GPL-2.0-only
#include "px4/posix_ipc.h"
#include "px4/identity.h"

#include <dtv/userland/control_client.h>
#include <dtv/userland/identity.h>
#include <dtv/userland/json.h>

#if defined(_WIN32)
#include "px4_windows_args.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace px4::userland::ipc::posix;
namespace wire = dtv::userland::wire;
namespace identity = dtv::userland::identity;
using CommonError = dtv::userland::Error;

#ifndef PX4CTL_TOOL_NAME
#define PX4CTL_TOOL_NAME "px4ctl"
#endif
#ifndef PX4CTL_PRODUCT_SLUG
#define PX4CTL_PRODUCT_SLUG "px4-userland"
#endif
#ifndef PX4CTL_PRODUCT_ENUM
#define PX4CTL_PRODUCT_ENUM px4
#endif
#ifndef PX4CTL_PRODUCT_VERSION
#define PX4CTL_PRODUCT_VERSION PX4_USERLAND_PRODUCT_VERSION
#endif

struct Arguments final {
    bool valid = false;
    bool help = false;
    bool group = false;
    std::string device;
    std::string instance;
    std::string runtime_directory;
    std::string_view error;
    bool status = false;
};

Arguments invalid(std::string_view message) noexcept
{
    Arguments result;
    result.error = message;
    return result;
}

Arguments parse_arguments(int argc, const char* const* argv) noexcept
{
    Arguments result;
    bool have_device = false;
    bool have_instance = false;
    bool have_runtime = false;
    for (int index = 1; index < argc; ++index) {
        if (argv[index] == nullptr) return invalid("null argument");
        const std::string_view option(argv[index]);
        if (option == "--help" || option == "-h") {
            if (argc != 2) return invalid("help cannot be combined");
            result.valid = true;
            result.help = true;
            return result;
        }
        if (option == "--group") {
            if (result.group) return invalid("duplicate --group");
            result.group = true;
            continue;
        }
        if (option == "--device" || option == "-d" || option == "--instance" ||
            option == "--runtime-dir") {
            if (index + 1 >= argc || argv[index + 1] == nullptr)
                return invalid("option requires a value");
            const std::string_view value(argv[++index]);
            if (option == "--device" || option == "-d") {
                if (have_device) return invalid("duplicate --device");
                have_device = true;
                result.device.assign(value);
            } else if (option == "--instance") {
                if (have_instance) return invalid("duplicate --instance");
                have_instance = true;
                result.instance.assign(value);
            } else {
                if (have_runtime) return invalid("duplicate --runtime-dir");
                have_runtime = true;
                result.runtime_directory.assign(value);
            }
            continue;
        }
        if (option == "status") {
            if (result.status) return invalid("duplicate status command");
            result.status = true;
            continue;
        }
        return invalid("only status is supported");
    }
    if (!result.status) return invalid("status command is required");
    if ((have_device && result.device.empty()) || (have_instance && result.instance.empty()))
        return invalid("selector value must not be empty");
    if (have_runtime && result.runtime_directory.empty())
        return invalid("--runtime-dir must not be empty");
#if !defined(_WIN32)
    if (result.group && !have_runtime)
        return invalid("--group requires --runtime-dir");
#endif
    if (have_instance) {
        const auto canonical = identity::canonicalize_instance(result.instance);
        if (!canonical) return invalid("--instance is invalid");
        result.instance.assign(canonical.value().bytes.data(), canonical.value().length);
    }
    result.valid = true;
    return result;
}

void usage(FILE* output) noexcept
{
    std::fprintf(output,
                 "usage: %s [--device serial:SERIAL|usb:BUS-PORT[,BUS-PORT...]] "
                 "[--instance TOKEN] [--runtime-dir PATH] [--group] status\n"
                 "       %s --help | --version\n", PX4CTL_TOOL_NAME, PX4CTL_TOOL_NAME);
}

class SocketTransport final : public wire::ClientTransport {
public:
    explicit SocketTransport(SocketStream stream) noexcept : stream_(std::move(stream)) {}

    dtv::userland::Result<void> write_all(dtv::userland::ByteView bytes,
                                          dtv::userland::Timeout timeout) noexcept override
    {
        const auto result = stream_.write_all(
            px4::userland::ByteView{bytes.data, bytes.size},
            px4::userland::Timeout{timeout.milliseconds});
        return result ? dtv::userland::Result<void>::success() :
                        dtv::userland::Result<void>::failure(
                            static_cast<CommonError>(result.error()));
    }

    dtv::userland::Result<std::size_t> read_some(
        dtv::userland::MutableByteView bytes,
        dtv::userland::Timeout timeout) noexcept override
    {
        const auto result = stream_.read_some(
            px4::userland::MutableByteView{bytes.data, bytes.size},
            px4::userland::Timeout{timeout.milliseconds});
        return result ? dtv::userland::Result<std::size_t>::success(result.value()) :
                        dtv::userland::Result<std::size_t>::failure(
                            static_cast<CommonError>(result.error()));
    }

    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void close() noexcept override { stream_.close(); }

private:
    SocketStream stream_;
};

dtv::userland::Result<identity::ResolvedIdentity> resolved_device(std::string_view value) noexcept
{
    const auto selector = identity::parse_device_selector(value);
    if (!selector) return dtv::userland::Result<identity::ResolvedIdentity>::failure(selector.error());
    return identity::resolve_selector(identity::Product::PX4CTL_PRODUCT_ENUM,
                                      selector.value());
}

bool matches_device(const wire::ListResponse& list,
                   const identity::ResolvedIdentity& expected) noexcept
{
    if (!list.enclosure.present) return false;
    if (expected.kind == identity::IdentityKind::serial) {
        const auto& identity = list.enclosure.value.identity;
        if (identity.uniqueness != wire::Uniqueness::unique_in_scan ||
            list.functions.count == 0U)
            return false;
#if defined(PX4CTL_PX4_IDENTITY)
        const px4::userland::DeviceProfile* profile =
            px4::userland::device_profile_for_usb_id(
            list.functions.items[0].vid, list.functions.items[0].pid);
        if (profile == nullptr || !profile->serial_identity_supported) return false;
        for (std::size_t index = 1U; index < list.functions.count; ++index) {
            const px4::userland::DeviceProfile* other =
                px4::userland::device_profile_for_usb_id(
                list.functions.items[index].vid, list.functions.items[index].pid);
            if (other == nullptr || other->model != profile->model) return false;
        }
        const auto& serial = list.enclosure.value.identity.serial;
        return identity.serial_state == wire::IdentitySerialState::present &&
               serial.present && serial.value.length == expected.serial.length &&
               std::memcmp(serial.value.bytes.data(), expected.serial.bytes.data(),
                           expected.serial.length) == 0;
#else
        const auto& serial = list.enclosure.value.identity.serial;
        return identity.serial_state == wire::IdentitySerialState::present &&
               identity.uniqueness == wire::Uniqueness::unique_in_scan &&
               serial.present && serial.value.length == expected.serial.length &&
               std::memcmp(serial.value.bytes.data(), expected.serial.bytes.data(),
                           expected.serial.length) == 0;
#endif
    }
    for (std::size_t i = 0U; i < expected.topology.count; ++i) {
        bool found = false;
        for (std::size_t j = 0U; j < list.functions.count; ++j) {
            const auto& path = list.functions.items[j].location.usb_path;
            if (path.present && path.value.length == expected.topology.paths[i].length &&
                std::memcmp(path.value.bytes.data(), expected.topology.paths[i].bytes.data(),
                            expected.topology.paths[i].length) == 0) {
                found = true;
                break;
            }
        }
        if (!found) return false;
    }
    return list.functions.count == expected.topology.count;
}

int exit_status(CommonError error) noexcept
{
    switch (error) {
    case CommonError::OK: return 0;
    case CommonError::INVALID_ARGUMENT: return 2;
    case CommonError::NOT_FOUND:
    case CommonError::NOT_READY:
    case CommonError::UNSUPPORTED: return 3;
    case CommonError::BUSY:
    case CommonError::RESOURCE_LIMIT: return 4;
    case CommonError::TIMEOUT: return 5;
    case CommonError::VERSION_MISMATCH:
    case CommonError::PROTOCOL_ERROR: return 6;
    case CommonError::USB_IO:
    case CommonError::DISCONNECTED: return 7;
    case CommonError::SLOW_CONSUMER: return 8;
    case CommonError::NO_CARD:
    case CommonError::CARD_REMOVED:
    case CommonError::BUFFER_TOO_SMALL: return 9;
    case CommonError::FIRMWARE_REJECTED: return 10;
    case CommonError::INTERNAL: return 70;
    }
    return 70;
}

}  // namespace

int main(int argc, char** argv)
{
#if defined(_WIN32)
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("%s %s\n", PX4CTL_PRODUCT_SLUG, PX4CTL_PRODUCT_VERSION);
        return 0;
    }
    const std::vector<std::string> owned_argv =
        px4::userland::cli::windows_argv_utf8(argc, argv);
    std::vector<const char*> argv_views;
    for (const std::string& value : owned_argv) argv_views.push_back(value.c_str());
    const Arguments arguments =
        parse_arguments(static_cast<int>(argv_views.size()), argv_views.data());
#else
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        std::printf("%s %s\n", PX4CTL_PRODUCT_SLUG, PX4CTL_PRODUCT_VERSION);
        return 0;
    }
    const Arguments arguments =
        parse_arguments(argc, const_cast<const char* const*>(argv));
#endif
    if (!arguments.valid) {
        std::fprintf(stderr, "%s: %.*s\n", PX4CTL_TOOL_NAME,
                     static_cast<int>(arguments.error.size()),
                     arguments.error.data());
        usage(stderr);
        return 2;
    }
    if (arguments.help) {
        usage(stdout);
        return 0;
    }
#if defined(_WIN32)
    if (arguments.group) {
        std::fprintf(stderr, "%s: --group is unsupported on Windows\n", PX4CTL_TOOL_NAME);
        return 3;
    }
#endif

    std::string instance = arguments.instance;
    identity::ResolvedIdentity expected{};
    const bool has_device = !arguments.device.empty();
    if (has_device) {
        const auto resolved = resolved_device(arguments.device);
        if (!resolved) {
            std::fprintf(stderr, "%s: invalid --device selector\n", PX4CTL_TOOL_NAME);
            return 2;
        }
        expected = resolved.value();
        if (instance.empty()) {
            const auto automatic = identity::auto_instance(expected);
            if (!automatic) return exit_status(automatic.error());
            instance.assign(automatic.value().bytes.data(), automatic.value().length);
        }
    }

    std::vector<std::string> runtime_directories;
    bool search_runtime_roots = arguments.runtime_directory.empty();
    if (!arguments.runtime_directory.empty()) {
        runtime_directories.push_back(arguments.runtime_directory);
    } else {
#if defined(_WIN32)
        const auto candidates = client_runtime_roots();
#else
        const auto candidates = client_runtime_roots();
#endif
        if (!candidates) return exit_status(candidates.error());
        for (std::size_t index = 0U; index < candidates.value().count; ++index)
            runtime_directories.emplace_back(candidates.value().paths[index].data());
    }

    struct RuntimeCandidate final {
        std::string runtime_directory;
        std::string instance;
    };
    const bool discover_instances = instance.empty();
    std::vector<RuntimeCandidate> endpoints;
    if (instance.empty()) {
        for (const std::string& runtime_directory : runtime_directories) {
            const auto discovered = discover_runtime_instances(
                runtime_directory.c_str(),
                arguments.group ? EndpointAccess::shared_group : EndpointAccess::private_user);
            if (!discovered) {
                if (search_runtime_roots &&
                    (discovered.error() == CommonError::NOT_FOUND ||
                     discovered.error() == CommonError::NOT_READY))
                    continue;
                return exit_status(discovered.error());
            }
            if (discovered.value().canonical_collision) {
                std::fprintf(stderr, "%s: ambiguous runtime instances\n", PX4CTL_TOOL_NAME);
                return 3;
            }
            for (std::size_t index = 0U; index < discovered.value().count; ++index)
                endpoints.push_back(RuntimeCandidate{
                    runtime_directory,
                    discovered.value().instances[index].data()});
        }
    } else {
        for (const std::string& runtime_directory : runtime_directories)
            endpoints.push_back(RuntimeCandidate{runtime_directory, instance});
    }
    if (endpoints.empty()) return 3;

    wire::StatusFetchResult snapshot{};
    bool found = false;
    for (const RuntimeCandidate& candidate : endpoints) {
        const EndpointConfig endpoint{
            candidate.runtime_directory.empty() ? nullptr :
                candidate.runtime_directory.c_str(),
            candidate.instance.c_str(), kControlEndpointName,
            arguments.group ? EndpointAccess::shared_group : EndpointAccess::private_user};
        auto stream = SocketStream::connect(endpoint, px4::userland::Timeout{2000U});
        if (!stream) {
            if ((discover_instances || search_runtime_roots) &&
                (stream.error() == CommonError::NOT_FOUND ||
                 stream.error() == CommonError::NOT_READY))
                continue;
            std::fprintf(stderr, "%s: connect failed (%u)\n", PX4CTL_TOOL_NAME,
                         static_cast<unsigned int>(stream.error()));
            return exit_status(stream.error());
        }
        SocketTransport transport(std::move(stream.value()));
        auto current = wire::fetch_status(transport, dtv::userland::Timeout{5000U});
        if (!current) {
            std::fprintf(stderr, "%s: status failed (%u, remote=%u)\n", PX4CTL_TOOL_NAME,
                         static_cast<unsigned int>(current.error),
                         static_cast<unsigned int>(current.remote_error_code));
            return exit_status(current.error);
        }
        static constexpr char expected_product[] = PX4CTL_PRODUCT_SLUG;
        const auto& product = current.snapshot.status.producer.product;
        if (product.length != sizeof(expected_product) - 1U ||
            std::memcmp(product.bytes.data(), expected_product,
                        sizeof(expected_product) - 1U) != 0) {
            std::fprintf(stderr, "%s: endpoint belongs to another product\n", PX4CTL_TOOL_NAME);
            return 3;
        }
        if (has_device && !matches_device(current.snapshot.list, expected)) {
            std::fprintf(stderr, "%s: connected instance does not match --device\n",
                         PX4CTL_TOOL_NAME);
            return 3;
        }
        if (!current.snapshot.status.context.instance.present) {
            std::fprintf(stderr, "%s: daemon omitted canonical instance\n", PX4CTL_TOOL_NAME);
            return 3;
        }
        const auto& actual_instance = current.snapshot.status.context.instance.value;
        if (actual_instance.length != candidate.instance.size() ||
            std::memcmp(actual_instance.bytes.data(), candidate.instance.data(),
                        candidate.instance.size()) != 0) {
            std::fprintf(stderr, "%s: connected instance does not match selector\n",
                         PX4CTL_TOOL_NAME);
            return 3;
        }
        if (found) {
            std::fprintf(stderr, "%s: multiple runtime roots publish this target\n",
                         PX4CTL_TOOL_NAME);
            return 3;
        }
        snapshot = std::move(current);
        found = true;
    }
    if (!found) return 3;
    wire::StatusContext context{&snapshot.snapshot.list};
    const auto size = wire::status_json_size(snapshot.snapshot.status, context);
    if (!size) return exit_status(size.error());
    std::vector<std::uint8_t> output(size.value());
    const auto encoded = wire::encode_status_json(snapshot.snapshot.status, context,
                                                   dtv::userland::MutableByteView{
                                                       output.data(), output.size()});
    if (!encoded || std::fwrite(output.data(), 1U, encoded.value(), stdout) != encoded.value() ||
        std::fflush(stdout) != 0) {
        std::fprintf(stderr, "%s: status output failed\n", PX4CTL_TOOL_NAME);
        return 70;
    }
    return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
#include "dtv/userland/host_runtime/control_socket.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <utility>

using dtv::userland::Error;
using dtv::userland::host_runtime::EndpointAccess;
using dtv::userland::host_runtime::EndpointConfig;
using dtv::userland::host_runtime::EndpointDiagnostic;
using dtv::userland::host_runtime::EndpointDiagnosticReason;

namespace {

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::fprintf(stderr, "core path check failed at line %d: %s\n", \
                         __LINE__, #expression); \
            return false; \
        } \
    } while (false)

constexpr const char* kProduct = "px4-userland";

bool make_root(std::string& output) noexcept
{
    char pattern[] = "/tmp/px4-core-path-XXXXXX";
    char* created = ::mkdtemp(pattern);
    if (created == nullptr) return false;
    output = created;
    if (::chmod(output.c_str(), 0700) != 0) return false;
    return true;
}

class RootGuard final {
public:
    explicit RootGuard(std::string path) noexcept : path_(std::move(path)) {}
    ~RootGuard() noexcept
    {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    RootGuard(const RootGuard&) = delete;
    RootGuard& operator=(const RootGuard&) = delete;

private:
    std::string path_;
};

bool test_core_path_layout()
{
    std::string root;
    CHECK(make_root(root));
    RootGuard guard(root);
    const EndpointConfig config{
        root, kProduct, "g-core",
        EndpointAccess::private_user, -1, "control.sock"};
    const auto path = dtv::userland::host_runtime::control_endpoint_path(config);
    CHECK(path);
    CHECK(path.value() == root + "/px4-userland/v1/g-core/control.sock");
    return true;
}

bool test_core_path_overlong()
{
    std::string root;
    CHECK(make_root(root));
    RootGuard guard(root);
    const std::string long_runtime = root + "/" + std::string(500U, 'x');
    EndpointDiagnostic diagnostic{};
    const EndpointConfig config{
        long_runtime, kProduct, "g-core",
        EndpointAccess::private_user, -1, "control.sock"};
    const auto path = dtv::userland::host_runtime::control_endpoint_path(
        config, &diagnostic);
    CHECK(!path && path.error() == Error::INVALID_ARGUMENT);
    CHECK(diagnostic.reason == EndpointDiagnosticReason::path_too_long);
    return true;
}

}  // namespace

int main()
{
    if (!test_core_path_layout() || !test_core_path_overlong()) {
        return 1;
    }
    std::printf("PX4 Core path contract fixtures passed\n");
    return 0;
}

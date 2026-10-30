// SPDX-License-Identifier: GPL-2.0-only
#include "dtv/userland/host_listener_context/identity_lease.h"
#include "dtv/userland/host_runtime/control_socket.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using dtv::userland::Error;
using dtv::userland::host_listener_context::IdentityLease;
using dtv::userland::host_listener_context::IdentityLeaseConfig;
using dtv::userland::host_runtime::EndpointAccess;
using dtv::userland::host_runtime::EndpointConfig;
using dtv::userland::host_runtime::EndpointDiagnostic;
using dtv::userland::host_runtime::EndpointDiagnosticReason;

namespace {

#define CHECK(expression) \
    do { \
        if (!(expression)) { \
            std::fprintf(stderr, "core path/lease check failed at line %d: %s\n", \
                         __LINE__, #expression); \
            return false; \
        } \
    } while (false)

constexpr const char* kProduct = "px4-userland";
constexpr const char* kLeasePrefix = ".px4-userland-";

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

bool test_core_identity_lease()
{
    std::string root;
    CHECK(make_root(root));
    RootGuard guard(root);
    const IdentityLeaseConfig config{
        root, kLeasePrefix, EndpointAccess::private_user};
    auto first = IdentityLease::acquire(config, "usb-1-2");
    CHECK(first);
    const std::string lock = root + "/.px4-userland-usb-1-2.lock";
    struct stat status {};
    CHECK(::stat(lock.c_str(), &status) == 0);
    CHECK(S_ISREG(status.st_mode));
    CHECK((status.st_mode & 07777) == 0600);
    CHECK(status.st_uid == ::geteuid());
    CHECK(status.st_nlink == 1);

    auto duplicate = IdentityLease::acquire(config, "usb-1-2");
    CHECK(!duplicate && duplicate.error() == Error::BUSY);
    auto other = IdentityLease::acquire(config, "usb-1-3");
    CHECK(other);
    CHECK(::access(lock.c_str(), F_OK) == 0);

    first.value().close();
    CHECK(::access(lock.c_str(), F_OK) != 0);
    auto reacquired = IdentityLease::acquire(config, "usb-1-2");
    CHECK(reacquired);
    return true;
}

}  // namespace

int main()
{
    if (!test_core_path_layout() || !test_core_path_overlong() ||
        !test_core_identity_lease()) {
        return 1;
    }
    std::printf("PX4 Core path/identity lease contract fixtures passed\n");
    return 0;
}

// SPDX-License-Identifier: GPL-2.0-only
#include "test_temp_directory.h"

extern "C" {
#include <ifdhandler.h>
}

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

class TempRuntime final {
public:
    TempRuntime()
    {
        std::string pattern = px4::userland::test::temporary_directory_template(
            "px4-ifd-presence-");
        char* made = ::mkdtemp(pattern.data());
        if (made != nullptr) {
            path_ = made;
            valid_ = ::chmod(path_.c_str(), 0700) == 0;
        }
    }
    ~TempRuntime() noexcept
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    bool valid() const noexcept { return valid_; }
    const std::string& path() const noexcept { return path_; }
private:
    std::string path_;
    bool valid_ = false;
};

bool test_presence_transport_errors()
{
    TempRuntime runtime;
    if (!runtime.valid()) return false;
    if (::chmod(runtime.path().c_str(), 0755) != 0) return false;
    constexpr const char* serial = "00001205000960";
    std::string device_name = "px4-userland:runtime=" + runtime.path() +
        ":device=" + serial + ":access=group";
    if (IFDHCreateChannelByName(0U, device_name.data()) != IFD_SUCCESS ||
        IFDHICCPresence(0U) != IFD_COMMUNICATION_ERROR ||
        IFDHCloseChannel(0U) != IFD_SUCCESS) return false;

    if (::chmod(runtime.path().c_str(), 0700) != 0) return false;
    device_name = "px4-userland:runtime=" + runtime.path() +
        ":device=" + serial + ":access=user";
    if (IFDHCreateChannelByName(0U, device_name.data()) != IFD_SUCCESS ||
        IFDHICCPresence(0U) != IFD_COMMUNICATION_ERROR ||
        IFDHCloseChannel(0U) != IFD_SUCCESS) return false;

    const std::string product = runtime.path() + "/px4-userland";
    const std::string version = product + "/v1";
    const std::string instance = version + "/" + serial;
    if (::mkdir(product.c_str(), 0700) != 0 || ::chmod(product.c_str(), 0700) != 0 ||
        ::mkdir(version.c_str(), 0700) != 0 || ::chmod(version.c_str(), 0700) != 0 ||
        ::mkdir(instance.c_str(), 0700) != 0 || ::chmod(instance.c_str(), 0700) != 0) return false;
    const std::string socket_path = instance + "/control.sock";
    const int listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (socket_path.size() >= sizeof(address.sun_path)) {
        (void)::close(listener);
        return false;
    }
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1U);
    const bool listening = ::bind(listener, reinterpret_cast<const sockaddr*>(&address),
                                   sizeof(address)) == 0 &&
        ::chmod(socket_path.c_str(), 0600) == 0 && ::listen(listener, 4) == 0;
    const bool timed_out = listening &&
        IFDHCreateChannelByName(0U, device_name.data()) == IFD_SUCCESS &&
        IFDHICCPresence(0U) == IFD_COMMUNICATION_ERROR &&
        IFDHCloseChannel(0U) == IFD_SUCCESS;
    (void)::close(listener);
    (void)::unlink(socket_path.c_str());
    if (timed_out) std::puts("ifdh_presence_namespace_timeout_abi=PASS");
    return timed_out;
}

}  // namespace

int main()
{
    if (IFDHICCPresence(1U) != IFD_NO_SUCH_DEVICE) {
        std::fputs("IFDHICCPresence invalid LUN ABI fixture failed\n", stderr);
        return 1;
    }
    if (!test_presence_transport_errors()) {
        std::fputs("IFDHICCPresence transport error ABI fixture failed\n", stderr);
        return 1;
    }
    return 0;
}

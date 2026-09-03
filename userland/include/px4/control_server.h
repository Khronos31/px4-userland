// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_CONTROL_SERVER_H
#define PX4_USERLAND_CONTROL_SERVER_H

#include "px4/card_service.h"
#include "px4/posix_ipc.h"

#include <memory>
#include <string_view>

namespace px4::userland::ipc::posix {

// POSIX control-plane engine used by px4d and socket integration tests. USB
// ownership remains outside this class; CardService calls are serialized by
// poll_once's single-threaded dispatch.
class PosixControlServer final {
public:
    static Result<std::unique_ptr<PosixControlServer>> create(
        const EndpointConfig& endpoint, CardService& card_service,
        std::string_view base_serial, bool ready = true,
        std::uint8_t usb_present_mask = 0x03U) noexcept;

    ~PosixControlServer() noexcept;
    PosixControlServer(const PosixControlServer&) = delete;
    PosixControlServer& operator=(const PosixControlServer&) = delete;

    // Processes accept/read/dispatch/event work for at most timeout. Repeated
    // calls form the foreground daemon loop and give signal state a bounded
    // observation interval.
    Result<void> poll_once(Timeout timeout) noexcept;
    Result<void> shutdown() noexcept;
    const char* endpoint_path() const noexcept;
    std::size_t connection_count() const noexcept;

private:
    struct Impl;
    explicit PosixControlServer(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace px4::userland::ipc::posix

#endif  // PX4_USERLAND_CONTROL_SERVER_H

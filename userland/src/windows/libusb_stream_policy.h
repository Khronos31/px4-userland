// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_WINDOWS_LIBUSB_STREAM_POLICY_H
#define PX4_USERLAND_WINDOWS_LIBUSB_STREAM_POLICY_H

#include <cstddef>

namespace px4::userland {

// A narrow seam for testing the WinUSB policy without a Windows host or USB.
class WindowsRawIoApi {
public:
    virtual ~WindowsRawIoApi() noexcept = default;
    virtual int supports_raw_io() noexcept = 0;
    virtual int maximum_packet_size() noexcept = 0;
    virtual int maximum_transfer_size() noexcept = 0;
    virtual int enable_raw_io() noexcept = 0;
};

int prepare_windows_stream(WindowsRawIoApi& api, std::size_t* transfer_size,
                           bool* raw_io_enabled) noexcept;

}  // namespace px4::userland

#endif

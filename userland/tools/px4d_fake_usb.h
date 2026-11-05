// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_PX4D_FAKE_USB_H
#define PX4_USERLAND_PX4D_FAKE_USB_H

#include "px4/error.h"
#include "px4/libusb_transport.h"

namespace px4::userland {

Result<std::unique_ptr<Q3U4Runtime>> make_fake_q3u4_runtime() noexcept;

}  // namespace px4::userland

#endif  // PX4_USERLAND_PX4D_FAKE_USB_H

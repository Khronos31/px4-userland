// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_L3_RUNTIME_USB_PORT_H
#define PX4_USERLAND_L3_RUNTIME_USB_PORT_H

#include "l3_adapter.h"
#include "px4/libusb_transport.h"

#include <cstdint>

namespace px4::userland {

// Host-owned USB provider for an already-open Q3U4Runtime.  It reports the
// runtime's claims as borrowed and performs bulk I/O through the runtime's
// transports.  Control transfers and generic cancellation remain separate.
class L3RuntimeUsbPort final : public L3UsbPort {
public:
    explicit L3RuntimeUsbPort(Q3U4Runtime& runtime) noexcept;
    ~L3RuntimeUsbPort() noexcept override = default;

    dtv_l3_claim_result claim(dtv_l3_provider_binding* binding,
                              std::uint32_t claim_index) noexcept override;
    dtv_l3_release_result release_claim(
        dtv_l3_provider_binding* binding,
        std::uint32_t claim_index) noexcept override;
    dtv_l3_result submit(dtv_l3_provider_binding* binding,
                         std::uint64_t transfer_id,
                         const dtv_l3_transfer_request* request,
                         dtv_l3_transfer_complete_fn complete,
                         void* callback_context) noexcept override;
    dtv_l3_result cancel(dtv_l3_provider_binding* binding,
                         std::uint64_t transfer_id) noexcept override;
    dtv_l3_cancel_scope cancel_scope() const noexcept override;

    unsigned int claim_calls = 0U;
    unsigned int release_calls = 0U;
    unsigned int write_calls = 0U;
    unsigned int read_calls = 0U;
    unsigned int completion_calls = 0U;

private:
    Q3U4Runtime* runtime_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_L3_RUNTIME_USB_PORT_H

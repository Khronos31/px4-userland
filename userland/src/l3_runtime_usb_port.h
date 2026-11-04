// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_L3_RUNTIME_USB_PORT_H
#define PX4_USERLAND_L3_RUNTIME_USB_PORT_H

#include "l3_adapter.h"
#include "px4/libusb_transport.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

namespace px4::userland {

// Host-owned USB provider for an already-open Q3U4Runtime.  It reports the
// runtime's claims as borrowed and performs bulk I/O through the runtime's
// transports.  Control transfers and generic cancellation remain separate.
class L3RuntimeUsbPort final : public L3UsbPort {
public:
    explicit L3RuntimeUsbPort(Q3U4Runtime& runtime) noexcept;
    L3RuntimeUsbPort(Transport& dev1, Transport& dev2) noexcept;
    ~L3RuntimeUsbPort() noexcept override;

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

    std::atomic<unsigned int> claim_calls{0U};
    std::atomic<unsigned int> release_calls{0U};
    std::atomic<unsigned int> submit_calls{0U};
    std::atomic<unsigned int> write_calls{0U};
    std::atomic<unsigned int> read_calls{0U};
    std::atomic<unsigned int> stream_read_calls{0U};
    std::atomic<unsigned int> completion_calls{0U};

private:
    struct WorkerState;
    struct Request;

    void run(std::uint32_t claim_index) noexcept;
    static dtv_l3_result from_error(Error error) noexcept;

    std::array<Transport*, 2U> transports_{};
    std::array<std::unique_ptr<WorkerState>, 2U> workers_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_L3_RUNTIME_USB_PORT_H

// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_L3_BACKEND_PRODUCT_H
#define PX4_USERLAND_L3_BACKEND_PRODUCT_H

#include "l3_adapter.h"
#include "px4/identity.h"
#include "px4/tuner_service.h"

#include <cstdint>
#include <memory>

namespace px4::userland {

// Uses the existing tuner backend and stream data plane as the product
// implementation.  USB claim/transfer work stays in the host-owned
// dtv_l3_usb_provider created from L3UsbPort.
class L3BackendProduct final : public L3Product {
public:
    // The candidate tune callback has no timeout argument; this is a product
    // candidate until the Core contract supplies one.
    static constexpr std::uint32_t kTuneTimeoutMs = 5000U;

    L3BackendProduct(TunerServiceBackend& tuner,
                     TunerStreamControl& stream,
                     const DeviceProfile& profile,
                     bool has_card_reader,
                     dtv_l3_cancel_scope cancel_scope,
                     L3OwnerLifecycle* lifecycle = nullptr) noexcept;
    ~L3BackendProduct() noexcept override;

    L3BackendProduct(const L3BackendProduct&) = delete;
    L3BackendProduct& operator=(const L3BackendProduct&) = delete;

    dtv_l3_capabilities capabilities() const noexcept override;
    const dtv_l3_capability_catalog* catalog() const noexcept override;
    dtv_l3_result bind(dtv_l3_context* owner) noexcept override;
    dtv_l3_result initialize(dtv_l3_context* owner) noexcept override;
    dtv_l3_result scope_query(std::uint32_t receiver_id,
                              dtv_l3_scope_info* scope) noexcept override;
    dtv_l3_result tune(std::uint32_t receiver_id, std::uint64_t requested_hz,
                       std::uint64_t* programmed_hz) noexcept override;
    dtv_l3_result tune_ex(std::uint32_t receiver_id,
                          const dtv_l3_tune_request* request,
                          std::uint64_t* programmed_hz) noexcept override;
    dtv_l3_result bridge_start(std::uint32_t bridge_id,
                               std::uint32_t receiver_id) noexcept override;
    dtv_l3_result bridge_stop(std::uint32_t bridge_id) noexcept override;
    dtv_l3_result stream_attach(std::uint64_t stream_id,
                                dtv_l3_ts_deliver_fn deliver,
                                void* callback_context) noexcept override;
    dtv_l3_result stream_attach_ex(std::uint64_t stream_id,
                                   std::uint32_t receiver_id,
                                   std::uint32_t bridge_id,
                                   dtv_l3_ts_deliver_fn deliver,
                                   void* callback_context) noexcept override;
    dtv_l3_result stream_stop(std::uint64_t stream_id) noexcept override;
    dtv_l3_result stream_stop_ex(std::uint64_t stream_id,
                                 std::uint32_t receiver_id,
                                 std::uint32_t bridge_id) noexcept override;

    // The candidate Core close has no product callback.  Callers that own the
    // backend must use this after dtv_l3_context_release() to close receivers
    // and stop any residual capture.
    dtv_l3_result close_all() noexcept;

private:
    dtv_l3_result stop_stream(std::uint64_t stream_id,
                              std::uint32_t receiver_id,
                              std::uint32_t bridge_id,
                              bool check_route) noexcept;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_L3_BACKEND_PRODUCT_H

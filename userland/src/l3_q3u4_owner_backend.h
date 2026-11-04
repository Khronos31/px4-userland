// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_L3_Q3U4_OWNER_BACKEND_H
#define PX4_USERLAND_L3_Q3U4_OWNER_BACKEND_H

#include "l3_adapter.h"
#include "px4/firmware.h"
#include "px4/identity.h"

#include <memory>

namespace px4::userland {

// Production Q3U4 construction using owner-backed controller transports.
// Standard px4d keeps its existing direct-runtime construction; this class is
// the L3 build's controller/transport instance connection.
class L3Q3U4OwnerBackend final : public L3Product {
public:
    L3Q3U4OwnerBackend(const DeviceProfile& profile, bool allow_lnb_15v,
                       bool initialize_firmware,
                       const FirmwareImage* firmware) noexcept;
    ~L3Q3U4OwnerBackend() noexcept override;

    L3Q3U4OwnerBackend(const L3Q3U4OwnerBackend&) = delete;
    L3Q3U4OwnerBackend& operator=(const L3Q3U4OwnerBackend&) = delete;

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
    dtv_l3_result lnb_status(std::uint32_t scope_id,
                             dtv_l3_lnb_state* out) noexcept override;
    dtv_l3_result lnb_set(std::uint32_t scope_id,
                          dtv_l3_lnb_command command,
                          std::uint64_t deadline_ns,
                          dtv_l3_lnb_state* out) noexcept override;
    dtv_l3_result lnb_restore(std::uint32_t scope_id,
                              dtv_l3_restore_result* out) noexcept override;
    void abandon() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_L3_Q3U4_OWNER_BACKEND_H

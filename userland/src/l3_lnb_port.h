// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_L3_LNB_PORT_H
#define PX4_USERLAND_L3_LNB_PORT_H

#include <dtv/l3_owner.h>

#include "px4/tuner_service.h"

#include <cstdint>

namespace px4::userland {

// Product-internal G8 LNB material.  It maps the existing
// TunerServiceBackend independent-LNB seam to the candidate G8 state shape.
// It is not a public L3 API and is not connected to the Core owner until the
// fixed Core close/shutdown SHA is published.
class L3LnbPort final {
public:
    L3LnbPort(TunerServiceBackend& backend, bool supported,
              bool allow_15v) noexcept;
    ~L3LnbPort() noexcept = default;

    L3LnbPort(const L3LnbPort&) = delete;
    L3LnbPort& operator=(const L3LnbPort&) = delete;

    bool supported() const noexcept { return supported_; }

    dtv_l3_result status(std::uint32_t scope_id,
                         dtv_l3_lnb_state* out) noexcept;
    dtv_l3_result set(std::uint32_t scope_id, dtv_l3_lnb_command command,
                      std::uint64_t deadline_ns,
                      dtv_l3_lnb_state* out) noexcept;
    dtv_l3_result restore(std::uint32_t scope_id,
                          dtv_l3_restore_result* out) noexcept;

private:
    enum class KnownState : std::uint8_t { unknown, off, on };

    KnownState query_known() const noexcept;
    void fill_status(dtv_l3_lnb_state* out,
                     dtv_l3_restore_result restoration) const noexcept;

    TunerServiceBackend& backend_;
    bool supported_;
    bool allow_15v_;
    std::uint64_t scope_generation_ = 1U;
    KnownState known_ = KnownState::unknown;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_L3_LNB_PORT_H

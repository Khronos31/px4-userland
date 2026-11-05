// SPDX-License-Identifier: GPL-2.0-only
#include "l3_lnb_port.h"

#include <chrono>
#include <cstddef>

namespace px4::userland {

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t monotonic_now_ns() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now().time_since_epoch())
            .count());
}

bool deadline_expired(std::uint64_t deadline_ns) noexcept
{
    return deadline_ns != 0U && monotonic_now_ns() > deadline_ns;
}

dtv_l3_result from_error(Error error) noexcept
{
    switch (error) {
    case Error::OK: return DTV_L3_OK;
    case Error::INVALID_ARGUMENT:
    case Error::BUFFER_TOO_SMALL: return DTV_L3_INVALID;
    case Error::NOT_FOUND: return DTV_L3_UNSUPPORTED;
    case Error::BUSY: return DTV_L3_BUSY;
    case Error::NOT_READY: return DTV_L3_FAILED;
    case Error::TIMEOUT: return DTV_L3_TIMEOUT;
    case Error::DISCONNECTED: return DTV_L3_DISCONNECTED;
    case Error::UNSUPPORTED: return DTV_L3_UNSUPPORTED;
    default: return DTV_L3_FAILED;
    }
}

}  // namespace

L3LnbPort::L3LnbPort(TunerServiceBackend& backend, bool supported,
                     bool allow_15v) noexcept
    : backend_(backend), supported_(supported), allow_15v_(allow_15v)
{
    known_ = supported_ ? query_known() : KnownState::unknown;
}

L3LnbPort::KnownState L3LnbPort::query_known() const noexcept
{
    const auto state = backend_.independent_lnb_power_state();
    if (!state.present) {
        return KnownState::unknown;
    }
    return state.value ? KnownState::on : KnownState::off;
}

void L3LnbPort::fill_status(
    dtv_l3_lnb_state* out, dtv_l3_restore_result restoration) const noexcept
{
    *out = dtv_l3_lnb_state{};
    if (known_ == KnownState::on) {
        out->controlled = DTV_L3_LNB_COMMAND_ON_15V;
        out->consistency = DTV_L3_LNB_CONSISTENCY_CONSISTENT;
    } else if (known_ == KnownState::off) {
        out->controlled = DTV_L3_LNB_COMMAND_OFF;
        out->consistency = DTV_L3_LNB_CONSISTENCY_CONSISTENT;
    } else {
        out->controlled = 0U;
        out->consistency = DTV_L3_LNB_CONSISTENCY_UNKNOWN;
    }
    out->readback = DTV_L3_SUPPORT_UNKNOWN;
    out->scope_generation = scope_generation_;
    out->restoration = restoration;
}

dtv_l3_result L3LnbPort::status(std::uint32_t scope_id,
                                dtv_l3_lnb_state* out) noexcept
{
    if (out == nullptr) {
        return DTV_L3_INVALID;
    }
    if (!supported_ || scope_id != 0U) {
        return DTV_L3_UNSUPPORTED;
    }
    known_ = query_known();
    fill_status(out, DTV_L3_RESTORE_NOT_ATTEMPTED);
    return DTV_L3_OK;
}

dtv_l3_result L3LnbPort::set(std::uint32_t scope_id,
                             dtv_l3_lnb_command command,
                             std::uint64_t deadline_ns,
                             dtv_l3_lnb_state* out) noexcept
{
    if (out == nullptr || (command != DTV_L3_LNB_OFF &&
                           command != DTV_L3_LNB_ON_15V)) {
        return DTV_L3_INVALID;
    }
    if (!supported_ || scope_id != 0U) {
        return DTV_L3_UNSUPPORTED;
    }
    if (command == DTV_L3_LNB_ON_15V && !allow_15v_) {
        return DTV_L3_UNSUPPORTED;
    }
    if (deadline_expired(deadline_ns)) {
        return DTV_L3_TIMEOUT;
    }
    const auto result =
        backend_.set_independent_lnb_power(command == DTV_L3_LNB_ON_15V);
    if (!result) {
        known_ = query_known();
        return from_error(result.error());
    }
    known_ = command == DTV_L3_LNB_ON_15V ? KnownState::on : KnownState::off;
    ++scope_generation_;
    fill_status(out, DTV_L3_RESTORE_NOT_ATTEMPTED);
    if (deadline_expired(deadline_ns)) {
        // The backend seam has no cancellation; the state was applied but the
        // operation missed its deadline. Keep the observed state and report it.
        return DTV_L3_TIMEOUT;
    }
    return DTV_L3_OK;
}

dtv_l3_result L3LnbPort::restore(std::uint32_t scope_id,
                                 dtv_l3_restore_result* out) noexcept
{
    if (out == nullptr) {
        return DTV_L3_INVALID;
    }
    *out = DTV_L3_RESTORE_NOT_ATTEMPTED;
    if (!supported_ || scope_id != 0U) {
        return DTV_L3_UNSUPPORTED;
    }
    const auto result = backend_.shutdown();
    if (result) {
        known_ = KnownState::off;
        ++scope_generation_;
        *out = DTV_L3_RESTORE_SUCCEEDED;
        return DTV_L3_OK;
    }
    known_ = query_known();
    *out = result.error() == Error::DISCONNECTED
               ? DTV_L3_RESTORE_UNKNOWN
               : DTV_L3_RESTORE_FAILED;
    return from_error(result.error());
}

}  // namespace px4::userland

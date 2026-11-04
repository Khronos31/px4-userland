// SPDX-License-Identifier: GPL-2.0-only
#include "l3_q3u4_owner_backend.h"

#include "l3_backend_product.h"
#include "l3_owner_transport.h"
#include "px4/it930x.h"
#include "q3u4_frontend.h"
#include "q3u4_lnb_power.h"
#include "px4/q3u4_stream.h"
#include "q3u4_tuner_backend.h"

#include <chrono>
#include <new>
#include <thread>

namespace px4::userland {

namespace {

dtv_l3_result from_error(Error error) noexcept
{
    switch (error) {
    case Error::OK: return DTV_L3_OK;
    case Error::INVALID_ARGUMENT:
    case Error::BUFFER_TOO_SMALL: return DTV_L3_INVALID;
    case Error::NOT_FOUND: return DTV_L3_STALE;
    case Error::BUSY: return DTV_L3_BUSY;
    case Error::NOT_READY: return DTV_L3_FAILED;
    case Error::TIMEOUT: return DTV_L3_TIMEOUT;
    case Error::DISCONNECTED: return DTV_L3_DISCONNECTED;
    case Error::UNSUPPORTED: return DTV_L3_UNSUPPORTED;
    default: return DTV_L3_FAILED;
    }
}

class SteadyClockDelay final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

}  // namespace

struct L3Q3U4OwnerBackend::Impl final {
    Impl(const DeviceProfile& profile_value, bool allow_lnb_15v_value,
         bool initialize_firmware_value,
         const FirmwareImage* firmware_value) noexcept
        : profile(profile_value),
          allow_lnb_15v(allow_lnb_15v_value),
          initialize_firmware(initialize_firmware_value),
          firmware(firmware_value)
    {
    }

    const DeviceProfile& profile;
    bool allow_lnb_15v;
    bool initialize_firmware;
    const FirmwareImage* firmware;
    dtv_l3_context* owner = nullptr;
    SteadyClockDelay delay;
    std::unique_ptr<L3OwnerTransport> dev1_transport;
    std::unique_ptr<L3OwnerTransport> dev2_transport;
    std::unique_ptr<It930xController> dev1;
    std::unique_ptr<It930xController> dev2;
    std::unique_ptr<It930xBridgeI2cMaster> dev1_i2c;
    std::unique_ptr<It930xBridgeI2cMaster> dev2_i2c;
    std::unique_ptr<It930xBackendPower> dev1_power;
    std::unique_ptr<It930xBackendPower> dev2_power;
    std::unique_ptr<It930xPsbPurger> dev1_purger;
    std::unique_ptr<It930xPsbPurger> dev2_purger;
    std::unique_ptr<Q3U4FrontendEnclosure> enclosure;
    std::unique_ptr<It930xLnbPower> lnb1;
    std::unique_ptr<It930xLnbPower> lnb2;
    std::unique_ptr<Q3U4LnbPowerCoordinator> lnb;
    std::unique_ptr<Q3U4FrontendTunerBackend> tuner;
    std::unique_ptr<Q3U4StreamDataPlane> stream;
    std::unique_ptr<L3BackendProduct> backend;
};

L3Q3U4OwnerBackend::L3Q3U4OwnerBackend(
    const DeviceProfile& profile, bool allow_lnb_15v,
    bool initialize_firmware, const FirmwareImage* firmware) noexcept
    : impl_(new (std::nothrow) Impl(profile, allow_lnb_15v,
                                    initialize_firmware, firmware))
{
}

L3Q3U4OwnerBackend::~L3Q3U4OwnerBackend() noexcept = default;

dtv_l3_capabilities L3Q3U4OwnerBackend::capabilities() const noexcept
{
    dtv_l3_capabilities out{};
    if (!impl_) {
        return out;
    }
    out.receiver_count = impl_->profile.receiver_count;
    out.reader_count = 1U;
    out.lnb_power = impl_->profile.supports_lnb_15v ? 1U : 0U;
    out.cancel_scope = DTV_L3_CANCEL_SCOPE_PIPE;
    return out;
}

const dtv_l3_capability_catalog* L3Q3U4OwnerBackend::catalog() const noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? nullptr
               : impl_->backend->catalog();
}

dtv_l3_result L3Q3U4OwnerBackend::bind(dtv_l3_context* owner) noexcept
{
    if (!impl_) {
        return DTV_L3_FAILED;
    }
    impl_->owner = owner;
    return owner == nullptr ? DTV_L3_INVALID : DTV_L3_OK;
}

dtv_l3_result L3Q3U4OwnerBackend::initialize(dtv_l3_context* owner) noexcept
{
    if (!impl_ || owner == nullptr ||
        impl_->profile.model != DeviceModel::px_q3u4) {
        return DTV_L3_FAILED;
    }
    impl_->owner = owner;
    impl_->dev1_transport = std::make_unique<L3OwnerTransport>(owner, 0U);
    impl_->dev2_transport = std::make_unique<L3OwnerTransport>(owner, 1U);
    impl_->dev1 = std::make_unique<It930xController>(
        *impl_->dev1_transport,
        CommandPacingOptions{CommandPacingMode::no_delay});
    impl_->dev2 = std::make_unique<It930xController>(
        *impl_->dev2_transport,
        CommandPacingOptions{CommandPacingMode::no_delay});
    if (impl_->initialize_firmware) {
        if (impl_->firmware == nullptr) {
            return DTV_L3_FAILED;
        }
        const auto first = impl_->dev1->initialize_q3u4(
            *impl_->firmware, InitializationPolicy::accept_cold_or_warm,
            impl_->profile.model);
        if (!first) {
            return from_error(first.error());
        }
        const auto second = impl_->dev2->initialize_q3u4(
            *impl_->firmware, InitializationPolicy::accept_cold_or_warm,
            impl_->profile.model);
        if (!second) {
            return from_error(second.error());
        }
    }
    impl_->dev1_i2c = std::make_unique<It930xBridgeI2cMaster>(*impl_->dev1);
    impl_->dev2_i2c = std::make_unique<It930xBridgeI2cMaster>(*impl_->dev2);
    impl_->dev1_power = std::make_unique<It930xBackendPower>(*impl_->dev1);
    impl_->dev2_power = std::make_unique<It930xBackendPower>(*impl_->dev2);
    impl_->dev1_purger = std::make_unique<It930xPsbPurger>(*impl_->dev1);
    impl_->dev2_purger = std::make_unique<It930xPsbPurger>(*impl_->dev2);
    impl_->enclosure = std::make_unique<Q3U4FrontendEnclosure>(
        *impl_->dev1_i2c, *impl_->dev2_i2c, *impl_->dev1_power,
        *impl_->dev2_power, impl_->delay, impl_->dev1_purger.get(),
        impl_->dev2_purger.get());
    impl_->lnb1 = std::make_unique<It930xLnbPower>(*impl_->dev1);
    impl_->lnb2 = std::make_unique<It930xLnbPower>(*impl_->dev2);
    impl_->lnb = std::make_unique<Q3U4LnbPowerCoordinator>(
        *impl_->lnb1, *impl_->lnb2, impl_->allow_lnb_15v);
    impl_->tuner = std::make_unique<Q3U4FrontendTunerBackend>(
        *impl_->enclosure, *impl_->lnb);
    auto stream = Q3U4StreamDataPlane::create(
        *impl_->dev1_transport, *impl_->dev2_transport);
    if (!stream) {
        return from_error(stream.error());
    }
    impl_->stream = std::move(stream.value());
    impl_->backend = std::make_unique<L3BackendProduct>(
        *impl_->tuner, *impl_->stream, impl_->profile, true,
        DTV_L3_CANCEL_SCOPE_PIPE, impl_->allow_lnb_15v);
    return DTV_L3_OK;
}

dtv_l3_result L3Q3U4OwnerBackend::scope_query(
    std::uint32_t receiver_id, dtv_l3_scope_info* scope) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->scope_query(receiver_id, scope);
}

dtv_l3_result L3Q3U4OwnerBackend::tune(
    std::uint32_t receiver_id, std::uint64_t requested_hz,
    std::uint64_t* programmed_hz) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->tune(receiver_id, requested_hz, programmed_hz);
}

dtv_l3_result L3Q3U4OwnerBackend::tune_ex(
    std::uint32_t receiver_id, const dtv_l3_tune_request* request,
    std::uint64_t* programmed_hz) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->tune_ex(receiver_id, request, programmed_hz);
}

dtv_l3_result L3Q3U4OwnerBackend::bridge_start(
    std::uint32_t bridge_id, std::uint32_t receiver_id) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->bridge_start(bridge_id, receiver_id);
}

dtv_l3_result L3Q3U4OwnerBackend::bridge_stop(
    std::uint32_t bridge_id) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->bridge_stop(bridge_id);
}

dtv_l3_result L3Q3U4OwnerBackend::stream_attach(
    std::uint64_t stream_id, dtv_l3_ts_deliver_fn deliver,
    void* callback_context) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->stream_attach(stream_id, deliver,
                                               callback_context);
}

dtv_l3_result L3Q3U4OwnerBackend::stream_attach_ex(
    std::uint64_t stream_id, std::uint32_t receiver_id,
    std::uint32_t bridge_id, dtv_l3_ts_deliver_fn deliver,
    void* callback_context) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->stream_attach_ex(
                     stream_id, receiver_id, bridge_id, deliver,
                     callback_context);
}

dtv_l3_result L3Q3U4OwnerBackend::stream_stop(
    std::uint64_t stream_id) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->stream_stop(stream_id);
}

dtv_l3_result L3Q3U4OwnerBackend::stream_stop_ex(
    std::uint64_t stream_id, std::uint32_t receiver_id,
    std::uint32_t bridge_id) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->stream_stop_ex(stream_id, receiver_id,
                                                bridge_id);
}

dtv_l3_result L3Q3U4OwnerBackend::lnb_status(
    std::uint32_t scope_id, dtv_l3_lnb_state* out) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->lnb_status(scope_id, out);
}

dtv_l3_result L3Q3U4OwnerBackend::lnb_set(
    std::uint32_t scope_id, dtv_l3_lnb_command command,
    std::uint64_t deadline_ns, dtv_l3_lnb_state* out) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->lnb_set(scope_id, command, deadline_ns, out);
}

dtv_l3_result L3Q3U4OwnerBackend::lnb_restore(
    std::uint32_t scope_id, dtv_l3_restore_result* out) noexcept
{
    return impl_ == nullptr || impl_->backend == nullptr
               ? DTV_L3_FAILED
               : impl_->backend->lnb_restore(scope_id, out);
}

void L3Q3U4OwnerBackend::abandon() noexcept
{
    if (impl_ != nullptr && impl_->backend != nullptr) {
        impl_->backend->abandon();
    }
}

}  // namespace px4::userland

// SPDX-License-Identifier: GPL-2.0-only
#include "l3_adapter.h"

namespace px4::userland {

namespace {

L3Product* product_from(void* context) noexcept
{
    return static_cast<L3Product*>(context);
}

}  // namespace

L3ProviderAdapter::L3ProviderAdapter(L3Product& product) noexcept
    : product_(product)
{
    provider_.provider_context = &product_;
    provider_.claim = &L3ProviderAdapter::claim_thunk;
    provider_.release_claim = &L3ProviderAdapter::release_claim_thunk;
    provider_.submit = &L3ProviderAdapter::submit_thunk;
    provider_.cancel = &L3ProviderAdapter::cancel_thunk;
    provider_.scope_query = &L3ProviderAdapter::scope_query_thunk;
    provider_.bridge_start = &L3ProviderAdapter::bridge_start_thunk;
    provider_.bridge_stop = &L3ProviderAdapter::bridge_stop_thunk;
    provider_.stream_attach = &L3ProviderAdapter::stream_attach_thunk;
    provider_.stream_stop = &L3ProviderAdapter::stream_stop_thunk;
    provider_.tune = &L3ProviderAdapter::tune_thunk;
    provider_.claim_cancel = &L3ProviderAdapter::claim_cancel_thunk;
    provider_.cancel_scope = product_.cancel_scope();
    provider_.logger = dtv_l3_logger{};
}

dtv_l3_claim_result L3ProviderAdapter::claim_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t claim_index) noexcept
{
    L3Product* product = product_from(context);
    if (product == nullptr) {
        return {DTV_L3_INVALID, DTV_L3_CLAIM_NONE};
    }
    return product->claim(binding, claim_index);
}

dtv_l3_release_result L3ProviderAdapter::release_claim_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t claim_index) noexcept
{
    L3Product* product = product_from(context);
    if (product == nullptr) {
        return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN, DTV_L3_CLAIM_UNKNOWN,
                DTV_L3_RESTORE_NOT_ATTEMPTED, DTV_L3_INVALID};
    }
    return product->release_claim(binding, claim_index);
}

dtv_l3_result L3ProviderAdapter::submit_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t transfer_id, const dtv_l3_transfer_request* request,
    dtv_l3_transfer_complete_fn complete,
    void* callback_context) noexcept
{
    L3Product* product = product_from(context);
    if (product == nullptr || request == nullptr || complete == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->submit(binding, transfer_id, request, complete,
                           callback_context);
}

dtv_l3_result L3ProviderAdapter::cancel_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t transfer_id) noexcept
{
    L3Product* product = product_from(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->cancel(binding, transfer_id);
}

dtv_l3_result L3ProviderAdapter::claim_cancel_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t claim_index) noexcept
{
    L3Product* product = product_from(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->claim_cancel(binding, claim_index);
}

dtv_l3_result L3ProviderAdapter::scope_query_thunk(
    void* context, std::uint32_t receiver_id,
    dtv_l3_scope_info* scope) noexcept
{
    L3Product* product = product_from(context);
    if (product == nullptr || scope == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->scope_query(receiver_id, scope);
}

dtv_l3_result L3ProviderAdapter::bridge_start_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t bridge_id, std::uint32_t receiver_id) noexcept
{
    (void)binding;
    L3Product* product = product_from(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->bridge_start(bridge_id, receiver_id);
}

dtv_l3_result L3ProviderAdapter::bridge_stop_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t bridge_id) noexcept
{
    (void)binding;
    L3Product* product = product_from(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->bridge_stop(bridge_id);
}

dtv_l3_result L3ProviderAdapter::stream_attach_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t stream_id, dtv_l3_ts_deliver_fn deliver,
    void* callback_context) noexcept
{
    (void)binding;
    L3Product* product = product_from(context);
    if (product == nullptr || deliver == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->stream_attach(stream_id, deliver, callback_context);
}

dtv_l3_result L3ProviderAdapter::stream_stop_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t stream_id) noexcept
{
    (void)binding;
    L3Product* product = product_from(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->stream_stop(stream_id);
}

dtv_l3_result L3ProviderAdapter::tune_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t receiver_id, std::uint64_t requested_hz,
    std::uint64_t* programmed_hz) noexcept
{
    (void)binding;
    L3Product* product = product_from(context);
    if (product == nullptr || programmed_hz == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->tune(receiver_id, requested_hz, programmed_hz);
}

}  // namespace px4::userland

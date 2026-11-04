// SPDX-License-Identifier: GPL-2.0-only
#include "l3_adapter.h"

namespace px4::userland {

namespace {

template <typename T>
T* context_from(void* context) noexcept
{
    return static_cast<T*>(context);
}

}  // namespace

L3UsbProviderAdapter::L3UsbProviderAdapter(L3UsbPort& port,
                                           std::uint32_t call_flags) noexcept
    : port_(port)
{
    provider_.provider_context = &port_;
    provider_.claim = &L3UsbProviderAdapter::claim_thunk;
    provider_.release_claim = &L3UsbProviderAdapter::release_claim_thunk;
    provider_.submit = &L3UsbProviderAdapter::submit_thunk;
    provider_.cancel = &L3UsbProviderAdapter::cancel_thunk;
    provider_.claim_cancel = &L3UsbProviderAdapter::claim_cancel_thunk;
    provider_.cancel_scope = port_.cancel_scope();
    provider_.call_flags = call_flags;
    provider_.logger = dtv_l3_logger{};
}

dtv_l3_claim_result L3UsbProviderAdapter::claim_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t claim_index) noexcept
{
    L3UsbPort* port = context_from<L3UsbPort>(context);
    if (port == nullptr) {
        return {DTV_L3_INVALID, DTV_L3_CLAIM_NONE};
    }
    return port->claim(binding, claim_index);
}

dtv_l3_release_result L3UsbProviderAdapter::release_claim_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t claim_index) noexcept
{
    L3UsbPort* port = context_from<L3UsbPort>(context);
    if (port == nullptr) {
        return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN, DTV_L3_CLAIM_UNKNOWN,
                DTV_L3_RESTORE_NOT_ATTEMPTED, DTV_L3_INVALID};
    }
    return port->release_claim(binding, claim_index);
}

dtv_l3_result L3UsbProviderAdapter::submit_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t transfer_id, const dtv_l3_transfer_request* request,
    dtv_l3_transfer_complete_fn complete,
    void* callback_context) noexcept
{
    L3UsbPort* port = context_from<L3UsbPort>(context);
    if (port == nullptr || request == nullptr || complete == nullptr) {
        return DTV_L3_INVALID;
    }
    return port->submit(binding, transfer_id, request, complete,
                        callback_context);
}

dtv_l3_result L3UsbProviderAdapter::cancel_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t transfer_id) noexcept
{
    L3UsbPort* port = context_from<L3UsbPort>(context);
    return port == nullptr ? DTV_L3_INVALID
                           : port->cancel(binding, transfer_id);
}

dtv_l3_result L3UsbProviderAdapter::claim_cancel_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t claim_index) noexcept
{
    L3UsbPort* port = context_from<L3UsbPort>(context);
    return port == nullptr ? DTV_L3_INVALID
                           : port->claim_cancel(binding, claim_index);
}

L3ProductAdapter::L3ProductAdapter(
    L3Product& product, const dtv_l3_firmware* firmware) noexcept
    : product_(product)
{
    adapter_.product_context = &product_;
    adapter_.capabilities = product_.capabilities();
    adapter_.catalog = product_.catalog();
    adapter_.firmware = firmware;
    adapter_.bind = &L3ProductAdapter::bind_thunk;
    adapter_.initialize = &L3ProductAdapter::initialize_thunk;
    adapter_.scope_query = &L3ProductAdapter::scope_query_thunk;
    adapter_.tune = &L3ProductAdapter::tune_thunk;
    adapter_.tune_ex = &L3ProductAdapter::tune_ex_thunk;
    adapter_.bridge_start = &L3ProductAdapter::bridge_start_thunk;
    adapter_.bridge_stop = &L3ProductAdapter::bridge_stop_thunk;
    adapter_.stream_attach = &L3ProductAdapter::stream_attach_thunk;
    adapter_.stream_attach_ex = &L3ProductAdapter::stream_attach_ex_thunk;
    adapter_.stream_stop = &L3ProductAdapter::stream_stop_thunk;
    adapter_.stream_stop_ex = &L3ProductAdapter::stream_stop_ex_thunk;
    adapter_.lnb_status = &L3ProductAdapter::lnb_status_thunk;
    adapter_.lnb_set = &L3ProductAdapter::lnb_set_thunk;
    adapter_.lnb_restore = &L3ProductAdapter::lnb_restore_thunk;
}

dtv_l3_result L3ProductAdapter::bind_thunk(void* context,
                                           dtv_l3_context* owner) noexcept
{
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID : product->bind(owner);
}

dtv_l3_result L3ProductAdapter::initialize_thunk(
    void* context, dtv_l3_context* owner) noexcept
{
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID : product->initialize(owner);
}

dtv_l3_result L3ProductAdapter::scope_query_thunk(
    void* context, std::uint32_t receiver_id,
    dtv_l3_scope_info* scope) noexcept
{
    L3Product* product = context_from<L3Product>(context);
    if (product == nullptr || scope == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->scope_query(receiver_id, scope);
}

dtv_l3_result L3ProductAdapter::tune_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t receiver_id, std::uint64_t requested_hz,
    std::uint64_t* programmed_hz) noexcept
{
    (void)binding;
    L3Product* product = context_from<L3Product>(context);
    if (product == nullptr || programmed_hz == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->tune(receiver_id, requested_hz, programmed_hz);
}

dtv_l3_result L3ProductAdapter::tune_ex_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t receiver_id, const dtv_l3_tune_request* request,
    std::uint64_t* programmed_hz) noexcept
{
    (void)binding;
    L3Product* product = context_from<L3Product>(context);
    if (product == nullptr || request == nullptr || programmed_hz == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->tune_ex(receiver_id, request, programmed_hz);
}

dtv_l3_result L3ProductAdapter::bridge_start_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t bridge_id, std::uint32_t receiver_id) noexcept
{
    (void)binding;
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->bridge_start(bridge_id, receiver_id);
}

dtv_l3_result L3ProductAdapter::bridge_stop_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint32_t bridge_id) noexcept
{
    (void)binding;
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->bridge_stop(bridge_id);
}

dtv_l3_result L3ProductAdapter::stream_attach_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t stream_id, dtv_l3_ts_deliver_fn deliver,
    void* callback_context) noexcept
{
    (void)binding;
    L3Product* product = context_from<L3Product>(context);
    if (product == nullptr || deliver == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->stream_attach(stream_id, deliver, callback_context);
}

dtv_l3_result L3ProductAdapter::stream_attach_ex_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t stream_id, std::uint32_t receiver_id,
    std::uint32_t bridge_id, dtv_l3_ts_deliver_fn deliver,
    void* callback_context) noexcept
{
    (void)binding;
    L3Product* product = context_from<L3Product>(context);
    if (product == nullptr || deliver == nullptr) {
        return DTV_L3_INVALID;
    }
    return product->stream_attach_ex(stream_id, receiver_id, bridge_id,
                                     deliver, callback_context);
}

dtv_l3_result L3ProductAdapter::stream_stop_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t stream_id) noexcept
{
    (void)binding;
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->stream_stop(stream_id);
}

dtv_l3_result L3ProductAdapter::stream_stop_ex_thunk(
    void* context, dtv_l3_provider_binding* binding,
    std::uint64_t stream_id, std::uint32_t receiver_id,
    std::uint32_t bridge_id) noexcept
{
    (void)binding;
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->stream_stop_ex(stream_id, receiver_id,
                                                        bridge_id);
}

dtv_l3_result L3ProductAdapter::lnb_status_thunk(
    void* context, std::uint32_t scope_id,
    dtv_l3_lnb_state* out) noexcept
{
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->lnb_status(scope_id, out);
}

dtv_l3_result L3ProductAdapter::lnb_set_thunk(
    void* context, std::uint32_t scope_id,
    dtv_l3_lnb_command command, std::uint64_t deadline_ns,
    dtv_l3_lnb_state* out) noexcept
{
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->lnb_set(scope_id, command,
                                                 deadline_ns, out);
}

dtv_l3_result L3ProductAdapter::lnb_restore_thunk(
    void* context, std::uint32_t scope_id,
    dtv_l3_restore_result* out) noexcept
{
    L3Product* product = context_from<L3Product>(context);
    return product == nullptr ? DTV_L3_INVALID
                              : product->lnb_restore(scope_id, out);
}

}  // namespace px4::userland

// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_L3_ADAPTER_H
#define PX4_USERLAND_L3_ADAPTER_H

#include <dtv/l3_owner.h>

#include <cstdint>

namespace px4::userland {

// Fixed product facts the candidate Core API has no slot for yet.  Consumers
// in this repository may inspect them; they are not part of the Core ABI.
struct L3Capabilities final {
    std::uint8_t bridge_count;
    std::uint8_t receiver_count;
    bool dual_system;
    bool supports_lnb_15v;
    bool has_card_reader;
};

// Internal seam between the Core provider callbacks and the PX4 product.  The
// methods mirror dtv_l3_provider so this header does not define a second L3
// API; the adapter below is only a thin C ABI bridge to the candidate Core.
class L3Product {
public:
    virtual ~L3Product() noexcept = default;

    virtual L3Capabilities capabilities() const noexcept = 0;
    virtual dtv_l3_cancel_scope cancel_scope() const noexcept
    {
        return DTV_L3_CANCEL_SCOPE_TRANSFER;
    }
    virtual dtv_l3_claim_result claim(
        dtv_l3_provider_binding* binding, std::uint32_t claim_index) noexcept = 0;
    virtual dtv_l3_release_result release_claim(
        dtv_l3_provider_binding* binding, std::uint32_t claim_index) noexcept = 0;
    virtual dtv_l3_result submit(
        dtv_l3_provider_binding* binding, std::uint64_t transfer_id,
        const dtv_l3_transfer_request* request,
        dtv_l3_transfer_complete_fn complete,
        void* callback_context) noexcept = 0;
    virtual dtv_l3_result cancel(dtv_l3_provider_binding* binding,
                                 std::uint64_t transfer_id) noexcept = 0;
    virtual dtv_l3_result claim_cancel(dtv_l3_provider_binding* binding,
                                       std::uint32_t claim_index) noexcept
    {
        (void)binding;
        (void)claim_index;
        return DTV_L3_UNSUPPORTED;
    }
    virtual dtv_l3_result scope_query(std::uint32_t receiver_id,
                                      dtv_l3_scope_info* scope) noexcept = 0;
    virtual dtv_l3_result bridge_start(std::uint32_t bridge_id,
                                       std::uint32_t receiver_id) noexcept = 0;
    virtual dtv_l3_result bridge_stop(std::uint32_t bridge_id) noexcept = 0;
    virtual dtv_l3_result stream_attach(std::uint64_t stream_id,
                                        dtv_l3_ts_deliver_fn deliver,
                                        void* callback_context) noexcept = 0;
    virtual dtv_l3_result stream_stop(std::uint64_t stream_id) noexcept = 0;
    virtual dtv_l3_result tune(std::uint32_t receiver_id,
                               std::uint64_t requested_hz,
                               std::uint64_t* programmed_hz) noexcept = 0;
};

class L3ProviderAdapter final {
public:
    explicit L3ProviderAdapter(L3Product& product) noexcept;
    ~L3ProviderAdapter() noexcept = default;

    L3ProviderAdapter(const L3ProviderAdapter&) = delete;
    L3ProviderAdapter& operator=(const L3ProviderAdapter&) = delete;

    dtv_l3_provider provider() const noexcept { return provider_; }
    L3Capabilities capabilities() const noexcept
    {
        return product_.capabilities();
    }

private:
    static dtv_l3_claim_result claim_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint32_t claim_index) noexcept;
    static dtv_l3_release_result release_claim_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint32_t claim_index) noexcept;
    static dtv_l3_result submit_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint64_t transfer_id, const dtv_l3_transfer_request* request,
        dtv_l3_transfer_complete_fn complete,
        void* callback_context) noexcept;
    static dtv_l3_result cancel_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint64_t transfer_id) noexcept;
    static dtv_l3_result claim_cancel_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint32_t claim_index) noexcept;
    static dtv_l3_result scope_query_thunk(
        void* context, std::uint32_t receiver_id,
        dtv_l3_scope_info* scope) noexcept;
    static dtv_l3_result bridge_start_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint32_t bridge_id, std::uint32_t receiver_id) noexcept;
    static dtv_l3_result bridge_stop_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint32_t bridge_id) noexcept;
    static dtv_l3_result stream_attach_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint64_t stream_id, dtv_l3_ts_deliver_fn deliver,
        void* callback_context) noexcept;
    static dtv_l3_result stream_stop_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint64_t stream_id) noexcept;
    static dtv_l3_result tune_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint32_t receiver_id, std::uint64_t requested_hz,
        std::uint64_t* programmed_hz) noexcept;

    L3Product& product_;
    dtv_l3_provider provider_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_L3_ADAPTER_H

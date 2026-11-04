// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_L3_ADAPTER_H
#define PX4_USERLAND_L3_ADAPTER_H

#include <dtv/l3_owner.h>

#include <cstdint>

namespace px4::userland {

// Caller-owned USB claim/transfer seam.  The L3 boundary keeps permission,
// claim identity, and cancellation with the host, so the product adapter does
// not open a second USB session behind an existing runtime.
class L3UsbPort {
public:
    virtual ~L3UsbPort() noexcept = default;
    virtual dtv_l3_claim_result claim(dtv_l3_provider_binding* binding,
                                      std::uint32_t claim_index) noexcept = 0;
    virtual dtv_l3_release_result release_claim(
        dtv_l3_provider_binding* binding,
        std::uint32_t claim_index) noexcept = 0;
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
    virtual dtv_l3_cancel_scope cancel_scope() const noexcept
    {
        return DTV_L3_CANCEL_SCOPE_TRANSFER;
    }
};

// USB-only C provider supplied to dtv_l3_open_ex()/dtv_l3_open_factory().
class L3UsbProviderAdapter final {
public:
    explicit L3UsbProviderAdapter(L3UsbPort& port) noexcept;
    ~L3UsbProviderAdapter() noexcept = default;

    L3UsbProviderAdapter(const L3UsbProviderAdapter&) = delete;
    L3UsbProviderAdapter& operator=(const L3UsbProviderAdapter&) = delete;

    dtv_l3_usb_provider provider() const noexcept { return provider_; }

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

    L3UsbPort& port_;
    dtv_l3_usb_provider provider_;
};

// Internal product seam.  The methods mirror dtv_l3_product_adapter so this
// header does not define a second public L3 API; the adapter below only binds
// the candidate Core C ABI to the PX4 product implementation.
class L3Product {
public:
    virtual ~L3Product() noexcept = default;

    virtual dtv_l3_capabilities capabilities() const noexcept = 0;
    virtual dtv_l3_result scope_query(std::uint32_t receiver_id,
                                      dtv_l3_scope_info* scope) noexcept = 0;
    virtual dtv_l3_result tune(std::uint32_t receiver_id,
                               std::uint64_t requested_hz,
                               std::uint64_t* programmed_hz) noexcept = 0;
    virtual dtv_l3_result bridge_start(std::uint32_t bridge_id,
                                       std::uint32_t receiver_id) noexcept = 0;
    virtual dtv_l3_result bridge_stop(std::uint32_t bridge_id) noexcept = 0;
    virtual dtv_l3_result stream_attach(std::uint64_t stream_id,
                                        dtv_l3_ts_deliver_fn deliver,
                                        void* callback_context) noexcept = 0;
    virtual dtv_l3_result stream_stop(std::uint64_t stream_id) noexcept = 0;
    virtual dtv_l3_result reader_acquire(
        std::uint32_t reader_id, dtv_l3_reader** reader_out) noexcept
    {
        (void)reader_id;
        (void)reader_out;
        return DTV_L3_UNSUPPORTED;
    }
    virtual dtv_l3_result lnb_set(std::uint32_t receiver_id,
                                  std::uint32_t mode) noexcept
    {
        (void)receiver_id;
        (void)mode;
        return DTV_L3_UNSUPPORTED;
    }
};

// Product-owned C adapter supplied to dtv_l3_open_ex()/dtv_l3_open_factory().
class L3ProductAdapter final {
public:
    explicit L3ProductAdapter(
        L3Product& product,
        const dtv_l3_firmware* firmware = nullptr) noexcept;
    ~L3ProductAdapter() noexcept = default;

    L3ProductAdapter(const L3ProductAdapter&) = delete;
    L3ProductAdapter& operator=(const L3ProductAdapter&) = delete;

    dtv_l3_product_adapter adapter() const noexcept { return adapter_; }

private:
    static dtv_l3_result scope_query_thunk(
        void* context, std::uint32_t receiver_id,
        dtv_l3_scope_info* scope) noexcept;
    static dtv_l3_result tune_thunk(
        void* context, dtv_l3_provider_binding* binding,
        std::uint32_t receiver_id, std::uint64_t requested_hz,
        std::uint64_t* programmed_hz) noexcept;
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
    static dtv_l3_result reader_acquire_thunk(
        void* context, std::uint32_t reader_id,
        dtv_l3_reader** reader_out) noexcept;
    static dtv_l3_result lnb_set_thunk(
        void* context, std::uint32_t receiver_id,
        std::uint32_t mode) noexcept;

    L3Product& product_;
    dtv_l3_product_adapter adapter_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_L3_ADAPTER_H

// SPDX-License-Identifier: GPL-2.0-only
#include "l3_test_harness.h"

#include "l3_adapter.h"
#include "l3_backend_product.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <new>
#include <vector>

namespace {

using namespace px4::userland;

class FakeTunerBackend final : public TunerServiceBackend {
public:
    FakeTunerBackend(std::uint8_t receiver_count,
                     int dual_receiver) noexcept
        : receiver_count_(receiver_count), dual_receiver_(dual_receiver)
    {
    }

    std::uint8_t receiver_count() const noexcept override
    {
        return receiver_count_;
    }

    bool receiver_supports(std::uint8_t receiver,
                           ipc::System system) const noexcept override
    {
        if (receiver >= receiver_count_) {
            return false;
        }
        if (dual_receiver_ >= 0 &&
            static_cast<int>(receiver) == dual_receiver_) {
            return true;
        }
        return (receiver < 2U) == (system == ipc::System::ISDB_S);
    }

    bool requires_terrestrial_lock_settle() const noexcept override
    {
        return false;
    }

    Result<void> open_receiver(std::uint8_t) noexcept override
    {
        ++open_calls;
        return Result<void>::success();
    }

    Result<void> tune_terrestrial(std::uint8_t receiver,
                                  std::uint32_t frequency_khz,
                                  std::uint32_t) noexcept override
    {
        (void)receiver;
        last_hz = static_cast<std::uint64_t>(frequency_khz) * 1000U;
        return Result<void>::success();
    }

    Result<void> tune_satellite(std::uint8_t receiver,
                                std::uint32_t frequency_khz,
                                std::uint32_t) noexcept override
    {
        (void)receiver;
        last_hz = static_cast<std::uint64_t>(frequency_khz) * 1000U;
        return Result<void>::success();
    }

    Result<TunerHzProgramming> tune_terrestrial_hz(
        std::uint8_t receiver, std::uint64_t frequency_hz,
        std::uint32_t) noexcept override
    {
        ++tune_calls;
        last_receiver = receiver;
        last_hz = frequency_hz;
        last_system = 0;
        TunerHzProgramming programmed;
        programmed.programmed_hz = frequency_hz;
        return Result<TunerHzProgramming>::success(programmed);
    }

    Result<TunerHzProgramming> tune_satellite_hz(
        std::uint8_t receiver, std::uint64_t frequency_hz,
        std::uint32_t) noexcept override
    {
        ++tune_calls;
        last_receiver = receiver;
        last_hz = frequency_hz;
        last_system = 1;
        TunerHzProgramming programmed;
        programmed.programmed_hz = frequency_hz;
        return Result<TunerHzProgramming>::success(programmed);
    }

    Result<bool> is_locked(std::uint8_t, ipc::System) noexcept override
    {
        return Result<bool>::success(true);
    }

    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }

    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }

    Result<void> close_receiver(std::uint8_t) noexcept override
    {
        ++close_calls;
        return Result<void>::success();
    }

    Result<void> begin_tune_power(std::uint8_t, ipc::System,
                                  std::uint8_t voltage) noexcept override
    {
        ++begin_power_calls;
        return voltage == 0U
            ? Result<void>::success()
            : Result<void>::failure(Error::UNSUPPORTED);
    }

    Result<void> commit_tune_power(std::uint8_t) noexcept override
    {
        ++commit_power_calls;
        return Result<void>::success();
    }

    Result<void> rollback_tune_power(std::uint8_t) noexcept override
    {
        ++rollback_power_calls;
        return Result<void>::success();
    }

    Result<void> start_capture(std::uint8_t receiver,
                               ipc::System) noexcept override
    {
        ++start_capture_calls;
        last_receiver = receiver;
        return Result<void>::success();
    }

    Result<void> stop_capture(std::uint8_t, ipc::System) noexcept override
    {
        ++stop_capture_calls;
        return Result<void>::success();
    }

    Result<void> shutdown() noexcept override
    {
        ++shutdown_calls;
        return Result<void>::success();
    }

    void mark_receiver_disconnected(std::uint8_t) noexcept override {}

    unsigned int open_calls = 0U;
    unsigned int close_calls = 0U;
    unsigned int tune_calls = 0U;
    unsigned int begin_power_calls = 0U;
    unsigned int commit_power_calls = 0U;
    unsigned int rollback_power_calls = 0U;
    unsigned int start_capture_calls = 0U;
    unsigned int stop_capture_calls = 0U;
    unsigned int shutdown_calls = 0U;
    unsigned int last_receiver = 0U;
    std::uint64_t last_hz = 0U;
    int last_system = 0;

private:
    std::uint8_t receiver_count_;
    int dual_receiver_;
};

class FakeStreamControl final : public TunerStreamControl {
public:
    Result<void> attach(const TunerAttachment& attachment) noexcept override
    {
        if (attachment.owner_client_id == 0U || attachment.lease_id == 0U ||
            attachment.attachment_id == 0U) {
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        }
        if (attached) {
            return Result<void>::failure(Error::BUSY);
        }
        attached = true;
        identity = attachment;
        delivered = false;
        return Result<void>::success();
    }

    Result<void> detach(const TunerAttachment& attachment) noexcept override
    {
        if (!attached || attachment.attachment_id != identity.attachment_id) {
            return Result<void>::failure(Error::NOT_FOUND);
        }
        attached = false;
        ++detach_calls;
        return Result<void>::success();
    }

    Result<TunerStreamReadResult> read(
        const TunerAttachment& attachment, MutableByteView output,
        Timeout) noexcept override
    {
        (void)attachment;
        TunerStreamReadResult result{};
        if (!attached) {
            result.eof = true;
            return Result<TunerStreamReadResult>::success(result);
        }
        if (!delivered) {
            const std::size_t capacity_packets = output.size / 188U;
            const std::size_t packets =
                capacity_packets < 2U ? capacity_packets : 2U;
            for (std::size_t packet = 0U; packet < packets; ++packet) {
                std::uint8_t* target = output.data + packet * 188U;
                for (std::size_t index = 0U; index < 188U; ++index) {
                    target[index] = static_cast<std::uint8_t>(
                        index == 0U ? 0x47U : (packet + index));
                }
            }
            result.bytes = packets * 188U;
            delivered = true;
            return Result<TunerStreamReadResult>::success(result);
        }
        result.eof = true;
        return Result<TunerStreamReadResult>::success(result);
    }

    Result<TunerStreamTerminal> terminal(
        const TunerAttachment&) const noexcept override
    {
        return Result<TunerStreamTerminal>::success(TunerStreamTerminal::none);
    }

    bool attached = false;
    bool delivered = false;
    unsigned int detach_calls = 0U;
    TunerAttachment identity{};
};

class FakeUsbPort final : public L3UsbPort {
public:
    FakeUsbPort(unsigned int claim_count, int fail_index,
                dtv_l3_cancel_scope scope) noexcept
        : fail_index_(fail_index), scope_(scope)
    {
        (void)claim_count;
    }

    dtv_l3_claim_result claim(dtv_l3_provider_binding*,
                              std::uint32_t claim_index) noexcept override
    {
        ++claim_calls;
        ++claim_attempts;
        if (static_cast<int>(claim_index) == fail_index_) {
            return {DTV_L3_FAILED, DTV_L3_CLAIM_RETAINED};
        }
        ++claim_successes;
        return {DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
    }

    dtv_l3_release_result release_claim(
        dtv_l3_provider_binding*, std::uint32_t claim_index) noexcept override
    {
        ++release_calls;
        release_order.push_back(claim_index);
        return {DTV_L3_RELEASE_QUIESCED, DTV_L3_CLAIM_NONE,
                DTV_L3_RESTORE_SUCCEEDED, DTV_L3_OK};
    }

    dtv_l3_result submit(dtv_l3_provider_binding* binding,
                         std::uint64_t transfer_id,
                         const dtv_l3_transfer_request* request,
                         dtv_l3_transfer_complete_fn complete,
                         void* callback_context) noexcept override
    {
        ++submit_calls;
        static const std::uint8_t data[4] = {0x11U, 0x22U, 0x33U, 0x44U};
        const dtv_l3_transfer_completion completion{
            DTV_L3_OK,
            request->in_capacity == 0U ? nullptr : data,
            request->in_capacity == 0U ? 0U : sizeof(data),
            request->in_capacity == 0U ? 0U : sizeof(data),
            1U};
        complete(binding, transfer_id, &completion, callback_context);
        return DTV_L3_ACCEPTED;
    }

    dtv_l3_result cancel(dtv_l3_provider_binding*, std::uint64_t) noexcept override
    {
        ++cancel_calls;
        return DTV_L3_ACCEPTED;
    }

    dtv_l3_cancel_scope cancel_scope() const noexcept override
    {
        return scope_;
    }

    int fail_index_ = -1;
    dtv_l3_cancel_scope scope_ = DTV_L3_CANCEL_SCOPE_TRANSFER;
    unsigned int claim_calls = 0U;
    unsigned int claim_attempts = 0U;
    unsigned int claim_successes = 0U;
    unsigned int release_calls = 0U;
    unsigned int submit_calls = 0U;
    unsigned int cancel_calls = 0U;
    std::vector<std::uint32_t> release_order;
};

}  // namespace

struct px4_l3_test_harness {
    px4_l3_test_harness(unsigned int claim_count, int fail_claim_index,
                        int dual_receiver) noexcept
        : profile{DeviceModel::px_w3u4, 0x083fU, "PX-W3U4", 1U, 4U,
                  false, true},
          backend(profile.receiver_count, dual_receiver),
          usb(claim_count, fail_claim_index, DTV_L3_CANCEL_SCOPE_PIPE),
          product(new (std::nothrow) L3BackendProduct(
              backend, stream, profile, true, DTV_L3_CANCEL_SCOPE_PIPE)),
          product_holder(product != nullptr
                             ? new (std::nothrow) L3ProductAdapter(*product,
                                                                  &firmware_view)
                             : nullptr),
          usb_holder(new (std::nothrow) L3UsbProviderAdapter(usb))
    {
        firmware_view.source = DTV_L3_FIRMWARE_BYTES;
        firmware_view.bytes = firmware;
        firmware_view.size = sizeof(firmware);
        if (product_holder != nullptr) {
            adapter = product_holder->adapter();
        }
        if (usb_holder != nullptr) {
            usb_provider = usb_holder->provider();
        }
        factory.struct_size = sizeof(factory);
        factory.factory_context = this;
        factory.create = &factory_create;
        factory.release = &factory_release;
    }

    ~px4_l3_test_harness() noexcept
    {
        delete usb_holder;
        delete product_holder;
        delete product;
    }

    static dtv_l3_result factory_create(
        void* context, const dtv_l3_open_args* args,
        const dtv_l3_product_adapter** adapter_out,
        uint32_t* claim_count_out,
        void** adapter_owner_out)
    {
        auto* harness = static_cast<px4_l3_test_harness*>(context);
        if (harness == nullptr || args == nullptr || adapter_out == nullptr ||
            claim_count_out == nullptr || adapter_owner_out == nullptr) {
            return DTV_L3_INVALID;
        }
        ++harness->factory_create_calls;
        harness->seen_args = *args;
        harness->seen_firmware = args->firmware;
        *adapter_out = &harness->adapter;
        *claim_count_out = 2U;
        *adapter_owner_out = harness;
        return DTV_L3_OK;
    }

    static void factory_release(void* owner)
    {
        auto* harness = static_cast<px4_l3_test_harness*>(owner);
        if (harness != nullptr) {
            ++harness->factory_release_calls;
        }
    }

    DeviceProfile profile;
    FakeTunerBackend backend;
    FakeStreamControl stream;
    FakeUsbPort usb;
    std::uint8_t firmware[4] = {0x10U, 0x20U, 0x30U, 0x40U};
    dtv_l3_firmware firmware_view{};
    L3BackendProduct* product = nullptr;
    L3ProductAdapter* product_holder = nullptr;
    L3UsbProviderAdapter* usb_holder = nullptr;
    dtv_l3_product_adapter adapter{};
    dtv_l3_usb_provider usb_provider{};
    dtv_l3_factory factory{};
    dtv_l3_open_args seen_args{};
    const dtv_l3_firmware* seen_firmware = nullptr;
    unsigned int factory_create_calls = 0U;
    unsigned int factory_release_calls = 0U;
};

extern "C" {

struct px4_l3_test_harness* px4_l3_test_create(unsigned int claim_count,
                                               int fail_claim_index,
                                               int dual_receiver)
{
    auto* harness = new (std::nothrow) px4_l3_test_harness(
        claim_count, fail_claim_index, dual_receiver);
    if (harness == nullptr || harness->product == nullptr ||
        harness->product_holder == nullptr || harness->usb_holder == nullptr) {
        delete harness;
        return nullptr;
    }
    return harness;
}

const dtv_l3_usb_provider* px4_l3_test_usb_provider(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? nullptr : &harness->usb_provider;
}

const dtv_l3_product_adapter* px4_l3_test_product_adapter(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? nullptr : &harness->adapter;
}

const dtv_l3_factory* px4_l3_test_factory(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? nullptr : &harness->factory;
}

const dtv_l3_open_args* px4_l3_test_seen_args(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? nullptr : &harness->seen_args;
}

const dtv_l3_firmware* px4_l3_test_seen_firmware(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? nullptr : harness->seen_firmware;
}

unsigned int px4_l3_test_factory_create_calls(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->factory_create_calls;
}

unsigned int px4_l3_test_factory_release_calls(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->factory_release_calls;
}

unsigned int px4_l3_test_claim_calls(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->usb.claim_calls;
}

unsigned int px4_l3_test_claim_successes(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->usb.claim_successes;
}

unsigned int px4_l3_test_release_calls(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->usb.release_calls;
}

unsigned int px4_l3_test_release_at(struct px4_l3_test_harness* harness,
                                     unsigned int index)
{
    if (harness == nullptr || index >= harness->usb.release_order.size()) {
        return 0xffffffffU;
    }
    return harness->usb.release_order[index];
}

unsigned int px4_l3_test_tune_calls(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->backend.tune_calls;
}

unsigned int px4_l3_test_open_calls(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->backend.open_calls;
}

unsigned int px4_l3_test_close_calls(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->backend.close_calls;
}

unsigned int px4_l3_test_start_capture_calls(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->backend.start_capture_calls;
}

unsigned int px4_l3_test_stop_capture_calls(
    struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->backend.stop_capture_calls;
}

unsigned int px4_l3_test_last_receiver(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->backend.last_receiver;
}

unsigned long long px4_l3_test_last_hz(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0ULL : harness->backend.last_hz;
}

int px4_l3_test_last_system(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? -1 : harness->backend.last_system;
}

unsigned int px4_l3_test_submit_calls(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->usb.submit_calls;
}

unsigned int px4_l3_test_cancel_calls(struct px4_l3_test_harness* harness)
{
    return harness == nullptr ? 0U : harness->usb.cancel_calls;
}

int px4_l3_test_destroy(struct px4_l3_test_harness* harness)
{
    if (harness == nullptr || harness->product == nullptr) {
        delete harness;
        return 1;
    }
    const dtv_l3_result closed = harness->product->close_all();
    delete harness;
    return closed == DTV_L3_OK ? 0 : 1;
}

}  // extern "C"

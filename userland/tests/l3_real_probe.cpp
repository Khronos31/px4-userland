// SPDX-License-Identifier: GPL-2.0-only
//
// Manual one-receiver probe for the experimental L3 product adapter.  It opens
// a caller-selected enclosure, uses the existing Q3U4 backend, and never runs
// under CTest.  Run it only on a host where the enclosure is assigned to this
// work; the host owns the USB claims through Q3U4Runtime and reports them as
// already-claimed to the L3 USB provider.

#include "l3_adapter.h"
#include "l3_q3u4_owner_backend.h"
#include "px4/firmware.h"
#include "px4/libusb_transport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>
#include <vector>
#include <thread>

namespace {

using namespace px4::userland;

dtv_l3_result from_error(Error error) noexcept
{
    switch (error) {
    case Error::OK: return DTV_L3_OK;
    case Error::INVALID_ARGUMENT:
    case Error::BUFFER_TOO_SMALL: return DTV_L3_INVALID;
    case Error::BUSY: return DTV_L3_BUSY;
    case Error::NOT_READY: return DTV_L3_FAILED;
    case Error::TIMEOUT: return DTV_L3_TIMEOUT;
    case Error::DISCONNECTED: return DTV_L3_DISCONNECTED;
    case Error::UNSUPPORTED: return DTV_L3_UNSUPPORTED;
    default: return DTV_L3_FAILED;
    }
}

// Q3U4Runtime already owns the libusb claims.  The L3 owner sees a borrowed
// claim; this host provider performs the actual bulk I/O through the runtime's
// transports.  Control transfers remain UNSUPPORTED at this seam.
class BorrowedRuntimeUsbPort final : public L3UsbPort {
public:
    explicit BorrowedRuntimeUsbPort(Q3U4Runtime& runtime) noexcept
        : runtime_(&runtime)
    {
    }

    dtv_l3_claim_result claim(dtv_l3_provider_binding*,
                              std::uint32_t claim_index) noexcept override
    {
        if (claim_index >= 2U) {
            return {DTV_L3_INVALID, DTV_L3_CLAIM_NONE};
        }
        ++claim_calls;
        return {DTV_L3_OK, DTV_L3_CLAIM_RETAINED};
    }

    dtv_l3_release_result release_claim(
        dtv_l3_provider_binding*, std::uint32_t claim_index) noexcept override
    {
        if (claim_index >= 2U) {
            return {DTV_L3_RELEASE_QUIESCENCE_UNKNOWN, DTV_L3_CLAIM_UNKNOWN,
                    DTV_L3_RESTORE_NOT_ATTEMPTED, DTV_L3_INVALID};
        }
        ++release_calls;
        return {DTV_L3_RELEASE_QUIESCED, DTV_L3_CLAIM_NONE,
                DTV_L3_RESTORE_SUCCEEDED, DTV_L3_OK};
    }

    dtv_l3_result submit(dtv_l3_provider_binding* binding,
                         std::uint64_t transfer_id,
                         const dtv_l3_transfer_request* request,
                         dtv_l3_transfer_complete_fn complete,
                         void* callback_context) noexcept override
    {
        if (runtime_ == nullptr || request == nullptr || complete == nullptr ||
            request->kind != DTV_L3_TRANSFER_BULK ||
            request->claim_index >= 2U) {
            return DTV_L3_UNSUPPORTED;
        }
        Transport& transport = request->claim_index == 0U
                                   ? runtime_->dev1() : runtime_->dev2();
        const Timeout timeout{request->timeout_ms};
        if (request->out_size != 0U) {
            const auto written = transport.bulk_write(
                static_cast<std::uint8_t>(request->endpoint),
                ByteView{request->out_data, request->out_size}, timeout);
            if (!written) {
                return from_error(written.error());
            }
            ++write_calls;
            const dtv_l3_transfer_completion completion{
                DTV_L3_OK, nullptr, 0U, written.value(), 1U};
            complete(binding, transfer_id, &completion, callback_context);
            ++completion_calls;
            return DTV_L3_ACCEPTED;
        }
        std::vector<std::uint8_t> buffer(request->in_capacity);
        const auto read = transport.bulk_read(
            static_cast<std::uint8_t>(request->endpoint),
            MutableByteView{buffer.data(), buffer.size()}, timeout);
        if (!read) {
            return from_error(read.error());
        }
        ++read_calls;
        const dtv_l3_transfer_completion completion{
            DTV_L3_OK, buffer.data(), read.value(), read.value(), 1U};
        complete(binding, transfer_id, &completion, callback_context);
        ++completion_calls;
        return DTV_L3_ACCEPTED;
    }

    dtv_l3_result cancel(dtv_l3_provider_binding*, std::uint64_t) noexcept override
    {
        return DTV_L3_UNSUPPORTED;
    }

    dtv_l3_cancel_scope cancel_scope() const noexcept override
    {
        return DTV_L3_CANCEL_SCOPE_PIPE;
    }

    Q3U4Runtime* runtime_ = nullptr;
    unsigned int claim_calls = 0U;
    unsigned int release_calls = 0U;
    unsigned int write_calls = 0U;
    unsigned int read_calls = 0U;
    unsigned int completion_calls = 0U;
};

class ProbeFactory final {
public:
    ProbeFactory(dtv_l3_product_adapter* adapter,
                 L3Q3U4OwnerBackend* product) noexcept
        : adapter_(adapter), product_(product)
    {
        factory_.struct_size = sizeof(factory_);
        factory_.factory_context = this;
        factory_.create = &ProbeFactory::create;
        factory_.release = &ProbeFactory::release;
    }

    const dtv_l3_factory* factory() const noexcept { return &factory_; }

    static dtv_l3_result create(void* context,
                                const dtv_l3_open_args* args,
                                const dtv_l3_product_adapter** adapter_out,
                                uint32_t* claim_count_out,
                                void** owner_out)
    {
        auto* self = static_cast<ProbeFactory*>(context);
        if (self == nullptr || args == nullptr || adapter_out == nullptr ||
            claim_count_out == nullptr || owner_out == nullptr) {
            return DTV_L3_INVALID;
        }
        self->seen_grant_count = args->grant_count;
        self->seen_grants = args->grants;
        self->seen_firmware = args->firmware;
        *adapter_out = self->adapter_;
        *claim_count_out = 2U;
        *owner_out = self;
        ++self->create_calls;
        return DTV_L3_OK;
    }

    static void release(void* owner)
    {
        auto* self = static_cast<ProbeFactory*>(owner);
        if (self != nullptr) {
            if (self->product_ != nullptr) {
                self->product_->abandon();
            }
            ++self->release_calls;
        }
    }

    unsigned int create_calls = 0U;
    unsigned int release_calls = 0U;
    uint32_t seen_grant_count = 0U;
    const dtv_l3_grant* seen_grants = nullptr;
    const dtv_l3_firmware* seen_firmware = nullptr;

private:
    dtv_l3_product_adapter* adapter_;
    L3Q3U4OwnerBackend* product_ = nullptr;
    dtv_l3_factory factory_{};
};

void usage()
{
    std::fprintf(
        stderr,
        "usage: px4_l3_real_probe --firmware PATH --serial SERIAL "
        "--receiver N --hz HZ [--wait-seconds N]\n");
}

bool parse_u64(const char* text, std::uint64_t* value)
{
    if (text == nullptr || value == nullptr || *text == '\0') {
        return false;
    }
    std::uint64_t result = 0U;
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        result = result * 10U + static_cast<std::uint64_t>(*cursor - '0');
    }
    *value = result;
    return true;
}

void sleep_ms(unsigned int milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

int wait_releasable(dtv_l3_context* owner, dtv_l3_close_status* status)
{
    for (unsigned int attempt = 0U; attempt < 3000U; ++attempt) {
        if (dtv_l3_close_status_get(owner, status) != DTV_L3_OK) {
            return 0;
        }
        if (status->releasable) {
            return 1;
        }
        sleep_ms(10U);
    }
    return 0;
}

int wait_open(dtv_l3_op* op, dtv_l3_op_result* result)
{
    for (unsigned int attempt = 0U; attempt < 1000U; ++attempt) {
        if (dtv_l3_op_poll(op, result) != DTV_L3_OK) {
            return 0;
        }
        if (result->state == DTV_L3_OP_DONE ||
            result->state == DTV_L3_OP_FAILED) {
            return 1;
        }
        sleep_ms(10U);
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    const char* firmware_path = nullptr;
    const char* serial = nullptr;
    std::uint64_t receiver_value = 0U;
    std::uint64_t frequency_hz = 0U;
    std::uint64_t wait_seconds = 15U;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        const bool has_value = index + 1 < argc;
        if (argument == "--firmware" && has_value) {
            firmware_path = argv[++index];
        } else if (argument == "--serial" && has_value) {
            serial = argv[++index];
        } else if (argument == "--receiver" && has_value) {
            if (!parse_u64(argv[++index], &receiver_value)) {
                usage();
                return 2;
            }
        } else if (argument == "--hz" && has_value) {
            if (!parse_u64(argv[++index], &frequency_hz)) {
                usage();
                return 2;
            }
        } else if (argument == "--wait-seconds" && has_value) {
            if (!parse_u64(argv[++index], &wait_seconds)) {
                usage();
                return 2;
            }
        } else {
            usage();
            return 2;
        }
    }
    if (firmware_path == nullptr || serial == nullptr ||
        receiver_value > 7U || frequency_hz == 0U) {
        usage();
        return 2;
    }

    const auto firmware = FirmwareProvider(firmware_path).load();
    if (!firmware) {
        std::fprintf(stderr, "firmware: %s\n", error_string(firmware.error()));
        return 3;
    }
    auto runtime = Q3U4Runtime::open_native(serial);
    if (!runtime) {
        std::fprintf(stderr, "open: %s\n", error_string(runtime.error()));
        return 3;
    }
    if (runtime.value()->bridge_count() != 2U ||
        runtime.value()->model() != DeviceModel::px_q3u4) {
        std::fprintf(stderr, "probe currently expects PX-Q3U4\n");
        return 4;
    }

    std::uint64_t programmed = 0U;
    std::size_t ts_bytes = 0U;
    dtv_l3_result opened_result = DTV_L3_FAILED;
    dtv_l3_result stream_stop_result = DTV_L3_FAILED;
    dtv_l3_result close_result = DTV_L3_FAILED;
    {
        Q3U4Runtime& device = *runtime.value();
        const DeviceProfile& profile = device_profile(device.model());
        L3Q3U4OwnerBackend product(profile, false, true, &firmware.value());
        BorrowedRuntimeUsbPort usb_port(device);
        const ByteView firmware_bytes = firmware.value().bytes();
        const dtv_l3_firmware_manifest manifest{
            1U, static_cast<std::uint64_t>(firmware_bytes.size),
            kIt930xFirmwareSha256.data(), 0U};
        const dtv_l3_firmware firmware_view{
            DTV_L3_FIRMWARE_BYTES, &manifest, nullptr, firmware_bytes.data,
            firmware_bytes.size, nullptr, nullptr};
        L3ProductAdapter product_adapter(product, &firmware_view);
        L3UsbProviderAdapter usb_adapter(usb_port);
        dtv_l3_product_adapter adapter = product_adapter.adapter();
        const dtv_l3_usb_provider usb = usb_adapter.provider();
        ProbeFactory factory(&adapter, &product);
        dtv_l3_grant_descriptor descriptors[2]{};
        for (std::size_t index = 0U; index < 2U; ++index) {
            const UsbLocation location = device.usb_location(index);
            descriptors[index].vendor_id = kQ3U4VendorId;
            descriptors[index].product_id = profile.product_id;
            descriptors[index].bus = location.has_bus ? location.bus : 0U;
            descriptors[index].address =
                location.has_address ? location.address : 0U;
            descriptors[index].num_interfaces = 1U;
            descriptors[index].num_endpoints = 2U;
        }
        dtv_l3_grant grants[2]{};
        grants[0].token = 1U;
        grants[0].descriptor = &descriptors[0];
        grants[1].token = 2U;
        grants[1].descriptor = &descriptors[1];
        dtv_l3_open_args open_args{};
        open_args.struct_size = sizeof(open_args);
        open_args.usb_provider = &usb;
        open_args.factory = factory.factory();
        open_args.grants = grants;
        open_args.grant_count = 2U;
        open_args.firmware = &firmware_view;
        open_args.power_permission = 0U;

        const dtv_l3_open_result opened = dtv_l3_open_with_args(&open_args);
        opened_result = opened.result;
        if (opened.result != DTV_L3_ACCEPTED || opened.owner == nullptr ||
            opened.op == nullptr) {
            std::fprintf(stderr, "open_factory: %d\n",
                         static_cast<int>(opened.result));
            return 7;
        }
        dtv_l3_op_result open_op_result{};
        if (!wait_open(opened.op, &open_op_result) ||
            open_op_result.result != DTV_L3_OK ||
            dtv_l3_op_release(opened.op) != DTV_L3_OK) {
            std::fprintf(stderr, "open wait: %d\n",
                         static_cast<int>(open_op_result.result));
            return 8;
        }
        dtv_l3_context* owner = opened.owner;
        dtv_l3_lease* lease = nullptr;
        dtv_l3_stream* stream_handle = nullptr;
        std::uint64_t revision = 0U;
        if (dtv_l3_lease_acquire(owner, static_cast<std::uint32_t>(
                                                   receiver_value),
                                 &lease) != DTV_L3_OK ||
            lease == nullptr ||
            dtv_l3_lease_tune(lease, frequency_hz, &programmed, &revision) !=
                DTV_L3_OK ||
            dtv_l3_lease_start(lease) != DTV_L3_OK ||
            dtv_l3_lease_attach(lease, &stream_handle) != DTV_L3_OK ||
            stream_handle == nullptr) {
            std::fprintf(stderr, "lease/tune/attach failed\n");
            (void)dtv_l3_close(owner, 2000U);
            return 9;
        }

        std::uint8_t buffer[188U * 64U];
        std::size_t bytes = 0U;
        dtv_l3_read_state read_state = DTV_L3_READ_WAIT;
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(wait_seconds);
        while (ts_bytes == 0U && std::chrono::steady_clock::now() < deadline) {
            const dtv_l3_result read = dtv_l3_stream_read_ts_wait(
                stream_handle, buffer, sizeof(buffer), &bytes, &read_state,
                1000U);
            if (read != DTV_L3_OK && read != DTV_L3_WAIT_TIMEOUT) {
                std::fprintf(stderr, "read: %d\n", static_cast<int>(read));
                break;
            }
            if (read == DTV_L3_OK && bytes != 0U) {
                ts_bytes += bytes;
            }
        }
        stream_stop_result = dtv_l3_stream_stop(stream_handle, 2000U);
        for (unsigned int attempt = 0U; attempt < 500U; ++attempt) {
            const dtv_l3_result released = dtv_l3_stream_release(
                stream_handle, DTV_L3_STREAM_RELEASE_DISCARD_UNREAD);
            if (released == DTV_L3_OK) {
                break;
            }
            if (released != DTV_L3_ACCEPTED && released != DTV_L3_BUSY) {
                break;
            }
            sleep_ms(10U);
        }
        for (unsigned int attempt = 0U; attempt < 500U; ++attempt) {
            const dtv_l3_result destroyed =
                dtv_l3_stream_destroy(stream_handle);
            if (destroyed == DTV_L3_OK) {
                break;
            }
            if (destroyed != DTV_L3_BUSY) {
                break;
            }
            sleep_ms(10U);
        }
        (void)dtv_l3_lease_release(lease);

        if (dtv_l3_close(owner, 2000U) != DTV_L3_OK) {
            std::fprintf(stderr, "close failed\n");
            return 10;
        }
        dtv_l3_close_status close_status{};
        if (!wait_releasable(owner, &close_status) ||
            dtv_l3_context_release(owner) != DTV_L3_OK) {
            std::fprintf(stderr, "release failed\n");
            return 11;
        }
        close_result = close_status.result;
        std::printf(
            "{\"receiver\":%llu,\"hz\":%llu,\"programmed_hz\":%llu,"
            "\"ts_bytes\":%zu,\"opened\":%d,\"stream_stop\":%d,"
            "\"close\":%d,\"quiesced\":%u,\"claims\":%u,\"releases\":%u,"
            "\"factory_create\":%u,\"factory_release\":%u}\n",
            static_cast<unsigned long long>(receiver_value),
            static_cast<unsigned long long>(frequency_hz),
            static_cast<unsigned long long>(programmed), ts_bytes,
            static_cast<int>(opened_result),
            static_cast<int>(stream_stop_result),
            static_cast<int>(close_result),
            static_cast<unsigned int>(close_status.physical_quiesced),
            usb_port.claim_calls, usb_port.release_calls,
            factory.create_calls, factory.release_calls);
    }

    runtime.value().reset();
    auto reopened = Q3U4Runtime::open_native(serial);
    if (!reopened) {
        std::fprintf(stderr, "reopen: %s\n",
                     error_string(reopened.error()));
        return 12;
    }
    std::printf("{\"reopened\":true}\n");
    reopened.value().reset();

    return ts_bytes != 0U && stream_stop_result == DTV_L3_OK &&
                   close_result == DTV_L3_OK
               ? 0
               : 13;
}

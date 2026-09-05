// SPDX-License-Identifier: GPL-2.0-only
#include "libusb_transport_internal.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <new>
#include <string>
#include <vector>

using px4::userland::DeviceObservation;
using px4::userland::Error;
using px4::userland::LibusbApi;
using px4::userland::MutableByteView;
using px4::userland::RuntimeTestAccess;
using px4::userland::StreamConfig;
using px4::userland::Timeout;
using px4::userland::UsbTopologyObservation;
using px4::userland::kTsInEndpoint;
using px4::userland::wait_for_libusb_detach_quiescence;

constexpr int kLibusbIoError = -1;

namespace {

#define DETACH_CHECK(condition)                                                        \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            return false;                                                              \
        }                                                                              \
    } while (false)

struct FakeDevice final {};
struct FakeHandle final {
    FakeDevice* device = nullptr;
};
struct FakeTransfer final {
    LibusbApi::TransferCallback callback = nullptr;
    void* context = nullptr;
    bool submitted = false;
};

class FakeApi final : public LibusbApi {
public:
    explicit FakeApi(std::vector<std::string>& events) noexcept : events_(events) {}
    ~FakeApi() noexcept override { events_.emplace_back("api-destroy"); }

    int init(Context* context, bool) noexcept override
    {
        *context = this;
        return 0;
    }
    void exit(Context) noexcept override { events_.emplace_back("exit"); }
    int get_device_list(Context, void** list, std::size_t* count) noexcept override
    {
        events_.emplace_back("list");
        if (detach_during_list) present_count = 0U;
        *list = this;
        *count = present_count;
        return 0;
    }
    Device list_device(void*, std::size_t index) noexcept override { return &devices[index]; }
    void free_device_list(void*) noexcept override { events_.emplace_back("free-list"); }
    int get_device_info(Device, DeviceObservation*, std::uint8_t*) noexcept override
    {
        return -1;
    }
    int get_config_descriptor(Device, unsigned int, ConfigDescriptor*) noexcept override
    {
        return -1;
    }
    int describe_config_descriptor(ConfigDescriptor, UsbTopologyObservation*) noexcept override
    {
        return -1;
    }
    void free_config_descriptor(ConfigDescriptor) noexcept override {}
    int get_device_from_handle(Handle handle, Device* output) noexcept override
    {
        events_.emplace_back("device");
        if (handle == nullptr || output == nullptr) return -1;
        *output = static_cast<FakeHandle*>(handle)->device;
        return *output == nullptr ? -1 : 0;
    }
    int get_serial_descriptor(Handle, std::uint8_t, char*, std::size_t,
                              std::size_t*) noexcept override
    {
        return -1;
    }
    int open(Device, Handle*) noexcept override { return -1; }
    int wrap_sys_device(Context, std::intptr_t, Handle*) noexcept override { return -1; }
    int claim_interface(Handle, int) noexcept override { return -1; }
    int release_interface(Handle, int) noexcept override
    {
        events_.emplace_back("release");
        return 0;
    }
    void close(Handle) noexcept override { events_.emplace_back("close"); }
    int bulk_transfer(Handle, std::uint8_t, std::uint8_t*, int, int*,
                      unsigned int) noexcept override
    {
        return -1;
    }
    Transfer alloc_transfer() noexcept override
    {
        return new (std::nothrow) FakeTransfer;
    }
    void fill_bulk_transfer(Transfer transfer, Handle, std::uint8_t, std::uint8_t*, int,
                            TransferCallback callback, void* context,
                            unsigned int) noexcept override
    {
        auto* fake = static_cast<FakeTransfer*>(transfer);
        fake->callback = callback;
        fake->context = context;
    }
    int submit_transfer(Transfer transfer) noexcept override
    {
        active_transfer = transfer;
        static_cast<FakeTransfer*>(transfer)->submitted = true;
        return 0;
    }
    int cancel_transfer(Transfer transfer) noexcept override
    {
        static_cast<FakeTransfer*>(transfer)->submitted = false;
        active_transfer = nullptr;
        return 0;
    }
    void free_transfer(Transfer transfer) noexcept override
    {
        if (active_transfer == transfer) active_transfer = nullptr;
        delete static_cast<FakeTransfer*>(transfer);
    }
    int handle_events(Context, unsigned int) noexcept override
    {
        events_.emplace_back("events");
        if (active_transfer != nullptr) {
            auto* transfer = static_cast<FakeTransfer*>(active_transfer);
            active_transfer = nullptr;
            transfer->submitted = false;
            transfer->callback(transfer, TransferStatus::no_device, 0,
                               transfer->context);
            return 0;
        }
        if (detach_on_event) present_count = 0U;
        return event_result;
    }
    bool requires_detach_quiescence() const noexcept override { return true; }

    std::vector<std::string>& events_;
    std::array<FakeDevice, 2U> devices{};
    Transfer active_transfer = nullptr;
    std::size_t present_count = devices.size();
    bool detach_on_event = true;
    bool detach_during_list = false;
    int event_result = 0;
};

bool test_runtime_quiesces_before_close_and_exit()
{
    std::vector<std::string> events;
    auto api = std::unique_ptr<FakeApi>(new FakeApi(events));
    FakeApi* api_ptr = api.get();
    FakeHandle first_handle{&api_ptr->devices[0U]};
    FakeHandle second_handle{&api_ptr->devices[1U]};
    const std::array<LibusbApi::Handle, 2U> handles{&first_handle, &second_handle};
    auto runtime = RuntimeTestAccess::create_shutdown_fixture(
        std::move(api), handles, false);
    DETACH_CHECK(runtime);
    DETACH_CHECK(runtime.value()->dev1().start_stream(
        StreamConfig{kTsInEndpoint, 188U, 1U}));
    const auto detached = runtime.value()->dev1().wait_stream({10U});
    DETACH_CHECK(!detached && detached.error() == Error::DISCONNECTED);
    events.clear();

    runtime.value().reset();

    const std::vector<std::string> expected{
        "device", "device", "list", "free-list", "events", "list", "free-list",
        "release", "close", "release", "close", "exit", "api-destroy"};
    DETACH_CHECK(events == expected);
    return true;
}

bool test_normal_runtime_skips_detach_quiescence()
{
    std::vector<std::string> events;
    auto api = std::unique_ptr<FakeApi>(new FakeApi(events));
    FakeApi* api_ptr = api.get();
    FakeHandle handle{&api_ptr->devices[0U]};
    const std::array<LibusbApi::Handle, 2U> handles{&handle, nullptr};
    auto runtime = RuntimeTestAccess::create_shutdown_fixture(
        std::move(api), handles, false);
    DETACH_CHECK(runtime);
    events.clear();

    runtime.value().reset();

    const std::vector<std::string> expected{
        "device", "list", "free-list", "release", "close", "exit", "api-destroy"};
    DETACH_CHECK(events == expected);
    DETACH_CHECK(std::count(events.begin(), events.end(), "events") == 0);
    return true;
}

bool test_unobserved_detach_is_synchronized_by_device_list()
{
    std::vector<std::string> events;
    auto api = std::unique_ptr<FakeApi>(new FakeApi(events));
    FakeApi* api_ptr = api.get();
    api_ptr->detach_during_list = true;
    FakeHandle handle{&api_ptr->devices[0U]};
    const std::array<LibusbApi::Handle, 2U> handles{&handle, nullptr};
    auto runtime = RuntimeTestAccess::create_shutdown_fixture(
        std::move(api), handles, false);
    DETACH_CHECK(runtime);
    events.clear();

    runtime.value().reset();

    const std::vector<std::string> expected{
        "device", "list", "free-list", "release", "close", "exit", "api-destroy"};
    DETACH_CHECK(events == expected);
    return true;
}

bool test_usb_io_hazard_waits_for_detach_without_no_device()
{
    std::vector<std::string> events;
    auto api = std::unique_ptr<FakeApi>(new FakeApi(events));
    FakeApi* api_ptr = api.get();
    FakeHandle handle{&api_ptr->devices[0U]};
    const std::array<LibusbApi::Handle, 2U> handles{&handle, nullptr};
    auto runtime = RuntimeTestAccess::create_shutdown_fixture(
        std::move(api), handles, false);
    DETACH_CHECK(runtime);
    std::array<std::uint8_t, 1U> buffer{};
    const auto io_failure = runtime.value()->dev1().bulk_read(
        0x81U, MutableByteView{buffer.data(), buffer.size()}, Timeout{1U});
    DETACH_CHECK(!io_failure && io_failure.error() == Error::USB_IO);
    events.clear();

    runtime.value().reset();

    const std::vector<std::string> expected{
        "device", "list", "free-list", "events", "list", "free-list",
        "release", "close", "exit", "api-destroy"};
    DETACH_CHECK(events == expected);
    return true;
}

bool test_detach_quiescence_is_bounded()
{
    std::vector<std::string> events;
    FakeApi api(events);
    api.detach_on_event = false;
    api.present_count = 1U;
    FakeHandle handle{&api.devices[0U]};
    const std::array<LibusbApi::Handle, 2U> handles{&handle, nullptr};

    DETACH_CHECK(!wait_for_libusb_detach_quiescence(
        api, &api, handles, true, 3U, 0U));
    DETACH_CHECK(std::count(events.begin(), events.end(), "events") == 3);
    DETACH_CHECK(std::count(events.begin(), events.end(), "list") == 4);
    return true;
}

bool test_event_failure_skips_libusb_exit()
{
    std::vector<std::string> events;
    auto api = std::unique_ptr<FakeApi>(new FakeApi(events));
    FakeApi* api_ptr = api.get();
    api_ptr->detach_on_event = false;
    api_ptr->event_result = kLibusbIoError;
    FakeHandle handle{&api_ptr->devices[0U]};
    const std::array<LibusbApi::Handle, 2U> handles{&handle, nullptr};
    auto runtime = RuntimeTestAccess::create_shutdown_fixture(
        std::move(api), handles, true);
    DETACH_CHECK(runtime);
    events.clear();

    runtime.value().reset();

    const std::vector<std::string> expected{
        "device", "list", "free-list", "events", "release", "close", "api-destroy"};
    DETACH_CHECK(events == expected);
    DETACH_CHECK(std::find(events.begin(), events.end(), "exit") == events.end());
    return true;
}

}  // namespace

int main()
{
    if (!test_runtime_quiesces_before_close_and_exit() ||
        !test_normal_runtime_skips_detach_quiescence() ||
        !test_unobserved_detach_is_synchronized_by_device_list() ||
        !test_usb_io_hazard_waits_for_detach_without_no_device() ||
        !test_detach_quiescence_is_bounded() ||
        !test_event_failure_skips_libusb_exit()) {
        return 1;
    }
    std::puts("libusb detach shutdown tests: PASS");
    return 0;
}

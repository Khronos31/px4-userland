// SPDX-License-Identifier: GPL-2.0-only
#include "px4/card_service.h"
#include "px4/control_server.h"
#include "px4/pcsc_ifd_adapter.h"
#include "px4/tuner_service.h"
#include "dtv/userland/host_listener_context/enclosure_context.h"
#include "dtv/userland/wire.h"
#include "test_temp_directory.h"

extern "C" {
#include <ifdhandler.h>
}

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;
using namespace px4::userland::ipc::posix;
using namespace px4::userland::pcsc;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,       \
                         #condition);                                                       \
            return false;                                                                   \
        }                                                                                   \
    } while (false)

struct MockState final {
    bool present = false;
    bool initialized = false;
    Error factory_connect_error = Error::OK;
    Error status_error = Error::OK;
    Error connect_error = Error::OK;
    Error reset_error = Error::OK;
    Error transmit_error = Error::OK;
    Error disconnect_error = Error::OK;
    std::uint64_t generation = 1U;
    std::size_t factory_connects = 0U;
    std::size_t card_connects = 0U;
    std::size_t resets = 0U;
    std::size_t transmits = 0U;
    std::size_t disconnects = 0U;
    std::size_t closes = 0U;
};

IfdCardStatus mock_status(const MockState& state) noexcept
{
    IfdCardStatus status{};
    status.present = state.present;
    status.initialized = state.initialized;
    status.reader_generation = state.generation;
    if (state.initialized) {
        status.atr[0] = 0x3bU;
        status.atr[1] = 0x10U;
        status.atr[2] = 0x12U;
        status.atr_length = 3U;
    }
    return status;
}

class MockClient final : public IfdCardClient {
public:
    explicit MockClient(MockState& state) noexcept : state_(state) {}

    Result<IfdCardStatus> status() noexcept override
    {
        return state_.status_error == Error::OK ?
                   Result<IfdCardStatus>::success(mock_status(state_)) :
                   Result<IfdCardStatus>::failure(state_.status_error);
    }

    Result<IfdCardConnectResult> connect_shared() noexcept override
    {
        ++state_.card_connects;
        if (state_.connect_error != Error::OK) {
            return Result<IfdCardConnectResult>::failure(state_.connect_error);
        }
        if (!state_.present) {
            return Result<IfdCardConnectResult>::failure(Error::NO_CARD);
        }
        state_.initialized = true;
        const auto status = mock_status(state_);
        IfdCardConnectResult result{};
        result.handle = 0x1234U;
        result.atr = status.atr;
        result.atr_length = status.atr_length;
        return Result<IfdCardConnectResult>::success(result);
    }

    Result<void> disconnect(std::uint64_t handle) noexcept override
    {
        ++state_.disconnects;
        if (handle != 0x1234U) return Result<void>::failure(Error::NOT_FOUND);
        if (state_.disconnect_error != Error::OK) {
            return Result<void>::failure(state_.disconnect_error);
        }
        state_.initialized = false;
        return Result<void>::success();
    }

    Result<IfdCardConnectResult> reset(std::uint64_t handle) noexcept override
    {
        ++state_.resets;
        if (handle != 0x1234U) {
            return Result<IfdCardConnectResult>::failure(Error::NOT_FOUND);
        }
        if (state_.reset_error != Error::OK) {
            return Result<IfdCardConnectResult>::failure(state_.reset_error);
        }
        if (!state_.present) {
            return Result<IfdCardConnectResult>::failure(Error::NO_CARD);
        }
        state_.initialized = true;
        const auto status = mock_status(state_);
        IfdCardConnectResult result{};
        result.handle = handle;
        result.atr = status.atr;
        result.atr_length = status.atr_length;
        return Result<IfdCardConnectResult>::success(result);
    }

    Result<std::size_t> transmit(std::uint64_t handle, ByteView apdu,
                                 MutableByteView response) noexcept override
    {
        ++state_.transmits;
        if (state_.transmit_error != Error::OK) {
            return Result<std::size_t>::failure(state_.transmit_error);
        }
        if (handle != 0x1234U || apdu.size == 0U || response.size < 2U) {
            return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
        }
        response.data[0] = 0x90U;
        response.data[1] = 0x00U;
        return Result<std::size_t>::success(2U);
    }

    void close() noexcept override { ++state_.closes; }

private:
    MockState& state_;
};

class MockFactory final : public IfdCardClientFactory {
public:
    explicit MockFactory(MockState& state) noexcept : state_(state) {}

    Result<std::unique_ptr<IfdCardClient>> connect(
        const IfdEndpoint& endpoint) noexcept override
    {
        ++state_.factory_connects;
        if (state_.factory_connect_error != Error::OK) {
            return Result<std::unique_ptr<IfdCardClient>>::failure(
                state_.factory_connect_error);
        }
        if (endpoint.device_instance != "00001205000960") {
            return Result<std::unique_ptr<IfdCardClient>>::failure(Error::NOT_FOUND);
        }
        return Result<std::unique_ptr<IfdCardClient>>::success(
            std::unique_ptr<IfdCardClient>(new MockClient(state_)));
    }

private:
    MockState& state_;
};

class NoopTunerBackend final : public TunerServiceBackend {
public:
    Result<void> open_receiver(std::uint8_t) noexcept override
    { return Result<void>::success(); }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t,
                                  std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<bool> is_locked(std::uint8_t, System) noexcept override
    { return Result<bool>::success(true); }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> close_receiver(std::uint8_t) noexcept override
    { return Result<void>::success(); }
};

class NoopTunerNonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, ipc::kNonceLength>> generate() noexcept override
    { return Result<std::array<std::uint8_t, ipc::kNonceLength>>::success({}); }
};

class NoopTunerTime final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override { return 0U; }
    void sleep_ms(std::uint32_t) noexcept override {}
};

bool replace_placeholder(std::string& text, std::string_view placeholder,
                         std::string_view value)
{
    const std::size_t offset = text.find(placeholder);
    if (offset == std::string::npos ||
        text.find(placeholder, offset + placeholder.size()) != std::string::npos) {
        return false;
    }
    text.replace(offset, placeholder.size(), value);
    return true;
}

std::string reader_setting(const std::string& config, std::string_view key)
{
    std::size_t line_start = 0U;
    while (line_start < config.size()) {
        const std::size_t line_end = config.find('\n', line_start);
        const std::size_t length = line_end == std::string::npos ?
                                       config.size() - line_start :
                                       line_end - line_start;
        const std::string_view line(config.data() + line_start, length);
        if (line.size() >= key.size() && line.substr(0U, key.size()) == key) {
            const std::size_t value_start = line.find_first_not_of(" \t", key.size());
            return value_start == std::string_view::npos ?
                       std::string{} : std::string(line.substr(value_start));
        }
        if (line_end == std::string::npos) break;
        line_start = line_end + 1U;
    }
    return {};
}

bool test_reader_config_template()
{
    const std::filesystem::path source_file(__FILE__);
    const std::filesystem::path repository =
        source_file.parent_path().parent_path().parent_path();
    const std::filesystem::path template_path =
        repository / "packaging/pcsc/reader.conf.d/px4-userland.conf.in";
    std::ifstream input(template_path);
    CHECK(input.good());
    std::string config;
    std::string line;
    while (std::getline(input, line)) {
        config.append(line);
        config.push_back('\n');
    }
    CHECK(input.eof());

    constexpr std::string_view runtime = "/tmp/px4-ifd";
    constexpr std::string_view serial = "00001205000960";
    constexpr std::string_view library = "/tmp/px4-ifd/libpx4-userland-ifd.so";
    for (const std::string_view access : {"user", "group"}) {
        std::string rendered = config;
        CHECK(replace_placeholder(rendered, "@PX4_RUNTIME_DIR@", runtime));
        CHECK(replace_placeholder(rendered, "@PX4_BASE_SERIAL@", serial));
        CHECK(replace_placeholder(rendered, "@PX4_ACCESS@", access));
        CHECK(replace_placeholder(rendered, "@PX4_IFD_LIBRARY@", library));
        CHECK(rendered.find("@PX4_") == std::string::npos);

        const std::string device_setting = reader_setting(rendered, "DEVICENAME");
        const std::string library_setting = reader_setting(rendered, "LIBPATH");
        CHECK(!device_setting.empty() && device_setting.front() != '"' &&
              device_setting.back() != '"' && device_setting.find(';') == std::string::npos);
        CHECK(!library_setting.empty() && library_setting.front() != '"' &&
              library_setting.back() != '"');
        CHECK(device_setting == "px4-userland:runtime=/tmp/px4-ifd:device=00001205000960:access=" +
                                   std::string(access));
        CHECK(library_setting == library);
        const auto parsed = parse_ifd_device_name(device_setting.c_str());
        CHECK(parsed && parsed.value().runtime_directory == runtime &&
              parsed.value().device_instance == serial &&
              parsed.value().group_access == (access == "group"));
    }
    return true;
}

bool test_device_name_and_error_mapping()
{
    const auto parsed = parse_ifd_device_name(
        "px4-userland:runtime=/run/user/1000:device=00001205000960:access=user");
    CHECK(parsed && parsed.value().runtime_directory == "/run/user/1000" &&
          parsed.value().device_instance == "00001205000960" &&
          !parsed.value().group_access);
    const auto default_runtime = parse_ifd_device_name(
        "px4-userland:device=00001205000960:access=group");
    CHECK(default_runtime && default_runtime.value().runtime_directory.empty() &&
          default_runtime.value().group_access);
    const auto named = parse_ifd_device_name(
        "px4-userland:runtime=/run/user/1000:instance=receiver.left:access=group");
    CHECK(named && named.value().runtime_directory == "/run/user/1000" &&
          named.value().device_instance == "receiver.left" && named.value().group_access);
    CHECK(!parse_ifd_device_name(
        "px4-userland:device=00001205000960:instance=receiver.left"));
    CHECK(!parse_ifd_device_name("px4-userland:instance=."));
    CHECK(!parse_ifd_device_name("px4-userland:instance=.."));
    CHECK(!parse_ifd_device_name("px4-userland:instance=bad/path"));
    CHECK(!parse_ifd_device_name("px4-userland:instance=bad:token"));
    CHECK(!parse_ifd_device_name("px4-userland:instance=999999999999999"));
    CHECK(!parse_ifd_device_name(
        "px4-userland:instance=receiver.left:instance=receiver.right"));
    CHECK(!parse_ifd_device_name(nullptr));
    CHECK(!parse_ifd_device_name("px4-userland:runtime=relative:device=00001205000960"));
    CHECK(!parse_ifd_device_name(
        "px4-userland:runtime=/tmp/px4:ifd:device=00001205000960"));
    CHECK(!parse_ifd_device_name("px4-userland:device=0000120500096"));
    CHECK(!parse_ifd_device_name(
        "px4-userland:device=00001205000960:device=00001205000960"));
    CHECK(!parse_ifd_device_name("px4-userland:device=00001205000960:unknown=x"));
    CHECK(!parse_ifd_device_name(
        "\"px4-userland:device=00001205000960:access=user\""));
    CHECK(map_ifd_error(Error::TIMEOUT, IfdOperation::transmit) ==
          IfdResult::response_timeout);
    CHECK(map_ifd_error(Error::NO_CARD, IfdOperation::power) ==
          IfdResult::error_power_action);
    CHECK(map_ifd_error(Error::CARD_REMOVED, IfdOperation::transmit) ==
          IfdResult::icc_not_present);
    CHECK(map_ifd_error(Error::DISCONNECTED, IfdOperation::status) ==
          IfdResult::no_such_device);
    CHECK(map_ifd_error(Error::BUFFER_TOO_SMALL, IfdOperation::transmit) ==
          IfdResult::insufficient_buffer);
    CHECK(map_ifd_error(Error::RESOURCE_LIMIT, IfdOperation::transmit) ==
          IfdResult::communication_error);
    return true;
}

bool test_adapter_state_and_recovery()
{
    MockState state;
    MockFactory factory(state);
    IfdAdapter adapter(factory);
    constexpr const char* name =
        "px4-userland:runtime=/tmp:device=00001205000960:access=user";
    CHECK(adapter.create_channel(0U, 0U) == IfdResult::not_supported);
    CHECK(adapter.create_channel_by_name(1U, name) == IfdResult::no_such_device);
    CHECK(adapter.create_channel_by_name(0U, name) == IfdResult::success);
    CHECK(adapter.create_channel_by_name(0U, name) ==
          IfdResult::communication_error);
    CHECK(adapter.presence(0U) == IfdResult::icc_not_present);

    std::array<std::uint8_t, kIfdAtrMaxLength> atr{};
    std::size_t atr_length = atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::power_up,
                        MutableByteView{atr.data(), atr.size()}, atr_length) ==
          IfdResult::error_power_action);
    CHECK(atr_length == 0U);

    state.present = true;
    ++state.generation;
    CHECK(adapter.presence(0U) == IfdResult::icc_present);
    atr_length = atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::power_up,
                        MutableByteView{atr.data(), atr.size()}, atr_length) ==
          IfdResult::success);
    CHECK(atr_length == 3U && atr[0] == 0x3bU && atr[2] == 0x12U);
    CHECK(adapter.set_protocol(0U, kIfdSetProtocolT1, 0U, 0U, 0U, 0U) ==
          IfdResult::success);
    CHECK(adapter.set_protocol(0U, kIfdSetProtocolT1, kIfdNegotiatePts1,
                               0x12U, 0U, 0U) == IfdResult::success);
    CHECK(adapter.set_protocol(0U, kIfdSetProtocolT1, kIfdNegotiatePts1,
                               0x11U, 0U, 0U) == IfdResult::error_pts_failure);
    CHECK(adapter.set_protocol(0U, 1U, 0U, 0U, 0U, 0U) ==
          IfdResult::protocol_not_supported);

    std::array<std::uint8_t, 8U> capability{};
    std::size_t capability_length = 0U;
    CHECK(adapter.get_capability(0U, kTagIfdAtr,
                                 MutableByteView{nullptr, 0U}, capability_length) ==
          IfdResult::insufficient_buffer);
    CHECK(capability_length == 3U);
    capability_length = capability.size();
    CHECK(adapter.get_capability(
              0U, kAttrAsyncProtocolTypes,
              MutableByteView{capability.data(), capability.size()}, capability_length) ==
          IfdResult::success);
    CHECK(capability_length == 4U && capability[0] == 2U);
    CHECK(adapter.get_capability(
              0U, 0xffffffffU,
              MutableByteView{capability.data(), capability.size()}, capability_length) ==
          IfdResult::error_tag);
    const std::array<std::uint8_t, 1U> slot{0U};
    CHECK(adapter.set_capability(0U, kTagIfdSlotNumber,
                                 ByteView{slot.data(), slot.size()}) ==
          IfdResult::success);
    CHECK(adapter.set_capability(0U, kTagIfdAtr,
                                 ByteView{slot.data(), slot.size()}) ==
          IfdResult::error_value_read_only);

    const std::array<std::uint8_t, 5U> apdu{0x90U, 0x30U, 0x00U, 0x00U, 0x00U};
    std::array<std::uint8_t, 16U> response{};
    for (const std::uint32_t wrong_protocol :
         std::array<std::uint32_t, 2U>{0U, kIfdSetProtocolT1}) {
        std::size_t response_length = response.size();
        CHECK(adapter.transmit(
                  0U, wrong_protocol, ByteView{apdu.data(), apdu.size()},
                  MutableByteView{response.data(), response.size()}, response_length) ==
              IfdResult::protocol_not_supported);
        CHECK(response_length == 0U);
    }
    for (std::size_t iteration = 0U; iteration < 100U; ++iteration) {
        std::size_t response_length = response.size();
        CHECK(adapter.transmit(
                  0U, kIfdTransmitProtocolT1,
                  ByteView{apdu.data(), apdu.size()},
                  MutableByteView{response.data(), response.size()}, response_length) ==
              IfdResult::success);
        CHECK(response_length == 2U && response[0] == 0x90U && response[1] == 0x00U);
    }
    CHECK(state.transmits == 100U);

    std::atomic<bool> concurrent_ok{true};
    std::array<std::thread, 4U> workers;
    for (std::size_t worker = 0U; worker < workers.size(); ++worker) {
        workers[worker] = std::thread([&]() {
            std::array<std::uint8_t, 8U> worker_response{};
            for (std::size_t iteration = 0U; iteration < 25U; ++iteration) {
                std::size_t worker_length = worker_response.size();
                if (adapter.transmit(
                        0U, kIfdTransmitProtocolT1,
                        ByteView{apdu.data(), apdu.size()},
                        MutableByteView{worker_response.data(), worker_response.size()},
                        worker_length) != IfdResult::success || worker_length != 2U) {
                    concurrent_ok.store(false);
                    return;
                }
            }
        });
    }
    for (std::thread& worker : workers) worker.join();
    CHECK(concurrent_ok.load() && state.transmits == 200U);

    state.present = false;
    state.initialized = false;
    ++state.generation;
    CHECK(adapter.presence(0U) == IfdResult::icc_not_present);
    std::size_t response_length = response.size();
    CHECK(adapter.transmit(
              0U, kIfdTransmitProtocolT1,
              ByteView{apdu.data(), apdu.size()},
              MutableByteView{response.data(), response.size()}, response_length) ==
          IfdResult::icc_not_present);
    state.present = true;
    ++state.generation;
    CHECK(adapter.presence(0U) == IfdResult::icc_present);
    atr_length = atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::reset,
                        MutableByteView{atr.data(), atr.size()}, atr_length) ==
          IfdResult::success);
    CHECK(state.resets == 1U);

    atr_length = atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::power_down,
                        MutableByteView{atr.data(), atr.size()}, atr_length) ==
          IfdResult::success);
    CHECK(atr_length == 0U && state.disconnects == 1U);
    CHECK(adapter.close_channel(0U) == IfdResult::success);
    CHECK(state.closes == 2U);

    state.status_error = Error::OK;
    CHECK(adapter.create_channel_by_name(0U, name) == IfdResult::success);
    state.status_error = Error::DISCONNECTED;
    CHECK(adapter.presence(0U) == IfdResult::communication_error);
    CHECK(adapter.presence(0U) == IfdResult::communication_error);
    state.status_error = Error::NOT_READY;
    CHECK(adapter.presence(0U) == IfdResult::communication_error);
    state.status_error = Error::NOT_FOUND;
    CHECK(adapter.presence(0U) == IfdResult::no_such_device);
    state.status_error = Error::TIMEOUT;
    CHECK(adapter.presence(0U) == IfdResult::communication_error);
    state.status_error = Error::UNSUPPORTED;
    CHECK(adapter.presence(0U) == IfdResult::communication_error);
    state.status_error = Error::NO_CARD;
    CHECK(adapter.presence(0U) == IfdResult::icc_not_present);
    state.status_error = Error::CARD_REMOVED;
    CHECK(adapter.presence(0U) == IfdResult::icc_not_present);
    state.factory_connect_error = Error::TIMEOUT;
    CHECK(adapter.close_channel(0U) == IfdResult::success);
    CHECK(adapter.create_channel_by_name(0U, name) == IfdResult::success);
    CHECK(adapter.presence(0U) == IfdResult::communication_error);
    state.factory_connect_error = Error::UNSUPPORTED;
    CHECK(adapter.presence(0U) == IfdResult::communication_error);
    CHECK(state.factory_connects == 11U && state.closes == 8U);
    return true;
}

bool test_adapter_lazy_reconnect()
{
    MockState state;
    state.present = true;
    state.factory_connect_error = Error::NOT_READY;
    MockFactory factory(state);
    IfdAdapter adapter(factory);
    constexpr const char* name =
        "px4-userland:device=00001205000960:access=user";

    CHECK(adapter.create_channel_by_name(0U, name) == IfdResult::success);
    CHECK(state.factory_connects == 1U);
    CHECK(adapter.presence(0U) == IfdResult::communication_error);
    CHECK(state.factory_connects == 2U);

    std::array<std::uint8_t, 1U> attribute{};
    std::size_t attribute_length = attribute.size();
    CHECK(adapter.get_capability(
              0U, kAttrIccPresence,
              MutableByteView{attribute.data(), attribute.size()},
              attribute_length) == IfdResult::success);
    CHECK(attribute_length == 1U && attribute[0] == 0U);

    std::array<std::uint8_t, kIfdAtrMaxLength> atr{};
    std::size_t atr_length = atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::power_up,
                        MutableByteView{atr.data(), atr.size()}, atr_length) ==
          IfdResult::icc_not_present);
    CHECK(atr_length == 0U);
    const std::array<std::uint8_t, 1U> apdu{0x00U};
    std::array<std::uint8_t, 2U> response{};
    std::size_t response_length = response.size();
    CHECK(adapter.transmit(
              0U, kIfdTransmitProtocolT1, ByteView{apdu.data(), apdu.size()},
              MutableByteView{response.data(), response.size()}, response_length) ==
          IfdResult::icc_not_present);
    CHECK(response_length == 0U);
    atr_length = atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::power_down,
                        MutableByteView{atr.data(), atr.size()}, atr_length) ==
          IfdResult::success);
    CHECK(atr_length == 0U);

    state.factory_connect_error = Error::OK;
    CHECK(adapter.presence(0U) == IfdResult::icc_present);
    CHECK(state.factory_connects == 6U);
    CHECK(adapter.close_channel(0U) == IfdResult::success);
    CHECK(state.closes == 1U);
    CHECK(adapter.close_channel(0U) == IfdResult::no_such_device);
    return true;
}

bool test_adapter_timeout_and_buffer_errors()
{
    MockState state;
    state.present = true;
    MockFactory factory(state);
    IfdAdapter adapter(factory);
    constexpr const char* name =
        "px4-userland:device=00001205000960:access=user";
    CHECK(adapter.create_channel_by_name(0U, name) == IfdResult::success);
    std::array<std::uint8_t, kIfdAtrMaxLength> atr{};
    std::size_t atr_length = atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::power_up,
                        MutableByteView{atr.data(), atr.size()}, atr_length) ==
          IfdResult::success);

    state.transmit_error = Error::TIMEOUT;
    const std::array<std::uint8_t, 1U> apdu{0x00U};
    std::array<std::uint8_t, 8U> response{};
    std::size_t response_length = response.size();
    CHECK(adapter.transmit(
              0U, kIfdTransmitProtocolT1,
              ByteView{apdu.data(), apdu.size()},
              MutableByteView{response.data(), response.size()}, response_length) ==
          IfdResult::response_timeout);
    CHECK(response_length == 0U && state.closes == 1U);
    CHECK(adapter.presence(0U) == IfdResult::icc_present);
    CHECK(state.factory_connects == 2U);

    state.transmit_error = Error::OK;
    state.initialized = false;
    std::array<std::uint8_t, 1U> short_atr{};
    atr_length = short_atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::power_up,
                        MutableByteView{short_atr.data(), short_atr.size()}, atr_length) ==
          IfdResult::insufficient_buffer);
    CHECK(atr_length == 3U);
    atr_length = atr.size();
    CHECK(adapter.power(0U, IfdPowerAction::reset,
                        MutableByteView{atr.data(), atr.size()}, atr_length) ==
          IfdResult::success);
    CHECK(adapter.close_channel(0U) == IfdResult::success);
    return true;
}

class TempRuntime final {
public:
    TempRuntime()
    {
        std::string pattern = test::temporary_directory_template("px4-ifd-");
        char* made = ::mkdtemp(pattern.data());
        if (made != nullptr) {
            path_ = made;
            valid_ = ::chmod(path_.c_str(), 0700) == 0;
        }
    }
    ~TempRuntime() noexcept
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    bool valid() const noexcept { return valid_; }
    const std::string& path() const noexcept { return path_; }
private:
    std::string path_;
    bool valid_ = false;
};

class Backend final : public CardServiceBackend {
public:
    Result<void> set_power(bool on) noexcept override
    {
        powered = on;
        return Result<void>::success();
    }
    Result<void> initialize_uart() noexcept override
    {
        return Result<void>::success();
    }
    Result<bool> detect_card() noexcept override
    {
        return Result<bool>::success(present.load());
    }
    std::atomic<bool> present{true};
    bool powered = false;
};

class Session final : public CardProtocolSession {
public:
    Session()
    {
        atr_.bytes[0] = 0x3bU;
        atr_.bytes[1] = 0x00U;
        atr_.length = 2U;
    }
    Result<void> initialize() noexcept override
    {
        if (!present()) return Result<void>::failure(Error::NO_CARD);
        initialized_ = true;
        return Result<void>::success();
    }
    Result<std::size_t> transmit(ByteView apdu,
                                 MutableByteView response) noexcept override
    {
        if (!initialized_ || apdu.size == 0U || response.size < 2U) {
            return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
        }
        response.data[0] = 0x90U;
        response.data[1] = 0x00U;
        return Result<std::size_t>::success(2U);
    }
    bool initialized() const noexcept override { return initialized_; }
    const CardAtrBytes& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override { initialized_ = false; }
    void bind(Backend& backend) noexcept { backend_ = &backend; }
private:
    bool present() const noexcept { return backend_ != nullptr && backend_->present.load(); }
    Backend* backend_ = nullptr;
    CardAtrBytes atr_{};
    bool initialized_ = false;
};

class ServerRunner final {
public:
    explicit ServerRunner(PosixControlServer& server) noexcept : server_(server)
    {
        thread_ = std::thread([this]() {
            while (!stop_.load()) {
                if (!server_.poll_once(Timeout{10U})) {
                    healthy_.store(false);
                    return;
                }
            }
        });
    }
    ~ServerRunner() noexcept { stop_and_join(); }
    void stop_and_join() noexcept
    {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
    }
    bool healthy() const noexcept { return healthy_.load(); }
private:
    PosixControlServer& server_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> healthy_{true};
    std::thread thread_;
};

#if !defined(PX4_CORE_IFD)
bool test_exported_abi_over_real_ipc()
{
    TempRuntime runtime;
    CHECK(runtime.valid());
    constexpr const char* serial = "00001205000960";
    const EndpointConfig endpoint{runtime.path().c_str(), serial,
                                  kControlEndpointName,
                                  EndpointAccess::private_user};
    Backend backend;
    Session session;
    session.bind(backend);
    CardService service(backend, session);
    NoopTunerBackend tuner_backend;
    NoopTunerNonce tuner_nonce;
    NoopTunerTime tuner_time;
    TunerService tuner_service(tuner_backend, tuner_nonce, tuner_time);
    auto server_result = PosixControlServer::create(
        endpoint, service, tuner_service, serial);
    CHECK(server_result);
    std::unique_ptr<PosixControlServer> server = std::move(server_result.value());
    const std::string socket_path = server->endpoint_path();
    ServerRunner runner(*server);

    std::string device_name = "px4-userland:runtime=" + runtime.path() +
                              ":device=" + serial + ":access=user";
    CHECK(IFDHCreateChannel(0U, 0U) == IFD_NOT_SUPPORTED);
    CHECK(IFDHCreateChannelByName(1U, device_name.data()) == IFD_NO_SUCH_DEVICE);
    CHECK(IFDHCreateChannelByName(0U, device_name.data()) == IFD_SUCCESS);
    CHECK(IFDHICCPresence(0U) == IFD_ICC_PRESENT);

    std::array<UCHAR, MAX_ATR_SIZE> atr{};
    DWORD atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_POWER_UP, atr.data(), &atr_length) == IFD_SUCCESS);
    CHECK(atr_length == 2U && atr[0] == 0x3bU);
    CHECK(IFDHSetProtocolParameters(0U, SCARD_PROTOCOL_T1, 0U, 0U, 0U, 0U) ==
          IFD_SUCCESS);

    atr.fill(0U);
    atr_length = 0U;
    CHECK(IFDHPowerICC(0U, IFD_RESET, atr.data(), &atr_length) == IFD_SUCCESS);
    CHECK(atr_length == 2U && atr[0] == 0x3bU && atr[1] == 0x00U);

    DWORD queried_length = 0U;
    CHECK(IFDHGetCapabilities(0U, TAG_IFD_ATR, &queried_length, nullptr) ==
          IFD_ERROR_INSUFFICIENT_BUFFER);
    CHECK(queried_length == 2U);
    std::array<UCHAR, MAX_ATR_SIZE> queried_atr{};
    queried_length = static_cast<DWORD>(queried_atr.size());
    CHECK(IFDHGetCapabilities(0U, TAG_IFD_ATR, &queried_length,
                             queried_atr.data()) == IFD_SUCCESS);
    CHECK(queried_length == 2U && queried_atr[0] == 0x3bU);

    std::array<UCHAR, 5U> apdu{0x90U, 0x30U, 0x00U, 0x00U, 0x00U};
    std::array<UCHAR, 16U> response{};
    DWORD response_length = static_cast<DWORD>(response.size());
    SCARD_IO_HEADER receive_pci{};
    const std::array<DWORD, 2U> invalid_transmit_protocols{
        0U, SCARD_PROTOCOL_T1};
    for (const DWORD protocol : invalid_transmit_protocols) {
        response_length = static_cast<DWORD>(response.size());
        const SCARD_IO_HEADER invalid_pci{protocol, sizeof(SCARD_IO_HEADER)};
        CHECK(IFDHTransmitToICC(0U, invalid_pci, apdu.data(),
                                static_cast<DWORD>(apdu.size()), response.data(),
                                &response_length, &receive_pci) ==
              IFD_PROTOCOL_NOT_SUPPORTED);
        CHECK(response_length == 0U);
    }
    const SCARD_IO_HEADER send_pci{kIfdTransmitProtocolT1,
                                   sizeof(SCARD_IO_HEADER)};
    response_length = static_cast<DWORD>(response.size());
    CHECK(IFDHTransmitToICC(0U, send_pci, apdu.data(),
                            static_cast<DWORD>(apdu.size()), response.data(),
                            &response_length, &receive_pci) == IFD_SUCCESS);
    CHECK(response_length == 2U && response[0] == 0x90U &&
          response[1] == 0x00U &&
          receive_pci.Protocol == kIfdTransmitProtocolT1);

    atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_RESET, atr.data(), &atr_length) == IFD_SUCCESS);
    backend.present.store(false);
    CHECK(IFDHICCPresence(0U) == IFD_ICC_NOT_PRESENT);
    response_length = static_cast<DWORD>(response.size());
    CHECK(IFDHTransmitToICC(0U, send_pci, apdu.data(),
                            static_cast<DWORD>(apdu.size()), response.data(),
                            &response_length, &receive_pci) == IFD_ICC_NOT_PRESENT);
    CHECK(response_length == 0U);
    backend.present.store(true);
    CHECK(IFDHICCPresence(0U) == IFD_ICC_PRESENT);
    atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_RESET, atr.data(), &atr_length) == IFD_SUCCESS);

    atr.fill(0xffU);
    atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_POWER_DOWN, atr.data(), &atr_length) == IFD_SUCCESS);
    CHECK(atr_length == 0U);
    for (const UCHAR value : atr) CHECK(value == 0U);
    CHECK(IFDHICCPresence(0U) == IFD_ICC_PRESENT);
    atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_POWER_UP, atr.data(), &atr_length) == IFD_SUCCESS);

    DWORD returned = 99U;
    CHECK(IFDHControl(0U, 0U, nullptr, 0U, nullptr, 0U, &returned) ==
          IFD_NOT_SUPPORTED);
    CHECK(returned == 0U);
    CHECK(IFDHICCPresence(1U) == IFD_NO_SUCH_DEVICE);
    CHECK(IFDHCloseChannel(0U) == IFD_SUCCESS);
    CHECK(IFDHCreateChannelByName(0U, device_name.data()) == IFD_SUCCESS);

    runner.stop_and_join();
    CHECK(runner.healthy());
    CHECK(server->shutdown());
    CHECK(!std::filesystem::exists(socket_path));
    CHECK(IFDHICCPresence(0U) == IFD_COMMUNICATION_ERROR);
    CHECK(IFDHCloseChannel(0U) == IFD_SUCCESS);
    return true;
}
#endif

#if defined(PX4_CORE_IFD)
namespace core_ifd {

namespace hlc = dtv::userland::host_listener_context;
namespace hr = dtv::userland::host_runtime;
namespace w = dtv::userland::wire;

template <std::size_t N>
w::WString<N> text(const char* value)
{
    w::WString<N> result;
    while (value[result.length] != '\0') {
        result.bytes[result.length] = value[result.length];
        ++result.length;
    }
    return result;
}

w::ListResponse make_list()
{
    w::ListResponse list;
    list.epoch[0] = 1U;
    list.producer.product = text<32U>("px4-userland");
    list.producer.version = text<64U>("1");
    list.topology.state = w::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = 1U;
    list.functions.count = 1U;
    auto& function = list.functions.items[0U];
    function.id = 1U;
    function.vid = 0x1234U;
    function.pid = 0x5678U;
    function.mode = w::Mode::runtime;
    function.serial_state = w::SerialState::absent;
    function.identification_reason = w::IdentificationReason::descriptor_unreadable;
    function.present = true;
    list.enclosure.present = true;
    list.enclosure.value.id = 0U;
    list.enclosure.value.identity.serial_state = w::IdentitySerialState::absent;
    list.receivers.present = true;
    list.receivers.value.count = 1U;
    auto& receiver = list.receivers.value.items[0U];
    receiver.id = 0U;
    receiver.systems.mask = 1U;
    receiver.binding.present = true;
    receiver.binding.value.function_id = 1U;
    receiver.binding.value.local_id = 0U;
    list.readers.present = true;
    list.readers.value.count = 1U;
    list.readers.value.items[0U].id = 0U;
    return list;
}

hlc::ContextConfig make_config(const std::string& root, const std::string& instance)
{
    hlc::ContextConfig value;
    value.runtime_directory = root;
    value.product = "px4-userland";
    value.instance = instance;
    value.access = hr::EndpointAccess::private_user;
    value.canonical_list = make_list();
    value.server.epoch = value.canonical_list.epoch;
    value.server.producer = value.canonical_list.producer;
    value.server.features = w::kFeatureStateEvent | w::kFeatureCardService;
    value.context.epoch = value.server.epoch;
    value.context.state = w::ContextState::ready;
    value.context.instance.present = true;
    value.context.instance.value.length = instance.size();
    std::memcpy(value.context.instance.value.bytes.data(), instance.data(),
                instance.size());
    value.stream_engine_epoch = 42U;
    value.stream_arm_budget_ms = 5000U;
    return value;
}

class Family final : public hr::EnclosureFamily {
public:
    Family(Backend& card, Session& session, NoopTunerBackend& tuner,
           NoopTunerTime& time) noexcept
        : card_(card), session_(session), tuner_(tuner), time_(time)
    {
    }
    px4::userland::TunerServiceBackend& tuner_backend() noexcept override { return tuner_; }
    px4::userland::CardServiceBackend& card_backend() noexcept override { return card_; }
    px4::userland::CardProtocolSession& card_protocol() noexcept override { return session_; }
    px4::userland::TunerServiceTime& time() noexcept override { return time_; }
    px4::userland::TunerStreamControl* stream_control() noexcept override { return nullptr; }
private:
    Backend& card_;
    Session& session_;
    NoopTunerBackend& tuner_;
    NoopTunerTime& time_;
};

class ServerRunner final {
public:
    explicit ServerRunner(hlc::EnclosureContext& context) noexcept : context_(context)
    {
        thread_ = std::thread([this]() {
            while (!stop_.load()) {
                if (!context_.poll_once(Timeout{10U})) {
                    healthy_.store(false);
                    return;
                }
            }
        });
    }
    ~ServerRunner() noexcept { stop_and_join(); }
    void stop_and_join() noexcept
    {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
    }
    bool healthy() const noexcept { return healthy_.load(); }
private:
    hlc::EnclosureContext& context_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> healthy_{true};
    std::thread thread_;
};

bool test_exported_abi_over_core_listener()
{
    TempRuntime runtime;
    CHECK(runtime.valid());
    constexpr const char* serial = "00001205000960";
    Backend backend;
    Session session;
    session.bind(backend);
    NoopTunerBackend tuner_backend;
    NoopTunerTime tuner_time;
    auto family = std::unique_ptr<hr::EnclosureFamily>(
        new Family(backend, session, tuner_backend, tuner_time));
    auto assembled = hlc::EnclosureContext::assemble(
        std::move(family), std::unique_ptr<hr::TunerNonceSource>(new NoopTunerNonce()),
        make_config(runtime.path(), serial));
    CHECK(assembled);
    auto context = std::move(assembled.value());
    CHECK(context->reserve_namespace());
    CHECK(context->publish());
    ServerRunner runner(*context);

    std::string device_name = "px4-userland:runtime=" + runtime.path() +
                              ":device=" + serial + ":access=user";
    CHECK(IFDHCreateChannel(0U, 0U) == IFD_NOT_SUPPORTED);
    CHECK(IFDHCreateChannelByName(1U, device_name.data()) == IFD_NO_SUCH_DEVICE);
    CHECK(IFDHCreateChannelByName(0U, device_name.data()) == IFD_SUCCESS);
    CHECK(IFDHICCPresence(0U) == IFD_ICC_PRESENT);

    std::array<UCHAR, MAX_ATR_SIZE> atr{};
    DWORD atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_POWER_UP, atr.data(), &atr_length) == IFD_SUCCESS);
    CHECK(atr_length == 2U && atr[0] == 0x3bU);
    CHECK(IFDHSetProtocolParameters(0U, SCARD_PROTOCOL_T1, 0U, 0U, 0U, 0U) ==
          IFD_SUCCESS);

    atr.fill(0U);
    atr_length = 0U;
    CHECK(IFDHPowerICC(0U, IFD_RESET, atr.data(), &atr_length) == IFD_SUCCESS);
    CHECK(atr_length == 2U && atr[0] == 0x3bU && atr[1] == 0x00U);

    DWORD queried_length = 0U;
    CHECK(IFDHGetCapabilities(0U, TAG_IFD_ATR, &queried_length, nullptr) ==
          IFD_ERROR_INSUFFICIENT_BUFFER);
    CHECK(queried_length == 2U);
    std::array<UCHAR, MAX_ATR_SIZE> queried_atr{};
    queried_length = static_cast<DWORD>(queried_atr.size());
    CHECK(IFDHGetCapabilities(0U, TAG_IFD_ATR, &queried_length,
                             queried_atr.data()) == IFD_SUCCESS);
    CHECK(queried_length == 2U && queried_atr[0] == 0x3bU);

    std::array<UCHAR, 5U> apdu{0x90U, 0x30U, 0x00U, 0x00U, 0x00U};
    std::array<UCHAR, 16U> response{};
    DWORD response_length = static_cast<DWORD>(response.size());
    SCARD_IO_HEADER receive_pci{};
    const std::array<DWORD, 2U> invalid_transmit_protocols{
        0U, SCARD_PROTOCOL_T1};
    for (const DWORD protocol : invalid_transmit_protocols) {
        response_length = static_cast<DWORD>(response.size());
        const SCARD_IO_HEADER invalid_pci{protocol, sizeof(SCARD_IO_HEADER)};
        CHECK(IFDHTransmitToICC(0U, invalid_pci, apdu.data(),
                                static_cast<DWORD>(apdu.size()), response.data(),
                                &response_length, &receive_pci) ==
              IFD_PROTOCOL_NOT_SUPPORTED);
        CHECK(response_length == 0U);
    }
    const SCARD_IO_HEADER send_pci{kIfdTransmitProtocolT1,
                                   sizeof(SCARD_IO_HEADER)};
    response_length = static_cast<DWORD>(response.size());
    CHECK(IFDHTransmitToICC(0U, send_pci, apdu.data(),
                            static_cast<DWORD>(apdu.size()), response.data(),
                            &response_length, &receive_pci) == IFD_SUCCESS);
    CHECK(response_length == 2U && response[0] == 0x90U &&
          response[1] == 0x00U &&
          receive_pci.Protocol == kIfdTransmitProtocolT1);

    atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_RESET, atr.data(), &atr_length) == IFD_SUCCESS);
    backend.present.store(false);
    CHECK(IFDHICCPresence(0U) == IFD_ICC_NOT_PRESENT);
    response_length = static_cast<DWORD>(response.size());
    CHECK(IFDHTransmitToICC(0U, send_pci, apdu.data(),
                            static_cast<DWORD>(apdu.size()), response.data(),
                            &response_length, &receive_pci) == IFD_ICC_NOT_PRESENT);
    CHECK(response_length == 0U);
    backend.present.store(true);
    CHECK(IFDHICCPresence(0U) == IFD_ICC_PRESENT);
    // Core CardService invalidates the old card handle across removal. The
    // IFD close may therefore report no-such-device; it still invalidates the
    // channel, after which a fresh channel rebinds the reinserted card.
    const RESPONSECODE close_after_removal = IFDHCloseChannel(0U);
    CHECK(close_after_removal == IFD_SUCCESS ||
          close_after_removal == IFD_NO_SUCH_DEVICE);
    CHECK(IFDHCreateChannelByName(0U, device_name.data()) == IFD_SUCCESS);
    atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_RESET, atr.data(), &atr_length) == IFD_SUCCESS);

    atr.fill(0xffU);
    atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_POWER_DOWN, atr.data(), &atr_length) == IFD_SUCCESS);
    CHECK(atr_length == 0U);
    for (const UCHAR value : atr) CHECK(value == 0U);
    CHECK(IFDHICCPresence(0U) == IFD_ICC_PRESENT);
    atr_length = static_cast<DWORD>(atr.size());
    CHECK(IFDHPowerICC(0U, IFD_POWER_UP, atr.data(), &atr_length) == IFD_SUCCESS);

    DWORD returned = 99U;
    CHECK(IFDHControl(0U, 0U, nullptr, 0U, nullptr, 0U, &returned) ==
          IFD_NOT_SUPPORTED);
    CHECK(returned == 0U);
    CHECK(IFDHICCPresence(1U) == IFD_NO_SUCH_DEVICE);
    CHECK(IFDHCloseChannel(0U) == IFD_SUCCESS);
    CHECK(IFDHCreateChannelByName(0U, device_name.data()) == IFD_SUCCESS);

    runner.stop_and_join();
    CHECK(runner.healthy());
    const auto closed = context->close(Timeout{5000U});
    CHECK(closed && closed.value() == hlc::CloseState::quiesced);
    CHECK(IFDHICCPresence(0U) == IFD_COMMUNICATION_ERROR);
    CHECK(IFDHCloseChannel(0U) == IFD_SUCCESS);
    return true;
}

}  // namespace core_ifd
#endif

}  // namespace

int main()
{
    if (!test_reader_config_template() ||
        !test_device_name_and_error_mapping() ||
        !test_adapter_state_and_recovery() ||
        !test_adapter_lazy_reconnect() ||
        !test_adapter_timeout_and_buffer_errors()
#if defined(PX4_CORE_IFD)
        || !core_ifd::test_exported_abi_over_core_listener()
#else
        || !test_exported_abi_over_real_ipc()
#endif
        ) {
        return 1;
    }
    std::printf("PASS pcsc_ifd\n");
    return 0;
}

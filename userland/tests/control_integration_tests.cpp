// SPDX-License-Identifier: GPL-2.0-only
#include "px4/card_service.h"
#include "px4/control_client.h"
#include "px4/control_server.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;
using namespace px4::userland::ipc::posix;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,       \
                         #condition);                                                       \
            return false;                                                                   \
        }                                                                                   \
    } while (false)

class TempRuntime final {
public:
    TempRuntime()
    {
        std::array<char, 64U> pattern{};
        const char* value = "/tmp/px4-control-XXXXXX";
        std::memcpy(pattern.data(), value, std::strlen(value) + 1U);
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
        power_calls.push_back(on);
        powered = on;
        return Result<void>::success();
    }
    Result<void> initialize_uart() noexcept override
    {
        ++uart_calls;
        return Result<void>::success();
    }
    Result<bool> detect_card() noexcept override
    {
        return Result<bool>::success(present.load());
    }
    std::vector<bool> power_calls;
    std::size_t uart_calls = 0U;
    bool powered = false;
    std::atomic<bool> present{true};
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
        initialized_ = true;
        ++initialize_calls;
        return Result<void>::success();
    }
    Result<std::size_t> transmit(ByteView apdu, MutableByteView response) noexcept override
    {
        ++transmit_calls;
        if (!initialized_ || apdu.size == 0U || response.size < 2U) {
            return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
        }
        response.data[0] = 0x61U;
        response.data[1] = 0x00U;
        return Result<std::size_t>::success(2U);
    }
    bool initialized() const noexcept override { return initialized_; }
    const CardAtr& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override { initialized_ = false; }
    std::size_t initialize_calls = 0U;
    std::size_t transmit_calls = 0U;
private:
    CardAtr atr_{};
    bool initialized_ = false;
};

class EventSink final : public ControlEventSink {
public:
    void on_device_event(const DeviceEventPayload& event) noexcept override
    {
        last = event;
        ++count;
    }
    DeviceEventPayload last{};
    std::size_t count = 0U;
};

template <typename Payload>
Result<ControlResponse> request(PosixControlClient& client, MessageType type,
                                const Payload& payload) noexcept
{
    std::array<std::uint8_t, kMaxControlPayload> encoded{};
    const auto size = encode_payload(payload,
        MutableByteView{encoded.data(), encoded.size()});
    if (!size) return Result<ControlResponse>::failure(size.error());
    return client.request(type, ByteView{encoded.data(), size.value()}, Timeout{2000U});
}

Result<ControlResponse> empty_request(PosixControlClient& client,
                                      MessageType type,
                                      ControlEventSink* sink = nullptr) noexcept
{
    return client.request(type, ByteView{nullptr, 0U}, Timeout{2000U}, sink);
}

bool test_control_server_card_flow_and_shutdown()
{
    TempRuntime runtime;
    CHECK(runtime.valid());
    constexpr const char* serial = "00001205000960";
    const EndpointConfig endpoint{runtime.path().c_str(), serial, kControlEndpointName,
                                  EndpointAccess::private_user};
    Backend backend;
    Session session;
    CardService service(backend, session);
    auto server_result = PosixControlServer::create(endpoint, service, serial);
    CHECK(server_result);
    std::unique_ptr<PosixControlServer> server = std::move(server_result.value());
    const std::string socket_path = server->endpoint_path();

    std::atomic<bool> stop{false};
    std::atomic<bool> server_ok{true};
    std::thread server_thread([&]() {
        while (!stop.load()) {
            const auto polled = server->poll_once(Timeout{20U});
            if (!polled) {
                server_ok.store(false);
                return;
            }
        }
    });

    auto first_result = PosixControlClient::connect(
        endpoint, kCapabilityCard | kCapabilityEvents, Timeout{2000U});
    auto second_result = PosixControlClient::connect(
        endpoint, kCapabilityCard, Timeout{2000U});
    CHECK(first_result && second_result);
    std::unique_ptr<PosixControlClient> first = std::move(first_result.value());
    std::unique_ptr<PosixControlClient> second = std::move(second_result.value());
    CHECK((first->negotiated_capabilities() &
           (kCapabilityCard | kCapabilityEvents)) ==
          (kCapabilityCard | kCapabilityEvents));

    const auto listed = empty_request(*first, MessageType::LIST);
    CHECK(listed);
    const auto list = decode_list_response_payload(
        ByteView{listed.value().payload.data(), listed.value().payload.size()});
    CHECK(list && list.value().receivers.size() == 8U && list.value().ready == 1U);
    CHECK(list.value().receivers[0].system == System::ISDB_S);
    CHECK(list.value().receivers[7].global_id == 7U);

    const auto daemon_status = empty_request(*first, MessageType::STATUS);
    CHECK(daemon_status);
    const auto status = decode_status_response_payload(
        ByteView{daemon_status.value().payload.data(),
                 daemon_status.value().payload.size()});
    CHECK(status && status.value().receiver_states[0] == ReceiverState::free &&
          status.value().receiver_states[7] == ReceiverState::free);
    const auto initial_card_status = empty_request(*first, MessageType::CARD_STATUS);
    CHECK(initial_card_status);
    const auto card_status_payload = decode_card_status_response_payload(
        ByteView{initial_card_status.value().payload.data(),
                 initial_card_status.value().payload.size()});
    CHECK(card_status_payload && card_status_payload.value().present == 1U &&
          card_status_payload.value().initialized == 0U);

    const auto opened1 = request(*first, MessageType::CARD_CONNECT,
                                 CardConnectRequestPayload{ShareMode::shared});
    const auto opened2 = request(*second, MessageType::CARD_CONNECT,
                                 CardConnectRequestPayload{ShareMode::shared});
    CHECK(opened1 && opened2);
    const auto card1 = decode_card_connect_response_payload(
        ByteView{opened1.value().payload.data(), opened1.value().payload.size()});
    const auto card2 = decode_card_connect_response_payload(
        ByteView{opened2.value().payload.data(), opened2.value().payload.size()});
    CHECK(card1 && card2 && card1.value().card_handle != card2.value().card_handle);

    const auto reconnected = request(
        *second, MessageType::CARD_RECONNECT,
        CardReconnectRequestPayload{card2.value().card_handle,
                                    ShareMode::shared, Disposition::leave});
    CHECK(reconnected);
    const auto reconnect_atr = decode_atr_payload(
        ByteView{reconnected.value().payload.data(), reconnected.value().payload.size()});
    CHECK(reconnect_atr && reconnect_atr.value().atr.size == 2U);

    const auto reset = request(*first, MessageType::CARD_RESET,
                               CardHandleRequestPayload{card1.value().card_handle});
    CHECK(reset);
    const auto reset_atr = decode_atr_payload(
        ByteView{reset.value().payload.data(), reset.value().payload.size()});
    CHECK(reset_atr && reset_atr.value().atr.size == 2U);

    CHECK(request(*first, MessageType::BEGIN_TRANSACTION,
                  CardHandleRequestPayload{card1.value().card_handle}));
    const std::array<std::uint8_t, 2U> apdu{0x00U, 0x84U};
    const auto busy = request(
        *second, MessageType::CARD_TRANSMIT,
        CardTransmitRequestPayload{card2.value().card_handle,
                                   ByteView{apdu.data(), apdu.size()}});
    CHECK(!busy && busy.error() == Error::BUSY);
    CHECK(request(*first, MessageType::END_TRANSACTION,
                  CardDispositionRequestPayload{card1.value().card_handle,
                                                Disposition::leave}));

    const auto transmitted = request(
        *first, MessageType::CARD_TRANSMIT,
        CardTransmitRequestPayload{card1.value().card_handle,
                                   ByteView{apdu.data(), apdu.size()}});
    CHECK(transmitted);
    const auto response = decode_card_transmit_response_payload(
        ByteView{transmitted.value().payload.data(), transmitted.value().payload.size()});
    CHECK(response && response.value().response.size == 2U);

    const auto unsupported = request(*first, MessageType::ACQUIRE,
                                     AcquireRequestPayload{0U});
    CHECK(!unsupported && unsupported.error() == Error::UNSUPPORTED);

    CHECK(request(*second, MessageType::CARD_DISCONNECT,
                  CardDispositionRequestPayload{card2.value().card_handle,
                                                Disposition::leave}));
    CHECK(service.handle_count() == 1U && service.powered());

    backend.present = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(300U));
    EventSink sink;
    const auto card_status = empty_request(*first, MessageType::CARD_STATUS, &sink);
    CHECK(card_status);
    CHECK(sink.count == 1U && sink.last.kind == DeviceEventKind::card_removed &&
          sink.last.target_type == EventTargetType::card);

    first->close();
    second->close();
    std::this_thread::sleep_for(std::chrono::milliseconds(100U));
    stop.store(true);
    server_thread.join();
    CHECK(server_ok.load());
    CHECK(service.handle_count() == 0U && !service.powered());
    CHECK(server->shutdown());
    CHECK(!std::filesystem::exists(socket_path));
    return true;
}

bool test_no_hello_timeout_and_malformed_disconnect()
{
    TempRuntime runtime;
    CHECK(runtime.valid());
    constexpr const char* serial = "00001205000961";
    const EndpointConfig endpoint{runtime.path().c_str(), serial, kControlEndpointName,
                                  EndpointAccess::private_user};
    Backend backend;
    Session session;
    CardService service(backend, session);
    auto server_result = PosixControlServer::create(endpoint, service, serial);
    CHECK(server_result);
    std::unique_ptr<PosixControlServer> server = std::move(server_result.value());

    std::atomic<bool> stop{false};
    std::thread server_thread([&]() {
        while (!stop.load()) (void)server->poll_once(Timeout{10U});
    });
    auto raw = SocketStream::connect(endpoint, Timeout{1000U});
    CHECK(raw);
    std::array<std::uint8_t, 8U> buffer{};
    const auto timeout = raw.value().read_some(
        MutableByteView{buffer.data(), buffer.size()}, Timeout{20U});
    CHECK(!timeout && timeout.error() == Error::TIMEOUT);

    const std::array<std::uint8_t, kFrameHeaderSize> malformed{};
    const ssize_t sent = ::send(raw.value().native_handle(), malformed.data(),
                                malformed.size(), 0);
    CHECK(sent == static_cast<ssize_t>(malformed.size()));
    Result<std::size_t> disconnected = Result<std::size_t>::failure(Error::TIMEOUT);
    for (std::size_t attempt = 0U; attempt < 100U; ++attempt) {
        disconnected = raw.value().read_some(
            MutableByteView{buffer.data(), buffer.size()}, Timeout{20U});
        if (!disconnected && disconnected.error() == Error::DISCONNECTED) break;
    }
    CHECK(!disconnected && disconnected.error() == Error::DISCONNECTED);
    stop.store(true);
    server_thread.join();
    CHECK(server->shutdown());
    return true;
}

}  // namespace

bool run_control_integration_tests()
{
    return test_control_server_card_flow_and_shutdown() &&
           test_no_hello_timeout_and_malformed_disconnect();
}

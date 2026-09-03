// SPDX-License-Identifier: GPL-2.0-only
#include "px4/control_server.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <poll.h>
#include <string>
#include <utility>
#include <vector>

namespace px4::userland::ipc::posix {
namespace {

constexpr std::uint32_t kServerCapabilities =
    kCapabilityEvents | kCapabilityCard | kCapabilityStreamStats;
constexpr std::uint32_t kClientWriteTimeoutMs = 250U;
constexpr std::uint32_t kPresencePollIntervalMs = 250U;
constexpr std::size_t kMaxControlConnections = 32U;

ErrorCode ipc_error(Error error) noexcept
{
    switch (error) {
    case Error::OK: return ErrorCode::OK;
    case Error::INVALID_ARGUMENT: return ErrorCode::INVALID_ARGUMENT;
    case Error::VERSION_MISMATCH: return ErrorCode::VERSION_MISMATCH;
    case Error::NOT_FOUND: return ErrorCode::NOT_FOUND;
    case Error::BUSY: return ErrorCode::BUSY;
    case Error::NOT_READY: return ErrorCode::NOT_READY;
    case Error::TIMEOUT: return ErrorCode::TIMEOUT;
    case Error::USB_IO: return ErrorCode::USB_IO;
    case Error::DISCONNECTED: return ErrorCode::DISCONNECTED;
    case Error::PROTOCOL_ERROR: return ErrorCode::PROTOCOL_ERROR;
    case Error::FIRMWARE_REJECTED: return ErrorCode::FIRMWARE_REJECTED;
    case Error::UNSUPPORTED: return ErrorCode::UNSUPPORTED;
    case Error::NO_CARD: return ErrorCode::NO_CARD;
    case Error::CARD_REMOVED: return ErrorCode::CARD_REMOVED;
    case Error::BUFFER_TOO_SMALL: return ErrorCode::BUFFER_TOO_SMALL;
    case Error::SLOW_CONSUMER: return ErrorCode::SLOW_CONSUMER;
    case Error::INTERNAL: return ErrorCode::INTERNAL;
    }
    return ErrorCode::INTERNAL;
}

std::array<ReceiverRecord, kReceiverCount> receiver_records() noexcept
{
    return std::array<ReceiverRecord, kReceiverCount>{
        ReceiverRecord{0U, 1U, 0U, System::ISDB_S},
        ReceiverRecord{1U, 1U, 1U, System::ISDB_S},
        ReceiverRecord{2U, 1U, 2U, System::ISDB_T},
        ReceiverRecord{3U, 1U, 3U, System::ISDB_T},
        ReceiverRecord{4U, 2U, 0U, System::ISDB_S},
        ReceiverRecord{5U, 2U, 1U, System::ISDB_S},
        ReceiverRecord{6U, 2U, 2U, System::ISDB_T},
        ReceiverRecord{7U, 2U, 3U, System::ISDB_T}};
}

int timeout_to_poll(Timeout timeout) noexcept
{
    return timeout.milliseconds > static_cast<std::uint32_t>(INT_MAX) ?
               INT_MAX : static_cast<int>(timeout.milliseconds);
}

}  // namespace

struct PosixControlServer::Impl final {
    class Client final : public FrameConsumer {
    public:
        Client(Impl& owner, CardClientId id, SocketStream stream) noexcept
            : owner_(owner), id_(id), stream_(std::move(stream)),
              framer_(MutableByteView{framing_storage_.data(), framing_storage_.size()}),
              state_(ConnectionRole::control_server, kServerCapabilities)
        {
        }

        Result<void> on_frame(const FrameView& frame) noexcept override
        {
            return owner_.on_frame(*this, frame);
        }

        template <typename Payload>
        Result<void> send_payload(MessageType type, MessageKind kind,
                                  std::uint32_t request_id,
                                  const Payload& payload) noexcept
        {
            const auto payload_size = encode_payload(
                payload, MutableByteView{payload_buffer_.data(), payload_buffer_.size()});
            if (!payload_size) {
                return Result<void>::failure(payload_size.error());
            }
            return send_encoded(type, kind, request_id, payload_size.value());
        }

        Result<void> send_empty(MessageType type, std::uint32_t request_id) noexcept
        {
            return send_encoded(type, MessageKind::response, request_id, 0U);
        }

        Result<void> send_error(MessageType type, std::uint32_t request_id,
                                Error error) noexcept
        {
            const char* detail = error_string(error);
            return send_payload(
                type, MessageKind::error_response, request_id,
                ErrorResponsePayload{ipc_error(error),
                                     ByteView{reinterpret_cast<const std::uint8_t*>(detail),
                                              std::strlen(detail)}});
        }

        Result<void> send_event(const DeviceEventPayload& event) noexcept
        {
            return send_payload(MessageType::DEVICE_EVENT, MessageKind::event, 0U, event);
        }

        bool events_enabled() const noexcept
        {
            return state_.phase() == ConnectionPhase::active &&
                   (state_.negotiated_capabilities() & kCapabilityEvents) != 0U;
        }

        Result<std::size_t> read_ready() noexcept
        {
            return stream_.read_frames(
                MutableByteView{read_buffer_.data(), read_buffer_.size()},
                framer_, *this, Timeout{0U});
        }

        CardClientId id() const noexcept { return id_; }
        int fd() const noexcept { return stream_.native_handle(); }
        bool closed() const noexcept
        {
            return state_.closed() || state_.poisoned() || !stream_.valid();
        }
        void close() noexcept { stream_.close(); }

        ConnectionStateMachine& state() noexcept { return state_; }

    private:
        Result<void> send_encoded(MessageType type, MessageKind kind,
                                  std::uint32_t request_id,
                                  std::size_t payload_size) noexcept
        {
            const FrameHeader header{
                kProtocolMajor, kProtocolMinor, type, kind, request_id,
                static_cast<std::uint32_t>(payload_size)};
            const auto frame_size = encode_frame(
                header, ByteView{payload_buffer_.data(), payload_size},
                MutableByteView{frame_buffer_.data(), frame_buffer_.size()});
            if (!frame_size) {
                return Result<void>::failure(frame_size.error());
            }
            const auto decoded = decode_frame(
                ByteView{frame_buffer_.data(), frame_size.value()});
            if (!decoded) {
                return Result<void>::failure(decoded.error());
            }
            const auto accepted = state_.process_outbound(decoded.value());
            if (!accepted) {
                return Result<void>::failure(accepted.error());
            }
            return stream_.write_frame(
                ByteView{frame_buffer_.data(), frame_size.value()},
                Timeout{kClientWriteTimeoutMs});
        }

        Impl& owner_;
        CardClientId id_;
        SocketStream stream_;
        std::array<std::uint8_t, kFrameHeaderSize + kMaxControlPayload> framing_storage_{};
        std::array<std::uint8_t, 8192U> read_buffer_{};
        std::array<std::uint8_t, kMaxControlPayload> payload_buffer_{};
        std::array<std::uint8_t, kFrameHeaderSize + kMaxControlPayload> frame_buffer_{};
        StreamFramer framer_;
        ConnectionStateMachine state_;
    };

    Impl(SocketListener listener_value, CardService& service,
         std::string_view serial, bool ready_value,
         std::uint8_t usb_mask) noexcept
        : listener(std::move(listener_value)), card_service(service), base_serial(serial),
          ready(ready_value), usb_present_mask(usb_mask),
          next_presence_poll(std::chrono::steady_clock::now())
    {
    }

    Result<void> send_service_error(Client& client, const FrameView& request,
                                    Error error) noexcept
    {
        return client.send_error(request.header.type, request.header.request_id, error);
    }

    Result<void> send_empty(Client& client, const FrameView& request) noexcept
    {
        return client.send_empty(request.header.type, request.header.request_id);
    }

    template <typename Payload>
    Result<void> send_success(Client& client, const FrameView& request,
                              const Payload& payload) noexcept
    {
        return client.send_payload(request.header.type, MessageKind::response,
                                   request.header.request_id, payload);
    }

    Result<void> dispatch(Client& client, const FrameView& request) noexcept
    {
        switch (request.header.type) {
        case MessageType::LIST: {
            const auto empty = decode_empty_payload(request.payload);
            if (!empty) return Result<void>::failure(Error::PROTOCOL_ERROR);
            const auto receivers = receiver_records();
            return send_success(
                client, request,
                ListResponsePayload{
                    1U,
                    ByteView{reinterpret_cast<const std::uint8_t*>(base_serial.data()),
                             base_serial.size()},
                    static_cast<std::uint8_t>(ready ? 1U : 0U), usb_present_mask,
                    receivers});
        }
        case MessageType::STATUS: {
            const auto status = card_service.status();
            if (!status) return send_service_error(client, request, status.error());
            std::array<ReceiverState, kReceiverCount> states{};
            states.fill(ReceiverState::free);
            return send_success(
                client, request,
                StatusResponsePayload{
                    1U, static_cast<std::uint8_t>(ready ? 1U : 0U), usb_present_mask,
                    static_cast<std::uint8_t>(status.value().present ? 1U : 0U),
                    static_cast<std::uint8_t>(status.value().initialized ? 1U : 0U),
                    states, 0U, 0U});
        }
        case MessageType::CARD_STATUS: {
            const auto status = card_service.status();
            if (!status) return send_service_error(client, request, status.error());
            return send_success(
                client, request,
                CardStatusResponsePayload{
                    static_cast<std::uint8_t>(status.value().present ? 1U : 0U),
                    static_cast<std::uint8_t>(status.value().initialized ? 1U : 0U),
                    status.value().reader_generation, status.value().atr.view()});
        }
        case MessageType::CARD_CONNECT: {
            const auto decoded = decode_card_connect_request_payload(request.payload);
            if (!decoded) return Result<void>::failure(Error::PROTOCOL_ERROR);
            const auto connected = card_service.connect(client.id(), decoded.value().share_mode);
            if (!connected) return send_service_error(client, request, connected.error());
            return send_success(
                client, request,
                CardConnectResponsePayload{connected.value().handle,
                                           connected.value().atr.view()});
        }
        case MessageType::CARD_RECONNECT: {
            const auto decoded = decode_card_reconnect_request_payload(request.payload);
            if (!decoded) return Result<void>::failure(Error::PROTOCOL_ERROR);
            const auto atr = card_service.reconnect(
                client.id(), decoded.value().card_handle, decoded.value().share_mode,
                decoded.value().disposition);
            if (!atr) return send_service_error(client, request, atr.error());
            return send_success(client, request, AtrPayload{atr.value().view()});
        }
        case MessageType::CARD_DISCONNECT: {
            const auto decoded = decode_card_disposition_request_payload(request.payload);
            if (!decoded) return Result<void>::failure(Error::PROTOCOL_ERROR);
            const auto result = card_service.disconnect(
                client.id(), decoded.value().card_handle, decoded.value().disposition);
            return result ? send_empty(client, request) :
                            send_service_error(client, request, result.error());
        }
        case MessageType::CARD_RESET: {
            const auto decoded = decode_card_handle_request_payload(request.payload);
            if (!decoded) return Result<void>::failure(Error::PROTOCOL_ERROR);
            const auto atr = card_service.reset(client.id(), decoded.value().card_handle);
            if (!atr) return send_service_error(client, request, atr.error());
            return send_success(client, request, AtrPayload{atr.value().view()});
        }
        case MessageType::CARD_TRANSMIT: {
            const auto decoded = decode_card_transmit_request_payload(request.payload);
            if (!decoded) return Result<void>::failure(Error::PROTOCOL_ERROR);
            std::array<std::uint8_t, kMaxCardPayload> response{};
            const auto transmitted = card_service.transmit(
                client.id(), decoded.value().card_handle, decoded.value().apdu,
                MutableByteView{response.data(), response.size()});
            if (!transmitted) {
                return send_service_error(client, request, transmitted.error());
            }
            return send_success(
                client, request,
                CardTransmitResponsePayload{
                    ByteView{response.data(), transmitted.value()}});
        }
        case MessageType::BEGIN_TRANSACTION: {
            const auto decoded = decode_card_handle_request_payload(request.payload);
            if (!decoded) return Result<void>::failure(Error::PROTOCOL_ERROR);
            const auto result = card_service.begin_transaction(
                client.id(), decoded.value().card_handle);
            return result ? send_empty(client, request) :
                            send_service_error(client, request, result.error());
        }
        case MessageType::END_TRANSACTION: {
            const auto decoded = decode_card_disposition_request_payload(request.payload);
            if (!decoded) return Result<void>::failure(Error::PROTOCOL_ERROR);
            const auto result = card_service.end_transaction(
                client.id(), decoded.value().card_handle, decoded.value().disposition);
            return result ? send_empty(client, request) :
                            send_service_error(client, request, result.error());
        }
        case MessageType::ACQUIRE:
        case MessageType::RELEASE:
        case MessageType::TUNE:
        case MessageType::START_STREAM:
        case MessageType::STOP_STREAM:
        case MessageType::STATS:
            return send_service_error(client, request, Error::UNSUPPORTED);
        case MessageType::HELLO:
        case MessageType::ATTACH_STREAM:
        case MessageType::TS_DATA:
        case MessageType::DEVICE_EVENT:
        case MessageType::STREAM_END:
            return Result<void>::failure(Error::PROTOCOL_ERROR);
        }
        return Result<void>::failure(Error::PROTOCOL_ERROR);
    }

    Result<void> on_frame(Client& client, const FrameView& frame) noexcept
    {
        const auto accepted = client.state().process_inbound(frame);
        if (!accepted) {
            return Result<void>::failure(accepted.error());
        }
        switch (accepted.value().event) {
        case ConnectionEvent::hello_response_required: {
            const auto hello = client.state().pending_hello_response();
            if (!hello) return Result<void>::failure(hello.error());
            return client.send_payload(MessageType::HELLO, MessageKind::response,
                                       frame.header.request_id, hello.value());
        }
        case ConnectionEvent::version_mismatch_response_required:
            return client.send_error(MessageType::HELLO, frame.header.request_id,
                                     Error::VERSION_MISMATCH);
        case ConnectionEvent::request_accepted:
            return dispatch(client, frame);
        case ConnectionEvent::response_accepted:
        case ConnectionEvent::event_accepted:
        case ConnectionEvent::attach_response_required:
        case ConnectionEvent::ts_data_accepted:
        case ConnectionEvent::stream_end_accepted:
            return Result<void>::failure(Error::PROTOCOL_ERROR);
        }
        return Result<void>::failure(Error::PROTOCOL_ERROR);
    }

    void remove_client(std::size_t index) noexcept
    {
        if (index >= clients.size()) return;
        const CardClientId id = clients[index]->id();
        clients[index]->close();
        const auto released = card_service.release_connection(id);
        if (!released && cleanup_error == Error::OK) cleanup_error = released.error();
        clients.erase(clients.begin() + static_cast<std::ptrdiff_t>(index));
    }

    Result<void> accept_ready() noexcept
    {
        while (true) {
            auto accepted = listener.accept(Timeout{0U});
            if (!accepted) {
                return accepted.error() == Error::TIMEOUT ? Result<void>::success() :
                                                            Result<void>::failure(accepted.error());
            }
            if (clients.size() >= kMaxControlConnections) {
                accepted.value().close();
                continue;
            }
            const CardClientId id = next_client_id++;
            if (next_client_id == 0U) next_client_id = 1U;
            clients.emplace_back(new Client(*this, id, std::move(accepted.value())));
        }
    }

    void broadcast_presence(const CardPresenceChange& change) noexcept
    {
        if (!change.changed) return;
        const DeviceEventPayload event{
            change.reader_generation,
            change.present ? DeviceEventKind::card_inserted : DeviceEventKind::card_removed,
            EventTargetType::card, 0U};
        for (std::size_t index = clients.size(); index > 0U; --index) {
            Client& client = *clients[index - 1U];
            if (client.events_enabled() && !client.send_event(event)) {
                remove_client(index - 1U);
            }
        }
    }

    SocketListener listener;
    CardService& card_service;
    std::string base_serial;
    std::vector<std::unique_ptr<Client>> clients;
    CardClientId next_client_id = 1U;
    bool ready;
    std::uint8_t usb_present_mask;
    bool shutdown_complete = false;
    Error cleanup_error = Error::OK;
    std::chrono::steady_clock::time_point next_presence_poll;
};

Result<std::unique_ptr<PosixControlServer>> PosixControlServer::create(
    const EndpointConfig& endpoint, CardService& card_service,
    std::string_view base_serial, bool ready,
    std::uint8_t usb_present_mask) noexcept
{
    if (base_serial.empty() || base_serial.size() > 0xffffU ||
        (usb_present_mask & 0xfcU) != 0U) {
        return Result<std::unique_ptr<PosixControlServer>>::failure(
            Error::INVALID_ARGUMENT);
    }
    auto listener = SocketListener::listen(endpoint);
    if (!listener) {
        return Result<std::unique_ptr<PosixControlServer>>::failure(listener.error());
    }
    auto impl = std::unique_ptr<Impl>(new Impl(
        std::move(listener.value()), card_service, base_serial, ready,
        usb_present_mask));
    return Result<std::unique_ptr<PosixControlServer>>::success(
        std::unique_ptr<PosixControlServer>(new PosixControlServer(std::move(impl))));
}

PosixControlServer::PosixControlServer(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

PosixControlServer::~PosixControlServer() noexcept
{
    (void)shutdown();
}

Result<void> PosixControlServer::poll_once(Timeout timeout) noexcept
{
    if (!impl_ || impl_->shutdown_complete) {
        return Result<void>::failure(Error::NOT_READY);
    }
    std::vector<pollfd> descriptors;
    descriptors.reserve(impl_->clients.size() + 1U);
    descriptors.push_back(pollfd{impl_->listener.native_handle(), POLLIN, 0});
    for (const auto& client : impl_->clients) {
        descriptors.push_back(pollfd{client->fd(), POLLIN, 0});
    }

    const auto now = std::chrono::steady_clock::now();
    std::uint32_t wait_ms = timeout.milliseconds;
    if (impl_->card_service.has_handles()) {
        if (now >= impl_->next_presence_poll) {
            wait_ms = 0U;
        } else {
            const auto until_poll = std::chrono::duration_cast<std::chrono::milliseconds>(
                impl_->next_presence_poll - now).count();
            wait_ms = std::min<std::uint32_t>(
                wait_ms, static_cast<std::uint32_t>(std::max<std::int64_t>(0, until_poll)));
        }
    }

    const int result = ::poll(descriptors.data(), descriptors.size(),
                              timeout_to_poll(Timeout{wait_ms}));
    if (result < 0) {
        return errno == EINTR ? Result<void>::success() :
                               Result<void>::failure(Error::INTERNAL);
    }

    if (!descriptors.empty() && (descriptors[0].revents & POLLIN) != 0) {
        const auto accepted = impl_->accept_ready();
        if (!accepted) return accepted;
    }
    for (std::size_t index = descriptors.size(); index > 1U; --index) {
        const std::size_t client_index = index - 2U;
        const short events = descriptors[index - 1U].revents;
        if (events == 0 || client_index >= impl_->clients.size()) continue;
        bool remove = (events & POLLNVAL) != 0;
        if (!remove && (events & (POLLIN | POLLHUP | POLLERR)) != 0) {
            const auto read = impl_->clients[client_index]->read_ready();
            remove = !read || impl_->clients[client_index]->closed();
        }
        if (remove) {
            impl_->remove_client(client_index);
        }
    }

    const auto after_io = std::chrono::steady_clock::now();
    if (impl_->card_service.has_handles() && after_io >= impl_->next_presence_poll) {
        const auto presence = impl_->card_service.poll_presence();
        if (!presence) return Result<void>::failure(presence.error());
        impl_->broadcast_presence(presence.value());
        impl_->next_presence_poll =
            after_io + std::chrono::milliseconds(kPresencePollIntervalMs);
    }
    if (impl_->cleanup_error != Error::OK) {
        const Error error = impl_->cleanup_error;
        impl_->cleanup_error = Error::OK;
        return Result<void>::failure(error);
    }
    return Result<void>::success();
}

Result<void> PosixControlServer::shutdown() noexcept
{
    if (!impl_ || impl_->shutdown_complete) {
        return Result<void>::success();
    }
    for (std::size_t index = impl_->clients.size(); index > 0U; --index) {
        impl_->remove_client(index - 1U);
    }
    const auto card_cleanup = impl_->card_service.shutdown();
    impl_->listener.close();
    impl_->shutdown_complete = true;
    if (impl_->cleanup_error != Error::OK) {
        return Result<void>::failure(impl_->cleanup_error);
    }
    return card_cleanup;
}

const char* PosixControlServer::endpoint_path() const noexcept
{
    return impl_ ? impl_->listener.endpoint_path() : "";
}

std::size_t PosixControlServer::connection_count() const noexcept
{
    return impl_ ? impl_->clients.size() : 0U;
}

}  // namespace px4::userland::ipc::posix

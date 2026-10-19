// SPDX-License-Identifier: GPL-2.0-only
// Native Windows, hardware-free tests of the actual typed CARD_* client and
// private AF_UNIX discovery. The peer is a scripted UDTV 1.0 IPC server, not
// USB/px4d. The client speaks the shared dtv-userland-core CARD control wire
// (HELLO/LIST/STATUS/CARD_STATUS/CARD_CONNECT/CARD_TRANSMIT/CARD_DISCONNECT).
#include "windows/winscard_card_client.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winscard.h>
#include <aclapi.h>
#include <winioctl.h>
#include "windows/windows_security.h"

#include "px4/posix_ipc.h"

#include <dtv/userland/records.h>
#include <dtv/userland/wire.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;
using namespace px4::userland::ipc::posix;
using windows_card::CardClient;
using Clock = std::chrono::steady_clock;
namespace w = dtv::userland::wire;

// UDTV 1.0 publishes exactly one local reader id.
constexpr std::uint16_t kReaderId = 0U;
constexpr std::uint8_t kEpochByte = 0x5aU;

int failures = 0;

#define CHECK(condition)                                                       \
    do {                                                                       \
        if (!(condition)) {                                                    \
            std::fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__,      \
                         __LINE__, #condition);                                \
            ++failures;                                                        \
        }                                                                      \
    } while (false)

template <std::size_t N>
bool set_text(w::WString<N>& out, const char* value) noexcept
{
    const std::size_t length = std::strlen(value);
    if (length > out.bytes.size()) return false;
    out.length = static_cast<std::uint16_t>(length);
    std::memcpy(out.bytes.data(), value, length);
    return true;
}

void set_producer(w::Producer& producer) noexcept
{
    (void)set_text(producer.product, "px4-userland");
    producer.version.length = 5U;
    std::memcpy(producer.version.bytes.data(), "1.2.3", 5U);
}

w::ListResponse fixture_list() noexcept
{
    w::ListResponse list{};
    list.epoch[0] = kEpochByte;
    set_producer(list.producer);
    list.topology.state = w::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = 1U;
    list.functions.count = 1U;
    auto& function = list.functions.items[0];
    function.id = 0U;
    function.vid = 0x1234U;
    function.pid = 0x5678U;
    function.mode = w::Mode::runtime;
    function.location.usb_path.present = true;
    function.location.usb_path.value.length = 3U;
    std::memcpy(function.location.usb_path.value.bytes.data(), "1-2", 3U);
    function.serial_state = w::SerialState::present;
    function.serial.present = true;
    (void)set_text(function.serial.value, "function-serial");
    function.model.present = true;
    (void)set_text(function.model.value.id, "model-id");
    (void)set_text(function.model.value.name, "Model name");
    list.enclosure.present = true;
    list.enclosure.value.id = 0U;
    list.enclosure.value.model.present = true;
    (void)set_text(list.enclosure.value.model.value.id, "model-id");
    (void)set_text(list.enclosure.value.model.value.name, "Model name");
    list.enclosure.value.identity.serial_state = w::IdentitySerialState::present;
    list.enclosure.value.identity.serial.present = true;
    (void)set_text(list.enclosure.value.identity.serial.value, "enclosure-serial");
    list.enclosure.value.identity.uniqueness = w::Uniqueness::unique_in_scan;
    list.receivers.present = true;
    list.receivers.value.count = 1U;
    list.receivers.value.items[0].id = 0U;
    list.receivers.value.items[0].systems.mask = 1U;
    list.readers.present = true;
    list.readers.value.count = 1U;
    list.readers.value.items[0].id = kReaderId;
    return list;
}

w::StatusResponse fixture_status(const w::ListResponse& list, bool ready) noexcept
{
    w::StatusResponse status{};
    status.producer = list.producer;
    status.context.epoch = list.epoch;
    status.context.state = ready ? w::ContextState::ready : w::ContextState::starting;
    status.context.instance.present = true;
    (void)set_text(status.context.instance.value, "instance");
    status.snapshot.sequence = 1U;
    status.snapshot.observed_at_ns = 5U;
    status.scope.kind = w::ScopeKind::enclosure;
    status.topology = list.topology;
    status.functions.present = true;
    status.functions.value = list.functions;
    status.receivers.present = true;
    status.receivers.value.count = 1U;
    status.receivers.value.items[0].descriptor = list.receivers.value.items[0];
    status.readers.present = true;
    status.readers.value.count = 1U;
    status.readers.value.items[0].descriptor = list.readers.value.items[0];
    status.readers.value.items[0].present = true;
    status.readers.value.items[0].card.state = w::CardState::present;
    status.readers.value.items[0].card.sampled_at_ns.present = true;
    status.readers.value.items[0].card.sampled_at_ns.value = 5U;
    status.enclosure.present = true;
    status.enclosure.value = list.enclosure.value;
    status.enclosure.value.identity.uniqueness = w::Uniqueness::unknown;
    return status;
}

class TempRoot final {
public:
    TempRoot()
    {
        std::array<wchar_t, MAX_PATH> base{};
        const DWORD length = ::GetEnvironmentVariableW(
            L"USERPROFILE", base.data(), static_cast<DWORD>(base.size()));
        if (length == 0U || length >= base.size()) return;
        for (unsigned attempt = 0U; attempt < 8U; ++attempt) {
            std::array<std::uint8_t, 6U> random{};
            std::array<wchar_t, 20U> token{};
            if (!windows_security::random_hex(random.data(), random.size(),
                                               token.data(), token.size())) return;
            // Exercise strict UTF-8 conversion as well as short AF_UNIX paths.
            const auto candidate = std::filesystem::path(base.data()) /
                (std::wstring(L"px4c-\u3042-") + token.data());
            if (::CreateDirectoryW(candidate.c_str(), nullptr) == 0) {
                if (::GetLastError() == ERROR_ALREADY_EXISTS) continue;
                return;
            }
            path_ = candidate;
            valid_ = windows_security::set_current_user_owner_and_dacl(path_.c_str());
            return;
        }
    }
    ~TempRoot() noexcept
    {
        if (!path_.empty()) {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
    }
    bool valid() const noexcept { return valid_; }
    std::string utf8() const { return path_.u8string(); }
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
    bool valid_ = false;
};

enum class TransmitBehavior : unsigned {
    echo, busy, disconnect, wrong_id, stall, no_card, card_removed
};

struct PeerOptions final {
    bool ready = true;
    bool stall_hello = false;
    std::uint64_t features = w::kFeatureCardService;
    std::uint64_t handle = 0x1122334455667788ULL;
    std::uint64_t generation = 0x8877665544332211ULL;
    bool invalid_connect = false;
    ErrorCode connect_error = ErrorCode::OK;
};

class MockDaemon final {
public:
    MockDaemon(const std::string& runtime, const std::string& instance,
               PeerOptions options = {}) : options_(options)
    {
        generation.store(options.generation);
        const EndpointConfig endpoint{runtime.c_str(), instance.c_str(),
                                       kControlEndpointName, EndpointAccess::private_user};
        auto listener = SocketListener::listen(endpoint);
        if (!listener) {
            std::fprintf(stderr, "mock listener error=%u runtime=%s instance=%s\n",
                         static_cast<unsigned>(listener.error()), runtime.c_str(),
                         instance.c_str());
            return;
        }
        listener_ = std::move(listener.value());
        thread_ = std::thread([this]() noexcept { run(); });
    }
    ~MockDaemon() noexcept
    {
        stop_.store(true);
        if (thread_.joinable()) thread_.join();
    }
    bool valid() const noexcept { return listener_.valid(); }

    std::atomic<unsigned> accepted{0U};
    std::atomic<unsigned> closed{0U};
    std::atomic<unsigned> card_connects{0U};
    std::atomic<unsigned> statuses{0U};
    std::atomic<unsigned> transmits{0U};
    std::atomic<unsigned> disconnects{0U};
    std::atomic<unsigned> last_mode{0U};
    std::atomic<unsigned> last_disposition{99U};
    std::atomic<bool> bad_request{false};
    std::atomic<bool> present{true};
    std::atomic<bool> initialized{true};
    std::atomic<std::uint64_t> generation{0U};
    std::atomic<std::uint8_t> status_atr_tail{0U};
    std::atomic<ErrorCode> status_error{ErrorCode::OK};
    std::atomic<TransmitBehavior> behavior{TransmitBehavior::echo};

private:
    template <typename Record>
    static bool encode_record(const Record& value,
                              dtv::userland::Result<std::size_t> (*encode)(
                                  const Record&, MutableByteView) noexcept,
                              std::array<std::uint8_t, w::kMaxControlPayload>& out,
                              std::size_t& out_size) noexcept
    {
        const auto size = encode(value, MutableByteView{out.data(), out.size()});
        if (!size) return false;
        out_size = size.value();
        return true;
    }

    class Peer final : public w::FrameHandler {
    public:
        Peer(MockDaemon& owner, SocketStream& stream) noexcept
            : owner_(owner), stream_(stream) {}

        Result<void> on_frame(const w::Frame& frame) noexcept override
        {
            if (frame.header.kind != w::Kind::request) return fail();
            const std::uint16_t opcode = frame.header.opcode;
            const std::uint32_t request_id = frame.header.request_id;
            switch (opcode) {
            case w::kOpcodeHello: {
                const auto hello = w::decode_hello_request(frame.payload);
                if (!hello || hello.value().required_features != w::kFeatureCardService)
                    return fail();
                if (owner_.options_.stall_hello) return Result<void>::success();
                w::HelloResponse response{};
                response.major = w::kVersionMajor;
                response.minor = w::kVersionMinor;
                response.features = owner_.options_.features;
                response.epoch[0] = kEpochByte;
                set_producer(response.producer);
                response.max_control_payload = static_cast<std::uint32_t>(w::kMaxControlPayload);
                response.max_ts_bytes = static_cast<std::uint32_t>(w::kMaxTsPacketBytes);
                return reply(opcode, request_id, response, &w::encode_hello_response);
            }
            case w::kOpcodeList: {
                const auto request = w::decode_list_request(frame.payload);
                if (!request) return fail();
                const w::ListResponse list = fixture_list();
                return reply(opcode, request_id, list, &w::encode_list_response);
            }
            case w::kOpcodeStatus: {
                const auto request = w::decode_status_request(frame.payload);
                if (!request) return fail();
                const w::ListResponse list = fixture_list();
                const w::StatusResponse status =
                    fixture_status(list, owner_.options_.ready);
                return reply(opcode, request_id, status, &w::encode_status_response);
            }
            case w::kOpcodeCardStatus: {
                const auto request = w::decode_card_status_request(frame.payload);
                if (!request) return fail();
                ++owner_.statuses;
                if (!owner_.options_.ready) return error(opcode, request_id, ErrorCode::NOT_READY);
                if (owner_.status_error.load() != ErrorCode::OK)
                    return error(opcode, request_id, owner_.status_error.load());
                const bool present = owner_.present.load();
                const bool initialized = present && connected_ && owner_.initialized.load();
                w::CardStatusResponse status{};
                status.key.epoch[0] = kEpochByte;
                status.key.reader_id = kReaderId;
                status.current_generation = owner_.generation.load();
                status.present = present;
                status.initialized = initialized;
                if (initialized) {
                    status.protocol.present = true;
                    status.protocol.value = w::CardProtocol::t1;
                    status.atr.present = true;
                    status.atr.value.length = 2U;
                    status.atr.value.bytes[0] = 0x3bU;
                    status.atr.value.bytes[1] = owner_.status_atr_tail.load();
                }
                return reply(opcode, request_id, status, &w::encode_card_status_response);
            }
            case w::kOpcodeCardConnect: {
                const auto card = w::decode_card_connect_request(frame.payload);
                if (!card) return fail();
                ++owner_.card_connects;
                owner_.last_mode.store(static_cast<unsigned>(card.value().share));
                if (owner_.options_.connect_error != ErrorCode::OK)
                    return error(opcode, request_id, owner_.options_.connect_error);
                if (!owner_.present.load()) return error(opcode, request_id, ErrorCode::NO_CARD);
                connected_ = true;
                w::CardSessionResult response{};
                response.epoch[0] = kEpochByte;
                response.reader_id = kReaderId;
                response.card_handle = owner_.options_.handle;
                response.bound_generation = owner_.generation.load();
                response.current_generation = owner_.generation.load();
                response.present = true;
                response.initialized = !owner_.options_.invalid_connect;
                response.usable = !owner_.options_.invalid_connect;
                if (response.usable) {
                    response.protocol.present = true;
                    response.protocol.value = w::CardProtocol::t1;
                    response.atr.present = true;
                    response.atr.value.length = 2U;
                    response.atr.value.bytes[0] = 0x3bU;
                    response.atr.value.bytes[1] = 0x00U;
                }
                return reply(opcode, request_id, response, &w::encode_card_session_result);
            }
            case w::kOpcodeCardTransmit: {
                const auto card = w::decode_card_transmit_request(frame.payload);
                if (!card || card.value().key.card_handle != owner_.options_.handle)
                    return fail();
                ++owner_.transmits;
                switch (owner_.behavior.load()) {
                case TransmitBehavior::busy:
                    return error(opcode, request_id, ErrorCode::BUSY);
                case TransmitBehavior::no_card:
                    return error(opcode, request_id, ErrorCode::NO_CARD);
                case TransmitBehavior::card_removed:
                    return error(opcode, request_id, ErrorCode::CARD_REMOVED);
                case TransmitBehavior::disconnect:
                    stream_.close();
                    return Result<void>::success();
                case TransmitBehavior::wrong_id:
                    return reply(opcode, request_id + 1U, echo_transmit(card.value()),
                                 &w::encode_card_transmit_response);
                case TransmitBehavior::stall:
                    return Result<void>::success();
                case TransmitBehavior::echo:
                    return reply(opcode, request_id, echo_transmit(card.value()),
                                 &w::encode_card_transmit_response);
                }
                return fail();
            }
            case w::kOpcodeCardDisconnect: {
                const auto card = w::decode_card_disconnect_request(frame.payload);
                if (!card || card.value().key.card_handle != owner_.options_.handle)
                    return fail();
                ++owner_.disconnects;
                owner_.last_disposition.store(static_cast<unsigned>(card.value().disposition));
                w::CardDisconnectResponse response{};
                response.key.epoch[0] = kEpochByte;
                response.key.reader_id = kReaderId;
                response.card_handle = card.value().key.card_handle;
                response.consumed = true;
                response.quiesced = true;
                return reply(opcode, request_id, response, &w::encode_card_disconnect_response);
            }
            default:
                return fail();
            }
        }

    private:
        static w::CardTransmitResponse echo_transmit(const w::CardTransmitRequest& request) noexcept
        {
            w::CardTransmitResponse response{};
            response.key = request.key;
            response.response = request.command;
            return response;
        }
        Result<void> fail() noexcept
        {
            owner_.bad_request.store(true);
            return Result<void>::failure(Error::PROTOCOL_ERROR);
        }
        template <typename Record>
        Result<void> reply(std::uint16_t opcode, std::uint32_t request_id, const Record& value,
                           dtv::userland::Result<std::size_t> (*encode)(
                               const Record&, MutableByteView) noexcept) noexcept
        {
            std::size_t size = 0U;
            if (!encode_record(value, encode, scratch_, size)) return fail();
            return send(opcode, request_id, ByteView{scratch_.data(), size}, w::Kind::response);
        }
        Result<void> error(std::uint16_t opcode, std::uint32_t request_id,
                           ErrorCode error_code) noexcept
        {
            w::ErrorResponse response{};
            response.epoch[0] = kEpochByte;
            response.error.code = static_cast<std::uint32_t>(error_code);
            response.error.operation.length = 4U;
            std::memcpy(response.error.operation.bytes.data(), "card", 4U);
            std::size_t size = 0U;
            if (!encode_record(response, &w::encode_error_response, scratch_, size)) return fail();
            return send(opcode, request_id, ByteView{scratch_.data(), size},
                        w::Kind::error_response);
        }
        Result<void> send(std::uint16_t opcode, std::uint32_t request_id, ByteView payload,
                          w::Kind kind) noexcept
        {
            w::Header header{};
            header.kind = kind;
            header.opcode = opcode;
            header.request_id = request_id;
            header.payload_length = static_cast<std::uint32_t>(payload.size);
            const auto size = w::encode_frame(
                header, payload, MutableByteView{out_.data(), out_.size()});
            if (!size) return fail();
            return stream_.write_all(ByteView{out_.data(), size.value()}, Timeout{500U});
        }
        MockDaemon& owner_;
        SocketStream& stream_;
        std::array<std::uint8_t, w::kMaxControlPayload> scratch_{};
        std::array<std::uint8_t, w::kHeaderSize + w::kMaxControlPayload> out_{};
        bool connected_ = false;
    };

    void run() noexcept
    {
        while (!stop_.load()) {
            auto accepted_stream = listener_.accept(Timeout{25U});
            if (!accepted_stream) {
                if (accepted_stream.error() != Error::TIMEOUT) bad_request.store(true);
                continue;
            }
            ++accepted;
            auto& stream = accepted_stream.value();
            std::array<std::uint8_t, w::kHeaderSize + w::kMaxControlPayload> storage{};
            std::array<std::uint8_t, 8192U> input{};
            w::FrameReceiver receiver(MutableByteView{storage.data(), storage.size()});
            Peer peer(*this, stream);
            while (!stop_.load()) {
                const auto result = stream.read_some(
                    MutableByteView{input.data(), input.size()}, Timeout{25U});
                if (!result) {
                    if (result.error() != Error::TIMEOUT) break;
                    continue;
                }
                const auto fed = receiver.feed(ByteView{input.data(), result.value()}, peer);
                if (!fed) break;
            }
            ++closed;
        }
    }

    PeerOptions options_;
    SocketListener listener_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

bool wait_for(const std::atomic<unsigned>& value, unsigned minimum)
{
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < deadline) {
        if (value.load() >= minimum) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void test_invalid_paths_and_instances()
{
    const std::vector<std::string> roots{
        "", ".", "relative", "C:relative", "\\rooted", "\\\\server\\share",
        "\\\\?\\UNC\\server\\share", "\\\\?\\C:\\temp", "\\\\.\\C:\\temp",
        "C:/temp", "C:\\foo\\..\\bar", "C:\\foo\\.\\bar", "C:\\foo.\\bar",
        "C:\\foo \\bar", "C:\\foo\\\\bar", "C:\\foo:bar", "C:\\foo*",
        "C:\\nul\\bar", "C:\\COM1.txt\\bar", std::string("C:\\foo\0bar", 10U),
        std::string("C:\\\xff", 4U)};
    for (const auto& root : roots) {
        const auto list = windows_card::list_readers(root);
        CHECK(!list && list.error() == Error::INVALID_ARGUMENT);
        const auto card = CardClient::connect(root, "test", ShareMode::shared);
        CHECK(!card && card.error() == Error::INVALID_ARGUMENT);
    }
    for (const char* name : {"", ".", "..", "a/b", "a\\b", "a:b", "name.",
                             "CON", "con.txt", "NUL", "AUX", "PRN", "LPT9", "COM1"}) {
        const auto card = CardClient::connect("C:\\unreachable", name, ShareMode::shared);
        CHECK(!card && card.error() == Error::INVALID_ARGUMENT);
    }
    const auto long_name = CardClient::connect("C:\\unreachable", std::string(81U, 'x'),
                                               ShareMode::shared);
    CHECK(!long_name && long_name.error() == Error::INVALID_ARGUMENT);
}

void test_discovery_filters_and_sorting()
{
    TempRoot root;
    CHECK(root.valid());
    if (!root.valid()) return;
    const auto empty = windows_card::list_readers(root.utf8());
    CHECK(empty && empty.value().empty());
    MockDaemon zulu(root.utf8(), "zulu");
    MockDaemon alpha(root.utf8(), "alpha");
    PeerOptions not_ready;
    not_ready.ready = false;
    MockDaemon starting(root.utf8(), "starting", not_ready);
    PeerOptions no_card;
    no_card.features = 0U;
    MockDaemon unsupported(root.utf8(), "unsupported", no_card);
    PeerOptions legacy;
    legacy.features = 0U;
    MockDaemon legacy_server(root.utf8(), "legacy", legacy);
    PeerOptions no_card_bit;
    no_card_bit.features = 0U;
    MockDaemon no_card_server(root.utf8(), "nocardbit", no_card_bit);
    CHECK(zulu.valid() && alpha.valid() && starting.valid() && unsupported.valid() &&
          legacy_server.valid() && no_card_server.valid());
    const auto stale_path = root.path() / "px4-userland" / "v1" / "stale";
    CHECK(::CreateDirectoryW(stale_path.c_str(), nullptr) != 0);
    CHECK(windows_security::set_current_user_owner_and_dacl(stale_path.c_str()));
    const auto listed = windows_card::list_readers(root.utf8());
    CHECK(listed && listed.value().size() == 2U);
    if (listed && listed.value().size() == 2U) {
        CHECK(listed.value()[0].instance == "alpha");
        CHECK(listed.value()[0].display_name == "PX4 userland (alpha)");
        CHECK(listed.value()[1].instance == "zulu");
    }
    CHECK(zulu.card_connects.load() == 0U && alpha.card_connects.load() == 0U);
    const auto unavailable = CardClient::connect(root.utf8(), "starting", ShareMode::shared);
    CHECK(!unavailable && unavailable.error() == Error::NOT_READY);
    const auto no_capability = CardClient::connect(root.utf8(), "unsupported", ShareMode::shared);
    CHECK(!no_capability && no_capability.error() == Error::UNSUPPORTED);
    const auto legacy_card = CardClient::connect(root.utf8(), "legacy", ShareMode::shared);
    CHECK(!legacy_card && legacy_card.error() == Error::UNSUPPORTED);
    const auto missing_card = CardClient::connect(root.utf8(), "nocardbit", ShareMode::shared);
    CHECK(!missing_card && missing_card.error() == Error::UNSUPPORTED);
    CHECK(starting.card_connects.load() == 0U && unsupported.card_connects.load() == 0U);
    CHECK(legacy_server.card_connects.load() == 0U && legacy_server.transmits.load() == 0U &&
          no_card_server.card_connects.load() == 0U && no_card_server.transmits.load() == 0U);
    const auto legacy_status = windows_card::reader_status(root.utf8(), "legacy");
    CHECK(!legacy_status && legacy_status.error() == Error::UNSUPPORTED);
    CHECK(legacy_server.statuses.load() == 0U && no_card_server.statuses.load() == 0U);
    CHECK(!alpha.bad_request.load() && !zulu.bad_request.load());
}

void test_typed_round_trip_and_lifetime()
{
    TempRoot root;
    CHECK(root.valid());
    if (!root.valid()) return;
    MockDaemon server(root.utf8(), "reader");
    CHECK(server.valid());
    if (!server.valid()) return;
    auto card = CardClient::connect(root.utf8(), "reader", ShareMode::exclusive);
    CHECK(card);
    if (!card) return;
    CHECK(server.last_mode.load() == static_cast<unsigned>(ShareMode::exclusive));
    // begin_card issues exactly one reader-keyed CARD_STATUS before CONNECT.
    CHECK(server.statuses.load() == 1U);
    server.status_atr_tail.store(0x55U);
    const auto status = card.value()->status();
    CHECK(status && status.value().present && status.value().initialized &&
          status.value().generation == 0x8877665544332211ULL &&
          status.value().atr == std::vector<std::uint8_t>({0x3bU, 0x00U}));
    CHECK(server.statuses.load() == 2U);
    const std::array<std::uint8_t, 5U> apdu{0x00U, 0xa4U, 0x00U, 0x00U, 0x00U};
    const auto response = card.value()->transmit(ByteView{apdu.data(), apdu.size()});
    CHECK(response && response.value() == std::vector<std::uint8_t>(apdu.begin(), apdu.end()));
    std::vector<std::uint8_t> maximum(kMaxCardPayload, 0xa5U);
    const auto large = card.value()->transmit(ByteView{maximum.data(), maximum.size()});
    CHECK(large && large.value() == maximum);
    CHECK(!card.value()->transmit(ByteView{nullptr, 1U}));
    CHECK(!card.value()->transmit(ByteView{apdu.data(), 0U}));
    CHECK(!card.value()->transmit(ByteView{maximum.data(), kMaxCardPayload + 1U}));
    CHECK(server.transmits.load() == 2U);
    server.behavior.store(TransmitBehavior::busy);
    const auto busy = card.value()->transmit(ByteView{apdu.data(), apdu.size()});
    CHECK(!busy && busy.error() == Error::BUSY);
    CHECK(server.transmits.load() == 3U);
    server.behavior.store(TransmitBehavior::echo);
    CHECK(card.value()->transmit(ByteView{apdu.data(), apdu.size()}));
    const auto invalid = card.value()->disconnect(static_cast<Disposition>(99U));
    CHECK(!invalid && invalid.error() == Error::INVALID_ARGUMENT);
    CHECK(server.disconnects.load() == 0U);
    CHECK(card.value()->disconnect(Disposition::reset));
    CHECK(server.last_disposition.load() == static_cast<unsigned>(Disposition::reset));
    const auto stale = card.value()->transmit(ByteView{apdu.data(), apdu.size()});
    CHECK(!stale && stale.error() == Error::NOT_FOUND);
    CHECK(!card.value()->disconnect(Disposition::leave));
    CHECK(wait_for(server.closed, 1U));
    card.value().reset();
    auto abandoned = CardClient::connect(root.utf8(), "reader", ShareMode::shared);
    CHECK(abandoned);
    abandoned.value().reset();
    CHECK(wait_for(server.closed, 2U));
    CHECK(server.disconnects.load() == 1U);
    CHECK(!server.bad_request.load());
}

void test_connect_errors()
{
    TempRoot root;
    CHECK(root.valid());
    if (!root.valid()) return;
    PeerOptions zero;
    zero.handle = 0U;
    MockDaemon zero_server(root.utf8(), "zero", zero);
    const auto zero_card = CardClient::connect(root.utf8(), "zero", ShareMode::shared);
    CHECK(!zero_card && zero_card.error() == Error::PROTOCOL_ERROR);
    PeerOptions empty_atr;
    empty_atr.invalid_connect = true;
    MockDaemon empty_atr_server(root.utf8(), "emptyatr", empty_atr);
    const auto empty_atr_card = CardClient::connect(root.utf8(), "emptyatr", ShareMode::shared);
    CHECK(!empty_atr_card && empty_atr_card.error() == Error::PROTOCOL_ERROR);
    CHECK(empty_atr_server.transmits.load() == 0U && wait_for(empty_atr_server.closed, 1U));
    PeerOptions busy;
    busy.connect_error = ErrorCode::BUSY;
    MockDaemon busy_server(root.utf8(), "busy", busy);
    const auto busy_card = CardClient::connect(root.utf8(), "busy", ShareMode::exclusive);
    CHECK(!busy_card && busy_card.error() == Error::BUSY);
    CHECK(busy_server.card_connects.load() == 1U);
    const auto invalid_mode = CardClient::connect(root.utf8(), "busy", static_cast<ShareMode>(0U));
    CHECK(!invalid_mode && invalid_mode.error() == Error::INVALID_ARGUMENT);
    CHECK(busy_server.card_connects.load() == 1U);
}

void test_terminal_errors_do_not_replay()
{
    const std::array<std::uint8_t, 2U> apdu{0x00U, 0x84U};
    for (const auto behavior : {TransmitBehavior::disconnect, TransmitBehavior::wrong_id,
                                TransmitBehavior::stall, TransmitBehavior::no_card,
                                TransmitBehavior::card_removed}) {
        TempRoot root;
        CHECK(root.valid());
        if (!root.valid()) return;
        MockDaemon server(root.utf8(), "reader");
        auto card = CardClient::connect(root.utf8(), "reader", ShareMode::shared);
        CHECK(card);
        if (!card) continue;
        server.behavior.store(behavior);
        const auto result = card.value()->transmit(ByteView{apdu.data(), apdu.size()});
        const Error expected = behavior == TransmitBehavior::disconnect ? Error::DISCONNECTED :
            (behavior == TransmitBehavior::wrong_id ? Error::PROTOCOL_ERROR :
             (behavior == TransmitBehavior::no_card ? Error::NO_CARD :
              (behavior == TransmitBehavior::card_removed ? Error::CARD_REMOVED : Error::TIMEOUT)));
        CHECK(!result && result.error() == expected);
        const auto stale = card.value()->transmit(ByteView{apdu.data(), apdu.size()});
        CHECK(!stale && stale.error() == Error::NOT_FOUND);
        CHECK(server.accepted.load() == 1U && server.transmits.load() == 1U);
        CHECK(wait_for(server.closed, 1U));
        CHECK(!server.bad_request.load());
    }
}

void test_reader_and_owner_status()
{
    {
        TempRoot root;
        CHECK(root.valid());
        if (!root.valid()) return;
        MockDaemon server(root.utf8(), "reader");
        const auto before_connect = windows_card::reader_status(root.utf8(), "reader");
        CHECK(before_connect && before_connect.value().present &&
              !before_connect.value().initialized && before_connect.value().atr.empty() &&
              before_connect.value().generation == 0x8877665544332211ULL);
        CHECK(server.card_connects.load() == 0U && server.transmits.load() == 0U &&
              server.disconnects.load() == 0U);
        // A reader snapshot need not initialize a card. Connecting is one
        // explicit CARD_CONNECT after the mandatory CARD_STATUS sample.
        auto card = CardClient::connect(root.utf8(), "reader", ShareMode::shared);
        CHECK(card);
        if (!card) return;
        ++server.generation;
        const auto changed = card.value()->status();
        CHECK(changed && changed.value().generation == server.generation.load());
        // The owning connection sees the reader-keyed sample. A failure is
        // terminal: the connection closes and later calls report NOT_FOUND.
        server.status_error.store(ErrorCode::CARD_REMOVED);
        const auto removed = card.value()->status();
        CHECK(!removed && removed.error() == Error::CARD_REMOVED);
        const auto stale = card.value()->status();
        CHECK(!stale && stale.error() == Error::NOT_FOUND);
        const std::uint8_t apdu = 0U;
        CHECK(!card.value()->transmit(ByteView{&apdu, 1U}));
        CHECK(server.card_connects.load() == 1U && server.transmits.load() == 0U);
        // reader_status sample, begin_card sample, one owner sample, one
        // failing owner sample.
        CHECK(server.statuses.load() == 4U);
    }
    {
        TempRoot root;
        CHECK(root.valid());
        if (!root.valid()) return;
        MockDaemon server(root.utf8(), "reader");
        server.present.store(false);
        const auto absent = windows_card::reader_status(root.utf8(), "reader");
        CHECK(absent && !absent.value().present && !absent.value().initialized &&
              absent.value().atr.empty());
        const auto no_card = CardClient::connect(root.utf8(), "reader", ShareMode::shared);
        CHECK(!no_card && no_card.error() == Error::NO_CARD);
        CHECK(server.card_connects.load() == 1U);
        server.present.store(true);
        auto card = CardClient::connect(root.utf8(), "reader", ShareMode::shared);
        CHECK(card);
        if (!card) return;
        server.present.store(false);
        const auto removed = card.value()->status();
        CHECK(!removed && removed.error() == Error::CARD_REMOVED);
        CHECK(!card.value()->status());
        CHECK(server.transmits.load() == 0U);
    }
    for (const auto error : {ErrorCode::NOT_FOUND, ErrorCode::NO_CARD,
                             ErrorCode::CARD_REMOVED, ErrorCode::NOT_READY}) {
        TempRoot root;
        CHECK(root.valid());
        if (!root.valid()) return;
        MockDaemon server(root.utf8(), "reader");
        auto card = CardClient::connect(root.utf8(), "reader", ShareMode::shared);
        CHECK(card);
        if (!card) continue;
        server.status_error.store(error);
        const auto status = card.value()->status();
        CHECK(!status && static_cast<unsigned>(status.error()) == static_cast<unsigned>(error));
        CHECK(!card.value()->status());
        const std::uint8_t apdu = 0U;
        CHECK(!card.value()->transmit(ByteView{&apdu, 1U}));
        CHECK(server.statuses.load() == 2U && server.card_connects.load() == 1U &&
              server.transmits.load() == 0U && server.disconnects.load() == 0U);
        CHECK(wait_for(server.closed, 1U));
        CHECK(server.accepted.load() == 1U && !server.bad_request.load());
    }
    {
        TempRoot root;
        CHECK(root.valid());
        if (!root.valid()) return;
        MockDaemon server(root.utf8(), "reader");
        auto card = CardClient::connect(root.utf8(), "reader", ShareMode::shared);
        CHECK(card);
        if (!card) return;
        server.initialized.store(false);
        const auto status = card.value()->status();
        CHECK(!status && status.error() == Error::NOT_READY);
        CHECK(!card.value()->status());
        CHECK(server.statuses.load() == 2U && server.transmits.load() == 0U);
    }
}

void test_discovery_entry_and_time_bounds()
{
    {
        TempRoot root;
        CHECK(root.valid());
        if (!root.valid()) return;
        const auto product = root.path() / "px4-userland";
        const auto version = product / "v1";
        CHECK(::CreateDirectoryW(product.c_str(), nullptr) != 0);
        CHECK(windows_security::set_current_user_owner_and_dacl(product.c_str()));
        CHECK(::CreateDirectoryW(version.c_str(), nullptr) != 0);
        CHECK(windows_security::set_current_user_owner_and_dacl(version.c_str()));
        for (unsigned i = 0U; i < 65U; ++i) {
            const auto entry = version / ("entry" + std::to_string(i));
            CHECK(::CreateDirectoryW(entry.c_str(), nullptr) != 0);
        }
        const auto list = windows_card::list_readers(root.utf8());
        CHECK(!list && list.error() == Error::BUFFER_TOO_SMALL);
    }
    {
        TempRoot root;
        CHECK(root.valid());
        if (!root.valid()) return;
        std::vector<std::unique_ptr<MockDaemon>> servers;
        PeerOptions stalled;
        stalled.stall_hello = true;
        for (unsigned i = 0U; i < 24U; ++i) {
            servers.emplace_back(new MockDaemon(root.utf8(), "stall" + std::to_string(i), stalled));
            CHECK(servers.back()->valid());
        }
        const auto start = Clock::now();
        const auto list = windows_card::list_readers(root.utf8());
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now() - start).count();
        CHECK(!list && list.error() == Error::TIMEOUT);
        CHECK(elapsed < 3000);
        for (const auto& server : servers) CHECK(server->card_connects.load() == 0U);
    }
}

void test_untrusted_security_fails_closed()
{
    TempRoot root;
    CHECK(root.valid());
    if (!root.valid()) return;
    MockDaemon server(root.utf8(), "reader");
    CHECK(server.valid());
    const auto product = root.path() / "px4-userland";
    const DWORD status = ::SetNamedSecurityInfoW(
        const_cast<wchar_t*>(product.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        nullptr, nullptr, nullptr, nullptr);
    CHECK(status == ERROR_SUCCESS);
    const auto list = windows_card::list_readers(root.utf8());
    CHECK(!list && list.error() == Error::INVALID_ARGUMENT);
    const auto card = CardClient::connect(root.utf8(), "reader", ShareMode::shared);
    CHECK(!card && card.error() == Error::INVALID_ARGUMENT);
    CHECK(server.accepted.load() == 0U);
    CHECK(windows_security::set_current_user_owner_and_dacl(product.c_str()));
    CHECK(::SetNamedSecurityInfoW(const_cast<wchar_t*>(root.path().c_str()), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS);
    const auto bad_root = windows_card::list_readers(root.utf8());
    CHECK(!bad_root && bad_root.error() == Error::INVALID_ARGUMENT);
    CHECK(server.accepted.load() == 0U);
    CHECK(windows_security::set_current_user_owner_and_dacl(root.path().c_str()));
}

bool create_junction(const std::filesystem::path& link,
                     const std::filesystem::path& target) noexcept
{
    // Directory junctions do not need symbolic-link privilege / Developer Mode.
    struct JunctionData final {
        DWORD tag;
        WORD data_length;
        WORD reserved;
        WORD substitute_offset;
        WORD substitute_length;
        WORD print_offset;
        WORD print_length;
        wchar_t paths[1024U];
    } data{};
    static_assert(offsetof(JunctionData, paths) == 16U);
    const std::wstring printable = target.wstring();
    const std::wstring substitute = L"\\??\\" + printable;
    const std::size_t characters = substitute.size() + 1U + printable.size() + 1U;
    if (characters > 1024U || ::CreateDirectoryW(link.c_str(), nullptr) == 0) return false;
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.data_length = static_cast<WORD>(8U + characters * sizeof(wchar_t));
    data.substitute_length = static_cast<WORD>(substitute.size() * sizeof(wchar_t));
    data.print_offset = static_cast<WORD>((substitute.size() + 1U) * sizeof(wchar_t));
    data.print_length = static_cast<WORD>(printable.size() * sizeof(wchar_t));
    std::memcpy(data.paths, substitute.c_str(), (substitute.size() + 1U) * sizeof(wchar_t));
    std::memcpy(data.paths + substitute.size() + 1U, printable.c_str(),
                (printable.size() + 1U) * sizeof(wchar_t));
    const HANDLE handle = ::CreateFileW(link.c_str(), GENERIC_WRITE, 0U, nullptr,
                                        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT |
                                        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD returned = 0U;
    const BOOL ok = ::DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, &data,
                                      static_cast<DWORD>(8U + data.data_length),
                                      nullptr, 0U, &returned, nullptr);
    ::CloseHandle(handle);
    return ok != 0;
}

void test_reparse_traversal_rejected()
{
    TempRoot original;
    TempRoot aliases;
    TempRoot instance_alias;
    CHECK(original.valid() && aliases.valid() && instance_alias.valid());
    if (!original.valid() || !aliases.valid() || !instance_alias.valid()) return;
    const auto runtime = original.path() / "runtime";
    CHECK(::CreateDirectoryW(runtime.c_str(), nullptr) != 0);
    CHECK(windows_security::set_current_user_owner_and_dacl(runtime.c_str()));
    MockDaemon server(runtime.u8string(), "reader");
    CHECK(server.valid());
    if (!server.valid()) return;

    const auto ancestor_link = aliases.path() / "jump";
    CHECK(create_junction(ancestor_link, original.path()));
    const std::string through_ancestor = (ancestor_link / "runtime").u8string();
    const auto ancestor_list = windows_card::list_readers(through_ancestor);
    CHECK(!ancestor_list && ancestor_list.error() == Error::INVALID_ARGUMENT);
    const auto ancestor_card = CardClient::connect(through_ancestor, "reader", ShareMode::shared);
    CHECK(!ancestor_card && ancestor_card.error() == Error::INVALID_ARGUMENT);

    const auto product_link = aliases.path() / "px4-userland";
    CHECK(create_junction(product_link, runtime / "px4-userland"));
    const auto product_list = windows_card::list_readers(aliases.utf8());
    CHECK(!product_list && product_list.error() == Error::INVALID_ARGUMENT);
    const auto private_product = instance_alias.path() / "px4-userland";
    CHECK(::CreateDirectoryW(private_product.c_str(), nullptr) != 0);
    CHECK(windows_security::set_current_user_owner_and_dacl(private_product.c_str()));
    const auto private_version = private_product / "v1";
    CHECK(::CreateDirectoryW(private_version.c_str(), nullptr) != 0);
    CHECK(windows_security::set_current_user_owner_and_dacl(private_version.c_str()));
    const auto reader_link = private_version / "reader";
    CHECK(create_junction(reader_link, runtime / "px4-userland" / "v1" / "reader"));
    const auto instance_list = windows_card::list_readers(instance_alias.utf8());
    CHECK(instance_list && instance_list.value().empty());
    const auto instance_card = CardClient::connect(instance_alias.utf8(), "reader",
                                                  ShareMode::shared);
    CHECK(!instance_card && instance_card.error() == Error::INVALID_ARGUMENT);
    CHECK(server.accepted.load() == 0U);
    CHECK(::RemoveDirectoryW(reader_link.c_str()) != 0);
    CHECK(::RemoveDirectoryW(product_link.c_str()) != 0);
    CHECK(::RemoveDirectoryW(ancestor_link.c_str()) != 0);
}

template <typename Function>
Function imported_function(HMODULE module, const char* name) noexcept
{
    const FARPROC raw = ::GetProcAddress(module, name);
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(raw));
    std::memcpy(&function, &raw, sizeof(function));
    return function;
}

void test_production_dll_tvcas_sequence()
{
    TempRoot root;
    CHECK(root.valid());
    if (!root.valid()) return;
    // Keep the synthetic name unique even on machines with real native
    // readers. The only selected card below is this private mock endpoint.
    const std::string root_name = root.path().filename().u8string();
    const std::string instance = "dll-" + root_name.substr(root_name.size() - 12U);
    MockDaemon server(root.utf8(), instance);
    CHECK(server.valid());
    if (!server.valid()) return;
    std::array<wchar_t, 32768U> executable{};
    const DWORD executable_length = ::GetModuleFileNameW(
        nullptr, executable.data(), static_cast<DWORD>(executable.size()));
    CHECK(executable_length != 0U && executable_length < executable.size());
    if (executable_length == 0U || executable_length >= executable.size()) return;
    const auto dll_path = std::filesystem::path(executable.data()).parent_path() / L"WinSCard.dll";
    const HMODULE dll = ::LoadLibraryExW(dll_path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    CHECK(dll != nullptr);
    if (dll == nullptr) return;
    std::array<wchar_t, 32768U> loaded_path{};
    const DWORD loaded_length = ::GetModuleFileNameW(
        dll, loaded_path.data(), static_cast<DWORD>(loaded_path.size()));
    std::error_code identity_error;
    const bool expected_module = loaded_length != 0U && loaded_length < loaded_path.size() &&
        std::filesystem::equivalent(dll_path, std::filesystem::path(loaded_path.data()), identity_error) &&
        !identity_error;
    CHECK(expected_module);
    if (!expected_module) { ::FreeLibrary(dll); return; }
    const auto establish = imported_function<decltype(&::SCardEstablishContext)>(dll, "SCardEstablishContext");
    const auto list = imported_function<decltype(&::SCardListReadersW)>(dll, "SCardListReadersW");
    const auto change = imported_function<decltype(&::SCardGetStatusChangeW)>(dll, "SCardGetStatusChangeW");
    const auto connect = imported_function<decltype(&::SCardConnectW)>(dll, "SCardConnectW");
    const auto status = imported_function<decltype(&::SCardStatusW)>(dll, "SCardStatusW");
    const auto transmit = imported_function<decltype(&::SCardTransmit)>(dll, "SCardTransmit");
    const auto disconnect = imported_function<decltype(&::SCardDisconnect)>(dll, "SCardDisconnect");
    const auto free_memory = imported_function<decltype(&::SCardFreeMemory)>(dll, "SCardFreeMemory");
    const auto release = imported_function<decltype(&::SCardReleaseContext)>(dll, "SCardReleaseContext");
    const auto is_valid = imported_function<decltype(&::SCardIsValidContext)>(dll, "SCardIsValidContext");
    const bool complete = establish && list && change && connect && status && transmit &&
                          disconnect && free_memory && release && is_valid;
    CHECK(complete);
    if (!complete) { ::FreeLibrary(dll); return; }

    std::array<wchar_t, 32768U> old_runtime{};
    ::SetLastError(ERROR_SUCCESS);
    const DWORD old_size = ::GetEnvironmentVariableW(L"PX4_WINSCARD_RUNTIME_DIR",
        old_runtime.data(), static_cast<DWORD>(old_runtime.size()));
    const bool old_missing = old_size == 0U && ::GetLastError() == ERROR_ENVVAR_NOT_FOUND;
    CHECK(old_size < old_runtime.size());
    if (old_size >= old_runtime.size()) { ::FreeLibrary(dll); return; }
    const BOOL configured = ::SetEnvironmentVariableW(L"PX4_WINSCARD_RUNTIME_DIR", root.path().c_str());
    CHECK(configured != 0);
    if (configured == 0) { ::FreeLibrary(dll); return; }

    SCARDCONTEXT context = 0U;
    CHECK(establish(SCARD_SCOPE_USER, nullptr, nullptr, &context) == SCARD_S_SUCCESS);
    if (context != 0U) {
        CHECK(is_valid(context) == SCARD_S_SUCCESS);
        wchar_t* names = nullptr;
        DWORD names_length = SCARD_AUTOALLOCATE;
        const LONG listed = list(context, nullptr, reinterpret_cast<LPWSTR>(&names), &names_length);
        CHECK(listed == SCARD_S_SUCCESS && names != nullptr && names_length >= 2U);
        const std::wstring reader = L"PX4 userland (" +
            std::wstring(instance.begin(), instance.end()) + L")";
        bool found_reader = false;
        if (listed == SCARD_S_SUCCESS && names != nullptr && names_length <= 65536U) {
            std::size_t offset = 0U;
            while (offset < names_length && names[offset] != L'\0') {
                std::size_t end = offset;
                while (end < names_length && names[end] != L'\0') ++end;
                CHECK(end < names_length);
                if (end == names_length) break;
                if (std::wstring(names + offset, end - offset) == reader) found_reader = true;
                offset = end + 1U;
            }
        }
        CHECK(found_reader);
        SCARD_READERSTATEW state{};
        state.szReader = reader.c_str();
        state.dwCurrentState = SCARD_STATE_UNAWARE;
        CHECK(change(context, 0U, &state, 1U) == SCARD_S_SUCCESS);
        CHECK((state.dwEventState & SCARD_STATE_PRESENT) != 0U &&
              (state.dwEventState & SCARD_STATE_EMPTY) == 0U);

        SCARDHANDLE card = 0U;
        DWORD active = SCARD_PROTOCOL_UNDEFINED;
        CHECK(connect(context, reader.c_str(), SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
                       &card, &active) == SCARD_S_SUCCESS);
        CHECK(card != 0U && active == SCARD_PROTOCOL_T1);
        if (card != 0U) {
            wchar_t* actual_reader = nullptr;
            DWORD reader_length = SCARD_AUTOALLOCATE;
            DWORD card_state = 0U;
            DWORD protocol = SCARD_PROTOCOL_UNDEFINED;
            std::array<BYTE, 32U> atr{};
            DWORD atr_length = static_cast<DWORD>(atr.size());
            CHECK(status(card, reinterpret_cast<LPWSTR>(&actual_reader), &reader_length,
                          &card_state, &protocol, atr.data(), &atr_length) == SCARD_S_SUCCESS);
            CHECK(actual_reader != nullptr && reader_length == reader.size() + 2U);
            if (actual_reader != nullptr) {
                CHECK(std::wstring(actual_reader) == reader);
                CHECK(free_memory(context, actual_reader) == SCARD_S_SUCCESS);
            }
            CHECK(card_state == SCARD_SPECIFIC && protocol == SCARD_PROTOCOL_T1 &&
                  atr_length == 2U && atr[0] == 0x3bU && atr[1] == 0x00U);
            const std::array<BYTE, 5U> apdu{0x00U, 0xa4U, 0x00U, 0x00U, 0x00U};
            const SCARD_IO_REQUEST send_pci{SCARD_PROTOCOL_T1, sizeof(SCARD_IO_REQUEST)};
            std::array<BYTE, 32U> response{};
            DWORD response_length = static_cast<DWORD>(response.size());
            CHECK(transmit(card, &send_pci, apdu.data(), static_cast<DWORD>(apdu.size()),
                            nullptr, response.data(), &response_length) == SCARD_S_SUCCESS);
            CHECK(response_length == apdu.size() &&
                  std::memcmp(response.data(), apdu.data(), apdu.size()) == 0);
            CHECK(disconnect(card, SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
        }
        if (names != nullptr) CHECK(free_memory(context, names) == SCARD_S_SUCCESS);
        CHECK(release(context) == SCARD_S_SUCCESS);
        CHECK(is_valid(context) == ERROR_INVALID_HANDLE);
    }
    CHECK(server.card_connects.load() == 1U && server.transmits.load() == 1U &&
          server.disconnects.load() == 1U && server.statuses.load() >= 1U &&
          !server.bad_request.load());

    // Cooperative shutdown while one real DLL call is awaiting its IPC
    // deadline. This deliberately does NOT use TerminateThread and is not
    // evidence about TvCas's five-second forced-thread-termination fallback.
    const std::wstring reader = L"PX4 userland (" +
        std::wstring(instance.begin(), instance.end()) + L")";
    SCARDCONTEXT stalled_context = 0U;
    CHECK(establish(SCARD_SCOPE_USER, nullptr, nullptr, &stalled_context) == SCARD_S_SUCCESS);
    if (stalled_context != 0U) {
        SCARDHANDLE stalled_card = 0U;
        DWORD active = SCARD_PROTOCOL_UNDEFINED;
        CHECK(connect(stalled_context, reader.c_str(), SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
                       &stalled_card, &active) == SCARD_S_SUCCESS);
        if (stalled_card != 0U) {
            const unsigned before = server.transmits.load();
            server.behavior.store(TransmitBehavior::stall);
            std::atomic<LONG> transmitted{SCARD_F_INTERNAL_ERROR};
            const auto started = Clock::now();
            std::thread sender([&]() noexcept {
                const BYTE apdu = 0U;
                const SCARD_IO_REQUEST pci{SCARD_PROTOCOL_T1, sizeof(SCARD_IO_REQUEST)};
                std::array<BYTE, 32U> response{};
                DWORD length = static_cast<DWORD>(response.size());
                transmitted.store(transmit(stalled_card, &pci, &apdu, 1U, nullptr,
                                            response.data(), &length));
            });
            CHECK(wait_for(server.transmits, before + 1U));
            // This call runs concurrently with the sender and must wait for
            // that operation rather than destroying its owning transport.
            const LONG released = release(stalled_context);
            sender.join();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - started).count();
            std::printf("cooperative stalled-transmit/context release: %lld ms\n",
                         static_cast<long long>(elapsed));
            CHECK(released == SCARD_S_SUCCESS && transmitted.load() == SCARD_E_TIMEOUT);
            CHECK(elapsed <= 8000);
            CHECK(server.transmits.load() == before + 1U);
            CHECK(is_valid(stalled_context) == ERROR_INVALID_HANDLE);
            CHECK(disconnect(stalled_card, SCARD_LEAVE_CARD) == SCARD_E_INVALID_HANDLE);
            stalled_context = 0U;
        }
        if (stalled_context != 0U) CHECK(release(stalled_context) == SCARD_S_SUCCESS);
    }
    server.behavior.store(TransmitBehavior::echo);
    // An explicit new context/card must work after cooperative teardown. No
    // previous APDU or card connection is silently retried by the DLL.
    SCARDCONTEXT fresh_context = 0U;
    CHECK(establish(SCARD_SCOPE_USER, nullptr, nullptr, &fresh_context) == SCARD_S_SUCCESS);
    if (fresh_context != 0U) {
        SCARDHANDLE fresh_card = 0U;
        DWORD active = SCARD_PROTOCOL_UNDEFINED;
        CHECK(connect(fresh_context, reader.c_str(), SCARD_SHARE_SHARED, SCARD_PROTOCOL_T1,
                       &fresh_card, &active) == SCARD_S_SUCCESS);
        CHECK(fresh_card != 0U && active == SCARD_PROTOCOL_T1);
        if (fresh_card != 0U) CHECK(disconnect(fresh_card, SCARD_LEAVE_CARD) == SCARD_S_SUCCESS);
        CHECK(release(fresh_context) == SCARD_S_SUCCESS);
    }
    CHECK(server.card_connects.load() == 3U && server.transmits.load() == 2U &&
          server.disconnects.load() == 2U && !server.bad_request.load());
    CHECK(::SetEnvironmentVariableW(L"PX4_WINSCARD_RUNTIME_DIR",
                                    old_missing ? nullptr : old_runtime.data()) != 0);
    // The production proxy intentionally pins its module once the native
    // provider is loaded; this releases only our LoadLibrary reference.
    ::FreeLibrary(dll);
}

}  // namespace

int main()
{
    test_invalid_paths_and_instances();
    test_discovery_filters_and_sorting();
    test_typed_round_trip_and_lifetime();
    test_connect_errors();
    test_terminal_errors_do_not_replay();
    test_reader_and_owner_status();
    test_discovery_entry_and_time_bounds();
    test_untrusted_security_fails_closed();
    test_reparse_traversal_rejected();
    test_production_dll_tvcas_sequence();
    if (failures != 0) {
        std::fprintf(stderr, "Windows WinSCard client tests: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("Windows WinSCard client tests: PASS");
    return 0;
}

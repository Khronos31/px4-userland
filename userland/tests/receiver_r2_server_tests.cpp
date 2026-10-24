// SPDX-License-Identifier: GPL-2.0-only
// R2 actual-socket teardown evidence using the common UDTV control records.
#include "px4/card_service.h"
#include "px4/control_server.h"
#include "px4/tuner_service.h"
#include "strict_frequency_profiles.h"

#include "dtv/userland/records.h"
#include "dtv/userland/wire.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;
using namespace px4::userland::ipc::posix;
namespace wire = dtv::userland::wire;
using dtv::userland::ByteView;
using dtv::userland::MutableByteView;

#define SERVER_CHECK(condition)                                                       \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "receiver R2 server check failed at %d: %s\n",       \
                         __LINE__, #condition);                                        \
            return false;                                                              \
        }                                                                              \
    } while (false)

constexpr std::chrono::milliseconds kWaitBound{10000};

class Gate final {
public:
    void arm()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entered_ = false;
        open_ = false;
        armed_ = true;
    }
    void pass()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!armed_) return;
        armed_ = false;
        entered_ = true;
        changed_.notify_all();
        changed_.wait_for(lock, kWaitBound, [&]() noexcept { return open_; });
    }
    bool wait_entered()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, kWaitBound, [&]() noexcept { return entered_; });
    }
    void open()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        armed_ = false;
        open_ = true;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool armed_ = false;
    bool entered_ = false;
    bool open_ = false;
};

class CardBackend final : public CardServiceBackend {
public:
    Result<void> set_power(bool) noexcept override { return Result<void>::success(); }
    Result<void> initialize_uart() noexcept override { return Result<void>::success(); }
    Result<bool> detect_card() noexcept override { return Result<bool>::success(false); }
};

class FakeCardSession final : public CardProtocolSession {
public:
    Result<void> initialize() noexcept override { return Result<void>::success(); }
    Result<std::size_t> transmit(ByteView, MutableByteView) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    bool initialized() const noexcept override { return false; }
    const CardAtr& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override {}

private:
    CardAtr atr_{};
};

class Nonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, kNonceLength>> generate() noexcept override
    {
        std::array<std::uint8_t, kNonceLength> value{};
        value[0U] = next_.fetch_add(1U);
        return Result<std::array<std::uint8_t, kNonceLength>>::success(value);
    }

private:
    std::atomic<std::uint8_t> next_{1U};
};

class Time final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override { return now_.load(); }
    void sleep_ms(std::uint32_t) noexcept override {}
    std::atomic<std::uint64_t> now_{0U};
};

class TunerBackend final : public TunerServiceBackend {
public:
    std::uint8_t receiver_count() const noexcept override { return 2U; }
    bool receiver_supports(std::uint8_t receiver, System system) const noexcept override
    {
        return receiver < 2U && system == System::ISDB_S;
    }
    TunerStrictProfile strict_profile(std::uint8_t receiver,
                                      System system) const noexcept override
    {
        return receiver_supports(receiver, system) ? rt710_satellite_strict_profile()
                                                   : TunerStrictProfile{};
    }
    Result<void> open_receiver(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t,
                                  std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<TunerHzProgramming> tune_terrestrial_hz(std::uint8_t, std::uint64_t,
                                                   std::uint32_t) noexcept override
    {
        return Result<TunerHzProgramming>::failure(Error::UNSUPPORTED);
    }
    Result<TunerHzProgramming> tune_satellite_hz(std::uint8_t receiver, std::uint64_t hz,
                                                 std::uint32_t) noexcept override
    {
        if (receiver == 1U) {
            receiver1_tune_inflight.store(true);
            tune_gate.pass();
            receiver1_tune_inflight.store(false);
        }
        TunerHzProgramming result{};
        result.programmed_hz.present = true;
        result.programmed_hz.value = hz;
        return Result<TunerHzProgramming>::success(result);
    }
    Result<bool> is_locked(std::uint8_t, System) noexcept override
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
    Result<void> close_receiver(std::uint8_t receiver) noexcept override
    {
        if (receiver < close_entries.size()) close_entries[receiver].fetch_add(1U);
        return Result<void>::success();
    }
    Result<void> begin_tune_power(std::uint8_t, System, std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> commit_tune_power(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> rollback_tune_power(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    void mark_receiver_disconnected(std::uint8_t) noexcept override {}
    Result<void> shutdown() noexcept override { return Result<void>::success(); }
    Result<void> start_capture(std::uint8_t, System) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> stop_capture(std::uint8_t, System) noexcept override
    {
        return Result<void>::success();
    }

    Gate tune_gate;
    std::atomic<bool> receiver1_tune_inflight{false};
    std::array<std::atomic<std::size_t>, 2U> close_entries{};
};

template <std::size_t N>
bool set_text(wire::WString<N>& out, const char* text) noexcept
{
    const std::size_t size = std::strlen(text);
    if (size > N) return false;
    out.length = static_cast<std::uint16_t>(size);
    std::memcpy(out.bytes.data(), text, size);
    return true;
}

ControlContext make_context()
{
    ControlContext context{};
    for (std::size_t index = 0U; index < context.epoch.size(); ++index)
        context.epoch[index] = static_cast<std::uint8_t>(0x30U + index * 3U);
    wire::ListResponse& list = context.list;
    list.epoch = context.epoch;
    (void)set_text(list.producer.product, "px4-userland-test");
    (void)set_text(list.producer.version, "1.0.0-test");
    list.topology.state = wire::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = 1U;
    list.functions.count = 1U;
    wire::UsbFunction& function = list.functions.items[0U];
    function.id = 0U;
    function.vid = 0x1234U;
    function.pid = 0x5678U;
    function.mode = wire::Mode::runtime;
    function.serial_state = wire::SerialState::absent;
    function.identification_reason = wire::IdentificationReason::model_unconfirmed;
    function.present = true;
    list.receivers.present = true;
    list.receivers.value.count = 2U;
    for (std::size_t index = 0U; index < 2U; ++index) {
        wire::ReceiverDescriptor& receiver = list.receivers.value.items[index];
        receiver.id = static_cast<std::uint16_t>(index);
        receiver.systems.mask = 0x02U;
        receiver.binding.present = true;
        receiver.binding.value = wire::Binding{0U, static_cast<std::uint16_t>(index)};
        context.receiver_index[index] = static_cast<std::uint8_t>(index);
    }
    list.readers.present = true;
    list.readers.value.count = 0U;
    return context;
}

struct Reply final {
    wire::Header header{};
    std::vector<std::uint8_t> payload;
    ByteView view() const noexcept { return ByteView{payload.data(), payload.size()}; }
};

class ReplyCollector final : public wire::FrameHandler {
public:
    Result<void> on_frame(const wire::Frame& frame) noexcept override
    {
        value.header = frame.header;
        value.payload.assign(frame.payload.data, frame.payload.data + frame.payload.size);
        ready = true;
        return Result<void>::success();
    }
    Reply value;
    bool ready = false;
};

class V1Client final {
public:
    static std::unique_ptr<V1Client> connect(const EndpointConfig& endpoint)
    {
        auto stream = SocketStream::connect(endpoint, Timeout{2000U});
        if (!stream) return nullptr;
        return std::unique_ptr<V1Client>(new V1Client(std::move(stream.value())));
    }

    template <typename Record>
    static std::vector<std::uint8_t> encode(
        const Record& record,
        Result<std::size_t> (*encoder)(const Record&, MutableByteView) noexcept)
    {
        std::array<std::uint8_t, wire::kMaxControlPayload> bytes{};
        const auto size = encoder(record, MutableByteView{bytes.data(), bytes.size()});
        if (!size) return {};
        return std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + size.value());
    }

    template <typename Record>
    bool request(std::uint16_t opcode, const Record& record,
                 Result<std::size_t> (*encoder)(const Record&, MutableByteView) noexcept,
                 Reply& reply)
    {
        const auto payload = encode(record, encoder);
        if (payload.empty()) return false;
        std::array<std::uint8_t, wire::kHeaderSize + wire::kMaxControlPayload> bytes{};
        wire::Header header;
        header.kind = wire::Kind::request;
        header.opcode = opcode;
        header.request_id = ++request_id_;
        header.payload_length = static_cast<std::uint32_t>(payload.size());
        const auto framed = wire::encode_frame(
            header, ByteView{payload.data(), payload.size()},
            MutableByteView{bytes.data(), bytes.size()});
        if (!framed || !stream_.write_all(ByteView{bytes.data(), framed.value()}, Timeout{2000U}))
            return false;
        return read_reply(reply) && reply.header.request_id == request_id_ &&
               reply.header.opcode == opcode;
    }

    bool hello()
    {
        wire::HelloRequest request;
        request.min = wire::Version{1U, 0U};
        request.max = wire::Version{1U, 0U};
        Reply reply;
        if (!this->request(wire::kOpcodeHello, request, &wire::encode_hello_request, reply) ||
            reply.header.kind != wire::Kind::response)
            return false;
        const auto response = wire::decode_hello_response(reply.view());
        if (!response) return false;
        epoch = response.value().epoch;
        return true;
    }

    bool acquire(std::uint16_t receiver, Reply& reply)
    {
        wire::AcquireRequest request;
        request.epoch = epoch;
        request.receiver_id = receiver;
        return this->request(wire::kOpcodeAcquire, request, &wire::encode_acquire_request, reply);
    }

    bool acquire_lease(std::uint16_t receiver, std::uint64_t& lease)
    {
        Reply reply;
        if (!acquire(receiver, reply) || reply.header.kind != wire::Kind::response) return false;
        const auto response = wire::decode_acquire_response(reply.view());
        if (!response || response.value().epoch != epoch) return false;
        lease = response.value().lease_id;
        return true;
    }

    bool tune(std::uint64_t lease, std::uint64_t frequency, Reply& reply,
              std::uint32_t timeout_ms = 1000U)
    {
        wire::TuneRequest request;
        request.epoch = epoch;
        request.lease_id = lease;
        request.system = wire::System::isdb_s;
        request.frequency_hz = frequency;
        request.selection = wire::Selection::slot;
        request.selection_value = 0U;
        request.lnb_voltage.present = true;
        request.lnb_voltage.value = 0U;
        request.timeout_ms = timeout_ms;
        return this->request(wire::kOpcodeTune, request, &wire::encode_tune_request, reply);
    }

    void close() noexcept { stream_.close(); }
    wire::Uuid epoch{};

private:
    explicit V1Client(SocketStream stream) noexcept
        : stream_(std::move(stream)), receiver_(MutableByteView{storage_.data(), storage_.size()})
    {
    }

    bool read_reply(Reply& reply)
    {
        const auto deadline = std::chrono::steady_clock::now() + kWaitBound;
        collector_.ready = false;
        while (!collector_.ready) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::array<std::uint8_t, 4096U> bytes{};
            const auto read = stream_.read_some(MutableByteView{bytes.data(), bytes.size()},
                                                Timeout{20U});
            if (!read) {
                if (read.error() == Error::DISCONNECTED) return false;
                continue;
            }
            const auto fed = receiver_.feed(ByteView{bytes.data(), read.value()}, collector_);
            if (!fed) return false;
        }
        reply = std::move(collector_.value);
        return true;
    }

    SocketStream stream_;
    std::array<std::uint8_t, wire::kMaxControlPayload> storage_{};
    wire::FrameReceiver receiver_;
    ReplyCollector collector_;
    std::uint32_t request_id_ = 0U;
};

template <typename T>
bool wait_for(std::chrono::milliseconds bound, T predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + bound;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

}  // namespace

int main()
{
    const auto base = std::filesystem::temp_directory_path() /
                      ("px4-r2-server-" + std::to_string(
                          std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::create_directories(base, error);
    if (error || ::chmod(base.c_str(), 0700) != 0) return 1;
    const std::string instance = "00001205000999";
    const EndpointConfig endpoint{base.c_str(), instance.c_str(), kControlEndpointName,
                                  EndpointAccess::private_user};

    CardBackend card_backend;
    FakeCardSession card_session;
    CardService card(card_backend, card_session);
    TunerBackend tuner_backend;
    Nonce nonce;
    Time time;
    TunerService tuner(tuner_backend, nonce, time);
    if (!tuner.configure_strict_capabilities()) {
        std::fprintf(stderr, "strict capabilities setup failed\n");
        std::filesystem::remove_all(base, error);
        return 1;
    }
    ControlContext context = make_context();
    if (wire::validate_list_response(context.list) != Error::OK) {
        std::fprintf(stderr, "control context is invalid\n");
        std::filesystem::remove_all(base, error);
        return 1;
    }
    auto created = PosixControlServer::create(endpoint, card, tuner, context);
    if (!created) {
        std::fprintf(stderr, "server create failed: %s\n", error_string(created.error()));
        std::filesystem::remove_all(base, error);
        return 1;
    }
    std::unique_ptr<PosixControlServer> server = std::move(created.value());
    std::atomic<bool> stop{false};
    std::atomic<bool> server_ok{true};
    std::thread poller([&]() {
        while (!stop.load()) {
            const auto polled = server->poll_once(Timeout{20U});
            if (!polled) {
                server_ok.store(false);
                return;
            }
        }
    });

    bool ok = true;
    bool observation_recorded = false;
    bool blocker_invalidated_first = false;
    std::thread blocker_request;
    auto owner = V1Client::connect(endpoint);
    auto blocker = V1Client::connect(endpoint);
    if (!owner || !blocker || !owner->hello() || !blocker->hello()) {
        std::fprintf(stderr, "v1 client connect/HELLO failed\n");
        ok = false;
    } else {
        std::uint64_t owner_lease = 0U;
        std::uint64_t blocker_lease = 0U;
        if (!owner->acquire_lease(0U, owner_lease) ||
            !blocker->acquire_lease(1U, blocker_lease)) {
            std::fprintf(stderr, "v1 ACQUIRE failed\n");
            ok = false;
        } else {
            Reply tuned;
            if (!owner->tune(owner_lease, 1318000000U, tuned) ||
                tuned.header.kind != wire::Kind::response) {
                std::fprintf(stderr, "owner strict v1 TUNE failed\n");
                ok = false;
            } else {
                const bool current0 = wait_for(kWaitBound, [&]() {
                    const auto metadata = tuner.tune_metadata(0U);
                    return metadata && metadata.value().current_valid;
                });
                if (!current0) {
                    std::fprintf(stderr, "owner tune did not reach current\n");
                    ok = false;
                } else {
                    tuner_backend.tune_gate.arm();
                    blocker_request = std::thread([&]() {
                        Reply response;
                        (void)blocker->tune(blocker_lease, 1320000000U, response, 15000U);
                    });
                    const bool blocker_held = tuner_backend.tune_gate.wait_entered();
                    if (!blocker_held) {
                        std::fprintf(stderr, "blocker tune was not held\n");
                        ok = false;
                    } else {
                        const auto before = tuner.tune_metadata(0U);
                        if (!(before && before.value().current_valid) ||
                            tuner_backend.close_entries[0U].load() != 0U) {
                            std::fprintf(stderr, "blocker supplied invalidation\n");
                            ok = false;
                        }
                        owner->close();
                        const bool disconnected = wait_for(kWaitBound, [&]() {
                            return server->connection_count() == 1U;
                        });
                        const auto revoked = tuner.tune_metadata(0U);
                        blocker_invalidated_first =
                            disconnected && revoked && !revoked.value().current_valid &&
                            tuner_backend.receiver1_tune_inflight.load() &&
                            tuner_backend.close_entries[0U].load() == 0U;
                        observation_recorded = true;
                        if (!blocker_invalidated_first) {
                            std::fprintf(stderr,
                                         "socket removal did not invalidate current before "
                                         "release (disconnected=%d inflight=%d close0=%zu)\n",
                                         disconnected ? 1 : 0,
                                         tuner_backend.receiver1_tune_inflight.load() ? 1 : 0,
                                         tuner_backend.close_entries[0U].load());
                            ok = false;
                        }
                    }
                }
            }
        }
    }
    tuner_backend.tune_gate.open();
    if (blocker_request.joinable()) blocker_request.join();
    if (observation_recorded) {
        const bool released = wait_for(kWaitBound, [&]() {
            return tuner_backend.close_entries[0U].load() == 1U;
        });
        const auto metadata = tuner.tune_metadata(0U);
        if (!released || !metadata || metadata.value().current_valid ||
            !metadata.value().last_successful_valid ||
            metadata.value().last_successful.requested_hz != 1318000000U) {
            std::fprintf(stderr, "late tune publication or release cleanup mismatch\n");
            ok = false;
        }
    }
    if (owner) owner->close();
    if (blocker) blocker->close();
    stop.store(true);
    if (poller.joinable()) poller.join();
    (void)server->shutdown();
    (void)card.shutdown();
    std::filesystem::remove_all(base, error);
    return (ok && observation_recorded && blocker_invalidated_first && server_ok.load()) ? 0 : 1;
}

// SPDX-License-Identifier: GPL-2.0-only
//
// UDTV control endpoint of the actual product server: real AF_UNIX sockets,
// the real worker lanes and the same TunerService/receiver engine, with mock
// receiver and card backends only.  The small client below is test code that
// uses the common codecs; it is not a shipping client.
#include "control_server_test_access.h"
#include "control_workers.h"
#include "px4/card_service.h"
#if defined(PX4_IFD_NEW_WIRE_PROOF)
#include "px4/pcsc_ifd_adapter.h"
#endif
#include "px4/control_context.h"
#include "px4/control_server.h"
#include "px4/posix_tuner_nonce.h"
#include "px4/q3u4_stream.h"
#include "px4/tuner_service.h"
#include "px4_ts_core.h"
#include "px4_ts_posix.h"
#include "strict_frequency_profiles.h"
#include "runtime_identity.h"

#include "dtv/userland/control_connection.h"
#include "dtv/userland/identity.h"
#include "dtv/userland/json.h"
#include "dtv/userland/records.h"
#include "dtv/userland/wire.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <grp.h>
#include <memory>
#include <optional>
#include <mutex>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;
using namespace px4::userland::ipc::posix;
using namespace px4::userland::cli;
namespace wire = dtv::userland::wire;
namespace identity = dtv::userland::identity;

#define V1_CHECK(condition)                                                           \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "runtime v1 check failed at %d: %s\n", __LINE__,     \
                         #condition);                                                  \
            return false;                                                              \
        }                                                                              \
    } while (false)

constexpr std::chrono::milliseconds kWaitBound{5000};

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

// ---------------------------------------------------------------------------
// Backends

class Gate final {
public:
    void arm()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        armed_ = true;
        entered_ = false;
        open_ = false;
    }
    void pass()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!armed_) return;
        armed_ = false;
        entered_ = true;
        changed_.notify_all();
        changed_.wait_for(lock, std::chrono::seconds(10), [&]() noexcept { return open_; });
    }
    bool wait_entered()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, kWaitBound, [&]() noexcept { return entered_; });
    }
    void open()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = true;
        armed_ = false;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool armed_ = false;
    bool entered_ = false;
    bool open_ = false;
};

// Counts every card-side call: generic STATUS must never reach any of them.
class CardBackend final : public CardServiceBackend {
public:
    Result<void> set_power(bool) noexcept override
    {
        power_calls.fetch_add(1U);
        return Result<void>::success();
    }
    Result<void> initialize_uart() noexcept override
    {
        uart_calls.fetch_add(1U);
        return Result<void>::success();
    }
    Result<bool> detect_card() noexcept override
    {
        detect_calls.fetch_add(1U);
        return Result<bool>::success(true);
    }
    std::atomic<std::size_t> power_calls{0U};
    std::atomic<std::size_t> uart_calls{0U};
    std::atomic<std::size_t> detect_calls{0U};
};

class CardSession final : public CardProtocolSession {
public:
    // Only the dedicated successful-APDU proof enables a synthetic card.
    // All pre-existing fixtures keep the original no-ATR/UNSUPPORTED behavior.
    void enable_mock_apdu() noexcept { mock_apdu_ = true; }
    Result<void> initialize() noexcept override
    {
        initialize_calls.fetch_add(1U);
        if (mock_apdu_) {
            ready_ = true;
            atr_.length = 2U;
            atr_.bytes[0U] = 0x3bU;
            atr_.bytes[1U] = 0x00U;
        }
        return Result<void>::success();
    }
    Result<std::size_t> transmit(ByteView, MutableByteView response) noexcept override
    {
        transmit_calls.fetch_add(1U);
        if (!mock_apdu_ || !ready_ || response.data == nullptr || response.size < 2U)
            return Result<std::size_t>::failure(Error::UNSUPPORTED);
        response.data[0U] = 0x90U;
        response.data[1U] = 0x00U;
        return Result<std::size_t>::success(2U);
    }
    bool initialized() const noexcept override { return ready_; }
    const CardAtr& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override { ready_ = false; }
    std::atomic<std::size_t> initialize_calls{0U};
    std::atomic<std::size_t> transmit_calls{0U};

private:
    bool mock_apdu_ = false;
    bool ready_ = false;
    CardAtr atr_{};
};

class Nonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, kNonceLength>> generate() noexcept override
    {
        std::array<std::uint8_t, kNonceLength> value{};
        value.fill(0x5aU);
        value[0U] = next_.fetch_add(1U);
        return Result<std::array<std::uint8_t, kNonceLength>>::success(value);
    }

private:
    std::atomic<std::uint8_t> next_{1U};
};

class LeaseIds final : public TunerLeaseIdSource {
public:
    Result<std::uint64_t> next() noexcept override
    {
        return Result<std::uint64_t>::success(
            0x91b4c2d87730e5a6ULL + next_.fetch_add(1U));
    }
    std::uint64_t calls() const noexcept { return next_.load(); }

private:
    std::atomic<std::uint64_t> next_{0U};
};

// Manually advanced engine clock; sleeps advance it, so lock polls finish.
class Clock final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override { return now.load(); }
    void sleep_ms(std::uint32_t milliseconds) noexcept override { now.fetch_add(milliseconds); }
    std::atomic<std::uint64_t> now{1000U};
};

// PX-Q3U4 receiver layout (0/1/4/5 ISDB-S, 2/3/6/7 ISDB-T) with the reviewed
// native strict profiles, so TUNE reaches the strict Hz entrance.
class Backend final : public TunerServiceBackend {
public:
    TunerStrictProfile strict_profile(std::uint8_t receiver,
                                      System system) const noexcept override
    {
        if (!receiver_supports(receiver, system)) return {};
        return system == System::ISDB_T ? r850_terrestrial_strict_profile()
                                        : rt710_satellite_strict_profile();
    }
    bool requires_terrestrial_lock_settle() const noexcept override { return false; }
    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        open_calls[receiver].fetch_add(1U);
        return Result<void>::success();
    }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t, std::uint32_t) noexcept override
    {
        khz_calls.fetch_add(1U);
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t, std::uint32_t) noexcept override
    {
        khz_calls.fetch_add(1U);
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<TunerHzProgramming> tune_terrestrial_hz(std::uint8_t receiver, std::uint64_t hz,
                                                   std::uint32_t) noexcept override
    {
        return program(receiver, hz);
    }
    Result<TunerHzProgramming> tune_satellite_hz(std::uint8_t receiver, std::uint64_t hz,
                                                 std::uint32_t) noexcept override
    {
        return program(receiver, hz);
    }
    Result<bool> is_locked(std::uint8_t, System) noexcept override
    {
        lock_calls.fetch_add(1U);
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
        if (receiver == gated_close_receiver.load()) close_gate.pass();
        close_calls[receiver].fetch_add(1U);
        return close_error.load() == Error::OK ? Result<void>::success()
                                               : Result<void>::failure(close_error.load());
    }
    Result<void> begin_tune_power(std::uint8_t receiver, System,
                                  std::uint8_t voltage) noexcept override
    {
        power_calls[receiver].fetch_add(1U);
        last_voltage.store(voltage);
        return Result<void>::success();
    }
    Result<void> start_capture(std::uint8_t, System) noexcept override
    {
        capture_calls.fetch_add(1U);
        return Result<void>::success();
    }
    Result<void> stop_capture(std::uint8_t, System) noexcept override
    {
        return Result<void>::success();
    }

    Result<TunerHzProgramming> program(std::uint8_t receiver, std::uint64_t hz) noexcept
    {
        if (receiver == gated_tune_receiver.load()) tune_gate.pass();
        hz_calls[receiver].fetch_add(1U);
        last_hz[receiver].store(hz);
        TunerHzProgramming programming{};
        // A deterministic driver command value distinct from the request.
        programming.programmed_hz.present = true;
        programming.programmed_hz.value = (hz / 1000U) * 1000U;
        return Result<TunerHzProgramming>::success(programming);
    }

    std::array<std::atomic<std::size_t>, kReceiverCount> open_calls{};
    std::array<std::atomic<std::size_t>, kReceiverCount> close_calls{};
    std::array<std::atomic<std::size_t>, kReceiverCount> hz_calls{};
    std::array<std::atomic<std::size_t>, kReceiverCount> power_calls{};
    std::array<std::atomic<std::uint64_t>, kReceiverCount> last_hz{};
    std::atomic<std::size_t> khz_calls{0U};
    std::atomic<std::size_t> lock_calls{0U};
    std::atomic<std::size_t> capture_calls{0U};
    std::atomic<std::uint8_t> last_voltage{0xffU};
    std::atomic<Error> close_error{Error::OK};
    std::atomic<std::uint8_t> gated_tune_receiver{0xffU};
    std::atomic<std::uint8_t> gated_close_receiver{0xffU};
    Gate tune_gate;
    Gate close_gate;
};

std::size_t total(const std::array<std::atomic<std::size_t>, kReceiverCount>& counts)
{
    std::size_t sum = 0U;
    for (const auto& count : counts) sum += count.load();
    return sum;
}

// Minimal USB bottom for the real Q3U4 data plane: push tagged packets and
// let the production pump/demux/store turn them into plain 188-byte TS.
class TaggedFakeTransport final : public Transport {
public:
    Result<std::size_t> bulk_read(std::uint8_t, MutableByteView, Timeout,
                                  BulkReadObservation*) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    Result<std::size_t> bulk_write(std::uint8_t, ByteView, Timeout) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    Result<void> start_stream(const StreamConfig&) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++starts;
        active = true;
        return Result<void>::success();
    }
    Result<StreamEvent> wait_stream(Timeout timeout) noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex);
        (void)changed.wait_for(lock, std::chrono::milliseconds(timeout.milliseconds),
                               [&]() noexcept { return !events.empty() || stopped; });
        if (stopped) return Result<StreamEvent>::failure(Error::DISCONNECTED);
        if (events.empty()) return Result<StreamEvent>::failure(Error::TIMEOUT);
        current = std::move(events.front());
        events.pop_front();
        return Result<StreamEvent>::success(
            StreamEvent{StreamEventKind::data, current.data(), current.size()});
    }
    Result<void> cancel_stream() noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        active = false;
        stopped = true;
        changed.notify_all();
        return Result<void>::success();
    }
    Result<void> stop_stream() noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        active = false;
        stopped = true;
        changed.notify_all();
        return Result<void>::success();
    }
    bool stream_active() const noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return active;
    }
    void push(std::vector<std::uint8_t> value) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(std::move(value));
        changed.notify_all();
    }

    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::vector<std::uint8_t>> events;
    std::vector<std::uint8_t> current;
    bool active = false;
    bool stopped = false;
    std::size_t starts = 0U;
};

// ---------------------------------------------------------------------------
// Context

class TempRuntime final {
public:
    explicit TempRuntime(const char* base_directory = nullptr)
    {
        const std::string base = base_directory == nullptr ?
            std::filesystem::temp_directory_path().string() : base_directory;
        std::string pattern = base + "/px4-v1-XXXXXX";
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

class EnvironmentRestore final {
public:
    explicit EnvironmentRestore(const char* name) : name_(name)
    {
        const char* value = std::getenv(name);
        if (value != nullptr) {
            existed_ = true;
            value_ = value;
        }
    }
    ~EnvironmentRestore() noexcept
    {
        if (existed_) (void)::setenv(name_.c_str(), value_.c_str(), 1);
        else (void)::unsetenv(name_.c_str());
    }
    EnvironmentRestore(const EnvironmentRestore&) = delete;
    EnvironmentRestore& operator=(const EnvironmentRestore&) = delete;

private:
    std::string name_;
    std::string value_;
    bool existed_ = false;
};

wire::Uuid make_epoch(std::uint8_t seed)
{
    wire::Uuid epoch{};
    for (std::size_t i = 0U; i < epoch.size(); ++i)
        epoch[i] = static_cast<std::uint8_t>(seed + i * 7U);
    return epoch;
}

DeviceObservation bridge(std::uint8_t dev, std::uint8_t address)
{
    DeviceObservation observation;
    observation.vendor_id = kQ3U4VendorId;
    observation.product_id = kQ3U4ProductId;
    observation.serial = std::string("0000120500096") + static_cast<char>('0' + dev);
    observation.location.has_bus = true;
    observation.location.bus = 3U;
    observation.location.has_address = true;
    observation.location.address = address;
    observation.location.port_count = 2U;
    observation.location.port_path[0] = 1U;
    observation.location.port_path[1] = dev;
    return observation;
}

// The product builder over a PX-Q3U4-shaped service and two observed bridges.
bool product_context(const TunerService& tuner, const wire::Uuid& epoch,
                     ControlContext& context, DeviceObservation& first,
                     DeviceObservation& second,
                     std::string_view instance = "test-instance",
                     std::optional<bool> serial_unique = std::nullopt)
{
    first = bridge(1U, 11U);
    second = bridge(2U, 12U);
    Px4ContextInput input{};
    input.model = DeviceModel::px_q3u4;
    input.base_serial = "00001205000960";
    input.bridges = {&first, &second};
    input.product_version = "9.8.7";
    input.instance = instance;
    input.epoch = epoch;
    input.serial_unique_in_scan = serial_unique;
    return static_cast<bool>(build_px4_control_context(input, tuner, context));
}

// Synthetic dispatcher coverage: sparse public IDs including 0 over legacy
// receivers 0, 1 and 2, and a known-empty reader set.  Not a P topology.
void sparse_context(ControlContext& context, const wire::Uuid& epoch)
{
    context = ControlContext{};
    context.epoch = epoch;
    wire::ListResponse& list = context.list;
    list.epoch = epoch;
    const char product[] = "synthetic";
    list.producer.product.length = sizeof(product) - 1U;
    std::memcpy(list.producer.product.bytes.data(), product, sizeof(product) - 1U);
    list.producer.version.length = 1U;
    list.producer.version.bytes[0] = '1';
    list.topology.state = wire::TopologyState::complete;
    list.functions.count = 1U;
    list.functions.items[0].id = 0U;
    list.functions.items[0].vid = 0x1234U;
    list.functions.items[0].pid = 0x5678U;
    list.functions.items[0].mode = wire::Mode::runtime;
    list.functions.items[0].serial_state = wire::SerialState::absent;
    list.functions.items[0].identification_reason = wire::IdentificationReason::model_unconfirmed;
    list.functions.items[0].present = true;
    list.receivers.present = true;
    list.receivers.value.count = 3U;
    const std::uint16_t ids[3] = {0U, 7U, 513U};
    const std::uint8_t legacy[3] = {2U, 3U, 6U};
    for (std::size_t i = 0U; i < 3U; ++i) {
        list.receivers.value.items[i].id = ids[i];
        list.receivers.value.items[i].systems.mask = 0x01U;
        list.receivers.value.items[i].binding.present = true;
        list.receivers.value.items[i].binding.value =
            wire::Binding{0U, static_cast<std::uint16_t>(i)};
        context.receiver_index[i] = legacy[i];
    }
    list.readers.present = true;
    list.readers.value.count = 0U;
}

// ---------------------------------------------------------------------------
// Test client: one UDTV control connection, one request at a time.

struct Reply final {
    wire::Header header{};
    std::vector<std::uint8_t> payload;
    ByteView view() const { return ByteView{payload.data(), payload.size()}; }
};

class Collector final : public wire::FrameHandler {
public:
    Result<void> on_frame(const wire::Frame& frame) noexcept override
    {
        Reply reply;
        reply.header = frame.header;
        reply.payload.assign(frame.payload.data, frame.payload.data + frame.payload.size);
        frames.push_back(std::move(reply));
        return Result<void>::success();
    }
    std::vector<Reply> frames;
};

class TestClient final {
public:
    explicit TestClient(SocketStream stream)
        : stream_(std::move(stream)),
          storage_(new std::array<std::uint8_t, wire::kMaxControlPayload>()),
          receiver_(MutableByteView{storage_->data(), storage_->size()})
    {
    }

    static std::unique_ptr<TestClient> connect(const EndpointConfig& endpoint)
    {
        auto stream = SocketStream::connect(endpoint, Timeout{2000U});
        if (!stream) return nullptr;
        return std::unique_ptr<TestClient>(new TestClient(std::move(stream.value())));
    }

    bool send_bytes(const std::uint8_t* data, std::size_t size)
    {
        return static_cast<bool>(stream_.write_all(ByteView{data, size}, Timeout{2000U}));
    }

    // Encodes one frame into bytes without sending it.
    static std::vector<std::uint8_t> frame(std::uint16_t opcode, std::uint32_t request_id,
                                           ByteView payload,
                                           wire::Kind kind = wire::Kind::request)
    {
        std::vector<std::uint8_t> bytes(wire::kHeaderSize + payload.size);
        wire::Header header;
        header.kind = kind;
        header.opcode = opcode;
        header.request_id = request_id;
        header.payload_length = static_cast<std::uint32_t>(payload.size);
        const auto written = wire::encode_frame(header, payload,
                                                MutableByteView{bytes.data(), bytes.size()});
        if (!written) bytes.clear();
        return bytes;
    }

    template <typename Record>
    static std::vector<std::uint8_t> encode(const Record& value,
                                            Result<std::size_t> (*encoder)(const Record&,
                                                                           MutableByteView) noexcept)
    {
        std::vector<std::uint8_t> bytes(wire::kMaxControlPayload);
        const auto size = encoder(value, MutableByteView{bytes.data(), bytes.size()});
        bytes.resize(size ? size.value() : 0U);
        return bytes;
    }

    // Reads until one frame arrives, the peer closes, or the bound expires.
    bool read_frame(Reply& out, std::chrono::milliseconds bound = kWaitBound)
    {
        const auto deadline = std::chrono::steady_clock::now() + bound;
        while (collector_.frames.empty()) {
            if (std::chrono::steady_clock::now() >= deadline || closed_) return false;
            std::array<std::uint8_t, 4096U> buffer{};
            const auto read = stream_.read_some(MutableByteView{buffer.data(), buffer.size()},
                                                Timeout{20U});
            if (!read) {
                if (read.error() == Error::DISCONNECTED) closed_ = true;
                continue;
            }
            if (!receiver_.feed(ByteView{buffer.data(), read.value()}, collector_)) return false;
        }
        out = std::move(collector_.frames.front());
        collector_.frames.erase(collector_.frames.begin());
        return true;
    }

    // True when the server closed this connection without sending a frame.
    bool closed_without_reply(std::chrono::milliseconds bound = kWaitBound)
    {
        Reply reply;
        if (read_frame(reply, bound)) return false;
        return closed_;
    }

    bool request(std::uint16_t opcode, const std::vector<std::uint8_t>& payload, Reply& out)
    {
        const auto bytes = frame(opcode, ++request_id_, ByteView{payload.data(), payload.size()});
        if (bytes.empty() || !send_bytes(bytes.data(), bytes.size())) return false;
        return read_frame(out) && out.header.request_id == request_id_ &&
               out.header.opcode == opcode;
    }

    bool hello(wire::HelloResponse* response = nullptr, std::uint64_t required = 0U)
    {
        wire::HelloRequest hello;
        hello.min = wire::Version{1U, 0U};
        hello.max = wire::Version{1U, 0U};
        hello.required_features = required;
        Reply reply;
        if (!request(wire::kOpcodeHello, encode(hello, &wire::encode_hello_request), reply) ||
            reply.header.kind != wire::Kind::response)
            return false;
        const auto decoded = wire::decode_hello_response(reply.view());
        if (!decoded) return false;
        epoch = decoded.value().epoch;
        if (response != nullptr) *response = decoded.value();
        return true;
    }

    static std::uint32_t error_code(const Reply& reply)
    {
        if (reply.header.kind != wire::Kind::error_response) return 0U;
        const auto decoded = wire::decode_error_response(reply.view());
        return decoded ? decoded.value().error.code : 0U;
    }

    bool list(wire::ListResponse& out)
    {
        wire::ListRequest request_value;
        request_value.epoch = epoch;
        Reply reply;
        if (!request(wire::kOpcodeList, encode(request_value, &wire::encode_list_request), reply) ||
            reply.header.kind != wire::Kind::response)
            return false;
        auto decoded = wire::decode_list_response(reply.view());
        if (!decoded) return false;
        out = decoded.value();
        return true;
    }

    // STATUS reply; status is decoded only for a success reply.
    bool status(const wire::Scope& scope, Reply& reply, wire::StatusResponse* status = nullptr,
                const wire::Uuid* use_epoch = nullptr)
    {
        wire::StatusRequest request_value;
        request_value.epoch = use_epoch != nullptr ? *use_epoch : epoch;
        request_value.scope = scope;
        if (!request(wire::kOpcodeStatus, encode(request_value, &wire::encode_status_request),
                     reply))
            return false;
        if (status != nullptr && reply.header.kind == wire::Kind::response) {
            auto decoded = wire::decode_status_response(reply.view());
            if (!decoded) return false;
            *status = decoded.value();
        }
        return true;
    }

    bool acquire(std::uint16_t receiver, Reply& reply, const wire::Uuid* use_epoch = nullptr)
    {
        wire::AcquireRequest request_value;
        request_value.epoch = use_epoch != nullptr ? *use_epoch : epoch;
        request_value.receiver_id = receiver;
        return request(wire::kOpcodeAcquire,
                       encode(request_value, &wire::encode_acquire_request), reply);
    }

    std::uint64_t acquire_lease(std::uint16_t receiver)
    {
        Reply reply;
        if (!acquire(receiver, reply) || reply.header.kind != wire::Kind::response) return 0U;
        const auto decoded = wire::decode_acquire_response(reply.view());
        return decoded && decoded.value().epoch == epoch ? decoded.value().lease_id : 0U;
    }

    bool release(std::uint64_t lease, Reply& reply)
    {
        wire::ReleaseRequest request_value;
        request_value.epoch = epoch;
        request_value.lease_id = lease;
        return request(wire::kOpcodeRelease,
                       encode(request_value, &wire::encode_release_request), reply);
    }

    bool tune(const wire::TuneRequest& value, Reply& reply)
    {
        return request(wire::kOpcodeTune, encode(value, &wire::encode_tune_request), reply);
    }

    bool start_stream(std::uint64_t lease, std::uint64_t revision, Reply& reply)
    {
        wire::StartRequest value;
        value.epoch = epoch;
        value.lease_id = lease;
        value.tune_revision = revision;
        return request(wire::kOpcodeStartStream,
                       encode(value, &wire::encode_start_request), reply);
    }

    bool start_stream_with_epoch(const wire::Uuid& use_epoch, std::uint64_t lease,
                                 std::uint64_t revision, Reply& reply)
    {
        wire::StartRequest value;
        value.epoch = use_epoch;
        value.lease_id = lease;
        value.tune_revision = revision;
        return request(wire::kOpcodeStartStream,
                       encode(value, &wire::encode_start_request), reply);
    }

    bool stop_stream(std::uint64_t lease, std::uint64_t stream_id, Reply& reply)
    {
        wire::StopRequest value;
        value.epoch = epoch;
        value.lease_id = lease;
        value.stream_id = stream_id;
        return request(wire::kOpcodeStopStream,
                       encode(value, &wire::encode_stop_request), reply);
    }

    // Sends a request without waiting for its reply.
    bool send_request(std::uint16_t opcode, const std::vector<std::uint8_t>& payload)
    {
        const auto bytes = frame(opcode, ++request_id_, ByteView{payload.data(), payload.size()});
        return !bytes.empty() && send_bytes(bytes.data(), bytes.size());
    }

    wire::TuneRequest terrestrial(std::uint64_t lease, std::uint64_t hz,
                                  std::uint32_t timeout_ms = 1000U) const
    {
        wire::TuneRequest value;
        value.epoch = epoch;
        value.lease_id = lease;
        value.system = wire::System::isdb_t;
        value.frequency_hz = hz;
        value.timeout_ms = timeout_ms;
        return value;
    }

    void close() { stream_.close(); }
    int fd() const { return stream_.native_handle(); }
    std::uint32_t last_request_id() const { return request_id_; }

    wire::Uuid epoch{};

private:
    SocketStream stream_;
    std::unique_ptr<std::array<std::uint8_t, wire::kMaxControlPayload>> storage_;
    wire::FrameReceiver receiver_;
    Collector collector_;
    std::uint32_t request_id_ = 0U;
    bool closed_ = false;
};

// ---------------------------------------------------------------------------
// Server fixture

struct Fixture final {
    TempRuntime runtime;
    std::string instance;
    EndpointConfig endpoint{};
    EndpointConfig stream_endpoint{};
    CardBackend card_backend;
    CardSession card_session;
    std::unique_ptr<CardService> card;
    Backend backend;
    Nonce nonce;
    LeaseIds lease_ids;
    Clock clock;
    std::unique_ptr<TunerService> tuner;
    std::unique_ptr<ControlContext> context{new ControlContext()};
    DeviceObservation first{};
    DeviceObservation second{};
    std::unique_ptr<PosixControlServer> server;
    TunerStreamControl* stream_control_value = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> server_ok{true};
    std::thread poller;

    explicit Fixture(const char* name, TunerStreamControl* stream_control = nullptr)
        : instance(name), stream_control_value(stream_control)
    {
        endpoint = EndpointConfig{runtime.path().c_str(), instance.c_str(),
                                  kControlEndpointName, EndpointAccess::private_user};
        stream_endpoint = EndpointConfig{runtime.path().c_str(), instance.c_str(),
                                         kStreamEndpointName, EndpointAccess::private_user};
        card.reset(new CardService(card_backend, card_session));
        tuner.reset(new TunerService(backend, nonce, clock, &lease_ids, nullptr,
                                     stream_control));
    }

    bool start(bool sparse = false, std::uint8_t epoch_seed = 0x10U,
               std::string_view published_instance = "test-instance",
               std::optional<bool> serial_unique = std::nullopt)
    {
        if (!runtime.valid() || !tuner->configure_strict_capabilities()) return false;
        if (sparse) {
            sparse_context(*context, make_epoch(epoch_seed));
        } else if (!product_context(*tuner, make_epoch(epoch_seed), *context, first, second,
                                    published_instance, serial_unique)) {
            return false;
        }
        auto created = PosixControlServer::create(endpoint, *card, *tuner, *context,
                                                  stream_control_value);
        if (!created) {
            std::fprintf(stderr, "server create failed: %s\n", error_string(created.error()));
            return false;
        }
        server = std::move(created.value());
        poller = std::thread([this]() {
            while (!stop.load()) {
                if (!server->poll_once(Timeout{10U})) {
                    server_ok.store(false);
                    return;
                }
            }
        });
        return true;
    }

    ~Fixture()
    {
        backend.tune_gate.open();
        backend.close_gate.open();
        stop.store(true);
        if (poller.joinable()) poller.join();
        if (server) (void)server->shutdown();
        if (card) (void)card->shutdown();
    }
};

bool card_untouched(const Fixture& f)
{
    return f.card_backend.power_calls.load() == 0U && f.card_backend.uart_calls.load() == 0U &&
           f.card_backend.detect_calls.load() == 0U &&
           f.card_session.initialize_calls.load() == 0U &&
           f.card_session.transmit_calls.load() == 0U;
}

bool contains(const std::vector<std::uint8_t>& haystack, const std::uint8_t* needle,
              std::size_t size)
{
    if (size == 0U || haystack.size() < size) return false;
    for (std::size_t i = 0U; i + size <= haystack.size(); ++i) {
        if (std::memcmp(haystack.data() + i, needle, size) == 0) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// 1. Framing and connection rules

bool test_framing_and_handshake()
{
    Fixture f("v1-framing");
    V1_CHECK(f.start());

    // HELLO split byte by byte across header and payload.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client);
        wire::HelloRequest hello;
        hello.min = wire::Version{1U, 0U};
        hello.max = wire::Version{1U, 0U};
        const auto payload = TestClient::encode(hello, &wire::encode_hello_request);
        const auto bytes = TestClient::frame(wire::kOpcodeHello, 1U,
                                             ByteView{payload.data(), payload.size()});
        for (std::size_t i = 0U; i < bytes.size(); ++i) {
            V1_CHECK(client->send_bytes(bytes.data() + i, 1U));
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        Reply reply;
        V1_CHECK(client->read_frame(reply) && reply.header.kind == wire::Kind::response &&
                 reply.header.opcode == wire::kOpcodeHello && reply.header.request_id == 1U);
        const auto decoded = wire::decode_hello_response(reply.view());
        V1_CHECK(decoded && decoded.value().epoch == make_epoch(0x10U) &&
                 decoded.value().features == 0U && decoded.value().major == 1U);
    }
    // Header in one write, payload coalesced with nothing else in a second.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client && client->hello());
        wire::ListRequest list;
        list.epoch = client->epoch;
        const auto payload = TestClient::encode(list, &wire::encode_list_request);
        const auto bytes = TestClient::frame(wire::kOpcodeList, 2U,
                                             ByteView{payload.data(), payload.size()});
        V1_CHECK(client->send_bytes(bytes.data(), 7U));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        V1_CHECK(client->send_bytes(bytes.data() + 7U, bytes.size() - 7U));
        Reply reply;
        V1_CHECK(client->read_frame(reply) && reply.header.kind == wire::Kind::response &&
                 reply.header.opcode == wire::kOpcodeList);
    }
    // A request pipelined behind an unanswered one breaks the single
    // outstanding request rule: the connection closes after the first reply.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client && client->hello());
        wire::ListRequest list;
        list.epoch = client->epoch;
        const auto payload = TestClient::encode(list, &wire::encode_list_request);
        auto bytes = TestClient::frame(wire::kOpcodeList, 2U,
                                       ByteView{payload.data(), payload.size()});
        const auto second = TestClient::frame(wire::kOpcodeList, 3U,
                                              ByteView{payload.data(), payload.size()});
        bytes.insert(bytes.end(), second.begin(), second.end());
        V1_CHECK(client->send_bytes(bytes.data(), bytes.size()));
        V1_CHECK(client->closed_without_reply());
    }
    // HELLO is required first.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client);
        wire::ListRequest list;
        list.epoch = make_epoch(0x10U);
        V1_CHECK(client->send_request(wire::kOpcodeList,
                                      TestClient::encode(list, &wire::encode_list_request)));
        V1_CHECK(client->closed_without_reply());
    }
    // Legacy PX4U magic is never interpreted: closed without a reply.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client);
        auto bytes = TestClient::frame(wire::kOpcodeHello, 1U, ByteView{nullptr, 0U});
        bytes[0] = 'P';
        bytes[1] = 'X';
        bytes[2] = '4';
        bytes[3] = 'U';
        V1_CHECK(client->send_bytes(bytes.data(), bytes.size()));
        V1_CHECK(client->closed_without_reply());
    }
    // Request ID 0 is a malformed envelope; an equal ID is a protocol error.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client && client->hello());
        auto bytes = TestClient::frame(wire::kOpcodeList, 9U, ByteView{nullptr, 0U});
        bytes[12] = bytes[13] = bytes[14] = bytes[15] = 0U;
        V1_CHECK(client->send_bytes(bytes.data(), bytes.size()));
        V1_CHECK(client->closed_without_reply());

        auto repeat = TestClient::connect(f.endpoint);
        V1_CHECK(repeat && repeat->hello());
        wire::ListRequest list;
        list.epoch = repeat->epoch;
        const auto payload = TestClient::encode(list, &wire::encode_list_request);
        const auto same = TestClient::frame(wire::kOpcodeList, repeat->last_request_id(),
                                            ByteView{payload.data(), payload.size()});
        V1_CHECK(repeat->send_bytes(same.data(), same.size()));
        V1_CHECK(repeat->closed_without_reply());
    }
    // Unknown opcode with a valid envelope is UNSUPPORTED and the connection
    // continues; a malformed payload of a served opcode closes it.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client && client->hello());
        Reply reply;
        V1_CHECK(client->request(0x0123U, std::vector<std::uint8_t>{1U, 2U}, reply));
        V1_CHECK(TestClient::error_code(reply) ==
                 static_cast<std::uint32_t>(Error::UNSUPPORTED));
        wire::ListResponse list;
        V1_CHECK(client->list(list));
        V1_CHECK(client->send_request(wire::kOpcodeList, std::vector<std::uint8_t>{1U, 2U, 3U}));
        V1_CHECK(client->closed_without_reply());
    }
    // Version and required-feature failures answer HELLO and close.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client);
        wire::HelloRequest hello;
        hello.min = wire::Version{2U, 0U};
        hello.max = wire::Version{2U, 0U};
        Reply reply;
        V1_CHECK(client->request(wire::kOpcodeHello,
                                 TestClient::encode(hello, &wire::encode_hello_request), reply));
        V1_CHECK(TestClient::error_code(reply) ==
                 static_cast<std::uint32_t>(Error::VERSION_MISMATCH));
        V1_CHECK(client->closed_without_reply());

        auto card = TestClient::connect(f.endpoint);
        V1_CHECK(card);
        // CARD_SERVICE is served now; STATE_EVENT is still unconnected and a
        // required bit outside the offered set must be refused.
        V1_CHECK(!card->hello(nullptr, wire::kFeatureStateEvent));
        V1_CHECK(card->closed_without_reply());
    }
    // Clean EOF and EOF inside a frame both end the connection; the truncated
    // request is never dispatched.
    {
        const std::size_t opens_before = total(f.backend.open_calls);
        auto clean = TestClient::connect(f.endpoint);
        auto truncated = TestClient::connect(f.endpoint);
        V1_CHECK(clean && truncated && clean->hello() && truncated->hello());
        V1_CHECK(wait_for(kWaitBound, [&]() { return f.server->connection_count() >= 2U; }));
        const std::size_t connected = f.server->connection_count();
        wire::AcquireRequest acquire;
        acquire.epoch = truncated->epoch;
        acquire.receiver_id = 2U;
        const auto payload = TestClient::encode(acquire, &wire::encode_acquire_request);
        const auto bytes = TestClient::frame(wire::kOpcodeAcquire, 9U,
                                             ByteView{payload.data(), payload.size()});
        V1_CHECK(truncated->send_bytes(bytes.data(), bytes.size() - 3U));
        clean->close();
        truncated->close();
        V1_CHECK(wait_for(kWaitBound,
                          [&]() { return f.server->connection_count() + 2U <= connected; }));
        V1_CHECK(total(f.backend.open_calls) == opens_before);
    }
    V1_CHECK(f.server_ok.load());
    V1_CHECK(card_untouched(f));
    return true;
}

// Partial response write: the request stays outstanding until its last byte
// is written, so a request sent while the reply is incomplete is refused.
struct PartialObservation final {
    std::atomic<std::size_t> partial{0U};
    Gate gate;
    static void on_partial(void* context, std::uint64_t) noexcept
    {
        auto& observation = *static_cast<PartialObservation*>(context);
        observation.partial.fetch_add(1U);
        observation.gate.pass();
    }
};

bool test_partial_response_write()
{
    Fixture f("v1-partial");
    V1_CHECK(f.start());
    PartialObservation observation;
    observation.gate.arm();
    ControlServerTestHooks hooks{};
    hooks.control_write_limit = 3U;
    hooks.control_write_partial = &PartialObservation::on_partial;
    hooks.context = &observation;
    set_control_server_test_hooks(&hooks);
    struct Reset final {
        Gate& gate;
        ~Reset()
        {
            gate.open();
            set_control_server_test_hooks(nullptr);
        }
    } reset{observation.gate};

    auto client = TestClient::connect(f.endpoint);
    V1_CHECK(client);
    wire::HelloRequest hello;
    hello.min = wire::Version{1U, 0U};
    hello.max = wire::Version{1U, 0U};
    V1_CHECK(client->send_request(wire::kOpcodeHello,
                                  TestClient::encode(hello, &wire::encode_hello_request)));
    V1_CHECK(observation.gate.wait_entered());
    wire::ListRequest list;
    list.epoch = make_epoch(0x10U);
    V1_CHECK(client->send_request(wire::kOpcodeList,
                                  TestClient::encode(list, &wire::encode_list_request)));
    observation.gate.open();
    // The partial HELLO reply may still finish, but the second request is
    // never answered and the connection closes.
    Reply reply;
    bool listed = false;
    while (client->read_frame(reply, std::chrono::milliseconds(3000))) {
        if (reply.header.opcode == wire::kOpcodeList) listed = true;
    }
    V1_CHECK(!listed);
    V1_CHECK(client->closed_without_reply(std::chrono::milliseconds(100)));

    // The same limit with a client that waits for each full reply works.
    auto patient = TestClient::connect(f.endpoint);
    V1_CHECK(patient && patient->hello());
    wire::ListResponse listed_response;
    V1_CHECK(patient->list(listed_response));
    V1_CHECK(f.server_ok.load());
    return true;
}

// ---------------------------------------------------------------------------
// 2. HELLO -> LIST -> STATUS on one connection

bool test_list_status_one_context()
{
    Fixture f("v1-status");
    V1_CHECK(f.start());
    auto client = TestClient::connect(f.endpoint);
    V1_CHECK(client);
    wire::HelloResponse hello;
    V1_CHECK(client->hello(&hello));
    std::unique_ptr<wire::ListResponse> list(new wire::ListResponse());
    V1_CHECK(client->list(*list));
    V1_CHECK(list->epoch == hello.epoch);
    V1_CHECK(wire::validate_list_response(*list) == dtv::userland::Error::OK);
    // Real P context from the builder: PX-Q3U4 two bridges, eight receivers,
    // one card reader, exact descriptor membership.
    V1_CHECK(list->functions.count == 2U && list->receivers.present &&
             list->receivers.value.count == 8U && list->readers.present &&
             list->readers.value.count == 1U && list->readers.value.items[0].id == 0U &&
             list->readers.value.items[0].binding.present &&
             list->readers.value.items[0].binding.value.function_id == 0U &&
             list->readers.value.items[0].binding.value.local_id == 0U);
    V1_CHECK(list->topology.state == wire::TopologyState::complete);
    V1_CHECK(list->enclosure.present && list->enclosure.value.identity.serial.present &&
             list->enclosure.value.identity.uniqueness == wire::Uniqueness::unknown);
    V1_CHECK(list->functions.items[1].location.usb_path.present &&
             std::string(list->functions.items[1].location.usb_path.value.bytes.data(),
                         list->functions.items[1].location.usb_path.value.length) == "3-1.2");
    for (std::uint16_t id = 0U; id < 8U; ++id) {
        const wire::ReceiverDescriptor& receiver = list->receivers.value.items[id];
        const bool satellite = id == 0U || id == 1U || id == 4U || id == 5U;
        V1_CHECK(receiver.id == id && receiver.binding.present &&
                 receiver.binding.value.function_id == id / 4U &&
                 receiver.binding.value.local_id == id % 4U &&
                 receiver.systems.mask == (satellite ? 0x02U : 0x01U) &&
                 receiver.capabilities.measurement_mask == 0U &&
                 ((receiver.capabilities.controls & wire::kControlBitLnb) != 0U) == satellite);
    }
    V1_CHECK(std::string(list->producer.version.bytes.data(), list->producer.version.length) ==
             "9.8.7");

    std::unique_ptr<wire::StatusResponse> status(new wire::StatusResponse());
    Reply reply;
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::enclosure, 0U}, reply, status.get()));
    V1_CHECK(reply.header.kind == wire::Kind::response);
    V1_CHECK(status->context.epoch == hello.epoch && status->context.instance.present &&
             status->context.state == wire::ContextState::ready);
    V1_CHECK(std::string(status->context.instance.value.bytes.data(),
                         status->context.instance.value.length) == "test-instance");
    V1_CHECK(status->snapshot.sequence > list->snapshot.sequence);
    wire::StatusContext status_context;
    status_context.list = list.get();
    V1_CHECK(wire::validate_status_response(*status, status_context) == dtv::userland::Error::OK);
    V1_CHECK(status->receivers.present && status->receivers.value.count == 8U);
    for (std::size_t i = 0U; i < 8U; ++i) {
        const wire::ReceiverStatus& receiver = status->receivers.value.items[i];
        V1_CHECK(receiver.present && receiver.state == wire::ReceiverState::free &&
                 !receiver.leased && receiver.operation == wire::Operation::idle &&
                 !receiver.current_tune.present && receiver.tune_revision == 0U);
    }
    // P's real reader is listed, with no cached sample and no handle data.
    V1_CHECK(status->readers.present && status->readers.value.count == 1U);
    const wire::ReaderStatus& reader = status->readers.value.items[0];
    V1_CHECK(reader.present && reader.card.state == wire::CardState::unknown &&
             !reader.card.sampled_at_ns.present &&
             reader.card.reason == static_cast<std::uint8_t>(wire::UnknownReason::not_sampled) &&
             reader.handles_open == 0U);
    // JSON from the exact same typed snapshot validates against the LIST.
    std::vector<std::uint8_t> json(70000U);
    const auto json_size = wire::encode_status_json(
        *status, status_context, MutableByteView{json.data(), json.size()});
    V1_CHECK(json_size && json_size.value() > 0U);

    // A lease never appears in STATUS: only leased=true.
    const std::uint64_t lease = client->acquire_lease(2U);
    V1_CHECK(lease != 0U);
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::receiver, 2U}, reply, status.get()));
    V1_CHECK(reply.header.kind == wire::Kind::response && status->receivers.value.count == 1U &&
             status->receivers.value.items[0].leased &&
             status->receivers.value.items[0].state == wire::ReceiverState::leased);
    std::array<std::uint8_t, 8U> lease_bytes{};
    for (std::size_t i = 0U; i < 8U; ++i)
        lease_bytes[i] = static_cast<std::uint8_t>(lease >> (8U * i));
    V1_CHECK(!contains(reply.payload, lease_bytes.data(), lease_bytes.size()));
    const std::array<std::uint8_t, ipc::kNonceLength> token{
        0x01U, 0x5aU, 0x5aU, 0x5aU, 0x5aU, 0x5aU, 0x5aU, 0x5aU,
        0x5aU, 0x5aU, 0x5aU, 0x5aU, 0x5aU, 0x5aU, 0x5aU, 0x5aU};
    V1_CHECK(!contains(reply.payload, token.data(), token.size()));

    // Wrong epoch, unknown public ID and invalid scope fail before any
    // backend or card callback.
    const std::size_t opens = total(f.backend.open_calls);
    const wire::Uuid stale = make_epoch(0x77U);
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::enclosure, 0U}, reply, nullptr, &stale));
    V1_CHECK(TestClient::error_code(reply) == wire::kWireErrorStaleHandle);
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::receiver, 8U}, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::reader, 1U}, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::stream, 1U}, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::reader, 0U}, reply, status.get()));
    V1_CHECK(reply.header.kind == wire::Kind::response && status->readers.value.count == 1U &&
             !status->receivers.present && !status->functions.present);
    Reply acquire_reply;
    V1_CHECK(client->acquire(3U, acquire_reply, &stale));
    V1_CHECK(TestClient::error_code(acquire_reply) == wire::kWireErrorStaleHandle);
    V1_CHECK(total(f.backend.open_calls) == opens);
    V1_CHECK(card_untouched(f));
    V1_CHECK(f.server_ok.load());
    return true;
}

bool test_sparse_ids_and_empty_readers()
{
    Fixture f("v1-sparse");
    V1_CHECK(f.start(true, 0x20U));
    auto client = TestClient::connect(f.endpoint);
    V1_CHECK(client && client->hello());
    std::unique_ptr<wire::ListResponse> list(new wire::ListResponse());
    V1_CHECK(client->list(*list));
    V1_CHECK(list->receivers.value.count == 3U && list->receivers.value.items[0].id == 0U &&
             list->receivers.value.items[2].id == 513U && list->readers.present &&
             list->readers.value.count == 0U);
    std::unique_ptr<wire::StatusResponse> status(new wire::StatusResponse());
    Reply reply;
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::enclosure, 0U}, reply, status.get()));
    V1_CHECK(reply.header.kind == wire::Kind::response && status->readers.present &&
             status->readers.value.count == 0U && status->receivers.value.count == 3U);
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::reader, 0U}, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    // Public ID 1 is not published; 513 maps to service receiver 6, 0 to 2.
    V1_CHECK(client->acquire(1U, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    V1_CHECK(total(f.backend.open_calls) == 0U);
    const std::uint64_t high = client->acquire_lease(513U);
    const std::uint64_t zero = client->acquire_lease(0U);
    V1_CHECK(high != 0U && zero != 0U);
    V1_CHECK(f.backend.open_calls[6].load() == 1U && f.backend.open_calls[2].load() == 1U &&
             total(f.backend.open_calls) == 2U);
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::receiver, 513U}, reply, status.get()));
    V1_CHECK(reply.header.kind == wire::Kind::response &&
             status->receivers.value.items[0].descriptor.id == 513U &&
             status->receivers.value.items[0].leased);
    Reply tuned;
    V1_CHECK(client->tune(client->terrestrial(high, 557142857U), tuned));
    V1_CHECK(tuned.header.kind == wire::Kind::response);
    V1_CHECK(f.backend.last_hz[6].load() == 557142857U && f.backend.hz_calls[2].load() == 0U);
    V1_CHECK(f.server_ok.load());
    return true;
}

// ---------------------------------------------------------------------------
// 3. ACQUIRE -> TUNE -> RELEASE through the real workers and engine

bool tune_result(const Reply& reply, wire::TuneResult& out)
{
    if (reply.header.kind != wire::Kind::response) return false;
    auto decoded = wire::decode_tune_result(reply.view());
    if (!decoded) return false;
    out = decoded.value();
    return true;
}

bool release_result(const Reply& reply, wire::ReleaseResponse& out)
{
    if (reply.header.kind != wire::Kind::response) return false;
    auto decoded = wire::decode_release_response(reply.view());
    if (!decoded) return false;
    out = decoded.value();
    return true;
}
bool start_result(const Reply& reply, wire::StartResponse& out)
{
    if (reply.header.kind != wire::Kind::response) return false;
    auto decoded = wire::decode_start_response(reply.view());
    if (!decoded) return false;
    out = decoded.value();
    return true;
}
bool stop_result(const Reply& reply, wire::StopResponse& out)
{
    if (reply.header.kind != wire::Kind::response) return false;
    auto decoded = wire::decode_stop_response(reply.view());
    if (!decoded) return false;
    out = decoded.value();
    return true;
}

bool test_tune_exact_hz_and_release()
{
    Fixture f("v1-tune");
    V1_CHECK(f.start());
    auto owner = TestClient::connect(f.endpoint);
    auto other = TestClient::connect(f.endpoint);
    V1_CHECK(owner && other && owner->hello() && other->hello());
    const std::uint64_t lease = owner->acquire_lease(2U);
    V1_CHECK(lease != 0U);
    Reply reply;
    V1_CHECK(other->acquire(2U, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::BUSY));

    // Fractional-kHz exact Hz request reaches the strict entrance unchanged.
    V1_CHECK(owner->tune(owner->terrestrial(lease, 473142857U), reply));
    wire::TuneResult result;
    V1_CHECK(tune_result(reply, result));
    V1_CHECK(result.epoch == owner->epoch && result.tune_revision == 1U &&
             result.requested_hz == 473142857U && result.programmed_hz.present &&
             result.programmed_hz.value == 473142000U &&
             result.verification == wire::Verification::lock &&
             result.lock.state == wire::MeasureState::valid && result.lock.value &&
             !result.selected_tsid.present && !result.selected_slot.present);
    V1_CHECK(f.backend.last_hz[2].load() == 473142857U && f.backend.khz_calls.load() == 0U);

    // Profile boundaries: 40 MHz is admitted, one Hz below is UNSUPPORTED
    // before any power or frontend call and spends no revision.
    V1_CHECK(owner->tune(owner->terrestrial(lease, 40000000U), reply));
    V1_CHECK(tune_result(reply, result) && result.tune_revision == 2U &&
             result.requested_hz == 40000000U);
    const std::size_t power_before = f.backend.power_calls[2].load();
    V1_CHECK(owner->tune(owner->terrestrial(lease, 39999999U), reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::UNSUPPORTED));
    V1_CHECK(f.backend.power_calls[2].load() == power_before);
    // LNA, PID subset and satellite automatic selection are not provided.
    wire::TuneRequest lna = owner->terrestrial(lease, 473142857U);
    lna.lna.present = true;
    lna.lna.value = true;
    V1_CHECK(owner->tune(lna, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::UNSUPPORTED));

    std::unique_ptr<wire::StatusResponse> status(new wire::StatusResponse());
    V1_CHECK(owner->status(wire::Scope{wire::ScopeKind::receiver, 2U}, reply, status.get()));
    const wire::ReceiverStatus& tuned = status->receivers.value.items[0];
    V1_CHECK(tuned.state == wire::ReceiverState::tuned && tuned.leased &&
             tuned.tune_revision == 2U && tuned.current_tune.present &&
             tuned.current_tune.value.revision == 2U &&
             tuned.current_tune.value.frequency_hz == 40000000U &&
             tuned.last_successful_tune.present);

    // Another owner's lease and a stale epoch never reach hardware.
    const std::size_t hz_before = total(f.backend.hz_calls);
    V1_CHECK(other->tune(other->terrestrial(lease, 473142857U), reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    wire::TuneRequest stale = owner->terrestrial(lease, 473142857U);
    stale.epoch = make_epoch(0x66U);
    V1_CHECK(owner->tune(stale, reply));
    V1_CHECK(TestClient::error_code(reply) == wire::kWireErrorStaleHandle);
    V1_CHECK(other->release(lease, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    V1_CHECK(total(f.backend.hz_calls) == hz_before && f.backend.close_calls[2].load() == 0U);

    // Satellite slot selection on receiver 0.
    const std::uint64_t satellite = owner->acquire_lease(0U);
    V1_CHECK(satellite != 0U);
    wire::TuneRequest bs;
    bs.epoch = owner->epoch;
    bs.lease_id = satellite;
    bs.system = wire::System::isdb_s;
    bs.frequency_hz = 1318000000U;
    bs.selection = wire::Selection::slot;
    bs.selection_value = 3U;
    bs.lnb_voltage.present = true;
    bs.lnb_voltage.value = 0U;
    bs.timeout_ms = 1000U;
    V1_CHECK(owner->tune(bs, reply));
    V1_CHECK(tune_result(reply, result) && result.selected_slot.present &&
             result.selected_slot.value == 3U && result.requested_hz == 1318000000U);
    V1_CHECK(f.backend.last_voltage.load() == 0U);

    // RELEASE reports the actual released/quiesced outcome.
    wire::ReleaseResponse released;
    V1_CHECK(owner->release(lease, reply));
    V1_CHECK(release_result(reply, released) && released.released && released.quiesced &&
             !released.error.present && released.lease_id == lease &&
             released.epoch == owner->epoch);
    V1_CHECK(owner->release(lease, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    V1_CHECK(other->acquire_lease(2U) != 0U);

    // A failing close is neither released nor quiesced and keeps the
    // receiver excluded.
    f.backend.close_error.store(Error::USB_IO);
    V1_CHECK(owner->release(satellite, reply));
    V1_CHECK(release_result(reply, released) && !released.released && !released.quiesced &&
             released.error.present &&
             released.error.value.code == static_cast<std::uint32_t>(Error::USB_IO));
    f.backend.close_error.store(Error::OK);
    V1_CHECK(owner->status(wire::Scope{wire::ScopeKind::receiver, 0U}, reply, status.get()));
    V1_CHECK(status->receivers.value.items[0].state == wire::ReceiverState::error &&
             !status->receivers.value.items[0].leased);
    V1_CHECK(other->acquire(0U, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_READY));
    V1_CHECK(f.backend.capture_calls.load() == 0U);
    V1_CHECK(card_untouched(f));
    V1_CHECK(f.server_ok.load());
    return true;
}

bool test_auto_identity_uses_profile_and_observed_functions()
{
    DeviceObservation first = bridge(1U, 11U);
    DeviceObservation second = bridge(2U, 12U);
    const std::array<const DeviceObservation*, 2U> functions{&first, &second};
    const auto m1 = automatic_px4_identity(DeviceModel::px_m1ur,
                                           "00001205000960", {&first, nullptr}, 1U, true);
    const auto s1 = automatic_px4_identity(DeviceModel::px_s1ur,
                                           "00001205000960", {&second, nullptr}, 1U, true);
    V1_CHECK(m1 && s1 && m1.value().kind == identity::IdentityKind::usb_topology &&
             s1.value().kind == identity::IdentityKind::usb_topology);
    const auto m1_name = identity::auto_instance(m1.value());
    const auto s1_name = identity::auto_instance(s1.value());
    V1_CHECK(m1_name && s1_name &&
             std::memcmp(m1_name.value().bytes.data(), s1_name.value().bytes.data(),
                         m1_name.value().length) != 0);

    const auto unknown_serial = automatic_px4_identity(DeviceModel::px_w3u4, "",
                                                        {&first, nullptr}, 1U, false);
    V1_CHECK(unknown_serial &&
             unknown_serial.value().kind == identity::IdentityKind::usb_topology);
    const auto unverified_serial = automatic_px4_identity(
        DeviceModel::px_w3u4, "123456789012345", {&first, nullptr}, 1U, false);
    V1_CHECK(unverified_serial &&
             unverified_serial.value().kind == identity::IdentityKind::usb_topology);
    const auto trusted_serial = automatic_px4_identity(DeviceModel::px_q3u4,
                                                        "00001205000960", functions, 2U, true);
    V1_CHECK(trusted_serial &&
             trusted_serial.value().kind == identity::IdentityKind::serial);
    const auto duplicate_serial = automatic_px4_identity(
        DeviceModel::px_q3u4, "00001205000960", functions, 2U, false);
    V1_CHECK(duplicate_serial &&
             duplicate_serial.value().kind == identity::IdentityKind::usb_topology);
    DeviceObservation unlocated = first;
    unlocated.location.has_bus = false;
    const auto no_identity = automatic_px4_identity(DeviceModel::px_s1ur,
                                                     "00001205000960",
                                                     {&unlocated, nullptr}, 1U, false);
    V1_CHECK(!no_identity && no_identity.error() == Error::INVALID_ARGUMENT);
    return true;
}

bool test_unnegotiated_pid_filter_stops_before_backend()
{
    Fixture f("v1-pid-feature");
    V1_CHECK(f.start());
    auto client = TestClient::connect(f.endpoint);
    V1_CHECK(client && client->hello());
    const std::uint64_t lease = client->acquire_lease(2U);
    V1_CHECK(lease != 0U);
    wire::TuneRequest tune = client->terrestrial(lease, 473142857U);
    tune.pids.present = true;
    tune.pids.value.count = 1U;
    tune.pids.value.items[0] = 256U;
    const std::size_t hz_before = total(f.backend.hz_calls);
    const std::size_t power_before = total(f.backend.power_calls);
    Reply reply;
    V1_CHECK(client->tune(tune, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::UNSUPPORTED));
    V1_CHECK(total(f.backend.hz_calls) == hz_before &&
             total(f.backend.power_calls) == power_before);
    V1_CHECK(f.server_ok.load());
    return true;
}

// The acceptance sample precedes queue publication: a ticket that waits in
// its lane past its budget is TIMEOUT without any power or frontend I/O.
bool test_queue_wait_counts_against_deadline()
{
    Fixture f("v1-queue");
    V1_CHECK(f.start());
    auto blocker = TestClient::connect(f.endpoint);
    auto waiter = TestClient::connect(f.endpoint);
    V1_CHECK(blocker && waiter && blocker->hello() && waiter->hello());
    // Receivers 2 and 3 share the first bridge lane.
    const std::uint64_t held = blocker->acquire_lease(3U);
    const std::uint64_t queued = waiter->acquire_lease(2U);
    V1_CHECK(held != 0U && queued != 0U);
    f.backend.gated_tune_receiver.store(3U);
    f.backend.tune_gate.arm();
    V1_CHECK(blocker->send_request(
        wire::kOpcodeTune,
        TestClient::encode(blocker->terrestrial(held, 473142857U, 5000U),
                           &wire::encode_tune_request)));
    V1_CHECK(f.backend.tune_gate.wait_entered());
    V1_CHECK(waiter->send_request(
        wire::kOpcodeTune,
        TestClient::encode(waiter->terrestrial(queued, 473142857U, 100U),
                           &wire::encode_tune_request)));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    f.clock.now.fetch_add(500U);
    f.backend.tune_gate.open();
    Reply reply;
    V1_CHECK(waiter->read_frame(reply) && reply.header.opcode == wire::kOpcodeTune);
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::TIMEOUT));
    V1_CHECK(f.backend.hz_calls[2].load() == 0U && f.backend.power_calls[2].load() == 0U);
    V1_CHECK(blocker->read_frame(reply) && reply.header.opcode == wire::kOpcodeTune);
    V1_CHECK(f.server_ok.load());
    return true;
}

// A publication signal failure aborts the admitted ticket without I/O.
bool test_publication_signal_failure()
{
    ControlWorkerStartupOptions options{};
    options.fail_tune_signal = true;
    ControlWorkerLanes::set_test_startup_options(&options);
    Fixture f("v1-signal");
    const bool started = f.start();
    ControlWorkerLanes::set_test_startup_options(nullptr);
    V1_CHECK(started);
    auto client = TestClient::connect(f.endpoint);
    V1_CHECK(client && client->hello());
    const std::uint64_t lease = client->acquire_lease(2U);
    V1_CHECK(lease != 0U);
    Reply reply;
    V1_CHECK(client->tune(client->terrestrial(lease, 473142857U), reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::INTERNAL));
    V1_CHECK(f.backend.hz_calls[2].load() == 0U && f.backend.power_calls[2].load() == 0U);
    std::unique_ptr<wire::StatusResponse> status(new wire::StatusResponse());
    V1_CHECK(client->status(wire::Scope{wire::ScopeKind::receiver, 2U}, reply, status.get()));
    const wire::ReceiverStatus& receiver = status->receivers.value.items[0];
    V1_CHECK(!receiver.current_tune.present && receiver.tune_revision == 1U &&
             receiver.operation == wire::Operation::idle && receiver.leased);
    V1_CHECK(f.server_ok.load());
    return true;
}

// Owner EOF while its tune is in the frontend: the current tune is
// invalidated before the hardware cleanup, no late success is published,
// and the receiver is not reacquirable until the cleanup completes.
bool test_owner_eof_during_tune_and_blocked_cleanup()
{
    Fixture f("v1-eof");
    V1_CHECK(f.start());
    auto owner = TestClient::connect(f.endpoint);
    auto monitor = TestClient::connect(f.endpoint);
    V1_CHECK(owner && monitor && owner->hello() && monitor->hello());
    const std::uint64_t lease = owner->acquire_lease(2U);
    V1_CHECK(lease != 0U);
    Reply reply;
    V1_CHECK(owner->tune(owner->terrestrial(lease, 473142857U), reply));
    V1_CHECK(reply.header.kind == wire::Kind::response);

    f.backend.gated_tune_receiver.store(2U);
    f.backend.tune_gate.arm();
    f.backend.gated_close_receiver.store(2U);
    f.backend.close_gate.arm();
    V1_CHECK(owner->send_request(
        wire::kOpcodeTune,
        TestClient::encode(owner->terrestrial(lease, 557142857U, 5000U),
                           &wire::encode_tune_request)));
    V1_CHECK(f.backend.tune_gate.wait_entered());
    V1_CHECK(::shutdown(owner->fd(), SHUT_RDWR) == 0);
    std::unique_ptr<wire::StatusResponse> status(new wire::StatusResponse());
    // Wait until the poll thread has observed EOF and synchronously revoked
    // the owner's ticket; starting the in-flight tune already invalidates the
    // old current record and is not proof of disconnect cleanup.
    V1_CHECK(wait_for(kWaitBound, [&]() { return f.server->connection_count() == 1U; }));
    const auto revoked = f.tuner->tune_metadata(2U);
    V1_CHECK(revoked && !revoked.value().current_valid);
    V1_CHECK(f.backend.close_calls[2].load() == 0U);
    f.backend.tune_gate.open();
    V1_CHECK(f.backend.close_gate.wait_entered());
    V1_CHECK(monitor->status(wire::Scope{wire::ScopeKind::receiver, 2U}, reply, status.get()));
    const wire::ReceiverStatus& cleaning = status->receivers.value.items[0];
    V1_CHECK(cleaning.leased && cleaning.operation == wire::Operation::releasing &&
             !cleaning.current_tune.present && cleaning.last_successful_tune.present &&
             cleaning.last_successful_tune.value.frequency_hz == 473142857U);
    V1_CHECK(monitor->acquire(2U, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::BUSY));
    f.backend.close_gate.open();
    V1_CHECK(wait_for(kWaitBound, [&]() { return f.backend.close_calls[2].load() == 1U; }));
    std::uint64_t reacquired = 0U;
    V1_CHECK(wait_for(kWaitBound, [&]() {
        reacquired = monitor->acquire_lease(2U);
        return reacquired != 0U;
    }));
    const auto metadata = f.tuner->tune_metadata(2U);
    V1_CHECK(metadata && !metadata.value().current_valid &&
             metadata.value().last_successful.requested_hz == 473142857U);
    V1_CHECK(f.server_ok.load());
    return true;
}

// ---------------------------------------------------------------------------
// 4. Operations that are not connected yet fail closed.

bool test_pending_operations_fail_closed()
{
    Fixture f("v1-pending");
    V1_CHECK(f.start());
    auto client = TestClient::connect(f.endpoint);
    V1_CHECK(client && client->hello());
    const std::uint64_t lease = client->acquire_lease(2U);
    V1_CHECK(lease != 0U);
    Reply reply;
    V1_CHECK(client->tune(client->terrestrial(lease, 473142857U), reply));
    wire::TuneResult result;
    V1_CHECK(tune_result(reply, result));
    wire::StartRequest start;
    start.epoch = client->epoch;
    start.lease_id = lease;
    start.tune_revision = result.tune_revision;
    // START_STREAM is now served (stage1).  Arm then stop it so the lease is
    // clean for the remaining failures-closed checks.
    V1_CHECK(client->request(wire::kOpcodeStartStream,
                             TestClient::encode(start, &wire::encode_start_request), reply));
    wire::StartResponse started;
    V1_CHECK(start_result(reply, started) && started.stream_id != 0U);
    wire::StopRequest stop_request;
    stop_request.epoch = client->epoch;
    stop_request.lease_id = lease;
    stop_request.stream_id = started.stream_id;
    V1_CHECK(client->request(wire::kOpcodeStopStream,
                             TestClient::encode(stop_request, &wire::encode_stop_request),
                             reply));
    wire::StopResponse stopped;
    V1_CHECK(stop_result(reply, stopped) && stopped.final.id == started.stream_id &&
             stopped.quiesced);
    wire::CardStatusRequest card;
    card.key.epoch = client->epoch;
    V1_CHECK(client->request(wire::kOpcodeCardStatus,
                             TestClient::encode(card, &wire::encode_card_status_request), reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::UNSUPPORTED));
    V1_CHECK(f.backend.capture_calls.load() == 0U && card_untouched(f));

    // Data endpoint: a well-formed ATTACH for a stream that no longer exists
    // is a typed NOT_FOUND and closes; a stale epoch is STALE_HANDLE; the
    // legacy magic is closed without a reply.  The attach path is now served,
    // so the old UNSUPPORTED expectation no longer describes the product.
    wire::AttachRequest attach;
    attach.epoch = client->epoch;
    attach.lease_id = lease;
    attach.stream_id = 1U;
    for (int round = 0; round < 2; ++round) {
        auto data = TestClient::connect(f.stream_endpoint);
        V1_CHECK(data);
        if (round == 1) attach.epoch = make_epoch(0x55U);
        V1_CHECK(data->request(wire::kOpcodeAttachStream,
                               TestClient::encode(attach, &wire::encode_attach_request), reply));
        V1_CHECK(TestClient::error_code(reply) ==
                 (round == 0 ? static_cast<std::uint32_t>(Error::NOT_FOUND)
                             : wire::kWireErrorStaleHandle));
        V1_CHECK(data->closed_without_reply());
    }
    auto legacy = TestClient::connect(f.stream_endpoint);
    V1_CHECK(legacy);
    auto bytes = TestClient::frame(wire::kOpcodeAttachStream, 1U, ByteView{nullptr, 0U});
    bytes[0] = 'P';
    bytes[1] = 'X';
    bytes[2] = '4';
    bytes[3] = 'U';
    V1_CHECK(legacy->send_bytes(bytes.data(), bytes.size()));
    V1_CHECK(legacy->closed_without_reply());
    V1_CHECK(f.backend.capture_calls.load() == 0U);
    V1_CHECK(f.server_ok.load());
    return true;
}

// ---------------------------------------------------------------------------
// 14. Card service over the control connection

bool test_card_service_over_control()
{
    Fixture f("v1-card");
    V1_CHECK(f.start());

    // CARD_SERVICE is refused with UNSUPPORTED until the client negotiates it,
    // and that refusal never reaches the card backend.
    {
        auto client = TestClient::connect(f.endpoint);
        V1_CHECK(client && client->hello());
        wire::CardStatusRequest card;
        card.key.epoch = client->epoch;
        card.key.reader_id = 0U;
        Reply reply;
        V1_CHECK(client->request(
            wire::kOpcodeCardStatus,
            TestClient::encode(card, &wire::encode_card_status_request), reply));
        V1_CHECK(TestClient::error_code(reply) ==
                 static_cast<std::uint32_t>(Error::UNSUPPORTED));
        V1_CHECK(card_untouched(f));
    }

    auto client = TestClient::connect(f.endpoint);
    V1_CHECK(client && client->hello(nullptr, wire::kFeatureCardService));

    // A reader id outside the canonical LIST is NOT_FOUND and powers nothing.
    {
        wire::CardStatusRequest card;
        card.key.epoch = client->epoch;
        card.key.reader_id = 0x1234U;
        Reply reply;
        V1_CHECK(client->request(
            wire::kOpcodeCardStatus,
            TestClient::encode(card, &wire::encode_card_status_request), reply));
        V1_CHECK(TestClient::error_code(reply) ==
                 static_cast<std::uint32_t>(Error::NOT_FOUND));
        V1_CHECK(card_untouched(f));
    }

    // The listed reader is observed through the card lane: the synthetic
    // backend reports a present but uninitialized card.
    {
        wire::CardStatusRequest card;
        card.key.epoch = client->epoch;
        card.key.reader_id = 0U;
        Reply reply;
        V1_CHECK(client->request(
            wire::kOpcodeCardStatus,
            TestClient::encode(card, &wire::encode_card_status_request), reply));
        V1_CHECK(reply.header.kind == wire::Kind::response);
        const auto decoded = wire::decode_card_status_response(reply.view());
        V1_CHECK(decoded);
        V1_CHECK(decoded.value().key.reader_id == 0U);
        V1_CHECK(decoded.value().current_generation != 0U);
        V1_CHECK(decoded.value().present && !decoded.value().initialized);
        V1_CHECK(!decoded.value().protocol.present && !decoded.value().atr.present);
        V1_CHECK(f.card_backend.detect_calls.load() != 0U);
    }

    // CONNECT reaches the lane and returns a bound handle.  The synthetic
    // session exposes no ATR, so the handle is reported not usable rather than
    // carrying a fabricated ATR.
    std::uint64_t handle = 0U;
    {
        wire::CardConnectRequest connect;
        connect.key.epoch = client->epoch;
        connect.key.reader_id = 0U;
        connect.share = wire::Share::shared;
        connect.preferred_protocols = 1U;
        Reply reply;
        V1_CHECK(client->request(
            wire::kOpcodeCardConnect,
            TestClient::encode(connect, &wire::encode_card_connect_request), reply));
        V1_CHECK(reply.header.kind == wire::Kind::response);
        const auto decoded = wire::decode_card_session_result(reply.view());
        V1_CHECK(decoded && decoded.value().card_handle != 0U);
        V1_CHECK(decoded.value().reader_id == 0U);
        V1_CHECK(!decoded.value().usable);
        handle = decoded.value().card_handle;
    }

    // A live handle presented with the wrong stored generation is STALE_HANDLE
    // (the wire code 16), and the APDU never reaches the session.
    {
        wire::CardTransmitRequest transmit;
        transmit.key.epoch = client->epoch;
        transmit.key.reader_id = 0U;
        transmit.key.bound_generation = 2U;
        transmit.key.card_handle = handle;
        transmit.command.length = 1U;
        transmit.command.bytes[0] = 0x00U;
        Reply reply;
        V1_CHECK(client->request(
            wire::kOpcodeCardTransmit,
            TestClient::encode(transmit, &wire::encode_card_transmit_request), reply));
        V1_CHECK(TestClient::error_code(reply) == wire::kWireErrorStaleHandle);
        V1_CHECK(f.card_session.transmit_calls.load() == 0U);
    }

    // DISCONNECT consumes the handle even though the session has no ATR.
    {
        wire::CardDisconnectRequest disconnect;
        disconnect.key.epoch = client->epoch;
        disconnect.key.reader_id = 0U;
        disconnect.key.bound_generation = 1U;
        disconnect.key.card_handle = handle;
        disconnect.disposition = wire::Disposition::leave;
        Reply reply;
        V1_CHECK(client->request(
            wire::kOpcodeCardDisconnect,
            TestClient::encode(disconnect, &wire::encode_card_disconnect_request), reply));
        V1_CHECK(reply.header.kind == wire::Kind::response);
        const auto decoded = wire::decode_card_disconnect_response(reply.view());
        V1_CHECK(decoded && decoded.value().consumed && decoded.value().quiesced);
    }

    // The consumed handle is now unknown to this owner.
    {
        wire::CardHandleStatusRequest status;
        status.key.epoch = client->epoch;
        status.key.reader_id = 0U;
        status.key.bound_generation = 1U;
        status.key.card_handle = handle;
        Reply reply;
        V1_CHECK(client->request(
            wire::kOpcodeCardHandleStatus,
            TestClient::encode(status, &wire::encode_card_handle_status_request), reply));
        V1_CHECK(TestClient::error_code(reply) ==
                 static_cast<std::uint32_t>(Error::NOT_FOUND));
    }

    V1_CHECK(f.server_ok.load());
    return true;
}

#if defined(PX4_IFD_NEW_WIRE_PROOF)
// Public PC/SC card-client factory on an actual newwire daemon; only the
// physical card is simulated. This does not alter any old IFD fixture.
bool test_public_ifd_newwire_card()
{
    Fixture f("ifd-newwire-card");
    f.card_session.enable_mock_apdu();
    V1_CHECK(f.start(false, 0x10U, f.instance));
    pcsc::PosixIfdCardClientFactory factory;
    pcsc::IfdEndpoint endpoint{};
    endpoint.runtime_directory = f.runtime.path();
    endpoint.device_instance = f.instance;
    const auto created = factory.connect(endpoint);
    V1_CHECK(created && created.value());
    auto& client = *created.value();
    const auto before = client.status();
    V1_CHECK(before && before.value().present);
    const auto opened = client.connect_shared();
    V1_CHECK(opened && opened.value().handle != 0U && opened.value().atr_length == 2U);
    const std::uint8_t apdu = 0x00U;
    std::array<std::uint8_t, 16> bytes{};
    const auto result = client.transmit(
        opened.value().handle, ByteView{&apdu,1U}, MutableByteView{bytes.data(),bytes.size()});
    V1_CHECK(result && result.value() == 2U && bytes[0] == 0x90U && bytes[1] == 0x00U);
    V1_CHECK(client.disconnect(opened.value().handle));
    client.close();
    V1_CHECK(f.card_session.transmit_calls.load() == 1U && f.server_ok.load());
    return true;
}
#endif

// Success-path CARD_SERVICE proof: one real newwire request chain against the
// existing CardService and card worker, with only the physical APDU stubbed.
bool test_card_successful_apdu_over_control()
{
    Fixture f("v1-card-success");
    f.card_session.enable_mock_apdu();
    V1_CHECK(f.start());
    auto client = TestClient::connect(f.endpoint);
    V1_CHECK(client && client->hello(nullptr, wire::kFeatureCardService));

    wire::CardConnectRequest connect;
    connect.key.epoch = client->epoch;
    connect.key.reader_id = 0U;
    connect.share = wire::Share::shared;
    connect.preferred_protocols = 1U;
    Reply reply;
    V1_CHECK(client->request(wire::kOpcodeCardConnect,
                             TestClient::encode(connect, &wire::encode_card_connect_request),
                             reply));
    V1_CHECK(reply.header.kind == wire::Kind::response);
    const auto joined = wire::decode_card_session_result(reply.view());
    V1_CHECK(joined && joined.value().card_handle != 0U &&
             joined.value().bound_generation != 0U &&
             joined.value().usable && joined.value().atr.present);

    wire::CardTransmitRequest transmit;
    transmit.key.epoch = client->epoch;
    transmit.key.reader_id = 0U;
    transmit.key.card_handle = joined.value().card_handle;
    transmit.key.bound_generation = joined.value().bound_generation;
    transmit.protocol = wire::CardProtocol::t1;
    transmit.command.length = 1U;
    transmit.command.bytes[0U] = 0x00U;
    V1_CHECK(client->request(wire::kOpcodeCardTransmit,
                             TestClient::encode(transmit, &wire::encode_card_transmit_request),
                             reply));
    V1_CHECK(reply.header.kind == wire::Kind::response);
    const auto response = wire::decode_card_transmit_response(reply.view());
    V1_CHECK(response && response.value().response.length == 2U &&
             response.value().response.bytes[0U] == 0x90U &&
             response.value().response.bytes[1U] == 0x00U);
    V1_CHECK(f.card_session.transmit_calls.load() == 1U);

    wire::CardDisconnectRequest disconnect;
    disconnect.key = transmit.key;
    disconnect.disposition = wire::Disposition::leave;
    V1_CHECK(client->request(
        wire::kOpcodeCardDisconnect,
        TestClient::encode(disconnect, &wire::encode_card_disconnect_request), reply));
    const auto closed = wire::decode_card_disconnect_response(reply.view());
    V1_CHECK(closed && closed.value().consumed && closed.value().quiesced);
    V1_CHECK(f.server_ok.load());
    return true;
}

// ---------------------------------------------------------------------------
// Separate process: a fresh epoch per daemon context, and a restarted daemon
// rejects the previous epoch.

volatile std::sig_atomic_t g_child_stop = 0;

void on_child_term(int) { g_child_stop = 1; }

[[noreturn]] void run_child_server(const std::string& runtime, int ready_fd,
                                   const std::string& requested_instance,
                                   bool shared_group = false,
                                   bool wrong_product = false,
                                   bool shared_serial_model = false,
                                   std::optional<bool> serial_unique = std::nullopt,
                                   bool serial_missing = false)
{
    std::signal(SIGTERM, on_child_term);
    CardBackend card_backend;
    CardSession card_session;
    CardService card(card_backend, card_session);
    Backend backend;
    PosixTunerNonceSource nonce;
    Clock clock;
    TunerService tuner(backend, nonce, clock);
    std::unique_ptr<ControlContext> context(new ControlContext());
    DeviceObservation first;
    DeviceObservation second;
    std::string instance = requested_instance;
    if (instance.empty()) {
        const auto serial = identity::make_serial_selector("00001205000960");
        identity::Selector selector{};
        if (serial) {
            selector.kind = identity::SelectorKind::serial;
            selector.serial = serial.value();
        }
        const auto resolved = serial ? identity::resolve_selector(identity::Product::px4,
                                                                    selector) :
                                       dtv::userland::Result<identity::ResolvedIdentity>::failure(
                                           dtv::userland::Error::INVALID_ARGUMENT);
        const auto automatic = resolved ? identity::auto_instance(resolved.value()) :
                                          dtv::userland::Result<identity::InstanceText>::failure(
                                              dtv::userland::Error::INVALID_ARGUMENT);
        if (automatic)
            instance.assign(automatic.value().bytes.data(), automatic.value().length);
    }
    const auto epoch = nonce.generate();
    int status = 1;
    if (epoch && tuner.configure_strict_capabilities() &&
        !instance.empty() &&
        product_context(tuner, epoch.value(), *context, first, second, instance,
                        serial_unique)) {
        if (serial_missing && context->list.enclosure.present) {
            auto& device_identity = context->list.enclosure.value.identity;
            device_identity.serial_state = wire::IdentitySerialState::absent;
            device_identity.serial.present = false;
            device_identity.uniqueness = wire::Uniqueness::unknown;
        }
        if (wrong_product) {
            auto& product = context->list.producer.product;
            product = {};
            constexpr char alien[] = "another-userland";
            product.length = sizeof(alien) - 1U;
            std::memcpy(product.bytes.data(), alien, sizeof(alien) - 1U);
        }
        if (shared_serial_model) {
            auto& model = context->list.enclosure.value.model.value;
            model.id = {};
            model.name = {};
            constexpr char model_id[] = "PX-M1UR";
            constexpr char model_name[] = "PX-M1UR";
            model.id.length = sizeof(model_id) - 1U;
            model.name.length = sizeof(model_name) - 1U;
            std::memcpy(model.id.bytes.data(), model_id, sizeof(model_id) - 1U);
            std::memcpy(model.name.bytes.data(), model_name, sizeof(model_name) - 1U);
        }
        const EndpointConfig endpoint{runtime.c_str(), instance.c_str(), kControlEndpointName,
            shared_group ? EndpointAccess::shared_group : EndpointAccess::private_user};
        auto server = PosixControlServer::create(endpoint, card, tuner, *context);
        if (server) {
            const char ready = 'R';
            (void)!::write(ready_fd, &ready, 1U);
            status = 0;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (g_child_stop == 0 && std::chrono::steady_clock::now() < deadline) {
                if (!server.value()->poll_once(Timeout{10U})) {
                    status = 1;
                    break;
                }
            }
            (void)server.value()->shutdown();
        }
    }
    (void)card.shutdown();
    ::close(ready_fd);
    std::_Exit(status);
}

// Starts the child and waits (bounded) for its ready byte.
pid_t start_child(const std::string& runtime, const std::string& instance,
                  bool shared_group = false, bool wrong_product = false,
                  bool shared_serial_model = false,
                  std::optional<bool> serial_unique = std::nullopt,
                  bool serial_missing = false)
{
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) return -1;
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::close(pipe_fds[0]);
        run_child_server(runtime, pipe_fds[1], instance, shared_group, wrong_product,
                         shared_serial_model, serial_unique, serial_missing);
    }
    ::close(pipe_fds[1]);
    if (pid < 0) {
        ::close(pipe_fds[0]);
        return -1;
    }
    pollfd descriptor{pipe_fds[0], POLLIN, 0};
    char ready = 0;
    const bool ok = ::poll(&descriptor, 1U, 10000) == 1 && ::read(pipe_fds[0], &ready, 1U) == 1 &&
                    ready == 'R';
    ::close(pipe_fds[0]);
    if (!ok) {
        ::kill(pid, SIGKILL);
        (void)::waitpid(pid, nullptr, 0);
        return -1;
    }
    return pid;
}

bool create_stale_endpoint(const std::string& runtime, std::string_view instance)
{
    const std::filesystem::path directory =
        std::filesystem::path(runtime) / "px4-userland" / "v1" / instance;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || ::chmod(directory.c_str(), 0700) != 0) return false;
    const std::filesystem::path endpoint = directory / "control.sock";
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string path = endpoint.string();
    if (path.size() >= sizeof(address.sun_path)) {
        ::close(fd);
        return false;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
    const bool bound = ::bind(fd, reinterpret_cast<const sockaddr*>(&address),
                              static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                                     path.size() + 1U)) == 0;
    (void)::close(fd);
    return bound && ::chmod(endpoint.c_str(), 0600) == 0;
}

int create_busy_endpoint(const std::string& runtime, std::string_view instance)
{
    const std::filesystem::path directory =
        std::filesystem::path(runtime) / "px4-userland" / "v1" / instance;
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error || ::chmod(directory.c_str(), 0700) != 0) return -1;
    const std::filesystem::path endpoint = directory / "control.sock";
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string path = endpoint.string();
    if (path.size() >= sizeof(address.sun_path)) {
        ::close(fd);
        return -1;
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1U);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address),
               static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) +
                                      path.size() + 1U)) != 0 ||
        ::chmod(endpoint.c_str(), 0600) != 0 || ::listen(fd, 1) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool run_ctl_status(const std::string& runtime, const std::string& instance,
                    int& status, std::string& output,
                    const char* device = nullptr, const char* command = "status")
{
    int pipe_fds[2] = {-1, -1};
    if (::pipe(pipe_fds) != 0) return false;
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::close(pipe_fds[0]);
        (void)::dup2(pipe_fds[1], STDOUT_FILENO);
        ::close(pipe_fds[1]);
        if (device != nullptr && !instance.empty())
            ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                    "--runtime-dir", runtime.c_str(), "--instance", instance.c_str(),
                    "--device", device, command, static_cast<char*>(nullptr));
        else if (device != nullptr)
            ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                    "--runtime-dir", runtime.c_str(), "--device", device,
                    command, static_cast<char*>(nullptr));
        else if (instance.empty())
            ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                    "--runtime-dir", runtime.c_str(), command, static_cast<char*>(nullptr));
        else
            ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                    "--runtime-dir", runtime.c_str(), "--instance", instance.c_str(),
                    command, static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    ::close(pipe_fds[1]);
    if (pid < 0) {
        ::close(pipe_fds[0]);
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    bool done = false;
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{pipe_fds[0], POLLIN | POLLHUP, 0};
        if (::poll(&descriptor, 1U, 100) > 0 &&
            (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[1024];
            const ssize_t count = ::read(pipe_fds[0], buffer, sizeof(buffer));
            if (count > 0) output.append(buffer, static_cast<std::size_t>(count));
        }
        if (::waitpid(pid, &status, WNOHANG) == pid) {
            done = true;
            break;
        }
    }
    ::close(pipe_fds[0]);
    if (!done) {
        (void)::kill(pid, SIGKILL);
        (void)::waitpid(pid, &status, 0);
    }
    return done;
}

// Stops the child with SIGTERM, escalating to SIGKILL after a bound.
bool stop_child(pid_t pid)
{
    if (pid <= 0) return false;
    ::kill(pid, SIGTERM);
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ::kill(pid, SIGKILL);
    (void)::waitpid(pid, &status, 0);
    return false;
}

bool test_fresh_epoch_after_restart()
{
    TempRuntime runtime;
    V1_CHECK(runtime.valid());
    const EndpointConfig endpoint{runtime.path().c_str(), "v1-process", kControlEndpointName,
                                  EndpointAccess::private_user};
    const pid_t first_pid = start_child(runtime.path(), "v1-process");
    V1_CHECK(first_pid > 0);
    wire::Uuid first_epoch{};
    {
        auto client = TestClient::connect(endpoint);
        const bool ok = client && client->hello();
        if (ok) first_epoch = client->epoch;
        if (!ok) {
            (void)stop_child(first_pid);
            return false;
        }
    }
    V1_CHECK(stop_child(first_pid));
    const pid_t second_pid = start_child(runtime.path(), "v1-process");
    V1_CHECK(second_pid > 0);
    bool ok = false;
    {
        auto client = TestClient::connect(endpoint);
        if (client && client->hello() && client->epoch != first_epoch) {
            wire::ListRequest list;
            list.epoch = first_epoch;
            Reply reply;
            ok = client->request(wire::kOpcodeList,
                                 TestClient::encode(list, &wire::encode_list_request), reply) &&
                 TestClient::error_code(reply) == wire::kWireErrorStaleHandle;
            wire::ListResponse fresh;
            ok = ok && client->list(fresh) && fresh.epoch == client->epoch;
        }
    }
    const bool stopped = stop_child(second_pid);
    V1_CHECK(ok && stopped);
    return true;
}

bool test_installed_ctl_status_against_daemon_process()
{
    EnvironmentRestore restore_xdg("XDG_RUNTIME_DIR");
    EnvironmentRestore restore_tmp("TMPDIR");
    TempRuntime parent;
    TempRuntime unsafe_xdg;
    V1_CHECK(parent.valid() && unsafe_xdg.valid());
    V1_CHECK(::chmod(unsafe_xdg.path().c_str(), 0755) == 0);
    V1_CHECK(::setenv("XDG_RUNTIME_DIR", unsafe_xdg.path().c_str(), 1) == 0);
    V1_CHECK(::setenv("TMPDIR", parent.path().c_str(), 1) == 0);
    const auto selected_root = select_publish_runtime_root();
    V1_CHECK(selected_root);
    const std::string runtime(selected_root.value().data());
    const pid_t server_pid = start_child(runtime, "", false, false, false,
                                         std::optional<bool>{true});
    V1_CHECK(server_pid > 0);
    V1_CHECK(create_stale_endpoint(runtime, "stale-instance"));
    const pid_t invalid_root_pid = ::fork();
    if (invalid_root_pid == 0) {
        ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                "--runtime-dir", unsafe_xdg.path().c_str(), "--device",
                "serial:00001205000960", "status", static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    V1_CHECK(invalid_root_pid > 0);
    const auto invalid_root_deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    int invalid_root_status = 0;
    bool invalid_root_done = false;
    while (std::chrono::steady_clock::now() < invalid_root_deadline) {
        const pid_t waited = ::waitpid(invalid_root_pid, &invalid_root_status, WNOHANG);
        if (waited == invalid_root_pid) {
            invalid_root_done = true;
            break;
        }
        if (waited < 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!invalid_root_done) {
        (void)::kill(invalid_root_pid, SIGKILL);
        (void)::waitpid(invalid_root_pid, &invalid_root_status, 0);
    }
    V1_CHECK(invalid_root_done && WIFEXITED(invalid_root_status) &&
             WEXITSTATUS(invalid_root_status) == 2);
    int output_pipe[2] = {-1, -1};
    if (::pipe(output_pipe) != 0) {
        (void)stop_child(server_pid);
        V1_CHECK(false);
    }
    const pid_t ctl_pid = ::fork();
    if (ctl_pid == 0) {
        ::close(output_pipe[0]);
        (void)::dup2(output_pipe[1], STDOUT_FILENO);
        ::close(output_pipe[1]);
        ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                "status", static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    ::close(output_pipe[1]);
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool child_done = false;
    bool output_eof = false;
    int child_status = 0;
    while (ctl_pid > 0 && std::chrono::steady_clock::now() < deadline &&
           !(output_eof && child_done)) {
        if (!output_eof) {
            pollfd descriptor{output_pipe[0], POLLIN | POLLHUP, 0};
            const int ready = ::poll(&descriptor, 1U, 100);
            if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
                char buffer[4096];
                const ssize_t count = ::read(output_pipe[0], buffer, sizeof(buffer));
                if (count > 0) output.append(buffer, static_cast<std::size_t>(count));
                else if (count == 0) output_eof = true;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!child_done && ::waitpid(ctl_pid, &child_status, WNOHANG) == ctl_pid)
            child_done = true;
    }
    ::close(output_pipe[0]);
    if (!child_done && ctl_pid > 0) {
        ::kill(ctl_pid, SIGKILL);
        (void)::waitpid(ctl_pid, &child_status, 0);
    }
    const bool one_json_line = !output.empty() && output.front() == '{' &&
                               output.back() == '\n' &&
                               output.find('\n') == output.size() - 1U &&
                               output.find("\"product\":\"px4-userland\"") != std::string::npos &&
                               output.find("\"version\":\"9.8.7\"") != std::string::npos &&
                               output.find("\"readers\":[{\"descriptor\":{\"id\":\"0\"") !=
                                   std::string::npos;
    const auto serial = identity::make_serial_selector("00001205000960");
    identity::Selector selector{};
    if (serial) {
        selector.kind = identity::SelectorKind::serial;
        selector.serial = serial.value();
    }
    const auto resolved = serial ? identity::resolve_selector(identity::Product::px4, selector) :
                                   dtv::userland::Result<identity::ResolvedIdentity>::failure(
                                       dtv::userland::Error::INVALID_ARGUMENT);
    const auto automatic = resolved ? identity::auto_instance(resolved.value()) :
                                      dtv::userland::Result<identity::InstanceText>::failure(
                                          dtv::userland::Error::INVALID_ARGUMENT);
    const std::string expected_instance = automatic ?
        std::string(automatic.value().bytes.data(), automatic.value().length) : std::string{};
    const bool same_instance = !expected_instance.empty() &&
        output.find("\"instance\":\"" + expected_instance + "\"") != std::string::npos;
    if (!one_json_line)
        std::fprintf(stderr, "ctl status stdout length=%zu eof=%d\n",
                     output.size(), output_eof);

    int assertion_pipe[2] = {-1, -1};
    bool assertion_ok = false;
    int assertion_exit_status = 0;
    if (!expected_instance.empty() && ::pipe(assertion_pipe) == 0) {
        const pid_t assertion_pid = ::fork();
        if (assertion_pid == 0) {
            ::close(assertion_pipe[0]);
            (void)::dup2(assertion_pipe[1], STDOUT_FILENO);
            ::close(assertion_pipe[1]);
            ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                    "--device", "serial:00001205000961", "--instance",
                    expected_instance.c_str(), "status", static_cast<char*>(nullptr));
            std::_Exit(127);
        }
        ::close(assertion_pipe[1]);
        std::string assertion_output;
        bool assertion_done = false;
        const auto assertion_deadline =
            std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (assertion_pid > 0 &&
               std::chrono::steady_clock::now() < assertion_deadline) {
            pollfd descriptor{assertion_pipe[0], POLLIN | POLLHUP, 0};
            const int ready = ::poll(&descriptor, 1U, 100);
            if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
                char buffer[4096];
                const ssize_t count = ::read(assertion_pipe[0], buffer,
                                             sizeof(buffer));
                if (count > 0)
                    assertion_output.append(buffer, static_cast<std::size_t>(count));
            }
            if (::waitpid(assertion_pid, &assertion_exit_status, WNOHANG) == assertion_pid) {
                assertion_done = true;
                break;
            }
        }
        ::close(assertion_pipe[0]);
        if (!assertion_done && assertion_pid > 0) {
            (void)::kill(assertion_pid, SIGKILL);
            (void)::waitpid(assertion_pid, &assertion_exit_status, 0);
        }
        assertion_ok = assertion_done && WIFEXITED(assertion_exit_status) &&
                       WEXITSTATUS(assertion_exit_status) == 3 && assertion_output.empty();
    } else if (assertion_pipe[0] >= 0) {
        ::close(assertion_pipe[0]);
    }
    const bool server_stopped = stop_child(server_pid);
    if (!(child_done && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0 &&
          server_stopped && one_json_line && same_instance && assertion_ok)) {
        std::fprintf(stderr,
                     "ctl assertion fixture: first=%d json=%d instance=%d assertion=%d server=%d status=%d\n",
                     child_done && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0,
                     one_json_line, same_instance, assertion_ok, server_stopped,
                     assertion_exit_status);
    }
    V1_CHECK(child_done && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0 &&
             server_stopped && one_json_line && same_instance && assertion_ok);
    return true;
}

bool test_actual_ctl_rejects_wrong_product()
{
    TempRuntime runtime;
    V1_CHECK(runtime.valid());
    const std::string instance = "other-product";
    const pid_t server_pid = start_child(runtime.path(), instance, false, true);
    V1_CHECK(server_pid > 0);
    int status = 0;
    std::string output;
    const bool done = run_ctl_status(runtime.path(), instance, status, output);
    const bool stopped = stop_child(server_pid);
    V1_CHECK(done && WIFEXITED(status) && WEXITSTATUS(status) == 3 &&
             output.empty() && stopped);
    return true;
}

bool test_actual_ctl_list_formats_typed_topology()
{
    TempRuntime runtime;
    V1_CHECK(runtime.valid());
    const std::string instance = "list-instance";
    const pid_t server_pid = start_child(runtime.path(), instance);
    V1_CHECK(server_pid > 0);
    int status = 0;
    std::string output;
    const bool done = run_ctl_status(runtime.path(), instance, status, output, nullptr, "list");
    const bool stopped = stop_child(server_pid);
    V1_CHECK(done && WIFEXITED(status) && WEXITSTATUS(status) == 0 && stopped);
    V1_CHECK(output.find("serial=00001205000960 ready=ready usb-present-mask=0x03\n") == 0U &&
             output.find("enclosure id=") != std::string::npos &&
             output.find("functions:\n  function id=") != std::string::npos &&
             output.find("receiver=0 device=1 local=0 system=ISDB-S\n") != std::string::npos &&
             output.find("receiver=4 device=2 local=0 system=ISDB-S\n") != std::string::npos &&
             output.find("readers:\n  reader id=") != std::string::npos);
    V1_CHECK(output.find("systems=") == std::string::npos &&
             output.find("{\"schema\"") == std::string::npos);
    return true;
}

bool test_actual_ctl_list_marks_missing_serial_unknown()
{
    TempRuntime runtime;
    V1_CHECK(runtime.valid());
    const std::string instance = "list-no-serial";
    const pid_t server_pid = start_child(runtime.path(), instance, false, false, false,
                                         std::nullopt, true);
    V1_CHECK(server_pid > 0);
    int status = 0;
    std::string output;
    const bool done = run_ctl_status(runtime.path(), instance, status, output, nullptr, "list");
    const bool stopped = stop_child(server_pid);
    V1_CHECK(done && WIFEXITED(status) && WEXITSTATUS(status) == 0 && stopped);
    V1_CHECK(output.find("serial=unknown") != std::string::npos &&
             output.find("serial=00001205000960") == std::string::npos &&
             output.find("ready=ready") != std::string::npos);
    return true;
}

bool test_ctl_rejects_shared_serial_assertion()
{
    TempRuntime runtime;
    V1_CHECK(runtime.valid());
    const std::string instance = "shared-serial";
    const pid_t server_pid = start_child(runtime.path(), instance, false, false, true);
    V1_CHECK(server_pid > 0);
    int status = 0;
    std::string output;
    const bool done = run_ctl_status(runtime.path(), instance, status, output,
                                     "serial:00001205000960");
    const bool stopped = stop_child(server_pid);
    V1_CHECK(done && WIFEXITED(status) && WEXITSTATUS(status) == 3 &&
             output.empty() && stopped);
    return true;
}

bool test_ctl_rejects_unverified_serial_assertion()
{
    const std::optional<bool> states[] = {std::nullopt, std::optional<bool>{false}};
    const auto serial = identity::make_serial_selector("00001205000960");
    V1_CHECK(serial);
    identity::Selector selector{};
    selector.kind = identity::SelectorKind::serial;
    selector.serial = serial.value();
    const auto resolved = identity::resolve_selector(identity::Product::px4, selector);
    V1_CHECK(resolved);
    const auto automatic = identity::auto_instance(resolved.value());
    V1_CHECK(automatic);
    const std::string instance(automatic.value().bytes.data(), automatic.value().length);
    for (const std::optional<bool>& unique : states) {
        TempRuntime runtime;
        V1_CHECK(runtime.valid());
        const pid_t server_pid = start_child(runtime.path(), instance, false, false,
                                             false, unique);
        V1_CHECK(server_pid > 0);
        int status = 0;
        std::string output;
        const bool done = run_ctl_status(runtime.path(), "", status, output,
                                         "serial:00001205000960");
        const bool stopped = stop_child(server_pid);
        V1_CHECK(done && WIFEXITED(status) && WEXITSTATUS(status) == 3 &&
                 output.empty() && stopped);
    }
    return true;
}

bool test_multiple_runtime_roots_rejected()
{
    EnvironmentRestore restore_xdg("XDG_RUNTIME_DIR");
    EnvironmentRestore restore_tmp("TMPDIR");
    TempRuntime first_root;
    TempRuntime temp_parent;
    V1_CHECK(first_root.valid() && temp_parent.valid());
    const std::string second_root = temp_parent.path() + "/dtv-userland-" +
        std::to_string(static_cast<unsigned long long>(::geteuid()));
    V1_CHECK(::mkdir(second_root.c_str(), 0700) == 0);
    V1_CHECK(::setenv("XDG_RUNTIME_DIR", first_root.path().c_str(), 1) == 0);
    V1_CHECK(::setenv("TMPDIR", temp_parent.path().c_str(), 1) == 0);
    const pid_t first_server = start_child(first_root.path(), "");
    V1_CHECK(first_server > 0);
    const pid_t second_server = start_child(second_root, "");
    if (second_server <= 0) {
        (void)stop_child(first_server);
        V1_CHECK(false);
    }
    int output_pipe[2] = {-1, -1};
    if (::pipe(output_pipe) != 0) {
        (void)stop_child(first_server);
        (void)stop_child(second_server);
        V1_CHECK(false);
    }
    const pid_t ctl_pid = ::fork();
    if (ctl_pid == 0) {
        ::close(output_pipe[0]);
        (void)::dup2(output_pipe[1], STDOUT_FILENO);
        ::close(output_pipe[1]);
        ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                "status", static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    ::close(output_pipe[1]);
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int child_status = 0;
    bool child_done = false;
    while (ctl_pid > 0 && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{output_pipe[0], POLLIN | POLLHUP, 0};
        const int ready = ::poll(&descriptor, 1U, 100);
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[4096];
            const ssize_t count = ::read(output_pipe[0], buffer, sizeof(buffer));
            if (count > 0) output.append(buffer, static_cast<std::size_t>(count));
        }
        const pid_t waited = ::waitpid(ctl_pid, &child_status, WNOHANG);
        if (waited == ctl_pid) {
            child_done = true;
            break;
        }
    }
    ::close(output_pipe[0]);
    if (!child_done && ctl_pid > 0) {
        (void)::kill(ctl_pid, SIGKILL);
        (void)::waitpid(ctl_pid, &child_status, 0);
    }
    const bool second_stopped = stop_child(second_server);
    const bool first_stopped = stop_child(first_server);
    V1_CHECK(child_done && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 3 &&
             output.empty() && first_stopped && second_stopped);
    return true;
}

bool test_busy_candidate_is_not_skipped()
{
    TempRuntime runtime;
    V1_CHECK(runtime.valid());
    const pid_t server_pid = start_child(runtime.path(), "ready-instance");
    V1_CHECK(server_pid > 0);
    const int busy_fd = create_busy_endpoint(runtime.path(), "busy-instance");
    V1_CHECK(busy_fd >= 0);
    int status = 0;
    std::string output;
    const bool done = run_ctl_status(runtime.path(), "", status, output);
    (void)::close(busy_fd);
    const bool stopped = stop_child(server_pid);
    V1_CHECK(done && WIFEXITED(status) && WEXITSTATUS(status) != 0 &&
             output.empty() && stopped);
    return true;
}

bool test_stale_candidate_and_one_live_instance()
{
    TempRuntime runtime;
    V1_CHECK(runtime.valid());
    const pid_t server_pid = start_child(runtime.path(), "ready-instance");
    V1_CHECK(server_pid > 0);
    V1_CHECK(create_stale_endpoint(runtime.path(), "stale-instance"));
    int status = 0;
    std::string output;
    const bool done = run_ctl_status(runtime.path(), "", status, output);
    const bool stopped = stop_child(server_pid);
    V1_CHECK(done && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
             output.find("\"instance\":\"ready-instance\"") != std::string::npos &&
             stopped);
    return true;
}

bool test_invalid_endpoint_prevents_automatic_root_fallback()
{
    EnvironmentRestore restore_xdg("XDG_RUNTIME_DIR");
    EnvironmentRestore restore_tmp("TMPDIR");
    TempRuntime first_root;
    TempRuntime temp_parent;
    V1_CHECK(first_root.valid() && temp_parent.valid());
    const std::string second_root = temp_parent.path() + "/dtv-userland-" +
        std::to_string(static_cast<unsigned long long>(::geteuid()));
    V1_CHECK(::mkdir(second_root.c_str(), 0700) == 0);
    V1_CHECK(::setenv("XDG_RUNTIME_DIR", first_root.path().c_str(), 1) == 0);
    V1_CHECK(::setenv("TMPDIR", temp_parent.path().c_str(), 1) == 0);
    const std::filesystem::path entry = std::filesystem::path(first_root.path()) /
        "px4-userland" / "v1" / "invalid-entry";
    std::error_code error;
    std::filesystem::create_directories(entry, error);
    V1_CHECK(!error);
    V1_CHECK(::chmod((std::filesystem::path(first_root.path()) / "px4-userland").c_str(),
                     0700) == 0);
    V1_CHECK(::chmod((std::filesystem::path(first_root.path()) / "px4-userland" / "v1").c_str(),
                     0700) == 0);
    V1_CHECK(::chmod(entry.c_str(), 0700) == 0);
    const std::filesystem::path endpoint = entry / "control.sock";
    const int invalid_fd = ::open(endpoint.c_str(), O_CREAT | O_WRONLY | O_CLOEXEC, 0600);
    V1_CHECK(invalid_fd >= 0 && ::close(invalid_fd) == 0);

    const pid_t server_pid = start_child(second_root, "later-live");
    V1_CHECK(server_pid > 0);
    int pipe_fds[2] = {-1, -1};
    V1_CHECK(::pipe(pipe_fds) == 0);
    const pid_t ctl_pid = ::fork();
    if (ctl_pid == 0) {
        ::close(pipe_fds[0]);
        (void)::dup2(pipe_fds[1], STDOUT_FILENO);
        ::close(pipe_fds[1]);
        ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                "status", static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    ::close(pipe_fds[1]);
    std::string output;
    int status = 0;
    bool done = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (ctl_pid > 0 && std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{pipe_fds[0], POLLIN | POLLHUP, 0};
        if (::poll(&descriptor, 1U, 100) > 0 &&
            (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[1024];
            const ssize_t count = ::read(pipe_fds[0], buffer, sizeof(buffer));
            if (count > 0) output.append(buffer, static_cast<std::size_t>(count));
        }
        if (::waitpid(ctl_pid, &status, WNOHANG) == ctl_pid) {
            done = true;
            break;
        }
    }
    ::close(pipe_fds[0]);
    if (!done && ctl_pid > 0) {
        (void)::kill(ctl_pid, SIGKILL);
        (void)::waitpid(ctl_pid, &status, 0);
    }
    const bool stopped = stop_child(server_pid);
    V1_CHECK(done && WIFEXITED(status) && WEXITSTATUS(status) == 2 &&
             output.empty() && stopped);
    return true;
}

bool test_case_canonical_instance_collision_rejected()
{
    EnvironmentRestore restore_xdg("XDG_RUNTIME_DIR");
    EnvironmentRestore restore_tmp("TMPDIR");
    TempRuntime root;
    TempRuntime temp_parent;
    if (!root.valid() || !temp_parent.valid() ||
        ::setenv("XDG_RUNTIME_DIR", root.path().c_str(), 1) != 0 ||
        ::setenv("TMPDIR", temp_parent.path().c_str(), 1) != 0)
        return false;
    const pid_t server_pid = start_child(root.path(), "v1-process");
    if (server_pid <= 0) return false;
    const std::string collision = root.path() + "/px4-userland/v1/V1-PROCESS";
    if (::mkdir(collision.c_str(), 0700) != 0) {
        (void)stop_child(server_pid);
        return false;
    }
    int output_pipe[2] = {-1, -1};
    if (::pipe(output_pipe) != 0) {
        (void)stop_child(server_pid);
        return false;
    }
    const pid_t ctl_pid = ::fork();
    if (ctl_pid == 0) {
        ::close(output_pipe[0]);
        (void)::dup2(output_pipe[1], STDOUT_FILENO);
        ::close(output_pipe[1]);
        ::execl(PX4CTL_EXECUTABLE, PX4CTL_EXECUTABLE,
                "status", static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    ::close(output_pipe[1]);
    if (ctl_pid < 0) {
        ::close(output_pipe[0]);
        (void)stop_child(server_pid);
        return false;
    }
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    int child_status = 0;
    bool child_done = false;
    while (std::chrono::steady_clock::now() < deadline) {
        pollfd descriptor{output_pipe[0], POLLIN | POLLHUP, 0};
        const int ready = ::poll(&descriptor, 1U, 100);
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            char buffer[4096];
            const ssize_t count = ::read(output_pipe[0], buffer, sizeof(buffer));
            if (count > 0) output.append(buffer, static_cast<std::size_t>(count));
        }
        if (::waitpid(ctl_pid, &child_status, WNOHANG) == ctl_pid) {
            child_done = true;
            break;
        }
    }
    ::close(output_pipe[0]);
    if (!child_done) {
        (void)::kill(ctl_pid, SIGKILL);
        (void)::waitpid(ctl_pid, &child_status, 0);
    }
    const bool server_stopped = stop_child(server_pid);
    return child_done && WIFEXITED(child_status) && WEXITSTATUS(child_status) == 3 &&
           output.empty() && server_stopped;
}

bool run_group_ctl_child(const std::string& runtime, const std::string& instance,
                         bool member, std::string& output, int& child_status)
{
    if (::geteuid() != 0) return false;
    const int executable = ::open(PX4CTL_EXECUTABLE, O_RDONLY | O_CLOEXEC);
    if (executable < 0) return false;
    int output_pipe[2] = {-1, -1};
    if (::pipe(output_pipe) != 0) {
        (void)::close(executable);
        return false;
    }
    const pid_t child = ::fork();
    if (child == 0) {
        ::close(output_pipe[0]);
        const gid_t group = ::getegid();
        if ((member && ::setgroups(1U, &group) != 0) ||
            (!member && ::setgroups(0U, nullptr) != 0) ||
            ::setgid(65534U) != 0 || ::setuid(65534U) != 0 ||
            ::access(runtime.c_str(), W_OK) == 0 ||
            ::dup2(output_pipe[1], STDOUT_FILENO) < 0)
            std::_Exit(10);
        ::close(output_pipe[1]);
        char* child_argv[] = {
            const_cast<char*>(PX4CTL_EXECUTABLE),
            const_cast<char*>("--runtime-dir"), const_cast<char*>(runtime.c_str()),
            const_cast<char*>("--group"), const_cast<char*>("--instance"),
            const_cast<char*>(instance.c_str()), const_cast<char*>("status"), nullptr};
        (void)::fexecve(executable, child_argv, environ);
        std::_Exit(127);
    }
    (void)::close(executable);
    ::close(output_pipe[1]);
    if (child < 0) {
        ::close(output_pipe[0]);
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool child_done = false;
    bool output_eof = false;
    while (std::chrono::steady_clock::now() < deadline &&
           !(child_done && output_eof)) {
        if (!output_eof) {
            pollfd descriptor{output_pipe[0], POLLIN | POLLHUP, 0};
            const int ready = ::poll(&descriptor, 1U, 100);
            if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
                char buffer[4096];
                const ssize_t count = ::read(output_pipe[0], buffer, sizeof(buffer));
                if (count > 0) output.append(buffer, static_cast<std::size_t>(count));
                else if (count == 0) output_eof = true;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!child_done && ::waitpid(child, &child_status, WNOHANG) == child)
            child_done = true;
    }
    ::close(output_pipe[0]);
    if (!child_done) {
        (void)::kill(child, SIGKILL);
        (void)::waitpid(child, &child_status, 0);
    }
    return child_done && output_eof;
}

bool test_shared_group_actual_ctl_credentials()
{
    TempRuntime runtime("/run");
    const std::string instance = "group-process";
    if (!runtime.valid() || ::geteuid() != 0 ||
        ::chmod(runtime.path().c_str(), 0750) != 0)
        return false;
    const pid_t server_pid = start_child(runtime.path(), instance, true);
    if (server_pid <= 0) return false;
    std::string member_output;
    int member_status = 0;
    const bool member_done = run_group_ctl_child(runtime.path(), instance, true,
                                                 member_output, member_status);
    std::string nonmember_output;
    int nonmember_status = 0;
    const bool nonmember_done = run_group_ctl_child(runtime.path(), instance, false,
                                                    nonmember_output, nonmember_status);
    const bool server_stopped = stop_child(server_pid);
    const bool one_json_line = !member_output.empty() && member_output.front() == '{' &&
        member_output.back() == '\n' &&
        member_output.find('\n') == member_output.size() - 1U &&
        member_output.find("\"instance\":\"group-process\"") != std::string::npos &&
        member_output.find("\"readers\":[{\"descriptor\":{\"id\":\"0\"") !=
            std::string::npos;
    const bool rejected = WIFEXITED(nonmember_status) &&
                          WEXITSTATUS(nonmember_status) == 70 &&
                          nonmember_output.empty();
    return member_done && WIFEXITED(member_status) && WEXITSTATUS(member_status) == 0 &&
           one_json_line && nonmember_done && rejected && server_stopped;
}

}  // namespace

bool test_v1_start_armed_stop_and_release()
{
    Fixture f("v1-start");
    V1_CHECK(f.start());
    auto owner = TestClient::connect(f.endpoint);
    auto other = TestClient::connect(f.endpoint);
    V1_CHECK(owner && other && owner->hello() && other->hello());
    const std::uint64_t lease = owner->acquire_lease(2U);
    V1_CHECK(lease != 0U);
    Reply reply;
    V1_CHECK(owner->tune(owner->terrestrial(lease, 473142857U), reply));
    wire::TuneResult tuned;
    V1_CHECK(tune_result(reply, tuned));
    const std::uint64_t revision = tuned.tune_revision;
    const std::size_t captures_before = f.backend.capture_calls.load();

    // Wrong owner, wire epoch and revision never reach a START mutation.
    V1_CHECK(other->start_stream(lease, revision, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::NOT_FOUND));
    V1_CHECK(owner->start_stream_with_epoch(make_epoch(0x66U), lease, revision, reply));
    V1_CHECK(TestClient::error_code(reply) == wire::kWireErrorStaleHandle);
    V1_CHECK(owner->start_stream(lease, revision + 1U, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::INVALID_ARGUMENT));
    V1_CHECK(f.backend.capture_calls.load() == captures_before);

    // START issues a new stream and attach token and arms the lease.
    V1_CHECK(owner->start_stream(lease, revision, reply));
    wire::StartResponse started;
    V1_CHECK(start_result(reply, started));
    V1_CHECK(started.epoch == owner->epoch && started.stream_id != 0U &&
             started.expires_after_ms > 0U);
    bool token_nonzero = false;
    for (std::uint8_t byte : started.attach_token) {
        if (byte != 0U) token_nonzero = true;
    }
    V1_CHECK(token_nonzero);
    // No physical capture before stage2 ATTACH.
    V1_CHECK(f.backend.capture_calls.load() == captures_before);

    // One armed stream per lease.
    V1_CHECK(owner->start_stream(lease, revision, reply));
    V1_CHECK(TestClient::error_code(reply) == static_cast<std::uint32_t>(Error::BUSY));

    // STATUS carries the phase/revision but no token field exists in the typed
    // record; the tune revision is unchanged by arming.
    std::unique_ptr<wire::StatusResponse> status(new wire::StatusResponse());
    V1_CHECK(owner->status(wire::Scope{wire::ScopeKind::receiver, 2U}, reply, status.get()));
    V1_CHECK(status->receivers.value.items[0].tune_revision == revision);

    // armed STOP returns the exact typed final and keeps the lease.
    V1_CHECK(owner->stop_stream(lease, started.stream_id, reply));
    wire::StopResponse stopped;
    V1_CHECK(stop_result(reply, stopped));
    V1_CHECK(stopped.epoch == owner->epoch && stopped.final.id == started.stream_id &&
             stopped.final.tune_revision == revision &&
             stopped.final.reason == wire::StreamEndReason::stopped &&
             !stopped.final.error.present && stopped.quiesced);
    // A repeated STOP never names a different stream.
    V1_CHECK(owner->stop_stream(lease, started.stream_id, reply));
    if (reply.header.kind == wire::Kind::response) {
        wire::StopResponse again;
        V1_CHECK(stop_result(reply, again) && again.final.id == started.stream_id);
    }
    // RELEASE succeeds and the lease becomes available.
    wire::ReleaseResponse released;
    V1_CHECK(owner->release(lease, reply));
    V1_CHECK(release_result(reply, released) && released.released && released.quiesced);
    V1_CHECK(other->acquire_lease(2U) != 0U);
    return true;
}

class RecordingClock final : public Px4TsClock {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
};

class RecordingSignal final : public Px4TsSignal {
public:
    bool stop_requested() const noexcept override { return false; }
};

std::vector<std::uint8_t> tagged_packets(std::uint8_t tag, std::size_t count)
{
    std::vector<std::uint8_t> result;
    result.reserve(count * 188U);
    for (std::size_t index = 0U; index < count; ++index) {
        std::array<std::uint8_t, 188U> packet{};
        packet[0] = static_cast<std::uint8_t>((tag << 4U) | 0x07U);
        packet[1] = 0x01U;
        packet[2] = 0x01U;
        packet[3] = static_cast<std::uint8_t>(0x10U | (index & 0x0fU));
        packet[4] = static_cast<std::uint8_t>(index);
        result.insert(result.end(), packet.begin(), packet.end());
    }
    return result;
}

std::vector<std::uint8_t> read_file(const std::string& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(input)),
                                     std::istreambuf_iterator<char>());
}

Px4TsArguments recording_arguments(const Fixture& fixture, std::uint64_t packets,
                                   bool duration)
{
    Px4TsArguments arguments{};
    arguments.valid = true;
    arguments.instance = fixture.instance;
    arguments.runtime_directory = fixture.runtime.path();
    arguments.receiver = 2U;
    arguments.system = ipc::System::ISDB_T;
    arguments.frequency_khz = 473142U;
    arguments.tune_timeout_ms = 10000U;
    arguments.output = "-";
    if (duration) {
        arguments.duration_set = true;
        arguments.duration_seconds = 1U;
    } else {
        arguments.packet_count_set = true;
        arguments.packet_count = packets;
    }
    return arguments;
}

bool test_normal_recording_file_output()
{
    TaggedFakeTransport dev1;
    TaggedFakeTransport dev2;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    V1_CHECK(plane);
    Fixture f("normal-recording", plane.value().get());
    V1_CHECK(f.start());

    constexpr std::size_t kPackets = 32U;
    // Tag 3 is receiver 2 on the first Q3U4 bridge.  The first packet is the
    // continuity baseline the real pump consumes before publishing.
    const std::vector<std::uint8_t> tagged = tagged_packets(3U, kPackets + 1U);
    dev1.push(tagged);

    const Px4TsArguments arguments = recording_arguments(f, kPackets, false);
    const std::string path = f.runtime.path() + "/normal.ts";
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                          0600);
    V1_CHECK(fd >= 0);
    Px4TsFailureKind kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics{};
    Px4TsRunResult run_result{};
    {
        Px4TsFdOutput output(fd, true);
        V1_CHECK(output.ready());
        RecordingClock clock;
        RecordingSignal signal;
        const auto result = Px4TsRunner::run_wire(arguments, output, clock, signal,
                                                  &kind, &diagnostics);
        if (!result)
        V1_CHECK(result);
        run_result = result.value();
    }
    V1_CHECK(diagnostics.stream_end_valid);
    V1_CHECK(diagnostics.packets_written == kPackets);
    V1_CHECK(run_result.counters.packets >= kPackets);
    V1_CHECK(run_result.counters.queue_drops == 0U);
    const std::vector<std::uint8_t> written = read_file(path);
    V1_CHECK(written.size() == kPackets * 188U);
    std::vector<std::uint8_t> expected(tagged.begin() + 188,
                                       tagged.begin() + static_cast<std::ptrdiff_t>(
                                                             (kPackets + 1U) * 188U));
    for (std::size_t index = 0U; index < kPackets; ++index)
        expected[index * 188U] = 0x47U;
    V1_CHECK(std::memcmp(written.data(), expected.data(), expected.size()) == 0);
    // The physical capture ran once through the existing receiver lane.
    V1_CHECK(f.backend.capture_calls.load() == 1U);
    V1_CHECK(f.server_ok.load());
    return true;
}

bool test_normal_recording_duration_without_ts()
{
    TaggedFakeTransport dev1;
    TaggedFakeTransport dev2;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    V1_CHECK(plane);
    Fixture f("normal-duration", plane.value().get());
    V1_CHECK(f.start());

    const Px4TsArguments arguments = recording_arguments(f, 0U, true);
    const std::string path = f.runtime.path() + "/duration.ts";
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                          0600);
    V1_CHECK(fd >= 0);
    Px4TsFailureKind kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics{};
    {
        Px4TsFdOutput output(fd, true);
        V1_CHECK(output.ready());
        RecordingClock clock;
        RecordingSignal signal;
        const auto result = Px4TsRunner::run_wire(arguments, output, clock, signal,
                                                  &kind, &diagnostics);
        if (!result)
        V1_CHECK(result);
    }
    V1_CHECK(diagnostics.stream_end_valid);
    V1_CHECK(diagnostics.packets_written == 0U);
    V1_CHECK(diagnostics.stream_end.counters.packets == 0U);
    V1_CHECK(read_file(path).empty());
    V1_CHECK(f.backend.capture_calls.load() == 1U);
    V1_CHECK(f.server_ok.load());
    return true;
}

bool test_normal_recording_small_drop_continues()
{
    TaggedFakeTransport dev1;
    TaggedFakeTransport dev2;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    V1_CHECK(plane);
    Fixture f("normal-drop", plane.value().get());
    V1_CHECK(f.start());

    // More than the ring holds in one burst: the v1 evict-oldest policy keeps
    // the stream alive and counts any eviction instead of ending it.
    constexpr std::size_t kPackets = Q3U4StreamDataPlane::kMinQueuePackets + 8U;
    const std::vector<std::uint8_t> tagged = tagged_packets(3U, kPackets + 1U);
    dev1.push(tagged);

    const Px4TsArguments arguments = recording_arguments(f, 0U, true);
    const std::string path = f.runtime.path() + "/drop.ts";
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                          0600);
    V1_CHECK(fd >= 0);
    Px4TsFailureKind kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics{};
    {
        Px4TsFdOutput output(fd, true);
        V1_CHECK(output.ready());
        RecordingClock clock;
        RecordingSignal signal;
        const auto result = Px4TsRunner::run_wire(arguments, output, clock, signal,
                                                  &kind, &diagnostics);
        if (!result)
        V1_CHECK(result);
    }
    V1_CHECK(diagnostics.stream_end_valid);
    // Every injected packet was either delivered or explicitly evicted; a
    // small overflow never becomes SLOW_CONSUMER or a stream failure.
    V1_CHECK(diagnostics.stream_end_valid);
    // Every injected packet is accounted by the store; the burst may evict
    // the oldest unread complete packets, but that never ends the stream.
    V1_CHECK(diagnostics.stream_end.counters.packets == kPackets);
    V1_CHECK(diagnostics.packets_written <= kPackets);
    V1_CHECK(diagnostics.packets_written > 0U);
    V1_CHECK(f.server_ok.load());
    return true;
}

int main()
{
    struct Case final {
        const char* name;
        bool (*function)();
    };
    const Case cases[] = {
        {"framing_and_handshake", test_framing_and_handshake},
        {"partial_response_write", test_partial_response_write},
        {"list_status_one_context", test_list_status_one_context},
        {"sparse_ids_and_empty_readers", test_sparse_ids_and_empty_readers},
        {"tune_exact_hz_and_release", test_tune_exact_hz_and_release},
        {"v1_start_armed_stop_and_release", test_v1_start_armed_stop_and_release},
        {"normal_recording_file_output", test_normal_recording_file_output},
        {"normal_recording_duration_without_ts",
         test_normal_recording_duration_without_ts},
        {"normal_recording_small_drop_continues",
         test_normal_recording_small_drop_continues},
        {"auto_identity_uses_profile_and_observed_functions",
         test_auto_identity_uses_profile_and_observed_functions},
        {"unnegotiated_pid_filter_stops_before_backend",
         test_unnegotiated_pid_filter_stops_before_backend},
        {"queue_wait_counts_against_deadline", test_queue_wait_counts_against_deadline},
        {"publication_signal_failure", test_publication_signal_failure},
        {"owner_eof_during_tune_and_blocked_cleanup",
         test_owner_eof_during_tune_and_blocked_cleanup},
        {"pending_operations_fail_closed", test_pending_operations_fail_closed},
        {"card_service_over_control", test_card_service_over_control},
        {"card_successful_apdu_over_control", test_card_successful_apdu_over_control},
#if defined(PX4_IFD_NEW_WIRE_PROOF)
        {"public_ifd_newwire_card", test_public_ifd_newwire_card},
#endif
        {"fresh_epoch_after_restart", test_fresh_epoch_after_restart},
        {"installed_ctl_status_against_daemon_process",
         test_installed_ctl_status_against_daemon_process},
        {"actual_ctl_rejects_wrong_product", test_actual_ctl_rejects_wrong_product},
        {"actual_ctl_list_formats_typed_topology",
         test_actual_ctl_list_formats_typed_topology},
        {"actual_ctl_list_marks_missing_serial_unknown",
         test_actual_ctl_list_marks_missing_serial_unknown},
        {"ctl_rejects_shared_serial_assertion", test_ctl_rejects_shared_serial_assertion},
        {"ctl_rejects_unverified_serial_assertion",
         test_ctl_rejects_unverified_serial_assertion},
        {"multiple_runtime_roots_rejected", test_multiple_runtime_roots_rejected},
        {"busy_candidate_is_not_skipped", test_busy_candidate_is_not_skipped},
        {"stale_candidate_and_one_live_instance",
         test_stale_candidate_and_one_live_instance},
        {"invalid_endpoint_prevents_automatic_root_fallback",
         test_invalid_endpoint_prevents_automatic_root_fallback},
        {"case_canonical_instance_collision_rejected",
         test_case_canonical_instance_collision_rejected},
        {"shared_group_actual_ctl_credentials",
         test_shared_group_actual_ctl_credentials},
    };
    int failures = 0;
    for (const Case& test : cases) {
        const bool passed = test.function();
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.name);
        if (!passed) ++failures;
    }
    return failures == 0 ? 0 : 1;
}

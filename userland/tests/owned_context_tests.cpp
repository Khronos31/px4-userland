// SPDX-License-Identifier: GPL-2.0-only
//
// Whole-context close of the production EnclosureContext: the actual owner,
// control server, worker lanes, TunerService/receiver engine and CardService,
// with mock hardware only at the bottom (tuner/card backends, clock, stream
// control and a descriptor-holding resource standing in for the runtime).
// Every gate is finite and an independent watchdog opens them, so a failure
// is reported instead of hanging.
#include "px4/card_service.h"
#include "px4/control_server.h"
#include "px4/enclosure_context.h"
#include "px4/posix_tuner_nonce.h"
#include "px4/tuner_service.h"
#include "px4d_signals.h"
#include "strict_frequency_profiles.h"

#include "dtv/userland/records.h"
#include "dtv/userland/wire.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;
using namespace px4::userland::ipc::posix;
namespace wire = dtv::userland::wire;

#define OWNED_CHECK(condition)                                                         \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "owned context check failed at %d: %s\n", __LINE__,  \
                         #condition);                                                  \
            return false;                                                              \
        }                                                                              \
    } while (false)

constexpr std::chrono::milliseconds kGateBound{20000};
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

// A finite gate: pass() blocks until open() or kGateBound.
class Gate final {
public:
    void arm()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        armed_ = true;
        open_ = false;
        entered_ = 0U;
    }
    void pass()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!armed_) return;
        ++entered_;
        changed_.notify_all();
        changed_.wait_for(lock, kGateBound, [&]() noexcept { return open_; });
    }
    bool wait_entered()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, kWaitBound, [&]() noexcept { return entered_ != 0U; });
    }
    void open()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        open_ = true;
        armed_ = false;
        changed_.notify_all();
    }
    std::size_t entered()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entered_;
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool armed_ = false;
    bool open_ = false;
    std::size_t entered_ = 0U;
};

// Destruction observer: each tracked object reports exactly when it dies.
struct Observers final {
    std::atomic<int> time{0};
    std::atomic<int> tuner_backend{0};
    std::atomic<int> card_backend{0};
    std::atomic<int> card_session{0};
    std::atomic<int> stream{0};
    std::atomic<int> descriptor{0};
    std::atomic<int> family{0};
    int total() const
    {
        return time.load() + tuner_backend.load() + card_backend.load() + card_session.load() +
               stream.load() + descriptor.load() + family.load();
    }
    bool each_once() const
    {
        return time.load() == 1 && tuner_backend.load() == 1 && card_backend.load() == 1 &&
               card_session.load() == 1 && stream.load() == 1 && descriptor.load() == 1 &&
               family.load() == 1;
    }
};

class Time final : public TunerServiceTime {
public:
    explicit Time(Observers& observers) : observers_(observers) {}
    ~Time() override { observers_.time.fetch_add(1); }
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

private:
    Observers& observers_;
};

// Two terrestrial receivers with the native strict profile.
class TunerBackend final : public TunerServiceBackend {
public:
    explicit TunerBackend(Observers& observers) : observers_(observers) {}
    ~TunerBackend() override { observers_.tuner_backend.fetch_add(1); }
    std::uint8_t receiver_count() const noexcept override { return 2U; }
    bool receiver_supports(std::uint8_t receiver, System system) const noexcept override
    {
        return receiver < 2U && system == System::ISDB_T;
    }
    TunerStrictProfile strict_profile(std::uint8_t receiver,
                                      System system) const noexcept override
    {
        return receiver_supports(receiver, system) ? r850_terrestrial_strict_profile()
                                                   : TunerStrictProfile{};
    }
    Result<void> open_receiver(std::uint8_t) noexcept override { return Result<void>::success(); }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t, std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t, std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<TunerHzProgramming> tune_terrestrial_hz(std::uint8_t, std::uint64_t,
                                                   std::uint32_t) noexcept override
    {
        tune_gate.pass();
        tune_calls.fetch_add(1U);
        return Result<TunerHzProgramming>::success({});
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
    Result<void> close_receiver(std::uint8_t) noexcept override
    {
        close_gate.pass();
        close_calls.fetch_add(1U);
        return Result<void>::success();
    }
    Result<void> shutdown() noexcept override
    {
        shutdown_gate.pass();
        shutdown_calls.fetch_add(1U);
        return Result<void>::success();
    }

    Gate tune_gate;
    Gate close_gate;
    Gate shutdown_gate;
    std::atomic<std::size_t> tune_calls{0U};
    std::atomic<std::size_t> close_calls{0U};
    std::atomic<std::size_t> shutdown_calls{0U};

private:
    Observers& observers_;
};

class CardBackend final : public CardServiceBackend {
public:
    explicit CardBackend(Observers& observers) : observers_(observers) {}
    ~CardBackend() override { observers_.card_backend.fetch_add(1); }
    Result<void> set_power(bool) noexcept override { return Result<void>::success(); }
    Result<void> initialize_uart() noexcept override { return Result<void>::success(); }
    Result<bool> detect_card() noexcept override { return Result<bool>::success(false); }

private:
    Observers& observers_;
};

class CardSession final : public CardProtocolSession {
public:
    explicit CardSession(Observers& observers) : observers_(observers) {}
    ~CardSession() override { observers_.card_session.fetch_add(1); }
    Result<void> initialize() noexcept override { return Result<void>::success(); }
    Result<std::size_t> transmit(ByteView, MutableByteView) noexcept override
    {
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    bool initialized() const noexcept override { return false; }
    const CardAtr& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override {}

private:
    Observers& observers_;
    CardAtr atr_{};
};

class Stream final : public TunerStreamControl {
public:
    explicit Stream(Observers& observers) : observers_(observers) {}
    ~Stream() override { observers_.stream.fetch_add(1); }
    Result<void> attach(const TunerAttachment&) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> detach(const TunerAttachment&) noexcept override
    {
        return Result<void>::failure(Error::NOT_FOUND);
    }

private:
    Observers& observers_;
};

// Stands in for the runtime's internal USB descriptor duplicate: a real
// descriptor that stays open until the family is released.
class Descriptor final {
public:
    explicit Descriptor(Observers& observers) : observers_(observers)
    {
        fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);
    }
    ~Descriptor()
    {
        if (fd_ >= 0) ::close(fd_);
        observers_.descriptor.fetch_add(1);
    }
    int fd() const { return fd_; }

private:
    Observers& observers_;
    int fd_ = -1;
};

// Members in dependency order: the descriptor and clock first (released
// last), the stream last (released first), like the product families.
class Family final : public EnclosureFamily {
public:
    explicit Family(Observers& observers)
        : observers_(observers), descriptor(observers), clock(observers), tuner(observers),
          card(observers), session(observers), stream(observers)
    {
    }
    ~Family() override { observers_.family.fetch_add(1); }
    TunerServiceBackend& tuner_backend() noexcept override { return tuner; }
    CardServiceBackend& card_backend() noexcept override { return card; }
    CardProtocolSession& card_protocol() noexcept override { return session; }
    TunerServiceTime& time() noexcept override { return clock; }
    TunerStreamControl* stream_control() noexcept override { return &stream; }

    Observers& observers_;
    Descriptor descriptor;
    Time clock;
    TunerBackend tuner;
    CardBackend card;
    CardSession session;
    Stream stream;
};

class TempRuntime final {
public:
    TempRuntime()
    {
        std::string pattern = std::filesystem::temp_directory_path().string() + "/px4-own-XXXXXX";
        char* made = ::mkdtemp(pattern.data());
        if (made != nullptr) {
            path_ = made;
            valid_ = ::chmod(path_.c_str(), 0700) == 0;
        }
    }
    ~TempRuntime()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    bool valid() const { return valid_; }
    const std::string& path() const { return path_; }

private:
    std::string path_;
    bool valid_ = false;
};

wire::Uuid make_epoch(std::uint8_t seed)
{
    wire::Uuid epoch{};
    for (std::size_t i = 0U; i < epoch.size(); ++i)
        epoch[i] = static_cast<std::uint8_t>(seed + i * 5U);
    return epoch;
}

void make_context(ControlContext& context, const wire::Uuid& epoch)
{
    context = ControlContext{};
    context.epoch = epoch;
    wire::ListResponse& list = context.list;
    list.epoch = epoch;
    const char product[] = "owned-test";
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
    list.receivers.value.count = 2U;
    for (std::uint16_t i = 0U; i < 2U; ++i) {
        list.receivers.value.items[i].id = i;
        list.receivers.value.items[i].systems.mask = 0x01U;
        list.receivers.value.items[i].binding.present = true;
        list.receivers.value.items[i].binding.value = wire::Binding{0U, i};
        context.receiver_index[i] = static_cast<std::uint8_t>(i);
    }
    list.readers.present = true;
    list.readers.value.count = 0U;
}

// One published owned context plus its serving thread.  The serving thread is
// stopped before close(), so poll and close are serialized as the contract
// requires.
struct Daemon final {
    Observers observers;
    Family* family = nullptr;
    std::unique_ptr<EnclosureContext> owned;
    TempRuntime* runtime = nullptr;
    std::string instance;
    std::atomic<bool> stop{false};
    std::atomic<bool> poll_ok{true};
    std::atomic<unsigned int> poll_calls{0U};
    std::thread poller;

    bool start(TempRuntime& root, const char* name, std::uint8_t seed,
               std::uint32_t poll_timeout_ms = 10U)
    {
        runtime = &root;
        instance = name;
        std::unique_ptr<Family> graph(new Family(observers));
        family = graph.get();
        auto assembled = EnclosureContext::assemble(
            std::move(graph), std::unique_ptr<TunerNonceSource>(new PosixTunerNonceSource()));
        if (!assembled) return false;
        owned = std::move(assembled.value());
        EnclosurePublication publication;
        publication.runtime_directory = root.path();
        publication.instance = instance;
        publication.lease_identity = instance;
        if (!owned->reserve_namespace(publication)) return false;
        std::unique_ptr<ControlContext> context(new ControlContext());
        make_context(*context, make_epoch(seed));
        if (!owned->publish(*context)) return false;
        poller = std::thread([this, poll_timeout_ms]() {
            while (!stop.load()) {
                ++poll_calls;
                if (!owned->poll_once(Timeout{poll_timeout_ms})) {
                    poll_ok.store(false);
                    return;
                }
            }
        });
        return true;
    }

    void stop_serving()
    {
        stop.store(true);
        if (poller.joinable()) poller.join();
    }

    EndpointConfig endpoint() const
    {
        return EndpointConfig{runtime->path().c_str(), instance.c_str(), kControlEndpointName,
                              EndpointAccess::private_user};
    }

    void open_gates()
    {
        if (family == nullptr || observers.family.load() != 0) return;
        family->tuner.tune_gate.open();
        family->tuner.close_gate.open();
        family->tuner.shutdown_gate.open();
    }

    ~Daemon()
    {
        stop_serving();
        // The owned context is released through close(); its destructor
        // waits for any retained cleanup.
        if (owned) (void)owned->close(std::chrono::milliseconds(kGateBound));
    }
};

// Opens every registered gate after a bound, so a failing test still ends;
// a fired watchdog is reported as a failure.
class Watchdog final {
public:
    explicit Watchdog(std::chrono::milliseconds bound)
        : thread_([this, bound]() {
              std::unique_lock<std::mutex> lock(mutex_);
              if (!changed_.wait_for(lock, bound, [&]() noexcept { return done_; })) {
                  fired_.store(true);
                  for (Daemon* daemon : daemons_) daemon->open_gates();
              }
          })
    {
    }
    ~Watchdog()
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
            changed_.notify_all();
        }
        thread_.join();
    }
    void watch(Daemon& daemon)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        daemons_.push_back(&daemon);
    }
    bool fired() const { return fired_.load(); }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    bool done_ = false;
    std::atomic<bool> fired_{false};
    std::vector<Daemon*> daemons_;
    std::thread thread_;
};

// Minimal UDTV request/response client over the real socket (test code).
class Client final {
public:
    static std::unique_ptr<Client> connect(const EndpointConfig& endpoint)
    {
        auto stream = SocketStream::connect(endpoint, Timeout{2000U});
        if (!stream) return nullptr;
        return std::unique_ptr<Client>(new Client(std::move(stream.value())));
    }

    template <typename Record>
    bool send(std::uint16_t opcode, const Record& value,
              Result<std::size_t> (*encode)(const Record&, MutableByteView) noexcept)
    {
        std::vector<std::uint8_t> payload(wire::kMaxControlPayload);
        const auto size = encode(value, MutableByteView{payload.data(), payload.size()});
        if (!size) return false;
        std::vector<std::uint8_t> frame(wire::kHeaderSize + size.value());
        wire::Header header;
        header.kind = wire::Kind::request;
        header.opcode = opcode;
        header.request_id = ++request_id_;
        header.payload_length = static_cast<std::uint32_t>(size.value());
        if (!wire::encode_frame(header, ByteView{payload.data(), size.value()},
                                MutableByteView{frame.data(), frame.size()}))
            return false;
        return static_cast<bool>(stream_.write_all(ByteView{frame.data(), frame.size()},
                                                   Timeout{2000U}));
    }

    bool read(wire::Header& header, std::vector<std::uint8_t>& payload)
    {
        const auto deadline = std::chrono::steady_clock::now() + kWaitBound;
        while (collector_.frames.empty()) {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            std::array<std::uint8_t, 4096U> buffer{};
            const auto read = stream_.read_some(MutableByteView{buffer.data(), buffer.size()},
                                                Timeout{20U});
            if (!read) {
                if (read.error() == Error::DISCONNECTED) return false;
                continue;
            }
            if (!receiver_.feed(ByteView{buffer.data(), read.value()}, collector_)) return false;
        }
        header = collector_.frames.front().first;
        payload = std::move(collector_.frames.front().second);
        collector_.frames.erase(collector_.frames.begin());
        return true;
    }

    bool hello()
    {
        wire::HelloRequest request;
        request.min = wire::Version{1U, 0U};
        request.max = wire::Version{1U, 0U};
        wire::Header header;
        std::vector<std::uint8_t> payload;
        if (!send(wire::kOpcodeHello, request, &wire::encode_hello_request) ||
            !read(header, payload) || header.kind != wire::Kind::response)
            return false;
        const auto decoded = wire::decode_hello_response(ByteView{payload.data(), payload.size()});
        if (!decoded) return false;
        epoch = decoded.value().epoch;
        return true;
    }

    std::uint64_t acquire(std::uint16_t receiver)
    {
        wire::AcquireRequest request;
        request.epoch = epoch;
        request.receiver_id = receiver;
        wire::Header header;
        std::vector<std::uint8_t> payload;
        if (!send(wire::kOpcodeAcquire, request, &wire::encode_acquire_request) ||
            !read(header, payload) || header.kind != wire::Kind::response)
            return 0U;
        const auto decoded =
            wire::decode_acquire_response(ByteView{payload.data(), payload.size()});
        return decoded ? decoded.value().lease_id : 0U;
    }

    bool send_tune(std::uint64_t lease)
    {
        wire::TuneRequest request;
        request.epoch = epoch;
        request.lease_id = lease;
        request.system = wire::System::isdb_t;
        request.frequency_hz = 473142857U;
        request.timeout_ms = 10000U;
        return send(wire::kOpcodeTune, request, &wire::encode_tune_request);
    }

    void close() { stream_.close(); }
    wire::Uuid epoch{};

private:
    class Collector final : public wire::FrameHandler {
    public:
        Result<void> on_frame(const wire::Frame& frame) noexcept override
        {
            frames.emplace_back(frame.header,
                                std::vector<std::uint8_t>(frame.payload.data,
                                                          frame.payload.data + frame.payload.size));
            return Result<void>::success();
        }
        std::vector<std::pair<wire::Header, std::vector<std::uint8_t>>> frames;
    };

    explicit Client(SocketStream stream)
        : stream_(std::move(stream)),
          storage_(new std::array<std::uint8_t, wire::kMaxControlPayload>()),
          receiver_(MutableByteView{storage_->data(), storage_->size()})
    {
    }

    SocketStream stream_;
    std::unique_ptr<std::array<std::uint8_t, wire::kMaxControlPayload>> storage_;
    wire::FrameReceiver receiver_;
    Collector collector_;
    std::uint32_t request_id_ = 0U;
};

bool descriptor_open(int fd) { return fd >= 0 && ::fcntl(fd, F_GETFD) != -1; }

bool namespace_busy(TempRuntime& root, const char* instance)
{
    Observers observers;
    auto assembled = EnclosureContext::assemble(
        std::unique_ptr<EnclosureFamily>(new Family(observers)),
        std::unique_ptr<TunerNonceSource>(new PosixTunerNonceSource()));
    if (!assembled) return false;
    EnclosurePublication publication;
    publication.runtime_directory = root.path();
    publication.instance = instance;
    publication.lease_identity = instance;
    const auto reserved = assembled.value()->reserve_namespace(publication);
    const bool busy = !reserved && reserved.error() == Error::BUSY;
    (void)assembled.value()->close(std::chrono::milliseconds(kWaitBound));
    return busy;
}

// ---------------------------------------------------------------------------

bool test_normal_close_releases_once()
{
    TempRuntime root;
    OWNED_CHECK(root.valid());
    Daemon daemon;
    OWNED_CHECK(daemon.start(root, "00001205000960", 0x10U));
    auto client = Client::connect(daemon.endpoint());
    OWNED_CHECK(client && client->hello() && client->acquire(0U) != 0U);
    const int fd = daemon.family->descriptor.fd();
    daemon.stop_serving();
    OWNED_CHECK(daemon.poll_ok.load());
    const ContextCloseResult closed = daemon.owned->close(std::chrono::milliseconds(kWaitBound));
    OWNED_CHECK(closed.quiesced && !closed.deadline_expired && closed.error == Error::OK);
    OWNED_CHECK(daemon.observers.each_once());
    (void)fd;
    // A repeated close observes the same completed cleanup.
    const ContextCloseResult again = daemon.owned->close(std::chrono::milliseconds(0));
    OWNED_CHECK(again.quiesced && daemon.observers.each_once());
    OWNED_CHECK(!daemon.owned->poll_once(Timeout{0U}));
    // The namespace is free again.
    OWNED_CHECK(!namespace_busy(root, "00001205000960"));
    return true;
}

bool test_blocked_cleanup_is_retained_then_collected()
{
    TempRuntime root;
    OWNED_CHECK(root.valid());
    Daemon a;
    Daemon b;
    Watchdog watchdog(std::chrono::seconds(30));
    watchdog.watch(a);
    watchdog.watch(b);
    OWNED_CHECK(a.start(root, "00001205000960", 0x10U));
    OWNED_CHECK(b.start(root, "00001205000961", 0x20U));
    auto client = Client::connect(a.endpoint());
    OWNED_CHECK(client && client->hello() && client->acquire(0U) != 0U);
    a.family->tuner.close_gate.arm();
    const int fd = a.family->descriptor.fd();
    a.stop_serving();

    // The release cleanup is held inside the backend: the budget expires and
    // nothing in the graph is released.
    const auto started = std::chrono::steady_clock::now();
    const ContextCloseResult first = a.owned->close(std::chrono::milliseconds(300));
    const auto elapsed = std::chrono::steady_clock::now() - started;
    OWNED_CHECK(!first.quiesced && first.deadline_expired);
    OWNED_CHECK(elapsed < std::chrono::milliseconds(3000));
    OWNED_CHECK(a.family->tuner.close_gate.wait_entered());
    OWNED_CHECK(a.observers.total() == 0);
    OWNED_CHECK(descriptor_open(fd));
    // The same enclosure namespace is refused while the context is retained.
    OWNED_CHECK(namespace_busy(root, "00001205000960"));
    // A second close does not issue a second cleanup.
    const ContextCloseResult second = a.owned->close(std::chrono::milliseconds(100));
    OWNED_CHECK(!second.quiesced && a.family->tuner.close_gate.entered() == 1U);
    OWNED_CHECK(a.observers.total() == 0);

    // An unrelated context keeps serving meanwhile.
    auto other = Client::connect(b.endpoint());
    OWNED_CHECK(other && other->hello() && other->acquire(1U) != 0U);

    // When the held operation returns, the same retained context is collected:
    // every observer exactly once, the descriptor closed, the namespace free.
    a.family->tuner.close_gate.open();
    const ContextCloseResult collected = a.owned->close(std::chrono::milliseconds(kWaitBound));
    OWNED_CHECK(collected.quiesced && collected.error == Error::OK);
    OWNED_CHECK(a.observers.each_once());
    OWNED_CHECK(!namespace_busy(root, "00001205000960"));
    // The old callback acted only on its own context.
    OWNED_CHECK(b.observers.total() == 0 && b.family->tuner.close_calls.load() == 0U &&
                b.poll_ok.load());
    b.stop_serving();
    const ContextCloseResult b_closed = b.owned->close(std::chrono::milliseconds(kWaitBound));
    OWNED_CHECK(b_closed.quiesced && b.observers.each_once());
    OWNED_CHECK(!watchdog.fired());
    return true;
}

bool test_held_callback_keeps_graph()
{
    TempRuntime root;
    OWNED_CHECK(root.valid());
    Daemon daemon;
    Watchdog watchdog(std::chrono::seconds(30));
    watchdog.watch(daemon);
    OWNED_CHECK(daemon.start(root, "00001205000962", 0x30U));
    auto client = Client::connect(daemon.endpoint());
    OWNED_CHECK(client && client->hello());
    const std::uint64_t lease = client->acquire(0U);
    OWNED_CHECK(lease != 0U);
    daemon.family->tuner.tune_gate.arm();
    OWNED_CHECK(client->send_tune(lease));
    OWNED_CHECK(daemon.family->tuner.tune_gate.wait_entered());
    daemon.stop_serving();
    const ContextCloseResult held = daemon.owned->close(std::chrono::milliseconds(300));
    OWNED_CHECK(!held.quiesced && daemon.observers.total() == 0);
    daemon.family->tuner.tune_gate.open();
    const ContextCloseResult collected =
        daemon.owned->close(std::chrono::milliseconds(kWaitBound));
    OWNED_CHECK(collected.quiesced && daemon.observers.each_once());
    OWNED_CHECK(!watchdog.fired());
    return true;
}

bool test_close_budget_includes_wait_for_poll()
{
    TempRuntime root;
    OWNED_CHECK(root.valid());
    Daemon daemon;
    OWNED_CHECK(daemon.start(root, "00001205000963", 0x40U, UINT32_MAX));
    auto client = Client::connect(daemon.endpoint());
    OWNED_CHECK(client != nullptr);
    OWNED_CHECK(wait_for(kWaitBound, [&]() { return daemon.poll_calls.load() >= 2U; }));

    ContextCloseResult first;
    const auto began = std::chrono::steady_clock::now();
    std::atomic<bool> close_done{false};
    std::thread closer([&]() {
        first = daemon.owned->close(std::chrono::milliseconds(50));
        close_done.store(true);
    });
    const bool returned_before_poll_wake = wait_for(std::chrono::milliseconds(250), [&]() {
        return close_done.load();
    });

    // Wake the deliberately long poll so the test can join every thread even
    // if the close deadline assertion fails.
    daemon.stop.store(true);
    const bool woke_poll = client->hello();
    daemon.stop_serving();
    closer.join();
    client->close();

    const auto elapsed = std::chrono::steady_clock::now() - began;
    OWNED_CHECK(woke_poll);
    OWNED_CHECK(returned_before_poll_wake);
    OWNED_CHECK(elapsed < std::chrono::milliseconds(150));
    OWNED_CHECK(!first.quiesced && first.deadline_expired);
    OWNED_CHECK(daemon.owned->close(std::chrono::milliseconds(kWaitBound)).quiesced);
    OWNED_CHECK(daemon.observers.each_once());
    OWNED_CHECK(daemon.poll_ok.load());
    return true;
}

// Standalone seam: a separate process whose whole-context cleanup is held
// past its budget takes the standalone policy and ends with the chosen
// nonzero status, without flushing stdio.  This observes the request and the
// exit status only; it is not evidence of kernel or holder reclamation.
bool test_standalone_deadline_exit()
{
    TempRuntime root;
    OWNED_CHECK(root.valid());
    int marker_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};
    OWNED_CHECK(::pipe(marker_pipe) == 0 && ::pipe(stdout_pipe) == 0);
    const pid_t pid = ::fork();
    OWNED_CHECK(pid >= 0);
    if (pid == 0) {
        ::close(marker_pipe[0]);
        ::close(stdout_pipe[0]);
        (void)::dup2(stdout_pipe[1], STDOUT_FILENO);
        Observers observers;
        std::unique_ptr<Family> graph(new Family(observers));
        Family* family = graph.get();
        family->tuner.shutdown_gate.arm();
        auto assembled = EnclosureContext::assemble(
            std::move(graph), std::unique_ptr<TunerNonceSource>(new PosixTunerNonceSource()));
        if (!assembled) std::_Exit(2);
        EnclosurePublication publication;
        publication.runtime_directory = root.path();
        publication.instance = "00001205000963";
        publication.lease_identity = "00001205000963";
        std::unique_ptr<ControlContext> context(new ControlContext());
        make_context(*context, make_epoch(0x40U));
        if (!assembled.value()->reserve_namespace(publication) ||
            !assembled.value()->publish(*context))
            std::_Exit(3);
        // Buffered, unflushed output that the primitive must not write.
        std::printf("unflushed-diagnostic");
        const ContextCloseResult closed =
            assembled.value()->close(std::chrono::milliseconds(200));
        if (closed.quiesced) std::_Exit(4);
        const char marker = 'D';
        (void)!::write(marker_pipe[1], &marker, 1U);
        px4::userland::px4d::exit_after_close_deadline(70);
    }
    ::close(marker_pipe[1]);
    ::close(stdout_pipe[1]);
    int status = 0;
    bool reaped = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (::waitpid(pid, &status, WNOHANG) == pid) {
            reaped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!reaped) {
        ::kill(pid, SIGKILL);
        (void)::waitpid(pid, &status, 0);
    }
    char marker = 0;
    const bool marked = ::read(marker_pipe[0], &marker, 1U) == 1 && marker == 'D';
    std::array<char, 64U> output{};
    const ssize_t written = ::read(stdout_pipe[0], output.data(), output.size());
    ::close(marker_pipe[0]);
    ::close(stdout_pipe[0]);
    OWNED_CHECK(reaped && marked);
    OWNED_CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 70);
    OWNED_CHECK(written == 0);
    return true;
}

}  // namespace

int main()
{
    struct Case final {
        const char* name;
        bool (*function)();
    };
    const Case cases[] = {
        {"normal_close_releases_once", test_normal_close_releases_once},
        {"blocked_cleanup_is_retained_then_collected",
         test_blocked_cleanup_is_retained_then_collected},
        {"held_callback_keeps_graph", test_held_callback_keeps_graph},
        {"close_budget_includes_wait_for_poll", test_close_budget_includes_wait_for_poll},
        {"standalone_deadline_exit", test_standalone_deadline_exit},
    };
    int failures = 0;
    for (const Case& test : cases) {
        const bool passed = test.function();
        std::printf("%s %s\n", passed ? "PASS" : "FAIL", test.name);
        std::fflush(stdout);
        if (!passed) ++failures;
    }
    return failures == 0 ? 0 : 1;
}

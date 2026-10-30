// SPDX-License-Identifier: GPL-2.0-only
//
// PX4 G2a: control-plane integration over the Core listener/worker path.
// The old control_integration_tests.cpp exercises the product PosixControlServer.
// These cases reproduce the PX4-specific observations through the Core
// EnclosureContext plus the product CardService/TunerService backends and the
// product receiver records. The old target and file stay in place until the
// separate retirement step.
#include "px4/identity.h"
#include "px4/ipc.h"
#include "px4d_receiver_records.h"

#include <dtv/userland/control_client.h>
#include <dtv/userland/host_daemon/tuner_service.h>
#include <dtv/userland/host_listener_context/enclosure_context.h>
#include <dtv/userland/host_runtime/control_socket.h>
#include <dtv/userland/wire.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {

namespace listener = dtv::userland::host_listener_context;
namespace host = dtv::userland::host_runtime;
namespace w = dtv::userland::wire;
using namespace px4::userland;
using namespace px4::userland::ipc;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,       \
                         #condition);                                                       \
            return false;                                                                   \
        }                                                                                   \
    } while (false)

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

class TempRuntime final {
public:
    TempRuntime()
    {
        // Keep the runtime root short so the AF_UNIX sun_path limit is not
        // consumed by macOS /var/folders paths.
        std::string pattern = "/tmp/px4-core-control-XXXXXX";
        char* made = ::mkdtemp(pattern.data());
        if (made != nullptr && ::chmod(made, 0700) == 0) path_ = made;
    }
    ~TempRuntime() noexcept
    {
        if (path_.empty()) return;
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }
    bool valid() const noexcept { return !path_.empty(); }
    const std::string& path() const noexcept { return path_; }
private:
    std::string path_;
};

class TunerBackend final : public TunerServiceBackend {
public:
    std::uint8_t receiver_count() const noexcept override { return 8U; }
    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        ++open_calls[receiver];
        if (receiver == 0U && block_acquire.load()) {
            {
                std::lock_guard<std::mutex> lock(acquire_mutex);
                acquire_entered.store(true);
            }
            acquire_condition.notify_all();
            while (block_acquire.load()) std::this_thread::yield();
        }
        return Result<void>::success();
    }
    Result<void> tune_terrestrial(std::uint8_t receiver, std::uint32_t,
                                  std::uint32_t) noexcept override
    {
        return block_tune_for(receiver, System::ISDB_T);
    }
    Result<void> tune_satellite(std::uint8_t receiver, std::uint32_t,
                                std::uint32_t) noexcept override
    {
        return block_tune_for(receiver, System::ISDB_S);
    }
    Result<TunerHzProgramming> tune_terrestrial_hz(std::uint8_t receiver,
                                                   std::uint64_t, std::uint32_t) noexcept override
    {
        return block_tune_hz_for(receiver, System::ISDB_T);
    }
    Result<TunerHzProgramming> tune_satellite_hz(std::uint8_t receiver,
                                                 std::uint64_t, std::uint32_t) noexcept override
    {
        return block_tune_hz_for(receiver, System::ISDB_S);
    }
    TunerStrictProfile strict_profile(std::uint8_t, System system) const noexcept override
    {
        TunerStrictProfile profile;
        profile.native_hz = true;
        if (system == System::ISDB_T) {
            profile.min_hz = 40000000U;
            profile.max_hz = 1002000000U;
        } else {
            profile.min_hz = 146875000U;
            profile.max_hz = 2350000000U;
            profile.lnb_voltage_mask = 0x03U;
            profile.slot_count = 12U;
        }
        return profile;
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
        ++close_calls[receiver];
        return Result<void>::success();
    }
    Result<void> begin_tune_power(std::uint8_t, System system,
                                  std::uint8_t voltage) noexcept override
    {
        if (system == System::ISDB_S && voltage == 15U) {
            if (!allow_lnb_15v.load()) return Result<void>::failure(Error::UNSUPPORTED);
            ++lnb_gpio_calls;
        }
        return Result<void>::success();
    }
    Result<void> start_capture(std::uint8_t receiver, System system) noexcept override
    {
        ++start_capture_calls[receiver];
        last_capture_system[receiver] = system;
        if (receiver == 2U && block_start_capture.load()) {
            {
                std::lock_guard<std::mutex> lock(start_capture_mutex);
                start_capture_entered.store(true);
            }
            start_capture_condition.notify_all();
            while (block_start_capture.load()) std::this_thread::yield();
        }
        if (receiver == 2U && fail_capture_start.load())
            return Result<void>::failure(Error::UNSUPPORTED);
        return Result<void>::success();
    }
    Result<void> stop_capture(std::uint8_t receiver, System system) noexcept override
    {
        ++stop_capture_calls[receiver];
        last_capture_system[receiver] = system;
        return Result<void>::success();
    }

    std::array<std::size_t, 8U> open_calls{};
    std::array<std::size_t, 8U> close_calls{};
    std::array<std::size_t, 8U> start_capture_calls{};
    std::array<std::size_t, 8U> stop_capture_calls{};
    std::array<System, 8U> last_capture_system{};
    std::atomic<bool> allow_lnb_15v{false};
    std::atomic<std::size_t> lnb_gpio_calls{0U};
    std::atomic<bool> block_acquire{false};
    std::atomic<bool> acquire_entered{false};
    std::mutex acquire_mutex;
    std::condition_variable acquire_condition;
    std::atomic<bool> block_tune{false};
    std::atomic<bool> tune_entered{false};
    std::mutex tune_mutex;
    std::condition_variable tune_condition;
    std::atomic<bool> fail_capture_start{false};
    std::atomic<bool> block_start_capture{false};
    std::atomic<bool> start_capture_entered{false};
    std::mutex start_capture_mutex;
    std::condition_variable start_capture_condition;

private:
    Result<TunerHzProgramming> block_tune_hz_for(std::uint8_t receiver,
                                                  System system) noexcept
    {
        if (receiver == 0U && block_tune.load()) {
            {
                std::lock_guard<std::mutex> lock(tune_mutex);
                tune_entered.store(true);
            }
            tune_condition.notify_all();
            while (block_tune.load()) std::this_thread::yield();
        }
        last_system[receiver] = system;
        ++tune_calls[receiver];
        return Result<TunerHzProgramming>::success({});
    }
    Result<void> block_tune_for(std::uint8_t receiver, System system) noexcept
    {
        if (receiver == 0U && block_tune.load()) {
            {
                std::lock_guard<std::mutex> lock(tune_mutex);
                tune_entered.store(true);
            }
            tune_condition.notify_all();
            while (block_tune.load()) std::this_thread::yield();
        }
        last_system[receiver] = system;
        ++tune_calls[receiver];
        return Result<void>::success();
    }
    std::array<std::size_t, 8U> tune_calls{};
    std::array<System, 8U> last_system{};
};

class Nonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, ipc::kNonceLength>> generate() noexcept override
    {
        std::array<std::uint8_t, ipc::kNonceLength> value{};
        value[0] = next++;
        return Result<std::array<std::uint8_t, ipc::kNonceLength>>::success(value);
    }
private:
    std::uint8_t next = 1U;
};

class Clock final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<
            std::chrono::milliseconds>(std::chrono::steady_clock::now()
                .time_since_epoch()).count());
    }
    void sleep_ms(std::uint32_t duration) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(duration));
    }
};

class Stream final : public TunerStreamControl {
public:
    Result<void> attach(const TunerAttachment& attachment) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        identity_ = attachment;
        attached_ = true;
        packets_remaining_ = 1U;
        counters_ = TunerStreamCounters{17U, 3196U, 1U, 2U, 3U, 4U, 5U, 6U};
        final_ = TunerStreamFinalSnapshot{};
        return Result<void>::success();
    }
    Result<void> detach(const TunerAttachment& attachment) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!same(attachment)) return Result<void>::failure(Error::NOT_FOUND);
        attached_ = false;
        final_.counters = counters_;
        final_.terminal = static_cast<std::uint8_t>(final_terminal_);
        return Result<void>::success();
    }
    void configure_terminal(TunerStreamTerminal terminal,
                            TunerStreamCounters counters) noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        final_terminal_ = terminal;
        counters_ = counters;
    }
    Result<TunerStreamCounters> stats(const TunerAttachment& attachment) const noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return attached_ && same(attachment)
            ? Result<TunerStreamCounters>::success(counters_)
            : Result<TunerStreamCounters>::failure(Error::NOT_FOUND);
    }
    Result<TunerStreamFinalSnapshot> final_snapshot(
        const TunerAttachment& attachment) const noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return same(attachment) ? Result<TunerStreamFinalSnapshot>::success(final_)
                                : Result<TunerStreamFinalSnapshot>::failure(Error::NOT_FOUND);
    }
    Result<void> release_final(const TunerAttachment& attachment) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return same(attachment) ? Result<void>::success()
                                : Result<void>::failure(Error::NOT_FOUND);
    }
    Result<TunerStreamReadResult> read(const TunerAttachment& attachment,
                                        MutableByteView output,
                                        Timeout) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!same(attachment)) return Result<TunerStreamReadResult>::failure(Error::NOT_FOUND);
        if (packets_remaining_ != 0U && output.size >= 188U) {
            for (std::size_t i = 0U; i < 188U; ++i)
                output.data[i] = static_cast<std::uint8_t>(i);
            --packets_remaining_;
            return Result<TunerStreamReadResult>::success(
                {188U, false, false, TunerStreamTerminal::none});
        }
        return Result<TunerStreamReadResult>::success(
            {0U, !attached_, attached_, attached_ ? TunerStreamTerminal::none
                                                   : final_terminal_});
    }
private:
    bool same(const TunerAttachment& other) const noexcept
    {
        return identity_.owner_client_id == other.owner_client_id &&
            identity_.lease_id == other.lease_id &&
            identity_.attachment_id == other.attachment_id &&
            identity_.receiver == other.receiver &&
            identity_.system == other.system && identity_.nonce == other.nonce;
    }
    mutable std::mutex mutex_;
    TunerAttachment identity_{};
    TunerStreamCounters counters_{};
    TunerStreamFinalSnapshot final_{};
    std::size_t packets_remaining_ = 0U;
    TunerStreamTerminal final_terminal_ = TunerStreamTerminal::stopped;
    bool attached_ = false;
};

class CardBackend final : public CardServiceBackend {
public:
    Result<void> set_power(bool on) noexcept override
    {
        power_calls.push_back(on);
        powered.store(on);
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
    std::atomic<bool> powered{false};
    std::atomic<bool> present{true};
};

class CardSession final : public CardProtocolSession {
public:
    CardSession()
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
        if (!initialized_ || apdu.size == 0U || response.size < 2U)
            return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
        response.data[0] = 0x61U;
        response.data[1] = 0x00U;
        return Result<std::size_t>::success(2U);
    }
    bool initialized() const noexcept override { return initialized_; }
    const CardAtrBytes& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override { initialized_ = false; }
    std::size_t initialize_calls = 0U;
    std::size_t transmit_calls = 0U;
private:
    CardAtrBytes atr_{};
    bool initialized_ = false;
};

class Family final : public host::EnclosureFamily {
public:
    explicit Family(std::atomic<bool>& shutdown) noexcept : shutdown_(shutdown) {}
    Result<void> shutdown() noexcept override
    {
        shutdown_.store(true);
        return Result<void>::success();
    }
    TunerServiceBackend& tuner_backend() noexcept override { return tuner_; }
    CardServiceBackend& card_backend() noexcept override { return card_; }
    CardProtocolSession& card_protocol() noexcept override { return card_session_; }
    TunerServiceTime& time() noexcept override { return clock_; }
    TunerStreamControl* stream_control() noexcept override { return &stream_; }
    TunerBackend tuner_;
    CardBackend card_;
    CardSession card_session_;
    Clock clock_;
    Stream stream_;

private:
    std::atomic<bool>& shutdown_;
};

w::ListResponse make_px4_list(const w::Uuid& epoch,
                              const w::Producer& producer)
{
    w::ListResponse list;
    list.epoch = epoch;
    list.producer = producer;
    list.snapshot.sequence = 1U;
    list.topology.state = w::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = 2U;
    list.enclosure.present = true;
    list.enclosure.value.id = 0U;
    list.enclosure.value.model.present = true;
    list.enclosure.value.model.value.name = text<128U>("PX-Q3U4");
    list.enclosure.value.identity.serial_state = w::IdentitySerialState::present;
    list.enclosure.value.identity.serial.present = true;
    list.enclosure.value.identity.serial.value = text<512U>("00001205000960");
    list.enclosure.value.identity.uniqueness = w::Uniqueness::unique_in_scan;
    list.enclosure.value.limits.max_concurrent_streams.present = true;
    list.enclosure.value.limits.max_concurrent_streams.value = 8U;

    list.functions.count = 2U;
    for (std::size_t index = 0U; index < 2U; ++index) {
        auto& function = list.functions.items[index];
        function.id = static_cast<std::uint16_t>(index + 1U);
        function.vid = kQ3U4VendorId;
        function.pid = kQ3U4ProductId;
        function.mode = w::Mode::runtime;
        function.serial_state = w::SerialState::present;
        std::string serial("00001205000960");
        serial.push_back(static_cast<char>('1' + index));
        function.serial.present = true;
        function.serial.value = text<512U>(serial.c_str());
        function.model.present = true;
        function.model.value.name = text<128U>("PX-Q3U4");
        function.identification_reason = w::IdentificationReason::none;
        function.present = true;
    }

    const auto records = px4_product::receiver_records(8U, false);
    if (records) {
        list.receivers.present = true;
        list.receivers.value.count = 8U;
        for (std::size_t index = 0U; index < 8U; ++index) {
            const auto& record = records.value()[index];
            auto& receiver = list.receivers.value.items[index];
            receiver.id = record.global_id;
            receiver.systems.mask = record.system == ipc::System::ISDB_T ? 1U :
                                    record.system == ipc::System::ISDB_S ? 2U : 3U;
            receiver.binding.present = true;
            receiver.binding.value.function_id = record.dev_id;
            receiver.binding.value.local_id = record.local_id;
            receiver.capabilities.measurement_mask = w::kMetricMaskLock;
            if (record.system != ipc::System::ISDB_T) {
                receiver.capabilities.controls |= w::kControlBitLnb;
                receiver.capabilities.lnb.values.count = 2U;
                receiver.capabilities.lnb.values.items[0U] = 0U;
                receiver.capabilities.lnb.values.items[1U] = 15U;
            }
        }
    }
    list.readers.present = true;
    list.readers.value.count = 1U;
    list.readers.value.items[0U].id = 0U;
    return list;
}

enum class FrameReadOutcome : std::uint8_t { frame, eof, error };

FrameReadOutcome read_frame_checked(w::TimedClientTransport& transport,
                                    w::Frame& frame, Error& error) noexcept
{
    error = Error::OK;
    std::array<std::uint8_t, w::kHeaderSize> header_bytes{};
    const auto read_exact = [&transport, &error](std::uint8_t* bytes,
                                                 std::size_t size,
                                                 bool header) noexcept {
        std::size_t offset = 0U;
        while (offset < size) {
            const auto read = transport.read_some({bytes + offset, size - offset},
                                                   Timeout{5000U});
            if (!read) {
                if (header && offset == 0U && read.error() == Error::DISCONNECTED) {
                    error = Error::DISCONNECTED;
                    return false;
                }
                error = read.error();
                return false;
            }
            if (read.value() == 0U) {
                if (header && offset == 0U) {
                    error = Error::DISCONNECTED;
                    return false;
                }
                error = Error::PROTOCOL_ERROR;
                return false;
            }
            offset += read.value();
        }
        return true;
    };
    if (!read_exact(header_bytes.data(), header_bytes.size(), true))
        return error == Error::DISCONNECTED ? FrameReadOutcome::eof :
                                              FrameReadOutcome::error;
    const auto header = w::decode_header({header_bytes.data(), header_bytes.size()});
    if (!header || header.value().payload_length > w::kMaxTsDataPayload) {
        error = Error::PROTOCOL_ERROR;
        return FrameReadOutcome::error;
    }
    static std::array<std::uint8_t, w::kMaxTsDataPayload> payload{};
    if (!read_exact(payload.data(), header.value().payload_length, false))
        return FrameReadOutcome::error;
    frame.header = header.value();
    frame.payload = {payload.data(), header.value().payload_length};
    return FrameReadOutcome::frame;
}

bool read_frame(w::TimedClientTransport& transport, w::Frame& frame) noexcept
{
    Error error = Error::OK;
    return read_frame_checked(transport, frame, error) == FrameReadOutcome::frame;
}

class ScriptedTransport final : public w::TimedClientTransport {
public:
    explicit ScriptedTransport(ByteView input) noexcept
    {
        size_ = std::min(input.size, bytes_.size());
        std::memcpy(bytes_.data(), input.data, size_);
    }
    Result<void> write_all(ByteView, Timeout) noexcept override
    {
        return Result<void>::success();
    }
    Result<std::size_t> read_some(MutableByteView output, Timeout) noexcept override
    {
        if (offset_ == size_) return Result<std::size_t>::success(0U);
        const std::size_t count = std::min(output.size, size_ - offset_);
        std::memcpy(output.data, bytes_.data() + offset_, count);
        offset_ += count;
        return Result<std::size_t>::success(count);
    }
    std::uint64_t monotonic_ms() noexcept override { return 0U; }
    void close() noexcept override {}
private:
    std::array<std::uint8_t, 32U> bytes_{};
    std::size_t size_ = 0U;
    std::size_t offset_ = 0U;
};

bool test_frame_read_boundary_classification()
{
    w::Frame frame{};
    Error error = Error::OK;

    const std::array<std::uint8_t, 3U> partial_header{};
    ScriptedTransport header_transport(ByteView{partial_header.data(), partial_header.size()});
    CHECK(read_frame_checked(header_transport, frame, error) == FrameReadOutcome::error);
    CHECK(error == Error::PROTOCOL_ERROR);

    std::array<std::uint8_t, w::kHeaderSize + 1U> partial_payload{};
    const auto header = w::encode_header(
        {w::Kind::event, w::kOpcodeTsData, 0U, 4U},
        {partial_payload.data(), w::kHeaderSize});
    CHECK(header);
    ScriptedTransport payload_transport(
        ByteView{partial_payload.data(), partial_payload.size()});
    CHECK(read_frame_checked(payload_transport, frame, error) == FrameReadOutcome::error);
    CHECK(error == Error::PROTOCOL_ERROR);
    return true;
}

template <typename Request>
bool write_record(w::TimedClientTransport& transport, std::uint16_t opcode,
                  std::uint32_t request_id, const Request& request,
                  Result<std::size_t> (*encode)(const Request&, MutableByteView) noexcept)
{
    std::array<std::uint8_t, w::kHeaderSize + w::kMaxControlPayload> bytes{};
    const auto size = w::encode_record_frame(w::Kind::request, opcode, request_id,
                                             request, encode,
                                             {bytes.data(), bytes.size()});
    if (!size) return false;
    return transport.write_all({bytes.data(), size.value()}, Timeout{5000U}).has_value();
}

class Fixture final {
public:
    Fixture() : runtime_(), shutdown_(false) {}
    ~Fixture() noexcept { stop(); }

    bool start(const char* instance, bool allow_lnb_15v = false)
    {
        if (!runtime_.valid()) return false;
        auto family = std::unique_ptr<Family>(new Family(shutdown_));
        family_ = family.get();
        family_->tuner_.allow_lnb_15v.store(allow_lnb_15v);
        listener::ContextConfig config;
        config.runtime_directory = runtime_.path();
        config.product = "px4-userland";
        config.instance = instance;
        config.access = host::EndpointAccess::private_user;
        w::Producer producer;
        producer.product = text<32U>("px4-userland");
        producer.version = text<64U>("1.0.0-test");
        producer.core_version.present = true;
        producer.core_version.value = text<64U>("test-core");
        w::Uuid epoch{};
        for (std::size_t i = 0U; i < epoch.size(); ++i)
            epoch[i] = static_cast<std::uint8_t>(i + 1U);
        config.server.epoch = epoch;
        config.server.producer = producer;
        config.server.features = w::kFeatureStateEvent | w::kFeatureCardService |
                                 w::kFeaturePidFilter;
        config.canonical_list = make_px4_list(epoch, producer);
        config.context.epoch = epoch;
        config.context.state = w::ContextState::ready;
        config.context.instance.present = true;
        config.context.instance.value = text<64U>(instance);
        config.stream_engine_epoch = 0x50583400ULL;
        config.stream_arm_budget_ms = 5000U;
        auto assembled = listener::EnclosureContext::assemble(
            std::move(family),
            std::unique_ptr<host::TunerNonceSource>(new Nonce()), config);
        if (!assembled) return false;
        context_ = std::move(assembled.value());
        if (!context_->reserve_namespace()) return false;
        if (!context_->publish()) return false;
        serving_.store(true);
        pthread_attr_t attributes;
        if (pthread_attr_init(&attributes) != 0) return false;
        const bool stack_ok =
            pthread_attr_setstacksize(&attributes, 8U * 1024U * 1024U) == 0;
        const bool started = stack_ok &&
            pthread_create(&poller_, &attributes, &Fixture::poll_entry, this) == 0;
        pthread_attr_destroy(&attributes);
        if (!started) return false;
        poller_started_ = true;
        instance_ = instance;
        return true;
    }

    void stop()
    {
        if (!context_) return;
        serving_.store(false);
        if (poller_started_) {
            pthread_join(poller_, nullptr);
            poller_started_ = false;
        }
        for (;;) {
            const auto closed = context_->close(Timeout{1000U});
            if (!closed || closed.value() == listener::CloseState::quiesced) break;
        }
        context_.reset();
    }

    bool poll_ok() const noexcept { return !poll_error_.load(); }
    Family& family() noexcept { return *family_; }
    const std::string& runtime() const noexcept { return runtime_.path(); }
    const std::string& instance() const noexcept { return instance_; }
    host::EndpointConfig control_endpoint() const noexcept
    {
        return host::EndpointConfig{runtime_.path(), "px4-userland", instance_,
                                    host::EndpointAccess::private_user, -1,
                                    "control.sock"};
    }
    host::EndpointConfig stream_endpoint() const noexcept
    {
        return host::EndpointConfig{runtime_.path(), "px4-userland", instance_,
                                    host::EndpointAccess::private_user, -1,
                                    "stream.sock"};
    }
    bool sockets_cleaned() const noexcept
    {
        const std::string base = runtime_.path() + "/px4-userland/v1/" + instance_;
        return !std::filesystem::exists(base + "/control.sock") &&
               !std::filesystem::exists(base + "/stream.sock");
    }

private:
    static void* poll_entry(void* value)
    {
        static_cast<Fixture*>(value)->poll_loop();
        return nullptr;
    }
    void poll_loop()
    {
        while (serving_.load()) {
            if (!context_->poll_once(Timeout{20U})) {
                poll_error_.store(true);
                return;
            }
        }
    }

    TempRuntime runtime_;
    std::unique_ptr<listener::EnclosureContext> context_;
    Family* family_ = nullptr;
    pthread_t poller_{};
    bool poller_started_ = false;
    std::atomic<bool> serving_{false};
    std::atomic<bool> shutdown_{false};
    std::atomic<bool> poll_error_{false};
    std::string instance_;
};

bool test_list_card_flow_and_shutdown()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000960"));
    auto control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(control);

    const auto status = w::fetch_status(*control.value(), Timeout{5000U});
    CHECK(status);
    CHECK(status.snapshot.list.receivers.present &&
          status.snapshot.list.receivers.value.count == 8U);
    CHECK(status.snapshot.list.receivers.value.items[0U].systems.mask == 2U);
    CHECK(status.snapshot.list.receivers.value.items[7U].id == 7U);
    CHECK(status.snapshot.list.readers.present &&
          status.snapshot.list.readers.value.count == 1U);
    CHECK(status.snapshot.status.receivers.present &&
          status.snapshot.status.receivers.value.count == 8U);
    CHECK(status.snapshot.status.receivers.value.items[0U].state == w::ReceiverState::free);
    control.value()->close();
    control.value().reset();
    control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(control);

    w::CardClientOptions options;
    options.reader_id = 0U;
    const auto opened = w::begin_card(*control.value(), options, Timeout{5000U});
    if (!opened) std::fprintf(stderr, "begin_card error=%u remote=%u poll_ok=%d\n",
        static_cast<unsigned>(opened.error), opened.remote_error_code,
        fixture.poll_ok() ? 1 : 0);
    CHECK(opened);
    auto session = opened.session;
    CHECK(opened.reader_status.present && !opened.reader_status.initialized);
    CHECK(opened.connection.present && opened.connection.atr.present &&
          opened.connection.atr.value.length == 2U);

    auto second_control = host::connect_control(fixture.control_endpoint(),
                                                Timeout{5000U});
    CHECK(second_control);
    const auto second_opened = w::begin_card(*second_control.value(), options,
                                             Timeout{5000U});
    CHECK(second_opened && second_opened.session.card_handle != session.card_handle);
    auto second_session = second_opened.session;

    auto third_control = host::connect_control(fixture.control_endpoint(),
                                               Timeout{5000U});
    CHECK(third_control);
    w::CardClientOptions exclusive_options = options;
    exclusive_options.share = w::Share::exclusive;
    const auto denied = w::begin_card(*third_control.value(), exclusive_options,
                                      Timeout{5000U});
    CHECK(!denied && denied.error == Error::BUSY);
    third_control.value()->close();
    third_control.value().reset();

    w::CardResetRequest foreign_reset{};
    foreign_reset.key.epoch = session.epoch;
    foreign_reset.key.reader_id = session.reader_id;
    foreign_reset.key.bound_generation = session.bound_generation;
    foreign_reset.key.card_handle = session.card_handle;
    CHECK(write_record(*second_control.value(), w::kOpcodeCardReset,
                       second_session.next_request_id,
                       foreign_reset, &w::encode_card_reset_request));
    ++second_session.next_request_id;
    w::Frame foreign_frame{};
    CHECK(read_frame(*second_control.value(), foreign_frame));
    CHECK(foreign_frame.header.kind == w::Kind::error_response);
    const auto foreign_error = w::decode_error_response(foreign_frame.payload);
    CHECK(foreign_error &&
          foreign_error.value().error.code == static_cast<std::uint32_t>(Error::NOT_FOUND));
    const auto second_disconnect = w::card_disconnect(*second_control.value(),
        second_session, w::Disposition::leave, Timeout{5000U});
    if (!second_disconnect) std::fprintf(stderr, "second card disconnect error=%u remote=%u\n",
        static_cast<unsigned>(second_disconnect.error), second_disconnect.remote_error_code);
    CHECK(second_disconnect);
    second_control.value()->close();
    second_control.value().reset();

    w::CardHandleKey key{};
    key.epoch = session.epoch;
    key.reader_id = session.reader_id;
    key.bound_generation = session.bound_generation;
    key.card_handle = session.card_handle;
    w::Frame card_frame;
    w::CardHandleStatusRequest handle_status{};
    handle_status.key = key;
    CHECK(write_record(*control.value(), w::kOpcodeCardHandleStatus, 6U,
                       handle_status, &w::encode_card_handle_status_request));
    CHECK(read_frame(*control.value(), card_frame));
    CHECK(card_frame.header.opcode == w::kOpcodeCardHandleStatus);
    CHECK(static_cast<bool>(w::decode_card_session_result(card_frame.payload)));

    w::BeginTransactionRequest begin{};
    begin.key = key;
    CHECK(write_record(*control.value(), w::kOpcodeBeginTransaction, 7U,
                       begin, &w::encode_begin_transaction_request));
    CHECK(read_frame(*control.value(), card_frame));
    CHECK(card_frame.header.opcode == w::kOpcodeBeginTransaction);
    CHECK(static_cast<bool>(w::decode_begin_transaction_response(card_frame.payload)));

    w::EndTransactionRequest end{};
    end.key = key;
    CHECK(write_record(*control.value(), w::kOpcodeEndTransaction, 8U,
                       end, &w::encode_end_transaction_request));
    CHECK(read_frame(*control.value(), card_frame));
    CHECK(card_frame.header.opcode == w::kOpcodeEndTransaction);
    CHECK(card_frame.payload.size == 0U);

    w::CardReconnectRequest reconnect{};
    reconnect.key = key;
    reconnect.preferred_protocols = 1U;
    CHECK(write_record(*control.value(), w::kOpcodeCardReconnect, 9U,
                       reconnect, &w::encode_card_reconnect_request));
    CHECK(read_frame(*control.value(), card_frame));
    CHECK(card_frame.header.opcode == w::kOpcodeCardReconnect);
    const auto reconnected = w::decode_card_session_result(card_frame.payload);
    CHECK(reconnected && reconnected.value().atr.present &&
          reconnected.value().atr.value.length == 2U);
    session.next_request_id = 10U;

    const auto reset = w::card_reset(*control.value(), session, Timeout{5000U});
    CHECK(reset);

    const std::array<std::uint8_t, 4U> apdu{{0x00U, 0x84U, 0x00U, 0x00U}};
    const auto transmitted = w::card_transmit(*control.value(), session,
        w::CardProtocol::t1, ByteView{apdu.data(), apdu.size()}, Timeout{5000U});
    CHECK(transmitted && transmitted.response.response.length == 2U &&
          transmitted.response.response.bytes[0U] == 0x61U);

    const auto disconnected = w::card_disconnect(*control.value(), session,
        w::Disposition::leave, Timeout{5000U});
    CHECK(disconnected && disconnected.response.consumed);

    fixture.family().card_.present.store(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(300U));

    control.value()->close();
    control.value().reset();
    fixture.stop();
    CHECK(fixture.poll_ok());
    CHECK(fixture.sockets_cleaned());
    return true;
}


bool test_card_wire_ownership_and_generation()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000971"));
    auto first = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    auto second = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    auto third = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(first && second && third);

    w::CardClientOptions options;
    options.reader_id = 0U;
    const auto first_open = w::begin_card(*first.value(), options, Timeout{5000U});
    const auto second_open = w::begin_card(*second.value(), options, Timeout{5000U});
    CHECK(first_open && second_open);
    auto first_session = first_open.session;
    auto second_session = second_open.session;
    CHECK(first_session.card_handle != second_session.card_handle);

    const auto initial = w::card_status(*first.value(), first_session, Timeout{5000U});
    CHECK(initial && initial.response.present);
    CHECK(initial.response.current_generation == first_session.bound_generation);

    w::CardHandleKey first_key{};
    first_key.epoch = first_session.epoch;
    first_key.reader_id = first_session.reader_id;
    first_key.bound_generation = first_session.bound_generation;
    first_key.card_handle = first_session.card_handle;
    w::CardHandleKey second_key = first_key;
    second_key.card_handle = second_session.card_handle;

    w::BeginTransactionRequest begin{};
    begin.key = first_key;
    CHECK(write_record(*first.value(), w::kOpcodeBeginTransaction,
                       first_session.next_request_id, begin,
                       &w::encode_begin_transaction_request));
    ++first_session.next_request_id;
    w::Frame frame{};
    CHECK(read_frame(*first.value(), frame));
    CHECK(frame.header.opcode == w::kOpcodeBeginTransaction &&
          static_cast<bool>(w::decode_begin_transaction_response(frame.payload)));

    const std::array<std::uint8_t, 2U> apdu{{0x00U, 0x84U}};
    w::CardTransmitRequest competing{};
    competing.key = second_key;
    competing.command.length = static_cast<std::uint32_t>(apdu.size());
    std::memcpy(competing.command.bytes.data(), apdu.data(), apdu.size());
    CHECK(write_record(*second.value(), w::kOpcodeCardTransmit,
                       second_session.next_request_id, competing,
                       &w::encode_card_transmit_request));
    ++second_session.next_request_id;
    CHECK(read_frame(*second.value(), frame));
    CHECK(frame.header.kind == w::Kind::error_response);
    const auto competing_error = w::decode_error_response(frame.payload);
    CHECK(competing_error &&
          competing_error.value().error.code == static_cast<std::uint32_t>(Error::BUSY));

    w::EndTransactionRequest end{};
    end.key = first_key;
    end.disposition = w::Disposition::leave;
    CHECK(write_record(*first.value(), w::kOpcodeEndTransaction,
                       first_session.next_request_id, end,
                       &w::encode_end_transaction_request));
    ++first_session.next_request_id;
    CHECK(read_frame(*first.value(), frame));
    CHECK(frame.header.opcode == w::kOpcodeEndTransaction && frame.payload.size == 0U);

    w::CardReconnectRequest upgrade{};
    upgrade.key = second_key;
    upgrade.share = w::Share::exclusive;
    upgrade.preferred_protocols = 1U;
    CHECK(write_record(*second.value(), w::kOpcodeCardReconnect,
                       second_session.next_request_id, upgrade,
                       &w::encode_card_reconnect_request));
    ++second_session.next_request_id;
    CHECK(read_frame(*second.value(), frame));
    CHECK(frame.header.kind == w::Kind::error_response);
    const auto upgrade_error = w::decode_error_response(frame.payload);
    CHECK(upgrade_error &&
          upgrade_error.value().error.code == static_cast<std::uint32_t>(Error::BUSY));

    // The transaction owner disconnects; its transaction must be released.
    begin.key = first_key;
    CHECK(write_record(*first.value(), w::kOpcodeBeginTransaction,
                       first_session.next_request_id, begin,
                       &w::encode_begin_transaction_request));
    ++first_session.next_request_id;
    CHECK(read_frame(*first.value(), frame));
    CHECK(frame.header.kind == w::Kind::response &&
          frame.header.opcode == w::kOpcodeBeginTransaction &&
          static_cast<bool>(w::decode_begin_transaction_response(frame.payload)));
    const auto owner_disconnect = w::card_disconnect(
        *first.value(), first_session, w::Disposition::leave, Timeout{5000U});
    CHECK(owner_disconnect);

    begin.key = second_key;
    CHECK(write_record(*second.value(), w::kOpcodeBeginTransaction,
                       second_session.next_request_id, begin,
                       &w::encode_begin_transaction_request));
    ++second_session.next_request_id;
    CHECK(read_frame(*second.value(), frame));
    CHECK(frame.header.kind == w::Kind::response &&
          frame.header.opcode == w::kOpcodeBeginTransaction &&
          static_cast<bool>(w::decode_begin_transaction_response(frame.payload)));
    end.key = second_key;
    CHECK(write_record(*second.value(), w::kOpcodeEndTransaction,
                       second_session.next_request_id, end,
                       &w::encode_end_transaction_request));
    ++second_session.next_request_id;
    CHECK(read_frame(*second.value(), frame));
    CHECK(frame.header.opcode == w::kOpcodeEndTransaction);

    // An observed removal/reinsert changes the generation; the old handle is
    // stale and a fresh connect succeeds.
    fixture.family().card_.present.store(false);
    const auto removed = w::card_status(*second.value(), second_session, Timeout{5000U});
    CHECK(removed && !removed.response.present);
    CHECK(removed.response.current_generation != second_session.bound_generation);
    fixture.family().card_.present.store(true);
    const auto reinserted = w::card_status(*second.value(), second_session, Timeout{5000U});
    CHECK(reinserted && reinserted.response.present);

    competing.key = second_key;
    CHECK(write_record(*second.value(), w::kOpcodeCardTransmit,
                       second_session.next_request_id, competing,
                       &w::encode_card_transmit_request));
    ++second_session.next_request_id;
    CHECK(read_frame(*second.value(), frame));
    CHECK(frame.header.kind == w::Kind::error_response);
    const auto stale_error = w::decode_error_response(frame.payload);
    CHECK(stale_error &&
          (stale_error.value().error.code ==
               static_cast<std::uint32_t>(Error::CARD_REMOVED) ||
           stale_error.value().error.code ==
               static_cast<std::uint32_t>(Error::NOT_FOUND)));

    const auto fresh = w::begin_card(*third.value(), options, Timeout{5000U});
    CHECK(fresh);
    auto fresh_session = fresh.session;
    CHECK(fresh_session.bound_generation == reinserted.response.current_generation);
    const auto fresh_disconnect = w::card_disconnect(
        *third.value(), fresh_session, w::Disposition::leave, Timeout{5000U});
    CHECK(fresh_disconnect);

    first.value()->close();
    second.value()->close();
    third.value()->close();

    bool powered_off = false;
    for (std::size_t attempt = 0U; attempt < 1000U && !powered_off; ++attempt) {
        powered_off = !fixture.family().card_.powered.load();
        if (!powered_off) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(powered_off);

    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}

bool send_attach(w::TimedClientTransport& transport,
                 const w::RecordingSession& session, bool split,
                 bool split_header = false)
{
    w::AttachRequest attach{};
    attach.epoch = session.epoch;
    attach.lease_id = session.lease_id;
    attach.stream_id = session.stream_id;
    attach.attach_token = session.attach_token;
    std::array<std::uint8_t, 128U> payload{};
    const auto size = w::encode_attach_request(attach, {payload.data(), payload.size()});
    if (!size) return false;
    std::array<std::uint8_t, w::kHeaderSize + 128U> frame{};
    if (!w::encode_header({w::Kind::request, w::kOpcodeAttachStream, 1U,
                           static_cast<std::uint32_t>(size.value())},
                          {frame.data(), w::kHeaderSize})) return false;
    std::memcpy(frame.data() + w::kHeaderSize, payload.data(), size.value());
    const std::size_t total = w::kHeaderSize + size.value();
    if (!split) return transport.write_all({frame.data(), total}, Timeout{5000U}).has_value();
    const std::size_t first = split_header ? w::kHeaderSize - 1U :
                                              w::kHeaderSize + size.value() / 2U;
    return transport.write_all({frame.data(), first}, Timeout{5000U}) &&
           transport.write_all({frame.data() + first, total - first}, Timeout{5000U});
}

bool write_attach(host::EndpointConfig endpoint, const w::RecordingSession& session,
                  bool split, std::unique_ptr<w::TimedClientTransport>& data)
{
    auto connection = host::connect_endpoint(endpoint, Timeout{5000U});
    if (!connection) return false;
    data = std::move(connection.value());
    return send_attach(*data, session, split);
}

bool write_attach_header_split(host::EndpointConfig endpoint,
                               const w::RecordingSession& session,
                               std::unique_ptr<w::TimedClientTransport>& data)
{
    auto connection = host::connect_endpoint(endpoint, Timeout{5000U});
    if (!connection) return false;
    data = std::move(connection.value());
    w::AttachRequest attach{};
    attach.epoch = session.epoch;
    attach.lease_id = session.lease_id;
    attach.stream_id = session.stream_id;
    attach.attach_token = session.attach_token;
    std::array<std::uint8_t, 128U> payload{};
    const auto size = w::encode_attach_request(attach, {payload.data(), payload.size()});
    if (!size) return false;
    std::array<std::uint8_t, w::kHeaderSize + 128U> frame{};
    if (!w::encode_header({w::Kind::request, w::kOpcodeAttachStream, 1U,
                           static_cast<std::uint32_t>(size.value())},
                          {frame.data(), w::kHeaderSize})) return false;
    std::memcpy(frame.data() + w::kHeaderSize, payload.data(), size.value());
    const std::size_t total = w::kHeaderSize + size.value();
    const std::size_t first = w::kHeaderSize - 1U;
    if (!data->write_all({frame.data(), first}, Timeout{5000U})) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(20U));
    return data->write_all({frame.data() + first, total - first}, Timeout{5000U}).has_value();
}

bool attach_data(host::EndpointConfig endpoint, const w::RecordingSession& session,
                 std::unique_ptr<w::TimedClientTransport>& data)
{
    auto connection = host::connect_endpoint(endpoint, Timeout{5000U});
    if (!connection) return false;
    data = std::move(connection.value());
    w::AttachRequest attach{};
    attach.epoch = session.epoch;
    attach.lease_id = session.lease_id;
    attach.stream_id = session.stream_id;
    attach.attach_token = session.attach_token;
    std::array<std::uint8_t, 128U> payload{};
    const auto size = w::encode_attach_request(attach, {payload.data(), payload.size()});
    if (!size) return false;
    std::array<std::uint8_t, w::kHeaderSize + 128U> frame{};
    if (!w::encode_header({w::Kind::request, w::kOpcodeAttachStream, 1U,
                           static_cast<std::uint32_t>(size.value())},
                          {frame.data(), w::kHeaderSize})) return false;
    std::memcpy(frame.data() + w::kHeaderSize, payload.data(), size.value());
    if (!data->write_all({frame.data(), w::kHeaderSize + size.value()}, Timeout{5000U}))
        return false;
    w::Frame response{};
    if (!read_frame(*data, response)) return false;
    if (response.header.kind != w::Kind::response ||
        response.header.opcode != w::kOpcodeAttachStream) return false;
    const auto attached = w::decode_attach_response(response.payload);
    return attached && attached.value().stream_id == session.stream_id;
}

bool run_terminal_case(const char* serial, TunerStreamTerminal terminal,
                       w::StreamEndReason expected_reason,
                       const TunerStreamCounters& counters)
{
    Fixture fixture;
    CHECK(fixture.start(serial));
    auto control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(control);
    w::RecordingOptions options{};
    options.receiver_id = 2U;
    options.tune.epoch = w::Uuid{};
    options.tune.system = w::System::isdb_t;
    options.tune.frequency_hz = 500000000U;
    options.tune.timeout_ms = 1000U;
    const auto recording = w::begin_recording(*control.value(), options, Timeout{10000U});
    CHECK(recording);
    auto session = recording.session;
    std::unique_ptr<w::TimedClientTransport> data;
    CHECK(attach_data(fixture.stream_endpoint(), session, data));
    w::Frame frame{};
    CHECK(read_frame(*data, frame));
    CHECK(frame.header.kind == w::Kind::event &&
          frame.header.opcode == w::kOpcodeTsDataEvent);

    fixture.family().stream_.configure_terminal(terminal, counters);
    const auto stopped = w::stop_recording(*control.value(), session, Timeout{5000U});
    CHECK(stopped && stopped.response.final.id == session.stream_id);
    CHECK(read_frame(*data, frame));
    CHECK(frame.header.kind == w::Kind::event &&
          frame.header.opcode == w::kOpcodeStreamEndEvent);
    const auto ended = w::decode_stream_end_event(frame.payload);
    CHECK(ended && ended.value().final.id == session.stream_id &&
          ended.value().final.reason == expected_reason);
    CHECK(ended.value().final.statistics.entries.count == 8U);

    const auto check_counter = [&ended](std::uint8_t id, std::uint64_t value,
                                         w::MeasureState state) {
        for (std::size_t index = 0U;
             index < ended.value().final.statistics.entries.count; ++index) {
            const auto& entry = ended.value().final.statistics.entries.items[index];
            if (entry.id != id) continue;
            const std::uint64_t expected =
                state == w::MeasureState::valid ? value : 0U;
            return entry.counter.measurement.value == expected &&
                   entry.counter.measurement.state == state;
        }
        return false;
    };
    CHECK(check_counter(w::kCounterIdTransmittedPackets, counters.packets,
                        counters.availability[0U]));
    CHECK(check_counter(w::kCounterIdQueueDroppedPackets, counters.queue_drops,
                        counters.availability[5U]));
    CHECK(check_counter(w::kCounterIdStreamBytes, counters.bytes,
                        counters.availability[1U]));
    CHECK(check_counter(w::kCounterIdSyncErrors, counters.sync_errors,
                        counters.availability[2U]));
    CHECK(check_counter(w::kCounterIdTeiPackets, counters.tei_packets,
                        counters.availability[3U]));
    CHECK(check_counter(w::kCounterIdContinuityErrors, counters.continuity_errors,
                        counters.availability[4U]));
    CHECK(check_counter(w::kCounterIdStreamUsbErrors, counters.usb_errors,
                        counters.availability[6U]));
    CHECK(check_counter(w::kCounterIdEmptyIntervals, counters.empty_intervals,
                        counters.availability[7U]));

    data->close();
    data.reset();
    CHECK(w::release_recording(*control.value(), session, Timeout{5000U}));
    control.value()->close();
    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}

bool test_terminal_reasons_and_statistics()
{
    TunerStreamCounters slow{};
    slow.packets = 11U;
    slow.bytes = 22U;
    slow.sync_errors = 33U;
    slow.tei_packets = 44U;
    slow.continuity_errors = 55U;
    slow.queue_drops = 66U;
    slow.usb_errors = 77U;
    slow.empty_intervals = 88U;
    slow.availability = {
        w::MeasureState::valid, w::MeasureState::unknown,
        w::MeasureState::valid, w::MeasureState::unsupported,
        w::MeasureState::valid, w::MeasureState::valid,
        w::MeasureState::valid, w::MeasureState::valid};
    TunerStreamCounters usb{};
    usb.packets = 101U;
    usb.bytes = 202U;
    usb.sync_errors = 303U;
    usb.tei_packets = 404U;
    usb.continuity_errors = 505U;
    usb.queue_drops = 606U;
    usb.usb_errors = 707U;
    usb.empty_intervals = 808U;
    usb.availability = {
        w::MeasureState::valid, w::MeasureState::valid,
        w::MeasureState::unknown, w::MeasureState::valid,
        w::MeasureState::valid, w::MeasureState::unsupported,
        w::MeasureState::valid, w::MeasureState::valid};

    return run_terminal_case("00001205000972", TunerStreamTerminal::slow_consumer,
                             w::StreamEndReason::slow_consumer, slow) &&
           run_terminal_case("00001205000973", TunerStreamTerminal::usb_error,
                             w::StreamEndReason::backend_error, usb);
}

bool test_tuner_flow_and_stream_end()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000962"));
    auto control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(control);

    w::RecordingOptions options{};
    options.receiver_id = 2U;
    options.tune.epoch = w::Uuid{};
    options.tune.system = w::System::isdb_t;
    options.tune.frequency_hz = 500000000U;
    options.tune.timeout_ms = 1000U;
    const auto recording = w::begin_recording(*control.value(), options, Timeout{10000U});
    if (!recording) std::fprintf(stderr, "begin_recording error=%u remote=%u\n",
        static_cast<unsigned>(recording.error), recording.remote_error_code);
    CHECK(recording);
    auto session = recording.session;

    auto busy_control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(busy_control);
    const auto busy = w::begin_recording(*busy_control.value(), options, Timeout{10000U});
    CHECK(!busy && busy.error == Error::BUSY);
    busy_control.value()->close();

    std::unique_ptr<w::TimedClientTransport> data;
    CHECK(attach_data(fixture.stream_endpoint(), session, data));
    w::Frame ts_frame{};
    CHECK(read_frame(*data, ts_frame));
    CHECK(ts_frame.header.kind == w::Kind::event &&
          ts_frame.header.opcode == w::kOpcodeTsDataEvent);
    const auto ts = w::decode_ts_data(ts_frame.payload);
    CHECK(ts && ts.value().stream_id == session.stream_id &&
          ts.value().packets.size == 188U);

    const auto stopped = w::stop_recording(*control.value(), session, Timeout{5000U});
    CHECK(stopped && stopped.response.final.id == session.stream_id);
    w::Frame end_frame{};
    CHECK(read_frame(*data, end_frame));
    CHECK(end_frame.header.kind == w::Kind::event &&
          end_frame.header.opcode == w::kOpcodeStreamEndEvent);
    const auto ended = w::decode_stream_end_event(end_frame.payload);
    CHECK(ended && ended.value().final.id == session.stream_id);
    data->close();
    data.reset();
    CHECK(w::release_recording(*control.value(), session, Timeout{5000U}));
    control.value()->close();
    control.value().reset();

    // PX4 publishes LNB control for satellite receivers, but this backend
    // refuses 15 V. The TUNE request must fail before any GPIO call.
    auto lnb_control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(lnb_control);
    w::RecordingOptions lnb_options{};
    lnb_options.receiver_id = 0U;
    lnb_options.tune.system = w::System::isdb_s;
    lnb_options.tune.frequency_hz = 200000000U;
    lnb_options.tune.selection = w::Selection::slot;
    lnb_options.tune.selection_value = 0U;
    lnb_options.tune.lnb_voltage.present = true;
    lnb_options.tune.lnb_voltage.value = 15U;
    lnb_options.tune.timeout_ms = 1000U;
    const auto denied = w::begin_recording(*lnb_control.value(), lnb_options,
                                           Timeout{10000U});
    CHECK(!denied && denied.error == Error::UNSUPPORTED);
    CHECK(fixture.family().tuner_.lnb_gpio_calls.load() == 0U);

    // The refused lease must be cleaned up so a valid satellite recording can
    // use the same receiver.
    lnb_options.tune.lnb_voltage.present = false;
    lnb_control.value()->close();
    lnb_control.value().reset();
    std::unique_ptr<w::TimedClientTransport> satellite_control;
    w::RecordingControlResult satellite{};
    for (std::size_t attempt = 0U; attempt < 300U; ++attempt) {
        auto connection = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
        if (!connection) continue;
        satellite = w::begin_recording(*connection.value(), lnb_options, Timeout{10000U});
        if (satellite) {
            satellite_control = std::move(connection.value());
            break;
        }
        connection.value()->close();
        if (satellite.error != Error::BUSY && satellite.error != Error::NOT_READY) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10U));
    }
    if (!satellite) std::fprintf(stderr, "satellite begin error=%u remote=%u\n",
        static_cast<unsigned>(satellite.error), satellite.remote_error_code);
    CHECK(satellite && satellite_control);
    CHECK(fixture.family().tuner_.open_calls[0U] >= 1U);
    CHECK(w::stop_recording(*satellite_control, satellite.session, Timeout{5000U}));
    CHECK(w::release_recording(*satellite_control, satellite.session, Timeout{5000U}));
    satellite_control->close();
    satellite_control.reset();

    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}


class LargeStackThread final {
public:
    ~LargeStackThread() noexcept { join(); }
    bool start(std::function<void()> work)
    {
        work_ = std::move(work);
        pthread_attr_t attributes;
        if (pthread_attr_init(&attributes) != 0) return false;
        const bool stack_ok =
            pthread_attr_setstacksize(&attributes, 8U * 1024U * 1024U) == 0;
        const bool started = stack_ok &&
            pthread_create(&thread_, &attributes, &LargeStackThread::entry, this) == 0;
        pthread_attr_destroy(&attributes);
        started_ = started;
        return started;
    }
    void join() noexcept
    {
        if (started_) {
            pthread_join(thread_, nullptr);
            started_ = false;
        }
    }

private:
    static void* entry(void* value)
    {
        static_cast<LargeStackThread*>(value)->work_();
        return nullptr;
    }
    std::function<void()> work_;
    pthread_t thread_{};
    bool started_ = false;
};

bool wait_flag(const std::atomic<bool>& flag)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!flag.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return flag.load();
}

w::RecordingOptions recording_options(std::uint16_t receiver, w::System system)
{
    w::RecordingOptions options{};
    options.receiver_id = receiver;
    options.tune.system = system;
    options.tune.frequency_hz = system == w::System::isdb_s ? 200000000U : 500000000U;
    if (system == w::System::isdb_s) {
        options.tune.selection = w::Selection::slot;
        options.tune.selection_value = 0U;
    }
    options.tune.timeout_ms = 1000U;
    return options;
}

bool begin_and_release(host::EndpointConfig endpoint, std::uint16_t receiver,
                       w::System system)
{
    auto control = host::connect_control(endpoint, Timeout{5000U});
    if (!control) return false;
    const auto options = recording_options(receiver, system);
    auto recording = w::begin_recording(*control.value(), options, Timeout{10000U});
    if (!recording) return false;
    const bool stopped = static_cast<bool>(
        w::stop_recording(*control.value(), recording.session, Timeout{5000U}));
    const bool released = static_cast<bool>(
        w::release_recording(*control.value(), recording.session, Timeout{5000U}));
    control.value()->close();
    return stopped && released;
}

class StartCaptureBarrierGuard final {
public:
    explicit StartCaptureBarrierGuard(std::atomic<bool>& flag) noexcept : flag_(flag) {}
    ~StartCaptureBarrierGuard() noexcept
    {
        if (active_) flag_.store(false);
    }
    void dismiss() noexcept { active_ = false; }
private:
    std::atomic<bool>& flag_;
    bool active_ = true;
};

class BlockedOperationGuard final {
public:
    BlockedOperationGuard(std::atomic<bool>& blocked, LargeStackThread& thread,
                          Fixture& fixture) noexcept
        : blocked_(blocked), thread_(thread), fixture_(fixture)
    {
    }
    ~BlockedOperationGuard() noexcept
    {
        if (!active_) return;
        blocked_.store(false);
        thread_.join();
        fixture_.stop();
    }
    void dismiss() noexcept { active_ = false; }

private:
    std::atomic<bool>& blocked_;
    LargeStackThread& thread_;
    Fixture& fixture_;
    bool active_ = true;
};

bool test_blocked_operations_and_lanes()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000963"));
    auto blocked_control = host::connect_control(fixture.control_endpoint(),
                                                 Timeout{5000U});
    CHECK(blocked_control);

    fixture.family().tuner_.block_acquire.store(true);
    std::atomic<bool> acquire_done{false};
    bool acquire_ok = false;
    LargeStackThread acquire_thread;
    BlockedOperationGuard acquire_guard(fixture.family().tuner_.block_acquire,
                                        acquire_thread, fixture);
    CHECK(acquire_thread.start([&] {
        const auto options = recording_options(0U, w::System::isdb_s);
        acquire_ok = static_cast<bool>(w::begin_recording(
            *blocked_control.value(), options, Timeout{10000U}));
        acquire_done.store(true);
    }));
    CHECK(wait_flag(fixture.family().tuner_.acquire_entered));

    // A second receiver lane must stay usable while receiver 0 blocks.
    CHECK(begin_and_release(fixture.control_endpoint(), 4U, w::System::isdb_s));
    auto status_control = host::connect_control(fixture.control_endpoint(),
                                                Timeout{5000U});
    CHECK(status_control);
    const auto lane_status = w::fetch_status(*status_control.value(), Timeout{5000U});
    CHECK(lane_status);
    w::ListRequest list_request{};
    list_request.epoch = lane_status.snapshot.list.epoch;
    CHECK(write_record(*status_control.value(), w::kOpcodeList, 11U,
                       list_request, &w::encode_list_request));
    w::Frame lane_frame{};
    CHECK(read_frame(*status_control.value(), lane_frame));
    CHECK(lane_frame.header.opcode == w::kOpcodeList);
    const auto lane_list = w::decode_list_response(lane_frame.payload);
    CHECK(lane_list && lane_list.value().receivers.present &&
          lane_list.value().receivers.value.count == 8U);
    status_control.value()->close();

    auto card_control = host::connect_control(fixture.control_endpoint(),
                                              Timeout{5000U});
    CHECK(card_control);
    w::CardClientOptions card_options;
    card_options.reader_id = 0U;
    const auto card_open = w::begin_card(*card_control.value(), card_options,
                                         Timeout{5000U});
    CHECK(card_open);
    auto card_session = card_open.session;
    const auto card_close = w::card_disconnect(
        *card_control.value(), card_session, w::Disposition::leave, Timeout{5000U});
    CHECK(card_close);
    card_control.value()->close();
    CHECK(!acquire_done.load());

    blocked_control.value()->close();
    fixture.family().tuner_.block_acquire.store(false);
    acquire_thread.join();
    acquire_guard.dismiss();
    CHECK(acquire_done.load() && !acquire_ok);
    blocked_control.value().reset();

    bool reacquired = false;
    for (std::size_t attempt = 0U; attempt < 300U && !reacquired; ++attempt) {
        reacquired = begin_and_release(fixture.control_endpoint(), 0U,
                                       w::System::isdb_s);
        if (!reacquired) std::this_thread::sleep_for(std::chrono::milliseconds(10U));
    }
    CHECK(reacquired);

    // Tune blocked in the same receiver lane: a client disconnect must release
    // the lease and let a replacement client acquire the receiver.
    auto tune_control = host::connect_control(fixture.control_endpoint(),
                                              Timeout{5000U});
    CHECK(tune_control);
    fixture.family().tuner_.block_tune.store(true);
    std::atomic<bool> tune_done{false};
    bool tune_ok = false;
    LargeStackThread tune_thread;
    BlockedOperationGuard tune_guard(fixture.family().tuner_.block_tune,
                                     tune_thread, fixture);
    CHECK(tune_thread.start([&] {
        const auto options = recording_options(0U, w::System::isdb_s);
        tune_ok = static_cast<bool>(w::begin_recording(
            *tune_control.value(), options, Timeout{10000U}));
        tune_done.store(true);
    }));
    CHECK(wait_flag(fixture.family().tuner_.tune_entered));
    tune_control.value()->close();
    fixture.family().tuner_.block_tune.store(false);
    tune_thread.join();
    tune_guard.dismiss();
    CHECK(tune_done.load() && !tune_ok);
    tune_control.value().reset();

    bool recovered = false;
    for (std::size_t attempt = 0U; attempt < 300U && !recovered; ++attempt) {
        recovered = begin_and_release(fixture.control_endpoint(), 0U,
                                      w::System::isdb_s);
        if (!recovered) std::this_thread::sleep_for(std::chrono::milliseconds(10U));
    }
    CHECK(recovered);

    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}


bool attach_expect_error(host::EndpointConfig endpoint,
                         const w::RecordingSession& session, Error expected)
{
    auto connection = host::connect_endpoint(endpoint, Timeout{5000U});
    if (!connection) return false;
    w::AttachRequest attach{};
    attach.epoch = session.epoch;
    attach.lease_id = session.lease_id;
    attach.stream_id = session.stream_id;
    attach.attach_token = session.attach_token;
    std::array<std::uint8_t, 128U> payload{};
    const auto size = w::encode_attach_request(attach, {payload.data(), payload.size()});
    if (!size) return false;
    std::array<std::uint8_t, w::kHeaderSize + 128U> frame{};
    if (!w::encode_header({w::Kind::request, w::kOpcodeAttachStream, 1U,
                           static_cast<std::uint32_t>(size.value())},
                          {frame.data(), w::kHeaderSize})) return false;
    std::memcpy(frame.data() + w::kHeaderSize, payload.data(), size.value());
    if (!connection.value()->write_all(
            {frame.data(), w::kHeaderSize + size.value()}, Timeout{5000U})) return false;
    w::Frame response{};
    if (!read_frame(*connection.value(), response)) return false;
    if (response.header.kind != w::Kind::error_response) return false;
    const auto error = w::decode_error_response(response.payload);
    connection.value()->close();
    return error && error.value().error.code == static_cast<std::uint32_t>(expected);
}

bool test_attach_failure_and_isolation()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000966"));
    fixture.family().tuner_.fail_capture_start.store(true);

    auto control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(control);
    const auto options = recording_options(2U, w::System::isdb_t);
    auto recording = w::begin_recording(*control.value(), options, Timeout{10000U});
    if (!recording) std::fprintf(stderr, "attach failure begin error=%u remote=%u\n",
        static_cast<unsigned>(recording.error), recording.remote_error_code);
    CHECK(recording);
    CHECK(attach_expect_error(fixture.stream_endpoint(), recording.session,
                              Error::UNSUPPORTED));

    // The refused stream owner must not keep the receiver lease. Another
    // receiver still works while the owner connection is dropped.
    control.value()->close();
    control.value().reset();
    CHECK(begin_and_release(fixture.control_endpoint(), 3U, w::System::isdb_t));

    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}



bool test_hup_before_attach_completion()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000968"));
    fixture.family().tuner_.block_start_capture.store(true);
    StartCaptureBarrierGuard barrier_guard(fixture.family().tuner_.block_start_capture);
    auto control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(control);
    const auto options = recording_options(2U, w::System::isdb_t);
    auto recording = w::begin_recording(*control.value(), options, Timeout{10000U});
    CHECK(recording);
    std::unique_ptr<w::TimedClientTransport> data;
    CHECK(write_attach(fixture.stream_endpoint(), recording.session, false, data));
    CHECK(wait_flag(fixture.family().tuner_.start_capture_entered));
    data->close();
    data.reset();
    fixture.family().tuner_.block_start_capture.store(false);
    // The control service must not wedge on the abandoned attach. Other
    // receivers and a fresh connection continue to work. Cleanup completion
    // and original-lease RELEASE are tracked separately in issue #137.
    CHECK(begin_and_release(fixture.control_endpoint(), 3U, w::System::isdb_t));
    barrier_guard.dismiss();
    control.value()->close();
    control.value().reset();
    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}

// This covers two writes split at header-1, but does not observe the
// listener receiving the first partial header. That old-case observation
// remains partial/unreached; see issue #132.
bool test_header_split_write_attach()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000970"));
    auto control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(control);
    const auto options = recording_options(2U, w::System::isdb_t);
    auto recording = w::begin_recording(*control.value(), options, Timeout{10000U});
    CHECK(recording);

    std::unique_ptr<w::TimedClientTransport> data;
    CHECK(write_attach_header_split(fixture.stream_endpoint(), recording.session, data));
    w::Frame response{};
    CHECK(read_frame(*data, response));
    CHECK(response.header.kind == w::Kind::response &&
          response.header.opcode == w::kOpcodeAttachStream);

    data->close();
    data.reset();
    (void)w::stop_recording(*control.value(), recording.session, Timeout{5000U});
    control.value()->close();
    control.value().reset();
    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}

bool test_fragmented_and_duplicate_attach()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000969"));
    auto control = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(control);
    const auto options = recording_options(2U, w::System::isdb_t);
    auto recording = w::begin_recording(*control.value(), options, Timeout{10000U});
    CHECK(recording);

    std::unique_ptr<w::TimedClientTransport> data;
    CHECK(write_attach(fixture.stream_endpoint(), recording.session, true, data));
    w::Frame response{};
    CHECK(read_frame(*data, response));
    CHECK(response.header.kind == w::Kind::response &&
          response.header.opcode == w::kOpcodeAttachStream);

    // The attached data connection accepts no second inbound frame. The
    // one-use token is rejected even on the connection that already attached.
    CHECK(send_attach(*data, recording.session, false));
    bool disconnected = false;
    for (std::size_t attempt = 0U; attempt < 200U && !disconnected; ++attempt) {
        w::Frame frame{};
        Error error = Error::OK;
        const auto outcome = read_frame_checked(*data, frame, error);
        if (outcome == FrameReadOutcome::eof) {
            disconnected = true;
            break;
        }
        if (outcome == FrameReadOutcome::error) {
            std::fprintf(stderr, "second-attach read error=%u\n",
                         static_cast<unsigned>(error));
            return false;
        }
        // Only the already-started stream's TS_DATA events may precede the
        // close. An ATTACH success response, error response, or any other
        // frame proves the second frame was accepted and must fail the test.
        CHECK(frame.header.kind == w::Kind::event &&
              frame.header.opcode == w::kOpcodeTsDataEvent);
        const auto ts = w::decode_ts_data(frame.payload);
        CHECK(ts && ts.value().stream_id == recording.session.stream_id &&
              ts.value().packets.size != 0U);
    }
    CHECK(disconnected);
    data->close();
    data.reset();

    // Reusing the same one-use attach token must not create a second session.
    auto duplicate = host::connect_endpoint(fixture.stream_endpoint(), Timeout{5000U});
    CHECK(duplicate);
    CHECK(send_attach(*duplicate.value(), recording.session, false));
    w::Frame duplicate_frame{};
    CHECK(read_frame(*duplicate.value(), duplicate_frame));
    CHECK(duplicate_frame.header.kind == w::Kind::error_response);
    const auto duplicate_error = w::decode_error_response(duplicate_frame.payload);
    CHECK(duplicate_error &&
          duplicate_error.value().error.code == static_cast<std::uint32_t>(Error::NOT_FOUND));
    duplicate.value()->close();

    (void)w::stop_recording(*control.value(), recording.session, Timeout{5000U});
    control.value()->close();
    control.value().reset();
    bool reacquired = false;
    for (std::size_t attempt = 0U; attempt < 300U && !reacquired; ++attempt) {
        reacquired = begin_and_release(fixture.control_endpoint(), 2U,
                                       w::System::isdb_t);
        if (!reacquired) std::this_thread::sleep_for(std::chrono::milliseconds(10U));
    }
    CHECK(reacquired);
    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}

bool test_no_hello_and_malformed_disconnect()
{
    Fixture fixture;
    CHECK(fixture.start("00001205000961"));
    auto raw = host::connect_control(fixture.control_endpoint(), Timeout{5000U});
    CHECK(raw);
    std::array<std::uint8_t, 8U> buffer{};
    const auto timeout = raw.value()->read_some(
        MutableByteView{buffer.data(), buffer.size()}, Timeout{20U});
    CHECK(!timeout && timeout.error() == Error::TIMEOUT);

    const std::array<std::uint8_t, w::kHeaderSize> malformed{};
    CHECK(raw.value()->write_all({malformed.data(), malformed.size()},
                                 Timeout{5000U}));
    Result<std::size_t> disconnected = Result<std::size_t>::failure(Error::TIMEOUT);
    for (std::size_t attempt = 0U; attempt < 100U; ++attempt) {
        disconnected = raw.value()->read_some(
            MutableByteView{buffer.data(), buffer.size()}, Timeout{20U});
        if ((disconnected && disconnected.value() == 0U) ||
            (!disconnected && disconnected.error() == Error::DISCONNECTED)) break;
    }
    CHECK((disconnected && disconnected.value() == 0U) ||
          (!disconnected && disconnected.error() == Error::DISCONNECTED));
    raw.value()->close();
    fixture.stop();
    CHECK(fixture.poll_ok());
    return true;
}

}  // namespace

int main()
{
    return test_list_card_flow_and_shutdown() &&
           test_card_wire_ownership_and_generation() &&
           test_terminal_reasons_and_statistics() &&
           test_tuner_flow_and_stream_end() &&
           test_blocked_operations_and_lanes() &&
           test_attach_failure_and_isolation() &&
           test_hup_before_attach_completion() &&
           test_frame_read_boundary_classification() &&
           test_header_split_write_attach() &&
           test_fragmented_and_duplicate_attach() &&
           test_no_hello_and_malformed_disconnect() ? 0 : 1;
}

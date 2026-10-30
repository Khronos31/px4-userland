// SPDX-License-Identifier: GPL-2.0-only
#include "dtv/userland/host_listener_context/enclosure_context.h"
#include "dtv/userland/host_runtime/control_socket.h"
#include "px4/ipc.h"
#include "px4/tuner_service.h"
#include <dtv/userland/host_daemon/card_service.h>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#include <sys/stat.h>

namespace {
namespace w = dtv::userland::wire;
using namespace dtv::userland;
using namespace dtv::userland::host_listener_context;
using namespace dtv::userland::host_runtime;
using namespace px4::userland;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        if (!(condition)) {                                                                 \
            std::fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__,       \
                         #condition);                                                       \
            return false;                                                                   \
        }                                                                                   \
    } while (false)

class TunerBackend final : public TunerServiceBackend {
public:
    std::uint8_t receiver_count() const noexcept override { return 8U; }
    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        std::unique_lock<std::mutex> lock(mutex_);
        opened_[receiver].store(true, std::memory_order_release);
        changed_.notify_all();
        if (receiver == 0U)
            changed_.wait(lock, [this] { return !block_receiver0_; });
        return Result<void>::success();
    }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t,
                                  std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<bool> is_locked(std::uint8_t, ipc::System) noexcept override
    { return Result<bool>::success(true); }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    { return Result<void>::success(); }
    Result<void> close_receiver(std::uint8_t) noexcept override
    { return Result<void>::success(); }

    bool wait_opened(std::uint8_t receiver)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        return changed_.wait_for(lock, std::chrono::seconds(5), [this, receiver] {
            return opened_[receiver].load(std::memory_order_acquire);
        });
    }
    void release_receiver0()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        block_receiver0_ = false;
        changed_.notify_all();
    }

private:
    std::mutex mutex_;
    std::condition_variable changed_;
    std::array<std::atomic<bool>, 8U> opened_{};
    bool block_receiver0_ = true;
};

class CardBackend final : public CardServiceBackend {
public:
    Result<void> set_power(bool) noexcept override { return Result<void>::success(); }
    Result<void> initialize_uart() noexcept override { return Result<void>::success(); }
    Result<bool> detect_card() noexcept override
    {
        detections.fetch_add(1U, std::memory_order_relaxed);
        return Result<bool>::success(true);
    }
    std::atomic<std::size_t> detections{0U};
};

class CardSession final : public CardProtocolSession {
public:
    Result<void> initialize() noexcept override { return Result<void>::success(); }
    Result<std::size_t> transmit(ByteView, MutableByteView) noexcept override
    { return Result<std::size_t>::success(0U); }
    bool initialized() const noexcept override { return false; }
    const CardAtrBytes& atr() const noexcept override { return atr_; }
    void invalidate() noexcept override {}
private:
    CardAtrBytes atr_{};
};

class TestTime final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    { std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds)); }
};

class Nonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, ipc::kNonceLength>> generate() noexcept override
    {
        std::array<std::uint8_t, ipc::kNonceLength> value{};
        value[0U] = 1U;
        return Result<std::array<std::uint8_t, ipc::kNonceLength>>::success(value);
    }
};

class Family final : public EnclosureFamily {
public:
    Result<void> shutdown() noexcept override { return Result<void>::success(); }
    TunerServiceBackend& tuner_backend() noexcept override { return tuner_; }
    CardServiceBackend& card_backend() noexcept override { return card_; }
    CardProtocolSession& card_protocol() noexcept override { return card_session_; }
    TunerServiceTime& time() noexcept override { return time_; }
    TunerStreamControl* stream_control() noexcept override { return nullptr; }
    TunerBackend tuner_;
    CardBackend card_;
private:
    CardSession card_session_;
    TestTime time_;
};

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
    for (std::size_t i = 0U; i < list.epoch.size(); ++i)
        list.epoch[i] = static_cast<std::uint8_t>(i + 1U);
    list.producer.product = text<32U>("px4-userland");
    list.producer.version = text<64U>("1");
    list.topology.state = w::TopologyState::complete;
    list.topology.expected_function_count.present = true;
    list.topology.expected_function_count.value = 2U;
    list.functions.count = 2U;
    for (std::size_t i = 0U; i < list.functions.count; ++i) {
        auto& function = list.functions.items[i];
        function.id = static_cast<std::uint16_t>(i + 1U);
        function.vid = 0x0511U;
        function.pid = 0x084aU;
        function.mode = w::Mode::runtime;
        function.serial_state = w::SerialState::absent;
        function.identification_reason = w::IdentificationReason::descriptor_unreadable;
        function.present = true;
    }
    list.enclosure.present = true;
    list.enclosure.value.id = 0U;
    list.enclosure.value.identity.serial_state = w::IdentitySerialState::absent;
    const auto records = ipc::receiver_records(8U, true);
    if (records) {
        list.receivers.present = true;
        list.receivers.value.count = 8U;
        for (std::size_t i = 0U; i < 8U; ++i) {
            auto& receiver = list.receivers.value.items[i];
            receiver.id = records.value()[i].global_id;
            receiver.systems.mask = records.value()[i].system == ipc::System::ISDB_T ? 1U :
                records.value()[i].system == ipc::System::ISDB_S ? 2U : 3U;
            receiver.binding.present = true;
            receiver.binding.value.function_id = records.value()[i].dev_id;
            receiver.binding.value.local_id = records.value()[i].local_id;
        }
    }
    list.readers.present = true;
    list.readers.value.count = 1U;
    list.readers.value.items[0U].id = 0U;
    return list;
}

bool read_frame(w::TimedClientTransport& transport, std::vector<std::uint8_t>& bytes)
{
    bytes.resize(w::kMaxFrameSize);
    std::size_t size = 0U;
    while (size < w::kHeaderSize) {
        const auto read = transport.read_some({bytes.data() + size, bytes.size() - size},
                                              Timeout{5000U});
        if (!read || read.value() == 0U) return false;
        size += read.value();
    }
    const auto header = w::decode_header({bytes.data(), w::kHeaderSize});
    if (!header) return false;
    const std::size_t frame_size = w::kHeaderSize + header.value().payload_length;
    if (frame_size > bytes.size()) return false;
    while (size < frame_size) {
        const auto read = transport.read_some({bytes.data() + size, bytes.size() - size},
                                              Timeout{5000U});
        if (!read || read.value() == 0U) return false;
        size += read.value();
    }
    bytes.resize(size);
    return static_cast<bool>(w::decode_frame({bytes.data(), bytes.size()}));
}

template <typename Record>
bool exchange_record(w::TimedClientTransport& transport, std::uint16_t opcode,
                     std::uint32_t request_id, const Record& request,
                     Result<std::size_t> (*encode)(const Record&, MutableByteView) noexcept,
                     std::vector<std::uint8_t>& response)
{
    std::array<std::uint8_t, w::kMaxFrameSize> bytes{};
    const auto size = w::encode_record_frame(w::Kind::request, opcode, request_id,
        request, encode, {bytes.data(), bytes.size()});
    if (!size || !transport.write_all({bytes.data(), size.value()}, Timeout{5000U}))
        return false;
    if (!read_frame(transport, response)) return false;
    const auto decoded = w::decode_frame({response.data(), response.size()});
    return decoded && decoded.value().header.kind == w::Kind::response &&
        decoded.value().header.opcode == opcode &&
        decoded.value().header.request_id == request_id;
}

bool open_client(const std::string& root,
                 std::unique_ptr<w::TimedClientTransport>& transport,
                 w::StatusSnapshot& snapshot)
{
    const EndpointConfig endpoint{root, "px4-userland", "worker-test"};
    auto connected = connect_control(endpoint, Timeout{5000U});
    if (!connected) return false;
    transport = std::move(connected.value());
    const auto fetched = w::fetch_status(*transport, Timeout{5000U},
                                         w::kFeatureCardService);
    if (!fetched) return false;
    snapshot = fetched.snapshot;
    return true;
}

bool acquire(w::TimedClientTransport& transport, const w::StatusSnapshot& snapshot,
             std::uint16_t receiver)
{
    std::vector<std::uint8_t> bytes;
    const w::AcquireRequest request{snapshot.list.epoch, receiver};
    if (!exchange_record(transport, w::kOpcodeAcquire, 4U, request,
                         &w::encode_acquire_request, bytes)) return false;
    const auto frame = w::decode_frame({bytes.data(), bytes.size()});
    if (!frame) return false;
    const auto decoded = w::decode_acquire_response(frame.value().payload);
    return decoded && decoded.value().lease_id != 0U;
}

bool card_status(w::TimedClientTransport& transport,
                 const w::StatusSnapshot& snapshot)
{
    std::vector<std::uint8_t> bytes;
    const w::CardStatusRequest request{w::ReaderKey{snapshot.list.epoch, 0U}};
    if (!exchange_record(transport, w::kOpcodeCardStatus, 4U, request,
                         &w::encode_card_status_request, bytes)) {
        std::fprintf(stderr, "CARD_STATUS exchange failed\n");
        return false;
    }
    const auto frame = w::decode_frame({bytes.data(), bytes.size()});
    if (!frame) return false;
    const auto decoded = w::decode_card_status_response(frame.value().payload);
    if (!decoded)
        std::fprintf(stderr, "CARD_STATUS decode error=%u payload=%zu\n",
                     static_cast<unsigned>(decoded.error()), frame.value().payload.size);
    return decoded && decoded.value().present;
}

bool test_product_listener_worker_lanes()
{
    const auto root = std::filesystem::path("/tmp") /
        ("px4-worker-core-" + std::to_string(static_cast<unsigned long>(::getpid())));
    const std::string runtime_directory = root.string();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    CHECK(!error);
    CHECK(::chmod(root.c_str(), 0700) == 0);

    Family* family_ptr = new Family;
    std::unique_ptr<EnclosureFamily> family(family_ptr);
    const w::ListResponse list = make_list();
    CHECK(list.receivers.present && list.receivers.value.count == 8U);
    ContextConfig config;
    config.runtime_directory = runtime_directory;
    config.product = "px4-userland";
    config.instance = "worker-test";
    config.server.epoch = list.epoch;
    config.server.producer = list.producer;
    config.server.features = w::kFeatureCardService;
    config.canonical_list = list;
    config.context.epoch = list.epoch;
    config.context.instance.present = true;
    config.context.instance.value = text<64U>("worker-test");
    config.stream_engine_epoch = 0x505834574f524bULL;
    config.stream_arm_budget_ms = 5000U;
    auto assembled = EnclosureContext::assemble(std::move(family),
        std::unique_ptr<Nonce>(new Nonce), config);
    if (!assembled)
        std::fprintf(stderr, "context assemble error=%u\n",
                     static_cast<unsigned>(assembled.error()));
    CHECK(assembled);
    CHECK(assembled.value()->reserve_namespace());
    CHECK(assembled.value()->publish());
    auto context = std::move(assembled.value());
    std::atomic<bool> serving{true};
    std::thread server([&] {
        while (serving.load(std::memory_order_acquire))
            (void)context->poll_once(Timeout{20U});
    });

    std::unique_ptr<w::TimedClientTransport> receiver0;
    w::StatusSnapshot receiver0_snapshot{};
    bool receiver0_result = false;
    bool passed = open_client(runtime_directory, receiver0, receiver0_snapshot);
    std::thread blocked;
    if (passed) {
        blocked = std::thread([&] {
            receiver0_result = acquire(*receiver0, receiver0_snapshot, 0U);
        });
        passed = family_ptr->tuner_.wait_opened(0U);
    }

    std::unique_ptr<w::TimedClientTransport> receiver4;
    w::StatusSnapshot receiver4_snapshot{};
    if (passed) passed = open_client(runtime_directory, receiver4, receiver4_snapshot);
    if (passed) passed = acquire(*receiver4, receiver4_snapshot, 4U);
    if (passed) passed = family_ptr->tuner_.wait_opened(4U);

    std::unique_ptr<w::TimedClientTransport> card;
    w::StatusSnapshot card_snapshot{};
    if (passed) passed = open_client(runtime_directory, card, card_snapshot);
    if (passed) passed = card_status(*card, card_snapshot);
    if (passed) passed =
        family_ptr->card_.detections.load(std::memory_order_relaxed) != 0U;

    family_ptr->tuner_.release_receiver0();
    if (blocked.joinable()) blocked.join();
    passed = passed && receiver0_result;

    if (receiver0) receiver0->close();
    if (receiver4) receiver4->close();
    if (card) card->close();
    const auto closed = context->close(Timeout{5000U});
    serving.store(false, std::memory_order_release);
    server.join();
    passed = passed && closed && closed.value() == CloseState::quiesced;
    context.reset();
    std::filesystem::remove_all(root, error);
    return passed && !error;
}

}  // namespace

int main()
{
    return test_product_listener_worker_lanes() ? 0 : 1;
}

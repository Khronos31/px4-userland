// SPDX-License-Identifier: GPL-2.0-only

#define main runtime_v1_server_tests_main
#include "runtime_v1_server_tests.cpp"
#undef main

namespace {

class NoOutput final : public Px4TsOutput {
public:
    Result<void> write(ByteView, const Px4TsSignal&) noexcept override
    {
        return Result<void>::success();
    }
};

class StopAfter final : public Px4TsSignal {
public:
    explicit StopAfter(std::chrono::milliseconds delay) noexcept
        : deadline_(std::chrono::steady_clock::now() + delay)
    {
    }

    bool stop_requested() const noexcept override
    {
        return std::chrono::steady_clock::now() >= deadline_;
    }

private:
    std::chrono::steady_clock::time_point deadline_;
};

class StreamStallProxy final {
public:
    explicit StreamStallProxy(const Fixture& fixture)
        : endpoint_(fixture.runtime.path() + "/px4-userland/v1/" + fixture.instance +
                    "/stream.sock"),
          server_endpoint_(endpoint_ + ".real")
    {
        std::error_code error;
        std::filesystem::rename(endpoint_, server_endpoint_, error);
        if (error) return;
        listener_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listener_ < 0) return;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (endpoint_.size() >= sizeof(address.sun_path)) return;
        std::memcpy(address.sun_path, endpoint_.c_str(), endpoint_.size() + 1U);
        if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            ::listen(listener_, 1) != 0 || ::chmod(endpoint_.c_str(), 0600) != 0)
            return;
        const int flags = ::fcntl(listener_, F_GETFL, 0);
        if (flags < 0 || ::fcntl(listener_, F_SETFL, flags | O_NONBLOCK) != 0) return;
        valid_ = true;
    }

    ~StreamStallProxy()
    {
        stop_.store(true);
        if (client_ >= 0) (void)::shutdown(client_, SHUT_RDWR);
        if (upstream_ >= 0) (void)::shutdown(upstream_, SHUT_RDWR);
        if (listener_ >= 0) (void)::close(listener_);
        if (thread_.joinable()) thread_.join();
        if (client_ >= 0) (void)::close(client_);
        if (upstream_ >= 0) (void)::close(upstream_);
        std::error_code error;
        std::filesystem::remove(endpoint_, error);
        std::filesystem::rename(server_endpoint_, endpoint_, error);
    }

    bool valid() const noexcept { return valid_; }

    void start()
    {
        thread_ = std::thread([this]() { relay(); });
    }

    bool saw_partial() const noexcept { return saw_partial_.load(); }

private:
    static bool send_all(int fd, const std::uint8_t* bytes, std::size_t size)
    {
        std::size_t offset = 0U;
        while (offset < size) {
            const ssize_t sent = ::send(fd, bytes + offset, size - offset, MSG_NOSIGNAL);
            if (sent > 0) {
                offset += static_cast<std::size_t>(sent);
            } else if (sent < 0 && errno == EINTR) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    }

    void relay_server_bytes(const std::uint8_t* bytes, std::size_t size)
    {
        if (stalled_) return;
        pending_.insert(pending_.end(), bytes, bytes + size);
        std::size_t offset = 0U;
        while (pending_.size() - offset >= wire::kHeaderSize) {
            const auto decoded = wire::decode_header(
                ByteView{pending_.data() + offset, wire::kHeaderSize});
            if (!decoded) {
                stop_.store(true);
                return;
            }
            const std::size_t frame_size = wire::kHeaderSize + decoded.value().payload_length;
            if (pending_.size() - offset < frame_size) break;
            if (saw_attach_ack_ && decoded.value().kind == wire::Kind::event &&
                decoded.value().opcode == wire::kOpcodeTsDataEvent) {
                const std::size_t partial_size = wire::kHeaderSize + 1U;
                if (send_all(client_, pending_.data() + offset, partial_size))
                    saw_partial_.store(true);
                stalled_ = true;
                return;
            }
            if (!send_all(client_, pending_.data() + offset, frame_size)) {
                stop_.store(true);
                return;
            }
            if (decoded.value().kind == wire::Kind::response &&
                decoded.value().opcode == wire::kOpcodeAttachStream)
                saw_attach_ack_ = true;
            offset += frame_size;
        }
        if (offset != 0U)
            pending_.erase(pending_.begin(), pending_.begin() +
                                                   static_cast<std::ptrdiff_t>(offset));
    }

    void relay()
    {
        while (!stop_.load() && client_ < 0) {
            pollfd descriptor{listener_, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1U, 50);
            if (ready < 0) {
                if (errno == EINTR) continue;
                stop_.store(true);
                return;
            }
            if (ready == 0) continue;
            if ((descriptor.revents & POLLIN) != 0)
                client_ = ::accept(listener_, nullptr, nullptr);
            if (client_ < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                stop_.store(true);
                return;
            }
        }
        if (stop_.load()) return;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, server_endpoint_.c_str(), server_endpoint_.size() + 1U);
        upstream_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (upstream_ < 0 ||
            ::connect(upstream_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            stop_.store(true);
            return;
        }
        std::array<std::uint8_t, 8192U> buffer{};
        while (!stop_.load()) {
            pollfd descriptors[2]{{client_, POLLIN, 0}, {upstream_, POLLIN, 0}};
            const int ready = ::poll(descriptors, 2U, 50);
            if (ready < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (ready == 0) continue;
            if ((descriptors[0].revents & POLLIN) != 0) {
                const ssize_t received = ::recv(client_, buffer.data(), buffer.size(), 0);
                if (received <= 0 ||
                    !send_all(upstream_, buffer.data(), static_cast<std::size_t>(received)))
                    break;
            }
            if ((descriptors[1].revents & POLLIN) != 0) {
                const ssize_t received = ::recv(upstream_, buffer.data(), buffer.size(), 0);
                if (received <= 0) break;
                relay_server_bytes(buffer.data(), static_cast<std::size_t>(received));
            }
            if ((descriptors[0].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) break;
        }
    }

    std::string endpoint_;
    std::string server_endpoint_;
    int listener_ = -1;
    int client_ = -1;
    int upstream_ = -1;
    std::atomic<bool> stop_{false};
    std::atomic<bool> saw_partial_{false};
    bool valid_ = false;
    bool saw_attach_ack_ = false;
    bool stalled_ = false;
    std::vector<std::uint8_t> pending_;
    std::thread thread_;
};

bool test_finite_duration_without_ts_past_idle_timeout()
{
    TaggedFakeTransport dev1;
    TaggedFakeTransport dev2;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    V1_CHECK(plane);
    Fixture f("wire-idle-duration", plane.value().get());
    V1_CHECK(f.start());

    Px4TsArguments arguments = recording_arguments(f, 0U, true);
    arguments.duration_seconds = 6U;
    Px4TsFailureKind kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics{};
    NoOutput output;
    RecordingClock clock;
    RecordingSignal signal;
    const auto started = std::chrono::steady_clock::now();
    const auto result = Px4TsRunner::run_wire(arguments, output, clock, signal,
                                              &kind, &diagnostics);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    V1_CHECK(result);
    V1_CHECK(elapsed >= std::chrono::seconds(5));
    V1_CHECK(diagnostics.stream_end_valid);
    V1_CHECK(diagnostics.packets_written == 0U);
    V1_CHECK(diagnostics.stream_end.counters.packets == 0U);
    V1_CHECK(f.backend.capture_calls.load() == 1U);
    V1_CHECK(f.server_ok.load());
    return true;
}

bool test_unbounded_idle_waits_past_timeout_until_signal()
{
    TaggedFakeTransport dev1;
    TaggedFakeTransport dev2;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    V1_CHECK(plane);
    Fixture f("wire-idle-signal", plane.value().get());
    V1_CHECK(f.start());

    Px4TsArguments arguments = recording_arguments(f, 0U, false);
    arguments.duration_set = false;
    arguments.packet_count_set = false;
    Px4TsFailureKind kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics{};
    NoOutput output;
    RecordingClock clock;
    StopAfter signal(std::chrono::seconds(6));
    const auto started = std::chrono::steady_clock::now();
    const auto result = Px4TsRunner::run_wire(arguments, output, clock, signal,
                                              &kind, &diagnostics);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    V1_CHECK(result);
    V1_CHECK(result.value().signal_stopped);
    V1_CHECK(elapsed >= std::chrono::seconds(5));
    V1_CHECK(diagnostics.stream_end_valid);
    V1_CHECK(diagnostics.packets_written == 0U);
    V1_CHECK(f.backend.capture_calls.load() == 1U);
    V1_CHECK(f.server_ok.load());
    return true;
}

bool test_partial_frame_pending_size_is_reported()
{
    std::array<std::uint8_t, wire::kMaxFrameSize> storage{};
    wire::FrameReceiver receiver(MutableByteView{storage.data(), storage.size()});
    const std::vector<std::uint8_t> frame = TestClient::frame(
        wire::kOpcodeHello, 1U, ByteView{nullptr, 0U});
    V1_CHECK(!frame.empty());
    Collector collector;
    const auto fed = receiver.feed(ByteView{frame.data(), 1U}, collector);
    V1_CHECK(fed);
    V1_CHECK(receiver.pending_size() == 1U);
    return true;
}

bool test_stalled_partial_frame_times_out_after_attach()
{
    TaggedFakeTransport dev1;
    TaggedFakeTransport dev2;
    const auto plane = Q3U4StreamDataPlane::create_for_test(
        dev1, dev2, Q3U4StreamDataPlane::kMinQueuePackets,
        Q3U4StreamDataPlane::StartupStabilizationTestConfig{0U, 0U, 0U});
    V1_CHECK(plane);
    Fixture f("wire-partial-timeout", plane.value().get());
    V1_CHECK(f.start());
    StreamStallProxy proxy(f);
    V1_CHECK(proxy.valid());
    proxy.start();

    Px4TsArguments arguments = recording_arguments(f, 0U, false);
    arguments.duration_set = false;
    arguments.packet_count_set = false;
    Px4TsFailureKind kind = Px4TsFailureKind::ipc;
    Px4TsRunDiagnostics diagnostics{};
    NoOutput output;
    RecordingClock clock;
    StopAfter signal(std::chrono::seconds(12));
    std::thread feeder([&]() {
        (void)wait_for(std::chrono::seconds(3), [&]() {
            return f.backend.capture_calls.load() != 0U;
        });
        dev1.push(tagged_packets(3U, 33U));
    });
    const auto started = std::chrono::steady_clock::now();
    const auto result = Px4TsRunner::run_wire(arguments, output, clock, signal,
                                              &kind, &diagnostics);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    feeder.join();

    V1_CHECK(!result && result.error() == Error::TIMEOUT);
    V1_CHECK(kind == Px4TsFailureKind::ts_integrity);
    V1_CHECK(elapsed >= std::chrono::seconds(5));
    V1_CHECK(proxy.saw_partial());
    V1_CHECK(!diagnostics.stream_end_valid);
    V1_CHECK(f.server_ok.load());
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
        {"finite_duration_without_ts_past_idle_timeout",
         test_finite_duration_without_ts_past_idle_timeout},
        {"unbounded_idle_waits_past_timeout_until_signal",
         test_unbounded_idle_waits_past_timeout_until_signal},
        {"partial_frame_pending_size_is_reported",
         test_partial_frame_pending_size_is_reported},
        {"stalled_partial_frame_times_out_after_attach",
         test_stalled_partial_frame_times_out_after_attach},
    };
    for (const Case& test : cases) {
        if (!test.function()) return 1;
        std::fprintf(stderr, "PASS %s\n", test.name);
    }
    return 0;
}

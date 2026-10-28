// SPDX-License-Identifier: GPL-2.0-only
#include "px4_ts_batched_output.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace px4::userland::cli;

class TestSignal final : public Px4TsSignal {
public:
    bool stop_requested() const noexcept override { return stopped; }
    bool stopped = false;
};

std::size_t partial_write_limit = 0U;
bool partial_write_done = false;
TestSignal* stop_after_write_signal = nullptr;
bool stop_after_write_pipe_full = false;
std::size_t stop_after_write_fill_packets = 0U;
volatile std::sig_atomic_t received_stop_signal = 0;
int signal_to_raise = 0;
int signal_pipe_read_fd = -1;
std::size_t signal_pipe_fill_packets = 0U;

enum class SignalPipeMode {
    fill_after_write,
    fill_before_write,
    close_consumer,
};

SignalPipeMode signal_pipe_mode = SignalPipeMode::fill_after_write;
bool signal_pipe_filled = false;

void test_stop_handler(int signal_number) noexcept
{
    received_stop_signal = signal_number;
}

class ActualSignal final : public Px4TsSignal {
public:
    bool stop_requested() const noexcept override { return received_stop_signal != 0; }
};

ssize_t partial_write_then_epipe(int fd, const void* bytes,
                                 std::size_t size) noexcept
{
    if (partial_write_done) {
        errno = EPIPE;
        return -1;
    }
    partial_write_done = true;
    return ::write(fd, bytes, size < partial_write_limit ? size : partial_write_limit);
}

ssize_t stop_when_pipe_full(int fd, const void* bytes, std::size_t size) noexcept
{
    const ssize_t written = ::write(fd, bytes, size);
    if (!stop_after_write_pipe_full && written > 0) {
        std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
        packet[0U] = 0x47U;
        for (;;) {
            const ssize_t filled = ::write(fd, packet.data(), packet.size());
            if (filled == static_cast<ssize_t>(packet.size())) {
                ++stop_after_write_fill_packets;
                continue;
            }
            if (filled < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                return filled;
            break;
        }
        stop_after_write_pipe_full = true;
    } else if (stop_after_write_pipe_full && written < 0 &&
               (errno == EAGAIN || errno == EWOULDBLOCK) &&
               stop_after_write_signal != nullptr) {
        stop_after_write_signal->stopped = true;
    }
    return written;
}

void fill_pipe_with_packets(int fd) noexcept
{
    std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
    packet[0U] = 0x47U;
    for (;;) {
        const ssize_t filled = ::write(fd, packet.data(), packet.size());
        if (filled == static_cast<ssize_t>(packet.size())) {
            ++signal_pipe_fill_packets;
            continue;
        }
        break;
    }
    signal_pipe_filled = true;
}

ssize_t signal_pipe_write(int fd, const void* bytes, std::size_t size) noexcept
{
    if (signal_pipe_mode == SignalPipeMode::close_consumer) {
        if (signal_pipe_read_fd >= 0) {
            (void)::close(signal_pipe_read_fd);
            signal_pipe_read_fd = -1;
            (void)::raise(signal_to_raise);
        }
        return ::write(fd, bytes, size);
    }
    if (signal_pipe_mode == SignalPipeMode::fill_before_write &&
        !signal_pipe_filled)
        fill_pipe_with_packets(fd);

    const ssize_t written = ::write(fd, bytes, size);
    if (signal_pipe_mode == SignalPipeMode::fill_after_write &&
        !signal_pipe_filled && written > 0)
        fill_pipe_with_packets(fd);
    if (signal_pipe_filled && written < 0 &&
        (errno == EAGAIN || errno == EWOULDBLOCK))
        (void)::raise(signal_to_raise);
    return written;
}

int fail(const char* message, int fd, const char* path) noexcept
{
    std::fprintf(stderr, "%s\n", message);
    if (fd >= 0) (void)::close(fd);
    if (path != nullptr) (void)::unlink(path);
    return 1;
}

bool test_pipe_output() noexcept
{
    int descriptors[2]{};
    if (::pipe(descriptors) != 0) return false;
    std::vector<std::uint8_t> received;
    bool read_ok = true;
    std::thread reader([&]() noexcept {
        std::array<std::uint8_t, 4096U> buffer{};
        for (;;) {
            const ssize_t count = ::read(descriptors[0], buffer.data(), buffer.size());
            if (count > 0) {
                received.insert(received.end(), buffer.begin(),
                                buffer.begin() + static_cast<std::size_t>(count));
                continue;
            }
            if (count == 0) break;
            if (errno == EINTR) continue;
            read_ok = false;
            break;
        }
    });

    std::vector<std::uint8_t> expected;
    expected.reserve(600U * Px4TsBatchedFdOutput::kPacketSize);
    TestSignal signal;
    bool write_ok = true;
    std::uint64_t written_packets = 0U;
    {
        Px4TsBatchedFdOutput output(descriptors[1], true);
        if (!output.ready()) {
            write_ok = false;
        } else {
            std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
            for (std::size_t index = 0U; index < 600U; ++index) {
                packet.fill(static_cast<std::uint8_t>(index));
                packet[0U] = 0x47U;
                expected.insert(expected.end(), packet.begin(), packet.end());
                if (!output.write(dtv::userland::ByteView{packet.data(), packet.size()},
                                  signal)) {
                    write_ok = false;
                    break;
                }
            }
            if (write_ok && !output.finish(signal)) write_ok = false;
            written_packets = output.written_packets();
        }
    }
    reader.join();
    (void)::close(descriptors[0]);
    return write_ok && read_ok && received == expected &&
           written_packets == 600U;
}

bool test_stop_when_pipe_fills() noexcept
{
    int descriptors[2]{};
    if (::pipe(descriptors) != 0) return false;
    TestSignal signal;
    stop_after_write_signal = &signal;
    stop_after_write_pipe_full = false;
    stop_after_write_fill_packets = 0U;
    bool stopped_cleanly = false;
    bool timeout_result = false;
    std::uint64_t written_packets = 0U;
    {
        Px4TsBatchedFdOutput output(descriptors[1], true, stop_when_pipe_full);
        if (output.ready()) {
            std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
            packet[0U] = 0x47U;
            bool accepted = true;
            for (std::size_t index = 0U;
                 index < 2U * Px4TsBatchedFdOutput::kPacketsPerWrite; ++index) {
                const auto written = output.write(
                    dtv::userland::ByteView{packet.data(), packet.size()}, signal);
                if (!written) {
                    accepted = false;
                    timeout_result = written.error() == px4::userland::Error::TIMEOUT;
                    break;
                }
            }
            stopped_cleanly = !accepted && signal.stop_requested() &&
                output.system_error() == 0 &&
                output.written_packets() > 0U &&
                output.written_packets() <
                    2U * Px4TsBatchedFdOutput::kPacketsPerWrite;
            written_packets = output.written_packets();
        }
    }
    stop_after_write_signal = nullptr;

    std::vector<std::uint8_t> received;
    std::array<std::uint8_t, 4096U> buffer{};
    for (;;) {
        const ssize_t count = ::read(descriptors[0], buffer.data(), buffer.size());
        if (count > 0) {
            received.insert(received.end(), buffer.begin(),
                            buffer.begin() + static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) stopped_cleanly = false;
        break;
    }
    (void)::close(descriptors[0]);
    return stopped_cleanly &&
           timeout_result &&
           received.size() == (written_packets + stop_after_write_fill_packets) *
                                  Px4TsBatchedFdOutput::kPacketSize &&
           received.size() % Px4TsBatchedFdOutput::kPacketSize == 0U;
}

bool test_real_signal_finish_path(int stop_signal, SignalPipeMode mode) noexcept
{
    int descriptors[2]{};
    if (::pipe(descriptors) != 0) return false;
    received_stop_signal = 0;
    signal_to_raise = stop_signal;
    signal_pipe_mode = mode;
    signal_pipe_read_fd = descriptors[0];
    signal_pipe_fill_packets = 0U;
    signal_pipe_filled = false;
    ActualSignal signal;
    bool stopped = false;
    px4::userland::Error observed_error = px4::userland::Error::OK;
    std::uint64_t written_packets = 0U;
    {
        Px4TsBatchedFdOutput output(descriptors[1], true, signal_pipe_write);
        if (output.ready()) {
            std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
            packet[0U] = 0x47U;
            bool accepted = true;
            const std::size_t packet_count = mode == SignalPipeMode::fill_after_write
                ? 2U * Px4TsBatchedFdOutput::kPacketsPerWrite : 12U;
            for (std::size_t index = 0U; index < packet_count; ++index) {
                const auto written = output.write(
                    dtv::userland::ByteView{packet.data(), packet.size()}, signal);
                if (!written) {
                    accepted = false;
                    observed_error = written.error();
                    break;
                }
            }
            if (accepted) {
                const auto finished = output.finish(signal);
                if (!finished) observed_error = finished.error();
                else observed_error = px4::userland::Error::OK;
            }
            stopped = observed_error == px4::userland::Error::TIMEOUT &&
                signal.stop_requested() &&
                Px4TsBatchedFdOutput::finish_exit_status(
                    observed_error, signal.stop_requested()) == 0;
            written_packets = output.written_packets();
        }
    }

    std::vector<std::uint8_t> received;
    if (signal_pipe_read_fd >= 0) {
        std::array<std::uint8_t, 4096U> buffer{};
        for (;;) {
            const ssize_t count = ::read(signal_pipe_read_fd, buffer.data(), buffer.size());
            if (count > 0) {
                received.insert(received.end(), buffer.begin(),
                                buffer.begin() + static_cast<std::size_t>(count));
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) stopped = false;
            break;
        }
        (void)::close(signal_pipe_read_fd);
        signal_pipe_read_fd = -1;
    }
    if (mode == SignalPipeMode::close_consumer)
        return stopped && received_stop_signal == stop_signal &&
               written_packets == 0U;
    return stopped && received_stop_signal == stop_signal &&
           received.size() == (written_packets + signal_pipe_fill_packets) *
                                  Px4TsBatchedFdOutput::kPacketSize &&
           received.size() % Px4TsBatchedFdOutput::kPacketSize == 0U;
}

bool test_finish_epipe_without_stop() noexcept
{
    int descriptors[2]{};
    if (::pipe(descriptors) != 0) return false;
    (void)::close(descriptors[0]);
    received_stop_signal = 0;
    ActualSignal signal;
    Px4TsBatchedFdOutput output(descriptors[1], true);
    if (!output.ready()) return false;
    std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
    packet[0U] = 0x47U;
    for (std::size_t index = 0U; index < 12U; ++index) {
        if (!output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal))
            return false;
    }
    const auto finished = output.finish(signal);
    return !finished && finished.error() == px4::userland::Error::INTERNAL &&
           output.system_error() == EPIPE &&
           Px4TsBatchedFdOutput::finish_exit_status(
               finished.error(), signal.stop_requested()) == 70;
}

bool test_epipe() noexcept
{
    int descriptors[2]{};
    if (::pipe(descriptors) != 0) return false;
    (void)::close(descriptors[0]);
    TestSignal signal;
    Px4TsBatchedFdOutput output(descriptors[1], true);
    if (!output.ready()) return false;
    std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
    packet[0U] = 0x47U;
    for (std::size_t index = 0U;
         index < Px4TsBatchedFdOutput::kPacketsPerWrite - 1U; ++index) {
        if (!output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal))
            return false;
    }
    if (output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal) ||
        output.system_error() != EPIPE || output.written_packets() != 0U)
        return false;
    return !output.finish(signal);
}

bool test_partial_write_diagnostic() noexcept
{
    char path[] = "/tmp/px4-ts-batched-output-partial-XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0) return false;
    (void)::unlink(path);
    partial_write_limit = 2U * Px4TsBatchedFdOutput::kPacketSize + 10U;
    partial_write_done = false;
    TestSignal signal;
    Px4TsBatchedFdOutput output(fd, true, partial_write_then_epipe);
    if (!output.ready()) return false;
    std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
    packet[0U] = 0x47U;
    for (std::size_t index = 0U;
         index < Px4TsBatchedFdOutput::kPacketsPerWrite - 1U; ++index) {
        if (!output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal))
            return false;
    }
    if (output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal) ||
        output.system_error() != EPIPE || output.written_packets() != 2U)
        return false;
    struct stat status {};
    if (::fstat(fd, &status) != 0 ||
        status.st_size != static_cast<off_t>(partial_write_limit))
        return false;
    FILE* diagnostic = std::tmpfile();
    if (diagnostic == nullptr) return false;
    print_px4_ts_written_diagnostic(diagnostic, output.written_packets());
    if (std::fflush(diagnostic) != 0 || std::fseek(diagnostic, 0L, SEEK_SET) != 0) {
        (void)std::fclose(diagnostic);
        return false;
    }
    char line[64U]{};
    const bool matches = std::fgets(line, sizeof(line), diagnostic) != nullptr &&
                         std::strcmp(line, "written packets=2 bytes=376\n") == 0;
    (void)std::fclose(diagnostic);
    return matches;
}

bool test_non_owned_fd() noexcept
{
    char path[] = "/tmp/px4-ts-batched-output-fd-XXXXXX";
    const int fd = ::mkstemp(path);
    if (fd < 0) return false;
    (void)::unlink(path);
    const int original_flags = ::fcntl(fd, F_GETFL);
    if (original_flags < 0) {
        (void)::close(fd);
        return false;
    }
    TestSignal signal;
    {
        Px4TsBatchedFdOutput output(fd, false);
        if (!output.ready() || !output.finish(signal)) {
            (void)::close(fd);
            return false;
        }
    }
    const int restored_flags = ::fcntl(fd, F_GETFL);
    const bool open_and_restored = restored_flags == original_flags;
    (void)::close(fd);
    return open_and_restored;
}

}  // namespace

int main()
{
    struct sigaction ignore_pipe {};
    ignore_pipe.sa_handler = SIG_IGN;
    if (sigemptyset(&ignore_pipe.sa_mask) != 0 ||
        ::sigaction(SIGPIPE, &ignore_pipe, nullptr) != 0)
        return 1;
    struct sigaction stop_action {};
    stop_action.sa_handler = test_stop_handler;
    if (sigemptyset(&stop_action.sa_mask) != 0 ||
        ::sigaction(SIGINT, &stop_action, nullptr) != 0 ||
        ::sigaction(SIGTERM, &stop_action, nullptr) != 0)
        return 1;

    std::string path = "/tmp/px4-ts-batched-output-XXXXXX";
    std::vector<char> template_path(path.begin(), path.end());
    template_path.push_back('\0');
    const int fd = ::mkstemp(template_path.data());
    if (fd < 0) return fail("mkstemp failed", -1, nullptr);
    path.assign(template_path.data());
    if (::unlink(path.c_str()) != 0) return fail("unlink failed", fd, path.c_str());

    Px4TsBatchedFdOutput output(fd, true);
    if (!output.ready()) return fail("output setup failed", -1, path.c_str());
    TestSignal signal;
    std::vector<std::uint8_t> expected;
    expected.reserve(273U * Px4TsBatchedFdOutput::kPacketSize);
    std::array<std::uint8_t, Px4TsBatchedFdOutput::kPacketSize> packet{};
    struct stat status {};

    for (std::size_t index = 0U; index < Px4TsBatchedFdOutput::kPacketsPerWrite - 1U;
         ++index) {
        packet.fill(static_cast<std::uint8_t>(index));
        packet[0U] = 0x47U;
        expected.insert(expected.end(), packet.begin(), packet.end());
        if (!output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal))
            return fail("buffered write failed", -1, path.c_str());
    }
    if (::fstat(fd, &status) != 0 || status.st_size != 0)
        return fail("partial batch reached the file", -1, path.c_str());

    packet.fill(static_cast<std::uint8_t>(Px4TsBatchedFdOutput::kPacketsPerWrite - 1U));
    packet[0U] = 0x47U;
    expected.insert(expected.end(), packet.begin(), packet.end());
    if (!output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal))
        return fail("full batch write failed", -1, path.c_str());
    if (::fstat(fd, &status) != 0 ||
        status.st_size != static_cast<off_t>(Px4TsBatchedFdOutput::kBufferSize) ||
        output.written_packets() != Px4TsBatchedFdOutput::kPacketsPerWrite)
        return fail("full batch was not flushed", -1, path.c_str());

    for (std::size_t index = Px4TsBatchedFdOutput::kPacketsPerWrite;
         index < 273U; ++index) {
        packet.fill(static_cast<std::uint8_t>(index));
        packet[0U] = 0x47U;
        expected.insert(expected.end(), packet.begin(), packet.end());
        if (!output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal))
            return fail("tail write failed", -1, path.c_str());
    }
    if (::fstat(fd, &status) != 0 ||
        status.st_size != static_cast<off_t>(Px4TsBatchedFdOutput::kBufferSize))
        return fail("tail batch reached the file before finish", -1, path.c_str());
    signal.stopped = true;
    if (!output.finish(signal)) return fail("finish failed", -1, path.c_str());
    if (::fstat(fd, &status) != 0 ||
        status.st_size != static_cast<off_t>(expected.size()) ||
        output.written_packets() != 273U)
        return fail("finish did not flush the tail", -1, path.c_str());

    std::vector<std::uint8_t> actual(expected.size());
    const ssize_t received = ::pread(fd, actual.data(), actual.size(), 0);
    if (received != static_cast<ssize_t>(expected.size()) || actual != expected)
        return fail("output bytes differ", -1, path.c_str());
    if (output.write(dtv::userland::ByteView{packet.data(), packet.size()}, signal).error() !=
        px4::userland::Error::TIMEOUT)
        return fail("write after stop was not rejected", -1, path.c_str());
    if (!test_pipe_output()) return fail("pipe partial-write test failed", -1, path.c_str());
    if (!test_stop_when_pipe_fills())
        return fail("stop split a pipe packet or failed to stop cleanly", -1, path.c_str());
    for (const int stop_signal : {SIGINT, SIGTERM}) {
        if (!test_real_signal_finish_path(stop_signal,
                                          SignalPipeMode::fill_after_write))
            return fail("signal during batch write did not exit cleanly", -1, path.c_str());
        if (!test_real_signal_finish_path(stop_signal,
                                          SignalPipeMode::fill_before_write))
            return fail("signal during tail finish did not exit cleanly", -1, path.c_str());
        if (!test_real_signal_finish_path(stop_signal,
                                          SignalPipeMode::close_consumer))
            return fail("signal and consumer close did not exit cleanly", -1, path.c_str());
    }
    if (!test_finish_epipe_without_stop())
        return fail("ordinary finish EPIPE was not preserved", -1, path.c_str());
    if (!test_epipe()) return fail("EPIPE propagation test failed", -1, path.c_str());
    if (!test_partial_write_diagnostic())
        return fail("partial-write diagnostic count failed", -1, path.c_str());
    if (!test_non_owned_fd())
        return fail("non-owned fd was not preserved", -1, path.c_str());
    return 0;
}

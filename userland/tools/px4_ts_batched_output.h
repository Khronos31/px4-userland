// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_PX4_TS_BATCHED_OUTPUT_H
#define PX4_USERLAND_PX4_TS_BATCHED_OUTPUT_H

#include <dtv/host_ts/px4_ts_core.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace px4::userland::cli {

class Px4TsBatchedFdOutput final : public Px4TsOutput {
public:
    using WriteFunction = ssize_t (*)(int, const void*, std::size_t);

    static constexpr std::size_t kPacketSize = 188U;
    static constexpr std::size_t kPacketsPerWrite = 256U;
    static constexpr std::size_t kBufferSize = kPacketSize * kPacketsPerWrite;

    explicit Px4TsBatchedFdOutput(int fd, bool close_fd,
                                  WriteFunction write_function = ::write) noexcept
        : fd_(fd), close_fd_(close_fd), write_function_(write_function)
    {
        if (fd_ < 0) return;
        original_flags_ = ::fcntl(fd_, F_GETFL);
        if (original_flags_ < 0 ||
            ::fcntl(fd_, F_SETFL, original_flags_ | O_NONBLOCK) < 0) {
            return;
        }
        struct stat status {};
        if (::fstat(fd_, &status) == 0 && S_ISFIFO(status.st_mode)) {
            const long pipe_buf = ::fpathconf(fd_, _PC_PIPE_BUF);
            write_limit_ = kPacketSize;
            if (pipe_buf > 0) {
                const std::size_t limit = static_cast<std::size_t>(pipe_buf);
                const std::size_t packets = limit / kPacketSize;
                if (packets > 0U) write_limit_ = packets * kPacketSize;
            }
            packet_atomic_writes_ = true;
        }
        ready_ = true;
    }

    ~Px4TsBatchedFdOutput() noexcept override
    {
        if (fd_ >= 0 && original_flags_ >= 0)
            (void)::fcntl(fd_, F_SETFL, original_flags_);
        if (close_fd_ && fd_ >= 0) (void)::close(fd_);
    }

    Px4TsBatchedFdOutput(const Px4TsBatchedFdOutput&) = delete;
    Px4TsBatchedFdOutput& operator=(const Px4TsBatchedFdOutput&) = delete;

    bool ready() const noexcept { return ready_; }

    int system_error() const noexcept { return system_error_; }

    std::uint64_t written_packets() const noexcept { return written_packets_; }

    Result<void> write(ByteView bytes, const Px4TsSignal& signal) noexcept override
    {
        if (!ready_ || failed_ || bytes.data == nullptr ||
            bytes.size != kPacketSize)
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        if (signal.stop_requested()) return Result<void>::failure(Error::TIMEOUT);
        if (buffered_ + bytes.size > buffer_.size()) {
            const auto flushed = flush_buffer(signal);
            if (!flushed) return flushed;
        }
        std::memcpy(buffer_.data() + buffered_, bytes.data, bytes.size);
        buffered_ += bytes.size;
        if (buffered_ == buffer_.size()) return flush_buffer(signal);
        return Result<void>::success();
    }

    Result<void> finish(const Px4TsSignal& signal) noexcept
    {
        if (!ready_ || failed_) return Result<void>::failure(Error::INTERNAL);
        return flush_buffer(signal);
    }

    static int finish_exit_status(Error error, bool stop_requested) noexcept
    {
        if (error == Error::TIMEOUT && stop_requested) return 0;
        return px4_ts_exit_status(error, Px4TsFailureKind::output);
    }

private:
    Result<void> fail(int error) noexcept
    {
        system_error_ = error;
        failed_ = true;
        buffered_ = 0U;
        return Result<void>::failure(Error::INTERNAL);
    }

    Result<void> stop() noexcept
    {
        buffered_ = 0U;
        return Result<void>::failure(Error::TIMEOUT);
    }

    bool stopping_at_packet_boundary(const Px4TsSignal& signal,
                                     std::size_t offset) const noexcept
    {
        return packet_atomic_writes_ && offset % kPacketSize == 0U &&
               signal.stop_requested();
    }

    Result<void> flush_buffer(const Px4TsSignal& signal) noexcept
    {
        if (buffered_ == 0U) return Result<void>::success();
        if (stopping_at_packet_boundary(signal, 0U)) return stop();
        std::size_t offset = 0U;
        while (offset < buffered_) {
            const ssize_t written =
                write_function_(fd_, buffer_.data() + offset,
                                (buffered_ - offset) < write_limit_
                                    ? buffered_ - offset : write_limit_);
            if (written > 0) {
                const std::size_t previous_offset = offset;
                offset += static_cast<std::size_t>(written);
                written_packets_ += offset / kPacketSize -
                                    previous_offset / kPacketSize;
                if (offset < buffered_ && signal.stop_requested()) {
                    if (stopping_at_packet_boundary(signal, offset)) return stop();
                    return fail(EINTR);
                }
                continue;
            }
            if (written < 0 && errno == EINTR) {
                if (signal.stop_requested()) {
                    if (stopping_at_packet_boundary(signal, offset)) return stop();
                    return fail(EINTR);
                }
                continue;
            }
            if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (signal.stop_requested()) {
                    if (stopping_at_packet_boundary(signal, offset)) return stop();
                    return fail(EAGAIN);
                }
                pollfd descriptor{fd_, POLLOUT, 0};
                int polled = 0;
                do {
                    polled = ::poll(&descriptor, 1U, 100);
                } while (polled < 0 && errno == EINTR && !signal.stop_requested());
                if (signal.stop_requested()) {
                    if (stopping_at_packet_boundary(signal, offset)) return stop();
                    return fail(EINTR);
                }
                if (polled < 0) return fail(errno);
                if (polled > 0 &&
                    (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
                    return fail(EIO);
                continue;
            }
            if (written < 0 && errno == EPIPE &&
                stopping_at_packet_boundary(signal, offset))
                return stop();
            return fail(written < 0 ? errno : EIO);
        }
        buffered_ = 0U;
        return Result<void>::success();
    }

    int fd_ = -1;
    bool close_fd_ = false;
    WriteFunction write_function_ = nullptr;
    int original_flags_ = -1;
    bool ready_ = false;
    bool failed_ = false;
    int system_error_ = 0;
    std::uint64_t written_packets_ = 0U;
    std::size_t write_limit_ = kBufferSize;
    bool packet_atomic_writes_ = false;
    std::array<std::uint8_t, kBufferSize> buffer_{};
    std::size_t buffered_ = 0U;
};

}  // namespace px4::userland::cli

namespace px4::userland::cli {

inline void print_px4_ts_written_diagnostic(FILE* stream,
                                             std::uint64_t packets) noexcept
{
    std::fprintf(stream, "written packets=%llu bytes=%llu\n",
                 static_cast<unsigned long long>(packets),
                 static_cast<unsigned long long>(packets * 188U));
}

}  // namespace px4::userland::cli

#endif  // PX4_USERLAND_PX4_TS_BATCHED_OUTPUT_H

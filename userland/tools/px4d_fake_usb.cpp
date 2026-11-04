// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_fake_usb.h"

#include "libusb_transport_internal.h"
#include "px4/transport.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

namespace px4::userland {
namespace {

std::uint16_t checksum(const std::uint8_t* data, std::size_t size) noexcept
{
    std::uint16_t sum = 0U;
    std::size_t index = 0U;
    while (index + 1U < size) {
        sum = static_cast<std::uint16_t>(sum +
            static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[index]) << 8U) |
                                       data[index + 1U]));
        index += 2U;
    }
    if (index < size) {
        sum = static_cast<std::uint16_t>(sum +
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[index]) << 8U));
    }
    return static_cast<std::uint16_t>(~sum);
}

class FakeQ3U4Transport final : public Transport {
public:
    Result<std::size_t> bulk_read(std::uint8_t endpoint, MutableByteView output,
                                  Timeout, BulkReadObservation*) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (endpoint != kCommandInEndpoint || output.data == nullptr ||
            output.size < pending_length_ + 5U) {
            return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
        }
        std::memset(output.data, 0, pending_length_ + 5U);
        if (last_command_ == 0x2aU && pending_length_ == 1U) {
            output.data[3U] = last_i2c_register_ == 0xb0U ? 0x0fU :
                              last_i2c_register_ == 0x80U ? 0x00U : 0x98U;
        } else if (last_command_ == 0x2aU && pending_length_ == 3U) {
            output.data[5U] = 0x02U;
        } else if (last_command_ == 0x2aU && pending_length_ == 4U) {
            output.data[6U] = 0x70U;
        }
        const std::size_t response_size = pending_length_ + 5U;
        output.data[0U] = static_cast<std::uint8_t>(response_size - 1U);
        output.data[1U] = sequence_;
        output.data[2U] = 0U;
        const std::uint16_t sum = checksum(output.data + 1U, response_size - 3U);
        output.data[response_size - 2U] = static_cast<std::uint8_t>(sum >> 8U);
        output.data[response_size - 1U] = static_cast<std::uint8_t>(sum);
        return Result<std::size_t>::success(response_size);
    }

    Result<std::size_t> bulk_write(std::uint8_t endpoint, ByteView input,
                                   Timeout) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (endpoint != kCommandOutEndpoint || input.data == nullptr || input.size < 5U ||
            input.data[1U] != 0U) {
            return Result<std::size_t>::failure(Error::INVALID_ARGUMENT);
        }
        last_command_ = input.data[2U];
        sequence_ = input.data[3U];
        pending_length_ = (last_command_ == 0x2aU || last_command_ == 0x00U)
                              ? input.data[4U] : 0U;
        if (last_command_ == 0x2bU && input.size > 7U)
            last_i2c_register_ = input.data[7U];
        return Result<std::size_t>::success(input.size);
    }

    Result<void> start_stream(const StreamConfig& config) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_ || config.endpoint != kTsInEndpoint || config.transfer_size !=
                188U * 816U || config.transfer_count != 6U)
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        stream_.fill(0xffU);
        for (std::size_t offset = 0U; offset + 188U <= stream_.size(); offset += 188U) {
            stream_[offset] = 0x47U;
            stream_[offset + 1U] = 0x01U;
            stream_[offset + 2U] = 0x00U;
            stream_[offset + 3U] = static_cast<std::uint8_t>(
                0x10U | ((offset / 188U) & 0x0fU));
        }
        active_ = true;
        return Result<void>::success();
    }

    Result<StreamEvent> wait_stream(Timeout timeout) noexcept override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(
            timeout.milliseconds > 2U ? 2U : timeout.milliseconds));
        std::lock_guard<std::mutex> lock(mutex_);
        if (!active_) return Result<StreamEvent>::failure(Error::NOT_READY);
        return Result<StreamEvent>::success(StreamEvent{
            StreamEventKind::short_transfer, stream_.data(), stream_.size()});
    }

    Result<void> cancel_stream() noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = false;
        return Result<void>::success();
    }

    Result<void> stop_stream() noexcept override { return cancel_stream(); }
    bool stream_active() const noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

private:
    mutable std::mutex mutex_;
    std::array<std::uint8_t, 188U * 816U> stream_{};
    std::size_t pending_length_ = 0U;
    std::uint8_t last_command_ = 0U;
    std::uint8_t sequence_ = 0U;
    std::uint8_t last_i2c_register_ = 0U;
    bool active_ = false;
};

}  // namespace

Result<std::unique_ptr<Q3U4Runtime>> make_fake_q3u4_runtime() noexcept
{
    std::unique_ptr<Transport> dev1(new (std::nothrow) FakeQ3U4Transport);
    std::unique_ptr<Transport> dev2(new (std::nothrow) FakeQ3U4Transport);
    if (!dev1 || !dev2)
        return Result<std::unique_ptr<Q3U4Runtime>>::failure(Error::INTERNAL);
    return RuntimeTestAccess::create_fake_q3u4(
        std::move(dev1), std::move(dev2), "00001205000960");
}

}  // namespace px4::userland

// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_L3_OWNER_TRANSPORT_H
#define PX4_USERLAND_L3_OWNER_TRANSPORT_H

#include "px4/transport.h"

#include <dtv/l3_owner.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace px4::userland {

// Product-internal Transport that routes bulk read/write through the common L3
// owner's transfer/op API.  It is not a public L3 API.  The existing direct
// Transport backends remain for paths that have not been moved yet.
class L3OwnerTransport final : public Transport {
public:
    L3OwnerTransport(dtv_l3_context* owner,
                     std::uint32_t claim_index) noexcept;
    ~L3OwnerTransport() noexcept override = default;

    L3OwnerTransport(const L3OwnerTransport&) = delete;
    L3OwnerTransport& operator=(const L3OwnerTransport&) = delete;

    Result<std::size_t> bulk_read(std::uint8_t endpoint,
                                  MutableByteView output,
                                  Timeout timeout,
                                  BulkReadObservation* observation) noexcept override;
    Result<std::size_t> bulk_write(std::uint8_t endpoint,
                                   ByteView input,
                                   Timeout timeout) noexcept override;

    Result<void> start_stream(const StreamConfig& config) noexcept override;
    Result<StreamEvent> wait_stream(Timeout timeout) noexcept override;
    Result<void> cancel_stream() noexcept override;
    Result<void> stop_stream() noexcept override;
    bool stream_active() const noexcept override;

private:
    Result<std::size_t> transfer(std::uint32_t kind, std::uint8_t endpoint,
                                 const std::uint8_t* out_data,
                                 std::size_t out_size,
                                 std::uint8_t* in_data,
                                 std::size_t in_capacity,
                                 Timeout timeout) noexcept;

    dtv_l3_context* owner_;
    std::uint32_t claim_index_;
    mutable std::mutex stream_mutex_;
    bool stream_active_ = false;
    StreamConfig stream_config_{};
    dtv_l3_op* stream_op_ = nullptr;
    std::vector<std::uint8_t> stream_buffer_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_L3_OWNER_TRANSPORT_H

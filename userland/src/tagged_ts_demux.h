// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_TAGGED_TS_DEMUX_H
#define PX4_USERLAND_TAGGED_TS_DEMUX_H

#include "px4/error.h"
#include "px4/transport.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace px4::userland {

// Demultiplexes the 188-byte TS packets emitted by a Q3U4/PX4 aggregation
// stream. The packet view passed to PacketSink is valid only during the call.
class TaggedTsDemux final {
public:
    static constexpr std::size_t kPacketSize = 188U;
    static constexpr std::size_t kSyncPacketCount = 4U;
    // Four complete packets are needed to acquire sync. Retaining the first
    // 4*188-1 bytes is sufficient to preserve every possible future start.
    static constexpr std::size_t kPendingCapacity =
        kMaxStreamTransfer + (kSyncPacketCount * kPacketSize);

    using PacketSink = Result<void> (*)(void*, std::size_t receiver_index,
                                        ByteView packet) noexcept;
    using InvalidTagObserver = void (*)(void*, std::uint8_t wire_sync) noexcept;
#if defined(DTV_USERLAND_DIAGNOSTICS)
    struct PacketHeaderScalars final {
        bool tei = false;
        std::uint8_t adaptation_field_control = 0U;
        bool has_adaptation_field = false;
        std::uint8_t adaptation_field_length = 0U;
        bool adaptation_length_overrun = false;
    };

    struct PacketBoundaryObservation final {
        std::size_t packet_ordinal = 0U;
        std::uint8_t wire_tag = 0U;
        bool forwarded = false;
        bool transfer_input_available = false;
        PacketHeaderScalars transfer_input;
        PacketHeaderScalars demux_input;
        PacketHeaderScalars sink;
        std::uint8_t transfer_copy_mismatch_mask = 0U;
        std::uint8_t demux_sink_mismatch_mask = 0U;
    };

    using PacketBoundaryObserver = void (*)(
        void*, const PacketBoundaryObservation&) noexcept;

    void observe_transfer_input(ByteView input) noexcept;
    bool transfer_input_sample_overflowed() const noexcept
    {
        return diagnostic_transfer_input_sample_overflow_;
    }
#endif

    struct Counters final {
        std::size_t input_bytes_accepted;
        std::size_t emitted_packets;
        std::size_t discarded_sync_search_bytes;
        std::size_t invalid_tag_packets;
        std::size_t sync_loss_events;
        std::size_t buffered_bytes;
    };

    static constexpr std::uint8_t kQ3U4MaxTag = 4U;
    // The MLT5 family tags five inputs, 0x17 through 0x57.
    static constexpr std::uint8_t kMlt5PeMaxTag = 5U;

    // Emits only receiver tags 1..max_tag (at most 7).
    explicit TaggedTsDemux(std::uint8_t max_tag = kQ3U4MaxTag,
                           bool plain_ts = false) noexcept;
    ~TaggedTsDemux() noexcept = default;

    TaggedTsDemux(const TaggedTsDemux&) = delete;
    TaggedTsDemux& operator=(const TaggedTsDemux&) = delete;
    TaggedTsDemux(TaggedTsDemux&&) = delete;
    TaggedTsDemux& operator=(TaggedTsDemux&&) = delete;

    // The input is copied into bounded internal storage before any sink call.
    // Consequently, the caller may release or alter input when this method
    // returns. If the sink fails, its exact error is returned and the failed
    // packet, together with every later copied byte, remains buffered. A
    // subsequent push with an empty ByteView retries that packet. Emitted
    // counts advance only after a successful sink call. Non-empty input that
    // would exceed kPendingCapacity is rejected with SLOW_CONSUMER and is not
    // copied; this also protects a failed/backlogged stream from overflow.
    // PacketSink must be non-null, including for an empty retry push.  The
    // optional observer receives aligned wire packets with the high-bit TEI
    // marker before those packets are discarded; it never changes the sink
    // failure/retry contract.
    Result<void> push(ByteView input, PacketSink sink, void* context,
                      InvalidTagObserver observer = nullptr,
                      void* observer_context = nullptr
#if defined(DTV_USERLAND_DIAGNOSTICS)
                      , PacketBoundaryObserver boundary_observer = nullptr,
                      void* boundary_context = nullptr
#endif
                      ) noexcept;

    // Discards parser state and all buffered bytes, and resets every counter.
    void reset() noexcept;

    Counters counters() const noexcept;

private:
    // A wire sync byte identifies packet alignment even when its high bit is
    // the receiver-local TEI marker.  Only tags 1..max_tag are emitted; TEI
    // and unknown tags are consumed as whole packets.
    bool is_packet_boundary(std::uint8_t value) const noexcept;
    void compact_pending() noexcept;
    bool acquire_sync() noexcept;
    void discard_bytes(std::size_t count) noexcept;
    void consume_packet() noexcept;

    std::uint8_t max_tag_;
    bool plain_ts_;
    std::unique_ptr<std::uint8_t[]> pending_;
    std::size_t pending_offset_ = 0U;
    std::size_t pending_size_ = 0U;
    bool synchronized_ = false;
    std::size_t input_bytes_accepted_ = 0U;
    std::size_t emitted_packets_ = 0U;
    std::size_t discarded_sync_search_bytes_ = 0U;
    std::size_t invalid_tag_packets_ = 0U;
    std::size_t sync_loss_events_ = 0U;
#if defined(DTV_USERLAND_DIAGNOSTICS)
    static constexpr std::size_t kDiagnosticSyncWindow =
        ((kSyncPacketCount - 1U) * kPacketSize) + 1U;
    static constexpr std::size_t kDiagnosticInputSampleCapacity =
        (kMaxStreamTransfer / kPacketSize) + kSyncPacketCount + 1U;

    struct DiagnosticInputSlot final {
        std::size_t offset = 0U;
        bool occupied = false;
        bool packet_boundary = false;
        bool candidate = false;
        bool tei_ready = false;
        bool adaptation_control_ready = false;
        bool header_ready = false;
        std::uint8_t wire_tag = 0U;
        PacketHeaderScalars header;
    };

    struct DiagnosticInputSample final {
        std::uint8_t wire_tag = 0U;
        std::uint8_t packed_flags = 0U;
        std::uint8_t adaptation_field_length = 0U;
    };

    void scan_transfer_input_byte(std::uint8_t value) noexcept;
    DiagnosticInputSlot* diagnostic_input_slot(std::size_t offset) noexcept;
    const DiagnosticInputSlot* diagnostic_input_slot(std::size_t offset) const noexcept;
    void emit_transfer_input_packet(std::size_t offset) noexcept;
    bool take_transfer_input_packet(std::size_t packet_ordinal,
                                    std::uint8_t wire_tag,
                                    PacketHeaderScalars& header) noexcept;
    PacketHeaderScalars packet_header_scalars(const std::uint8_t* packet) const noexcept;
    static std::uint8_t packet_header_mismatch_mask(
        const PacketHeaderScalars& input,
        const PacketHeaderScalars& output) noexcept;

    std::array<DiagnosticInputSlot, kDiagnosticSyncWindow> diagnostic_input_slots_{};
    std::array<DiagnosticInputSample, kDiagnosticInputSampleCapacity>
        diagnostic_transfer_input_samples_{};
    std::size_t diagnostic_transfer_input_sample_head_ = 0U;
    std::size_t diagnostic_transfer_input_sample_count_ = 0U;
    std::size_t diagnostic_transfer_input_head_ordinal_ = 0U;
    std::size_t diagnostic_input_packet_ordinal_ = 0U;
    std::size_t diagnostic_input_byte_offset_ = 0U;
    std::size_t diagnostic_input_next_packet_offset_ = 0U;
    bool diagnostic_input_synchronized_ = false;
    bool diagnostic_transfer_input_sample_overflow_ = false;
    std::size_t diagnostic_packet_ordinal_ = 0U;
#endif
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_TAGGED_TS_DEMUX_H

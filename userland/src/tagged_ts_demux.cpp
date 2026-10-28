// Modified/ported for px4-userland on 2026-09-02; MLT5 support added on 2026-09-24.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit d748866f0da1cb3656106a520de4e9d7f073aacd (v0.6.1).
// Origin paths: driver/px4_device.c, driver/pxmlt_device.c, driver/ts_sync.h,
// winusb/src/DriverHost_PX4/px4_device.cpp,
// winusb/tests/ts_sync_condition_test.cpp.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only

#include "tagged_ts_demux.h"

#include <cstring>
#include <new>

namespace px4::userland {

#if defined(DTV_USERLAND_DIAGNOSTICS)
namespace {

TaggedTsDemux::PacketHeaderScalars extract_packet_header_scalars(
    const std::uint8_t* packet) noexcept
{
    TaggedTsDemux::PacketHeaderScalars result{};
    result.tei = (packet[1U] & 0x80U) != 0U;
    result.adaptation_field_control =
        static_cast<std::uint8_t>((packet[3U] >> 4U) & 0x03U);
    result.has_adaptation_field = (result.adaptation_field_control & 0x02U) != 0U;
    if (result.has_adaptation_field) {
        result.adaptation_field_length = packet[4U];
        result.adaptation_length_overrun =
            static_cast<std::size_t>(result.adaptation_field_length) + 5U >
            TaggedTsDemux::kPacketSize;
    }
    return result;
}

std::uint8_t compare_packet_header_scalars(
    const TaggedTsDemux::PacketHeaderScalars& input,
    const TaggedTsDemux::PacketHeaderScalars& sink) noexcept
{
    std::uint8_t result = 0U;
    if (input.tei != sink.tei) result |= 0x01U;
    if (input.adaptation_field_control != sink.adaptation_field_control)
        result |= 0x02U;
    if (input.has_adaptation_field != sink.has_adaptation_field ||
        (input.has_adaptation_field && sink.has_adaptation_field &&
         input.adaptation_field_length != sink.adaptation_field_length))
        result |= 0x04U;
    if (input.adaptation_length_overrun != sink.adaptation_length_overrun)
        result |= 0x08U;
    return result;
}

}  // namespace
#endif

#if defined(DTV_USERLAND_DIAGNOSTICS)
TaggedTsDemux::DiagnosticInputSlot* TaggedTsDemux::diagnostic_input_slot(
    std::size_t offset) noexcept
{
    DiagnosticInputSlot& slot =
        diagnostic_input_slots_[offset % kDiagnosticSyncWindow];
    return slot.occupied && slot.offset == offset ? &slot : nullptr;
}

const TaggedTsDemux::DiagnosticInputSlot* TaggedTsDemux::diagnostic_input_slot(
    std::size_t offset) const noexcept
{
    const DiagnosticInputSlot& slot =
        diagnostic_input_slots_[offset % kDiagnosticSyncWindow];
    return slot.occupied && slot.offset == offset ? &slot : nullptr;
}

TaggedTsDemux::PacketHeaderScalars TaggedTsDemux::packet_header_scalars(
    const std::uint8_t* packet) const noexcept
{
    return extract_packet_header_scalars(packet);
}

std::uint8_t TaggedTsDemux::packet_header_mismatch_mask(
    const PacketHeaderScalars& input,
    const PacketHeaderScalars& output) noexcept
{
    return compare_packet_header_scalars(input, output);
}

void TaggedTsDemux::emit_transfer_input_packet(std::size_t offset) noexcept
{
    const DiagnosticInputSlot* slot = diagnostic_input_slot(offset);
    if (slot == nullptr || !slot->header_ready) return;
    const std::size_t packet_ordinal = diagnostic_input_packet_ordinal_++;
    if (diagnostic_transfer_input_sample_count_ ==
        diagnostic_transfer_input_samples_.size()) {
        diagnostic_transfer_input_sample_overflow_ = true;
        return;
    }
    const std::size_t tail =
        (diagnostic_transfer_input_sample_head_ +
         diagnostic_transfer_input_sample_count_) %
        diagnostic_transfer_input_samples_.size();
    if (diagnostic_transfer_input_sample_count_ == 0U)
        diagnostic_transfer_input_head_ordinal_ = packet_ordinal;
    std::uint8_t packed_flags = slot->header.tei ? 0x01U : 0U;
    packed_flags |= static_cast<std::uint8_t>(
        (slot->header.adaptation_field_control & 0x03U) << 1U);
    if (slot->header.adaptation_length_overrun) packed_flags |= 0x08U;
    diagnostic_transfer_input_samples_[tail] = DiagnosticInputSample{
        slot->wire_tag, packed_flags, slot->header.adaptation_field_length};
    ++diagnostic_transfer_input_sample_count_;
}

bool TaggedTsDemux::take_transfer_input_packet(
    std::size_t packet_ordinal, std::uint8_t wire_tag,
    PacketHeaderScalars& header) noexcept
{
    if (diagnostic_transfer_input_sample_count_ == 0U) return false;
    const DiagnosticInputSample& sample =
        diagnostic_transfer_input_samples_[diagnostic_transfer_input_sample_head_];
    if (diagnostic_transfer_input_head_ordinal_ != packet_ordinal ||
        sample.wire_tag != wire_tag)
        return false;
    header.tei = (sample.packed_flags & 0x01U) != 0U;
    header.adaptation_field_control =
        static_cast<std::uint8_t>((sample.packed_flags >> 1U) & 0x03U);
    header.has_adaptation_field =
        (header.adaptation_field_control & 0x02U) != 0U;
    header.adaptation_field_length = sample.adaptation_field_length;
    header.adaptation_length_overrun = (sample.packed_flags & 0x08U) != 0U;
    ++diagnostic_transfer_input_head_ordinal_;
    diagnostic_transfer_input_sample_head_ =
        (diagnostic_transfer_input_sample_head_ + 1U) %
        diagnostic_transfer_input_samples_.size();
    --diagnostic_transfer_input_sample_count_;
    return true;
}

void TaggedTsDemux::scan_transfer_input_byte(std::uint8_t value) noexcept
{
    const std::size_t offset = diagnostic_input_byte_offset_++;
    DiagnosticInputSlot& current =
        diagnostic_input_slots_[offset % kDiagnosticSyncWindow];
    current = DiagnosticInputSlot{};
    current.offset = offset;
    current.occupied = true;
    current.packet_boundary = is_packet_boundary(value);
    if (current.packet_boundary) {
        current.candidate = true;
        current.wire_tag = plain_ts_
                               ? 1U
                               : static_cast<std::uint8_t>((value >> 4U) & 0x07U);
    }

    if (offset >= 1U) {
        if (DiagnosticInputSlot* candidate = diagnostic_input_slot(offset - 1U)) {
            if (candidate->candidate) {
                candidate->header.tei = (value & 0x80U) != 0U;
                candidate->tei_ready = true;
            }
        }
    }
    if (offset >= 3U) {
        if (DiagnosticInputSlot* candidate = diagnostic_input_slot(offset - 3U)) {
            if (candidate->candidate) {
                candidate->header.adaptation_field_control =
                    static_cast<std::uint8_t>((value >> 4U) & 0x03U);
                candidate->header.has_adaptation_field =
                    (candidate->header.adaptation_field_control & 0x02U) != 0U;
                candidate->adaptation_control_ready = true;
                if (!candidate->header.has_adaptation_field &&
                    candidate->tei_ready) {
                    candidate->header_ready = true;
                }
            }
        }
    }
    if (offset >= 4U) {
        if (DiagnosticInputSlot* candidate = diagnostic_input_slot(offset - 4U)) {
            if (candidate->candidate && candidate->adaptation_control_ready &&
                candidate->header.has_adaptation_field) {
                candidate->header.adaptation_field_length = value;
                candidate->header.adaptation_length_overrun =
                    static_cast<std::size_t>(value) + 5U > kPacketSize;
                candidate->header_ready = candidate->tei_ready;
            }
        }
    }

    if (!diagnostic_input_synchronized_ && offset >= 3U * kPacketSize) {
        const std::size_t start = offset - (3U * kPacketSize);
        const DiagnosticInputSlot* first = diagnostic_input_slot(start);
        const DiagnosticInputSlot* second = diagnostic_input_slot(start + kPacketSize);
        const DiagnosticInputSlot* third = diagnostic_input_slot(start + 2U * kPacketSize);
        const DiagnosticInputSlot* fourth = diagnostic_input_slot(start + 3U * kPacketSize);
        if (first != nullptr && second != nullptr && third != nullptr &&
            fourth != nullptr && first->packet_boundary &&
            second->packet_boundary && third->packet_boundary &&
            fourth->packet_boundary) {
            diagnostic_input_synchronized_ = true;
            diagnostic_input_next_packet_offset_ = start;
        }
    } else if (diagnostic_input_synchronized_ &&
               offset == diagnostic_input_next_packet_offset_ &&
               !current.packet_boundary) {
        diagnostic_input_synchronized_ = false;
    }

    while (diagnostic_input_synchronized_ &&
           diagnostic_input_next_packet_offset_ + kPacketSize - 1U <= offset) {
        emit_transfer_input_packet(diagnostic_input_next_packet_offset_);
        diagnostic_input_next_packet_offset_ += kPacketSize;
    }
}

void TaggedTsDemux::observe_transfer_input(ByteView input) noexcept
{
    if (input.data == nullptr) return;
    for (std::size_t index = 0U; index < input.size; ++index)
        scan_transfer_input_byte(input.data[index]);
}
#endif

TaggedTsDemux::TaggedTsDemux(std::uint8_t max_tag, bool plain_ts) noexcept
    : max_tag_(max_tag > 7U ? std::uint8_t{7U} : max_tag),
      plain_ts_(plain_ts),
      pending_(new (std::nothrow) std::uint8_t[kPendingCapacity])
{
}

bool TaggedTsDemux::is_packet_boundary(std::uint8_t value) const noexcept
{
    return plain_ts_ ? value == 0x47U : (value & 0x0fU) == 0x07U;
}

void TaggedTsDemux::compact_pending() noexcept
{
    if (pending_offset_ == 0U) {
        return;
    }
    if (pending_size_ != 0U) {
        std::memmove(pending_.get(), pending_.get() + pending_offset_, pending_size_);
    }
    pending_offset_ = 0U;
}

void TaggedTsDemux::discard_bytes(std::size_t count) noexcept
{
    if (count == 0U) {
        return;
    }
    pending_offset_ += count;
    pending_size_ -= count;
    discarded_sync_search_bytes_ += count;
}

void TaggedTsDemux::consume_packet() noexcept
{
    pending_offset_ += kPacketSize;
    pending_size_ -= kPacketSize;
}

bool TaggedTsDemux::acquire_sync() noexcept
{
    const std::size_t required = kSyncPacketCount * kPacketSize;
    if (pending_size_ < required) {
        return false;
    }

    const std::size_t last_start = pending_size_ - required;
    for (std::size_t start = 0U; start <= last_start; ++start) {
        bool valid = true;
        for (std::size_t packet = 0U; packet < kSyncPacketCount; ++packet) {
            if (!is_packet_boundary(
                    pending_[pending_offset_ + start + (packet * kPacketSize)])) {
                valid = false;
                break;
            }
        }
        if (valid) {
            discard_bytes(start);
            return true;
        }
    }

    // Keep enough trailing bytes for a four-packet sequence whose first byte
    // arrives in the next push. This is also what prevents unbounded growth
    // when the input is a long run of garbage.
    discard_bytes(pending_size_ - (required - 1U));
    return false;
}

Result<void> TaggedTsDemux::push(ByteView input, PacketSink sink, void* context,
                                 InvalidTagObserver observer,
                                 void* observer_context
#if defined(DTV_USERLAND_DIAGNOSTICS)
                                 , PacketBoundaryObserver boundary_observer,
                                 void* boundary_context
#endif
                                 ) noexcept
{
    if (sink == nullptr || (input.size != 0U && input.data == nullptr) ||
        input.size > kMaxStreamTransfer) {
        return Result<void>::failure(Error::INVALID_ARGUMENT);
    }
    if (pending_ == nullptr) {
        return Result<void>::failure(Error::INTERNAL);
    }
    if (pending_size_ > kPendingCapacity ||
        input.size > kPendingCapacity - pending_size_) {
        return Result<void>::failure(Error::SLOW_CONSUMER);
    }

    if (input.size != 0U) {
        const std::size_t available_tail =
            kPendingCapacity - pending_offset_ - pending_size_;
        if (input.size > available_tail) {
            compact_pending();
        }
        std::memcpy(pending_.get() + pending_offset_ + pending_size_, input.data,
                    input.size);
        pending_size_ += input.size;
        input_bytes_accepted_ += input.size;
    }

    while (true) {
        if (!synchronized_) {
            if (!acquire_sync()) {
                return Result<void>::success();
            }
            synchronized_ = true;
        }

        if (pending_size_ < kPacketSize) {
            return Result<void>::success();
        }

        if (!is_packet_boundary(pending_[pending_offset_])) {
            synchronized_ = false;
            ++sync_loss_events_;
            discard_bytes(1U);
            continue;
        }

        const std::uint8_t wire_sync = pending_[pending_offset_];
#if defined(DTV_USERLAND_DIAGNOSTICS)
        const std::size_t packet_ordinal = diagnostic_packet_ordinal_++;
        const std::uint8_t diagnostic_tag = plain_ts_
                                                ? 1U
                                                : static_cast<std::uint8_t>(
                                                      (wire_sync >> 4U) & 0x07U);
        const PacketHeaderScalars demux_input_header =
            packet_header_scalars(pending_.get() + pending_offset_);
        PacketHeaderScalars transfer_input_header{};
        const bool transfer_input_available = take_transfer_input_packet(
            packet_ordinal, diagnostic_tag, transfer_input_header);
#endif
        if (plain_ts_) {
#if defined(DTV_USERLAND_DIAGNOSTICS)
            if (boundary_observer != nullptr) {
                const PacketBoundaryObservation observation{
                    packet_ordinal, 1U, true, transfer_input_available,
                    transfer_input_header, demux_input_header, demux_input_header,
                    transfer_input_available
                        ? packet_header_mismatch_mask(transfer_input_header,
                                                      demux_input_header)
                        : std::uint8_t{0U},
                    0U};
                boundary_observer(boundary_context, observation);
            }
#endif
            const Result<void> result = sink(context, 0U,
                ByteView{pending_.get() + pending_offset_, kPacketSize});
            if (!result) return result;
            ++emitted_packets_;
            consume_packet();
            continue;
        }
        const std::uint8_t tag =
            static_cast<std::uint8_t>((wire_sync >> 4U) & 0x07U);
        if (tag == 0U || tag > max_tag_ || (wire_sync & 0x80U) != 0U) {
            ++invalid_tag_packets_;
#if defined(DTV_USERLAND_DIAGNOSTICS)
            if (boundary_observer != nullptr) {
                const PacketBoundaryObservation observation{
                    packet_ordinal, tag, false, transfer_input_available,
                    transfer_input_header, demux_input_header, {},
                    transfer_input_available
                        ? packet_header_mismatch_mask(transfer_input_header,
                                                      demux_input_header)
                        : std::uint8_t{0U},
                    0U};
                boundary_observer(boundary_context, observation);
            }
#endif
            if ((wire_sync & 0x80U) != 0U && observer != nullptr)
                observer(observer_context, wire_sync);
            consume_packet();
            continue;
        }

        std::uint8_t packet[kPacketSize];
        std::memcpy(packet, pending_.get() + pending_offset_, kPacketSize);
        packet[0U] = 0x47U;
#if defined(DTV_USERLAND_DIAGNOSTICS)
        if (boundary_observer != nullptr) {
            const PacketHeaderScalars sink_header = packet_header_scalars(packet);
            const PacketBoundaryObservation observation{
                packet_ordinal, tag, true, transfer_input_available,
                transfer_input_header, demux_input_header, sink_header,
                transfer_input_available
                    ? packet_header_mismatch_mask(transfer_input_header,
                                                  demux_input_header)
                    : std::uint8_t{0U},
                packet_header_mismatch_mask(demux_input_header, sink_header)};
            boundary_observer(boundary_context, observation);
        }
#endif
        const Result<void> result =
            sink(context, static_cast<std::size_t>(tag - 1U), ByteView{packet, kPacketSize});
        if (!result) {
            return result;
        }
        ++emitted_packets_;
        consume_packet();
    }
}

void TaggedTsDemux::reset() noexcept
{
    pending_offset_ = 0U;
    pending_size_ = 0U;
    synchronized_ = false;
    input_bytes_accepted_ = 0U;
    emitted_packets_ = 0U;
    discarded_sync_search_bytes_ = 0U;
    invalid_tag_packets_ = 0U;
    sync_loss_events_ = 0U;
#if defined(DTV_USERLAND_DIAGNOSTICS)
    diagnostic_packet_ordinal_ = 0U;
    diagnostic_input_slots_ = {};
    diagnostic_transfer_input_samples_ = {};
    diagnostic_transfer_input_sample_head_ = 0U;
    diagnostic_transfer_input_sample_count_ = 0U;
    diagnostic_transfer_input_head_ordinal_ = 0U;
    diagnostic_input_packet_ordinal_ = 0U;
    diagnostic_input_byte_offset_ = 0U;
    diagnostic_input_next_packet_offset_ = 0U;
    diagnostic_input_synchronized_ = false;
    diagnostic_transfer_input_sample_overflow_ = false;
#endif
}

TaggedTsDemux::Counters TaggedTsDemux::counters() const noexcept
{
    return Counters{input_bytes_accepted_, emitted_packets_, discarded_sync_search_bytes_,
                    invalid_tag_packets_, sync_loss_events_, pending_size_};
}

}  // namespace px4::userland

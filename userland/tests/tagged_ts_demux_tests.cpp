// Modified/ported for px4-userland on 2026-09-03.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: driver/px4_device.c, driver/ts_sync.h,
// winusb/src/DriverHost_PX4/px4_device.cpp,
// winusb/tests/ts_sync_condition_test.cpp.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only

#include "tagged_ts_demux.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using namespace px4::userland;

#define DEMUX_CHECK(condition)                                                        \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "tagged demux check failed at %s:%d: %s\n",          \
                         __FILE__, __LINE__, #condition);                             \
            return false;                                                             \
        }                                                                              \
    } while (false)

using Packet = std::array<std::uint8_t, TaggedTsDemux::kPacketSize>;

Packet make_packet(std::uint8_t tag, std::uint8_t fill)
{
    Packet packet{};
    packet[0U] = static_cast<std::uint8_t>((tag << 4U) | 0x07U);
    packet.fill(fill);
    packet[0U] = static_cast<std::uint8_t>((tag << 4U) | 0x07U);
    return packet;
}

void append_packet(std::vector<std::uint8_t>& stream, const Packet& packet)
{
    stream.insert(stream.end(), packet.begin(), packet.end());
}

struct SinkState final {
    std::vector<std::vector<std::uint8_t>> packets;
    std::vector<std::size_t> receiver_indices;
    Error next_error = Error::OK;
    std::size_t failures_remaining = 0U;
    bool delay_failure = false;
    std::size_t successes_before_failure = 0U;
};

Result<void> record_packet(void* context, std::size_t receiver_index,
                           ByteView packet) noexcept
{
    auto& state = *static_cast<SinkState*>(context);
    if (state.failures_remaining != 0U &&
        (!state.delay_failure || state.packets.size() >= state.successes_before_failure)) {
        --state.failures_remaining;
        return Result<void>::failure(state.next_error);
    }
    state.receiver_indices.push_back(receiver_index);
    state.packets.emplace_back(packet.data, packet.data + packet.size);
    return Result<void>::success();
}

bool check_output(const SinkState& state, const std::vector<Packet>& expected)
{
    DEMUX_CHECK(state.packets.size() == expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        DEMUX_CHECK(state.packets[index].size() == TaggedTsDemux::kPacketSize);
        DEMUX_CHECK(state.packets[index][0U] == 0x47U);
        for (std::size_t byte = 1U; byte < TaggedTsDemux::kPacketSize; ++byte) {
            DEMUX_CHECK(state.packets[index][byte] == expected[index][byte]);
        }
    }
    return true;
}

bool test_aligned_tags_and_copy()
{
    std::vector<std::uint8_t> stream;
    std::vector<Packet> expected;
    for (std::uint8_t tag = 1U; tag <= 4U; ++tag) {
        expected.push_back(make_packet(tag, static_cast<std::uint8_t>(0xa0U + tag)));
        append_packet(stream, expected.back());
    }
    const auto original = stream;
    SinkState sink;
    TaggedTsDemux demux;
    DEMUX_CHECK(demux.push(ByteView{stream.data(), stream.size()}, record_packet, &sink));
    DEMUX_CHECK(stream == original);
    DEMUX_CHECK(check_output(sink, expected));
    DEMUX_CHECK((sink.receiver_indices == std::vector<std::size_t>{0U, 1U, 2U, 3U}));
    const auto counters = demux.counters();
    DEMUX_CHECK(counters.input_bytes_accepted == stream.size());
    DEMUX_CHECK(counters.emitted_packets == 4U);
    DEMUX_CHECK(counters.discarded_sync_search_bytes == 0U);
    DEMUX_CHECK(counters.invalid_tag_packets == 0U);
    DEMUX_CHECK(counters.buffered_bytes == 0U);
    return true;
}

bool test_every_split_position()
{
    std::vector<std::uint8_t> stream;
    std::vector<Packet> expected;
    for (std::uint8_t tag = 1U; tag <= 4U; ++tag) {
        expected.push_back(make_packet(tag, static_cast<std::uint8_t>(0xb0U + tag)));
        append_packet(stream, expected.back());
    }

    TaggedTsDemux demux;
    SinkState sink;
    for (std::size_t split = 0U; split <= stream.size(); ++split) {
        demux.reset();
        sink.packets.clear();
        sink.receiver_indices.clear();
        DEMUX_CHECK(demux.push(ByteView{stream.data(), split}, record_packet, &sink));
        DEMUX_CHECK(demux.push(ByteView{stream.data() + split, stream.size() - split},
                               record_packet, &sink));
        DEMUX_CHECK(check_output(sink, expected));
    }
    return true;
}

bool test_one_byte_feed()
{
    std::vector<std::uint8_t> stream;
    std::vector<Packet> expected;
    for (std::uint8_t tag = 1U; tag <= 4U; ++tag) {
        expected.push_back(make_packet(tag, static_cast<std::uint8_t>(0xc0U + tag)));
        append_packet(stream, expected.back());
    }
    SinkState sink;
    TaggedTsDemux demux;
    for (std::size_t index = 0U; index < stream.size(); ++index) {
        DEMUX_CHECK(demux.push(ByteView{stream.data() + index, 1U}, record_packet, &sink));
    }
    DEMUX_CHECK(check_output(sink, expected));
    DEMUX_CHECK(demux.counters().buffered_bytes == 0U);
    return true;
}

bool test_garbage_false_sync_and_remainder()
{
    std::vector<std::uint8_t> stream(23U, 0xa5U);
    for (std::uint8_t tag = 1U; tag <= 3U; ++tag) {
        append_packet(stream, make_packet(tag, 0xa1U));
    }
    auto false_packet = make_packet(4U, 0xa2U);
    false_packet[0U] = 0x46U;
    append_packet(stream, false_packet);

    std::vector<Packet> expected;
    for (std::uint8_t tag = 1U; tag <= 4U; ++tag) {
        expected.push_back(make_packet(tag, static_cast<std::uint8_t>(0xd0U + tag)));
        append_packet(stream, expected.back());
    }

    SinkState sink;
    TaggedTsDemux demux;
    const std::size_t true_stream_offset = 23U + (4U * TaggedTsDemux::kPacketSize);
    const std::size_t split = true_stream_offset + 2U * TaggedTsDemux::kPacketSize + 17U;
    DEMUX_CHECK(demux.push(ByteView{stream.data(), split}, record_packet, &sink));
    DEMUX_CHECK(sink.packets.empty());
    DEMUX_CHECK(demux.counters().buffered_bytes ==
                (TaggedTsDemux::kSyncPacketCount * TaggedTsDemux::kPacketSize) - 1U);
    DEMUX_CHECK(demux.push(ByteView{stream.data() + split, stream.size() - split},
                           record_packet, &sink));
    DEMUX_CHECK(check_output(sink, expected));
    DEMUX_CHECK(demux.counters().discarded_sync_search_bytes == true_stream_offset);
    return true;
}

bool test_invalid_tags_are_consumed()
{
    std::vector<std::uint8_t> stream;
    constexpr std::array<std::uint8_t, 4U> tags{0U, 5U, 6U, 7U};
    for (std::uint8_t tag : tags) {
        append_packet(stream, make_packet(tag, 0xe0U));
    }
    const auto valid = make_packet(1U, 0xe1U);
    append_packet(stream, valid);
    SinkState sink;
    TaggedTsDemux demux;
    DEMUX_CHECK(demux.push(ByteView{stream.data(), stream.size()}, record_packet, &sink));
    DEMUX_CHECK(sink.packets.size() == 1U);
    DEMUX_CHECK(sink.receiver_indices[0U] == 0U);
    DEMUX_CHECK(sink.packets[0U][0U] == 0x47U);
    DEMUX_CHECK(demux.counters().invalid_tag_packets == 4U);
    DEMUX_CHECK(demux.counters().buffered_bytes == 0U);
    return true;
}

struct TagObserverState final {
    std::vector<std::uint8_t> wire_syncs;
};

void observe_invalid_tag(void* context, std::uint8_t wire_sync) noexcept
{
    auto& state = *static_cast<TagObserverState*>(context);
    state.wire_syncs.push_back(wire_sync);
}

bool test_aligned_tei_observer_preserves_boundary()
{
    std::vector<std::uint8_t> input;
    append_packet(input, make_packet(1U, 0x11U));
    append_packet(input, make_packet(9U, 0x22U));
    append_packet(input, make_packet(2U, 0x33U));
    append_packet(input, make_packet(5U, 0x44U));
    append_packet(input, make_packet(3U, 0x55U));

    SinkState sink;
    TagObserverState observer;
    TaggedTsDemux demux;
    DEMUX_CHECK(demux.push(ByteView{input.data(), input.size()}, record_packet, &sink,
                           observe_invalid_tag, &observer));
    DEMUX_CHECK((sink.receiver_indices == std::vector<std::size_t>{0U, 1U, 2U}));
    DEMUX_CHECK((observer.wire_syncs == std::vector<std::uint8_t>{0x97U}));
    DEMUX_CHECK(demux.counters().invalid_tag_packets == 2U);
    DEMUX_CHECK(demux.counters().emitted_packets == 3U);
    DEMUX_CHECK(demux.counters().buffered_bytes == 0U);
    return true;
}

bool test_reset()
{
    std::array<std::uint8_t, 3U> partial{0xa5U, 0xa5U, 0xa5U};
    TaggedTsDemux demux;
    SinkState sink;
    DEMUX_CHECK(demux.push(ByteView{partial.data(), partial.size()}, record_packet, &sink));
    DEMUX_CHECK(demux.counters().input_bytes_accepted == partial.size());
    demux.reset();
    const auto counters = demux.counters();
    DEMUX_CHECK(counters.input_bytes_accepted == 0U);
    DEMUX_CHECK(counters.emitted_packets == 0U);
    DEMUX_CHECK(counters.discarded_sync_search_bytes == 0U);
    DEMUX_CHECK(counters.invalid_tag_packets == 0U);
    DEMUX_CHECK(counters.buffered_bytes == 0U);
    std::vector<std::uint8_t> stream;
    std::vector<Packet> expected;
    for (std::uint8_t tag = 1U; tag <= 4U; ++tag) {
        expected.push_back(make_packet(tag, static_cast<std::uint8_t>(0xf0U + tag)));
        append_packet(stream, expected.back());
    }
    DEMUX_CHECK(demux.push(ByteView{stream.data(), stream.size()}, record_packet, &sink));
    DEMUX_CHECK(check_output(sink, expected));
    return true;
}

bool test_sink_failure_retry()
{
    std::vector<std::uint8_t> stream;
    std::vector<Packet> expected;
    constexpr std::array<std::uint8_t, 6U> tags{1U, 2U, 3U, 4U, 1U, 2U};
    for (std::uint8_t tag : tags) {
        expected.push_back(make_packet(tag, static_cast<std::uint8_t>(0x20U + tag)));
        append_packet(stream, expected.back());
    }
    SinkState sink;
    sink.next_error = Error::USB_IO;
    sink.failures_remaining = 1U;
    TaggedTsDemux demux;
    const auto failed = demux.push(ByteView{stream.data(), stream.size()}, record_packet, &sink);
    DEMUX_CHECK(!failed && failed.error() == Error::USB_IO);
    DEMUX_CHECK(sink.packets.empty());
    DEMUX_CHECK(demux.counters().emitted_packets == 0U);
    DEMUX_CHECK(demux.counters().buffered_bytes == stream.size());
    std::memset(stream.data(), 0, stream.size());
    DEMUX_CHECK(demux.push(ByteView{nullptr, 0U}, record_packet, &sink));
    DEMUX_CHECK(check_output(sink, expected));
    DEMUX_CHECK(demux.counters().emitted_packets == expected.size());
    DEMUX_CHECK(demux.counters().buffered_bytes == 0U);
    return true;
}

bool test_full_transfer_stress()
{
    constexpr std::size_t kBatchPacketCount = 816U;
    constexpr std::size_t kBatchCount = 4U;
    constexpr std::size_t kBatchSize = kBatchPacketCount * TaggedTsDemux::kPacketSize;

    std::vector<std::uint8_t> stream;
    std::vector<Packet> expected;
    stream.reserve(kBatchCount * kBatchSize);
    expected.reserve(kBatchCount * kBatchPacketCount);
    for (std::size_t batch = 0U; batch < kBatchCount; ++batch) {
        for (std::size_t packet = 0U; packet < kBatchPacketCount; ++packet) {
            const auto tag = static_cast<std::uint8_t>((packet % 4U) + 1U);
            const auto fill = static_cast<std::uint8_t>(batch + packet);
            expected.push_back(make_packet(tag, fill));
            append_packet(stream, expected.back());
        }
    }

    SinkState sink;
    TaggedTsDemux demux;
    for (std::size_t batch = 0U; batch < kBatchCount; ++batch) {
        const auto* input = stream.data() + (batch * kBatchSize);
        DEMUX_CHECK(demux.push(ByteView{input, kBatchSize}, record_packet, &sink));
    }

    DEMUX_CHECK(check_output(sink, expected));
    DEMUX_CHECK(sink.receiver_indices.size() == expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        DEMUX_CHECK(sink.receiver_indices[index] == index % 4U);
    }
    const auto counters = demux.counters();
    DEMUX_CHECK(counters.input_bytes_accepted == stream.size());
    DEMUX_CHECK(counters.emitted_packets == expected.size());
    DEMUX_CHECK(counters.discarded_sync_search_bytes == 0U);
    DEMUX_CHECK(counters.invalid_tag_packets == 0U);
    DEMUX_CHECK(counters.buffered_bytes == 0U);
    return true;
}

bool test_sink_failure_after_many_successes()
{
    constexpr std::size_t kPacketCount = 2U * 816U;
    constexpr std::size_t kFailedPacket = 500U;

    std::vector<std::uint8_t> stream;
    std::vector<Packet> expected;
    stream.reserve(kPacketCount * TaggedTsDemux::kPacketSize);
    expected.reserve(kPacketCount);
    for (std::size_t packet = 0U; packet < kPacketCount; ++packet) {
        const auto tag = static_cast<std::uint8_t>((packet % 4U) + 1U);
        expected.push_back(make_packet(tag, static_cast<std::uint8_t>(0x40U + packet)));
        append_packet(stream, expected.back());
    }

    SinkState sink;
    sink.next_error = Error::USB_IO;
    sink.failures_remaining = 1U;
    sink.delay_failure = true;
    sink.successes_before_failure = kFailedPacket;
    TaggedTsDemux demux;
    const auto failed = demux.push(ByteView{stream.data(), stream.size()}, record_packet, &sink);
    DEMUX_CHECK(!failed && failed.error() == Error::USB_IO);
    DEMUX_CHECK(sink.packets.size() == kFailedPacket);
    DEMUX_CHECK(demux.counters().emitted_packets == kFailedPacket);
    DEMUX_CHECK(demux.counters().buffered_bytes ==
                stream.size() - (kFailedPacket * TaggedTsDemux::kPacketSize));

    std::memset(stream.data(), 0, stream.size());
    DEMUX_CHECK(demux.push(ByteView{nullptr, 0U}, record_packet, &sink));
    DEMUX_CHECK(check_output(sink, expected));
    DEMUX_CHECK(sink.receiver_indices.size() == expected.size());
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        DEMUX_CHECK(sink.receiver_indices[index] == index % 4U);
    }
    DEMUX_CHECK(demux.counters().emitted_packets == expected.size());
    DEMUX_CHECK(demux.counters().buffered_bytes == 0U);
    return true;
}

bool test_input_limits_and_bounded_garbage()
{
    std::vector<std::uint8_t> oversized(kMaxStreamTransfer + 1U, 0xa5U);
    const auto original = oversized;
    TaggedTsDemux demux;
    SinkState sink;
    const auto rejected = demux.push(ByteView{oversized.data(), oversized.size()}, record_packet, &sink);
    DEMUX_CHECK(!rejected && rejected.error() == Error::INVALID_ARGUMENT);
    DEMUX_CHECK(oversized == original);
    DEMUX_CHECK(demux.counters().input_bytes_accepted == 0U);
    DEMUX_CHECK(demux.counters().buffered_bytes == 0U);

    std::vector<std::uint8_t> garbage(4096U, 0xa5U);
    for (std::size_t iteration = 0U; iteration < 100U; ++iteration) {
        DEMUX_CHECK(demux.push(ByteView{garbage.data(), garbage.size()}, record_packet, &sink));
        DEMUX_CHECK(demux.counters().buffered_bytes <
                    TaggedTsDemux::kSyncPacketCount * TaggedTsDemux::kPacketSize);
    }

    TaggedTsDemux blocked;
    SinkState failing;
    failing.next_error = Error::TIMEOUT;
    failing.failures_remaining = 1U;
    std::vector<std::uint8_t> four_packets;
    for (std::uint8_t tag = 1U; tag <= 4U; ++tag) {
        append_packet(four_packets, make_packet(tag, 0x55U));
    }
    four_packets.push_back(0xa5U);
    DEMUX_CHECK(!blocked.push(ByteView{four_packets.data(), four_packets.size()}, record_packet,
                             &failing));
    std::vector<std::uint8_t> max_input(kMaxStreamTransfer, 0xa5U);
    const auto slow = blocked.push(ByteView{max_input.data(), max_input.size()}, record_packet,
                                   &failing);
    DEMUX_CHECK(!slow && slow.error() == Error::SLOW_CONSUMER);
    DEMUX_CHECK(blocked.counters().input_bytes_accepted == four_packets.size());
    DEMUX_CHECK(blocked.counters().buffered_bytes == four_packets.size());
    return true;
}

}  // namespace

bool run_tagged_ts_demux_tests()
{
    return test_aligned_tags_and_copy() && test_every_split_position() &&
           test_one_byte_feed() && test_garbage_false_sync_and_remainder() &&
           test_invalid_tags_are_consumed() && test_aligned_tei_observer_preserves_boundary() &&
           test_reset() && test_sink_failure_retry() &&
           test_full_transfer_stress() && test_sink_failure_after_many_successes() &&
           test_input_limits_and_bounded_garbage();
}

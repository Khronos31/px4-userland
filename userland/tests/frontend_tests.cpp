// Modified/ported for px4-userland on 2026-09-02.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: driver/tc90522.c, driver/r850.c, driver/rt710.c,
// driver/px4_device.c.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only

#include "bridge_i2c.h"
#include "tc90522.h"

#include "px4/mock_transport.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <initializer_list>
#include <utility>
#include <vector>

namespace {

using namespace px4::userland;

#define FRONTEND_CHECK(condition)                                                   \
    do {                                                                             \
        if (!(condition)) {                                                          \
            std::fprintf(stderr, "frontend check failed at %s:%d: %s\n",           \
                         __FILE__, __LINE__, #condition);                            \
            return false;                                                            \
        }                                                                            \
    } while (false)

struct RecordedOperation final {
    BridgeI2cRequestType type;
    std::uint8_t address;
    std::vector<std::uint8_t> data;
};

class RecordingBridge final : public BridgeI2cMaster {
public:
    Result<void> request(BridgeI2cRequest* requests,
                         std::size_t count) noexcept override
    {
        if (requests == nullptr || count == 0U) {
            return Result<void>::failure(Error::INVALID_ARGUMENT);
        }
        std::vector<RecordedOperation> batch;
        batch.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            auto& request = requests[index];
            if (request.type == BridgeI2cRequestType::write) {
                batch.push_back(RecordedOperation{
                    request.type, request.address,
                    std::vector<std::uint8_t>(request.write_data.data,
                                               request.write_data.data + request.write_data.size)});
            } else if (request.type == BridgeI2cRequestType::read) {
                if (request.read_data.size == 0U || request.read_data.data == nullptr) {
                    return Result<void>::failure(Error::INVALID_ARGUMENT);
                }
                if (fail_reads) {
                    return Result<void>::failure(failure);
                }
                if (read_values.empty()) {
                    return Result<void>::failure(Error::PROTOCOL_ERROR);
                }
                const auto value = std::move(read_values.front());
                read_values.pop_front();
                if (value.size() != request.read_data.size) {
                    return Result<void>::failure(Error::PROTOCOL_ERROR);
                }
                std::memcpy(request.read_data.data, value.data(), value.size());
                batch.push_back(RecordedOperation{request.type, request.address, value});
            } else {
                return Result<void>::failure(Error::INVALID_ARGUMENT);
            }
        }
        batches.push_back(std::move(batch));
        if (fail_writes) {
            return Result<void>::failure(failure);
        }
        return Result<void>::success();
    }

    void queue_read(std::initializer_list<std::uint8_t> value)
    {
        read_values.emplace_back(value);
    }

    void clear()
    {
        batches.clear();
        read_values.clear();
        fail_reads = false;
        fail_writes = false;
        failure = Error::DISCONNECTED;
    }

    std::vector<std::vector<RecordedOperation>> batches;
    std::deque<std::vector<std::uint8_t>> read_values;
    bool fail_reads = false;
    bool fail_writes = false;
    Error failure = Error::DISCONNECTED;
};

bool check_write(const RecordedOperation& operation, std::uint8_t address,
                std::initializer_list<std::uint8_t> data)
{
    return operation.type == BridgeI2cRequestType::write &&
           operation.address == address &&
           operation.data == std::vector<std::uint8_t>(data);
}

bool check_read(const RecordedOperation& operation, std::uint8_t address,
                std::initializer_list<std::uint8_t> data)
{
    return operation.type == BridgeI2cRequestType::read &&
           operation.address == address &&
           operation.data == std::vector<std::uint8_t>(data);
}

bool test_mapping()
{
    constexpr std::array<std::uint8_t, 4U> addresses{0x11U, 0x13U, 0x10U, 0x12U};
    for (std::uint8_t index = 0U; index < 4U; ++index) {
        const auto mapping = q3u4_receiver_mapping(index);
        FRONTEND_CHECK(mapping);
        FRONTEND_CHECK(mapping.value().local_receiver == index);
        FRONTEND_CHECK(mapping.value().demod_address == addresses[index]);
        FRONTEND_CHECK(mapping.value().secondary == ((index & 1U) != 0U));
        FRONTEND_CHECK(mapping.value().system ==
                       (index < 2U ? Tc90522System::isdb_s : Tc90522System::isdb_t));
    }
    FRONTEND_CHECK(!q3u4_receiver_mapping(4U));
    return true;
}

bool test_bridge_batch_recording()
{
    RecordingBridge bridge;
    std::array<std::uint8_t, 1U> first{0x10U};
    std::array<std::uint8_t, 2U> second{0x20U, 0x21U};
    std::array<std::uint8_t, 1U> result{};
    std::array<BridgeI2cRequest, 3U> requests{
        BridgeI2cRequest{BridgeI2cRequestType::write, 0x11U,
                         ByteView{first.data(), first.size()}, MutableByteView{nullptr, 0U}},
        BridgeI2cRequest{BridgeI2cRequestType::write, 0x13U,
                         ByteView{second.data(), second.size()}, MutableByteView{nullptr, 0U}},
        BridgeI2cRequest{BridgeI2cRequestType::read, 0x11U,
                         ByteView{nullptr, 0U}, MutableByteView{result.data(), result.size()}}};
    bridge.queue_read({0x55U});
    FRONTEND_CHECK(bridge.request(requests.data(), requests.size()));
    FRONTEND_CHECK(bridge.batches.size() == 1U);
    FRONTEND_CHECK(bridge.batches[0].size() == 3U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0x10U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x13U, {0x20U, 0x21U}));
    FRONTEND_CHECK(check_read(bridge.batches[0][2], 0x11U, {0x55U}));
    FRONTEND_CHECK(result[0] == 0x55U);
    // A frontend operation is submitted as one complete batch.  The recording
    // seam makes the lock boundary deterministic without scheduling assumptions;
    // the production adaptor holds its mutex around the same request call.
    return true;
}

bool test_tc_direct_and_multiple()
{
    RecordingBridge bridge;
    const auto mapping = q3u4_receiver_mapping(2U);
    FRONTEND_CHECK(mapping);
    Tc90522 demod(bridge, mapping.value());

    const std::array<std::uint8_t, 2U> values{0xa5U, 0x5aU};
    FRONTEND_CHECK(demod.write_regs(0x20U, ByteView{values.data(), values.size()}));
    FRONTEND_CHECK(bridge.batches.size() == 1U && bridge.batches[0].size() == 1U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x20U, 0xa5U, 0x5aU}));

    bridge.clear();
    bridge.queue_read({0x11U, 0x22U});
    const auto read = demod.read_regs(0x20U, 2U);
    FRONTEND_CHECK(read && read.value() == std::vector<std::uint8_t>({0x11U, 0x22U}));
    FRONTEND_CHECK(bridge.batches.size() == 1U && bridge.batches[0].size() == 2U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x20U}));
    FRONTEND_CHECK(check_read(bridge.batches[0][1], 0x10U, {0x11U, 0x22U}));

    bridge.clear();
    std::array<std::uint8_t, 1U> first_read{};
    std::array<std::uint8_t, 2U> second_read{};
    std::vector<Tc90522RegisterRead> reads{
        Tc90522RegisterRead{0x30U, MutableByteView{first_read.data(), first_read.size()}},
        Tc90522RegisterRead{0x40U, MutableByteView{second_read.data(), second_read.size()}}};
    bridge.queue_read({0x01U});
    bridge.queue_read({0x02U, 0x03U});
    FRONTEND_CHECK(demod.read_multiple_regs(reads));
    FRONTEND_CHECK(bridge.batches.size() == 1U && bridge.batches[0].size() == 4U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x30U}));
    FRONTEND_CHECK(check_read(bridge.batches[0][1], 0x10U, {0x01U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][2], 0x10U, {0x40U}));
    FRONTEND_CHECK(check_read(bridge.batches[0][3], 0x10U, {0x02U, 0x03U}));

    bridge.clear();
    const std::array<std::uint8_t, 1U> one{0x99U};
    const std::array<std::uint8_t, 1U> two{0x88U};
    std::vector<Tc90522RegisterWrite> writes{
        Tc90522RegisterWrite{0x50U, ByteView{one.data(), one.size()}},
        Tc90522RegisterWrite{0x51U, ByteView{two.data(), two.size()}}};
    FRONTEND_CHECK(demod.write_multiple_regs(writes));
    FRONTEND_CHECK(bridge.batches.size() == 1U && bridge.batches[0].size() == 2U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x50U, 0x99U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x10U, {0x51U, 0x88U}));

    bridge.clear();
    bridge.fail_reads = true;
    const auto failed_read = demod.read_regs(0x20U, 1U);
    FRONTEND_CHECK(!failed_read && failed_read.error() == Error::DISCONNECTED);
    bridge.clear();
    bridge.queue_read({0x01U});
    const auto short_read = demod.read_regs(0x20U, 2U);
    FRONTEND_CHECK(!short_read && short_read.error() == Error::PROTOCOL_ERROR);

    std::array<std::uint8_t, 1U> invalid_data{};
    FRONTEND_CHECK(!demod.read_regs(0x20U, 0U));
    FRONTEND_CHECK(!demod.read_regs(0x20U, 256U));
    FRONTEND_CHECK(!demod.write_regs(0x20U, ByteView{nullptr, 1U}));
    FRONTEND_CHECK(!demod.write_regs(0x20U, ByteView{invalid_data.data(), 0U}));
    std::vector<Tc90522RegisterRead> empty_reads;
    std::vector<Tc90522RegisterWrite> empty_writes;
    FRONTEND_CHECK(!demod.read_multiple_regs(empty_reads));
    FRONTEND_CHECK(!demod.write_multiple_regs(empty_writes));
    return true;
}

bool test_repeater_translation()
{
    RecordingBridge bridge;
    const auto mapping = q3u4_receiver_mapping(0U);
    FRONTEND_CHECK(mapping);
    Tc90522 demod(bridge, mapping.value());
    const std::array<std::uint8_t, 2U> write_data{0x33U, 0x44U};

    Tc90522I2cRequest one_write{
        Tc90522RequestType::write, 0x7cU,
        ByteView{write_data.data(), write_data.size()}, MutableByteView{nullptr, 0U}};
    FRONTEND_CHECK(demod.downstream_request(&one_write, 1U));
    FRONTEND_CHECK(bridge.batches.size() == 1U && bridge.batches[0].size() == 1U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U,
                               {0xfeU, 0xf8U, 0x33U, 0x44U}));

    bridge.clear();
    std::array<std::uint8_t, 3U> read_data{};
    Tc90522I2cRequest write_read[2]{
        Tc90522I2cRequest{Tc90522RequestType::write, 0x7cU,
                          ByteView{write_data.data(), write_data.size()},
                          MutableByteView{nullptr, 0U}},
        Tc90522I2cRequest{Tc90522RequestType::read, 0x7aU,
                          ByteView{nullptr, 0U}, MutableByteView{read_data.data(), read_data.size()}}};
    bridge.queue_read({0x01U, 0x02U, 0x03U});
    FRONTEND_CHECK(demod.downstream_request(write_read, 2U));
    FRONTEND_CHECK(bridge.batches.size() == 1U && bridge.batches[0].size() == 3U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U,
                               {0xfeU, 0xf8U, 0x33U, 0x44U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x11U, {0xfeU, 0xf5U}));
    FRONTEND_CHECK(check_read(bridge.batches[0][2], 0x11U, {0x01U, 0x02U, 0x03U}));

    bridge.clear();
    std::array<std::uint8_t, 1U> mixed_read{};
    Tc90522I2cRequest mixed[3]{
        Tc90522I2cRequest{Tc90522RequestType::read, 0x7aU,
                          ByteView{nullptr, 0U}, MutableByteView{mixed_read.data(), mixed_read.size()}},
        Tc90522I2cRequest{Tc90522RequestType::write, 0x7cU,
                          ByteView{write_data.data(), write_data.size()},
                          MutableByteView{nullptr, 0U}},
        Tc90522I2cRequest{Tc90522RequestType::read, 0x7cU,
                          ByteView{nullptr, 0U}, MutableByteView{read_data.data(), read_data.size()}}};
    bridge.queue_read({0x09U});
    bridge.queue_read({0x0aU, 0x0bU, 0x0cU});
    FRONTEND_CHECK(demod.downstream_request(mixed, 3U));
    FRONTEND_CHECK(bridge.batches[0].size() == 5U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0xfeU, 0xf5U}));
    FRONTEND_CHECK(check_read(bridge.batches[0][1], 0x11U, {0x09U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][2], 0x11U, {0xfeU, 0xf8U, 0x33U, 0x44U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][3], 0x11U, {0xfeU, 0xf9U}));
    FRONTEND_CHECK(check_read(bridge.batches[0][4], 0x11U, {0x0aU, 0x0bU, 0x0cU}));

    Tc90522I2cRequest invalid{
        Tc90522RequestType::undefined, 0x7cU, ByteView{nullptr, 0U}, MutableByteView{nullptr, 0U}};
    const auto before = bridge.batches.size();
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    FRONTEND_CHECK(bridge.batches.size() == before);
    invalid.type = Tc90522RequestType::write;
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    invalid.write_data = ByteView{write_data.data(), 0U};
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    invalid.write_data = ByteView{nullptr, 1U};
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    const std::array<std::uint8_t, 254U> oversized_write{};
    invalid.write_data = ByteView{oversized_write.data(), oversized_write.size()};
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    invalid.write_data = ByteView{write_data.data(), write_data.size()};
    invalid.address = 0x80U;
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    invalid.address = 0x7cU;
    invalid.write_data = ByteView{write_data.data(), 0U};
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    invalid.type = Tc90522RequestType::read;
    invalid.write_data = ByteView{nullptr, 0U};
    invalid.read_data = MutableByteView{nullptr, 1U};
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    invalid.read_data = MutableByteView{read_data.data(), 0U};
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    std::array<std::uint8_t, 256U> oversized_read{};
    invalid.read_data = MutableByteView{oversized_read.data(), oversized_read.size()};
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 1U));
    FRONTEND_CHECK(!demod.downstream_request(nullptr, 1U));
    FRONTEND_CHECK(!demod.downstream_request(&invalid, 0U));
    return true;
}

bool test_ts_helpers_and_decoding()
{
    RecordingBridge bridge;
    const auto s_mapping = q3u4_receiver_mapping(0U);
    const auto s_secondary_mapping = q3u4_receiver_mapping(1U);
    const auto t_mapping = q3u4_receiver_mapping(2U);
    FRONTEND_CHECK(s_mapping && s_secondary_mapping && t_mapping);
    Tc90522 satellite(bridge, s_mapping.value());
    Tc90522 satellite_secondary(bridge, s_secondary_mapping.value());
    Tc90522 terrestrial(bridge, t_mapping.value());

    FRONTEND_CHECK(satellite.sleep_s(true));
    FRONTEND_CHECK(bridge.batches.size() == 1U && bridge.batches[0].size() == 2U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0x13U, 0x80U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x11U, {0x17U, 0xffU}));
    bridge.clear();
    FRONTEND_CHECK(satellite.sleep_s(false));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0x13U, 0x00U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x11U, {0x17U, 0x00U}));
    bridge.clear();
    FRONTEND_CHECK(satellite.set_agc_s(false));
    FRONTEND_CHECK(bridge.batches[0].size() == 4U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0x0aU, 0x00U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x11U, {0x10U, 0xb0U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][2], 0x11U, {0x11U, 0x02U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][3], 0x11U, {0x03U, 0x01U}));
    bridge.clear();
    FRONTEND_CHECK(satellite_secondary.set_agc_s(true));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x13U, {0x0aU, 0xffU}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x13U, {0x10U, 0x32U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][2], 0x13U, {0x11U, 0x00U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][3], 0x13U, {0x03U, 0x01U}));
    bridge.clear();
    FRONTEND_CHECK(satellite.enable_ts_pins_s(false));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0x1cU, 0x80U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x11U, {0x1fU, 0x22U}));
    bridge.clear();
    FRONTEND_CHECK(satellite.enable_ts_pins_s(true));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0x1cU, 0x00U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x11U, {0x1fU, 0x00U}));
    bridge.clear();
    bridge.queue_read({0x12U, 0x34U});
    const auto tmcc = satellite.tmcc_get_tsid_s(11U);
    FRONTEND_CHECK(tmcc && tmcc.value() == 0x1234U);
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0xe4U}));
    FRONTEND_CHECK(check_read(bridge.batches[0][1], 0x11U, {0x12U, 0x34U}));
    FRONTEND_CHECK(!satellite.tmcc_get_tsid_s(12U));
    bridge.clear();
    bridge.queue_read({0xabU, 0xcdU});
    FRONTEND_CHECK(satellite.get_tsid_s().value() == 0xabcdU);
    bridge.clear();
    FRONTEND_CHECK(satellite.set_tsid_s(0x1234U));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x11U, {0x8fU, 0x12U, 0x34U}));
    bridge.clear();
    bridge.queue_read({0x01U, 0x02U});
    FRONTEND_CHECK(satellite.get_cn_s().value() == 0x0102U);
    bridge.clear();
    bridge.queue_read({0x12U, 0x34U, 0x56U});
    FRONTEND_CHECK(terrestrial.get_cndat_t().value() == 0x123456U);

    bridge.clear();
    FRONTEND_CHECK(terrestrial.sleep_t(true));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x03U, 0xf0U}));
    bridge.clear();
    FRONTEND_CHECK(terrestrial.sleep_t(false));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x03U, 0x00U}));
    bridge.clear();
    FRONTEND_CHECK(terrestrial.set_agc_t(false));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x25U, 0x00U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x10U, {0x20U, 0x00U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][2], 0x10U, {0x23U, 0x4dU}));
    FRONTEND_CHECK(check_write(bridge.batches[0][3], 0x10U, {0x01U, 0x50U}));
    bridge.clear();
    FRONTEND_CHECK(terrestrial.set_agc_t(true));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x25U, 0x00U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][1], 0x10U, {0x20U, 0x00U}));
    FRONTEND_CHECK(check_write(bridge.batches[0][2], 0x10U, {0x23U, 0x4cU}));
    FRONTEND_CHECK(check_write(bridge.batches[0][3], 0x10U, {0x01U, 0x50U}));
    bridge.clear();
    FRONTEND_CHECK(terrestrial.enable_ts_pins_t(false));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x1dU, 0xa8U}));
    bridge.clear();
    FRONTEND_CHECK(terrestrial.enable_ts_pins_t(true));
    FRONTEND_CHECK(check_write(bridge.batches[0][0], 0x10U, {0x1dU, 0x00U}));

    bridge.clear();
    bridge.queue_read({0x28U});
    FRONTEND_CHECK(terrestrial.is_signal_locked_t().value() == false);
    bridge.clear();
    bridge.queue_read({0x00U});
    bridge.queue_read({0x08U});
    FRONTEND_CHECK(terrestrial.is_signal_locked_t().value() == true);
    bridge.clear();
    bridge.queue_read({0x10U});
    FRONTEND_CHECK(satellite.is_signal_locked_s().value() == false);
    bridge.clear();
    bridge.queue_read({0x00U});
    FRONTEND_CHECK(satellite.is_signal_locked_s().value() == true);
    bridge.clear();
    bridge.fail_reads = true;
    const auto failed_lock = terrestrial.is_signal_locked_t();
    FRONTEND_CHECK(!failed_lock && failed_lock.error() == Error::DISCONNECTED);
    bridge.clear();
    bridge.fail_reads = true;
    const auto failed_satellite_lock = satellite.is_signal_locked_s();
    FRONTEND_CHECK(!failed_satellite_lock && failed_satellite_lock.error() == Error::DISCONNECTED);
    return true;
}

std::uint16_t checksum(const std::uint8_t* data, std::size_t size) noexcept
{
    std::uint16_t sum = 0U;
    for (std::size_t index = 0U; index < size; index += 2U) {
        sum = static_cast<std::uint16_t>(sum +
            static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[index]) << 8U |
                (index + 1U < size ? data[index + 1U] : 0U)));
    }
    return static_cast<std::uint16_t>(~sum);
}

std::vector<std::uint8_t> frame(std::uint16_t command, std::uint8_t sequence,
                                std::initializer_list<std::uint8_t> payload)
{
    std::vector<std::uint8_t> result(payload.size() + 6U, 0U);
    result[0] = static_cast<std::uint8_t>(result.size() - 1U);
    result[1] = static_cast<std::uint8_t>(command >> 8U);
    result[2] = static_cast<std::uint8_t>(command);
    result[3] = sequence;
    std::copy(payload.begin(), payload.end(), result.begin() + 4U);
    const auto sum = checksum(result.data() + 1U, result.size() - 3U);
    result[result.size() - 2U] = static_cast<std::uint8_t>(sum >> 8U);
    result.back() = static_cast<std::uint8_t>(sum);
    return result;
}

std::vector<std::uint8_t> response(std::uint8_t sequence)
{
    std::vector<std::uint8_t> result(5U, 0U);
    result[0] = 4U;
    result[1] = sequence;
    const auto sum = checksum(result.data() + 1U, 2U);
    result[3] = static_cast<std::uint8_t>(sum >> 8U);
    result[4] = static_cast<std::uint8_t>(sum);
    return result;
}

std::vector<std::uint8_t> response_with_payload(
    std::uint8_t sequence, std::initializer_list<std::uint8_t> payload)
{
    std::vector<std::uint8_t> result(payload.size() + 5U, 0U);
    result[0] = static_cast<std::uint8_t>(result.size() - 1U);
    result[1] = sequence;
    std::copy(payload.begin(), payload.end(), result.begin() + 3U);
    const auto sum = checksum(result.data() + 1U, result.size() - 3U);
    result[result.size() - 2U] = static_cast<std::uint8_t>(sum >> 8U);
    result.back() = static_cast<std::uint8_t>(sum);
    return result;
}

bool test_controller_wire_batch()
{
    MockTransport transport;
    It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
    It930xBridgeI2cMaster bridge(controller);
    const std::array<std::uint8_t, 1U> first{0xa5U};
    const std::array<std::uint8_t, 1U> second{0x5aU};
    BridgeI2cRequest requests[2]{
        BridgeI2cRequest{BridgeI2cRequestType::write, 0x11U,
                         ByteView{first.data(), first.size()}, MutableByteView{nullptr, 0U}},
        BridgeI2cRequest{BridgeI2cRequestType::write, 0x13U,
                         ByteView{second.data(), second.size()}, MutableByteView{nullptr, 0U}}};
    const auto first_frame = frame(0x2bU, 0U, {1U, 2U, 0x22U, 0xa5U});
    const auto second_frame = frame(0x2bU, 1U, {1U, 2U, 0x26U, 0x5aU});
    const auto first_response = response(0U);
    const auto second_response = response(1U);
    transport.expect_bulk_write(kCommandOutEndpoint,
                                ByteView{first_frame.data(), first_frame.size()});
    transport.expect_bulk_read(kCommandInEndpoint,
                               ByteView{first_response.data(), first_response.size()});
    transport.expect_bulk_write(kCommandOutEndpoint,
                                ByteView{second_frame.data(), second_frame.size()});
    transport.expect_bulk_read(kCommandInEndpoint,
                               ByteView{second_response.data(), second_response.size()});
    FRONTEND_CHECK(bridge.request(requests, 2U));
    FRONTEND_CHECK(transport.remaining_expectations() == 0U);

    std::array<std::uint8_t, 2U> read_data{};
    BridgeI2cRequest read_request{
        BridgeI2cRequestType::read, 0x11U, ByteView{nullptr, 0U},
        MutableByteView{read_data.data(), read_data.size()}};
    const auto read_frame = frame(0x2aU, 2U, {2U, 2U, 0x22U});
    const auto read_response = response_with_payload(2U, {0x12U, 0x34U});
    transport.expect_bulk_write(kCommandOutEndpoint,
                                ByteView{read_frame.data(), read_frame.size()});
    transport.expect_bulk_read(kCommandInEndpoint,
                               ByteView{read_response.data(), read_response.size()});
    FRONTEND_CHECK(bridge.request(&read_request, 1U));
    const std::array<std::uint8_t, 2U> expected_read{0x12U, 0x34U};
    FRONTEND_CHECK(read_data == expected_read);
    FRONTEND_CHECK(transport.remaining_expectations() == 0U);
    return true;
}

bool test_controller_failure_detail_for_translated_batch()
{
    const std::array<std::uint8_t, 1U> pointer{0x00U};
    std::array<std::uint8_t, 1U> read_data{};
    const auto mapping = q3u4_receiver_mapping(0U);
    FRONTEND_CHECK(mapping);

    for (std::size_t failed_index = 0U; failed_index < 3U; ++failed_index) {
        MockTransport transport;
        It930xController controller(transport,
                                    CommandPacingOptions{CommandPacingMode::no_delay});
        It930xBridgeI2cMaster bridge(controller);
        Tc90522I2cRequest downstream[2U]{
            Tc90522I2cRequest{Tc90522RequestType::write, 0x7cU,
                              ByteView{pointer.data(), pointer.size()},
                              MutableByteView{nullptr, 0U}},
            Tc90522I2cRequest{Tc90522RequestType::read, 0x7aU,
                              ByteView{nullptr, 0U},
                              MutableByteView{read_data.data(), read_data.size()}}};
        const auto first_payload = frame(0x2bU, 0U, {3U, 2U, 0x22U, 0xfeU, 0xf8U, 0x00U});
        const auto second_payload = frame(0x2bU, 1U, {2U, 2U, 0x22U, 0xfeU, 0xf5U});
        const auto third_payload = frame(0x2aU, 2U, {1U, 2U, 0x22U});
        const std::array<std::vector<std::uint8_t>, 3U> commands{
            first_payload, second_payload, third_payload};
        for (std::size_t index = 0U; index < 3U; ++index) {
            if (index == failed_index && index != 2U) {
                transport.expect_bulk_write(kCommandOutEndpoint,
                                            ByteView{commands[index].data(), commands[index].size()},
                                            MockOutcome::timeout);
            } else {
                transport.expect_bulk_write(kCommandOutEndpoint,
                                            ByteView{commands[index].data(), commands[index].size()});
                if (index == failed_index) {
                    transport.expect_bulk_read(kCommandInEndpoint,
                                               ByteView{nullptr, 0U},
                                               MockOutcome::timeout);
                } else if (index == 2U) {
                    const auto result = response_with_payload(
                        static_cast<std::uint8_t>(index), {0x70U});
                    transport.expect_bulk_read(kCommandInEndpoint,
                                               ByteView{result.data(), result.size()});
                } else {
                    const auto result = response(static_cast<std::uint8_t>(index));
                    transport.expect_bulk_read(kCommandInEndpoint,
                                               ByteView{result.data(), result.size()});
                }
            }
        }
        Tc90522 demod(bridge, mapping.value());
        const auto result = demod.downstream_request(downstream, 2U);
        FRONTEND_CHECK(!result && result.error() == Error::TIMEOUT);
        FRONTEND_CHECK(transport.operations().size() ==
                       (failed_index < 2U ? failed_index * 2U + 1U : 6U));
        FRONTEND_CHECK(transport.remaining_expectations() ==
                       (failed_index < 2U ? (2U - failed_index) * 2U : 0U));
        const auto detail = bridge.failure_detail();
        FRONTEND_CHECK(detail.valid && detail.request_index == failed_index);
        FRONTEND_CHECK(detail.type == (failed_index == 2U ? BridgeI2cRequestType::read
                                                          : BridgeI2cRequestType::write));
        FRONTEND_CHECK(detail.address == 0x11U && detail.write_length ==
                       (failed_index == 0U ? 3U : failed_index == 1U ? 2U : 0U));
        FRONTEND_CHECK(detail.read_length == (failed_index == 2U ? 1U : 0U));
        FRONTEND_CHECK(detail.captured_byte_count == (failed_index == 0U ? 3U :
                                                       failed_index == 1U ? 2U : 0U));
        if (failed_index == 0U) {
            FRONTEND_CHECK(detail.write_data[0] == 0xfeU && detail.write_data[1] == 0xf8U &&
                           detail.write_data[2] == 0x00U);
        } else if (failed_index == 1U) {
            FRONTEND_CHECK(detail.write_data[0] == 0xfeU && detail.write_data[1] == 0xf5U);
        }
    }

    MockTransport mismatch_transport;
    It930xController mismatch_controller(
        mismatch_transport, CommandPacingOptions{CommandPacingMode::no_delay});
    It930xBridgeI2cMaster mismatch_bridge(mismatch_controller);
    const auto command = frame(0x2aU, 0U, {1U, 2U, 0x22U});
    const auto empty_response = response(0U);
    mismatch_transport.expect_bulk_write(kCommandOutEndpoint,
                                         ByteView{command.data(), command.size()});
    mismatch_transport.expect_bulk_read(kCommandInEndpoint,
                                        ByteView{empty_response.data(), empty_response.size()});
    BridgeI2cRequest read_request{BridgeI2cRequestType::read, 0x11U,
                                  ByteView{nullptr, 0U},
                                  MutableByteView{read_data.data(), read_data.size()}};
    const auto mismatch = mismatch_bridge.request(&read_request, 1U);
    FRONTEND_CHECK(!mismatch && mismatch.error() == Error::PROTOCOL_ERROR);
    FRONTEND_CHECK(mismatch_bridge.failure_detail().valid &&
                   mismatch_bridge.failure_detail().request_index == 0U);
    const auto good_command = frame(0x2aU, 1U, {1U, 2U, 0x22U});
    const auto good_response = response_with_payload(1U, {0x70U});
    mismatch_transport.expect_bulk_write(kCommandOutEndpoint,
                                         ByteView{good_command.data(), good_command.size()});
    mismatch_transport.expect_bulk_read(kCommandInEndpoint,
                                        ByteView{good_response.data(), good_response.size()});
    FRONTEND_CHECK(mismatch_bridge.request(&read_request, 1U));
    FRONTEND_CHECK(!mismatch_bridge.failure_detail().valid);
    return true;
}

}  // namespace

bool run_frontend_tests()
{
    return test_mapping() && test_bridge_batch_recording() &&
           test_tc_direct_and_multiple() && test_repeater_translation() &&
           test_ts_helpers_and_decoding() && test_controller_wire_batch() &&
           test_controller_failure_detail_for_translated_batch();
}

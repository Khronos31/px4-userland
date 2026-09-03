// Modified/ported for px4-userland on 2026-09-03.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: driver/it930x.c, winusb/src/DriverHost_PX4/smart_card.cpp.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only
#include "px4/it930x.h"
#include "px4/mock_transport.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace px4::userland;

constexpr CommandPacingOptions kNoPacing{CommandPacingMode::no_delay};

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "it930x card check failed at line %d: %s\n", __LINE__, \
                         #condition);                                                      \
            return false;                                                                 \
        }                                                                                 \
    } while (false)

std::uint16_t checksum(const std::uint8_t* data, std::size_t size) noexcept
{
    std::uint16_t sum = 0U;
    std::size_t index = 0U;
    while (index + 1U < size) {
        sum = static_cast<std::uint16_t>(
            sum + static_cast<std::uint16_t>(
                      (static_cast<std::uint16_t>(data[index]) << 8U) | data[index + 1U]));
        index += 2U;
    }
    if (index < size) {
        sum = static_cast<std::uint16_t>(
            sum + static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[index]) << 8U));
    }
    return static_cast<std::uint16_t>(~sum);
}

std::vector<std::uint8_t> request_frame(std::uint16_t command, std::uint8_t sequence,
                                        ByteView payload)
{
    std::vector<std::uint8_t> frame(payload.size + 6U, 0U);
    frame[0] = static_cast<std::uint8_t>(frame.size() - 1U);
    frame[1] = static_cast<std::uint8_t>(command >> 8U);
    frame[2] = static_cast<std::uint8_t>(command);
    frame[3] = sequence;
    if (payload.size != 0U) {
        std::copy(payload.data, payload.data + payload.size, frame.begin() + 4U);
    }
    const auto sum = checksum(frame.data() + 1U, frame.size() - 3U);
    frame[frame.size() - 2U] = static_cast<std::uint8_t>(sum >> 8U);
    frame.back() = static_cast<std::uint8_t>(sum);
    return frame;
}

std::vector<std::uint8_t> response_frame(std::uint8_t sequence, ByteView payload)
{
    std::vector<std::uint8_t> frame(payload.size + 5U, 0U);
    frame[0] = static_cast<std::uint8_t>(frame.size() - 1U);
    frame[1] = sequence;
    if (payload.size != 0U) {
        std::copy(payload.data, payload.data + payload.size, frame.begin() + 3U);
    }
    const auto sum = checksum(frame.data() + 1U, frame.size() - 3U);
    frame[frame.size() - 2U] = static_cast<std::uint8_t>(sum >> 8U);
    frame.back() = static_cast<std::uint8_t>(sum);
    return frame;
}

void expect_command(MockTransport& transport, std::uint8_t& sequence,
                    std::uint16_t command, ByteView request_payload,
                    ByteView response_payload = ByteView{nullptr, 0U})
{
    const auto request = request_frame(command, sequence, request_payload);
    const auto response = response_frame(sequence, response_payload);
    transport.expect_bulk_write(kCommandOutEndpoint, ByteView{request.data(), request.size()});
    transport.expect_bulk_read(kCommandInEndpoint, ByteView{response.data(), response.size()});
    ++sequence;
}

std::vector<std::uint8_t> register_payload(std::uint32_t reg, ByteView values,
                                           std::size_t read_length = 0U)
{
    const std::uint8_t register_size = (reg & 0xff000000U) != 0U
                                           ? 4U
                                           : (reg & 0x00ff0000U) != 0U
                                                 ? 3U
                                                 : (reg & 0x0000ff00U) != 0U ? 2U : 1U;
    std::vector<std::uint8_t> payload(6U + values.size, 0U);
    payload[0] = static_cast<std::uint8_t>(read_length != 0U ? read_length : values.size);
    payload[1] = register_size;
    payload[2] = static_cast<std::uint8_t>(reg >> 24U);
    payload[3] = static_cast<std::uint8_t>(reg >> 16U);
    payload[4] = static_cast<std::uint8_t>(reg >> 8U);
    payload[5] = static_cast<std::uint8_t>(reg);
    if (values.size != 0U) {
        std::copy(values.data, values.data + values.size, payload.begin() + 6U);
    }
    return payload;
}

void expect_register_write(MockTransport& transport, std::uint8_t& sequence,
                           std::uint32_t reg, std::uint8_t value)
{
    const auto payload = register_payload(reg, ByteView{&value, 1U});
    expect_command(transport, sequence, 0x01U, ByteView{payload.data(), payload.size()});
}

void expect_register_read(MockTransport& transport, std::uint8_t& sequence,
                          std::uint32_t reg, std::uint8_t value)
{
    const auto payload = register_payload(reg, ByteView{nullptr, 0U}, 1U);
    expect_command(transport, sequence, 0x00U, ByteView{payload.data(), payload.size()},
                   ByteView{&value, 1U});
}

void expect_uart_read(MockTransport& transport, std::uint8_t& sequence,
                      const std::vector<std::uint8_t>& bytes)
{
    const auto length = static_cast<std::uint8_t>(bytes.size());
    expect_command(transport, sequence, 0x33U, ByteView{&length, 1U},
                   ByteView{bytes.data(), bytes.size()});
}

void expect_uart_write(MockTransport& transport, std::uint8_t& sequence,
                       const std::uint8_t* data, std::size_t size)
{
    std::vector<std::uint8_t> payload(size + 1U, 0U);
    payload[0] = static_cast<std::uint8_t>(size);
    std::copy(data, data + size, payload.begin() + 1U);
    expect_command(transport, sequence, 0x34U, ByteView{payload.data(), payload.size()});
}

class DelayRecorder final : public It930xCardDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        calls.push_back(milliseconds);
    }

    std::vector<std::uint32_t> calls;
};

bool test_initialize_detect_ready_and_baud_rates()
{
    MockTransport transport;
    std::uint8_t sequence = 0U;
    constexpr std::uint8_t uart_mode = 1U;
    expect_command(transport, sequence, 0x37U, ByteView{&uart_mode, 1U});
    expect_register_write(transport, sequence, 0xd8c8U, 0U);
    expect_register_write(transport, sequence, 0xd8c9U, 1U);
    expect_register_read(transport, sequence, 0xd8c6U, 0U);
    expect_register_read(transport, sequence, 0xd8c6U, 1U);
    expect_register_read(transport, sequence, 0x496aU, 1U);
    expect_register_read(transport, sequence, 0x496aU, 0U);
    constexpr std::array<std::uint8_t, 3U> encodings{0U, 1U, 0xefU};
    for (const auto encoding : encodings) {
        expect_command(transport, sequence, 0x35U, ByteView{&encoding, 1U});
    }

    It930xController controller(transport, kNoPacing);
    CHECK(controller.initialize_card_uart());
    const auto inserted = controller.detect_card();
    CHECK(inserted && inserted.value());
    const auto absent = controller.detect_card();
    CHECK(absent && !absent.value());
    const auto ready = controller.card_data_ready();
    CHECK(ready && ready.value());
    const auto not_ready = controller.card_data_ready();
    CHECK(not_ready && !not_ready.value());
    CHECK(controller.set_card_baud_rate(It930xCardBaudRate::baud_9600));
    CHECK(controller.set_card_baud_rate(It930xCardBaudRate::baud_19200));
    CHECK(controller.set_card_baud_rate(It930xCardBaudRate::baud_38400));
    const auto invalid = controller.set_card_baud_rate(
        static_cast<It930xCardBaudRate>(0xffU));
    CHECK(!invalid && invalid.error() == Error::INVALID_ARGUMENT);
    CHECK(transport.remaining_expectations() == 0U);
    return true;
}

bool test_reset_sequence()
{
    MockTransport transport;
    std::uint8_t sequence = 0U;
    expect_register_write(transport, sequence, 0xd8e4U, 1U);
    expect_register_write(transport, sequence, 0xd8e5U, 1U);
    expect_register_write(transport, sequence, 0xd8e3U, 0U);
    expect_register_write(transport, sequence, 0x7904U, 2U);
    constexpr std::uint8_t initial_baud = 0U;
    expect_command(transport, sequence, 0x35U, ByteView{&initial_baud, 1U});
    expect_register_write(transport, sequence, 0xd8e3U, 1U);

    DelayRecorder delay;
    It930xController controller(transport, kNoPacing);
    CHECK(controller.reset_card(delay));
    CHECK((delay.calls == std::vector<std::uint32_t>{5U}));
    CHECK(transport.remaining_expectations() == 0U);
    return true;
}

bool test_uart_read_chunking_and_bounds()
{
    std::array<std::uint8_t, 70U> expected{};
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        expected[index] = static_cast<std::uint8_t>(index + 1U);
    }

    MockTransport transport;
    std::uint8_t sequence = 0U;
    expect_register_read(transport, sequence, 0x496bU, 70U);
    expect_uart_read(transport, sequence,
                     std::vector<std::uint8_t>(expected.begin(), expected.begin() + 32U));
    expect_register_read(transport, sequence, 0x496bU, 38U);
    expect_uart_read(transport, sequence,
                     std::vector<std::uint8_t>(expected.begin() + 32U,
                                               expected.begin() + 64U));
    expect_register_read(transport, sequence, 0x496bU, 6U);
    expect_uart_read(transport, sequence,
                     std::vector<std::uint8_t>(expected.begin() + 64U, expected.end()));

    It930xController controller(transport, kNoPacing);
    std::array<std::uint8_t, 70U> actual{};
    const auto read = controller.read_card_data(MutableByteView{actual.data(), actual.size()});
    CHECK(read && read.value() == actual.size());
    CHECK(actual == expected);
    CHECK(transport.remaining_expectations() == 0U);

    CHECK(!controller.read_card_data(MutableByteView{nullptr, 1U}));
    CHECK(!controller.read_card_data(MutableByteView{actual.data(), 0U}));
    CHECK(!controller.read_card_data(MutableByteView{actual.data(), 256U}));
    return true;
}

bool test_uart_write_chunking_realsend_and_bounds()
{
    std::array<std::uint8_t, 100U> data{};
    for (std::size_t index = 0U; index < data.size(); ++index) {
        data[index] = static_cast<std::uint8_t>(index);
    }

    MockTransport transport;
    std::uint8_t sequence = 0U;
    expect_uart_write(transport, sequence, data.data(), 48U);
    expect_uart_write(transport, sequence, data.data() + 48U, 48U);
    expect_register_write(transport, sequence, 0x4965U, 1U);
    expect_uart_write(transport, sequence, data.data() + 96U, 4U);

    It930xController controller(transport, kNoPacing);
    CHECK(controller.write_card_data(ByteView{data.data(), data.size()}));
    CHECK(transport.remaining_expectations() == 0U);
    CHECK(!controller.write_card_data(ByteView{nullptr, 1U}));
    CHECK(!controller.write_card_data(ByteView{data.data(), 0U}));
    CHECK(!controller.write_card_data(ByteView{data.data(), 256U}));
    return true;
}

}  // namespace

bool run_it930x_card_tests()
{
    return test_initialize_detect_ready_and_baud_rates() && test_reset_sequence() &&
           test_uart_read_chunking_and_bounds() &&
           test_uart_write_chunking_realsend_and_bounds();
}

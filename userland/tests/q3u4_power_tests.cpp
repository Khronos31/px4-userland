// Modified/ported for px4-userland on 2026-09-03.
//
// Copyright (c) 2018-2021 nns779
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: driver/px4_device.c,
// winusb/src/DriverHost_PX4/px4_device.cpp.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only
#include "px4/it930x.h"
#include "px4/mock_transport.h"

#include "it930x_protocol.h"
#include "q3u4_power.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace px4::userland;

constexpr std::uint32_t kGpio7Output = 0xd8c3U;
constexpr std::uint32_t kGpio2Output = 0xd8b7U;
constexpr std::uint32_t kStreamGate = 0xda1dU;
constexpr std::size_t kPsbPurgeSize = 1024U;
constexpr CommandPacingOptions kFastPacing{CommandPacingMode::no_delay};

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "q3u4 power check failed at line %d: %s\\n", __LINE__, #condition); \
            return false;                                                              \
        }                                                                              \
    } while (false)

std::uint16_t checksum(const std::uint8_t* data, std::size_t size) noexcept
{
    std::uint16_t sum = 0U;
    std::size_t index = 0U;
    while (index + 1U < size) {
        sum = static_cast<std::uint16_t>(
            sum + static_cast<std::uint16_t>((static_cast<std::uint16_t>(data[index]) << 8U) |
                                              data[index + 1U]));
        index += 2U;
    }
    if (index < size) {
        sum = static_cast<std::uint16_t>(
            sum + static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[index]) << 8U));
    }
    return static_cast<std::uint16_t>(~sum);
}

std::vector<std::uint8_t> command_frame(std::uint16_t command, std::uint8_t sequence,
                                        ByteView payload)
{
    std::vector<std::uint8_t> frame(payload.size + 6U, 0U);
    frame[0] = static_cast<std::uint8_t>(frame.size() - 1U);
    frame[1] = static_cast<std::uint8_t>(command >> 8U);
    frame[2] = static_cast<std::uint8_t>(command);
    frame[3] = sequence;
    for (std::size_t index = 0U; index < payload.size; ++index) {
        frame[4U + index] = payload.data[index];
    }
    const auto sum = checksum(frame.data() + 1U, frame.size() - 3U);
    frame[frame.size() - 2U] = static_cast<std::uint8_t>(sum >> 8U);
    frame.back() = static_cast<std::uint8_t>(sum);
    return frame;
}

std::vector<std::uint8_t> register_payload(std::uint32_t reg, std::size_t length,
                                            std::uint8_t value = 0U)
{
    return {static_cast<std::uint8_t>(length), 2U,
            static_cast<std::uint8_t>(reg >> 24U), static_cast<std::uint8_t>(reg >> 16U),
            static_cast<std::uint8_t>(reg >> 8U), static_cast<std::uint8_t>(reg), value};
}

std::vector<std::uint8_t> empty_response(std::uint8_t sequence)
{
    std::array<std::uint8_t, 5U> response{4U, sequence, 0U, 0U, 0U};
    const auto sum = checksum(response.data() + 1U, response.size() - 3U);
    response[3] = static_cast<std::uint8_t>(sum >> 8U);
    response[4] = static_cast<std::uint8_t>(sum);
    return {response.begin(), response.end()};
}

void expect_write(MockTransport& transport, std::uint8_t& sequence,
                  std::uint32_t reg, std::uint8_t value,
                  MockOutcome outcome = MockOutcome::success)
{
    const auto payload = register_payload(reg, 1U, value);
    const auto request = command_frame(0x01U, sequence, ByteView{payload.data(), payload.size()});
    transport.expect_bulk_write(kCommandOutEndpoint,
                                ByteView{request.data(), request.size()}, outcome);
    if (outcome == MockOutcome::success || outcome == MockOutcome::short_transfer) {
        const auto response = empty_response(sequence);
        transport.expect_bulk_read(kCommandInEndpoint,
                                   ByteView{response.data(), response.size()});
    }
    ++sequence;
}

void expect_read(MockTransport& transport, std::uint8_t& sequence, std::uint32_t reg,
                 std::uint8_t value, MockOutcome outcome = MockOutcome::success)
{
    const auto payload = register_payload(reg, 1U);
    const auto request = command_frame(0x00U, sequence, ByteView{payload.data(), 6U});
    std::vector<std::uint8_t> framed_response{5U, sequence, 0U, value, 0U, 0U};
    const auto sum = checksum(framed_response.data() + 1U, framed_response.size() - 3U);
    framed_response[framed_response.size() - 2U] = static_cast<std::uint8_t>(sum >> 8U);
    framed_response.back() = static_cast<std::uint8_t>(sum);
    transport.expect_bulk_write(kCommandOutEndpoint,
                                ByteView{request.data(), request.size()});
    transport.expect_bulk_read(kCommandInEndpoint,
                               ByteView{framed_response.data(), framed_response.size()}, outcome);
    ++sequence;
}

void expect_rmw(MockTransport& transport, std::uint8_t& sequence, std::uint32_t reg,
                std::uint8_t current, std::uint8_t expected,
                MockOutcome read_outcome = MockOutcome::success,
                MockOutcome write_outcome = MockOutcome::success)
{
    expect_read(transport, sequence, reg, current, read_outcome);
    if (read_outcome == MockOutcome::success) {
        expect_write(transport, sequence, reg, expected, write_outcome);
    }
}

class DelayRecorder final : public Q3U4Delay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        sleeps.push_back(milliseconds);
    }

    std::vector<std::uint32_t> sleeps;
};

class FakeBackend final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool on, Q3U4Delay&) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        calls.push_back(on);
        if (next_failure < failures.size()) {
            const Error error = failures[next_failure++];
            return Result<void>::failure(error);
        }
        state = on;
        return Result<void>::success();
    }

    void fail_next(Error error)
    {
        std::lock_guard<std::mutex> lock(mutex);
        failures.push_back(error);
    }

    std::vector<bool> calls_copy() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return calls;
    }

    void clear_calls()
    {
        std::lock_guard<std::mutex> lock(mutex);
        calls.clear();
    }

    mutable std::mutex mutex;
    std::vector<bool> calls;
    std::vector<Error> failures;
    std::size_t next_failure = 0U;
    bool state = false;
};

bool check_snapshot(const Q3U4PowerCoordinator& coordinator, std::uint8_t receivers,
                    bool card, Q3U4PowerState dev1, Q3U4PowerState dev2)
{
    const auto snapshot = coordinator.snapshot();
    return snapshot.receiver_mask == receivers && snapshot.card_acquired == card &&
           snapshot.backend_state[0] == dev1 && snapshot.backend_state[1] == dev2;
}

}  // namespace

bool run_q3u4_power_tests()
{
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio7Output, 0U);
        expect_write(transport, sequence, kGpio2Output, 1U);
        expect_write(transport, sequence, kGpio2Output, 0U);
        expect_write(transport, sequence, kGpio7Output, 1U);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        CHECK(controller.set_q3u4_backend_power(true, delay));
        CHECK(controller.set_q3u4_backend_power(true, delay));
        CHECK(controller.set_q3u4_backend_power(false, delay));
        CHECK(controller.set_q3u4_backend_power(false, delay));
        CHECK((delay.sleeps == std::vector<std::uint32_t>{80U, 20U}));
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio7Output, 0U, MockOutcome::timeout);
        expect_write(transport, sequence, kGpio2Output, 0U);
        expect_write(transport, sequence, kGpio7Output, 1U);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        CHECK(!controller.set_q3u4_backend_power(true, delay));
        CHECK(delay.sleeps.empty());
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio7Output, 0U, MockOutcome::timeout);
        expect_write(transport, sequence, kGpio2Output, 0U, MockOutcome::disconnect);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        const auto result = controller.set_q3u4_backend_power(true, delay);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        // DISCONNECTED is terminal: do not issue the old recovery GPIO 7 write.
        CHECK(!controller.set_q3u4_backend_power(false, delay));
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio7Output, 0U, MockOutcome::disconnect);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        const auto result = controller.set_q3u4_backend_power(true, delay);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        CHECK(!controller.set_q3u4_backend_power(false, delay));
        // A disconnected controller rejects later cleanup without another write.
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio7Output, 0U);
        expect_write(transport, sequence, kGpio2Output, 1U, MockOutcome::timeout);
        expect_write(transport, sequence, kGpio2Output, 0U, MockOutcome::disconnect);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        const auto result = controller.set_q3u4_backend_power(true, delay);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        CHECK(!controller.set_q3u4_backend_power(false, delay));
        // A disconnect during backend cleanup overrides the recoverable timeout.
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio7Output, 0U);
        expect_write(transport, sequence, kGpio2Output, 1U, MockOutcome::timeout);
        expect_write(transport, sequence, kGpio2Output, 0U);
        expect_write(transport, sequence, kGpio7Output, 1U, MockOutcome::disconnect);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        const auto result = controller.set_q3u4_backend_power(true, delay);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        CHECK(!controller.set_q3u4_backend_power(false, delay));
        // A disconnect during reset cleanup also overrides the timeout.
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio7Output, 0U);
        expect_write(transport, sequence, kGpio2Output, 1U, MockOutcome::timeout);
        expect_write(transport, sequence, kGpio2Output, 0U);
        expect_write(transport, sequence, kGpio7Output, 1U);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        CHECK(!controller.set_q3u4_backend_power(true, delay));
        CHECK((delay.sleeps == std::vector<std::uint32_t>{80U}));
        CHECK(controller.set_q3u4_backend_power(false, delay));
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio2Output, 0U, MockOutcome::disconnect);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        const auto result = controller.set_q3u4_backend_power(false, delay);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        CHECK(!controller.set_q3u4_backend_power(false, delay));
        // Later release/cleanup must not retry either backend GPIO.
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio2Output, 0U);
        expect_write(transport, sequence, kGpio7Output, 1U, MockOutcome::protocol_error);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        CHECK(!controller.set_q3u4_backend_power(false, delay));
        CHECK(transport.remaining_expectations() == 0U);
    }

    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_write(transport, sequence, kGpio2Output, 0U, MockOutcome::timeout);
        expect_write(transport, sequence, kGpio7Output, 1U, MockOutcome::disconnect);
        DelayRecorder delay;
        It930xController controller(transport, CommandPacingOptions{CommandPacingMode::no_delay});
        const auto result = controller.set_q3u4_backend_power(false, delay);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        CHECK(!controller.set_q3u4_backend_power(true, delay));
        // DISCONNECTED on the final recovery step overrides the timeout.
        CHECK(transport.remaining_expectations() == 0U);
    }

    // A normal short completion is accepted and clears bit 0 afterward.
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_rmw(transport, sequence, kStreamGate, 0x80U, 0x81U);
        std::array<std::uint8_t, kPsbPurgeSize> purge{};
        transport.expect_bulk_read(kTsInEndpoint, ByteView{purge.data(), purge.size()},
                                   MockOutcome::success, 188U);
        expect_rmw(transport, sequence, kStreamGate, 0x81U, 0x80U);
        const Timeout timeout{417U};
        PsbPurgeObservation observation{true, Error::USB_IO, 99U};
        It930xController controller(transport, kFastPacing);
        CHECK(controller.purge_psb(timeout, &observation));
        CHECK(observation.read_attempted);
        CHECK(observation.completion_error == Error::OK);
        CHECK(observation.transferred == 188U);
        CHECK(transport.remaining_expectations() == 0U);
        CHECK(transport.operations().size() == 9U);
        CHECK(transport.bulk_read_sizes().size() == 5U);
        CHECK(transport.bulk_read_sizes()[2] == kPsbPurgeSize);
        CHECK(transport.timeouts().size() == 9U);
        CHECK(transport.timeouts()[4].milliseconds == timeout.milliseconds);
    }

    // A partial timeout is reported as a 512-byte completion and is accepted.
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_rmw(transport, sequence, kStreamGate, 0U, 1U);
        std::array<std::uint8_t, 512U> purge{};
        transport.expect_bulk_read(kTsInEndpoint, ByteView{purge.data(), purge.size()},
                                   MockOutcome::timeout, purge.size());
        expect_rmw(transport, sequence, kStreamGate, 1U, 0U);
        const Timeout timeout{509U};
        PsbPurgeObservation observation{};
        It930xController controller(transport, kFastPacing);
        CHECK(controller.purge_psb(timeout, &observation));
        CHECK(observation.read_attempted);
        CHECK(observation.completion_error == Error::TIMEOUT);
        CHECK(observation.transferred == 512U);
        CHECK(transport.remaining_expectations() == 0U);
        CHECK(transport.bulk_read_sizes()[2] == kPsbPurgeSize);
        CHECK(transport.timeouts()[4].milliseconds == timeout.milliseconds);
    }

    // A timeout with no transferred bytes is fatal, while gate cleanup still runs.
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_rmw(transport, sequence, kStreamGate, 0U, 1U);
        transport.expect_bulk_read(kTsInEndpoint, ByteView{nullptr, 0U}, MockOutcome::timeout);
        expect_rmw(transport, sequence, kStreamGate, 1U, 0U);
        PsbPurgeObservation observation{true, Error::USB_IO, 99U};
        const auto result = It930xController(transport, kFastPacing).purge_psb(
            Timeout{557U}, &observation);
        CHECK(!result && result.error() == Error::TIMEOUT);
        CHECK(observation.read_attempted);
        CHECK(observation.completion_error == Error::TIMEOUT);
        CHECK(observation.transferred == 0U);
        CHECK(transport.remaining_expectations() == 0U);
    }

    // A partial timeout other than the reference 512-byte completion is fatal.
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_rmw(transport, sequence, kStreamGate, 0U, 1U);
        std::array<std::uint8_t, kPsbPurgeSize> purge{};
        transport.expect_bulk_read(kTsInEndpoint, ByteView{purge.data(), purge.size()},
                                   MockOutcome::timeout, 188U);
        expect_rmw(transport, sequence, kStreamGate, 1U, 0U);
        PsbPurgeObservation observation{};
        const auto result = It930xController(transport, kFastPacing).purge_psb(
            Timeout{563U}, &observation);
        CHECK(!result && result.error() == Error::PROTOCOL_ERROR);
        CHECK(observation.read_attempted);
        CHECK(observation.completion_error == Error::TIMEOUT);
        CHECK(observation.transferred == 188U);
        CHECK(transport.remaining_expectations() == 0U);
    }

    // If setting the gate fails, cleanup is still attempted and no TS read is issued.
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_rmw(transport, sequence, kStreamGate, 0U, 1U,
                   MockOutcome::success, MockOutcome::usb_io);
        expect_rmw(transport, sequence, kStreamGate, 0U, 0U);
        It930xController controller(transport, kFastPacing);
        PsbPurgeObservation observation{true, Error::USB_IO, 99U};
        const auto result = controller.purge_psb(Timeout{601U}, &observation);
        CHECK(!result && result.error() == Error::USB_IO);
        CHECK(!observation.read_attempted);
        CHECK(observation.completion_error == Error::OK);
        CHECK(observation.transferred == 0U);
        CHECK(transport.remaining_expectations() == 0U);
        CHECK(transport.operations().size() == 7U);
        CHECK(transport.bulk_read_sizes().size() == 3U);
        for (const auto size : transport.bulk_read_sizes()) {
            CHECK(size != kPsbPurgeSize);
        }
    }

    // A non-timeout TS read failure is fatal, even after cleanup succeeds.
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_rmw(transport, sequence, kStreamGate, 0U, 1U);
        transport.expect_bulk_read(kTsInEndpoint, ByteView{nullptr, 0U}, MockOutcome::disconnect);
        expect_rmw(transport, sequence, kStreamGate, 1U, 0U);
        It930xController controller(transport, kFastPacing);
        PsbPurgeObservation observation{};
        const auto result = controller.purge_psb(Timeout{607U}, &observation);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        CHECK(observation.read_attempted);
        CHECK(observation.completion_error == Error::DISCONNECTED);
        CHECK(observation.transferred == 0U);
        CHECK(transport.remaining_expectations() == 0U);
    }

    // Cleanup failure is reported when all preceding operations succeeded.
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_rmw(transport, sequence, kStreamGate, 0U, 1U);
        std::array<std::uint8_t, kPsbPurgeSize> purge{};
        transport.expect_bulk_read(kTsInEndpoint, ByteView{purge.data(), purge.size()});
        expect_rmw(transport, sequence, kStreamGate, 1U, 0U,
                   MockOutcome::success, MockOutcome::usb_io);
        It930xController controller(transport, kFastPacing);
        const auto result = controller.purge_psb(Timeout{613U});
        CHECK(!result && result.error() == Error::USB_IO);
        CHECK(transport.remaining_expectations() == 0U);
    }

    // The primary TS read error wins over a later cleanup error.
    {
        MockTransport transport;
        std::uint8_t sequence = 0U;
        expect_rmw(transport, sequence, kStreamGate, 0U, 1U);
        transport.expect_bulk_read(kTsInEndpoint, ByteView{nullptr, 0U}, MockOutcome::protocol_error);
        expect_rmw(transport, sequence, kStreamGate, 1U, 0U,
                   MockOutcome::success, MockOutcome::usb_io);
        It930xController controller(transport, kFastPacing);
        const auto result = controller.purge_psb(Timeout{619U});
        CHECK(!result && result.error() == Error::PROTOCOL_ERROR);
        CHECK(transport.remaining_expectations() == 0U);
    }

    // The coupled matrix is independent of which bridge owns the receiver.
    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));

        CHECK(coordinator.acquire_receiver(0U));
        CHECK(check_snapshot(coordinator, 0x01U, false, Q3U4PowerState::on,
                             Q3U4PowerState::on));
        CHECK(dev1.calls_copy() == std::vector<bool>{true});
        CHECK(dev2.calls_copy() == std::vector<bool>{true});

        CHECK(coordinator.acquire_receiver(4U));
        CHECK(coordinator.release_receiver(0U));
        CHECK(check_snapshot(coordinator, 0x10U, false, Q3U4PowerState::on,
                             Q3U4PowerState::on));
        CHECK(coordinator.release_receiver(4U));
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));
        CHECK((dev1.calls_copy() == std::vector<bool>{true, false}));
        CHECK((dev2.calls_copy() == std::vector<bool>{true, false}));

        CHECK(coordinator.acquire_receiver(0U));
        CHECK(coordinator.acquire_receiver(4U));
        CHECK(coordinator.release_receiver(4U));
        CHECK(check_snapshot(coordinator, 0x01U, false, Q3U4PowerState::on,
                             Q3U4PowerState::on));
        CHECK(coordinator.release_receiver(0U));
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));

        CHECK(coordinator.acquire_card());
        CHECK(check_snapshot(coordinator, 0U, true, Q3U4PowerState::on,
                             Q3U4PowerState::off));
        CHECK((dev1.calls_copy() == std::vector<bool>{true, false, true, false, true}));
        CHECK((dev2.calls_copy() == std::vector<bool>{true, false, true, false}));
        CHECK(coordinator.acquire_receiver(7U));
        CHECK(check_snapshot(coordinator, 0x80U, true, Q3U4PowerState::on,
                             Q3U4PowerState::on));
        // Closing the card side first must leave the receiver's coupled
        // backend power untouched.
        CHECK(coordinator.release_card());
        CHECK(check_snapshot(coordinator, 0x80U, false, Q3U4PowerState::on,
                             Q3U4PowerState::on));
        CHECK(coordinator.release_receiver(7U));
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));
    }

    // Ownership validation is explicit and cannot underflow a counter.
    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        const auto invalid_acquire = coordinator.acquire_receiver(8U);
        CHECK(!invalid_acquire && invalid_acquire.error() == Error::INVALID_ARGUMENT);
        const auto missing_release = coordinator.release_receiver(0U);
        CHECK(!missing_release && missing_release.error() == Error::INVALID_ARGUMENT);
        CHECK(coordinator.acquire_receiver(0U));
        const auto duplicate_acquire = coordinator.acquire_receiver(0U);
        CHECK(!duplicate_acquire && duplicate_acquire.error() == Error::BUSY);
        const auto other_missing_release = coordinator.release_receiver(1U);
        CHECK(!other_missing_release && other_missing_release.error() == Error::INVALID_ARGUMENT);
        CHECK(coordinator.release_receiver(0U));
        const auto second_release = coordinator.release_receiver(0U);
        CHECK(!second_release && second_release.error() == Error::INVALID_ARGUMENT);
        CHECK(coordinator.acquire_card());
        const auto duplicate_card = coordinator.acquire_card();
        CHECK(!duplicate_card && duplicate_card.error() == Error::BUSY);
        CHECK(coordinator.release_card());
        const auto missing_card = coordinator.release_card();
        CHECK(!missing_card && missing_card.error() == Error::INVALID_ARGUMENT);
        const auto invalid_bridge = coordinator.disconnect(static_cast<Q3U4Bridge>(2U));
        CHECK(!invalid_bridge && invalid_bridge.error() == Error::INVALID_ARGUMENT);
    }

    // Acquisition is transactional: every failed transition rolls the
    // logical reference back, and a non-disconnect unknown state is retried.
    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        dev1.fail_next(Error::TIMEOUT);
        const auto result = coordinator.acquire_receiver(0U);
        CHECK(!result && result.error() == Error::TIMEOUT);
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));
        CHECK((dev1.calls_copy() == std::vector<bool>{true, false}));
        CHECK(dev2.calls_copy().empty());
    }

    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        dev2.fail_next(Error::USB_IO);
        const auto result = coordinator.acquire_receiver(4U);
        CHECK(!result && result.error() == Error::USB_IO);
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));
        CHECK(dev1.calls_copy().empty());
        CHECK((dev2.calls_copy() == std::vector<bool>{true, false}));
    }

    // Releases always remove the logical owner, even when physical shutdown
    // fails; the affected bridge remains unknown for a later retry.
    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        CHECK(coordinator.acquire_receiver(0U));
        dev1.clear_calls();
        dev2.clear_calls();
        dev1.fail_next(Error::USB_IO);
        const auto result = coordinator.release_receiver(0U);
        CHECK(!result && result.error() == Error::USB_IO);
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::unknown,
                             Q3U4PowerState::on));
        CHECK(dev1.calls_copy() == std::vector<bool>{false});
        CHECK(dev2.calls_copy().empty());
        CHECK(coordinator.reconcile());
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));
    }

    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        CHECK(coordinator.acquire_receiver(0U));
        dev1.clear_calls();
        dev2.clear_calls();
        dev2.fail_next(Error::USB_IO);
        const auto result = coordinator.release_receiver(0U);
        CHECK(!result && result.error() == Error::USB_IO);
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::unknown));
        CHECK(dev1.calls_copy() == std::vector<bool>{false});
        CHECK(dev2.calls_copy() == std::vector<bool>{false});
        CHECK(coordinator.reconcile());
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));
    }

    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        dev1.fail_next(Error::TIMEOUT);
        const auto acquire = coordinator.acquire_card();
        CHECK(!acquire && acquire.error() == Error::TIMEOUT);
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));
        CHECK((dev1.calls_copy() == std::vector<bool>{true, false}));
        CHECK(dev2.calls_copy().empty());

        CHECK(coordinator.acquire_card());
        dev1.clear_calls();
        dev1.fail_next(Error::USB_IO);
        const auto release = coordinator.release_card();
        CHECK(!release && release.error() == Error::USB_IO);
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::unknown,
                             Q3U4PowerState::off));
        CHECK(coordinator.reconcile());
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::off));
    }

    // A disconnected bridge is terminal. Disconnect clears all logical
    // references and the surviving bridge is not touched during that path.
    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        CHECK(coordinator.acquire_receiver(0U));
        CHECK(coordinator.acquire_card());
        dev1.clear_calls();
        dev2.clear_calls();
        CHECK(coordinator.disconnect(Q3U4Bridge::dev2));
        CHECK(dev1.calls_copy().empty());
        CHECK(dev2.calls_copy().empty());
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::unknown,
                             Q3U4PowerState::disconnected));
        CHECK(!coordinator.release_receiver(0U));
        CHECK(!coordinator.release_card());
        const auto result = coordinator.acquire_receiver(4U);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        const auto card_retry = coordinator.acquire_card();
        CHECK(!card_retry && card_retry.error() == Error::DISCONNECTED);
        const auto reconcile = coordinator.reconcile();
        CHECK(!reconcile && reconcile.error() == Error::DISCONNECTED);
        CHECK(dev1.calls_copy().empty());
        CHECK(dev2.calls_copy().empty());
    }

    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        CHECK(coordinator.acquire_receiver(0U));
        dev1.clear_calls();
        dev2.clear_calls();
        CHECK(coordinator.disconnect(Q3U4Bridge::dev1));
        CHECK(dev1.calls_copy().empty());
        CHECK(dev2.calls_copy().empty());
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::disconnected,
                             Q3U4PowerState::unknown));
        const auto result = coordinator.acquire_card();
        CHECK(!result && result.error() == Error::DISCONNECTED);
        CHECK(dev1.calls_copy().empty());
    }

    // An Error::DISCONNECTED returned by a backend has the same terminal
    // effect as the explicit transport-loss path.
    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        dev2.fail_next(Error::DISCONNECTED);
        const auto result = coordinator.acquire_receiver(4U);
        CHECK(!result && result.error() == Error::DISCONNECTED);
        CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                             Q3U4PowerState::disconnected));
        CHECK(dev1.calls_copy().empty());
        CHECK((dev2.calls_copy() == std::vector<bool>{true}));

        const auto retry = coordinator.acquire_receiver(0U);
        CHECK(!retry && retry.error() == Error::DISCONNECTED);
        CHECK(dev1.calls_copy().empty());
        CHECK((dev2.calls_copy() == std::vector<bool>{true}));
    }

    // All eight IDs can be acquired and released concurrently. The barriers
    // make each phase deterministic while exercising the coordinator mutex.
    {
        FakeBackend dev1;
        FakeBackend dev2;
        DelayRecorder delay;
        Q3U4PowerCoordinator coordinator(dev1, dev2, delay);
        for (std::size_t round = 0U; round < 32U; ++round) {
            std::atomic<unsigned> ready{0U};
            std::atomic<bool> go{false};
            std::array<Error, 8U> acquire_errors{};
            std::array<std::thread, 8U> workers;
            for (std::size_t id = 0U; id < workers.size(); ++id) {
                workers[id] = std::thread([&, id]() {
                    ready.fetch_add(1U);
                    while (!go.load()) std::this_thread::yield();
                    acquire_errors[id] = coordinator.acquire_receiver(
                        static_cast<std::uint8_t>(id)).error();
                });
            }
            while (ready.load() != workers.size()) std::this_thread::yield();
            go.store(true);
            for (std::thread& worker : workers) worker.join();
            for (const Error error : acquire_errors) CHECK(error == Error::OK);
            CHECK(check_snapshot(coordinator, 0xffU, false, Q3U4PowerState::on,
                                 Q3U4PowerState::on));

            ready.store(0U);
            go.store(false);
            std::array<Error, 8U> release_errors{};
            for (std::size_t id = 0U; id < workers.size(); ++id) {
                workers[id] = std::thread([&, id]() {
                    ready.fetch_add(1U);
                    while (!go.load()) std::this_thread::yield();
                    release_errors[id] = coordinator.release_receiver(
                        static_cast<std::uint8_t>(id)).error();
                });
            }
            while (ready.load() != workers.size()) std::this_thread::yield();
            go.store(true);
            for (std::thread& worker : workers) worker.join();
            for (const Error error : release_errors) CHECK(error == Error::OK);
            CHECK(check_snapshot(coordinator, 0U, false, Q3U4PowerState::off,
                                 Q3U4PowerState::off));
        }
    }

    return true;
}

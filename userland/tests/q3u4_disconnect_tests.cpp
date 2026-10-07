// SPDX-License-Identifier: GPL-2.0-only
//
// Issue #25: a Q3U4 receiver disconnect must reach the shared backend power
// coordinator and the frontend bank state, so no later cleanup issues I/O to
// the lost bridge. These tests cover the unit contract for the metadata-only
// notification, the alive-bridge/card release contract, the W3U4 assembly,
// and the end-to-end tuner-service path.

#include "q3u4_frontend.h"
#include "q3u4_lnb_power.h"
#include "q3u4_tuner_backend.h"

#include "px4/tuner_service.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

namespace {

using namespace px4::userland;
using namespace px4::userland::ipc;

#define DISCONNECT_CHECK(condition)                                                    \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::fprintf(stderr, "q3u4 disconnect check failed at line %d: %s\n",    \
                         __LINE__, #condition);                                       \
            return false;                                                             \
        }                                                                             \
    } while (false)

// Records every bridge I2C batch and can simulate a transport loss on the
// next transaction. The read responses mirror the accepted frontend contract
// so a full open/tune/capture sequence is observable.
class Bridge final : public BridgeI2cMaster {
public:
    struct Operation final {
        std::uint8_t address;
        std::vector<std::uint8_t> data;
    };

    Result<void> request(BridgeI2cRequest* requests, std::size_t count) noexcept override
    {
        ++requests_seen;
        batches.emplace_back();
        for (std::size_t i = 0U; i < count; ++i) {
            batches.back().push_back(
                {requests[i].address,
                 requests[i].type == BridgeI2cRequestType::write
                     ? std::vector<std::uint8_t>(requests[i].write_data.data,
                                                 requests[i].write_data.data +
                                                     requests[i].write_data.size)
                     : std::vector<std::uint8_t>{}});
            if (requests[i].type == BridgeI2cRequestType::read) {
                for (std::size_t j = 0U; j < requests[i].read_data.size; ++j)
                    requests[i].read_data.data[j] = 0U;
                if (requests[i].read_data.size == 1U && count == 3U)
                    requests[i].read_data.data[0] = 0x98U;
                if (requests[i].read_data.size == 1U && count == 2U &&
                    requests[0].write_data.size == 1U &&
                    requests[0].write_data.data[0] == 0xb0U)
                    requests[i].read_data.data[0] = 0x0fU;
                if (requests[i].read_data.size == 3U)
                    requests[i].read_data.data[2] = 0x02U;
                if (requests[i].read_data.size == 4U)
                    requests[i].read_data.data[3] = 0x70U;
                batches.back()[i].data.assign(
                    requests[i].read_data.data,
                    requests[i].read_data.data + requests[i].read_data.size);
            }
        }
        if (fail_all) return Result<void>::failure(failure);
        return Result<void>::success();
    }

    std::size_t requests_seen = 0U;
    std::vector<std::vector<Operation>> batches;
    bool fail_all = false;
    Error failure = Error::USB_IO;
};

class Delay final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        sleeps.push_back(milliseconds);
    }

    std::vector<std::uint32_t> sleeps;
};

class BackendPower final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool on, Q3U4Delay&) noexcept override
    {
        states.push_back(on);
        return Result<void>::success();
    }

    std::vector<bool> states;
};

class AbsentBridgePower final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool, Q3U4Delay&) noexcept override
    {
        return Result<void>::success();
    }
};

class LnbPower final : public Q3U4LnbPower {
public:
    Result<void> set_lnb_power(bool on) noexcept override
    {
        states.push_back(on);
        return Result<void>::success();
    }

    std::vector<bool> states;
};

class Clock final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override { return now; }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        sleeps.push_back(milliseconds);
        now += milliseconds;
    }

    std::uint64_t now = 0U;
    std::vector<std::uint32_t> sleeps;
};

class Nonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, kNonceLength>> generate() noexcept override
    {
        std::array<std::uint8_t, kNonceLength> value{};
        value[0] = next++;
        return Result<std::array<std::uint8_t, kNonceLength>>::success(value);
    }

    std::uint8_t next = 1U;
};

class StreamControl final : public TunerStreamControl {
public:
    Result<void> attach(const TunerAttachment& value) noexcept override
    {
        attachments.push_back(value);
        return Result<void>::success();
    }

    Result<void> detach(const TunerAttachment& value) noexcept override
    {
        detached.push_back(value);
        return Result<void>::success();
    }

    std::vector<TunerAttachment> attachments;
    std::vector<TunerAttachment> detached;
};

TuneRequestPayload terrestrial(std::uint64_t lease_id,
                               std::uint64_t frequency = 527143U) noexcept
{
    return TuneRequestPayload{lease_id, System::ISDB_T, frequency, 0xffffU,
                              0xffffU, 6000000U, 0U, 5000U};
}

// W3U4 has a single bridge and reports four receivers. The production assembly
// wraps the same Q3U4FrontendTunerBackend; this mirrors that wrapper.
class W3U4Backend final : public TunerServiceBackend {
public:
    W3U4Backend(Q3U4FrontendEnclosure& enclosure,
                Q3U4LnbPowerCoordinator& lnb_power) noexcept
        : inner_(enclosure, lnb_power)
    {
    }

    std::uint8_t receiver_count() const noexcept override
    {
        return kW3U4ReceiverCount;
    }

    bool receiver_supports(std::uint8_t receiver,
                           System system) const noexcept override
    {
        if (receiver >= kW3U4ReceiverCount) return false;
        const bool satellite = receiver < 2U;
        return system == (satellite ? System::ISDB_S : System::ISDB_T);
    }

    bool requires_terrestrial_lock_settle() const noexcept override { return true; }

    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        return inner_.open_receiver(receiver);
    }
    Result<void> tune_terrestrial(std::uint8_t receiver, std::uint32_t frequency_khz,
                                  std::uint32_t timeout_ms) noexcept override
    {
        return inner_.tune_terrestrial(receiver, frequency_khz, timeout_ms);
    }
    Result<void> tune_satellite(std::uint8_t receiver, std::uint32_t frequency_khz,
                                std::uint32_t timeout_ms) noexcept override
    {
        return inner_.tune_satellite(receiver, frequency_khz, timeout_ms);
    }
    Result<bool> is_locked(std::uint8_t receiver, System system) noexcept override
    {
        return inner_.is_locked(receiver, system);
    }
    Result<void> select_satellite_slot(std::uint8_t receiver, std::uint8_t slot,
                                       std::uint32_t timeout_ms) noexcept override
    {
        return inner_.select_satellite_slot(receiver, slot, timeout_ms);
    }
    Result<void> select_satellite_tsid(std::uint8_t receiver, std::uint16_t tsid,
                                       std::uint32_t timeout_ms) noexcept override
    {
        return inner_.select_satellite_tsid(receiver, tsid, timeout_ms);
    }
    Result<void> close_receiver(std::uint8_t receiver) noexcept override
    {
        return inner_.close_receiver(receiver);
    }
    Result<void> start_capture(std::uint8_t receiver, System system) noexcept override
    {
        return inner_.start_capture(receiver, system);
    }
    Result<void> stop_capture(std::uint8_t receiver, System system) noexcept override
    {
        return inner_.stop_capture(receiver, system);
    }
    Result<void> begin_tune_power(std::uint8_t receiver, System system,
                                  std::uint8_t lnb_voltage) noexcept override
    {
        return inner_.begin_tune_power(receiver, system, lnb_voltage);
    }
    Result<void> commit_tune_power(std::uint8_t receiver) noexcept override
    {
        return inner_.commit_tune_power(receiver);
    }
    Result<void> rollback_tune_power(std::uint8_t receiver) noexcept override
    {
        return inner_.rollback_tune_power(receiver);
    }
    void mark_receiver_disconnected(std::uint8_t receiver) noexcept override
    {
        inner_.mark_receiver_disconnected(receiver);
    }
    Result<void> shutdown() noexcept override { return inner_.shutdown(); }

private:
    Q3U4FrontendTunerBackend inner_;
};

// Unit contract: notification is idempotent, metadata-only, and every later
// frontend operation on the lost bridge fails with DISCONNECTED and no I/O.
bool test_mark_idempotent_and_disconnected_operations()
{
    Bridge dev1;
    Bridge dev2;
    BackendPower dev1_power;
    BackendPower dev2_power;
    LnbPower dev1_lnb;
    LnbPower dev2_lnb;
    Delay delay;
    Q3U4FrontendEnclosure enclosure(dev1, dev2, dev1_power, dev2_power, delay);
    Q3U4LnbPowerCoordinator lnb(dev1_lnb, dev2_lnb, true);
    Q3U4FrontendTunerBackend backend(enclosure, lnb);

    DISCONNECT_CHECK(enclosure.open_terrestrial(2U));
    DISCONNECT_CHECK(enclosure.tune_terrestrial(2U, 527143U));
    DISCONNECT_CHECK(enclosure.start_terrestrial_capture(2U));
    DISCONNECT_CHECK(enclosure.open_terrestrial(6U));

    const std::size_t requests_before = dev1.requests_seen;
    const std::size_t power_before = dev1_power.states.size();

    backend.mark_receiver_disconnected(2U);
    backend.mark_receiver_disconnected(2U);

    // Notification itself is metadata-only.
    DISCONNECT_CHECK(dev1.requests_seen == requests_before);
    DISCONNECT_CHECK(dev1_power.states.size() == power_before);
    DISCONNECT_CHECK(enclosure.power_snapshot().backend_state[0] ==
                     Q3U4PowerState::disconnected);
    DISCONNECT_CHECK(enclosure.power_snapshot().receiver_mask == 0U);
    DISCONNECT_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::closed);

    // Every operation on the disconnected bank answers DISCONNECTED with no I/O.
    DISCONNECT_CHECK(enclosure.open_terrestrial(3U).error() == Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.open_satellite(0U).error() == Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.tune_terrestrial(2U, 527143U).error() ==
                     Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.select_satellite_slot(0U, 0U).error() ==
                     Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.select_satellite_tsid(0U, 0U).error() ==
                     Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.start_terrestrial_capture(2U).error() ==
                     Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.stop_terrestrial_capture(2U).error() ==
                     Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.is_terrestrial_locked(2U).error() ==
                     Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.is_satellite_locked(0U).error() ==
                     Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.bank(Q3U4Bridge::dev1)
                         .terrestrial_loop_through(2U)
                         .error() == Error::DISCONNECTED);
    const auto closed = enclosure.close_receiver(2U);
    DISCONNECT_CHECK(!closed && closed.error() == Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::closed);
    DISCONNECT_CHECK(dev1.requests_seen == requests_before);
    DISCONNECT_CHECK(dev1_power.states.size() == power_before);

    // The surviving bridge keeps its logical state and its release resolves
    // without turning the already-cleared shared reference into an error.
    DISCONNECT_CHECK(enclosure.close_receiver(6U));
    DISCONNECT_CHECK(enclosure.receiver_state(6U) == Q3U4ReceiverState::closed);
    return true;
}

// A release or card release whose owned reference was already cleared by the
// disconnect is success without a bus write; acquisition stays refused.
bool test_alive_bridge_and_card_release_after_disconnect()
{
    Bridge dev1;
    Bridge dev2;
    BackendPower dev1_power;
    BackendPower dev2_power;
    LnbPower dev1_lnb;
    LnbPower dev2_lnb;
    Delay delay;
    Q3U4FrontendEnclosure enclosure(dev1, dev2, dev1_power, dev2_power, delay);
    Q3U4LnbPowerCoordinator lnb(dev1_lnb, dev2_lnb, true);
    Q3U4FrontendTunerBackend backend(enclosure, lnb);

    DISCONNECT_CHECK(enclosure.acquire_card());
    DISCONNECT_CHECK(enclosure.open_terrestrial(6U));

    backend.mark_receiver_disconnected(2U);

    const std::size_t requests_before = dev1.requests_seen;
    const std::size_t power_before = dev1_power.states.size();
    DISCONNECT_CHECK(enclosure.release_card());
    DISCONNECT_CHECK(dev1.requests_seen == requests_before);
    DISCONNECT_CHECK(dev1_power.states.size() == power_before);

    DISCONNECT_CHECK(enclosure.close_receiver(6U));
    DISCONNECT_CHECK(enclosure.acquire_card().error() == Error::DISCONNECTED);
    DISCONNECT_CHECK(enclosure.open_terrestrial(6U).error() == Error::DISCONNECTED);
    return true;
}

// End-to-end path through TunerService: capture, observe a transport loss on
// one bridge, release both leases, then shut down. No I/O may reach the lost
// bridge after the loss is observed.
bool test_tuner_service_q3u4_disconnect_path()
{
    Bridge dev1;
    Bridge dev2;
    BackendPower dev1_power;
    BackendPower dev2_power;
    LnbPower dev1_lnb;
    LnbPower dev2_lnb;
    Delay delay;

    std::size_t dev1_after = 0U;
    std::size_t dev1_power_after = 0U;
    std::size_t dev2_before = 0U;
    {
        Q3U4FrontendEnclosure enclosure(dev1, dev2, dev1_power, dev2_power, delay);
        Q3U4LnbPowerCoordinator lnb(dev1_lnb, dev2_lnb, true);
        Q3U4FrontendTunerBackend backend(enclosure, lnb);
        Clock clock;
        Nonce nonce;
        StreamControl stream;
        TunerService service(backend, nonce, clock, nullptr, nullptr, &stream);

        const auto first = service.acquire(1U, 2U);
        DISCONNECT_CHECK(first);
        DISCONNECT_CHECK(service.tune(1U, terrestrial(first.value().lease_id)));
        DISCONNECT_CHECK(service.start_stream(1U, first.value().lease_id));
        DISCONNECT_CHECK(
            service.attach_stream(first.value().lease_id, first.value().nonce));

        const auto second = service.acquire(2U, 6U);
        DISCONNECT_CHECK(second);
        DISCONNECT_CHECK(service.tune(2U, terrestrial(second.value().lease_id)));
        DISCONNECT_CHECK(service.start_stream(2U, second.value().lease_id));
        DISCONNECT_CHECK(
            service.attach_stream(second.value().lease_id, second.value().nonce));

        dev2_before = dev2.requests_seen;
        dev1.fail_all = true;
        dev1.failure = Error::DISCONNECTED;

        // The first failing transaction observes the loss and notifies the
        // shared power/frontend layer before any later cleanup runs.
        const auto stopped = service.stop_stream(1U, first.value().lease_id);
        DISCONNECT_CHECK(!stopped && stopped.error() == Error::DISCONNECTED);

        dev1_after = dev1.requests_seen;
        dev1_power_after = dev1_power.states.size();

        const auto snapshot = enclosure.power_snapshot();
        DISCONNECT_CHECK(snapshot.backend_state[0] == Q3U4PowerState::disconnected);
        DISCONNECT_CHECK(snapshot.receiver_mask == 0U);
        DISCONNECT_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::closed);

        const auto released_first = service.release(1U, first.value().lease_id);
        DISCONNECT_CHECK(!released_first &&
                         released_first.error() == Error::DISCONNECTED);
        // The surviving bridge's release resolves without an error.
        DISCONNECT_CHECK(service.release(2U, second.value().lease_id));

        DISCONNECT_CHECK(enclosure.open_terrestrial(3U).error() ==
                         Error::DISCONNECTED);
        DISCONNECT_CHECK(enclosure.acquire_card().error() == Error::DISCONNECTED);

        (void)service.shutdown();
    }

    // Destruction of the enclosure must not write to the lost bridge either.
    DISCONNECT_CHECK(dev1.requests_seen == dev1_after);
    DISCONNECT_CHECK(dev1_power.states.size() == dev1_power_after);
    DISCONNECT_CHECK(dev2.requests_seen > dev2_before);
    return true;
}

// The single-bridge W3U4 assembly (absent second bridge) follows the same path.
bool test_tuner_service_w3u4_disconnect_path()
{
    Bridge bridge;
    BackendPower power;
    AbsentBridgePower absent_power;
    LnbPower lnb_backend;
    LnbPower unused_lnb;
    Delay delay;

    std::size_t after = 0U;
    std::size_t power_after = 0U;
    {
        Q3U4FrontendEnclosure enclosure(bridge, bridge, power, absent_power, delay);
        Q3U4LnbPowerCoordinator lnb(lnb_backend, unused_lnb, true);
        W3U4Backend backend(enclosure, lnb);
        Clock clock;
        Nonce nonce;
        StreamControl stream;
        TunerService service(backend, nonce, clock, nullptr, nullptr, &stream);

        const auto lease = service.acquire(1U, 2U);
        DISCONNECT_CHECK(lease);
        DISCONNECT_CHECK(service.tune(1U, terrestrial(lease.value().lease_id)));
        DISCONNECT_CHECK(service.start_stream(1U, lease.value().lease_id));
        DISCONNECT_CHECK(
            service.attach_stream(lease.value().lease_id, lease.value().nonce));

        bridge.fail_all = true;
        bridge.failure = Error::DISCONNECTED;
        const auto stopped = service.stop_stream(1U, lease.value().lease_id);
        DISCONNECT_CHECK(!stopped && stopped.error() == Error::DISCONNECTED);

        after = bridge.requests_seen;
        power_after = power.states.size();

        DISCONNECT_CHECK(enclosure.power_snapshot().backend_state[0] ==
                         Q3U4PowerState::disconnected);
        DISCONNECT_CHECK(enclosure.power_snapshot().receiver_mask == 0U);
        DISCONNECT_CHECK(enclosure.receiver_state(2U) == Q3U4ReceiverState::closed);
        DISCONNECT_CHECK(enclosure.open_terrestrial(3U).error() ==
                         Error::DISCONNECTED);

        const auto released = service.release(1U, lease.value().lease_id);
        DISCONNECT_CHECK(!released && released.error() == Error::DISCONNECTED);
        (void)service.shutdown();
    }

    DISCONNECT_CHECK(bridge.requests_seen == after);
    DISCONNECT_CHECK(power.states.size() == power_after);
    return true;
}

}  // namespace

int main()
{
    if (!test_mark_idempotent_and_disconnected_operations()) return 1;
    if (!test_alive_bridge_and_card_release_after_disconnect()) return 2;
    if (!test_tuner_service_q3u4_disconnect_path()) return 3;
    if (!test_tuner_service_w3u4_disconnect_path()) return 4;
    std::printf("q3u4 disconnect tests passed\n");
    return 0;
}

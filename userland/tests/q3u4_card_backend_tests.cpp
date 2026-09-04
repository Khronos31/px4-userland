// SPDX-License-Identifier: GPL-2.0-only
#include "q3u4_card_backend.h"

#include "px4/mock_transport.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <vector>

namespace {

using namespace px4::userland;

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "q3u4 card backend check failed at line %d: %s\n", \
                         __LINE__, #condition);                                       \
            return false;                                                              \
        }                                                                              \
    } while (false)

class Bridge final : public BridgeI2cMaster {
public:
    Result<void> request(BridgeI2cRequest*, std::size_t) noexcept override
    {
        return Result<void>::success();
    }
};

class Delay final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t) noexcept override {}
};

class Power final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool on, Q3U4Delay&) noexcept override
    {
        calls.push_back(on);
        if (!outcomes.empty()) {
            const Error error = outcomes.front();
            outcomes.pop_front();
            if (error != Error::OK) return Result<void>::failure(error);
        }
        return Result<void>::success();
    }

    std::vector<bool> calls;
    std::deque<Error> outcomes;
};

Q3U4FrontendEnclosure make_enclosure(Bridge& dev1_bridge, Bridge& dev2_bridge,
                                     Power& dev1_power, Power& dev2_power,
                                     Delay& delay)
{
    return Q3U4FrontendEnclosure(dev1_bridge, dev2_bridge, dev1_power,
                                 dev2_power, delay);
}

bool test_shared_card_matrix()
{
    Bridge dev1_bridge;
    Bridge dev2_bridge;
    Power dev1_power;
    Power dev2_power;
    Delay delay;
    auto enclosure = make_enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                    dev2_power, delay);
    MockTransport transport;
    It930xController controller(transport,
                                CommandPacingOptions{CommandPacingMode::no_delay});
    Q3U4CardBackend backend(controller, enclosure);

    CHECK(backend.set_power(true));
    CHECK(enclosure.power_snapshot().card_acquired);
    CHECK((dev1_power.calls == std::vector<bool>{true}));
    CHECK(dev2_power.calls.empty());
    CHECK(backend.set_power(true));
    CHECK(backend.set_power(false));
    CHECK(!enclosure.power_snapshot().card_acquired);
    CHECK((dev1_power.calls == std::vector<bool>{true, false}));
    CHECK(backend.set_power(false));
    return true;
}

bool test_failed_acquire_cleanup_preserves_error()
{
    Bridge dev1_bridge;
    Bridge dev2_bridge;
    Power dev1_power;
    Power dev2_power;
    Delay delay;
    auto enclosure = make_enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                    dev2_power, delay);
    MockTransport transport;
    It930xController controller(transport,
                                CommandPacingOptions{CommandPacingMode::no_delay});
    Q3U4CardBackend backend(controller, enclosure);
    dev1_power.outcomes.push_back(Error::TIMEOUT);

    const auto acquire = backend.set_power(true);
    CHECK(!acquire && acquire.error() == Error::TIMEOUT);
    // CardService invokes false cleanup after a failed true. It must not turn
    // that original TIMEOUT into INVALID_ARGUMENT at the adapter boundary.
    const auto cleanup = backend.set_power(false);
    CHECK(cleanup);
    CHECK(!enclosure.power_snapshot().card_acquired);
    CHECK((dev1_power.calls == std::vector<bool>{true, false}));
    CHECK(dev2_power.calls.empty());
    return true;
}

bool test_failed_acquire_rollback_and_cleanup_reconcile()
{
    Bridge dev1_bridge;
    Bridge dev2_bridge;
    Power dev1_power;
    Power dev2_power;
    Delay delay;
    auto enclosure = make_enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                    dev2_power, delay);
    MockTransport transport;
    It930xController controller(transport,
                                CommandPacingOptions{CommandPacingMode::no_delay});
    Q3U4CardBackend backend(controller, enclosure);

    // The acquire ON fails, then the coordinator's rollback OFF fails too.
    // CardService-equivalent false cleanup must reconcile that unknown state.
    dev1_power.outcomes.push_back(Error::TIMEOUT);
    dev1_power.outcomes.push_back(Error::USB_IO);
    const auto acquire = backend.set_power(true);
    CHECK(!acquire && acquire.error() == Error::TIMEOUT);
    CHECK(!enclosure.power_snapshot().card_acquired);
    CHECK(enclosure.power_snapshot().backend_state[0] == Q3U4PowerState::unknown);

    const auto cleanup = backend.set_power(false);
    CHECK(cleanup);
    const auto snapshot = enclosure.power_snapshot();
    CHECK(!snapshot.card_acquired);
    CHECK(snapshot.backend_state[0] == Q3U4PowerState::off);
    CHECK(snapshot.backend_state[1] == Q3U4PowerState::off);
    CHECK((dev1_power.calls == std::vector<bool>{true, false, false}));
    CHECK(dev2_power.calls.empty());
    return true;
}

bool test_failed_release_discards_adapter_reference()
{
    Bridge dev1_bridge;
    Bridge dev2_bridge;
    Power dev1_power;
    Power dev2_power;
    Delay delay;
    auto enclosure = make_enclosure(dev1_bridge, dev2_bridge, dev1_power,
                                    dev2_power, delay);
    MockTransport transport;
    It930xController controller(transport,
                                CommandPacingOptions{CommandPacingMode::no_delay});
    Q3U4CardBackend backend(controller, enclosure);
    CHECK(backend.set_power(true));
    dev1_power.outcomes.push_back(Error::USB_IO);
    const auto release = backend.set_power(false);
    CHECK(!release && release.error() == Error::USB_IO);
    CHECK(!enclosure.power_snapshot().card_acquired);
    // The second cleanup is not a duplicate coordinator release.
    CHECK(backend.set_power(false));
    return true;
}

}  // namespace

bool run_q3u4_card_backend_tests()
{
    return test_shared_card_matrix() &&
           test_failed_acquire_cleanup_preserves_error() &&
           test_failed_acquire_rollback_and_cleanup_reconcile() &&
           test_failed_release_discards_adapter_reference();
}

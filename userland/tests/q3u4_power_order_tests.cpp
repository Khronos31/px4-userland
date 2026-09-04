// SPDX-License-Identifier: GPL-2.0-only
#include "q3u4_power.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <utility>
#include <vector>

using namespace px4::userland;

namespace {

struct Call final {
    Q3U4Bridge bridge;
    bool on;
};

class Delay final : public Q3U4Delay {
public:
    void sleep_ms(std::uint32_t) noexcept override {}
};

class Backend final : public Q3U4BackendPower {
public:
    Backend(Q3U4Bridge bridge, std::vector<Call>& calls) noexcept
        : bridge_(bridge), calls_(calls)
    {
    }

    Result<void> set_backend_power(bool on, Q3U4Delay&) noexcept override
    {
        calls_.push_back(Call{bridge_, on});
        if (next_error_ != Error::OK) {
            const Error error = next_error_;
            next_error_ = Error::OK;
            return Result<void>::failure(error);
        }
        return Result<void>::success();
    }

    void fail_next(Error error) noexcept { next_error_ = error; }

private:
    Q3U4Bridge bridge_;
    std::vector<Call>& calls_;
    Error next_error_ = Error::OK;
};

bool check(const std::vector<Call>& actual, std::initializer_list<Call> expected)
{
    if (actual.size() != expected.size()) return false;
    auto it = expected.begin();
    for (const Call& call : actual) {
        if (call.bridge != it->bridge || call.on != it->on) return false;
        ++it;
    }
    return true;
}

bool check_snapshot(const Q3U4PowerCoordinator& coordinator,
                    std::uint8_t receiver_mask,
                    Q3U4PowerState dev1,
                    Q3U4PowerState dev2)
{
    const Q3U4PowerSnapshot snapshot = coordinator.snapshot();
    return snapshot.receiver_mask == receiver_mask && !snapshot.card_acquired &&
           snapshot.backend_state[0] == dev1 && snapshot.backend_state[1] == dev2;
}

}  // namespace

int main()
{
    Delay delay;
    std::vector<Call> calls;
    Backend dev1(Q3U4Bridge::dev1, calls);
    Backend dev2(Q3U4Bridge::dev2, calls);
    Q3U4PowerCoordinator coordinator(dev1, dev2, delay);

    // Reference px4_mldev_set_power() handles the requesting device first.
    if (!coordinator.acquire_receiver(4U) ||
        !check(calls, {{Q3U4Bridge::dev2, true}, {Q3U4Bridge::dev1, true}})) {
        std::fprintf(stderr, "dev2 acquire order mismatch\n");
        return 1;
    }
    calls.clear();
    if (!coordinator.release_receiver(4U) ||
        !check(calls, {{Q3U4Bridge::dev2, false}, {Q3U4Bridge::dev1, false}})) {
        std::fprintf(stderr, "dev2 release order mismatch\n");
        return 2;
    }

    calls.clear();
    if (!coordinator.acquire_receiver(0U) ||
        !check(calls, {{Q3U4Bridge::dev1, true}, {Q3U4Bridge::dev2, true}})) {
        std::fprintf(stderr, "dev1 acquire order mismatch\n");
        return 3;
    }
    calls.clear();
    if (!coordinator.release_receiver(0U) ||
        !check(calls, {{Q3U4Bridge::dev1, false}, {Q3U4Bridge::dev2, false}})) {
        std::fprintf(stderr, "dev1 release order mismatch\n");
        return 4;
    }

    // A first-bridge failure must not touch the second bridge. Acquisition
    // rollback retries only the failed bridge to restore its known-off state.
    calls.clear();
    dev2.fail_next(Error::USB_IO);
    const auto dev2_failure = coordinator.acquire_receiver(4U);
    if (dev2_failure || dev2_failure.error() != Error::USB_IO ||
        !check(calls, {{Q3U4Bridge::dev2, true}, {Q3U4Bridge::dev2, false}}) ||
        !check_snapshot(coordinator, 0U, Q3U4PowerState::off, Q3U4PowerState::off)) {
        std::fprintf(stderr, "dev2 first-failure short-circuit mismatch\n");
        return 5;
    }

    calls.clear();
    dev1.fail_next(Error::TIMEOUT);
    const auto dev1_failure = coordinator.acquire_receiver(0U);
    if (dev1_failure || dev1_failure.error() != Error::TIMEOUT ||
        !check(calls, {{Q3U4Bridge::dev1, true}, {Q3U4Bridge::dev1, false}}) ||
        !check_snapshot(coordinator, 0U, Q3U4PowerState::off, Q3U4PowerState::off)) {
        std::fprintf(stderr, "dev1 first-failure short-circuit mismatch\n");
        return 6;
    }

    // If the second bridge fails, rollback preserves the requester's ordering.
    calls.clear();
    dev2.fail_next(Error::USB_IO);
    const auto dev1_second_failure = coordinator.acquire_receiver(0U);
    if (dev1_second_failure || dev1_second_failure.error() != Error::USB_IO ||
        !check(calls, {{Q3U4Bridge::dev1, true},
                       {Q3U4Bridge::dev2, true},
                       {Q3U4Bridge::dev1, false},
                       {Q3U4Bridge::dev2, false}}) ||
        !check_snapshot(coordinator, 0U, Q3U4PowerState::off, Q3U4PowerState::off)) {
        std::fprintf(stderr, "dev1 second-failure rollback order mismatch\n");
        return 7;
    }

    calls.clear();
    dev1.fail_next(Error::TIMEOUT);
    const auto dev2_second_failure = coordinator.acquire_receiver(4U);
    if (dev2_second_failure || dev2_second_failure.error() != Error::TIMEOUT ||
        !check(calls, {{Q3U4Bridge::dev2, true},
                       {Q3U4Bridge::dev1, true},
                       {Q3U4Bridge::dev2, false},
                       {Q3U4Bridge::dev1, false}}) ||
        !check_snapshot(coordinator, 0U, Q3U4PowerState::off, Q3U4PowerState::off)) {
        std::fprintf(stderr, "dev2 second-failure rollback order mismatch\n");
        return 8;
    }
    return 0;
}

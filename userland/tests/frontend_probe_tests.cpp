// SPDX-License-Identifier: GPL-2.0-only
#include "frontend_probe_support.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {
using namespace px4::userland;
#define CHECK(condition) do { if (!(condition)) return false; } while (false)

class Delay final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override { sleeps.push_back(milliseconds); }
    std::vector<std::uint32_t> sleeps;
};

class Power final : public Q3U4BackendPower {
public:
    explicit Power(std::vector<int>& events, int id) noexcept : events_(events), id_(id) {}
    Result<void> set_backend_power(bool on, Q3U4Delay&) noexcept override
    {
        events_.push_back(id_ * 10 + (on ? 1 : 0));
        if (on && fail_on) return Result<void>::failure(Error::USB_IO);
        if (!on && fail_off != Error::OK) return Result<void>::failure(fail_off);
        return Result<void>::success();
    }
    bool fail_on = false;
    Error fail_off = Error::OK;
private:
    std::vector<int>& events_;
    int id_;
};

struct CleanupState final {
    int close_calls = 0;
    int off_calls = 0;
    Error close_error = Error::OK;
    Error off_error = Error::OK;
};

Result<void> cleanup_close(void* context) noexcept
{
    auto& state = *static_cast<CleanupState*>(context);
    ++state.close_calls;
    return state.close_error == Error::OK ? Result<void>::success()
                                          : Result<void>::failure(state.close_error);
}

Result<void> cleanup_off(void* context) noexcept
{
    auto& state = *static_cast<CleanupState*>(context);
    ++state.off_calls;
    return state.off_error == Error::OK ? Result<void>::success()
                                        : Result<void>::failure(state.off_error);
}

struct Checks final { std::vector<Result<bool>> values; std::size_t next = 0U; };
Result<bool> check(void* context) noexcept
{
    auto& checks = *static_cast<Checks*>(context);
    if (checks.next >= checks.values.size()) return Result<bool>::success(false);
    return checks.values[checks.next++];
}

bool parser_tests()
{
    const char* valid[] = {"px4-frontend-probe", "--base", "b", "--firmware", "f",
                           "--device", "1", "--receiver", "2", "--frequency-khz", "40000",
                           "--tune-terrestrial"};
    CHECK(parse_frontend_probe_arguments(12, valid).valid);
    const char* bad_number[] = {"p", "--base", "b", "--firmware", "f", "--device", "1",
                                "--receiver", "2", "--frequency-khz", "40x", "--tune-terrestrial"};
    CHECK(!parse_frontend_probe_arguments(12, bad_number).valid);
    const char* bad_frequency[] = {"p", "--base", "b", "--firmware", "f", "--device", "1",
                                   "--receiver", "2", "--frequency-khz", "1002001", "--tune-terrestrial"};
    CHECK(!parse_frontend_probe_arguments(12, bad_frequency).valid);
    const char* duplicate[] = {"p", "--base", "b", "--base", "c", "--firmware", "f",
                               "--device", "1", "--receiver", "2", "--frequency-khz", "40000",
                               "--tune-terrestrial"};
    CHECK(!parse_frontend_probe_arguments(14, duplicate).valid);
    const char* unknown[] = {"p", "--base", "b", "--firmware", "f", "--device", "1",
                             "--receiver", "2", "--frequency-khz", "40000", "--wat"};
    CHECK(!parse_frontend_probe_arguments(12, unknown).valid);
    const char* missing[] = {"p", "--base", "b", "--firmware", "f", "--device", "1"};
    CHECK(!parse_frontend_probe_arguments(7, missing).valid);
    const char* duplicate_flag[] = {"p", "--base", "b", "--firmware", "f", "--device", "1",
                                    "--receiver", "2", "--frequency-khz", "40000",
                                    "--tune-terrestrial", "--tune-terrestrial"};
    CHECK(!parse_frontend_probe_arguments(13, duplicate_flag).valid);
    const char* duplicate_value[] = {"p", "--base", "b", "--firmware", "f", "--device", "1",
                                     "--receiver", "2", "--frequency-khz", "40000", "--frequency-khz",
                                     "40001", "--tune-terrestrial"};
    CHECK(!parse_frontend_probe_arguments(14, duplicate_value).valid);
    const char* wrong_device[] = {"p", "--base", "b", "--firmware", "f", "--device", "2",
                                  "--receiver", "2", "--frequency-khz", "40000", "--tune-terrestrial"};
    CHECK(!parse_frontend_probe_arguments(12, wrong_device).valid);
    const char* wrong_receiver[] = {"p", "--base", "b", "--firmware", "f", "--device", "1",
                                    "--receiver", "1", "--frequency-khz", "40000", "--tune-terrestrial"};
    CHECK(!parse_frontend_probe_arguments(12, wrong_receiver).valid);
    const char* lower_bound[] = {"p", "--base", "b", "--firmware", "f", "--device", "1",
                                 "--receiver", "2", "--frequency-khz", "40000", "--tune-terrestrial"};
    CHECK(parse_frontend_probe_arguments(12, lower_bound).valid);
    return true;
}

bool power_tests()
{
    std::vector<int> events;
    Power one(events, 1), two(events, 2);
    CoupledProbePower coupled(one, two);
    Delay delay;
    CHECK(coupled.set_backend_power(true, delay));
    CHECK(events == std::vector<int>({11, 21}));
    events.clear(); two.fail_on = true;
    CHECK(!coupled.set_backend_power(true, delay));
    CHECK(events == std::vector<int>({11, 21, 10}));
    events.clear(); one.fail_off = Error::USB_IO; two.fail_off = Error::TIMEOUT;
    const auto off = coupled.set_backend_power(false, delay);
    CHECK(!off && off.error() == Error::USB_IO);
    CHECK(events == std::vector<int>({10, 20}));
    return true;
}

bool lock_tests()
{
    Delay delay;
    Checks immediate{{Result<bool>::success(true)}};
    auto result = poll_frontend_probe_lock(check, &immediate, delay);
    CHECK(result.locked && result.checks == 1U && result.elapsed_ms == 350U);
    CHECK(delay.sleeps == std::vector<std::uint32_t>({350U}));
    delay.sleeps.clear();
    Checks delayed{{Result<bool>::success(false), Result<bool>::success(false), Result<bool>::success(true)}};
    result = poll_frontend_probe_lock(check, &delayed, delay);
    CHECK(result.locked && result.checks == 3U && result.elapsed_ms == 350U);
    CHECK(delay.sleeps == std::vector<std::uint32_t>({10U, 10U, 330U}));
    Checks error_then_success{{Result<bool>::failure(Error::USB_IO), Result<bool>::success(true)}};
    delay.sleeps.clear();
    result = poll_frontend_probe_lock(check, &error_then_success, delay);
    CHECK(result.locked && result.checks == 2U && delay.sleeps == std::vector<std::uint32_t>({10U, 340U}));
    Checks timeout{std::vector<Result<bool>>(300U, Result<bool>::success(false))};
    delay.sleeps.clear();
    result = poll_frontend_probe_lock(check, &timeout, delay);
    CHECK(!result.locked && result.error == Error::TIMEOUT && result.checks == 300U);
    Checks final_error{std::vector<Result<bool>>(300U, Result<bool>::failure(Error::USB_IO))};
    result = poll_frontend_probe_lock(check, &final_error, delay);
    CHECK(!result.locked && result.error == Error::USB_IO && result.checks == 300U);
    return true;
}

bool cleanup_tests()
{
    CleanupState state;
    Error error = Error::OK;
    {
        ProbeCleanupGuard guard(cleanup_close, &state, cleanup_off, &state, error);
    }
    CHECK(state.close_calls == 1 && state.off_calls == 1 && error == Error::OK);
    state.close_error = Error::PROTOCOL_ERROR;
    state.off_error = Error::USB_IO;
    error = Error::OK;
    {
        ProbeCleanupGuard guard(cleanup_close, &state, cleanup_off, &state, error);
    }
    CHECK(state.close_calls == 2 && state.off_calls == 2 && error == Error::PROTOCOL_ERROR);
    CHECK(frontend_probe_cleanup_status(5, Error::USB_IO) == 5);
    CHECK(frontend_probe_cleanup_status(0, Error::USB_IO) == 6);
    CHECK(frontend_probe_cleanup_status(0, Error::OK) == 0);
    return true;
}
}  // namespace

bool run_frontend_probe_tests()
{
    return parser_tests() && power_tests() && lock_tests() && cleanup_tests();
}

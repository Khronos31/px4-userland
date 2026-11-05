// SPDX-License-Identifier: GPL-2.0-only
#include "l3_lnb_port.h"
#include "q3u4_lnb_power.h"

#include <cstdint>
#include <cstdio>

namespace {

using namespace px4::userland;

class FakeLnbPower final : public Q3U4LnbPower {
public:
    Result<void> set_lnb_power(bool on) noexcept override
    {
        ++calls;
        on_ = on;
        if (fail) {
            return Result<void>::failure(Error::USB_IO);
        }
        return Result<void>::success();
    }

    bool on_ = false;
    bool fail = false;
    unsigned int calls = 0U;
};

class FakeBackend final : public TunerServiceBackend {
public:
    std::uint8_t receiver_count() const noexcept override { return 4U; }

    bool receiver_supports(std::uint8_t receiver,
                           ipc::System system) const noexcept override
    {
        return receiver < 4U &&
               (receiver < 2U) == (system == ipc::System::ISDB_S);
    }

    Result<void> open_receiver(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }

    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t,
                                  std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }

    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }

    Result<bool> is_locked(std::uint8_t, ipc::System) noexcept override
    {
        return Result<bool>::success(true);
    }

    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }

    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    {
        return Result<void>::success();
    }

    Result<void> close_receiver(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }

    Result<void> set_independent_lnb_power(bool enabled) noexcept override
    {
        ++set_calls;
        if (fail_set) {
            return Result<void>::failure(Error::UNSUPPORTED);
        }
        independent.present = true;
        independent.value = enabled;
        return Result<void>::success();
    }

    dtv::userland::wire::Optional<bool>
    independent_lnb_power_state() noexcept override
    {
        return independent;
    }

    Result<void> shutdown() noexcept override
    {
        ++shutdown_calls;
        independent.present = true;
        independent.value = false;
        return Result<void>::success();
    }

    dtv::userland::wire::Optional<bool> independent{};
    bool fail_set = false;
    unsigned int set_calls = 0U;
    unsigned int shutdown_calls = 0U;
};

bool check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "check failed: %s\n", message);
    }
    return condition;
}

bool test_c3_tune_transaction() noexcept
{
    FakeLnbPower dev1;
    FakeLnbPower dev2;
    Q3U4LnbPowerCoordinator coordinator(dev1, dev2, true);
    if (!check(coordinator.set_independent_power(true).has_value(),
               "independent on")) {
        return false;
    }
    if (!check(dev1.on_ && dev2.on_, "both bridges on")) {
        return false;
    }
    if (!check(coordinator.begin_tune(0U, 0U).has_value(),
               "begin 0V tune") ||
        !check(coordinator.commit_tune(0U).has_value(), "commit 0V tune")) {
        return false;
    }
    const Q3U4LnbPowerSnapshot after_commit = coordinator.snapshot();
    if (!check(after_commit.physical_state[0U] == Q3U4LnbPhysicalState::on &&
                   after_commit.physical_state[1U] == Q3U4LnbPhysicalState::on,
               "independent state survives commit")) {
        return false;
    }
    if (!check(coordinator.begin_tune(1U, 0U).has_value(),
               "begin rollback tune") ||
        !check(coordinator.rollback_tune(1U).has_value(), "rollback tune")) {
        return false;
    }
    const Q3U4LnbPowerSnapshot after_rollback = coordinator.snapshot();
    if (!check(after_rollback.physical_state[0U] == Q3U4LnbPhysicalState::on &&
                   after_rollback.physical_state[1U] == Q3U4LnbPhysicalState::on,
               "independent state survives rollback")) {
        return false;
    }
    if (!check(coordinator.shutdown().has_value(), "shutdown")) {
        return false;
    }
    const Q3U4LnbPowerSnapshot after_shutdown = coordinator.snapshot();
    return check(!dev1.on_ && !dev2.on_ &&
                     after_shutdown.physical_state[0U] ==
                         Q3U4LnbPhysicalState::off &&
                     after_shutdown.physical_state[1U] ==
                         Q3U4LnbPhysicalState::off,
                 "shutdown drives both bridges off");
}

bool test_lnb_port_mapping() noexcept
{
    FakeBackend backend;
    L3LnbPort unsupported(backend, false, false);
    dtv_l3_lnb_state state{};
    dtv_l3_restore_result restoration = DTV_L3_RESTORE_NOT_ATTEMPTED;
    if (!check(unsupported.status(0U, &state) == DTV_L3_UNSUPPORTED,
               "unsupported status") ||
        !check(unsupported.set(0U, DTV_L3_LNB_OFF, 0U, &state) ==
                   DTV_L3_UNSUPPORTED,
               "unsupported set") ||
        !check(unsupported.restore(0U, &restoration) == DTV_L3_UNSUPPORTED,
               "unsupported restore")) {
        return false;
    }

    backend.independent.present = true;
    backend.independent.value = false;
    L3LnbPort port(backend, true, false);
    if (!check(port.status(0U, &state) == DTV_L3_OK &&
                   state.controlled == DTV_L3_LNB_COMMAND_OFF &&
                   state.consistency == DTV_L3_LNB_CONSISTENCY_CONSISTENT,
               "status off") ||
        !check(port.set(0U, DTV_L3_LNB_ON_15V, 0U, &state) ==
                   DTV_L3_UNSUPPORTED,
               "15V permission")) {
        return false;
    }

    L3LnbPort allowed(backend, true, true);
    if (!check(allowed.set(0U, DTV_L3_LNB_ON_15V, 0U, &state) == DTV_L3_OK &&
                   state.controlled == DTV_L3_LNB_COMMAND_ON_15V &&
                   state.consistency == DTV_L3_LNB_CONSISTENCY_CONSISTENT &&
                   state.readback == DTV_L3_SUPPORT_UNKNOWN,
               "set on")) {
        return false;
    }
    const unsigned int set_calls = backend.set_calls;
    if (!check(allowed.set(0U, DTV_L3_LNB_OFF, 1U, &state) == DTV_L3_TIMEOUT &&
                   backend.set_calls == set_calls,
               "expired deadline has no side effect")) {
        return false;
    }
    backend.fail_set = true;
    if (!check(allowed.set(0U, DTV_L3_LNB_OFF, 0U, &state) ==
                   DTV_L3_UNSUPPORTED,
               "backend failure mapping")) {
        return false;
    }
    backend.fail_set = false;
    if (!check(allowed.restore(0U, &restoration) == DTV_L3_OK &&
                   restoration == DTV_L3_RESTORE_SUCCEEDED &&
                   backend.shutdown_calls == 1U,
               "restore")) {
        return false;
    }
    return check(allowed.status(0U, &state) == DTV_L3_OK &&
                     state.controlled == DTV_L3_LNB_COMMAND_OFF,
                 "status off after restore");
}

}  // namespace

int main()
{
    const bool c3 = test_c3_tune_transaction();
    const bool mapping = test_lnb_port_mapping();
    return c3 && mapping ? 0 : 1;
}

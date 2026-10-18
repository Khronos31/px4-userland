// SPDX-License-Identifier: GPL-2.0-only
// R3 prepared Hz worker routes using the accepted R2 worker test fixtures.
#include "private/receiver_r2_test_support.h"
#include "single_receiver_frontend.h"
#include "strict_frequency_profiles.h"

#include <limits>

namespace {

class R3NoopTransport final : public Transport {
public:
    Result<std::size_t> bulk_read(std::uint8_t, MutableByteView, Timeout,
                                  BulkReadObservation*) noexcept override
    {
        ++calls;
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    Result<std::size_t> bulk_write(std::uint8_t, ByteView, Timeout) noexcept override
    {
        ++calls;
        return Result<std::size_t>::failure(Error::UNSUPPORTED);
    }
    Result<void> start_stream(const StreamConfig&) noexcept override
    {
        ++calls;
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<StreamEvent> wait_stream(Timeout) noexcept override
    {
        ++calls;
        return Result<StreamEvent>::failure(Error::UNSUPPORTED);
    }
    Result<void> cancel_stream() noexcept override
    {
        ++calls;
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> stop_stream() noexcept override
    {
        ++calls;
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    bool stream_active() const noexcept override { return false; }
    unsigned calls = 0U;
};

class R3NoopBridge final : public BridgeI2cMaster {
public:
    Result<void> request(BridgeI2cRequest*, std::size_t) noexcept override
    {
        ++calls;
        return Result<void>::success();
    }
    unsigned calls = 0U;
};

class R3NoopPower final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool, Q3U4Delay&) noexcept override
    {
        ++calls;
        return Result<void>::success();
    }
    unsigned calls = 0U;
};

class R3NoopDelay final : public Q3U4FrontendDelay {
public:
    void sleep_ms(std::uint32_t) noexcept override { ++calls; }
    unsigned calls = 0U;
};

struct R3ActualSingleProfile final {
    R3NoopBridge bridge{};
    R3NoopTransport transport{};
    It930xController controller{transport,
        CommandPacingOptions{CommandPacingMode::no_delay}};
    R3NoopPower power{};
    R3NoopDelay delay{};
    SingleReceiverFrontend frontend{bridge, controller, power, delay,
                                    DeviceModel::dtv02_1t1s_u, true};
};

class R3RecordingBackend final : public TunerServiceBackend {
public:
    std::uint8_t receiver_count() const noexcept override { return 2U; }
    bool receiver_supports(std::uint8_t receiver, System system) const noexcept override
    {
        if (single_profile != nullptr && receiver == 0U)
            return single_profile->receiver_supports(receiver, system);
        return (receiver == 0U && system == System::ISDB_T) ||
               (receiver == 1U && system == System::ISDB_S);
    }
    TunerStrictProfile strict_profile(std::uint8_t receiver,
                                      System system) const noexcept override
    {
        if (!receiver_supports(receiver, system)) return {};
        if (single_profile != nullptr && receiver == 0U)
            return single_profile->strict_profile(receiver, system);
        auto profile = system == System::ISDB_T
            ? r850_terrestrial_strict_profile() : rt710_satellite_strict_profile();
        profile.lnb_voltage_mask = system == System::ISDB_S && lnb_control ? 0x03U : 0U;
        return profile;
    }
    Result<void> open_receiver(std::uint8_t receiver) noexcept override
    {
        calls.push_back("open " + std::to_string(receiver));
        return Result<void>::success();
    }
    Result<void> tune_terrestrial(std::uint8_t, std::uint32_t,
                                  std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<void> tune_satellite(std::uint8_t, std::uint32_t,
                                std::uint32_t) noexcept override
    {
        return Result<void>::failure(Error::UNSUPPORTED);
    }
    Result<TunerHzProgramming> tune_terrestrial_hz(std::uint8_t receiver,
                                                   std::uint64_t hz,
                                                   std::uint32_t) noexcept override
    {
        record_hz("tune_t_hz", receiver, hz);
        return Result<TunerHzProgramming>::success({});
    }
    Result<TunerHzProgramming> tune_satellite_hz(std::uint8_t receiver,
                                                 std::uint64_t hz,
                                                 std::uint32_t) noexcept override
    {
        record_hz("tune_s_hz", receiver, hz);
        return Result<TunerHzProgramming>::success({});
    }
    Result<bool> is_locked(std::uint8_t, System) noexcept override
    {
        calls.push_back("lock");
        return Result<bool>::success(true);
    }
    Result<void> select_satellite_slot(std::uint8_t, std::uint8_t,
                                       std::uint32_t) noexcept override
    {
        calls.push_back("slot");
        return Result<void>::success();
    }
    Result<void> select_satellite_tsid(std::uint8_t, std::uint16_t,
                                       std::uint32_t) noexcept override
    {
        calls.push_back("tsid");
        return Result<void>::success();
    }
    Result<void> close_receiver(std::uint8_t) noexcept override
    {
        return Result<void>::success();
    }
    Result<void> begin_tune_power(std::uint8_t receiver, System,
                                  std::uint8_t voltage) noexcept override
    {
        calls.push_back("power " + std::to_string(receiver) + " " +
                        std::to_string(voltage));
        return Result<void>::success();
    }
    Result<void> commit_tune_power(std::uint8_t receiver) noexcept override
    {
        calls.push_back("commit " + std::to_string(receiver));
        return Result<void>::success();
    }

    void record_hz(const char* name, std::uint8_t receiver, std::uint64_t hz)
    {
        exact_hz = hz;
        last_receiver = receiver;
        calls.push_back(std::string(name) + " " + std::to_string(receiver));
    }

    std::vector<std::string> calls;
    std::uint64_t exact_hz = 0U;
    std::uint8_t last_receiver = 0xffU;
    bool lnb_control = true;
    const TunerServiceBackend* single_profile = nullptr;
};

struct R3Fixture final {
    FakeCardBackend card_backend;
    FakeCardSession card_session{};
    CardService card{card_backend, card_session};
    FakeNonce nonce{};
    FakeTime time{};
    R3RecordingBackend backend{};
    TunerService service{backend, nonce, time};
};

struct R3SingleFixture final {
    R3ActualSingleProfile actual{};
    FakeNonce nonce{};
    FakeTime time{};
    R3RecordingBackend backend{};
    TunerService service{backend, nonce, time};

    R3SingleFixture() { backend.single_profile = &actual.frontend; }
};

bool same_tune_record(const dtv::userland::detail::TuneRecord& left,
                      const dtv::userland::detail::TuneRecord& right)
{
    return left.system == right.system && left.frequency_khz == right.frequency_khz &&
           left.stream_id == right.stream_id && left.slot == right.slot &&
           left.lnb_voltage == right.lnb_voltage && left.timeout_ms == right.timeout_ms &&
           left.entrance == right.entrance && left.requested_hz == right.requested_hz &&
           left.programmed_hz.present == right.programmed_hz.present &&
           left.programmed_hz.value == right.programmed_hz.value &&
           left.verification == right.verification && left.lock.state == right.lock.state &&
           left.lock.reason == right.lock.reason && left.lock.value == right.lock.value &&
           left.lock.sampled_at_ns == right.lock.sampled_at_ns;
}

bool test_single_receiver_profile_rejects_explicit_lnb()
{
    R3SingleFixture f;
    const auto actual_profile = f.actual.frontend.strict_profile(0U, System::ISDB_S);
    if (!actual_profile.native_hz || actual_profile.lnb_voltage_mask != 0U ||
        actual_profile.min_hz != 146875000U ||
        !f.service.configure_strict_capabilities() || !f.service.acquire(1U, 0U)) {
        std::fprintf(stderr, "R3 single profile/configure failed native=%u lnb=%u min=%llu\n",
                     actual_profile.native_hz ? 1U : 0U, actual_profile.lnb_voltage_mask,
                     static_cast<unsigned long long>(actual_profile.min_hz));
        return false;
    }

    TunerHzTuneRequest request{};
    request.lease_id = 1U;
    request.system = System::ISDB_S;
    request.frequency_hz = 1049480123U;
    request.selection = dtv::userland::wire::Selection::slot;
    request.selection_value = 3U;
    request.timeout_ms = 1000U;
    f.time.now.store(79U);
    const auto absent = f.service.prepare_tune_hz(0U, request);
    const auto admitted = f.service.try_admit_prepared_tune(1U, absent, 79U);
    if (!admitted || admitted.value().receiver != 0U || admitted.value().accepted_at_ms != 79U) {
        std::fprintf(stderr, "R3 single absent admission failed error=%u\n",
                     static_cast<unsigned>(admitted ? Error::OK : admitted.error()));
        return false;
    }
    const auto executed = f.service.execute_admitted_tune(admitted.value());
    if (!executed) {
        std::fprintf(stderr, "R3 single absent execution failed error=%u\n",
                     static_cast<unsigned>(executed.error()));
        for (const auto& call : f.backend.calls) std::fprintf(stderr, "  %s\n", call.c_str());
        return false;
    }
    const auto before = f.service.tune_metadata(0U);
    const auto before_status = f.service.status();
    if (!before || !before_status || !before.value().current_valid ||
        before.value().revision == 0U ||
        before.value().current_revision != before.value().revision ||
        before.value().current.requested_hz != request.frequency_hz ||
        f.actual.transport.calls != 0U || f.actual.bridge.calls != 0U ||
        f.actual.power.calls != 0U || f.actual.delay.calls != 0U) {
        std::fprintf(stderr, "R3 single seed metadata failed meta=%u status=%u valid=%u revision=%llu current_revision=%llu hz=%llu transport=%u bridge=%u power=%u delay=%u\n",
                     before ? 1U : 0U, before_status ? 1U : 0U,
                     before && before.value().current_valid ? 1U : 0U,
                     static_cast<unsigned long long>(before ? before.value().revision : 0U),
                     static_cast<unsigned long long>(before ? before.value().current_revision : 0U),
                     static_cast<unsigned long long>(before ? before.value().current.requested_hz : 0U),
                     f.actual.transport.calls, f.actual.bridge.calls,
                     f.actual.power.calls, f.actual.delay.calls);
        return false;
    }
    const std::size_t backend_calls_before = f.backend.calls.size();

    for (const std::uint8_t voltage : {0U, 15U}) {
        request.lnb_voltage = {true, voltage};
        const auto prepared = f.service.prepare_tune_hz(0U, request);
        const auto rejected = f.service.try_admit_prepared_tune(1U, prepared, 80U);
        const auto after = f.service.tune_metadata(0U);
        const auto after_status = f.service.status();
        if (rejected || rejected.error() != Error::UNSUPPORTED || !after || !after_status ||
            after.value().revision != before.value().revision ||
            after.value().current_valid != before.value().current_valid ||
            after.value().current_revision != before.value().current_revision ||
            !same_tune_record(after.value().current, before.value().current) ||
            after_status.value().generation != before_status.value().generation ||
            f.backend.calls.size() != backend_calls_before ||
            f.actual.transport.calls != 0U || f.actual.bridge.calls != 0U ||
            f.actual.power.calls != 0U || f.actual.delay.calls != 0U) {
            std::fprintf(stderr, "R3 single rejection voltage=%u error=%u revision=%llu/%llu current_revision=%llu/%llu backend=%zu/%zu\n",
                         voltage, static_cast<unsigned>(rejected ? Error::OK : rejected.error()),
                         static_cast<unsigned long long>(after ? after.value().revision : 0U),
                         static_cast<unsigned long long>(before.value().revision),
                         static_cast<unsigned long long>(after ? after.value().current_revision : 0U),
                         static_cast<unsigned long long>(before.value().current_revision),
                         f.backend.calls.size(), backend_calls_before);
            return false;
        }
    }
    return true;
}

#define R3_CHECK(condition)                                                           \
    do {                                                                               \
        if (!(condition)) {                                                            \
            std::fprintf(stderr, "receiver R3 prepared worker check failed at %d: %s\n", \
                         __LINE__, #condition);                                        \
            return false;                                                              \
        }                                                                              \
    } while (false)

bool test_prepared_worker_route_and_rejection()
{
    R3Fixture f;
    R3_CHECK(static_cast<bool>(f.service.configure_strict_capabilities()));
    R3_CHECK(static_cast<bool>(f.service.acquire(1U, 1U)));
    f.backend.calls.clear();

    TunerHzTuneRequest request{};
    request.lease_id = 1U;
    request.system = System::ISDB_S;
    request.frequency_hz = 1049480123U;
    request.selection = dtv::userland::wire::Selection::slot;
    request.selection_value = 3U;
    request.timeout_ms = 1000U;
    f.time.now.store(77U);
    const auto prepared = f.service.prepare_tune_hz(1U, request);
    R3_CHECK(prepared.hz_entrance && prepared.hz.prepared_receiver == 1U &&
             prepared.hz.requested_hz == 1049480123U);
    const auto admitted = f.service.try_admit_prepared_tune(1U, prepared, 77U);
    R3_CHECK(admitted && admitted.value().accepted_at_ms == 77U &&
             admitted.value().request.requested_hz == 1049480123U &&
             admitted.value().receiver == 1U);
    R3_CHECK(f.service.execute_admitted_tune(admitted.value()));
    const auto seeded_current = f.service.tune_metadata(1U);
    R3_CHECK(seeded_current && seeded_current.value().revision != 0U &&
             seeded_current.value().current_valid &&
             seeded_current.value().current_revision == seeded_current.value().revision &&
             seeded_current.value().current.requested_hz == 1049480123U);
    f.backend.calls.clear();

    R3_CHECK(test_single_receiver_profile_rejects_explicit_lnb());

    request.timeout_ms = 99U;
    const auto invalid_scalar = f.service.prepare_tune_hz(1U, request);
    const auto precedence = f.service.try_admit_prepared_tune(1U, invalid_scalar, 80U);
    R3_CHECK(!precedence && precedence.error() == Error::INVALID_ARGUMENT);

    request.timeout_ms = 1000U;
    request.lnb_voltage = {};
    const auto worker_prepared = f.service.prepare_tune_hz(1U, request);
    ControlWorkerStartupOptions options{};
    auto workers = ControlWorkerLanes::create_for_test(f.card, f.service, options);
    R3_CHECK(workers);
    ControlWorkerTask task{};
    task.operation = ControlWorkerOperation::tuner_tune;
    task.kind = ControlWorkerTaskKind::client_request;
    task.client_id = 1U;
    task.receiver = 1U;
    task.lease_id = 1U;
    task.accepted_at_ms = 123U;
    task.tune_prepared = true;
    task.prepared_tune = worker_prepared;
    f.time.now.store(123U);
    R3_CHECK(workers.value()->submit(ControlWorkerLane::tuner_dev1, task));
    ControlWorkerCompletion completion{};
    R3_CHECK(drain_until(*workers.value(), ControlWorkerLane::tuner_dev1,
                         ControlWorkerOperation::tuner_tune, completion));
    R3_CHECK(completion.error == Error::OK && f.backend.last_receiver == 1U &&
             f.backend.exact_hz == 1049480123U);
    R3_CHECK(f.backend.calls.size() == 5U &&
             f.backend.calls[0U] == "power 1 0" &&
             f.backend.calls[1U] == "tune_s_hz 1" &&
             f.backend.calls[2U] == "lock" && f.backend.calls[3U] == "slot" &&
             f.backend.calls[4U] == "commit 1");
    R3_CHECK(workers.value()->stop_and_join());

    auto old_wire = tune_payload(1U, System::ISDB_S);
    old_wire.frequency_khz = std::numeric_limits<std::uint64_t>::max();
    const auto overflow = f.service.prepare_tune(1U, old_wire);
    const auto overflow_admission = f.service.try_admit_prepared_tune(1U, overflow, 200U);
    R3_CHECK(!overflow_admission && overflow_admission.error() == Error::INVALID_ARGUMENT);
    return true;
}

}  // namespace

int main()
{
    return test_prepared_worker_route_and_rejection() ? 0 : 1;
}

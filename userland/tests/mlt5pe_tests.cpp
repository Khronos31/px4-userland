// SPDX-License-Identifier: GPL-2.0-only
#include "px4/identity.h"
#include "px4/ipc.h"
#include "px4/tuner_service.h"

#include "mlt5pe_backend.h"
#include "mlt5pe_frontend.h"
#include "mlt5pe_power.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <tuple>
#include <vector>

namespace {

using namespace px4::userland;

#define MLT_CHECK(condition)                                                          \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::fprintf(stderr, "mlt5pe check failed at %s:%d: %s\n", __FILE__,     \
                         __LINE__, #condition);                                       \
            return false;                                                             \
        }                                                                             \
    } while (false)

// ---------------------------------------------------------------------------
// Identity

UsbTopologyObservation topology()
{
    UsbInterfaceObservation interface;
    interface.endpoints = {
        {0x81U, EndpointType::bulk, 512U}, {0x02U, EndpointType::bulk, 512U},
        {0x84U, EndpointType::bulk, 512U}, {0x85U, EndpointType::bulk, 512U}};
    return UsbTopologyObservation{{interface}};
}

DeviceObservation observation(std::uint16_t product_id, const char* serial)
{
    DeviceObservation value;
    value.vendor_id = kQ3U4VendorId;
    value.product_id = product_id;
    value.serial = serial;
    value.speed = UsbSpeed::high;
    value.topology = topology();
    return value;
}

bool test_identity()
{
    MLT_CHECK(device_profile_for_usb_id(0x0511U, 0x024eU)->model == DeviceModel::px_mlt5pe);
    MLT_CHECK(device_profile_for_usb_id(0x0511U, 0x924eU)->model ==
              DeviceModel::dtv02a_5ts_p);
    MLT_CHECK(device_profile_for_usb_id(0x0511U, 0x084aU)->model == DeviceModel::px_q3u4);
    MLT_CHECK(device_profile_for_usb_id(0x0511U, 0x083fU)->model == DeviceModel::px_w3u4);
    MLT_CHECK(device_profile(DeviceModel::px_w3u4).bridge_count == 1U &&
              device_profile(DeviceModel::px_w3u4).receiver_count == 4U);
    // Other PX-MLT/ISDB6014 product IDs remain unsupported.
    MLT_CHECK(device_profile_for_usb_id(0x0511U, 0x084eU) == nullptr);
    MLT_CHECK(device_profile_for_usb_id(0x0511U, 0x0252U) == nullptr);
    MLT_CHECK(device_profile_for_usb_id(0x0511U, 0x0254U) == nullptr);
    MLT_CHECK(device_profile_for_usb_id(0x1234U, 0x924eU) == nullptr);
    MLT_CHECK(device_profile(DeviceModel::dtv02a_5ts_p).bridge_count == 1U &&
              device_profile(DeviceModel::dtv02a_5ts_p).receiver_count == 5U);

    MLT_CHECK(valid_device_instance("00001205000960"));
    MLT_CHECK(valid_device_instance("000020263901491"));
    MLT_CHECK(!valid_device_instance("0000202639014912"));
    MLT_CHECK(!valid_device_instance("00002026390149x"));
    MLT_CHECK(!valid_device_instance(""));

    const auto dtv = observation(kDtv02a5TsPProductId, "000020263901491");
    // The last digit is not a bridge number: a single-bridge serial ending in
    // 0 or 9 is still valid.
    const auto mlt = observation(kPxMlt5PeProductId, "000011112222330");
    MLT_CHECK(validate_q3u4_observation(dtv) == ObservationStatus::usable);
    MLT_CHECK(validate_q3u4_observation(mlt) == ObservationStatus::usable);
    MLT_CHECK(validate_q3u4_observation(observation(kDtv02a5TsPProductId, "00002026390149")) ==
              ObservationStatus::invalid_serial);
    MLT_CHECK(validate_q3u4_observation(observation(kDtv02a5TsPProductId, "A00020263901491")) ==
              ObservationStatus::invalid_serial);
    auto slow = dtv;
    slow.speed = UsbSpeed::full;
    MLT_CHECK(validate_q3u4_observation(slow) == ObservationStatus::insufficient_speed);

    const auto w3u4_a = observation(kW3U4ProductId, "000012050009601");
    const auto w3u4_b = observation(kW3U4ProductId, "000012050009602");
    MLT_CHECK(validate_q3u4_observation(w3u4_a) == ObservationStatus::usable);
    const auto w3u4_grouped = group_q3u4_devices(
        std::vector<DeviceObservation>{w3u4_a, w3u4_b});
    MLT_CHECK(w3u4_grouped && w3u4_grouped.value().groups.size() == 2U);
    for (const Q3U4Group& group : w3u4_grouped.value().groups) {
        MLT_CHECK(group.status == GroupStatus::ready &&
                  group.model == DeviceModel::px_w3u4 && group.devices[0U] &&
                  !group.devices[1U]);
    }

    const auto q3u4_main = observation(kQ3U4ProductId, "000012050009601");
    const auto q3u4_sub = observation(kQ3U4ProductId, "000012050009602");
    const auto grouped =
        group_q3u4_devices(std::vector<DeviceObservation>{dtv, q3u4_sub, mlt, q3u4_main});
    MLT_CHECK(grouped && grouped.value().groups.size() == 3U);
    for (const Q3U4Group& group : grouped.value().groups) {
        MLT_CHECK(group.status == GroupStatus::ready);
        if (group.base_serial == "000020263901491") {
            MLT_CHECK(group.model == DeviceModel::dtv02a_5ts_p && group.devices[0U] &&
                      !group.devices[1U]);
        } else if (group.base_serial == "000011112222330") {
            MLT_CHECK(group.model == DeviceModel::px_mlt5pe);
        } else {
            MLT_CHECK(group.base_serial == "00001205000960" &&
                      group.model == DeviceModel::px_q3u4);
        }
    }
    // Three ready enclosures require an explicit selection.
    MLT_CHECK(select_ready_q3u4_group(grouped.value(), {}).error() == Error::INVALID_ARGUMENT);
    const auto selected = select_ready_q3u4_group(grouped.value(), "000020263901491");
    MLT_CHECK(selected &&
              grouped.value().groups[selected.value()].model == DeviceModel::dtv02a_5ts_p);
    MLT_CHECK(select_ready_q3u4_group(grouped.value(), "000020263901499").error() ==
              Error::NOT_FOUND);

    const auto duplicate = group_q3u4_devices(std::vector<DeviceObservation>{dtv, dtv});
    MLT_CHECK(duplicate && duplicate.value().groups.size() == 1U &&
              duplicate.value().groups[0U].status == GroupStatus::duplicate);
    const auto alone = group_q3u4_devices(std::vector<DeviceObservation>{dtv});
    MLT_CHECK(alone && select_ready_q3u4_group(alone.value(), {}).value() == 0U);
    return true;
}

// ---------------------------------------------------------------------------
// IPC value domains

bool test_ipc_list_and_status()
{
    const auto records = ipc::receiver_records(ipc::kMlt5PeReceiverCount);
    MLT_CHECK(records);
    MLT_CHECK(!ipc::receiver_records(6U) && !ipc::receiver_records(0U));
    const std::string serial = "000020263901491";
    ipc::ListResponsePayload list{
        7U, ByteView{reinterpret_cast<const std::uint8_t*>(serial.data()), serial.size()},
        1U, 0x01U, records.value(), ipc::kMlt5PeReceiverCount};
    std::array<std::uint8_t, 128U> buffer{};
    const auto encoded = ipc::encode_payload(list, MutableByteView{buffer.data(), buffer.size()});
    MLT_CHECK(encoded && encoded.value() == 14U + serial.size() + 5U * 4U);
    // generation, serial, ready, mask, count 5, card readers 1, then records.
    const std::size_t counts = 8U + 2U + serial.size() + 2U;
    MLT_CHECK(buffer[counts] == 5U && buffer[counts + 1U] == 1U);
    MLT_CHECK(buffer[counts + 2U] == 0U && buffer[counts + 3U] == 1U &&
              buffer[counts + 4U] == 0U && buffer[counts + 5U] == 3U);
    const auto decoded =
        ipc::decode_list_response_payload(ByteView{buffer.data(), encoded.value()});
    MLT_CHECK(decoded && decoded.value().receiver_count == 5U);
    MLT_CHECK(decoded.value().receivers[4U].global_id == 4U &&
              decoded.value().receivers[4U].dev_id == 1U &&
              decoded.value().receivers[4U].local_id == 4U &&
              decoded.value().receivers[4U].system == ipc::System::ISDB_T_OR_S);

    auto second_device = list;
    second_device.usb_present_mask = 0x03U;
    MLT_CHECK(!ipc::encode_payload(second_device, MutableByteView{buffer.data(), buffer.size()}));
    auto fixed_system = list;
    fixed_system.receivers[2U].system = ipc::System::ISDB_T;
    MLT_CHECK(!ipc::encode_payload(fixed_system, MutableByteView{buffer.data(), buffer.size()}));
    // A tampered count-5 record is rejected by the decoder as well.
    std::array<std::uint8_t, 128U> tampered = buffer;
    tampered[counts + 5U] = 1U;
    MLT_CHECK(!ipc::decode_list_response_payload(ByteView{tampered.data(), encoded.value()}));
    tampered = buffer;
    tampered[counts] = 6U;
    MLT_CHECK(!ipc::decode_list_response_payload(ByteView{tampered.data(), encoded.value()}));

    // Q3U4 keeps its fixed table and default count.
    const auto w3u4 = ipc::receiver_records(ipc::kW3U4ReceiverCount);
    MLT_CHECK(w3u4);
    MLT_CHECK(w3u4.value()[0].system == ipc::System::ISDB_S &&
              w3u4.value()[0].dev_id == 1U && w3u4.value()[0].local_id == 0U);
    MLT_CHECK(w3u4.value()[2].system == ipc::System::ISDB_T &&
              w3u4.value()[2].local_id == 2U);
    const std::string w3u4_serial = "000012050009601";
    ipc::ListResponsePayload w3u4_list{
        1U,
        ByteView{reinterpret_cast<const std::uint8_t*>(w3u4_serial.data()), w3u4_serial.size()},
        1U, 0x01U, w3u4.value(), ipc::kW3U4ReceiverCount};
    const auto w3u4_encoded =
        ipc::encode_payload(w3u4_list, MutableByteView{buffer.data(), buffer.size()});
    MLT_CHECK(w3u4_encoded && w3u4_encoded.value() == 14U + w3u4_serial.size() + 4U * 4U);
    const auto w3u4_decoded = ipc::decode_list_response_payload(
        ByteView{buffer.data(), w3u4_encoded.value()});
    MLT_CHECK(w3u4_decoded && w3u4_decoded.value().receiver_count == 4U &&
              w3u4_decoded.value().usb_present_mask == 0x01U &&
              w3u4_decoded.value().receivers[1].system == ipc::System::ISDB_S &&
              w3u4_decoded.value().receivers[3].system == ipc::System::ISDB_T);

    const auto q3u4 = ipc::receiver_records(ipc::kQ3U4ReceiverCount);
    MLT_CHECK(q3u4 && q3u4.value()[4U].dev_id == 2U &&
              q3u4.value()[4U].system == ipc::System::ISDB_S);
    ipc::ListResponsePayload q3u4_list{
        1U, ByteView{reinterpret_cast<const std::uint8_t*>(serial.data()), 14U}, 1U, 0x03U,
        q3u4.value()};
    MLT_CHECK(q3u4_list.receiver_count == 8U);
    MLT_CHECK(ipc::encode_payload(q3u4_list, MutableByteView{buffer.data(), buffer.size()})
                  .value() == 46U + 14U);

    ipc::StatusResponsePayload status{};
    status.generation = 1U;
    status.usb_present_mask = 0x01U;
    for (std::size_t index = 5U; index < ipc::kReceiverCount; ++index)
        status.receiver_states[index] = ipc::ReceiverState::absent;
    const auto status_size =
        ipc::encode_payload(status, MutableByteView{buffer.data(), buffer.size()});
    MLT_CHECK(status_size && buffer[12U + 5U] == 5U);
    const auto status_decoded =
        ipc::decode_status_response_payload(ByteView{buffer.data(), status_size.value()});
    MLT_CHECK(status_decoded &&
              status_decoded.value().receiver_states[7U] == ipc::ReceiverState::absent);
    buffer[12U + 7U] = 6U;
    MLT_CHECK(!ipc::decode_status_response_payload(ByteView{buffer.data(), status_size.value()}));

    // ISDB_T_OR_S is a LIST capability, never a TUNE system.
    ipc::TuneRequestPayload tune{1U, ipc::System::ISDB_T_OR_S, 557143U, 0xffffU, 0xffffU,
                                 6000000U, 0U, 1000U};
    MLT_CHECK(!ipc::encode_payload(tune, MutableByteView{buffer.data(), buffer.size()}));
    return true;
}

// ---------------------------------------------------------------------------
// Frontend over a simulated I2C bus

// Emulates the CXD2856ER bank register (0x00) and simple register storage per
// 7-bit address, plus a demod-controlled tuner gate.  All operations are
// recorded for sequence assertions.
class SimulatedBus final : public BridgeI2cMaster {
public:
    struct Operation final {
        bool read;
        std::uint8_t address;
        std::vector<std::uint8_t> data;
    };

    Result<void> request(BridgeI2cRequest* requests, std::size_t count) noexcept override
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (std::size_t index = 0U; index < count; ++index) {
            BridgeI2cRequest& request = requests[index];
            if (fail_address == request.address) return Result<void>::failure(fail_error);
            if (request.type == BridgeI2cRequestType::write) {
                std::vector<std::uint8_t> data(request.write_data.data,
                                               request.write_data.data + request.write_data.size);
                operations.push_back(Operation{false, request.address, data});
                pointer[request.address] = data[0U];
                if (data.size() == 1U) continue;
                if (request.address == 0x60U && !gate_open_any()) {
                    return Result<void>::failure(Error::USB_IO);
                }
                for (std::size_t offset = 1U; offset < data.size(); ++offset) {
                    const auto reg = static_cast<std::uint8_t>(data[0U] + offset - 1U);
                    if (reg == 0x00U && offset == 1U) bank[request.address] = data[offset];
                    registers[key(request.address, reg)] = data[offset];
                    // SLVX 0x08 is the CXD2856ER tuner gate (slvx = slvt + 2).
                    if (reg == 0x08U) gate[request.address] = data[offset] == 1U;
                }
            } else {
                operations.push_back(Operation{true, request.address, {}});
                for (std::size_t offset = 0U; offset < request.read_data.size; ++offset) {
                    const auto reg = static_cast<std::uint8_t>(pointer[request.address] + offset);
                    const auto found = registers.find(key(request.address, reg));
                    request.read_data.data[offset] =
                        found == registers.end() ? 0U : found->second;
                }
            }
        }
        return Result<void>::success();
    }

    void set(std::uint8_t address, std::uint8_t bank_value, std::uint8_t reg,
             std::uint8_t value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        registers[std::make_tuple(address, bank_value, reg)] = value;
    }

    bool gate_open_any() const noexcept
    {
        for (const auto& entry : gate) {
            if (entry.second) return true;
        }
        return false;
    }

    std::size_t writes_to(std::uint8_t address) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::size_t count = 0U;
        for (const Operation& operation : operations) {
            if (!operation.read && operation.address == address) ++count;
        }
        return count;
    }

    mutable std::mutex mutex;
    std::vector<Operation> operations;
    std::map<std::tuple<std::uint8_t, std::uint8_t, std::uint8_t>, std::uint8_t> registers;
    std::map<std::uint8_t, std::uint8_t> pointer;
    std::map<std::uint8_t, std::uint8_t> bank;
    std::map<std::uint8_t, bool> gate;
    int fail_address = -1;
    Error fail_error = Error::USB_IO;

private:
    std::tuple<std::uint8_t, std::uint8_t, std::uint8_t> key(std::uint8_t address,
                                                            std::uint8_t reg)
    {
        // Bank-register writes themselves are stored under the current bank.
        return std::make_tuple(address, reg == 0x00U ? std::uint8_t{0U} : bank[address], reg);
    }
};

class RecordingDelay final : public Mlt5PeDelay {
public:
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        sleeps.push_back(milliseconds);
    }
    std::vector<std::uint32_t> sleeps;
};

class RecordingPower final : public Q3U4BackendPower {
public:
    Result<void> set_backend_power(bool on, Q3U4Delay&) noexcept override
    {
        calls.push_back(on);
        if (results.empty()) return Result<void>::success();
        const Error error = results.front();
        results.pop_front();
        return error == Error::OK ? Result<void>::success() : Result<void>::failure(error);
    }
    std::vector<bool> calls;
    std::deque<Error> results;
};

class CountingPurger final : public Q3U4PsbPurger {
public:
    Result<void> purge() noexcept override
    {
        ++count;
        return Result<void>::success();
    }
    std::size_t count = 0U;
};

class RecordingLnb final : public Q3U4LnbPower {
public:
    Result<void> set_lnb_power(bool on) noexcept override
    {
        calls.push_back(on);
        if (results.empty()) return Result<void>::success();
        const Error error = results.front();
        results.pop_front();
        return error == Error::OK ? Result<void>::success() : Result<void>::failure(error);
    }
    std::vector<bool> calls;
    std::deque<Error> results;
};

bool test_frontend_open_tune_and_power()
{
    SimulatedBus bus1;
    SimulatedBus bus3;
    RecordingDelay delay;
    RecordingPower power;
    CountingPurger purger;
    Mlt5PeFrontend frontend(bus1, bus3, power, delay, &purger);

    // Receiver 1 is the demodulator at 0x6c (SLVX 0x6e) on bus 1.
    MLT_CHECK(frontend.open_receiver(1U));
    MLT_CHECK((power.calls == std::vector<bool>{true}));
    MLT_CHECK(bus3.operations.empty());
    MLT_CHECK(!bus1.operations.empty());
    // cxd2856er_init starts with SLVX 0x00 = 0 and 0x10 = 1.
    MLT_CHECK(bus1.operations[0U].address == 0x6eU &&
              (bus1.operations[0U].data == std::vector<std::uint8_t>{0x00U, 0x00U}));
    MLT_CHECK(bus1.operations[1U].address == 0x6eU &&
              (bus1.operations[1U].data == std::vector<std::uint8_t>{0x10U, 0x01U}));
    // The tuner at 0x60 was powered on only inside a gate open/close pair and
    // the gate is closed again.
    MLT_CHECK(bus1.writes_to(0x60U) != 0U && !bus1.gate_open_any());
    MLT_CHECK(frontend.receiver_state(1U) == Mlt5PeReceiverState::open);
    MLT_CHECK(frontend.open_receiver(1U).error() == Error::BUSY);

    // Capture requires a tuned receiver.
    MLT_CHECK(frontend.start_capture(1U).error() == Error::NOT_READY);

    MLT_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_t, 557143U));
    MLT_CHECK(frontend.receiver_state(1U) == Mlt5PeReceiverState::tuned);
    // The 20-bit kHz frequency is written into tuner register 0x68 + 8.
    bool frequency_written = false;
    for (const auto& operation : bus1.operations) {
        if (!operation.read && operation.address == 0x60U && operation.data[0U] == 0x68U &&
            operation.data.size() == 18U) {
            frequency_written = operation.data[9U] == (557143U & 0xffU) &&
                                operation.data[10U] == ((557143U >> 8U) & 0xffU) &&
                                operation.data[11U] == ((557143U >> 16U) & 0x0fU);
        }
    }
    MLT_CHECK(frequency_written);
    // post_tune ends with SLVT 0xc3 = 0.
    MLT_CHECK(bus1.operations.back().address == 0x6cU &&
              (bus1.operations.back().data == std::vector<std::uint8_t>{0xc3U, 0x00U}));

    // Lock state: bank 0x60 register 0x10 bit 0; bit 4 is a definitive miss.
    MLT_CHECK(frontend.is_terrestrial_locked(1U).value() == false);
    bus1.set(0x6cU, 0x60U, 0x10U, 0x01U);
    MLT_CHECK(frontend.is_terrestrial_locked(1U).value() == true);
    bus1.set(0x6cU, 0x60U, 0x10U, 0x10U);
    MLT_CHECK(frontend.is_terrestrial_locked(1U).error() == Error::TIMEOUT);
    MLT_CHECK(frontend.is_satellite_locked(1U).error() == Error::NOT_READY);

    // Retune the same receiver to ISDB-S; the stream is selected first.
    MLT_CHECK(frontend.select_satellite_tsid(1U, 0x4010U));
    MLT_CHECK((bus1.operations.back().data == std::vector<std::uint8_t>{0xe9U, 0x40U, 0x10U, 0x00U}));
    MLT_CHECK(frontend.select_satellite_slot(1U, 8U).error() == Error::INVALID_ARGUMENT);
    MLT_CHECK(frontend.tune(1U, Cxd2856erSystem::isdb_s, 1049480U));
    bus1.set(0x6cU, 0xa0U, 0x12U, 0x40U);
    MLT_CHECK(frontend.is_satellite_locked(1U).value() == true);
    MLT_CHECK(frontend.is_terrestrial_locked(1U).error() == Error::NOT_READY);

    // A second receiver on the other bus shares backend power.
    MLT_CHECK(frontend.open_receiver(0U));
    MLT_CHECK((power.calls == std::vector<bool>{true}));
    MLT_CHECK(bus3.operations[0U].address == 0x67U);
    MLT_CHECK(frontend.tune(0U, Cxd2856erSystem::isdb_t, 473143U));

    // The PSB is purged only before the first capture.
    MLT_CHECK(frontend.start_capture(1U) && purger.count == 1U);
    MLT_CHECK(frontend.start_capture(0U) && purger.count == 1U);
    MLT_CHECK(frontend.tune(0U, Cxd2856erSystem::isdb_t, 473143U).error() == Error::BUSY);
    MLT_CHECK(frontend.stop_capture(0U) && frontend.stop_capture(0U));
    MLT_CHECK(frontend.stop_capture(1U));
    MLT_CHECK(frontend.start_capture(0U) && purger.count == 2U);
    MLT_CHECK(frontend.stop_capture(0U));

    // The card keeps backend power after every receiver closes.
    MLT_CHECK(frontend.acquire_card());
    MLT_CHECK(frontend.close_receiver(0U) && frontend.close_receiver(1U));
    MLT_CHECK((power.calls == std::vector<bool>{true}));
    MLT_CHECK(frontend.close_receiver(1U));
    MLT_CHECK(frontend.release_card());
    MLT_CHECK((power.calls == std::vector<bool>{true, false}));
    MLT_CHECK(frontend.receiver_state(1U) == Mlt5PeReceiverState::closed);
    MLT_CHECK(frontend.open_receiver(5U).error() == Error::INVALID_ARGUMENT);
    return true;
}

bool test_frontend_open_failures()
{
    SimulatedBus bus1;
    SimulatedBus bus3;
    RecordingDelay delay;
    RecordingPower power;
    Mlt5PeFrontend frontend(bus1, bus3, power, delay);

    // A tuner that does not report crystal settle (0x1a != 0) fails open and
    // releases the power it acquired.
    bus3.set(0x60U, 0U, 0x1aU, 0x01U);
    MLT_CHECK(frontend.open_receiver(0U).error() == Error::PROTOCOL_ERROR);
    MLT_CHECK((power.calls == std::vector<bool>{true, false}));
    MLT_CHECK(frontend.receiver_state(0U) == Mlt5PeReceiverState::closed);
    MLT_CHECK(!bus3.gate_open_any());

    // An I2C failure on the demodulator is returned unchanged.
    bus1.fail_address = 0x6eU;
    MLT_CHECK(frontend.open_receiver(1U).error() == Error::USB_IO);
    MLT_CHECK((power.calls == std::vector<bool>{true, false, true, false}));

    // Power failure prevents any I2C traffic; unknown state is retried.
    power.results.push_back(Error::USB_IO);
    const std::size_t before = bus1.operations.size();
    MLT_CHECK(frontend.open_receiver(2U).error() == Error::USB_IO);
    MLT_CHECK(bus1.operations.size() == before);
    MLT_CHECK(frontend.reconcile_power());

    // Transport loss is terminal for power.
    power.results.push_back(Error::DISCONNECTED);
    MLT_CHECK(frontend.acquire_card().error() == Error::DISCONNECTED);
    MLT_CHECK(frontend.open_receiver(3U).error() == Error::DISCONNECTED);
    return true;
}

// ---------------------------------------------------------------------------
// Power and LNB coordinators

bool test_lnb_power()
{
    RecordingLnb lnb;
    Mlt5PeLnbPowerCoordinator locked(lnb, false);
    MLT_CHECK(locked.begin_tune(0U, 15U).error() == Error::UNSUPPORTED);
    MLT_CHECK(lnb.calls.empty());
    MLT_CHECK(locked.begin_tune(0U, 0U) && locked.commit_tune(0U));
    MLT_CHECK(lnb.calls.empty());

    Mlt5PeLnbPowerCoordinator coordinator(lnb, true);
    MLT_CHECK(coordinator.begin_tune(5U, 0U).error() == Error::INVALID_ARGUMENT);
    MLT_CHECK(coordinator.begin_tune(0U, 12U).error() == Error::INVALID_ARGUMENT);
    MLT_CHECK(coordinator.begin_tune(0U, 15U) && coordinator.commit_tune(0U));
    MLT_CHECK(coordinator.begin_tune(4U, 15U) && coordinator.commit_tune(4U));
    MLT_CHECK((lnb.calls == std::vector<bool>{true}));
    MLT_CHECK(coordinator.snapshot().ref_count == 2U);

    // Retuning receiver 0 to ISDB-T drops its reference; the other keeps 15 V.
    MLT_CHECK(coordinator.begin_tune(0U, 0U) && coordinator.commit_tune(0U));
    MLT_CHECK(coordinator.snapshot().ref_count == 1U && lnb.calls.size() == 1U);
    // A failed retune of receiver 4 to 0 V rolls back to its 15 V request.
    MLT_CHECK(coordinator.begin_tune(4U, 0U));
    MLT_CHECK((lnb.calls == std::vector<bool>{true, false}));
    MLT_CHECK(coordinator.rollback_tune(4U));
    MLT_CHECK((lnb.calls == std::vector<bool>{true, false, true}));

    // An ambiguous OFF write leaves cleanup debt that shutdown retries.
    lnb.results.push_back(Error::TIMEOUT);
    MLT_CHECK(coordinator.release_receiver(4U).error() == Error::TIMEOUT);
    MLT_CHECK(coordinator.snapshot().cleanup_debt &&
              coordinator.snapshot().physical_state == Q3U4LnbPhysicalState::unknown);
    MLT_CHECK(coordinator.shutdown());
    MLT_CHECK(!coordinator.snapshot().cleanup_debt && lnb.calls.back() == false);

    const std::size_t writes = lnb.calls.size();
    coordinator.disconnect();
    MLT_CHECK(coordinator.begin_tune(1U, 15U).error() == Error::DISCONNECTED);
    MLT_CHECK(coordinator.shutdown().error() == Error::DISCONNECTED);
    MLT_CHECK(lnb.calls.size() == writes);
    return true;
}

// ---------------------------------------------------------------------------
// TunerService end to end over the simulated frontend

class FixedNonce final : public TunerNonceSource {
public:
    Result<std::array<std::uint8_t, ipc::kNonceLength>> generate() noexcept override
    {
        return Result<std::array<std::uint8_t, ipc::kNonceLength>>::success({});
    }
};

class ServiceTime final : public TunerServiceTime {
public:
    std::uint64_t monotonic_ms() noexcept override { return now; }
    void sleep_ms(std::uint32_t milliseconds) noexcept override { now += milliseconds; }
    std::uint64_t now = 1000U;
};

ipc::TuneRequestPayload terrestrial(std::uint64_t lease)
{
    return ipc::TuneRequestPayload{lease, ipc::System::ISDB_T, 557143U, 0xffffU, 0xffffU,
                                   6000000U, 0U, 2000U};
}

ipc::TuneRequestPayload satellite(std::uint64_t lease, std::uint8_t voltage = 0U)
{
    return ipc::TuneRequestPayload{lease, ipc::System::ISDB_S, 1049480U, 0x4010U, 0xffffU,
                                   0U, voltage, 2000U};
}

bool test_tuner_service_dual_system()
{
    SimulatedBus bus1;
    SimulatedBus bus3;
    RecordingDelay delay;
    RecordingPower power;
    Mlt5PeFrontend frontend(bus1, bus3, power, delay);
    RecordingLnb lnb;
    Mlt5PeLnbPowerCoordinator lnb_power(lnb, true);
    Mlt5PeTunerBackend backend(frontend, lnb_power);
    FixedNonce nonce;
    ServiceTime time;
    TunerService service(backend, nonce, time);

    const auto status = service.status();
    MLT_CHECK(status && status.value().receiver_states[4U] == ipc::ReceiverState::free &&
              status.value().receiver_states[5U] == ipc::ReceiverState::absent &&
              status.value().receiver_states[7U] == ipc::ReceiverState::absent);
    MLT_CHECK(service.acquire(1U, 5U).error() == Error::INVALID_ARGUMENT);

    // Receiver 2 is 0x64 on bus 1; make both systems report lock.
    bus1.set(0x64U, 0x60U, 0x10U, 0x01U);
    bus1.set(0x64U, 0xa0U, 0x12U, 0x40U);
    const auto lease = service.acquire(1U, 2U);
    MLT_CHECK(lease);
    const std::uint64_t id = lease.value().lease_id;

    MLT_CHECK(service.tune(1U, terrestrial(id)));
    // ISDB-S on the same lease: the TSID is written before the frontend tune.
    const std::size_t before = bus1.operations.size();
    MLT_CHECK(service.tune(1U, satellite(id, 15U)));
    MLT_CHECK(bus1.operations.size() > before + 2U);
    MLT_CHECK(bus1.operations[before].address == 0x64U &&
              (bus1.operations[before].data == std::vector<std::uint8_t>{0x00U, 0xc0U}));
    MLT_CHECK((bus1.operations[before + 1U].data ==
               std::vector<std::uint8_t>{0xe9U, 0x40U, 0x10U, 0x00U}));
    MLT_CHECK((lnb.calls == std::vector<bool>{true}));

    // Back to ISDB-T: 0 V is committed and the LNB turns off.
    MLT_CHECK(service.tune(1U, terrestrial(id)));
    MLT_CHECK((lnb.calls == std::vector<bool>{true, false}));
    auto bad_lnb = terrestrial(id);
    bad_lnb.lnb_voltage = 15U;
    MLT_CHECK(service.tune(1U, bad_lnb).error() == Error::INVALID_ARGUMENT);

    // A satellite tune that never locks fails and rolls back to 0 V.
    bus1.set(0x64U, 0xa0U, 0x12U, 0x00U);
    MLT_CHECK(service.tune(1U, satellite(id, 15U)).error() == Error::TIMEOUT);
    MLT_CHECK((lnb.calls == std::vector<bool>{true, false, true, false}));

    MLT_CHECK(service.release(1U, id));
    MLT_CHECK(frontend.receiver_state(2U) == Mlt5PeReceiverState::closed);
    MLT_CHECK((power.calls == std::vector<bool>{true, false}));
    return true;
}

}  // namespace

bool run_mlt5pe_tests()
{
    return test_identity() && test_ipc_list_and_status() &&
           test_frontend_open_tune_and_power() && test_frontend_open_failures() &&
           test_lnb_power() && test_tuner_service_dual_system();
}

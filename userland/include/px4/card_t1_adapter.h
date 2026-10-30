// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_CARD_T1_ADAPTER_H
#define PX4_USERLAND_CARD_T1_ADAPTER_H

#include "px4/card.h"

#include <dtv/userland/host_card_t1/card_protocol_session.h>
#include <dtv/userland/host_card_t1/card_t1.h>

#include <cstddef>
#include <cstdint>

namespace px4::userland {

// Thin production seams from the existing It930x hardware/time to the Core
// T=1 owner. The product keeps USB, power and stop cleanup; Core owns the T=1
// framing, ATR parsing, EDC and deadline policy.
class CardT1TimeAdapter final : public dtv::userland::host_card_t1::CardTime {
public:
    explicit CardT1TimeAdapter(::px4::userland::CardTime& time) noexcept : time_(time) {}

    std::uint64_t monotonic_ms() noexcept override { return time_.monotonic_ms(); }
    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        time_.sleep_ms(milliseconds);
    }

private:
    ::px4::userland::CardTime& time_;
};

class CardT1DelayBridge final : public It930xCardDelay {
public:
    explicit CardT1DelayBridge(
        dtv::userland::host_card_t1::CardDelay& delay) noexcept : delay_(delay) {}

    void sleep_ms(std::uint32_t milliseconds) noexcept override
    {
        delay_.sleep_ms(milliseconds);
    }

private:
    dtv::userland::host_card_t1::CardDelay& delay_;
};

class CardT1HardwareAdapter final
    : public dtv::userland::host_card_t1::CardHardware {
public:
    explicit CardT1HardwareAdapter(::px4::userland::CardHardware& hardware) noexcept
        : hardware_(hardware)
    {
    }

    Result<bool> detect_card() noexcept override
    {
        return hardware_.detect_card();
    }

    Result<void> reset_card(
        dtv::userland::host_card_t1::CardDelay& delay) noexcept override
    {
        CardT1DelayBridge bridge(delay);
        return hardware_.reset_card(bridge);
    }

    Result<bool> data_ready() noexcept override
    {
        return hardware_.data_ready();
    }

    Result<std::size_t> read_data(MutableByteView output) noexcept override
    {
        return hardware_.read_data(output);
    }

    Result<void> write_data(ByteView input) noexcept override
    {
        return hardware_.write_data(input);
    }

    Result<void> set_baud_rate(
        dtv::userland::host_card_t1::CardBaudRate baud_rate) noexcept override
    {
        switch (baud_rate) {
        case dtv::userland::host_card_t1::CardBaudRate::baud_9600:
            return hardware_.set_baud_rate(It930xCardBaudRate::baud_9600);
        case dtv::userland::host_card_t1::CardBaudRate::baud_19200:
            return hardware_.set_baud_rate(It930xCardBaudRate::baud_19200);
        case dtv::userland::host_card_t1::CardBaudRate::baud_38400:
            return hardware_.set_baud_rate(It930xCardBaudRate::baud_38400);
        }
        return Result<void>::failure(Error::UNSUPPORTED);
    }

private:
    ::px4::userland::CardHardware& hardware_;
};

using CardT1Session = dtv::userland::host_card_t1::CardSession;
using CardT1ProtocolSession = dtv::userland::host_card_t1::CardProtocolSessionAdapter;

}  // namespace px4::userland

#endif  // PX4_USERLAND_CARD_T1_ADAPTER_H

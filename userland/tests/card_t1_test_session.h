// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_CARD_T1_TEST_SESSION_H
#define PX4_USERLAND_CARD_T1_TEST_SESSION_H

#include "px4/card.h"

#if defined(PX4_CARD_TESTS_CORE_T1)

#include "px4/card_t1_adapter.h"

#include <dtv/userland/host_card_t1/card_t1.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace px4::userland {

inline CardAtr to_product_atr(
    const dtv::userland::host_card_t1::CardAtr& source) noexcept
{
    CardAtr result;
    result.length = source.length;
    std::copy(source.bytes.begin(), source.bytes.begin() + source.length,
              result.bytes.begin());
    switch (source.baud_rate) {
    case dtv::userland::host_card_t1::CardBaudRate::baud_9600:
        result.baud_rate = It930xCardBaudRate::baud_9600;
        break;
    case dtv::userland::host_card_t1::CardBaudRate::baud_19200:
        result.baud_rate = It930xCardBaudRate::baud_19200;
        break;
    case dtv::userland::host_card_t1::CardBaudRate::baud_38400:
        result.baud_rate = It930xCardBaudRate::baud_38400;
        break;
    }
    result.ifsc = source.ifsc;
    result.edc = source.edc == dtv::userland::host_card_t1::CardEdc::crc
        ? CardEdc::crc : CardEdc::lrc;
    result.block_timeout_ms = source.block_timeout_ms;
    return result;
}

class CardT1TestSession final {
public:
    CardT1TestSession(CardHardware& hardware, CardTime& time) noexcept
        : hardware_adapter_(hardware), time_adapter_(time),
          session_(hardware_adapter_, time_adapter_)
    {
    }

    Result<void> initialize() noexcept { return session_.initialize(); }
    Result<std::size_t> transmit(ByteView apdu,
                                 MutableByteView response) noexcept
    {
        return session_.transmit(apdu, response);
    }
    bool initialized() const noexcept { return session_.initialized(); }
    CardAtr atr() const noexcept { return to_product_atr(session_.atr()); }
    void invalidate() noexcept { session_.invalidate(); }

private:
    CardT1HardwareAdapter hardware_adapter_;
    CardT1TimeAdapter time_adapter_;
    dtv::userland::host_card_t1::CardSession session_;
};

inline Result<CardAtr> test_parse_card_atr(ByteView bytes) noexcept
{
    const auto parsed = dtv::userland::host_card_t1::parse_card_atr(bytes);
    if (!parsed) return Result<CardAtr>::failure(parsed.error());
    return Result<CardAtr>::success(to_product_atr(parsed.value()));
}

inline Result<CardAtr> test_reset_and_read_card_atr(
    CardHardware& hardware, CardTime& time) noexcept
{
    CardT1HardwareAdapter hardware_adapter(hardware);
    CardT1TimeAdapter time_adapter(time);
    const auto atr = dtv::userland::host_card_t1::reset_and_read_card_atr(
        hardware_adapter, time_adapter);
    if (!atr) return Result<CardAtr>::failure(atr.error());
    return Result<CardAtr>::success(to_product_atr(atr.value()));
}

}  // namespace px4::userland

#else

namespace px4::userland {

using CardT1TestSession = CardSession;

inline Result<CardAtr> test_parse_card_atr(ByteView bytes) noexcept
{
    return parse_card_atr(bytes);
}

inline Result<CardAtr> test_reset_and_read_card_atr(
    CardHardware& hardware, CardTime& time) noexcept
{
    return reset_and_read_card_atr(hardware, time);
}

}  // namespace px4::userland

#endif  // PX4_CARD_TESTS_CORE_T1

#endif  // PX4_USERLAND_CARD_T1_TEST_SESSION_H

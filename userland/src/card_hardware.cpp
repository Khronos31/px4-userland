// Modified/ported for px4-userland on 2026-09-03.
//
// Derived from tsukumijima/px4_drv commit 9eedea8c502875a788697984b93b50032339b9aa.
// Origin paths: winusb/src/DriverHost_PX4/card_device.hpp,
// winusb/src/DriverHost_PX4/smart_card.hpp,
// winusb/src/DriverHost_PX4/smart_card.cpp,
// winusb/src/WinSCard_PX4/bcas_atr.hpp.
// Source snapshot maintained by tsukumijima.
// SPDX-License-Identifier: GPL-2.0-only
#include "px4/card.h"
#if defined(_WIN32)
#include "px4/platform_sleep.h"
#endif

#include <chrono>
#include <cstdint>
#include <thread>

namespace px4::userland {

std::uint64_t SystemCardTime::monotonic_ms() noexcept
{
    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void SystemCardTime::sleep_ms(std::uint32_t milliseconds) noexcept
{
#if defined(_WIN32)
    (void)platform::sleep_milliseconds(milliseconds);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

Result<bool> It930xCardHardware::detect_card() noexcept
{
    return controller_.detect_card();
}

Result<void> It930xCardHardware::reset_card(It930xCardDelay& delay) noexcept
{
    return controller_.reset_card(delay);
}

Result<bool> It930xCardHardware::data_ready() noexcept
{
    return controller_.card_data_ready();
}

Result<std::size_t> It930xCardHardware::read_data(MutableByteView output) noexcept
{
    return controller_.read_card_data(output);
}

Result<void> It930xCardHardware::write_data(ByteView input) noexcept
{
    return controller_.write_card_data(input);
}

Result<void> It930xCardHardware::set_baud_rate(It930xCardBaudRate baud_rate) noexcept
{
    return controller_.set_card_baud_rate(baud_rate);
}

}  // namespace px4::userland

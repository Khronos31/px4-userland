// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_PX4D_RECEIVER_RECORDS_H
#define PX4_USERLAND_PX4D_RECEIVER_RECORDS_H

#include "px4/ipc.h"

#include <array>
#include <cstdint>

namespace px4::userland::px4_product {

inline Result<std::array<ipc::ReceiverRecord, ipc::kReceiverCount>> receiver_records(
    std::uint8_t receiver_count, bool dual_system = false) noexcept
{
    std::array<ipc::ReceiverRecord, ipc::kReceiverCount> records{};
    if (receiver_count != ipc::kSingleReceiverCount &&
        receiver_count != ipc::kMlt3PeReceiverCount &&
        receiver_count != ipc::kW3U4ReceiverCount &&
        receiver_count != ipc::kMlt5PeReceiverCount &&
        receiver_count != ipc::kQ3U4ReceiverCount) {
        return Result<std::array<ipc::ReceiverRecord, ipc::kReceiverCount>>::failure(
            Error::INVALID_ARGUMENT);
    }
    for (std::size_t index = 0U; index < receiver_count; ++index) {
        const auto id = static_cast<std::uint8_t>(index);
        if (dual_system || receiver_count == ipc::kMlt3PeReceiverCount ||
            receiver_count == ipc::kMlt5PeReceiverCount) {
            records[index] = ipc::ReceiverRecord{id, 1U, id, ipc::System::ISDB_T_OR_S};
        } else if (receiver_count == ipc::kSingleReceiverCount) {
            records[index] = ipc::ReceiverRecord{id, 1U, id, ipc::System::ISDB_T};
        } else {
            const auto local = static_cast<std::uint8_t>(index % 4U);
            records[index] = ipc::ReceiverRecord{
                id, static_cast<std::uint8_t>((index / 4U) + 1U), local,
                local < 2U ? ipc::System::ISDB_S : ipc::System::ISDB_T};
        }
    }
    return Result<std::array<ipc::ReceiverRecord, ipc::kReceiverCount>>::success(records);
}

}  // namespace px4::userland::px4_product

#endif  // PX4_USERLAND_PX4D_RECEIVER_RECORDS_H

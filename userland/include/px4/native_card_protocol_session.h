// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_NATIVE_CARD_PROTOCOL_SESSION_H
#define PX4_USERLAND_NATIVE_CARD_PROTOCOL_SESSION_H

#include "px4/card.h"
#include "px4/card_service.h"

namespace px4::userland {

class NativeCardProtocolSession final : public CardProtocolSession {
public:
    explicit NativeCardProtocolSession(CardSession& session) noexcept : session_(session) {}

    Result<void> initialize() noexcept override { return session_.initialize(); }
    Result<std::size_t> transmit(ByteView apdu,
                                 MutableByteView response) noexcept override
    {
        return session_.transmit(apdu, response);
    }
    bool initialized() const noexcept override { return session_.initialized(); }
    const CardAtr& atr() const noexcept override { return session_.atr(); }
    void invalidate() noexcept override { session_.invalidate(); }

private:
    CardSession& session_;
};

}  // namespace px4::userland

#endif  // PX4_USERLAND_NATIVE_CARD_PROTOCOL_SESSION_H

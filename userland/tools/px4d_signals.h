// SPDX-License-Identifier: GPL-2.0-only
#ifndef PX4_USERLAND_PX4D_SIGNALS_H
#define PX4_USERLAND_PX4D_SIGNALS_H

namespace px4::userland::px4d {

bool install_signal_handlers() noexcept;
bool stop_requested() noexcept;

}  // namespace px4::userland::px4d

#endif

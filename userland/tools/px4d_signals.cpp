// SPDX-License-Identifier: GPL-2.0-only
#include "px4d_signals.h"

#include <csignal>
#include <signal.h>

namespace {

volatile std::sig_atomic_t stop_requested_flag = 0;

void stop_signal_handler(int) noexcept
{
    stop_requested_flag = 1;
}

}  // namespace

namespace px4::userland::px4d {

bool stop_requested() noexcept
{
    return stop_requested_flag != 0;
}

bool install_signal_handlers() noexcept
{
    struct sigaction action {};
    action.sa_handler = stop_signal_handler;
    if (sigemptyset(&action.sa_mask) != 0) return false;
    action.sa_flags = 0;

    struct sigaction previous_int {};
    if (::sigaction(SIGINT, &action, &previous_int) != 0) return false;
    struct sigaction previous_term {};
    if (::sigaction(SIGTERM, &action, &previous_term) != 0) {
        (void)::sigaction(SIGINT, &previous_int, nullptr);
        return false;
    }
    struct sigaction previous_hup {};
    if (::sigaction(SIGHUP, &action, &previous_hup) != 0) {
        (void)::sigaction(SIGTERM, &previous_term, nullptr);
        (void)::sigaction(SIGINT, &previous_int, nullptr);
        return false;
    }
    return true;
}

}  // namespace px4::userland::px4d

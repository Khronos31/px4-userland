// SPDX-License-Identifier: GPL-2.0-only
#include "px4/libusb_transport.h"
#include <android/log.h>
#include <cstddef>
#include <libusb.h>

extern "C" void* memcpy(void*, const void*, std::size_t);

// These retained relocations force both the px4 libusb transport translation
// unit and the official static libusb archive into the final ELF. They are
// never called, so running this linkcheck cannot initialize libusb or touch USB.
[[gnu::used]] static volatile const auto kNativeEnumerate =
    &px4::userland::Q3U4Runtime::enumerate_native;
[[gnu::used]] static volatile const auto kLibusbInit = &libusb_init;
[[gnu::used]] static volatile const auto kLibusbWrap = &libusb_wrap_sys_device;
[[gnu::used]] static volatile const auto kLibusbClose = &libusb_close;
[[gnu::used]] static volatile const auto kAndroidLogPrint = &__android_log_print;
[[gnu::used]] static volatile const auto kLibcMemcpy = &memcpy;

int main()
{
    // Read the anchors so the final link retains their relocations. This is
    // deliberately not a call: the executable remains USB-inert when run.
    return (kNativeEnumerate == nullptr || kLibusbInit == nullptr || kLibusbWrap == nullptr ||
            kLibusbClose == nullptr ||
            kAndroidLogPrint == nullptr || kLibcMemcpy == nullptr)
               ? 1
               : 0;
}

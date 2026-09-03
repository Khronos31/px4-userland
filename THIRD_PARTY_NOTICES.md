# Third-party notices and release inventory

This inventory distinguishes source-tree dependencies, system dependencies, and code that is statically present in a
release binary. It is an engineering inventory, not a legal opinion. A notice in this file does not by itself complete
the compliance work for a binary release.

## No vendored dependency in the repository source

The repository source does not vendor libusb, pcsc-lite, the Android NDK, libc++/libc++abi, musl, or firmware. Native
CMake discovers libusb from the host or an explicitly supplied library and discovers the PC/SC IFD headers/library
from the host. The Android build script obtains a pinned libusb source archive during the build; that archive is not
checked into this repository. GitHub Actions and their runners are CI infrastructure, not product dependencies in a
release source snapshot.

## Dependencies present in release binaries

### Android: static libusb

The Android build links libusb 1.0.28 statically (`libusb-1.0.a`). libusb is licensed under LGPL-2.1-or-later. The
release inventory must include the applicable libusb source/license notices and a practical way for a recipient to
relink the application with a modified compatible libusb, such as relinkable application objects together with the
corresponding libusb source and build instructions, or an equivalent arrangement confirmed for the specific release.

Primary license text: [libusb 1.0.28 `COPYING`](https://github.com/libusb/libusb/blob/v1.0.28/COPYING).

Providing only this notice file is not sufficient. The libusb source archive, its verified hash, the relink materials,
and the release-specific build instructions are release blockers for an Android binary release.

### Android: NDK C++ runtime

The Android build uses NDK r27 and `CMAKE_ANDROID_STL_TYPE=c++_static`; the resulting artifacts can contain the
statically linked libc++ and libc++abi runtime portions. The NDK-provided `NOTICE` and `NOTICE.toolchain` files must
be reviewed and packaged with the binary release, including the applicable LLVM Project Apache-2.0 WITH LLVM-exception
terms and the legacy/third-party notices identified by that NDK. The exact archive members linked into each final
artifact must be recorded for the release.

NDK notice packaging and the release-specific static-link inventory are release blockers. This document does not
claim that Android binary release compliance is complete.

Primary license and notice texts: [LLVM `LICENSE.txt`](https://llvm.org/LICENSE.txt), Android NDK r27
[`NOTICE`](https://android.googlesource.com/toolchain/prebuilts/ndk/r27/+/refs/heads/main/NOTICE), and
[`NOTICE.toolchain`](https://android.googlesource.com/toolchain/prebuilts/ndk/r27/+/refs/heads/main/NOTICE.toolchain).

### Linux and macOS: system/dynamic dependencies

The intended native builds use host-provided dynamic dependencies rather than vendored product copies:

- libusb 1.0.x, under the installed package's LGPL-2.1-or-later terms and notices;
- system PC/SC / pcsc-lite libraries and the `ifdhandler.h` ABI. The system pcsc-lite core is normally distributed
  under BSD-3-Clause terms; the exact package version and copyright file must be recorded for each release;
- the host C++ runtime and other OS-provided libraries, which remain system dependencies when dynamically linked.

Primary pcsc-lite license text: [pcsc-lite `COPYING`](https://github.com/LudovicRousseau/PCSC/blob/master/COPYING).

The actual ELF or Mach-O linkage must be checked for every release so that an archive intended to be dynamic has not
silently absorbed a static library. If pcsc-lite or another system component is instead bundled into a release binary,
that binary becomes a separate inventory item requiring its own notices and corresponding obligations.

### CI-only actions

The workflow uses `actions/checkout@v4` and `nttld/setup-ndk@v1` on CI runners. Their repositories are MIT-licensed
CI inputs and are not normally included in the product source or release binaries. A future release workflow should
pin action commits and retain its supply-chain evidence separately.

## Future Linux musl static artifact

A Linux musl static release is planned but is not implemented or released at this stage. When it exists, the selected
musl version's `COPYRIGHT` (musl is MIT-licensed with additional listed BSD/MIT portions), the actual static crt/libc
contents, and every other static library must be inventoried and shipped as required. This is a future musl-static
release blocker, separate from the current Android blockers; it does not mean the current dynamic native build is
already non-compliant.

Primary license text: musl [`COPYRIGHT`](https://git.musl-libc.org/cgit/musl/plain/COPYRIGHT).

## Firmware is separate and not included

The IT930x firmware is not a third-party library used by the source build and is not included as a source file, blob,
archive member, or release artifact. It is a separately supplied runtime input whose licensing and acquisition remain
outside this third-party library inventory. The project does not download, extract, or transform vendor firmware.

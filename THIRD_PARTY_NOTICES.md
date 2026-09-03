# Third-party notices and release inventory

This inventory distinguishes source-tree dependencies, system dependencies, and code that is statically present in a
release binary. It is an engineering distribution contract. A notice in this file does not by itself complete the
compliance work for a binary release.

## No vendored dependency in the repository source

The repository source does not vendor libusb, pcsc-lite, the Android NDK, libc++/libc++abi, musl, or firmware. Native
CMake discovers libusb from the host or an explicitly supplied library and discovers the PC/SC IFD headers/library
from the host. The Android build script obtains a pinned libusb source archive during the build; that archive is not
checked into this repository. GitHub Actions and their runners are CI infrastructure, not product dependencies in a
release source snapshot.

## Dependencies present in release binaries

### Android: static libusb

The Android build links libusb 1.0.28 statically (`libusb-1.0.a`). libusb remains licensed under LGPL-2.1-or-later.
The planned release uses the LGPL-2.1 §6(d) route, with same-place access to the §6(a) corresponding-source materials:
the exact px4-userland source, the exact libusb source used for the binary, verification hashes, and build/relink
instructions. The complete px4-userland source is GPL-2.0-only and is rebuildable, so separate application `.o` files as
a relinkable deliverable are not required for this release contract. This does not claim that libusb was converted to
GPL under LGPL §3; libusb's LGPL terms remain applicable.

Primary license text: [libusb 1.0.28 `COPYING`](https://github.com/libusb/libusb/blob/v1.0.28/COPYING).

Providing only this notice file, or relying only on GitHub's automatically generated source archive, is not sufficient.
The exact libusb source must be present in the corresponding-source archive because the GitHub archive does not contain
the downloaded libusb source. The Android binary release gate is incomplete until the same GitHub Release page contains
both the binary archive and its corresponding-source archive with the release-specific hash and build/relink evidence.

Each Android binary archive must contain, at its top level, the GPL license (`LICENSE`), libusb's LGPL license text
(`libusb/COPYING` or an equivalent clearly identified copy), a prominent plain-text third-party notice, this
`THIRD_PARTY_NOTICES.md`, and `README.md`. The prominent notice must identify the static libusb 1.0.28 and the NDK
runtime and point to the full notices and corresponding-source archive.

### Android: NDK C++ runtime

The Android build uses NDK r27 and `CMAKE_ANDROID_STL_TYPE=c++_static`; the resulting artifacts can contain the
statically linked libc++ and libc++abi runtime portions. The release must include the applicable LLVM Project
Apache-2.0 WITH LLVM-exception terms, the NDK-provided `NOTICE` and `NOTICE.toolchain` files, and the legacy/third-party
notices identified by that NDK. The exact archive members actually linked into each final artifact must be recorded in
an immutable release inventory; listing the whole NDK without the linked-member inventory is insufficient.

NDK notice packaging and the release-specific static-link inventory are release blockers. This document does not
claim that Android binary release compliance is complete.

Primary license and notice texts: [LLVM `LICENSE.txt`](https://llvm.org/LICENSE.txt), Android NDK r27
[`NOTICE`](https://android.googlesource.com/toolchain/prebuilts/ndk/r27/+/refs/heads/main/NOTICE), and
[`NOTICE.toolchain`](https://android.googlesource.com/toolchain/prebuilts/ndk/r27/+/refs/heads/main/NOTICE.toolchain).

### Linux and macOS: system/dynamic dependencies

The intended native release builds use host-provided dynamic dependencies rather than vendored product copies. The Linux
x86_64 artifact is musl-dynamic/Siano-style: its PT_INTERP names `/lib/ld-musl-x86_64.so.1`, which the target host must
provide, and it uses shared host libusb rather than a static libusb copy. Existing glibc native builds are
CI/development builds, not the Linux release artifact.

- libusb 1.0.x, under the installed package's LGPL-2.1-or-later terms and notices;
- system PC/SC / pcsc-lite libraries and the `ifdhandler.h` ABI. The system pcsc-lite core is normally distributed
  under BSD-3-Clause terms; the exact package version and copyright file must be recorded for each release;
- the host C++ runtime and other OS-provided libraries, which remain system dependencies when dynamically linked.

Primary pcsc-lite license text: [pcsc-lite `COPYING`](https://github.com/LudovicRousseau/PCSC/blob/master/COPYING).

For every actual release binary, the dynamic dependency claim must be verified from the binary and retained with the
release evidence. For the Linux x86_64 musl artifact, use `readelf -l`/`readelf -d` to verify the ELF interpreter
`/lib/ld-musl-x86_64.so.1`, `NEEDED` `libusb-1.0.so.0`, and `NEEDED` `libc.musl-x86_64.so.1`. For macOS, use `otool -L`
to verify the host-provided dynamic libusb. If libusb is static, or if pcsc-lite or another system component is bundled,
switch that binary to the Android-equivalent static source obligations and inventory its exact source, license, notices,
and build/relink materials.

### CI-only actions

The workflow uses `actions/checkout@v4` and `nttld/setup-ndk@v1` on CI runners. Their repositories are MIT-licensed
CI inputs and are not normally included in the product source or release binaries. A future release workflow should
pin action commits and retain its supply-chain evidence separately.

## Future Linux musl static artifact

A Linux musl static release is planned but is not implemented or released at this stage. When it exists, the selected
musl version's exact source and `COPYRIGHT` (musl is MIT-licensed with additional listed BSD/MIT portions), the actual
static crt/libc contents, and every other static library must be inventoried and shipped with its exact license texts,
notices, and build instructions. This is a future musl-static release blocker, separate from the current Android
blockers; it does not mean the current dynamic native build is already non-compliant.

Primary license text: musl [`COPYRIGHT`](https://git.musl-libc.org/cgit/musl/plain/COPYRIGHT).

## Firmware is separate and not included

The IT930x firmware is not a third-party library used by the source build and is not included as a source file, blob,
archive member, or release artifact. It is a separately supplied runtime input whose licensing and acquisition remain
outside this third-party library inventory. The project does not download, extract, or transform vendor firmware.

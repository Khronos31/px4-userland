# Third-party notices and release-candidate contract

This document describes the dependencies and materials for the currently implemented `release-candidate` packaging
contract. It is not, by itself, a declaration that the project is stable or ready for a general release. The release
candidate workflow generates and audits four platform archives plus one corresponding-source archive, then uploads them
together with an outer `SHA256SUMS` file.

## No vendored dependency in the repository source

The repository source does not vendor libusb, pcsc-lite, the Android NDK, libc++/libc++abi, musl, or firmware. Native
CMake discovers libusb and PC/SC headers from the host or from explicit build inputs. The Android build obtains the
verified libusb source archive during the build; it is not checked into this repository.

The source archive is made from the exact committed source ref selected by the workflow. It also contains the exact
verified libusb 1.0.28 source archive, its `COPYING`, checksums, and `BUILD-RELINK.md`. It excludes `.git`, build outputs,
firmware, APK/add-on material, and vendor drivers.

## Android binary archives

The Android archives are API 24+ Bionic builds for `aarch64` and `armv7a`. libusb 1.0.28 is statically linked as
`libusb-1.0.a` and remains licensed under LGPL-2.1-or-later. Each Android archive includes:

- `libusb/COPYING`;
- the exact NDK `source.properties` revision used by the build;
- NDK `NOTICE` and `NOTICE.toolchain`;
- a prominent `DEPENDENCY-NOTICE.txt` identifying libusb 1.0.28, static linkage, LGPL-2.1-or-later, the NDK revision and
  notice materials, and the corresponding-source archive;
- sanitized `evidence/inventory/*-static-archives.tsv` files listing archive basenames, member names, and categories;
- `manifest.json` and `SHA256SUMS`, both checked against the archived payload.

The static NDK C++ runtime portions, including the applicable `libc++`/`libc++abi` portions, are under Apache-2.0 WITH
LLVM-exception. The bundled NDK `NOTICE` and `NOTICE.toolchain` provide the associated terms and notices for those
runtime materials. The exact NDK revision is retained in `ndk/source.properties` and identified in the prominent
dependency notice.

The CI release-candidate path uses NDK r27d. The archive audit retains and checks the exact `27.x` revision recorded in
`ndk/source.properties`; it does not replace that revision with a generic NDK label. The static inventory is generated
from explicit linker-map inputs, but raw linker maps and their source/build/NDK paths are not shipped in the archive.

The corresponding-source archive provides the exact px4-userland source and exact libusb source needed to rebuild or
relink the Android artifacts. The libusb 1.0.28 obligation follows LGPL-2.1-or-later section 6(d), with the corresponding
source and relink route provided under section 6(a): the same candidate handoff contains the exact source, verification
hashes, and `BUILD-RELINK.md`. libusb's LGPL terms remain applicable; this contract does not claim that libusb was
converted to GPL under LGPL section 3. The complete px4-userland source is GPL-2.0-only and is rebuildable, so separate
application object files are not required as a relinkable deliverable under this contract.

Primary license text: [libusb 1.0.28 `COPYING`](https://github.com/libusb/libusb/blob/v1.0.28/COPYING).

## Linux and macOS native archives

The Linux archive is x86_64 musl-dynamic. It requires the host interpreter
`/lib/ld-musl-x86_64.so.1` and host-provided shared `libusb-1.0.so.0`. The macOS archive is Apple Silicon and uses
host-provided dynamic libusb. Both native archives also require their host system C++ runtime: Linux commonly needs
Alpine-provided dynamic `libstdc++.so.6`, `libgcc_s.so.1`, and related libraries, while macOS needs system `libc++`.
The actual `NEEDED`/dynamic dependency lists are retained in `evidence/binary-audit.json`. Native archives do not bundle
a libusb or PC/SC client library.

Only `px4d` directly requires libusb. `px4-ts` and `px4ctl` are IPC clients and must not directly require libusb. The
Linux/macOS IFD adapter also communicates with `px4d` over IPC and must not directly require libusb or a PC/SC client
library. A host PC/SC consumer/pcscd loads the IFD adapter through the supplied reader template.

The native dependency claim is verified from each built binary: Linux uses `readelf` for the musl interpreter and shared
libusb, while macOS uses `otool` for host-provided dynamic libusb and the IFD's dependency restrictions. The native
archive notice identifies these as host-provided dynamic dependencies.

Primary PC/SC license reference: [pcsc-lite `COPYING`](https://github.com/LudovicRousseau/PCSC/blob/master/COPYING).
The exact host package versions remain deployment-specific system inputs and are not copied into the native archives.

## CI and archive audit

The local packaging scripts and `.github/workflows/build_userland.yml` implement the same release-candidate contract.
They require explicit already-built platform inputs, strict `N.N.N` version matching, and the exact pinned libusb source
where Android or source packaging needs it. They audit archive allowlists, required files, manifests, checksums, path
traversal, symlinks/hardlinks, firmware, Windows, probe, kernel/DKMS, and vendor content.

The final CI artifact is named `release-candidate` and contains exactly these five archives and the outer `SHA256SUMS`:

- `px4-userland-<version>-linux-x86_64.tar.gz`;
- `px4-userland-<version>-darwin-arm64.tar.gz`;
- `px4-userland-<version>-android-aarch64.tar.gz`;
- `px4-userland-<version>-android-armv7a.tar.gz`;
- `px4-userland-<version>-source.tar.gz`.

This artifact is a candidate handoff, not a Git tag or GitHub Release. Stable acceptance is described in [`SPEC.md`](SPEC.md):
the final archives themselves must pass the HAOS 2-hour soak and the 30-minute-or-longer target-environment tests, including
terrestrial/satellite capture, stop/reopen, USB detach/reconnect, and the Q3U4 internal card path. The receiver 7 reference
comparison and the unloaded-only LNB limitation are recorded as specified there. Stable publication also requires no major
unresolved issue and a pre-publication review.

## CI-only actions

GitHub Actions and their runners are CI infrastructure rather than product dependencies in a release source snapshot.
The workflow uses SHA-pinned checkout, artifact, and NDK setup actions. Their notices and licenses apply to the CI
execution environment; they are not copied into the product archives.

## Firmware is separate and not included

IT930x firmware is not a third-party library used by the source build and is not included as a source file, blob, archive
member, or release artifact. It is a separately supplied runtime input whose licensing and acquisition remain outside this
inventory. The project does not download, extract, or transform vendor firmware.

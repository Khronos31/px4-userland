# AGENTS.md

## Source of truth

- Normative product requirements are in `SPEC.md`. Read the relevant section before implementation.
- If an observed device behavior conflicts with the frozen specification, update its version and rationale before changing the implementation.
- `px4-userland` targets only PLEX PX-Q3U4 (`0511:084a`). Do not claim support for related devices without a versioned specification change and hardware regression coverage.

## Supported scope

- Runtime targets are Linux (including environments where kernel modules cannot be installed), Android/Termux, Android/ad-hoc APK testing, and macOS.
- Every target runtime must support both the Q3U4 tuners and its internal card reader.
- Windows is outside the product scope. Windows users may use `tsukumijima/px4_drv`, which is a separate product with a different CLI and IPC interface.
- The Android APK is an ad-hoc hardware test path, not a release artifact.
- Firmware is not distributed, downloaded, extracted, or transformed by this repository.

## Implementation rules

- Keep the portable core C++17, exception-free and RTTI-free. Do not use glibc extensions or GNU-only APIs required for core functionality.
- Preserve glibc, musl, Bionic API 24+, and macOS portability. Keep platform APIs outside the portable core.
- Use fixed-width integers and checked lengths at USB, firmware, IPC, ATR, APDU, and TS boundaries.
- Preserve existing tests. Do not change expectations, fixtures, mocks, or skips merely to make a failure pass; add tests for new behavior.
- Do not reintroduce Linux kernel modules, chardev/ioctl interfaces, DKMS/Debian packaging, legacy udev rules, Windows artifacts, or non-Q3U4 implementations.
- Physical USB changes, card insertion/removal, antenna changes, power changes, and other hardware operations require user confirmation before execution.
- Preserve existing dirty-tree work. Use `apply_patch` for edits and do not reset, checkout, or broadly reformat unrelated files.
- Do not run `git add`, `git commit`, or `git push` unless the user explicitly requests that operation.
- Create public issues only for unresolved problems known at publication time; do not create preventive placeholder issues.

## Handoff requirements

- Report changed files, commands run, results, and unverified scope at the end of each increment.

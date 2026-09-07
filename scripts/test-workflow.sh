#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Offline guard for packaging workflow invariants.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
workflow=$root/.github/workflows/build_userland.yml
test -f "$workflow"
# shellcheck disable=SC2016
test "$(grep -c 'docker run --rm -e GITHUB_SHA="\$GITHUB_SHA"' "$workflow")" -ge 4
grep -F 'linux-glibc-x86_64' "$workflow" >/dev/null
grep -F 'linux-musl-x86_64' "$workflow" >/dev/null
grep -F 'linux-glibc-aarch64' "$workflow" >/dev/null
grep -F 'linux-musl-aarch64' "$workflow" >/dev/null
test "$(grep -c 'scripts/test-static-relink.sh --libusb-source-archive /src/third_party/libusb-1.0.30.tar.bz2' "$workflow")" -eq 2
test "$(grep -c 'name: Corresponding-source static relink proof' "$workflow")" -eq 2
grep -F 'source-relink-x86_64, source-relink-aarch64' "$workflow" >/dev/null
test "$(grep -c 'packaged-musl-' "$workflow")" -eq 2
test "$(grep -c 'PX4_BUILD_TESTS=ON' "$workflow")" -ge 3
test "$(grep -c 'ctest --test-dir' "$workflow")" -ge 3
if grep -E -- 'package-artifact\.sh --platform linux-(x86_64|aarch64)( |$)' "$workflow" >/dev/null; then
    printf '%s\n' 'workflow contains an ambiguous generic Linux package' >&2
    exit 1
fi
test "$(grep -c 'sudo chown -R' "$workflow")" -ge 2
test "$(grep -c 'apt_install_retry()' "$workflow")" -eq 2
# shellcheck disable=SC2016
test "$(grep -c '\"$attempt\" -le 3' "$workflow")" -eq 2
test "$(grep -c 'find /var/lib/apt/lists -mindepth 1 -depth -delete' "$workflow")" -eq 2
test "$(grep -c 'debian:11@sha256:' "$workflow")" -eq 2
digest_values=$(grep -o 'debian:11@sha256:[0-9a-f]*' "$workflow" | sort -u)
test "$(printf '%s\n' "$digest_values" | grep -c .)" -eq 1
digest=${digest_values#debian:11@sha256:}
case "$digest" in
    *[!0-9a-f]*|'')
        printf '%s\n' 'Debian image digest must be lowercase hexadecimal' >&2
        exit 1
        ;;
esac
test "${#digest}" -eq 64
test "$(grep -c 'http://snapshot.debian.org/archive/debian/20260825T000000Z bullseye main' "$workflow")" -eq 2
test "$(grep -c 'http://snapshot.debian.org/archive/debian-security/20260825T000000Z bullseye-security main' "$workflow")" -eq 2
test "$(grep -c 'check-valid-until=no' "$workflow")" -eq 4
if grep -E 'debian:11([[:space:]]|$)' "$workflow" >/dev/null; then
    printf '%s\n' 'Debian 11 image must be digest-pinned' >&2
    exit 1
fi
if grep -F 'find /var/lib/apt/lists -mindepth 1 -maxdepth 1' "$workflow" >/dev/null; then
    printf '%s\n' 'apt list cleanup must be recursive' >&2
    exit 1
fi
grep -F 'expected two libusb version URLs before replacement' "$root/scripts/test-static-relink.sh" >/dev/null
# shellcheck disable=SC2016
grep -F '[ "$modified_marker_count" -ge 1 ]' "$root/scripts/test-static-relink.sh" >/dev/null
# shellcheck disable=SC2016
grep -F 'Authorization: Bearer $GITHUB_TOKEN' "$root/.github/workflows/check-libusb.yml" >/dev/null
grep -F '7つのbinary archive' "$root/SPEC.md" >/dev/null
if grep -F '8 binary archive' "$root/SPEC.md" >/dev/null || grep -F '5つのbinary archive' "$root/SPEC.md" >/dev/null; then
    printf '%s\n' 'SPEC archive count is stale' >&2
    exit 1
fi
printf '%s\n' 'workflow packaging invariants: PASS'

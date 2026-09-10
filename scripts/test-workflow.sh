#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Offline guard for packaging workflow invariants.
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
workflow=$root/.github/workflows/build_userland.yml
test -f "$workflow"
if "$root/scripts/build-android.sh" --abi unsupported --output "/tmp/px4-userland-invalid-abi-$$"; then
    printf '%s\n' 'build-android.sh accepted an unsupported ABI' >&2
    exit 1
fi
"$root/scripts/verify-android-elf.sh" --self-test >/dev/null
android_block=$(awk '
    $0 == "  android-api-24:" { in_job = 1; next }
    in_job && /^  [^ ]/ { exit }
    in_job { print }
' "$workflow")
printf '%s\n' "$android_block" | grep -F 'abi: x86_64' >/dev/null
printf '%s\n' "$android_block" | grep -F 'platform: android-x86_64' >/dev/null
printf '%s\n' "$android_block" | grep -F 'scripts/package-artifact.sh --platform' >/dev/null
printf '%s\n' "$android_block" | grep -F 'actions/upload-artifact@' >/dev/null
printf '%s\n' "$android_block" | grep -F 'shellcheck packaging/termux/px4-termux' >/dev/null
grep -F 'android-api-24, source-archive' "$workflow" >/dev/null
grep -F 'android-x86_64' "$workflow" >/dev/null
grep -F 'len(actual) != 9' "$workflow" >/dev/null
grep -F 'android_x86_64_archive' "$workflow" >/dev/null
grep -F 'TERMUX_LAUNCHER' "$root/scripts/audit-artifact.py" >/dev/null
grep -F '"packaging" / "termux"' "$root/scripts/package-artifact.py" >/dev/null
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
grep -F '"$strip_tool" --strip-all' "$root/scripts/build-linux-static.sh" >/dev/null
# shellcheck disable=SC2016
grep -F '"$strip_tool" --strip-unneeded' "$root/scripts/build-linux-ifd.sh" >/dev/null
grep -F 'readelf, "-SW"' "$root/scripts/audit-artifact.py" >/dev/null
grep -F 'segname\s+__DWARF' "$root/scripts/audit-artifact.py" >/dev/null
grep -F 'nlocalsym' "$root/scripts/audit-artifact.py" >/dev/null
if grep -F '"-N"' "$root/scripts/package-artifact.py" >/dev/null; then
    printf '%s\n' 'Darwin packaging must not use strip -N' >&2
    exit 1
fi
grep -F '"-S", "-x"' "$root/scripts/package-artifact.py" >/dev/null
grep -F 'IFD_EXPORTS' "$root/scripts/audit-artifact.py" >/dev/null
grep -F 'strip_darwin_stage' "$root/scripts/package-artifact.py" >/dev/null
grep -F 'command -v strip' "$workflow" >/dev/null
grep -F 'apt-get install --no-install-recommends -y binutils cmake ninja-build' "$workflow" >/dev/null
grep -F 'Install native ELF audit tools' "$workflow" >/dev/null
# shellcheck disable=SC2016
grep -F 'Authorization: Bearer $GITHUB_TOKEN' "$root/.github/workflows/check-libusb.yml" >/dev/null
grep -F '8つのbinary archive' "$root/SPEC.md" >/dev/null
grep -F 'access=@PX4_ACCESS@' "$root/packaging/pcsc/reader.conf.d/px4-userland.conf.in" >/dev/null
grep -F 'sudo install -d -o root -g pcscd -m 0750 /run/px4-userland' "$root/README.md" >/dev/null
grep -F 'mktemp -d' "$root/README.md" >/dev/null
grep -F 'px4-userland.XXXXXX' "$root/README.md" >/dev/null
grep -F 'px4d_pid=$!' "$root/README.md" >/dev/null
grep -F ' -lt 30' "$root/README.md" >/dev/null
grep -F 'rmdir ' "$root/README.md" >/dev/null
grep -F 'trap cleanup EXIT' "$root/README.md" >/dev/null
if grep -F 'rm -rf' "$root/README.md" >/dev/null; then
    printf '%s\n' 'README private runtime cleanup must remain narrow' >&2
    exit 1
fi
grep -F 'px4-ts --device BASE_SERIAL --receiver 0..7 --system isdb-t|isdb-s --frequency-khz N [--runtime-dir PATH] [--group]' "$root/README.md" >/dev/null
grep -F '3つすべてに' "$root/README.md" >/dev/null
grep -F '完全静的CLI + glibc/musl別IFD' "$root/README.md" >/dev/null
test "$(grep -c 'name: Release candidate Linux x86_64 Ubuntu artifact smoke' "$workflow")" -eq 1
test "$(grep -c 'name: Release candidate Linux x86_64 Alpine artifact smoke' "$workflow")" -eq 1
test "$(grep -c 'name: Release candidate Linux aarch64 Ubuntu artifact smoke' "$workflow")" -eq 1
test "$(grep -c 'name: Release candidate Linux aarch64 Alpine artifact smoke' "$workflow")" -eq 1
test "$(grep -c 'name: Run exact candidate static CLIs and glibc IFD on Ubuntu$' "$workflow")" -eq 1
test "$(grep -c 'name: Run exact candidate static CLIs and glibc IFD on Ubuntu arm64' "$workflow")" -eq 1
test "$(grep -c 'name: Run exact candidate static CLIs and musl IFD in Alpine$' "$workflow")" -eq 1
test "$(grep -c 'name: Run exact candidate static CLIs and musl IFD in Alpine arm64' "$workflow")" -eq 1
test "$(grep -c 'needs: release-candidate' "$workflow")" -eq 4
for job in \
    release-candidate-linux-ubuntu-x86_64 \
    release-candidate-linux-alpine-x86_64 \
    release-candidate-linux-ubuntu-aarch64 \
    release-candidate-linux-alpine-aarch64; do
    block=$(awk -v job="$job" '
        $0 == "  " job ":" { in_job = 1; next }
        in_job && /^  [^ ]/ { exit }
        in_job { print }
    ' "$workflow")
    checkout_line=$(printf '%s\n' "$block" | grep -n 'actions/checkout@' | head -n 1 | cut -d: -f1)
    download_line=$(printf '%s\n' "$block" | grep -n 'actions/download-artifact@' | head -n 1 | cut -d: -f1)
    test -n "$checkout_line" && test -n "$download_line" && test "$checkout_line" -lt "$download_line"
done
if grep -F '7つのbinary archive' "$root/SPEC.md" >/dev/null || grep -F '5つのbinary archive' "$root/SPEC.md" >/dev/null; then
    printf '%s\n' 'SPEC archive count is stale' >&2
    exit 1
fi
printf '%s\n' 'workflow packaging invariants: PASS'

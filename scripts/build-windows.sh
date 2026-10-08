#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Cross-build the Windows x64 (Phase 1) production CLIs with a pinned
# llvm-mingw/UCRT toolchain. The toolchain and dependency versions and their
# SHA-256 values are fixed here; no compiler or dependency binary is vendored
# into the tracked tree. The PE/import audit and the deterministic zip are not
# done here: they are the integrated interfaces in audit-artifact.py and
# package-artifact.py.
set -eu

# Fixed timestamp so the libusb DLL and PE headers are reproducible.
SOURCE_DATE_EPOCH=1700000000
export SOURCE_DATE_EPOCH

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
output=
test_output=
cache=${PX4_WINDOWS_CACHE:-"$root/.windows-toolchain"}
skip_toolchain_download=0
usage() {
    printf '%s\n' "usage: $0 --output DIR [--test-output DIR] [--cache DIR] [--skip-toolchain-download]"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
    --output) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; output=$2; shift 2 ;;
    --test-output) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; test_output=$2; shift 2 ;;
    --cache) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; cache=$2; shift 2 ;;
    --skip-toolchain-download) skip_toolchain_download=1; shift ;;
    --help) usage; exit 0 ;;
    *) printf '%s\n' "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done
[ -n "$output" ] || { usage >&2; exit 2; }

# Pinned inputs. Update together with the release record.
llvm_mingw_version=20250910
llvm_mingw_archive="llvm-mingw-${llvm_mingw_version}-ucrt-ubuntu-22.04-x86_64.tar.xz"
llvm_mingw_sha256=f83556c9ffa4d4291fadea1a0776c1383332dacdf4d7fbdf974c2928cb32c6f7
llvm_mingw_url="https://github.com/mstorsjo/llvm-mingw/releases/download/${llvm_mingw_version}/${llvm_mingw_archive}"

libusb_version=1.0.30
libusb_archive="libusb-${libusb_version}.tar.bz2"
libusb_sha256=fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf
libusb_url="https://github.com/libusb/libusb/releases/download/v${libusb_version}/${libusb_archive}"

command -v cmake >/dev/null || { printf '%s\n' 'cmake is required' >&2; exit 1; }
command -v sha256sum >/dev/null || { printf '%s\n' 'sha256sum is required' >&2; exit 1; }
command -v make >/dev/null || { printf '%s\n' 'make is required' >&2; exit 1; }
if command -v ninja >/dev/null; then make_program=$(command -v ninja)
elif command -v samurai >/dev/null; then make_program=$(command -v samurai)
elif command -v samu >/dev/null; then make_program=$(command -v samu)
else printf '%s\n' 'ninja or samurai is required' >&2; exit 1
fi

mkdir -p "$output" "$cache"
output=$(CDPATH='' cd -- "$output" && pwd)
cache=$(CDPATH='' cd -- "$cache" && pwd)
[ -z "$test_output" ] || { mkdir -p "$test_output"; test_output=$(CDPATH='' cd -- "$test_output" && pwd); }

work=$(mktemp -d /tmp/px4-windows.XXXXXX)
cleanup() { find "$work" -depth -delete; }
trap cleanup EXIT HUP INT TERM

# The pinned archive is verified on every run, and the compiler tree is always
# freshly extracted from that verified archive into this task's workspace. The
# cache therefore only holds the archive; a stale or tampered extracted tree in
# the cache is never trusted.
llvm_archive="$cache/$llvm_mingw_archive"
if [ ! -f "$llvm_archive" ]; then
    [ "$skip_toolchain_download" -eq 0 ] || {
        printf '%s\n' "toolchain archive not present at $llvm_archive; cannot verify the pinned toolchain" >&2
        exit 1
    }
    command -v curl >/dev/null || { printf '%s\n' 'curl is required to fetch the toolchain' >&2; exit 1; }
    curl -fsSL --retry 2 -o "$llvm_archive" "$llvm_mingw_url"
fi
actual=$(sha256sum "$llvm_archive" | awk '{print $1}')
[ "$actual" = "$llvm_mingw_sha256" ] || {
    printf '%s\n' "llvm-mingw checksum mismatch: $actual" >&2
    exit 1
}
mkdir -p "$work/toolchain"
tar -xf "$llvm_archive" -C "$work/toolchain"
toolchain_dir="$work/toolchain/llvm-mingw-${llvm_mingw_version}-ucrt-ubuntu-22.04-x86_64"
[ -d "$toolchain_dir" ] || {
    printf '%s\n' 'llvm-mingw archive did not expand as expected' >&2
    exit 1
}

# The narrow llvm-mingw runtime license texts shipped in the Windows archive
# must be the exact texts of this checksum-verified toolchain. Compare the
# tracked copies against the freshly extracted archive and fail on any drift.
licenses_root="$root/packaging/windows-toolchain/licenses"
for license in \
    LICENSE.TXT \
    mingw32/COPYING \
    mingw32/COPYING.MinGW-w64-runtime.txt \
    mingw32/COPYING.MinGW-w64.txt \
    mingw32/COPYING.winpthreads.txt \
    mingw32/COPYING.winstorecompat.txt; do
    case "$license" in
    LICENSE.TXT) extracted="$toolchain_dir/LICENSE.TXT" ;;
    *) extracted="$toolchain_dir/x86_64-w64-mingw32/share/$license" ;;
    esac
    [ -f "$licenses_root/$license" ] || {
        printf '%s\n' "tracked toolchain license is missing: $licenses_root/$license" >&2
        exit 1
    }
    cmp -s "$licenses_root/$license" "$extracted" || {
        printf '%s\n' "tracked toolchain license differs from the verified toolchain: $license" >&2
        exit 1
    }
done

toolchain_bin="$toolchain_dir/bin"
[ -x "$toolchain_bin/x86_64-w64-mingw32-clang++" ] || {
    printf '%s\n' "llvm-mingw C++ compiler missing under $toolchain_bin" >&2
    exit 1
}
PATH="$toolchain_bin:$PATH"
export PATH

# Build a shared libusb with the same pinned toolchain. A same-toolchain DLL
# avoids static LGPL relink expansion and keeps the import table auditable.
# The source path is pinned out of the DLL so the PE audit can reject leaks.
libusb_archive_path="$cache/$libusb_archive"
if [ ! -f "$libusb_archive_path" ]; then
    [ "$skip_toolchain_download" -eq 0 ] || {
        printf '%s\n' "libusb archive not present at $libusb_archive_path" >&2
        exit 1
    }
    command -v curl >/dev/null || { printf '%s\n' 'curl is required to fetch libusb' >&2; exit 1; }
    curl -fsSL --retry 2 -o "$libusb_archive_path" "$libusb_url"
fi
actual=$(sha256sum "$libusb_archive_path" | awk '{print $1}')
[ "$actual" = "$libusb_sha256" ] || {
    printf '%s\n' "libusb checksum mismatch: $actual" >&2
    exit 1
}
mkdir -p "$work/libusb"
tar -xjf "$libusb_archive_path" -C "$work/libusb" --strip-components=1
[ -x "$work/libusb/configure" ] || {
    printf '%s\n' 'libusb release archive does not contain a configure script' >&2
    exit 1
}
(
    cd "$work/libusb" &&
        ./configure --host=x86_64-w64-mingw32 --prefix="$work/libusb-prefix" \
            --enable-shared --disable-static --disable-udev \
            --disable-examples-build --disable-tests-build \
            CC="$toolchain_bin/x86_64-w64-mingw32-clang" \
            AR="$toolchain_bin/llvm-ar" RANLIB="$toolchain_bin/llvm-ranlib" \
            CFLAGS="-O2 -g0 -ffile-prefix-map=$work=. -fdebug-prefix-map=$work=. -fmacro-prefix-map=$work=." \
            LDFLAGS="-Wl,-s -Wl,--no-insert-timestamp -Wl,--build-id=none" &&
        make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)" &&
        make install
)
libusb_include="$work/libusb-prefix/include/libusb-1.0"
libusb_dll="$work/libusb-prefix/bin/libusb-1.0.dll"
libusb_implib="$work/libusb-prefix/lib/libusb-1.0.dll.a"
if [ ! -f "$libusb_dll" ] || [ ! -f "$libusb_implib" ] || [ ! -f "$libusb_include/libusb.h" ]; then
    printf '%s\n' 'shared libusb DLL/import library was not produced' >&2
    exit 1
fi

configure_build() {
    build_dir=$1
    tests=$2
    libusb=$3
    cmake -S "$root" -B "$build_dir" -G Ninja -DCMAKE_MAKE_PROGRAM="$make_program" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
        -DCMAKE_C_COMPILER="$toolchain_bin/x86_64-w64-mingw32-clang" \
        -DCMAKE_CXX_COMPILER="$toolchain_bin/x86_64-w64-mingw32-clang++" \
        -DCMAKE_RC_COMPILER="$toolchain_bin/llvm-rc" \
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,-s" \
        -DPX4_BUILD_TESTS="$tests" -DPX4_BUILD_PCSC_IFD=OFF -DPX4_ENABLE_LIBUSB="$libusb" \
        -DPX4_LIBUSB_INCLUDE_DIR="$libusb_include" \
        -DPX4_LIBUSB_LIBRARY="$libusb_implib"
}

build="$work/build"
configure_build "$build" OFF ON
cmake --build "$build" --target px4d px4-ts px4ctl

for program in px4d px4-ts px4ctl; do
    [ -f "$build/$program.exe" ] || {
        printf '%s\n' "expected executable $program.exe was not produced" >&2
        exit 1
    }
done
cp "$libusb_dll" "$output/libusb-1.0.dll"
for program in px4d px4-ts px4ctl; do
    cp "$build/$program.exe" "$output/$program.exe"
done

if [ -n "$test_output" ]; then
    test_build="$work/build-tests"
    configure_build "$test_build" ON OFF
    cmake --build "$test_build"
    for test_program in "$test_build"/*_tests.exe; do
        [ -f "$test_program" ] || continue
        cp "$test_program" "$test_output/"
    done
    [ -f "$test_output/px4_windows_tests.exe" ] || {
        printf '%s\n' 'Windows test executables were not produced' >&2
        exit 1
    }
    cp "$libusb_dll" "$test_output/libusb-1.0.dll"
fi

printf '%s\n' "Windows Phase 1 build staged in $output"
[ -z "$test_output" ] || printf '%s\n' "Windows Phase 1 tests staged in $test_output"

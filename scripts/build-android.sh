#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Build the Android API-24 command-line artifacts with isolated static libusb.
set -eu

api=24
abi=aarch64
output=

usage()
{
    printf '%s\n' "usage: $0 --abi aarch64|arm64-v8a|armv7a|armeabi-v7a --output DIR"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
    --abi)
        [ "$#" -ge 2 ] || { usage >&2; exit 2; }
        abi=$2
        shift 2
        ;;
    --output)
        [ "$#" -ge 2 ] || { usage >&2; exit 2; }
        output=$2
        shift 2
        ;;
    --help)
        usage
        exit 0
        ;;
    *)
        printf '%s\n' "unknown argument: $1" >&2
        usage >&2
        exit 2
        ;;
    esac
done

[ -n "$output" ] || { printf '%s\n' '--output is required' >&2; exit 2; }
case "$abi" in
arm64-v8a|aarch64)
    abi=arm64-v8a
    clang_triple=aarch64-linux-android${api}
    autotools_host=aarch64-linux-android
    expected_interpreter=/system/bin/linker64
    ;;
armeabi-v7a|armv7a)
    abi=armeabi-v7a
    clang_triple=armv7a-linux-androideabi${api}
    autotools_host=armv7a-linux-androideabi
    expected_interpreter=/system/bin/linker
    ;;
*)
    printf '%s\n' "unsupported ABI: $abi" >&2
    exit 2
    ;;
esac

ndk=${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}
[ -n "$ndk" ] && [ -d "$ndk" ] || {
    printf '%s\n' 'ANDROID_NDK_HOME must point to NDK r26 or newer' >&2
    exit 1
}
revision=$(sed -n 's/^Pkg.Revision = //p' "$ndk/source.properties" | head -n 1)
major=${revision%%.*}
case "$major" in
''|*[!0-9]*)
    printf '%s\n' "cannot determine NDK revision: $ndk/source.properties" >&2
    exit 1
    ;;
esac
[ "$major" -ge 26 ] || {
    printf '%s\n' "NDK r26+ is required, got $revision" >&2
    exit 1
}

case "$(uname -s)-$(uname -m)" in
Linux-x86_64) prebuilt=linux-x86_64 ;;
Linux-aarch64) prebuilt=linux-aarch64 ;;
Darwin-arm64) prebuilt=darwin-arm64 ;;
Darwin-x86_64) prebuilt=darwin-x86_64 ;;
*) printf '%s\n' "unsupported build host: $(uname -s) $(uname -m)" >&2; exit 1 ;;
esac

toolchain=$ndk/toolchains/llvm/prebuilt/$prebuilt
cc=$toolchain/bin/${clang_triple}-clang
cxx=$toolchain/bin/${clang_triple}-clang++
ar=$toolchain/bin/llvm-ar
ranlib=$toolchain/bin/llvm-ranlib
strip=$toolchain/bin/llvm-strip
nm=$toolchain/bin/llvm-nm
for tool in "$cc" "$cxx" "$ar" "$ranlib" "$strip" "$nm"; do
    [ -x "$tool" ] || { printf '%s\n' "missing NDK tool: $tool" >&2; exit 1; }
done

root=$(cd -- "$(dirname "$0")/.." && pwd)
cmake_bin=$(command -v cmake)
ninja_bin=$(command -v ninja)
[ -x "$cmake_bin" ] || { printf '%s\n' 'cmake is required' >&2; exit 1; }
[ -x "$ninja_bin" ] || { printf '%s\n' 'ninja is required' >&2; exit 1; }
host_path=$(dirname "$cmake_bin"):$(dirname "$ninja_bin"):/usr/bin:/bin
if [ -d "$output" ]; then
    output=$(cd -- "$output" && pwd)
else
    mkdir -p "$output"
    output=$(cd -- "$output" && pwd)
fi
work=$(mktemp -d /tmp/px4-userland-android-2b2.XXXXXX)
publish_probe_tmp=
publish_daemon_tmp=
publish_control_tmp=
cleanup()
{
    if [ -d "$work" ]; then
        find "$work" -depth -delete
    fi
    for temporary in "$publish_probe_tmp" "$publish_daemon_tmp" "$publish_control_tmp"; do
        if [ -n "$temporary" ] && [ -e "$temporary" ]; then
            find "$temporary" -delete
        fi
    done
}
trap cleanup EXIT HUP INT TERM
prefix=$work/prefix
archive=$work/libusb-1.0.28.tar.bz2
libusb_source=$work/libusb-1.0.28
cmake_build=$work/cmake
output_probe=$output/px4-ts-probe-$abi
output_daemon=$output/px4d-$abi
output_control=$output/px4ctl-$abi
mkdir -p "$prefix" "$work/tmp"

libusb_url=https://github.com/libusb/libusb/releases/download/v1.0.28/libusb-1.0.28.tar.bz2
libusb_sha256=966bb0d231f94a474eaae2e67da5ec844d3527a1f386456394ff432580634b29
curl -fsSL --retry 2 -o "$archive" "$libusb_url"
if command -v sha256sum >/dev/null 2>&1; then
    actual=$(sha256sum "$archive" | awk '{print $1}')
else
    actual=$(shasum -a 256 "$archive" | awk '{print $1}')
fi
[ "$actual" = "$libusb_sha256" ] || {
    printf '%s\n' "libusb checksum mismatch: $actual" >&2
    exit 1
}
tar -xjf "$archive" -C "$work"

jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '%s' 2)
env -i \
    PATH="$host_path" \
    TMPDIR="$work/tmp" \
    LC_ALL=C \
    CC="$cc" \
    CXX="$cxx" \
    AR="$ar" \
    RANLIB="$ranlib" \
    CFLAGS='-O2 -fPIC' \
    LDFLAGS='-fPIC' \
    PKG_CONFIG=/bin/false \
    /bin/sh -c "
        set -eu
        cd \"$libusb_source\"
        ./configure \\
            --host=$autotools_host \\
            --prefix=\"$prefix\" \\
            --libdir=\"$prefix/lib\" \\
            --disable-shared \\
            --enable-static \\
            --with-pic \\
            --disable-udev \\
            --disable-examples-build \\
            --disable-tests-build \\
            --disable-dependency-tracking
        make -j$jobs
        make install
    "

[ -f "$prefix/lib/libusb-1.0.a" ] || {
    printf '%s\n' 'static libusb archive was not produced' >&2
    exit 1
}
if find "$prefix/lib" -maxdepth 1 -type f \( -name '*.so' -o -name '*.dylib' \) | grep -q .; then
    printf '%s\n' 'shared libusb output is forbidden' >&2
    exit 1
fi

ndk_cmake=$ndk/build/cmake/android.toolchain.cmake
env -i \
    PATH=/usr/bin:/bin \
    TMPDIR="$work/tmp" \
    LC_ALL=C \
    ANDROID_NDK_HOME="$ndk" \
    PKG_CONFIG=/bin/false \
    "$cmake_bin" -S "$root" -B "$cmake_build" \
        -G Ninja \
        -DCMAKE_MAKE_PROGRAM="$ninja_bin" \
        -DCMAKE_TOOLCHAIN_FILE="$ndk_cmake" \
        -DANDROID_ABI="$abi" \
        -DANDROID_PLATFORM=android-$api \
        -DCMAKE_ANDROID_STL_TYPE=c++_static \
        -DCMAKE_BUILD_TYPE=Release \
        -DPX4_ENABLE_LIBUSB=ON \
        -DPX4_BUILD_TESTS=OFF \
        -DPX4_BUILD_TOOLS=ON \
        -DPX4_BUILD_ANDROID_LINKCHECK=ON \
        -DPX4_LIBUSB_INCLUDE_DIR="$prefix/include/libusb-1.0" \
        -DPX4_LIBUSB_LIBRARY="$prefix/lib/libusb-1.0.a"
env -i \
    PATH="$host_path" \
    TMPDIR="$work/tmp" \
    LC_ALL=C \
    "$cmake_bin" --build "$cmake_build" \
        --target px4-ts-probe px4d px4ctl -j"$jobs"

for binary in px4-ts-probe px4d px4ctl; do
    "$root/scripts/verify-android-elf.sh" \
        "$cmake_build/$binary" "$expected_interpreter"
done

verify_static_libusb()
{
    binary=$1
    nm_output=$("$nm" "$binary")
    for symbol in libusb_init libusb_wrap_sys_device libusb_close; do
        printf '%s\n' "$nm_output" | grep -E "[[:space:]]$symbol$" >/dev/null || {
            printf '%s\n' "missing static libusb symbol in $binary: $symbol" >&2
            exit 1
        }
    done
}
verify_static_libusb "$cmake_build/px4-ts-probe"
verify_static_libusb "$cmake_build/px4d"

# Copy all verified artifacts to adjacent temporary files first.  Each final
# rename is atomic and no published artifact is touched before all copies pass.
publish_probe_tmp=$(mktemp "$output_probe.tmp.XXXXXX")
publish_daemon_tmp=$(mktemp "$output_daemon.tmp.XXXXXX")
publish_control_tmp=$(mktemp "$output_control.tmp.XXXXXX")
cp "$cmake_build/px4-ts-probe" "$publish_probe_tmp"
cp "$cmake_build/px4d" "$publish_daemon_tmp"
cp "$cmake_build/px4ctl" "$publish_control_tmp"
chmod 0755 "$publish_probe_tmp" "$publish_daemon_tmp" "$publish_control_tmp"
mv -f "$publish_probe_tmp" "$output_probe"
publish_probe_tmp=
mv -f "$publish_daemon_tmp" "$output_daemon"
publish_daemon_tmp=
mv -f "$publish_control_tmp" "$output_control"
publish_control_tmp=
printf '%s\n' "built $output_probe"
printf '%s\n' "built $output_daemon"
printf '%s\n' "built $output_control"

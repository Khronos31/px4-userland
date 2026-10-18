#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Build the PC/SC IFD Handler for the host libc, separately from static CLIs.
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
libc=
output=
usage() { printf '%s\n' "usage: $0 --libc glibc|musl --output DIR"; }
while [ "$#" -gt 0 ]; do
    case "$1" in
    --libc) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; libc=$2; shift 2 ;;
    --output) [ "$#" -ge 2 ] || { usage >&2; exit 2; }; output=$2; shift 2 ;;
    --help) usage; exit 0 ;;
    *) printf '%s\n' "unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done
case "$libc" in glibc|musl) ;; *) printf '%s\n' '--libc must be glibc or musl' >&2; exit 2 ;; esac
[ -n "$output" ] || { usage >&2; exit 2; }
command -v cmake >/dev/null || { printf '%s\n' 'cmake is required' >&2; exit 1; }
make_program=
if command -v ninja >/dev/null; then make_program=$(command -v ninja)
elif command -v samu >/dev/null; then make_program=$(command -v samu)
elif command -v samurai >/dev/null; then make_program=$(command -v samurai)
else printf '%s\n' 'ninja or samurai is required' >&2; exit 1
fi
cxx_compiler=${CXX:-c++}
cxx_compiler=$(command -v "$cxx_compiler" || true)
[ -n "$cxx_compiler" ] || { printf '%s\n' 'the selected C++ compiler is unavailable' >&2; exit 1; }
cxx_compiler=$(readlink -f "$cxx_compiler")
nm_tool=$(command -v nm || command -v llvm-nm || true)
strip_tool=$(command -v strip || command -v llvm-strip || true)
[ -n "$nm_tool" ] || { printf '%s\n' 'target-native nm is required' >&2; exit 1; }
[ -n "$strip_tool" ] || { printf '%s\n' 'target-native strip is required' >&2; exit 1; }
mkdir -p "$output"
output=$(CDPATH='' cd -- "$output" && pwd)
work=$(mktemp -d /tmp/px4-linux-ifd.XXXXXX)
cleanup() { find "$work" -depth -delete; }
trap cleanup EXIT HUP INT TERM
if [ "$libc" = glibc ]; then
    floor=$(getconf GNU_LIBC_VERSION 2>/dev/null || true)
    case "$floor" in glibc\ 2.31) : ;; *)
        printf '%s\n' "glibc baseline 2.31 required; detected ${floor:-unknown}" >&2
        exit 1
        esac
fi
cmake -S "$root" -B "$work/build" -G Ninja -DCMAKE_MAKE_PROGRAM="$make_program" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_COMPILER="$cxx_compiler" \
    -DPX4_BUILD_TESTS=OFF -DPX4_ENABLE_LIBUSB=OFF \
    -DPX4_BUILD_PCSC_IFD=ON -DPX4_REQUIRE_PCSC_IFD=ON \
    -DCMAKE_CXX_FLAGS='-fPIC -static-libstdc++ -static-libgcc' \
    -DCMAKE_SHARED_LINKER_FLAGS='-static-libstdc++ -static-libgcc'
cmake --build "$work/build" --target px4_ifdhandler
library=$work/build/libpx4-userland-ifd.so
verify_ifd_exports()
{
    symbols=$("$nm_tool" -D --defined-only "$1")
    for symbol in \
        IFDHCreateChannel IFDHCreateChannelByName IFDHCloseChannel \
        IFDHGetCapabilities IFDHSetCapabilities IFDHSetProtocolParameters \
        IFDHPowerICC IFDHTransmitToICC IFDHControl IFDHICCPresence; do
        printf '%s\n' "$symbols" | grep -E "[[:space:]]_?$symbol$" >/dev/null || {
            printf '%s\n' "missing exported IFD symbol: $symbol" >&2
            exit 1
        }
    done
}

# Prove the required IFD ABI before stripping, then preserve it through the
# release operation.  --strip-unneeded removes DWARF/.symtab without dropping
# symbols retained in the dynamic export set.
verify_ifd_exports "$library"
"$strip_tool" --strip-unneeded "$library"
verify_ifd_exports "$library"
cp "$library" "$output/px4-userland-ifd.so"
chmod 0755 "$output/px4-userland-ifd.so"
mkdir -p "$output/evidence"
runtime_input()
{
    input=$($cxx_compiler -print-file-name="$1")
    [ -f "$input" ] || { printf '%s\n' "selected compiler has no $1 input: $input" >&2; exit 1; }
    input=$(readlink -f "$input")
    if [ "$libc" = glibc ]; then
        owner=$(dpkg-query -S "$input" 2>/dev/null | head -n 1 | cut -d: -f 1)
        version=$(dpkg-query -W -f='${Version}' "$owner" 2>/dev/null || true)
    else
        owner=$(apk info -W "$input" 2>/dev/null | sed -n 's/.* is owned by //p' | head -n 1)
        version=$owner
    fi
    [ -n "$owner" ] && [ -n "$version" ] || {
        printf '%s\n' "runtime input has no selected toolchain package owner: $input" >&2
        exit 1
    }
    printf '%s|%s|%s|%s\n' "$input" "$(sha256sum "$input" | awk '{print $1}')" "$owner" "$version"
}
libc_info=$(runtime_input libc.so)
libgcc_input=$(runtime_input libgcc.a)
libstdcxx_input=$(runtime_input libstdc++.a)
if [ "$libc" = glibc ]; then
    libc_version=$(getconf GNU_LIBC_VERSION)
else
    libc_version=$(apk info -v musl 2>/dev/null | head -n 1)
fi
cat > "$output/evidence/toolchain-ifd.properties" <<EOF
compiler=$cxx_compiler
compiler_sha256=$(sha256sum "$cxx_compiler" | awk '{print $1}')
compiler_version=$($cxx_compiler -dumpfullversion -dumpversion)
compiler_target=$($cxx_compiler -dumpmachine)
libc=$libc
libc_version=$libc_version
libc_input=$(printf '%s' "$libc_info" | cut -d '|' -f 1)
libc_input_sha256=$(printf '%s' "$libc_info" | cut -d '|' -f 2)
libc_input_owner=$(printf '%s' "$libc_info" | cut -d '|' -f 3)
libc_input_package_version=$(printf '%s' "$libc_info" | cut -d '|' -f 4)
libgcc_archive=$(printf '%s' "$libgcc_input" | cut -d '|' -f 1)
libgcc_archive_sha256=$(printf '%s' "$libgcc_input" | cut -d '|' -f 2)
libgcc_archive_owner=$(printf '%s' "$libgcc_input" | cut -d '|' -f 3)
libgcc_package_version=$(printf '%s' "$libgcc_input" | cut -d '|' -f 4)
libstdcxx_archive=$(printf '%s' "$libstdcxx_input" | cut -d '|' -f 1)
libstdcxx_archive_sha256=$(printf '%s' "$libstdcxx_input" | cut -d '|' -f 2)
libstdcxx_archive_owner=$(printf '%s' "$libstdcxx_input" | cut -d '|' -f 3)
libstdcxx_package_version=$(printf '%s' "$libstdcxx_input" | cut -d '|' -f 4)
runtime=libgcc+libstdc++
linkage=shared-ifd-static-gcc-runtime
EOF

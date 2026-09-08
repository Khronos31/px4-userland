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

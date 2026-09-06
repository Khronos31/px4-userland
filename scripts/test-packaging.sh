#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
python3 "$script_dir/audit-artifact.py" --self-test
python3 "$script_dir/audit-artifact.py" --help | grep -F -- '--ndk-root' >/dev/null
python3 "$script_dir/package-artifact.py" --self-test
python3 "$script_dir/android-link-inventory.py" --help >/dev/null
test_root=$(mktemp -d /tmp/px4-package-self-test.XXXXXX)
trap 'find "$test_root" -depth -delete' EXIT
version=$(tr -d '\n' < "$script_dir/../VERSION")
mkdir -p "$test_root/build"
printf '%s\n' px4d px4-ts px4ctl | while IFS= read -r program; do
    printf '%s\n' synthetic >"$test_root/build/$program"
    chmod 0755 "$test_root/build/$program"
done
printf '%s\n' synthetic >"$test_root/build/ifd.so"
chmod 0755 "$test_root/build/ifd.so"
mkdir -p "$test_root/ifd.bundle/Contents/MacOS"
printf '%s\n' synthetic >"$test_root/ifd.bundle/Contents/Info.plist"
printf '%s\n' synthetic >"$test_root/ifd.bundle/Contents/MacOS/libpx4-userland-ifd.dylib"
PATH="$script_dir/testdata:$PATH" python3 "$script_dir/package-artifact.py" \
    --platform linux-x86_64 --version "$version" --build-dir "$test_root/build" \
    --ifd-library "$test_root/build/ifd.so" \
    --reader-template "$script_dir/../packaging/pcsc/reader.conf.d/px4-userland.conf.in" \
    --output-dir "$test_root/out"
PATH="$script_dir/testdata:$PATH" python3 "$script_dir/package-artifact.py" \
    --platform linux-x86_64 --version "$version" --build-dir "$test_root/build" \
    --ifd-library "$test_root/build/ifd.so" \
    --reader-template "$script_dir/../packaging/pcsc/reader.conf.d/px4-userland.conf.in" \
    --output-dir "$test_root/out-second"
first_sha=$(sha256sum "$test_root/out/px4-userland-$version-linux-x86_64.tar.gz" | awk '{print $1}')
second_sha=$(sha256sum "$test_root/out-second/px4-userland-$version-linux-x86_64.tar.gz" | awk '{print $1}')
[ "$first_sha" = "$second_sha" ] || {
    printf '%s\n' 'deterministic package self-test failed' >&2
    exit 1
}
if tar -xOzf "$test_root/out/px4-userland-$version-linux-x86_64.tar.gz" evidence/binary-audit.json | grep -F "$test_root" >/dev/null; then
    printf '%s\n' 'binary-audit.json leaked temporary input path' >&2
    exit 1
fi
PATH="$script_dir/testdata:$PATH" python3 "$script_dir/package-artifact.py" \
    --platform darwin-arm64 --version "$version" --build-dir "$test_root/build" \
    --ifd-bundle "$test_root/ifd.bundle" \
    --reader-template "$script_dir/../packaging/pcsc/reader.conf.d/px4-userland.conf.in" \
    --output-dir "$test_root/out-macos"
PX4_TEST_ARCH=aarch64 PATH="$script_dir/testdata:$PATH" python3 "$script_dir/package-artifact.py" \
    --platform linux-aarch64 --version "$version" --build-dir "$test_root/build" \
    --ifd-library "$test_root/build/ifd.so" \
    --reader-template "$script_dir/../packaging/pcsc/reader.conf.d/px4-userland.conf.in" \
    --output-dir "$test_root/out-aarch64"
PX4_TEST_ARCH=aarch64 PATH="$script_dir/testdata:$PATH" python3 "$script_dir/audit-artifact.py" \
    --platform linux-aarch64 \
    --archive "$test_root/out-aarch64/px4-userland-$version-linux-aarch64.tar.gz"
if PATH="$script_dir/testdata:$PATH" PX4_TEST_LINKAGE=bad-all \
    python3 "$script_dir/audit-artifact.py" --platform linux-x86_64 --build-dir "$test_root/build" \
    --ifd-library "$test_root/build/ifd.so"; then
    printf '%s\n' 'negative Linux libusb linkage test failed' >&2
    exit 1
fi
if PATH="$script_dir/testdata:$PATH" PX4_TEST_OTOOL_MODE=bad-pcsc \
    python3 "$script_dir/audit-artifact.py" --platform darwin-arm64 --build-dir "$test_root/build" \
    --ifd-bundle "$test_root/ifd.bundle"; then
    printf '%s\n' 'negative macOS PCSC linkage test failed' >&2
    exit 1
fi
if PATH="$script_dir/testdata:$PATH" PX4_TEST_OTOOL_MODE=bad-ifd-libusb \
    python3 "$script_dir/audit-artifact.py" --platform darwin-arm64 --build-dir "$test_root/build" \
    --ifd-bundle "$test_root/ifd.bundle"; then
    printf '%s\n' 'negative macOS IFD libusb linkage test failed' >&2
    exit 1
fi
real_android_platform=${PX4_REAL_ANDROID_PLATFORM:-}
real_android_build_dir=${PX4_REAL_ANDROID_BUILD_DIR:-}
real_android_link_map_dir=${PX4_REAL_ANDROID_LINK_MAP_DIR:-}
real_android_ndk_root=${PX4_REAL_ANDROID_NDK_ROOT:-}
real_android_libusb_archive=${PX4_REAL_ANDROID_LIBUSB_SOURCE_ARCHIVE:-}
real_android_binary_suffix=${PX4_REAL_ANDROID_BINARY_SUFFIX:-}
if [ -n "$real_android_platform$real_android_build_dir$real_android_link_map_dir$real_android_ndk_root$real_android_libusb_archive" ]; then
    for required_input in \
        "$real_android_platform" "$real_android_build_dir" "$real_android_link_map_dir" \
        "$real_android_ndk_root" "$real_android_libusb_archive"; do
        [ -n "$required_input" ] || {
            printf '%s\n' 'incomplete PX4_REAL_ANDROID_* input set' >&2
            exit 1
        }
    done
    case "$real_android_platform" in
    android-aarch64|android-armv7a) ;;
    *)
        printf '%s\n' "unsupported PX4_REAL_ANDROID_PLATFORM: $real_android_platform" >&2
        exit 1
        ;;
    esac
    real_android_version=$(python3 - "$script_dir/../VERSION" <<'PY'
from pathlib import Path
import sys

data = Path(sys.argv[1]).read_bytes()
if not data.endswith(b"\n") or data.count(b"\n") != 1:
    raise SystemExit("VERSION must contain exactly one newline")
print(data[:-1].decode("ascii"))
PY
)
    python3 "$script_dir/package-artifact.py" \
        --platform "$real_android_platform" --version "$real_android_version" \
        --build-dir "$real_android_build_dir" --binary-suffix "$real_android_binary_suffix" \
        --libusb-source-archive "$real_android_libusb_archive" \
        --ndk-root "$real_android_ndk_root" --link-map-dir "$real_android_link_map_dir" \
        --output-dir "$test_root/real-android"
    python3 "$script_dir/audit-artifact.py" \
        --platform "$real_android_platform" \
        --archive "$test_root/real-android/px4-userland-$real_android_version-$real_android_platform.tar.gz"
    python3 - \
        "$test_root/real-android/px4-userland-$real_android_version-$real_android_platform.tar.gz" \
        "$test_root" "$script_dir/.." "$real_android_build_dir" "$real_android_link_map_dir" "$real_android_ndk_root" <<'PY'
import pathlib
import sys
import tarfile

archive, temp_root, source_root, build_dir, link_map_dir, ndk_root = sys.argv[1:]
forbidden = tuple(str(pathlib.Path(path).resolve()) for path in
                  (temp_root, source_root, build_dir, link_map_dir, ndk_root))
with tarfile.open(archive, "r:gz") as stream:
    for member in stream.getmembers():
        if member.name.endswith(".map") or member.name.startswith("evidence/maps/"):
            raise SystemExit(f"raw linker map entered Android archive: {member.name}")
        handle = stream.extractfile(member)
        if handle is None:
            raise SystemExit(f"cannot read archive member: {member.name}")
        payload = handle.read()
        for path in forbidden:
            if path.encode() in payload:
                raise SystemExit(f"input path entered Android archive: {path}")
PY
    printf '%s\n' 'real Android package and final archive audit: PASS'
fi
real_libusb_archive=${PX4_LIBUSB_1_0_28_ARCHIVE:-}
if [ -n "$real_libusb_archive" ]; then
    [ -f "$real_libusb_archive" ] || {
        printf '%s\n' "PX4_LIBUSB_1_0_28_ARCHIVE is not a file: $real_libusb_archive" >&2
        exit 1
    }
    python3 "$script_dir/package-artifact.py" --self-test \
        --libusb-source-archive "$real_libusb_archive"
    source_ref=${PX4_SOURCE_REF:-HEAD}
    if git -C "$script_dir/.." cat-file -e "$source_ref:VERSION" 2>/dev/null; then
        python3 "$script_dir/package-source.py" --version "$version" \
            --source-root "$script_dir/.." --source-ref "$source_ref" \
            --libusb-source-archive "$real_libusb_archive" --output-dir "$test_root/source"
    else
        if python3 "$script_dir/package-source.py" --version "$version" \
            --source-root "$script_dir/.." --source-ref "$source_ref" \
            --libusb-source-archive "$real_libusb_archive" --output-dir "$test_root/source"; then
            printf '%s\n' 'source package accepted a source ref without VERSION' >&2
            exit 1
        fi
        printf '%s\n' 'source package correctly refused source ref without VERSION'
    fi
fi
printf '%s\n' 'packaging self-tests: PASS'

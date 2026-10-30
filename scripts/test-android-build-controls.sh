#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Exercise Android build controls with mocked toolchain boundaries.
set -eu

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
fixture=$(mktemp -d "${TMPDIR:-/tmp}/px4-android-build-controls.XXXXXX")
cleanup()
{
    status=$?
    trap - EXIT HUP INT TERM
    if [ "$status" -ne 0 ]; then
        for log in "$fixture"/*/*.log; do
            [ -f "$log" ] || continue
            printf '%s\n' "--- $log ---" >&2
            cat "$log" >&2
        done
    fi
    rm -rf "$fixture"
    exit "$status"
}
trap cleanup EXIT
trap 'exit 1' HUP INT TERM

prepare_case()
{
    case_dir=$fixture/$1
    host_system=${2:-Linux}
    host_machine=${3:-x86_64}
    case "$host_system-$host_machine" in
    Linux-x86_64) ndk_host=linux-x86_64 ;;
    Linux-aarch64) ndk_host=linux-aarch64 ;;
    Darwin-arm64) ndk_host=darwin-arm64 ;;
    Darwin-x86_64) ndk_host=darwin-x86_64 ;;
    *) printf '%s\n' "unsupported synthetic host: $host_system $host_machine" >&2; exit 1 ;;
    esac
    mkdir -p "$case_dir/bin" "$case_dir/caller" "$case_dir/libusb" \
        "$case_dir/ndk/toolchains/llvm/prebuilt/$ndk_host/bin"
    printf '%s\n' "$host_system" >"$case_dir/host-system"
    printf '%s\n' "$host_machine" >"$case_dir/host-machine"
    printf '%s\n' 'Pkg.Revision = 27.0.12077973' >"$case_dir/ndk/source.properties"
    cat >"$case_dir/libusb/configure" <<'EOF'
#!/bin/sh
set -eu
for argument do
    case "$argument" in
    --prefix=*) printf '%s\n' "${argument#*=}" >.mock-prefix ;;
    esac
done
EOF
    chmod 0755 "$case_dir/libusb/configure"
    printf '%s\n' 'synthetic libusb COPYING' >"$case_dir/libusb/COPYING"

    cat >"$case_dir/bin/cmake" <<'EOF'
#!/bin/sh
set -eu
case_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
printf '%s\n' "$@" >>"$case_dir/cmake.args"
if [ "${1:-}" = --build ]; then
    printf '%s\n' build >>"$case_dir/cmake.log"
    for argument do
        case "$argument" in
        -j*) printf '%s\n' "$argument" >>"$case_dir/cmake-jobs.log" ;;
        esac
    done
    exit 0
fi
printf '%s\n' configure >>"$case_dir/cmake.log"
build_dir=
core_source=
while [ "$#" -gt 0 ]; do
    case "$1" in
    -B)
        build_dir=$2
        shift 2
        ;;
    -DDTV_USERLAND_CORE_SOURCE_DIR=*)
        core_source=${1#*=}
        printf '%s\n' "$core_source" >"$case_dir/core-source.log"
        shift
        ;;
    *) shift ;;
    esac
done
if [ -n "$core_source" ] && [ ! -f "$core_source/CMakeLists.txt" ]; then
    printf '%s\n' 'dtv-userland-core source override has no CMakeLists.txt' >&2
    exit 1
fi
mkdir -p "$build_dir"
for binary in px4-ts-probe px4d; do
    : >"$build_dir/$binary"
done
EOF
    cat >"$case_dir/bin/make" <<'EOF'
#!/bin/sh
set -eu
case_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
printf '%s\n' "$*" >>"$case_dir/make.args"
if [ "${1:-}" = install ]; then
    prefix=$(cat .mock-prefix)
    mkdir -p "$prefix/lib"
    : >"$prefix/lib/libusb-1.0.a"
fi
EOF
    cat >"$case_dir/bin/getconf" <<'EOF'
#!/bin/sh
set -eu
case_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
: >"$case_dir/getconf.called"
[ ! -e "$case_dir/getconf-fail" ] || exit 1
printf '%s\n' 7
EOF
    cat >"$case_dir/bin/uname" <<'EOF'
#!/bin/sh
set -eu
case_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
printf '%s\n' "${1:-}" >>"$case_dir/uname.args"
case "${1:-}" in
-s) cat "$case_dir/host-system" ;;
-m) cat "$case_dir/host-machine" ;;
*) exit 2 ;;
esac
EOF
    cat >"$case_dir/bin/curl" <<'EOF'
#!/bin/sh
case_dir=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd -P)
: >"$case_dir/curl.called"
exit 88
EOF
    for tool in cmake make getconf uname curl; do
        chmod 0755 "$case_dir/bin/$tool"
    done
    cat >"$case_dir/bin/ninja" <<'EOF'
#!/bin/sh
exit 0
EOF
    chmod 0755 "$case_dir/bin/ninja"
    for tool in aarch64-linux-android24-clang aarch64-linux-android24-clang++ \
        llvm-ar llvm-ranlib llvm-strip; do
        printf '#!/bin/sh\nexit 0\n' >"$case_dir/ndk/toolchains/llvm/prebuilt/$ndk_host/bin/$tool"
        chmod 0755 "$case_dir/ndk/toolchains/llvm/prebuilt/$ndk_host/bin/$tool"
    done
    cat >"$case_dir/ndk/toolchains/llvm/prebuilt/$ndk_host/bin/llvm-nm" <<'EOF'
#!/bin/sh
exit 0
EOF
    chmod 0755 "$case_dir/ndk/toolchains/llvm/prebuilt/$ndk_host/bin/llvm-nm"
}

run_build()
{
    case_dir=$1
    shift
    (
        cd -- "$case_dir/caller"
        env -i \
            PATH="$case_dir/bin:/usr/bin:/bin" \
            ANDROID_NDK_HOME="$case_dir/ndk" \
            "$@" \
            "$root/scripts/build-android.sh" \
            --abi aarch64 --output "$case_dir/output" --libusb-source "$case_dir/libusb"
    )
}

run_build_without_source()
{
    case_dir=$1
    shift
    (
        cd -- "$case_dir/caller"
        env -i \
            PATH="$case_dir/bin:/usr/bin:/bin" \
            ANDROID_NDK_HOME="$case_dir/ndk" \
            "$@" \
            "$root/scripts/build-android.sh" \
            --abi aarch64 --output "$case_dir/output"
    )
}

run_build_with_evidence()
{
    case_dir=$1
    shift
    (
        cd -- "$case_dir/caller"
        env -i \
            PATH="$case_dir/bin:/usr/bin:/bin" \
            ANDROID_NDK_HOME="$case_dir/ndk" \
            "$@" \
            "$root/scripts/build-android.sh" \
            --abi aarch64 --output "$case_dir/output" --evidence "$case_dir/evidence"
    )
}

expect_mocked_build_stop()
{
    case_dir=$1
    shift
    if run_build "$case_dir" "$@" >"$case_dir/build.log" 2>&1; then
        printf '%s\n' 'mocked Android build unexpectedly passed the compiler boundary' >&2
        exit 1
    fi
    grep -F 'missing static libusb symbol in' "$case_dir/build.log" >/dev/null
}

prepare_case default
expect_mocked_build_stop "$fixture/default"
grep -Fx -- '-j7' "$fixture/default/make.args" >/dev/null
grep -Fx -- '-j7' "$fixture/default/cmake-jobs.log" >/dev/null
[ -f "$fixture/default/getconf.called" ]
[ ! -e "$fixture/default/core-source.log" ]

prepare_case synthetic-darwin-arm64 Darwin arm64
expect_mocked_build_stop "$fixture/synthetic-darwin-arm64" PX4_ANDROID_BUILD_JOBS=2
grep -Fx -- '-j2' "$fixture/synthetic-darwin-arm64/make.args" >/dev/null
grep -Fx -- '-j2' "$fixture/synthetic-darwin-arm64/cmake-jobs.log" >/dev/null
grep -Fx -- '-s' "$fixture/synthetic-darwin-arm64/uname.args" >/dev/null
grep -Fx -- '-m' "$fixture/synthetic-darwin-arm64/uname.args" >/dev/null
[ -x "$fixture/synthetic-darwin-arm64/ndk/toolchains/llvm/prebuilt/darwin-arm64/bin/llvm-nm" ]

prepare_case default-fallback
: >"$fixture/default-fallback/getconf-fail"
expect_mocked_build_stop "$fixture/default-fallback"
grep -Fx -- '-j2' "$fixture/default-fallback/make.args" >/dev/null
grep -Fx -- '-j2' "$fixture/default-fallback/cmake-jobs.log" >/dev/null

prepare_case override
mkdir -p "$fixture/core-valid"
printf '%s\n' 'cmake_minimum_required(VERSION 3.20)' >"$fixture/core-valid/CMakeLists.txt"
expect_mocked_build_stop "$fixture/override" \
    PX4_ANDROID_BUILD_JOBS=3 \
    DTV_USERLAND_CORE_SOURCE_DIR="$fixture/core-valid"
grep -Fx -- '-j3' "$fixture/override/make.args" >/dev/null
grep -Fx -- '-j3' "$fixture/override/cmake-jobs.log" >/dev/null
grep -Fx -- "$fixture/core-valid" "$fixture/override/core-source.log" >/dev/null
[ ! -e "$fixture/override/getconf.called" ]

prepare_case relative-core
mkdir -p "$fixture/relative-core/caller/core-source"
printf '%s\n' 'cmake_minimum_required(VERSION 3.20)' \
    >"$fixture/relative-core/caller/core-source/CMakeLists.txt"
expect_mocked_build_stop "$fixture/relative-core" DTV_USERLAND_CORE_SOURCE_DIR=core-source
grep -Fx -- "$fixture/relative-core/caller/core-source" \
    "$fixture/relative-core/core-source.log" >/dev/null

prepare_case empty-core
expect_mocked_build_stop "$fixture/empty-core" DTV_USERLAND_CORE_SOURCE_DIR=
[ ! -e "$fixture/empty-core/core-source.log" ]

for invalid_jobs in empty zero nonnumeric; do
    prepare_case "invalid-$invalid_jobs"
    case "$invalid_jobs" in
    empty) value= ;;
    zero) value=0 ;;
    nonnumeric) value=2x ;;
    esac
    if run_build_without_source "$fixture/invalid-$invalid_jobs" "PX4_ANDROID_BUILD_JOBS=$value" \
        >"$fixture/invalid-$invalid_jobs/result.log" 2>&1; then
        printf '%s\n' "build-android.sh accepted $invalid_jobs PX4_ANDROID_BUILD_JOBS" >&2
        exit 1
    fi
    grep -F 'PX4_ANDROID_BUILD_JOBS must be a positive integer' \
        "$fixture/invalid-$invalid_jobs/result.log" >/dev/null
    [ ! -e "$fixture/invalid-$invalid_jobs/make.args" ]
    [ ! -e "$fixture/invalid-$invalid_jobs/cmake.log" ]
    [ ! -e "$fixture/invalid-$invalid_jobs/output" ]
    [ ! -e "$fixture/invalid-$invalid_jobs/curl.called" ]
done

prepare_case evidence-core
mkdir -p "$fixture/evidence-core/core-source"
printf '%s\n' 'cmake_minimum_required(VERSION 3.20)' \
    >"$fixture/evidence-core/core-source/CMakeLists.txt"
if run_build_with_evidence "$fixture/evidence-core" \
    DTV_USERLAND_CORE_SOURCE_DIR="$fixture/evidence-core/core-source" \
    >"$fixture/evidence-core/result.log" 2>&1; then
    printf '%s\n' 'build-android.sh accepted an unpinned Core source with release evidence settings' >&2
    exit 1
fi
grep -F -- '--evidence cannot use an unpinned DTV_USERLAND_CORE_SOURCE_DIR override' \
    "$fixture/evidence-core/result.log" >/dev/null
[ ! -e "$fixture/evidence-core/curl.called" ]
[ ! -e "$fixture/evidence-core/make.args" ]
[ ! -e "$fixture/evidence-core/cmake.log" ]
[ ! -e "$fixture/evidence-core/output" ]
[ ! -e "$fixture/evidence-core/evidence" ]
prepare_case invalid-core
if run_build "$fixture/invalid-core" \
    DTV_USERLAND_CORE_SOURCE_DIR="$fixture/core-missing" \
    >"$fixture/invalid-core/result.log" 2>&1; then
    printf '%s\n' 'build-android.sh accepted a Core override without CMakeLists.txt' >&2
    exit 1
fi
grep -Fx -- "$fixture/core-missing" "$fixture/invalid-core/core-source.log" >/dev/null
grep -F 'dtv-userland-core source override has no CMakeLists.txt' \
    "$fixture/invalid-core/result.log" >/dev/null
grep -Fx configure "$fixture/invalid-core/cmake.log" >/dev/null
if grep -Fx build "$fixture/invalid-core/cmake.log" >/dev/null; then
    printf '%s\n' 'CMake build continued after rejecting the Core source override' >&2
    exit 1
fi
[ ! -e "$fixture/invalid-core/output/px4d-aarch64" ]

printf '%s\n' 'Android build controls: PASS (mocked make/CMake/NDK/compiler boundary)'

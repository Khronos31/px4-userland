#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu

build=$1
version=$2
test_root=$(mktemp -d "${TMPDIR:-/tmp}/px4-cli-test.XXXXXX")
trap 'find "$test_root" -depth -delete' EXIT
for program in px4d px4-ts px4ctl px4-termux; do
    if [ "$program" = px4-termux ]; then
        set -- sh "$build/$program"
    else
        set -- "$build/$program"
    fi
    name=$program
    [ "$program" != px4ctl ] || name=px4-userland
    output=$("$@" --version)
    [ "$output" = "$name $version" ]
    "$@" --help >"$test_root/$program.help"
    if grep -F -- '-F' "$test_root/$program.help" >/dev/null; then exit 1; fi
    status=0
    "$@" -F "$test_root/missing.bin" >/dev/null 2>"$test_root/$program.err" || status=$?
    [ "$status" -eq 2 ]
    case "$program" in
        px4d|px4-ts|px4-termux) grep -qi 'unknown argument' "$test_root/$program.err" ;;
    esac
    status=0
    "$@" --version --unknown >"$test_root/$program.version" 2>&1 || status=$?
    [ "$status" -eq 0 ]
    grep -F "$name $version" "$test_root/$program.version" >/dev/null
    status=0
    "$@" --unknown --version >"$test_root/$program.version" 2>&1 || status=$?
    [ "$status" -eq 0 ]
    grep -F "$name $version" "$test_root/$program.version" >/dev/null
    for arguments in '--version --help' '--help --version' '--help --unknown'; do
        status=0
        "$@" $arguments >"$test_root/$program.metadata" 2>&1 || status=$?
        [ "$status" -eq 0 ]
        grep -F 'usage:' "$test_root/$program.metadata" >/dev/null
    done
done
for option in --list -l; do
    status=0
    "$build/px4d" "$option" >/dev/null 2>&1 || status=$?
    [ "$status" -eq 2 ]
done
"$build/px4d" --models | grep -F PX-Q3U4 >/dev/null
"$build/px4d" -h >"$test_root/short-help" 2>"$test_root/help.err"
"$build/px4d" --help >"$test_root/long-help"
cmp "$test_root/short-help" "$test_root/long-help"
[ ! -s "$test_root/help.err" ]
status=0
"$build/px4d" -F "$test_root/missing.bin" >"$test_root/out" 2>"$test_root/-F.err" || status=$?
[ "$status" -eq 2 ]
grep -Fx 'argument error: unknown argument' "$test_root/-F.err" >/dev/null
grep -F 'px4d --device' "$test_root/out" >/dev/null
if grep -F -- '-F' "$test_root/out" >/dev/null; then exit 1; fi
status=0
"$build/px4d" --device serial:00001205000960 --firmware "$test_root/missing.bin" \
    >"$test_root/out" 2>"$test_root/--firmware.err" || status=$?
[ "$status" -eq 3 ]
[ ! -s "$test_root/out" ]
status=0
"$build/px4d" --device serial:00001205000960 --firmware >/dev/null 2>&1 || status=$?
[ "$status" -eq 2 ]
for option in -v --verbose; do
    status=0
    "$build/px4d" --device serial:00001205000960 --firmware "$test_root/missing.bin" "$option" \
        >"$test_root/out" 2>"$test_root/$option.err" || status=$?
    [ "$status" -eq 3 ]
    [ ! -s "$test_root/out" ]
    grep -Fx 'px4d DEBUG: loading firmware' "$test_root/$option.err" >/dev/null
    sed '/^px4d DEBUG: loading firmware$/d' "$test_root/$option.err" >"$test_root/filtered.err"
    cmp "$test_root/filtered.err" "$test_root/--firmware.err"
    if grep -F "$test_root" "$test_root/$option.err" >/dev/null; then exit 1; fi
    if grep -F 'opening USB enclosure' "$test_root/$option.err" >/dev/null; then exit 1; fi
done
cmp "$test_root/-v.err" "$test_root/--verbose.err"
printf '%s\n' 'PX4 device-free CLI tests: PASS'

#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# Collect a PX-W3U4 report. Paste the whole stdout into a GitHub issue.
# USB enumeration is read-only. px4d is started only for 0511:083f, in a
# private runtime directory, and is stopped before the script exits. Firmware
# is not rewritten when the bridge already has an image loaded.
set -eu

section() {
    printf '\n## %s\n' "$1"
}

note() {
    printf '%s\n' "$1"
}

have() {
    command -v "$1" >/dev/null 2>&1
}

bindir=${PX4_BINDIR:-}
find_bin() {
    name=$1
    if [ -n "$bindir" ] && [ -x "$bindir/$name" ]; then
        printf '%s\n' "$bindir/$name"
        return 0
    fi
    if have "$name"; then
        command -v "$name"
        return 0
    fi
    return 1
}

firmware=
if [ -n "${PX4_FIRMWARE:-}" ]; then
    firmware=$PX4_FIRMWARE
else
    for candidate in ./it930x-firmware.bin \
        /usr/local/share/px4-userland/it930x-firmware.bin \
        /usr/share/px4-userland/it930x-firmware.bin
    do
        if [ -f "$candidate" ]; then
            firmware=$candidate
            break
        fi
    done
fi

runtime=
px4d_pid=
# Called from the EXIT trap.
# shellcheck disable=SC2329
cleanup() {
    if [ -n "$px4d_pid" ]; then
        kill -INT "$px4d_pid" 2>/dev/null || true
        wait "$px4d_pid" 2>/dev/null || true
        px4d_pid=
    fi
    if [ -n "$runtime" ]; then
        rm -rf "$runtime"
        runtime=
    fi
}
trap cleanup EXIT INT TERM

printf '%s\n' 'px4-userland PX-W3U4 report'
printf '%s\n' 'Paste this whole log into a GitHub issue. It does not include the firmware file.'
date -Is 2>/dev/null || date

section 'host'
uname -srm || true
if [ -r /etc/os-release ]; then
    # shellcheck disable=SC1091
    . /etc/os-release
    printf 'os=%s %s\n' "${NAME:-unknown}" "${VERSION:-}"
fi
id || true

section 'usb'
if have lsusb; then
    lsusb -d 0511: || note 'lsusb: no 0511 devices'
else
    note 'lsusb: not installed'
fi

w3u4_serials=
sysfs=/sys/bus/usb/devices
if [ -d "$sysfs" ]; then
    for device in "$sysfs"/*; do
        [ -r "$device/idVendor" ] || continue
        vendor=$(cat "$device/idVendor")
        [ "$vendor" = 0511 ] || continue
        product=$(cat "$device/idProduct" 2>/dev/null || printf '%s' '?')
        serial=$(cat "$device/serial" 2>/dev/null || printf '%s' '?')
        speed=$(cat "$device/speed" 2>/dev/null || printf '%s' '?')
        printf 'sysfs %s id=%s:%s serial=%s speed=%s\n' \
            "$(basename "$device")" "$vendor" "$product" "$serial" "$speed"
        if [ "$product" = 083f ] && [ "$serial" != '?' ]; then
            w3u4_serials="$w3u4_serials $serial"
        fi
    done
else
    note 'sysfs: /sys/bus/usb/devices is not available'
fi

section 'kernel'
if dmesg -T >/tmp/px4-w3u4-dmesg.txt 2>/tmp/px4-w3u4-dmesg.err; then
    if grep -E '0511:083f|083f|it930x|usb [0-9]+-[0-9]' /tmp/px4-w3u4-dmesg.txt | tail -n 40; then
        :
    else
        note 'dmesg: no recent USB lines matched'
    fi
else
    note 'dmesg: not readable'
    cat /tmp/px4-w3u4-dmesg.err || true
fi
rm -f /tmp/px4-w3u4-dmesg.txt /tmp/px4-w3u4-dmesg.err

section 'tools'
px4d=$(find_bin px4d || true)
px4ctl=$(find_bin px4ctl || true)
px4ts=$(find_bin px4-ts || true)
printf 'px4d=%s\n' "${px4d:-missing}"
printf 'px4ctl=%s\n' "${px4ctl:-missing}"
printf 'px4-ts=%s\n' "${px4ts:-missing}"
printf 'firmware=%s\n' "${firmware:-missing}"
if [ -n "$px4d" ]; then
    "$px4d" --help >/dev/null || note 'px4d --help failed'
fi

if [ -z "$w3u4_serials" ]; then
    section 'result'
    note 'No PX-W3U4 (0511:083f) was visible. Plug it in directly and run this script again.'
    exit 1
fi

if [ -z "$px4d" ] || [ -z "$px4ctl" ] || [ -z "$firmware" ]; then
    section 'result'
    note 'USB identity was collected. px4d session skipped because px4d, px4ctl, or the firmware file is missing.'
    note 'Set PX4_BINDIR and PX4_FIRMWARE, then run again.'
    exit 0
fi

for serial in $w3u4_serials; do
    section "px4d $serial"
    runtime=$(mktemp -d)
    chmod 700 "$runtime"
    log=$runtime/px4d.log
    "$px4d" --device "$serial" --firmware "$firmware" --runtime-dir "$runtime" >"$log" 2>&1 &
    px4d_pid=$!
    ready=0
    i=0
    while [ "$i" -lt 90 ]; do
        if grep -q 'px4d ready' "$log"; then
            ready=1
            break
        fi
        if ! kill -0 "$px4d_pid" 2>/dev/null; then
            break
        fi
        i=$((i + 1))
        sleep 1
    done
    note "ready=$ready"
    cat "$log" || true
    if [ "$ready" -eq 1 ]; then
        note '--- px4ctl list ---'
        "$px4ctl" --device "$serial" --runtime-dir "$runtime" list || true
        note '--- px4ctl status ---'
        "$px4ctl" --device "$serial" --runtime-dir "$runtime" status || true
        note '--- card ---'
        "$px4ctl" --device "$serial" --runtime-dir "$runtime" card-status || true
        "$px4ctl" --device "$serial" --runtime-dir "$runtime" card-atr || true
        if [ -n "$px4ts" ] && [ "${PX4_REPORT_TUNE:-1}" != 0 ]; then
            note '--- terrestrial receiver 2, 5s, 527143 kHz ---'
            "$px4ts" --device "$serial" --runtime-dir "$runtime" \
                --receiver 2 --system isdb-t --frequency-khz 527143 \
                --duration-seconds 5 --output /dev/null || true
            note '--- satellite receiver 0, 5s, 1318000 kHz, slot 0, LNB 0V ---'
            "$px4ts" --device "$serial" --runtime-dir "$runtime" \
                --receiver 0 --system isdb-s --frequency-khz 1318000 \
                --slot 0 --lnb-voltage 0 --duration-seconds 5 --output /dev/null || true
            note '--- px4ctl status after tune ---'
            "$px4ctl" --device "$serial" --runtime-dir "$runtime" status || true
        fi
    fi
    kill -INT "$px4d_pid" 2>/dev/null || true
    wait "$px4d_pid" 2>/dev/null || true
    px4d_pid=
    rm -rf "$runtime"
    runtime=
done

section 'result'
note 'Finished. Paste this log into the issue. A tune timeout usually means the antenna was not connected.'
exit 0

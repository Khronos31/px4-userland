#!/bin/sh
set -eu

build=$1
test_root=$(mktemp -d "${TMPDIR:-/tmp}/px4-l3-owner-ipc.XXXXXX")
daemon_pid=
finish_daemon() {
    pid=$daemon_pid
    term_attempted=$1
    if [ "$term_attempted" = yes ]; then
        kill -TERM "$pid" 2>/dev/null || true
    fi
    if wait "$pid"; then
        wait_status=0
    else
        wait_status=$?
    fi
    signal=none
    if [ "$wait_status" -gt 128 ]; then
        signal_number=$((wait_status - 128))
        signal=$(kill -l "$signal_number" 2>/dev/null || printf unknown)
    fi
    printf 'px4d-l3-fake-usb pid=%s wait-status=%s signal=%s term-attempted=%s\n' \
        "$pid" "$wait_status" "$signal" "$term_attempted" >&2
    daemon_pid=
    return "$wait_status"
}
cleanup() {
    if [ -n "$daemon_pid" ]; then
        finish_daemon yes || true
    fi
    rm -rf "$test_root"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$test_root/runtime"
mkfifo "$test_root/stdin"
exec 3<>"$test_root/stdin"
"$build/px4d-l3-fake-usb" \
    --device serial:00001205000960 \
    --firmware /dev/null \
    --instance l3-owner-stream-test \
    --runtime-dir "$test_root/runtime" \
    >"$test_root/daemon.log" 2>&1 <"$test_root/stdin" 3>&- &
daemon_pid=$!

ready=0
attempt=0
while [ "$attempt" -lt 100 ]; do
    if grep -q 'px4d ready:' "$test_root/daemon.log"; then
        ready=1
        break
    fi
    if ! kill -0 "$daemon_pid" 2>/dev/null; then
        break
    fi
    attempt=$((attempt + 1))
    sleep 0.1
done
if [ "$ready" -ne 1 ]; then
    cat "$test_root/daemon.log" >&2
    exit 1
fi

if ! "$build/px4-ts" \
    --device serial:00001205000960 \
    --instance l3-owner-stream-test \
    --runtime-dir "$test_root/runtime" \
    --receiver 2 \
    --channel T13 \
    --time 1 \
    --output "$test_root/capture.ts"; then
    cat "$test_root/daemon.log" >&2
    exit 1
fi

capture_bytes=$(wc -c <"$test_root/capture.ts" | tr -d ' ')
if [ "$capture_bytes" -lt 188 ]; then
    printf 'fake USB IPC capture too short: %s bytes\n' "$capture_bytes" >&2
    exit 1
fi

exec 3>&-
if finish_daemon no; then
    :
else
    exit $?
fi

#!/usr/bin/env bash
#
# Smoke test: launches bt-speaker-cli in headless test mode (-o null --no-dbus),
# queries its control socket, verifies clean shutdown, and checks error paths.

set -euo pipefail

BIN="${1:-./build/bt-speaker-cli}"
TMP="$(mktemp -d)"
cleanup() {
    if [ -n "${PID:-}" ] && kill -0 "$PID" 2>/dev/null; then
        kill "$PID" 2>/dev/null || true
        wait "$PID" 2>/dev/null || true
    fi
    rm -rf "$TMP"
}
trap cleanup EXIT

SOCK="$TMP/bt.sock"
STATE="$TMP/state"
mkdir -p "$STATE"

"$BIN" -o null --no-dbus -n test-bt --control-socket "$SOCK" --state-dir "$STATE" \
    --log-level debug >/dev/null 2>"$TMP/daemon.log" &
PID=$!

for _ in $(seq 1 50); do
    if [ -S "$SOCK" ]; then
        break
    fi
    sleep 0.1
done
[ -S "$SOCK" ] || { cat "$TMP/daemon.log" >&2; exit 1; }

STATUS="$("$BIN" status --control-socket "$SOCK")"
printf '%s\n' "$STATUS" | grep -q 'name: test-bt'
printf '%s\n' "$STATUS" | grep -q 'output: null'
printf '%s\n' "$STATUS" | grep -q 'stream: idle'

"$BIN" vol 55 --control-socket "$SOCK"
"$BIN" delay 40 --control-socket "$SOCK"
"$BIN" mute on --control-socket "$SOCK"

STATUS2="$("$BIN" status --control-socket "$SOCK")"
printf '%s\n' "$STATUS2" | grep -q 'volume: 55 (muted)'
printf '%s\n' "$STATUS2" | grep -q 'output delay: 40 ms'

kill -TERM "$PID"
wait "$PID"
PID=''

grep -q 'Shutting down' "$TMP/daemon.log"
printf 'smoke_test: PASS\n'

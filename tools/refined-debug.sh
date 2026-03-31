#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=${BUILD_DIR:-build}
BLUE=${BLUE:-$BUILD_DIR/blueyos.elf}
QEMU_LOG=/tmp/blueyos-qemu-refined.log
RUN_DIR="$BUILD_DIR/run"
QEMU_PID_FILE="$RUN_DIR/qemu-refined.pid"

mkdir -p "$RUN_DIR"

echo "Refreshing kernel and disk image before refined debug session..."
BUILD_DIR="$BUILD_DIR" bash tools/rebuild-debug-disk.sh

echo "Stopping any stale QEMU instances..."
pkill -f '^qemu-system-i386 ' 2>/dev/null || true
sleep 1

echo "Starting QEMU (gdb server) in background..."
nohup bash tools/qemu-run.sh -S -gdb tcp::1234 > "$QEMU_LOG" 2>&1 &
QEMU_PID=$!
echo "$QEMU_PID" > "$QEMU_PID_FILE"
echo "QEMU PID: $QEMU_PID (log: $QEMU_LOG)"

cleanup() {
    echo "Cleaning up..."
    if [ -f "$QEMU_PID_FILE" ]; then
        kill "$(cat "$QEMU_PID_FILE")" 2>/dev/null || true
        rm -f "$QEMU_PID_FILE"
    fi
}
trap cleanup EXIT

echo "Waiting for GDB stub on :1234..."
for _ in $(seq 1 40); do
    if ss -ltn | grep -q ':1234 '; then
        break
    fi
    sleep 0.25
done

if ! ss -ltn | grep -q ':1234 '; then
    echo "ERROR: QEMU GDB stub did not come up on :1234" >&2
    tail -n 50 "$QEMU_LOG" >&2 || true
    exit 1
fi

echo "Launching GDB with staged BiscuitFS watchpoints."
gdb -batch -q "$BLUE" -x tools/itoa_conditional_watch.gdb

echo "Refined GDB session complete. QEMU will be terminated by cleanup."
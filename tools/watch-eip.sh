#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

ADDR=${1:-0x125330}
BUILD_DIR=${BUILD_DIR:-build}
BLUE=${BLUE:-$BUILD_DIR/blueyos.elf}
QEMU_LOG=/tmp/blueyos-watch-qemu.log
GDB_LOG=/tmp/blueyos-watch-gdb.log
RUN_DIR="$BUILD_DIR/run"
QEMU_PID_FILE="$RUN_DIR/qemu-watch.pid"

mkdir -p "$RUN_DIR"

echo "Refreshing kernel and disk image before debug session..."
BUILD_DIR="$BUILD_DIR" bash tools/rebuild-debug-disk.sh

echo "Stopping any stale QEMU instances..."
pkill -f '^qemu-system-i386 ' 2>/dev/null || true
sleep 1

echo "Starting QEMU (gdb server) in background... (log: $QEMU_LOG)"
nohup bash tools/qemu-run.sh -S -gdb tcp::1234 > "$QEMU_LOG" 2>&1 &
QEMU_PID=$!
echo $QEMU_PID > "$QEMU_PID_FILE"
echo "QEMU PID: $QEMU_PID"

cleanup() {
    echo "Cleaning up QEMU..."
    if [ -f "$QEMU_PID_FILE" ]; then
        kill "$(cat $QEMU_PID_FILE)" 2>/dev/null || true
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

cat > /tmp/blueyos-watch.gdb <<-GDBCMD
set pagination off
set architecture i386
set disassembly-flavor intel
target remote :1234
printf "Attached to :1234\n"
printf "Setting hardware watchpoint on %s\n" "$ADDR"
# Use an access watchpoint which gdb remote stubs often accept
awatch *(int*)$ADDR
continue
printf "\n=== WATCHPOINT HIT ===\n"
printf "watched addr: %s\n" "$ADDR"
p/x *(unsigned int*)$ADDR
x/16i *(unsigned int*)$ADDR
bt
info registers
quit
GDBCMD

echo "Running GDB script (log: $GDB_LOG)"
gdb -q "$BLUE" --command=/tmp/blueyos-watch.gdb &> "$GDB_LOG" || true

echo "GDB finished. Output saved to $GDB_LOG"

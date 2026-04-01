#!/usr/bin/env bash
set -euo pipefail

OUT_DIR=build/run
mkdir -p "$OUT_DIR"

QEMU_LOG=/tmp/blueyos-qemu-serial.log
GDB_OUT=/tmp/blueyos-gdb-auto.txt
FAULT_ADDR=${1:-0x70004000}
GDB_PORT=${2:-1234}

pkill -f '^qemu-system-i386 ' || true

# Start QEMU paused with gdb server
nohup bash tools/qemu-run.sh -S -gdb tcp::${GDB_PORT} > "$QEMU_LOG" 2>&1 &
QEMU_PID=$!
echo $QEMU_PID > "$OUT_DIR/qemu-paused.pid"

# Wait for gdb port
for i in {1..40}; do
  ss -ltn | grep -q ":${GDB_PORT} " && break || sleep 0.25
done

# Run GDB batch to collect backtrace, registers, and memory near fault address
GDB_CMD=(
  gdb -batch -q build/kernel/bkernel
  -ex "target remote :${GDB_PORT}"
  -ex "set pagination off"
  -ex "break isr_handler"
  -ex "continue"
  -ex "bt full"
  -ex "info registers"
  -ex "x/64wx ${FAULT_ADDR}"
  -ex "quit"
)

{
  "${GDB_CMD[@]}"
} > "$GDB_OUT" 2>&1 || true

sleep 0.5

kill $QEMU_PID || true

echo "GDB output saved to: $GDB_OUT"
echo "QEMU serial log: $QEMU_LOG (tail below)"

tail -n 200 "$QEMU_LOG" || true

exit 0

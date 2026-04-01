#!/usr/bin/env bash
set -euo pipefail
OUT_DIR=build/run
mkdir -p "$OUT_DIR"
QEMU_LOG=/tmp/blueyos-qemu-serial.log
GDB_OUT=/tmp/blueyos-gdb-on-oops.txt
GDB_PORT=${1:-1234}
TIMEOUT=${2:-60}
PATTERN=${3:-"System halting."}

pkill -f '^qemu-system-i386 ' || true

# Start QEMU with gdb server (running)
nohup bash tools/qemu-run.sh -gdb tcp::${GDB_PORT} > "$QEMU_LOG" 2>&1 &
QEMU_PID=$!
echo $QEMU_PID > "$OUT_DIR/qemu-on-oops.pid"

# Wait for the OOPS pattern in the serial log
SECONDS=0
while :; do
  if grep -q "$PATTERN" "$QEMU_LOG"; then
    break
  fi
  if [ "$SECONDS" -ge "$TIMEOUT" ]; then
    echo "Timeout waiting for OOPS in $QEMU_LOG" >&2
    break
  fi
  sleep 0.5
done

# Attach GDB and dump backtrace/registers (also load init symbols if available)
gdb -batch -q build/blueyos.elf \
  -ex "target remote :${GDB_PORT}" \
  -ex "set pagination off" \
  -ex "add-symbol-file build/user/init-musl.elf 0x400000" \
  -ex "bt full" \
  -ex "info registers" \
  -ex "x/64wx 0x70004000" \
  -ex "quit" \
  > "$GDB_OUT" 2>&1 || true

sleep 0.2

# Stop QEMU
kill $QEMU_PID || true

echo "GDB output saved to: $GDB_OUT"
echo "QEMU serial log: $QEMU_LOG (tail below)"

tail -n 200 "$QEMU_LOG" || true

exit 0

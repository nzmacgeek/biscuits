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

#!/usr/bin/env bash
# Wrapper to the relocated script at scripts/experimental/debug/gdb-auto-attach.sh
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
if [ -x "$ROOT_DIR/scripts/experimental/debug/gdb-auto-attach.sh" ]; then
  exec "$ROOT_DIR/scripts/experimental/debug/gdb-auto-attach.sh" "$@"
else
  echo "scripts/experimental/debug/gdb-auto-attach.sh not found" >&2
  exit 1
fi
  -ex "break isr_handler"

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

ADDR=${1:-0x125330}
BUILD_DIR=${BUILD_DIR:-build}
BLUE=${BLUE:-$BUILD_DIR/blueyos.elf}
QEMU_LOG=/tmp/blueyos-break-qemu.log
GDB_LOG=/tmp/blueyos-break-gdb.log
RUN_DIR="$BUILD_DIR/run"
#!/usr/bin/env bash
# Wrapper to relocated script
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
if [ -x "$ROOT_DIR/scripts/experimental/debug/break-writers.sh" ]; then
  exec "$ROOT_DIR/scripts/experimental/debug/break-writers.sh" "$@"
else
  echo "scripts/experimental/debug/break-writers.sh not found" >&2
  exit 1
fi


#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

ADDR=${1:-0x125330}
BUILD_DIR=${BUILD_DIR:-build}
BLUE=${BLUE:-$BUILD_DIR/blueyos.elf}
QEMU_LOG=/tmp/blueyos-break-qemu.log
GDB_LOG=/tmp/blueyos-break-gdb.log
RUN_DIR="$BUILD_DIR/run"
QEMU_PID_FILE="$RUN_DIR/qemu-break.pid"

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

cat > /tmp/blueyos-break.gdb <<-GDBCMD
set pagination off
set architecture i386
set disassembly-flavor intel
target remote :1234
printf "Attached to :1234\n"

# List of likely writer/debug functions (no early kernel_main breakpoint so
# the kernel boots normally and exercises filesystem writes)

break syslog_write
commands
  printf "\n--- HIT syslog_write ---\n"
  p/x *(unsigned int*)$ADDR
  x/16x $ADDR
  # Dump the in-kernel flush history (if present)
  p/x &syslog_flush_hist
  p/x syslog_flush_hist_head
  p/x syslog_flush_hist_count
  x/64x &syslog_flush_hist
  bt
  continue
end

break syslog_flush_to_fs
commands
  printf "\n--- HIT syslog_flush_to_fs ---\n"
  p/x *(unsigned int*)$ADDR
  x/16x $ADDR
  p/x &syslog_flush_hist
  p/x syslog_flush_hist_head
  p/x syslog_flush_hist_count
  x/64x &syslog_flush_hist
  bt
  continue
end

break vfs_write
commands
  printf "\n--- HIT vfs_write ---\n"
  p/x *(unsigned int*)$ADDR
  x/16x $ADDR
  p/x &syslog_flush_hist
  p/x syslog_flush_hist_head
  p/x syslog_flush_hist_count
  x/64x &syslog_flush_hist
  bt
  continue
end

break fat_vfs_write
commands
  printf "\n--- HIT fat_vfs_write ---\n"
  p/x *(unsigned int*)$ADDR
  x/16x $ADDR
  p/x &syslog_flush_hist
  p/x syslog_flush_hist_head
  p/x syslog_flush_hist_count
  x/64x &syslog_flush_hist
  bt
  continue
end

break blueyfs.c
# fallback: break functions known to touch data
break write_page
commands
  printf "\n--- HIT write_page ---\n"
  p/x *(unsigned int*)$ADDR
  x/16x $ADDR
  p/x &syslog_flush_hist
  p/x syslog_flush_hist_head
  p/x syslog_flush_hist_count
  x/64x &syslog_flush_hist
  bt
  continue
end

break vfs_open
commands
  printf "\n--- HIT vfs_open ---\n"
  p/x *(unsigned int*)$ADDR
  x/16x $ADDR
  p/x &syslog_flush_hist
  p/x syslog_flush_hist_head
  p/x syslog_flush_hist_count
  x/64x &syslog_flush_hist
  bt
  continue
end

printf "Running and waiting for any breakpoint to hit...\n"
continue
quit
GDBCMD

echo "Running GDB script (log: $GDB_LOG)"
gdb -q "$BLUE" --command=/tmp/blueyos-break.gdb &> "$GDB_LOG" || true

echo "GDB finished. Output saved to $GDB_LOG"
echo "Tail of GDB log:" 
tail -n 200 "$GDB_LOG" || true

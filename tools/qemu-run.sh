#!/usr/bin/env bash
# tools/qemu-run.sh - Launch BlueyOS in QEMU
# "Mum! Dad! Come play!" - Bluey and Bingo
#
# Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
# licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#
# Usage: bash tools/qemu-run.sh [extra qemu args]
# Press Ctrl+A then X to quit QEMU serial mode.

set -e
cd "$(dirname "$0")/.."

BUILD_DIR=${BUILD_DIR:-build}
DISK_IMAGE=${DISK_IMAGE:-$BUILD_DIR/blueyos-disk.img}
LOG_DISK_IMAGE=${LOG_DISK_IMAGE:-$BUILD_DIR/blueyos-log-fat.img}

if [ ! -f "$DISK_IMAGE" ]; then
    echo "ERROR: $DISK_IMAGE not found. Run 'make disk' first!"
    exit 1
fi

echo "Starting BlueyOS in QEMU..."
echo "  Memory: 1024MB | Boot: hard disk via GRUB | Serial: stdio"
if [ -f "$LOG_DISK_IMAGE" ]; then
    echo "  Extra disk: $LOG_DISK_IMAGE (IDE index 1)"
fi
echo ""

QEMU_ARGS=(
    -drive "file=$DISK_IMAGE,format=raw,if=ide,index=0"
    -boot c
    -m 1024M
    -serial stdio
    -no-reboot
    -no-shutdown
)

if [ -f "$LOG_DISK_IMAGE" ]; then
    QEMU_ARGS+=( -drive "file=$LOG_DISK_IMAGE,format=raw,if=ide,index=1" )
fi

exec qemu-system-i386 "${QEMU_ARGS[@]}" "$@"

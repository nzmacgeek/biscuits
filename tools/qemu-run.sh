#!/usr/bin/env bash
# tools/qemu-run.sh - Launch BlueyOS in QEMU
# "Mum! Dad! Come play!" - Bluey and Bingo
#
# Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
# licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#
# Usage: bash tools/qemu-run.sh [extra qemu args]
# Default serial capture is written to build/qemu-serial.log.
# Set SERIAL_MODE=stdio to route serial back to the terminal.

set -e
cd "$(dirname "$0")/.."

BUILD_DIR=${BUILD_DIR:-build}
DISK_IMAGE=${DISK_IMAGE:-$BUILD_DIR/blueyos-disk.img}
LOG_DISK_IMAGE=${LOG_DISK_IMAGE:-$BUILD_DIR/blueyos-log-fat.img}
SERIAL_MODE=${SERIAL_MODE:-file}
SERIAL_LOG=${SERIAL_LOG:-$BUILD_DIR/qemu-serial.log}

case "$SERIAL_MODE" in
    file|stdio)
        ;;
    *)
        echo "ERROR: unsupported SERIAL_MODE='$SERIAL_MODE' (expected 'file' or 'stdio')"
        exit 1
        ;;
esac

if [ ! -f "$DISK_IMAGE" ]; then
    echo "ERROR: $DISK_IMAGE not found. Run 'make disk' first!"
    exit 1
fi

mkdir -p "$(dirname "$SERIAL_LOG")"

echo "Starting BlueyOS in QEMU..."
if [ "$SERIAL_MODE" = "stdio" ]; then
    echo "  Memory: 1024MB | Boot: hard disk via GRUB | Display: GTK GUI | Serial: stdio"
else
    rm -f "$SERIAL_LOG"
    echo "  Memory: 1024MB | Boot: hard disk via GRUB | Display: GTK GUI | Serial: logged to $SERIAL_LOG"
fi
if [ -f "$LOG_DISK_IMAGE" ]; then
    echo "  Extra disk: $LOG_DISK_IMAGE (IDE index 1)"
fi
echo ""

if [ "$SERIAL_MODE" = "stdio" ]; then
    SERIAL_ARGS=( -serial stdio )
else
    SERIAL_ARGS=( -serial "file:$SERIAL_LOG" )
fi

QEMU_ARGS=(
    -drive "file=$DISK_IMAGE,format=raw,if=ide,index=0"
    -boot c
    -m 1024M
    -display gtk
    -netdev user,id=usernet -device ne2k_pci,netdev=usernet 
    -vga std
    "${SERIAL_ARGS[@]}"
    -no-reboot
    -no-shutdown
)

if [ -f "$LOG_DISK_IMAGE" ]; then
    QEMU_ARGS+=( -drive "file=$LOG_DISK_IMAGE,format=raw,if=ide,index=1" )
fi

exec qemu-system-i386 "${QEMU_ARGS[@]}" "$@"

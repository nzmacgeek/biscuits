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

if [ ! -f blueyos.iso ]; then
    echo "ERROR: blueyos.iso not found. Run 'make iso' first!"
    exit 1
fi

echo "Starting BlueyOS in QEMU..."
echo "  Memory: 256MB | SMP: ${QEMU_SMP:-4} CPUs | Serial: stdio | No reboot on crash"
echo "  Press Ctrl+A then X to exit QEMU"
echo ""

exec qemu-system-i386 \
    -cdrom blueyos.iso \
    -m 256M \
    -smp "${QEMU_SMP:-4}" \
    -serial stdio \
    -no-reboot \
    -no-shutdown \
    "$@"

#!/usr/bin/env bash
# tools/mkdisk.sh - Create a bootable BlueyOS ISO image
# "Time to pack up and go home!" - Bluey
#
# Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
# licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#
# Prerequisites: grub-mkrescue, grub-pc-bin, xorriso
# Usage: Run from repository root after 'make'

set -e
cd "$(dirname "$0")/.."

BUILD_DIR=${BUILD_DIR:-build}
KERNEL_IMAGE=${KERNEL_IMAGE:-$BUILD_DIR/blueyos.elf}
ISO_IMAGE=${ISO_IMAGE:-$BUILD_DIR/blueyos.iso}
ISO_STAGE_DIR=${ISO_STAGE_DIR:-$BUILD_DIR/isodir}

echo "BlueyOS: Building ISO image..."
echo "  (This is the best day EVER! - Bluey)"

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "ERROR: $KERNEL_IMAGE not found. Run 'make' first!"
    exit 1
fi

mkdir -p "$ISO_STAGE_DIR/boot/grub"
cp "$KERNEL_IMAGE" "$ISO_STAGE_DIR/boot/blueyos.elf"
cp grub.cfg "$ISO_STAGE_DIR/boot/grub/grub.cfg"

grub-mkrescue -o "$ISO_IMAGE" "$ISO_STAGE_DIR/" 2>&1
echo ""
echo "  Done! $ISO_IMAGE created ($(du -sh "$ISO_IMAGE" | cut -f1))"
echo "  Run with: bash tools/qemu-run.sh"
echo "  Or:       make run"

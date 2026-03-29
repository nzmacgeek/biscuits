#!/usr/bin/env bash
# tools/mkdisk.sh - Create a bootable BlueyOS ISO image
# "Time to pack up and go home!" - Bluey
#
# Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
# licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#
# Prerequisites: grub-mkrescue, grub-pc-bin, xorriso, mtools
# Usage: Run from repository root after 'make'

set -e
cd "$(dirname "$0")/.."

echo "BlueyOS: Building ISO image..."
echo "  (This is the best day EVER! - Bluey)"

if [ ! -f blueyos.elf ]; then
    echo "ERROR: blueyos.elf not found. Run 'make' first!"
    exit 1
fi

mkdir -p isodir/boot/grub
cp blueyos.elf isodir/boot/blueyos.elf
cp grub.cfg    isodir/boot/grub/grub.cfg

grub-mkrescue -o blueyos.iso isodir/ 2>&1
echo ""
echo "  Done! blueyos.iso created ($(du -sh blueyos.iso | cut -f1))"
echo "  Run with: bash tools/qemu-run.sh"
echo "  Or:       make run"

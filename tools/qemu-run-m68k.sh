#!/usr/bin/env bash
# tools/qemu-run-m68k.sh - Launch BlueyOS M68K in QEMU with serial capture

set -euo pipefail

cd "$(dirname "$0")/.."

if [ ! -f blueyos-m68k.elf ]; then
    echo "ERROR: blueyos-m68k.elf not found. Run 'make ARCH=m68k blueyos-m68k.elf' first!"
    exit 1
fi

PID_FILE="qemu.pid"
SERIAL_A_LOG="qemu-serial-a.log"
SERIAL_B_LOG="qemu-serial-b.log"
DISPLAY_MODE="${BLUEYOS_M68K_DISPLAY:-headless}"
RAM_MB="${BLUEYOS_M68K_RAM_MB:-32}"
QEMU_EXTRA_ARGS=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --gtk)
            DISPLAY_MODE="gtk"
            shift
            ;;
        --headless)
            DISPLAY_MODE="headless"
            shift
            ;;
        --)
            shift
            while [ "$#" -gt 0 ]; do
                QEMU_EXTRA_ARGS+=("$1")
                shift
            done
            ;;
        *)
            QEMU_EXTRA_ARGS+=("$1")
            shift
            ;;
    esac
done

if [ -s "$PID_FILE" ]; then
    OLD_PID="$(cat "$PID_FILE")"
    if kill -0 "$OLD_PID" 2>/dev/null; then
        echo "Stopping existing QEMU PID $OLD_PID"
        kill "$OLD_PID"
        sleep 1
    fi
fi

rm -f "$PID_FILE" "$SERIAL_A_LOG" "$SERIAL_B_LOG"

echo "Starting BlueyOS M68K in QEMU..."
echo "  Machine : q800"
echo "  RAM     : ${RAM_MB} MB"
echo "  Display : $DISPLAY_MODE"
echo "  Audio   : disabled"
echo "  SerialA : $SERIAL_A_LOG"
echo "  SerialB : $SERIAL_B_LOG"
echo ""

MACHINE_ARG="q800,audiodev=noaudio"
DISPLAY_ARGS=()

if [ "$DISPLAY_MODE" = "headless" ]; then
    MACHINE_ARG="$MACHINE_ARG,graphics=off"
    DISPLAY_ARGS=(-display none)
else
    DISPLAY_ARGS=(-display gtk)
fi

qemu-system-m68k \
    -M "$MACHINE_ARG" \
    -m "$RAM_MB" \
    -audiodev none,id=noaudio \
    "${DISPLAY_ARGS[@]}" \
    -monitor none \
    -serial file:"$SERIAL_A_LOG" \
    -serial file:"$SERIAL_B_LOG" \
    -pidfile "$PID_FILE" \
    -daemonize \
    -kernel blueyos-m68k.elf \
    "${QEMU_EXTRA_ARGS[@]}"

sleep 1

if [ ! -s "$PID_FILE" ]; then
    echo "ERROR: QEMU did not write $PID_FILE"
    exit 1
fi

echo "QEMU started with PID $(cat "$PID_FILE")"
echo "Check $SERIAL_A_LOG and $SERIAL_B_LOG for captured serial output."
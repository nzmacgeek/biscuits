#!/usr/bin/env bash
set -euo pipefail
ELF=${1:-build/blueyos.elf}
OUT_ADDRS=${2:-/tmp/div_addrs.txt}
OUT_GDB=${3:-/tmp/div_breaks.gdb}

if [ ! -f "$ELF" ]; then
  echo "Error: ELF '$ELF' not found" >&2
  exit 2
fi

# Find div/idiv mnemonics (AT&T or Intel suffixes) and extract addresses
# Use default objdump output (AT&T) and match any line containing 'div' or 'idiv'
objdump -d "$ELF" \
  | awk '/div/ || /idiv/ { gsub(/:/,"",$1); print "0x"$1 }' \
  | sort -u > "$OUT_ADDRS"

if [ ! -s "$OUT_ADDRS" ]; then
  echo "No div/idiv instructions found in $ELF" >&2
  exit 0
fi

# Generate a simple GDB command file that sets breakpoints at each address
{
  echo "# Auto-generated breakpoints for div/idiv in $ELF"
  echo "set pagination off"
  # Read addresses file explicitly to avoid empty loop when redirected
  if [ -s "$OUT_ADDRS" ]; then
    while IFS= read -r addr; do
      # Validate basic hex form
      if [[ "$addr" =~ ^0x[0-9a-fA-F]+$ ]]; then
        echo "break *$addr"
      fi
    done < "$OUT_ADDRS"
  fi
  echo "echo Generated $(wc -l < \"$OUT_ADDRS\") breakpoints\n"
} > "$OUT_GDB"

chmod +x "$OUT_GDB" 2>/dev/null || true

echo "Wrote $OUT_ADDRS and $OUT_GDB"
exit 0

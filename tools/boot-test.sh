#!/usr/bin/env bash
# Repeated boot test for BlueyOS
# Usage: tools/boot-test.sh [runs] [timeout-seconds]

set -euo pipefail

RUNS=${1:-5}
TIMEOUT=${2:-30}
BUILD_DIR=${BUILD_DIR:-build}

TMP_PREFIX=/tmp/blueyos-boot

echo "Boot test: runs=$RUNS timeout=${TIMEOUT}s BUILD_DIR=${BUILD_DIR}"
for i in $(seq 1 $RUNS); do
  echo "=== boot $i ==="
  OUT=${TMP_PREFIX}-$i.log
  BUILD_DIR=$BUILD_DIR timeout ${TIMEOUT}s bash tools/qemu-run.sh -display none -monitor none > "$OUT" 2>&1 || true
  if grep -q "*** OOPS" "$OUT"; then
    echo "OOPS detected on run $i: see $OUT"
    tail -n 120 "$OUT"
    exit 2
  fi
  if grep -Eqi "\binit\b|starting init|init:|/bin/init" "$OUT"; then
    echo "Init-related output detected on run $i: see $OUT"
    tail -n 120 "$OUT"
    exit 0
  fi
  echo "No OOPS detected on run $i; continuing"
done

echo "Completed $RUNS runs with no OOPS detected (logs: ${TMP_PREFIX}-*.log)"
exit 0

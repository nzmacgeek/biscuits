#!/usr/bin/env bash
# Compatibility wrapper: forwards to scripts/build-disk-with-stage.sh if present.

set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
if [ -x "$ROOT_DIR/scripts/build-disk-with-stage.sh" ]; then
  exec "$ROOT_DIR/scripts/build-disk-with-stage.sh" "$@"
else
  echo "scripts/build-disk-with-stage.sh missing; falling back to tools implementation"
  python3 "$SCRIPT_DIR/mkbluey_disk.py" "$@"
fi

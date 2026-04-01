#!/usr/bin/env bash
# Wrapper to relocated script
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
if [ -x "$ROOT_DIR/scripts/experimental/debug/debug-gdb.sh" ]; then
  exec "$ROOT_DIR/scripts/experimental/debug/debug-gdb.sh" "$@"
else
  echo "scripts/experimental/debug/debug-gdb.sh not found" >&2
  exit 1
fi

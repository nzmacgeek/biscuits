#!/usr/bin/env bash
# Wrapper to relocated script
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
if [ -x "$ROOT_DIR/scripts/experimental/debug/gdb-on-oops.sh" ]; then
  exec "$ROOT_DIR/scripts/experimental/debug/gdb-on-oops.sh" "$@"
else
  echo "scripts/experimental/debug/gdb-on-oops.sh not found" >&2
  exit 1
fi

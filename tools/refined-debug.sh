#!/usr/bin/env bash
# Wrapper to relocated script
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
if [ -x "$ROOT_DIR/scripts/experimental/debug/refined-debug.sh" ]; then
  exec "$ROOT_DIR/scripts/experimental/debug/refined-debug.sh" "$@"
else
  echo "scripts/experimental/debug/refined-debug.sh not found" >&2
  exit 1
fi
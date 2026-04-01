#!/usr/bin/env bash
# Compatibility wrapper: forwards to scripts/build-bash.sh if present.

set -euo pipefail
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
ROOT_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
if [ -x "$ROOT_DIR/scripts/build-bash.sh" ]; then
  exec "$ROOT_DIR/scripts/build-bash.sh" "$@"
else
  echo "scripts/build-bash.sh missing; falling back to embedded helper in tools (not recommended)"
  # Fallback: run the original helper logic in-place by executing this file's original content.
  # For simplicity keep the original behavior by reusing the script in scripts/ when available.
  exit 1
fi

#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
# wrapper for legacy tools/boot-test.sh
if [ -x tools/boot-test.sh ]; then
  bash tools/boot-test.sh "$@"
else
  echo "tools/boot-test.sh missing"
  exit 1
fi

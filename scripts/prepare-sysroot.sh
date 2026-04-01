#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# assemble build/sysroot from built artifacts
make sysroot

echo "sysroot assembled at build/sysroot"

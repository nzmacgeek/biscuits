#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR=${BUILD_DIR:-build}

echo "Rebuilding BlueyOS from a clean slate..."
make BUILD_DIR="$BUILD_DIR" full-clean
make DEBUG=1 BUILD_DIR="$BUILD_DIR" disk -j"$(nproc)"

#!/usr/bin/env bash
# Normalize line endings and script shebangs for cross-platform portability
set -e
cd "$(dirname "$0")/.."

find . -type f \( -name "*.sh" -o -name "*.c" -o -name "*.h" -o -name "*.S" -o -name "*.asm" -o -name "*.ld" -o -name "Makefile" -o -name "*.cfg" -o -name "*.md" -o -name "*.txt" \) -print0 | xargs -0 sed -i 's/\r$//' || true
find . -type f -name "*.sh" -print0 | xargs -0 chmod +x || true
# Prefer env-wrapped shebang for portability
find . -type f -name "*.sh" -print0 | xargs -0 sed -i '1s|#!/bin/bash|#!/usr/bin/env bash|' || true

# Show short git status to indicate changed files (no-op outside a git repo)
git status --porcelain || true

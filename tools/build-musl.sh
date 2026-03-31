#!/usr/bin/env bash
set -euo pipefail

# Simple musl cross-build helper (stub).
# Usage:
#   TOOLS_DIR=./tools ./tools/build-musl.sh --prefix=./tools/musl-install
# Environment hints:
#   - CC: cross compiler (e.g. i686-linux-gnu-gcc) or set CROSS_COMPILE
#   - TARGET: target triplet (default: i386-linux-gnu)

PREFIX=./tools/musl-install
TARGET=${TARGET:-i386-linux-gnu}
MUSL_VERSION=${MUSL_VERSION:-1.2.4}
MUSL_TARBALL=musl-${MUSL_VERSION}.tar.gz
MUSL_URL=https://musl.libc.org/releases/${MUSL_TARBALL}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix=*) PREFIX=${1#*=}; shift ;;
    --target=*) TARGET=${1#*=}; shift ;;
    --musl-version=*) MUSL_VERSION=${1#*=}; shift ;;
    *) echo "Unknown arg: $1"; exit 1 ;;
  esac
done

mkdir -p /tmp/musl-build
cd /tmp/musl-build

if [ ! -f "${MUSL_TARBALL}" ]; then
  echo "Downloading musl ${MUSL_VERSION}..."
  curl -fsSLO "${MUSL_URL}"
fi

rm -rf musl-${MUSL_VERSION}
tar xzf ${MUSL_TARBALL}
pushd musl-${MUSL_VERSION}

echo "Configuring musl for target ${TARGET} (prefix=${PREFIX})"

# Allow the user to provide a cross-compiler via CC or CROSS_COMPILE
if [ -n "${CC-}" ]; then
  export CC
elif [ -n "${CROSS_COMPILE-}" ]; then
  export CC=${CROSS_COMPILE}gcc
fi

./configure --prefix=${PREFIX} --target=${TARGET} || true
make -j$(nproc)
make install DESTDIR=${PREFIX}

popd
echo "musl install should be under: ${PREFIX}"

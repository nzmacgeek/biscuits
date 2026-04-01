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

# If building for BlueyOS, inject BlueyOS syscall numbers so musl's
# syscall wrappers use the kernel's numbering.
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
if printf "%s" "${TARGET}" | grep -qi "blueyos"; then
  if [ -f "${SCRIPT_DIR}/musl-blueyos-syscall.h" ]; then
    echo "Patching musl syscall table for BlueyOS target"
    mkdir -p include/bits
    cp "${SCRIPT_DIR}/musl-blueyos-syscall.h" include/bits/syscall.h
  else
    echo "Warning: musl-blueyos-syscall.h not found in ${SCRIPT_DIR}; continuing without patch"
  fi
fi

# Allow the user to provide a cross-compiler via CC or CROSS_COMPILE
if [ -n "${CC-}" ]; then
  export CC
elif [ -n "${CROSS_COMPILE-}" ]; then
  export CC=${CROSS_COMPILE}gcc
fi

# Ensure archive/linker tools are set so musl's Makefile doesn't look for
# target-prefixed binutils (e.g. i386-blueyos-ar) when using a host multilib toolchain.
if [ -n "${AR-}" ]; then
  export AR
elif [ -n "${CROSS_COMPILE-}" ]; then
  export AR=${CROSS_COMPILE}ar
else
  export AR=ar
fi

if [ -n "${RANLIB-}" ]; then
  export RANLIB
elif [ -n "${CROSS_COMPILE-}" ]; then
  export RANLIB=${CROSS_COMPILE}ranlib
else
  export RANLIB=ranlib
fi

if [ -n "${NM-}" ]; then
  export NM
elif [ -n "${CROSS_COMPILE-}" ]; then
  export NM=${CROSS_COMPILE}nm
else
  export NM=nm
fi

if [ -n "${STRIP-}" ]; then
  export STRIP
elif [ -n "${CROSS_COMPILE-}" ]; then
  export STRIP=${CROSS_COMPILE}strip
else
  export STRIP=strip
fi

./configure --prefix=${PREFIX} --target=${TARGET}
make -j$(nproc)
make install

popd
echo "musl install should be under: ${PREFIX}"

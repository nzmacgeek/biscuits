#!/usr/bin/env bash
set -euo pipefail

# Build the local musl-blueyos tree and install it to the destinations this
# repo actually uses:
# 1. a local build prefix for `make musl-init` and helper scripts
# 2. the external BlueyOS runtime sysroot used by `make disk`
# 3. an optional musl prefix under the cross toolchain tree

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
REPO_ROOT=$(cd "${SCRIPT_DIR}/.." && pwd)

SOURCE_DIR="${REPO_ROOT}/musl-blueyos"
BUILD_ROOT="${REPO_ROOT}/build/musl-build"
LOCAL_PREFIX="${REPO_ROOT}/build/userspace/musl"
SYSROOT_DEST="/opt/blueyos-sysroot"
CROSS_PREFIX="/opt/blueyos-cross/musl"
TARGET="${TARGET:-i386-linux-gnu}"
JOBS="${JOBS:-$(nproc)}"

INSTALL_LOCAL=1
INSTALL_SYSROOT=1
INSTALL_CROSS=1

usage() {
  cat <<'EOF'
Usage: tools/build-musl.sh [options]

Options:
  --source=DIR         musl source tree to build (default: repo musl-blueyos)
  --build-root=DIR     out-of-tree build root (default: repo build/musl-build)
  --prefix=DIR         local musl install prefix for repo builds
  --sysroot=DIR        runtime sysroot root directory (installs into DIR/usr and DIR/lib)
  --cross-prefix=DIR   cross musl prefix (wrapper/tools install under DIR)
  --target=TRIPLET     musl target triplet (default: i386-linux-gnu)
  --jobs=N             parallel make jobs (default: nproc)
  --skip-local         skip local repo musl install
  --skip-sysroot       skip runtime sysroot install
  --skip-cross         skip cross-prefix install

Environment overrides:
  CC, AR, RANLIB, NM, STRIP, TARGET, JOBS
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --source=*) SOURCE_DIR=${1#*=}; shift ;;
    --build-root=*) BUILD_ROOT=${1#*=}; shift ;;
    --prefix=*) LOCAL_PREFIX=${1#*=}; shift ;;
    --sysroot=*) SYSROOT_DEST=${1#*=}; shift ;;
    --cross-prefix=*) CROSS_PREFIX=${1#*=}; shift ;;
    --target=*) TARGET=${1#*=}; shift ;;
    --jobs=*) JOBS=${1#*=}; shift ;;
    --skip-local) INSTALL_LOCAL=0; shift ;;
    --skip-sysroot) INSTALL_SYSROOT=0; shift ;;
    --skip-cross) INSTALL_CROSS=0; shift ;;
    --help|-h) usage; exit 0 ;;
    *) echo "Unknown arg: $1"; usage; exit 1 ;;
  esac
done

if [[ ! -d "${SOURCE_DIR}" ]]; then
  echo "musl source tree not found: ${SOURCE_DIR}" >&2
  exit 1
fi

if [[ -z "${CC:-}" ]]; then
  if command -v i686-linux-gnu-gcc >/dev/null 2>&1; then
    CC="i686-linux-gnu-gcc -m32"
  else
    CC="gcc -m32"
  fi
fi
AR=${AR:-ar}
RANLIB=${RANLIB:-ranlib}
NM=${NM:-nm}
STRIP=${STRIP:-strip}

mkdir -p "${BUILD_ROOT}"

ensure_writable_dir() {
  local path="$1"
  local label="$2"

  if [[ -e "${path}" ]]; then
    if [[ ! -w "${path}" ]]; then
      echo "[MUSL] ${label} is not writable: ${path}" >&2
      echo "       override the destination or adjust ownership/permissions first" >&2
      exit 1
    fi
    return 0
  fi

  local parent
  parent=$(dirname "${path}")
  if [[ ! -d "${parent}" || ! -w "${parent}" ]]; then
    echo "[MUSL] cannot create ${label}: ${path}" >&2
    echo "       parent directory is not writable: ${parent}" >&2
    echo "       override the destination or adjust ownership/permissions first" >&2
    exit 1
  fi
}

build_and_install() {
  local name="$1"
  local prefix="$2"
  local syslibdir="$3"
  local destdir="$4"
  local wrapper_arg="$5"
  local build_dir="${BUILD_ROOT}/${name}"

  echo "[MUSL] Configuring ${name} build"
  rm -rf "${build_dir}"
  mkdir -p "${build_dir}"

  pushd "${build_dir}" >/dev/null
  CC="${CC}" \
  AR="${AR}" \
  RANLIB="${RANLIB}" \
  NM="${NM}" \
  STRIP="${STRIP}" \
  "${SOURCE_DIR}/configure" \
    --prefix="${prefix}" \
    --syslibdir="${syslibdir}" \
    --target="${TARGET}" \
    ${wrapper_arg}

  make -j"${JOBS}"
  if [[ -n "${destdir}" ]]; then
    make install DESTDIR="${destdir}"
  else
    make install
  fi
  popd >/dev/null
}

if [[ "${INSTALL_LOCAL}" == "1" ]]; then
  ensure_writable_dir "${LOCAL_PREFIX}" "local musl prefix"
  mkdir -p "${LOCAL_PREFIX}"
  build_and_install local "${LOCAL_PREFIX}" "${LOCAL_PREFIX}/lib" "" "--enable-wrapper=gcc"
fi

if [[ "${INSTALL_SYSROOT}" == "1" ]]; then
  ensure_writable_dir "${SYSROOT_DEST}" "runtime sysroot"
  mkdir -p "${SYSROOT_DEST}"
  build_and_install sysroot "/usr" "/lib" "${SYSROOT_DEST}" "--disable-wrapper"
fi

if [[ "${INSTALL_CROSS}" == "1" ]]; then
  ensure_writable_dir "${CROSS_PREFIX}" "cross musl prefix"
  mkdir -p "${CROSS_PREFIX}"
  build_and_install cross "${CROSS_PREFIX}" "${CROSS_PREFIX}/lib" "" "--enable-wrapper=gcc"
fi

echo "[MUSL] complete"
if [[ "${INSTALL_LOCAL}" == "1" ]]; then
  echo "  local prefix : ${LOCAL_PREFIX}"
fi
if [[ "${INSTALL_SYSROOT}" == "1" ]]; then
  echo "  runtime sysroot : ${SYSROOT_DEST}"
fi
if [[ "${INSTALL_CROSS}" == "1" ]]; then
  echo "  cross prefix : ${CROSS_PREFIX}"
fi
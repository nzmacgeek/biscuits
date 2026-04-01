#!/usr/bin/env bash
# Helper to build readline/ncurses/bash against the musl sysroot
# Usage: scripts/build-bash.sh [--musl-prefix /tmp/blueyos-musl] [--workdir /tmp/blueyos-bld]

set -euo pipefail

MUSL_PREFIX=/tmp/blueyos-musl
WORKDIR=/tmp/blueyos-bld
READLINE_V=8.2
NCURSES_V=6.3
BASH_V=5.1

while [ $# -gt 0 ]; do
  case "$1" in
    --musl-prefix) MUSL_PREFIX="$2"; shift 2;;
    --workdir) WORKDIR="$2"; shift 2;;
    --help) echo "Usage: $0 [--musl-prefix PREFIX] [--workdir DIR]"; exit 0;;
    *) echo "Unknown arg: $1"; exit 1;;
  esac
done

echo "Build helper: MUSL_PREFIX=$MUSL_PREFIX WORKDIR=$WORKDIR"
mkdir -p "$WORKDIR/src" "$WORKDIR/build"

command -v i686-linux-gnu-gcc >/dev/null 2>&1 || { echo "i686 cross-compiler not found (i686-linux-gnu-gcc)"; exit 2; }
if [ ! -d "$MUSL_PREFIX" ]; then
  echo "Musl sysroot not found at $MUSL_PREFIX"; exit 3
fi

cd "$WORKDIR/src"

download() {
  url="$1"
  file=$(basename "$url")
  if [ ! -f "$file" ]; then
    echo "Downloading $file..."
    curl -LO "$url"
  else
    echo "$file already present"
  fi
}

build_and_install() {
  srcdir="$1"
  shift
  pushd "$srcdir"
  echo "Configuring in $srcdir"
  CFLAGS='-m32 -static -isystem ${MUSL_PREFIX}/include' \
    LDFLAGS='-L${MUSL_PREFIX}/lib' \
    ./configure --host=i686-linux-gnu --prefix=${MUSL_PREFIX} "$@"
  make -j$(nproc)
  make install
  popd
}

echo "(1/3) Readline"
download "https://ftp.gnu.org/gnu/readline/readline-${READLINE_V}.tar.gz"
tar xzf "readline-${READLINE_V}.tar.gz"
build_and_install "readline-${READLINE_V}"

echo "(2/3) ncurses"
download "https://invisible-mirror.net/archives/ncurses/ncurses-${NCURSES_V}.tar.gz"
tar xzf "ncurses-${NCURSES_V}.tar.gz"
build_and_install "ncurses-${NCURSES_V}" --with-shared=no --with-normal --without-debug

echo "(3/3) bash"
download "https://ftp.gnu.org/gnu/bash/bash-${BASH_V}.tar.gz"
tar xzf "bash-${BASH_V}.tar.gz"
EXTRACTED_DIR=""
if [ -d "bash-${BASH_V}" ]; then
  EXTRACTED_DIR="bash-${BASH_V}"
else
  EXTRACTED_DIR=$(tar -tzf "bash-${BASH_V}.tar.gz" | head -1 | cut -d/ -f1)
fi
if [ -z "$EXTRACTED_DIR" ] || [ ! -d "$EXTRACTED_DIR" ]; then
  echo "Failed to locate extracted bash sources"; exit 4
fi
pushd "$EXTRACTED_DIR"
CFLAGS='-m32 -static -isystem ${MUSL_PREFIX}/include' LDFLAGS='-L${MUSL_PREFIX}/lib' \
  ./configure --host=i686-linux-gnu --prefix=/usr --without-bash-malloc --enable-static-link
# Build bash single-threaded to avoid races generating builtins headers
make -j1
echo "Built bash: $(pwd)/bash"
echo "Note: install or copy the binary into your image root (e.g. /bin) manually."
popd

echo "Done. If build failed, inspect logs in $WORKDIR/src"

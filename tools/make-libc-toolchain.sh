#!/usr/bin/env bash
set -euo pipefail

wget https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.xz
wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
tar xf binutils-2.41.tar.xz && tar xf gcc-13.2.0.tar.xz

mkdir build-binutils && cd build-binutils
../binutils-2.41/configure --target=i686-elf \
    --prefix=/opt/blueyos-cross --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && sudo make install
cd ..

# Build GCC (C only first)
mkdir build-gcc && cd build-gcc
../gcc-13.2.0/configure --target=i686-elf \
    --prefix=/opt/blueyos-cross --disable-nls --enable-languages=c \
    --without-headers
make -j$(nproc) all-gcc all-target-libgcc
sudo make install-gcc install-target-libgcc
cd ..
export PATH="/opt/blueyos-cross/bin:$PATH"

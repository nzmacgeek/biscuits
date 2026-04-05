# Porting Bash (or a Shell) to BlueyOS

> "I'm in charge!" — Bluey Heeler, *Camping* episode  
> Episode ref: *Baby Race* — everyone arrives at their own pace

---

> **⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️**  
> BlueyOS is an AI-generated research OS.  See [SECURITY.md](../SECURITY.md).

---

## Overview

BlueyOS ships with a built-in shell (`shell/shell.c`) that handles basic
commands. This document explains how to port Bash 5.x (or any POSIX sh) to
run as a proper userspace program on BlueyOS, using the `int 0x80` syscall
interface and the ELF loader.

The approach has two stages:

1. **Port uClibc / musl** — a minimal C library that provides the POSIX
   runtime Bash needs (`malloc`, `printf`, `fork`, `exec`, …).
2. **Port Bash** — configure it to use the ported libc and the BlueyOS
   syscall numbers.

BlueyOS is now moving toward a Linux-like root filesystem model as well:

1. The kernel chooses a root device from boot parameters such as
    `root=/dev/hda1 rootfstype=biscuitfs`.
2. The mounted root should provide a conventional Unix tree:
    `/bin`, `/boot`, `/etc`, `/lib`, `/root`, `/tmp`, `/usr`, `/var`.
3. User-space programs should be executable from any absolute VFS path,
    rather than being special-cased to fixed kernel locations.

---

## 0. Building BlueyOS and the musl sysroot

This section covers building the kernel, assembling an out-of-tree sysroot, and
building musl-blueyos into that sysroot so userspace programs can be linked
against it independently of this source tree.

### 0.1 Host prerequisites

```bash
# Debian / Ubuntu
sudo apt-get install \
    build-essential gcc-multilib nasm \
    gcc-i686-linux-gnu g++-i686-linux-gnu \
    binutils-i686-linux-gnu \
    xorriso grub-pc-bin \
    qemu-system-x86 \
    python3 curl git
```

`gcc-multilib` provides `-m32` support used by the i386 kernel build.
`gcc-i686-linux-gnu` is the Debian multiarch cross-compiler used to build userspace
binaries (bash, claw, dimsim). No separate §0.8 cross-compiler build is required when
this package is available.
`xorriso` and `grub-pc-bin` are only needed for `make iso` / `make disk`.

### 0.2 Building the kernel

```bash
git clone https://github.com/nzmacgeek/biscuits.git
cd biscuits

# Release build (default)
make -j$(nproc)

# Debug build (adds -g -O0 -DDEBUG)
make DEBUG=1 -j$(nproc)
```

The kernel ELF lands at `build/kernel/bkernel`.

To produce a bootable ISO or raw disk image:

```bash
make tools-host   # compile mkfs / mkswap / fsck host utilities
make iso          # grub + kernel ISO  → build/blueyos.iso
make disk         # BiscuitFS disk image → build/blueyos.raw
```

### 0.3 Sysroot layout (out-of-tree)

Pick a location outside the biscuits repository so it can be shared across
builds and toolchain upgrades:

```
/opt/blueyos-sysroot/
├── bin/          # userspace binaries (init, bash, …)
├── boot/         # kernel image copied here for disk assembly
├── etc/          # runtime config (passwd, shells, environment)
├── lib/          # musl libc.a and, later, shared objects
└── usr/
    ├── include/  # musl public headers
    └── lib/      # additional static archives
```

```bash
export BLUEYOS_SYSROOT=/opt/blueyos-sysroot
sudo mkdir -p \
    ${BLUEYOS_SYSROOT}/{bin,boot,etc,lib,sbin} \
    ${BLUEYOS_SYSROOT}/usr/{include,lib}
sudo chown -R $(id -u):$(id -g) ${BLUEYOS_SYSROOT}
```

The kernel supports an `init=` boot argument (default `/sbin/init`), so place
your init binary at `${BLUEYOS_SYSROOT}/sbin/init`.

### 0.4 Building musl-blueyos

The `musl-blueyos` repository is a fork of musl with BlueyOS-specific syscall
mappings pre-applied.

```bash
git clone https://github.com/nzmacgeek/musl-blueyos.git
cd musl-blueyos

# Clean any previous failed configure attempt
make distclean 2>/dev/null || true

# --target=i386-linux-musl forces ARCH=i386 (matches i?86* in configure's
# case statement).  CC is set explicitly so the cross-tool trycc probes are
# skipped.  AR and RANLIB must be set explicitly; otherwise configure
# derives them as $(CROSS_COMPILE)ar which won't exist on the host.
# --arch is NOT a real option in this configure script -- it is silently
# ignored; only --target controls ARCH.
CC="gcc -m32" \
CFLAGS="-m32" \
AR=ar \
RANLIB=ranlib \
./configure \
    --target=i386-linux-musl \
    --prefix=${BLUEYOS_SYSROOT}/usr \
    --syslibdir=${BLUEYOS_SYSROOT}/lib \
    --disable-shared

# Sanity-check before building
grep -E '^(ARCH|AR|RANLIB|CC) ' config.mak
# Expected:
#   ARCH = i386
#   AR = ar
#   RANLIB = ranlib
#   CC = gcc -m32

make -j$(nproc)
make install
```

After installation the sysroot will contain:

- `${BLUEYOS_SYSROOT}/usr/include/` — public C headers
- `${BLUEYOS_SYSROOT}/usr/lib/libc.a` — static libc archive
- `${BLUEYOS_SYSROOT}/usr/lib/crt1.o`, `crti.o`, `crtn.o` — CRT objects
- `${BLUEYOS_SYSROOT}/lib/libc.so` — dynamic loader stub (unused in static builds)

### 0.5 Linking a userspace program against the sysroot

```bash
# Compile a C file into a static i386 ELF suitable for BlueyOS
gcc -m32 -static -nostdlib -no-pie \
    -isystem ${BLUEYOS_SYSROOT}/usr/include \
    -L${BLUEYOS_SYSROOT}/usr/lib \
    -Wl,-m,elf_i386 \
    -Wl,-Ttext,0x00400000 \
    -o ${BLUEYOS_SYSROOT}/sbin/init \
    your_init.c \
    ${BLUEYOS_SYSROOT}/usr/lib/crt1.o \
    ${BLUEYOS_SYSROOT}/usr/lib/crti.o \
    -lc \
    ${BLUEYOS_SYSROOT}/usr/lib/crtn.o
```

The link address `0x00400000` matches the ELF load address expected by the
BlueyOS ELF loader.  Use `-Ttext,0x3ff000` only if you need the interpreter
stub segment at the very beginning of address space (musl's own layout does
this).

### 0.6 Packaging the sysroot into a disk image

Copy the kernel into the sysroot's boot directory, then use the biscuits
build system to wrap everything into a BiscuitFS image:

```bash
cd /path/to/biscuits

# Copy the kernel
cp build/kernel/bkernel ${BLUEYOS_SYSROOT}/boot/bkernel

# Build the disk image using the external sysroot
python3 tools/mkbluey_disk.py \
    --image build/blueyos.raw \
    --kernel build/kernel/bkernel \
    --root-extra-dir ${BLUEYOS_SYSROOT} \
    --boot-extra-dir ${BLUEYOS_SYSROOT}/boot \
    --mkfs-tool  build/tools/mkfs_blueyfs \
    --mkswap-tool build/tools/mkswap_blueyfs
```

Or, point the Makefile at the external sysroot directly:

```bash
make tools-host
make disk MUSL_PREFIX=${BLUEYOS_SYSROOT}/usr BUILD_SYSROOT=${BLUEYOS_SYSROOT}
```

### 0.7 Running in QEMU

```bash
BUILD_DIR=build bash tools/qemu-run.sh
```

The boot argument `init=/sbin/init` is the default.  Override it in
`grub.cfg` or by editing the QEMU command line:

```bash
qemu-system-i386 \
    -cdrom build/blueyos.iso \
    -hda build/blueyos.raw \
    -serial stdio \
    -append "root=/dev/hda1 rootfstype=biscuitfs init=/sbin/init"
```

### 0.8 Building the i686-elf cross-compiler

A cross-compiler that runs on the host but produces i386/i686 ELFs is needed
before any userspace can be built.  The target triplet used throughout is
`i686-elf`.

> **Triplet note:** `i686-elf` is a 2-part `cpu-os` triplet.  `elf` is
> recognised by autoconf's `config.sub` as a bare-metal system type with no
> kernel, so no patching is needed.  The `i686` CPU level is appropriate
> for QEMU's default x86 machine (Pentium Pro+).

**binutils**

```bash
export BLUEYOS_CROSS=/opt/blueyos-cross
export PATH="${BLUEYOS_CROSS}/bin:${PATH}"

wget https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.xz
tar xf binutils-2.41.tar.xz
mkdir build-binutils && cd build-binutils
../binutils-2.41/configure \
    --target=i686-elf \
    --prefix=${BLUEYOS_CROSS} \
    --with-sysroot=${BLUEYOS_SYSROOT} \
    --disable-nls \
    --disable-werror
make -j$(nproc)
sudo make install
cd ..
```

**GCC (C and C++, cross stage)**

```bash
wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
tar xf gcc-13.2.0.tar.xz
cd gcc-13.2.0 && contrib/download_prerequisites && cd ..
mkdir build-gcc-cross && cd build-gcc-cross
../gcc-13.2.0/configure \
    --target=i686-elf \
    --prefix=${BLUEYOS_CROSS} \
    --with-sysroot=${BLUEYOS_SYSROOT} \
    --disable-nls \
    --enable-languages=c,c++ \
    --without-headers \
    --disable-libstdcxx \
    --disable-shared \
    --disable-multilib
make -j$(nproc) all-gcc all-target-libgcc
sudo make install-gcc install-target-libgcc
cd ..
```

After this step, `i686-elf-gcc` and `i686-elf-g++` are available.  The musl
headers installed in §0.4 are visible via `--with-sysroot`, so you can drop
`-nostdinc` for normal userspace builds.

### 0.9 Building bash for the sysroot

Build bash 5.x as a statically-linked i386 ELF using the cross-compiler.

```bash
wget https://ftp.gnu.org/gnu/bash/bash-5.2.21.tar.gz
tar xf bash-5.2.21.tar.gz
cd bash-5.2.21

cat > config/blueyos.cache << 'EOF'
ac_cv_func_getpgrp_void=yes
ac_cv_func_setpgrp_void=yes
ac_cv_func_getcwd_malloc=no
ac_cv_have_decl_alarm=no
ac_cv_func_lstat=yes
ac_cv_func_readlink=no
ac_cv_sys_restartable_syscalls=yes
bash_cv_job_control_missing=missing
bash_cv_sys_named_pipes=missing
bash_cv_func_sigsetjmp=yes
bash_cv_have_posix_sigsetjmp=yes
bash_cv_must_reinstall_sighandlers=no
bash_cv_func_ctype_nonascii=no
bash_cv_wcontinued_broken=yes
EOF
# Note: bash_cv_sys_named_pipes=missing refers to FIFOs (mkfifo), not
# anonymous pipes (pipe(2)) — anonymous pipes ARE implemented in BlueyOS.

# i686-linux-gnu-gcc is a glibc cross-compiler. Even with --sysroot pointing
# to musl, GCC still searches its own 'include-fixed/' directory which contains
# processed glibc headers.  Those headers cause:
#   open64/stat64/fcntl64  — glibc LFS64 redirects (_FILE_OFFSET_BITS tricks)
#   __isoc23_strtol etc.   — glibc C23 renames injected by GCC 13+ stdlib.h
#   __mbrlen               — glibc internal alias declared in wchar.h
#   __printf_chk etc.      — FORTIFY_SOURCE from glibc stdio.h
#
# -nostdinc suppresses ALL built-in include paths (including include-fixed/).
# We then add back only:
#   GCC_INCDIR  — compiler-internal builtins (stddef.h, stdarg.h, float.h …)
#   BLUEYOS_SYSROOT/usr/include — musl public headers (clean, no glibc)
# --sysroot stays in LDFLAGS so the linker finds crt1.o and libc.a.
GCC_INCDIR=$(i686-linux-gnu-gcc -print-file-name=include)

CC=i686-linux-gnu-gcc \
AR=i686-linux-gnu-ar \
RANLIB=i686-linux-gnu-ranlib \
CFLAGS="-O2 -fno-stack-protector -D_FORTIFY_SOURCE=0 -nostdinc -isystem ${GCC_INCDIR} -isystem ${BLUEYOS_SYSROOT}/usr/include" \
LDFLAGS="-static --sysroot=${BLUEYOS_SYSROOT} -L${BLUEYOS_SYSROOT}/usr/lib" \
./configure \
    --build=$(gcc -dumpmachine) \
    --host=i686-linux-gnu \
    --without-bash-malloc \
    --disable-nls \
    --disable-readline \
    --disable-history \
    --cache-file=config/blueyos.cache \
    --prefix=/usr

make -j$(nproc)
make install DESTDIR=${BLUEYOS_SYSROOT}

# Provide /bin/sh and /bin/bash symlinks
ln -sf /usr/bin/bash ${BLUEYOS_SYSROOT}/bin/bash
ln -sf /usr/bin/bash ${BLUEYOS_SYSROOT}/bin/sh
```

At this point the sysroot has a working shell at `/bin/bash`.  The kernel will
fall back to `/bin/bash` if `/sbin/init` is not found.

### 0.10 Building claw and installing as /sbin/init

`claw` is the BlueyOS init implementation.  Adapt the paths below to wherever
the claw source tree lives.

```bash
cd /path/to/claw

# Configure against the BlueyOS sysroot using the cross-compiler.
# Use -nostdinc to prevent glibc header contamination from the cross-compiler
# (same rationale as the bash section above).
GCC_INCDIR=$(i686-linux-gnu-gcc -print-file-name=include)

CC=i686-linux-gnu-gcc \
CXX=i686-linux-gnu-g++ \
CFLAGS="-O2 -fno-stack-protector -D_FORTIFY_SOURCE=0 -nostdinc -isystem ${GCC_INCDIR} -isystem ${BLUEYOS_SYSROOT}/usr/include" \
LDFLAGS="-static --sysroot=${BLUEYOS_SYSROOT} -L${BLUEYOS_SYSROOT}/usr/lib" \
./configure \
    --build=$(gcc -dumpmachine) \
    --host=i686-linux-gnu \
    --prefix=/usr \
    --sysconfdir=/etc

make -j$(nproc)

# Install into staging sysroot; the init binary ends up at /usr/sbin/init
make install DESTDIR=${BLUEYOS_SYSROOT}

# The kernel defaults to init=/sbin/init, so place it there as well
install -Dm755 ${BLUEYOS_SYSROOT}/usr/sbin/init ${BLUEYOS_SYSROOT}/sbin/init
```

If claw uses a plain Makefile rather than autoconf, replace the `./configure`
step with whatever variable-passing idiom its Makefile expects, for example:

```bash
make -j$(nproc) \
    CC=i686-linux-gnu-gcc \
    LDFLAGS="-static --sysroot=${BLUEYOS_SYSROOT} -L${BLUEYOS_SYSROOT}/usr/lib" \
    PREFIX=/usr \
    DESTDIR=${BLUEYOS_SYSROOT} \
    install
install -Dm755 ${BLUEYOS_SYSROOT}/usr/sbin/init ${BLUEYOS_SYSROOT}/sbin/init
```

### 0.11 Building dimsim for the sysroot

`dimsim` follows the same cross-compile pattern.

```bash
cd /path/to/dimsim

GCC_INCDIR=$(i686-linux-gnu-gcc -print-file-name=include)

CC=i686-linux-gnu-gcc \
CXX=i686-linux-gnu-g++ \
CFLAGS="-O2 -fno-stack-protector -D_FORTIFY_SOURCE=0 -nostdinc -isystem ${GCC_INCDIR} -isystem ${BLUEYOS_SYSROOT}/usr/include" \
LDFLAGS="-static --sysroot=${BLUEYOS_SYSROOT} -L${BLUEYOS_SYSROOT}/usr/lib" \
./configure \
    --build=$(gcc -dumpmachine) \
    --host=i686-linux-gnu \
    --prefix=/usr

make -j$(nproc)
make install DESTDIR=${BLUEYOS_SYSROOT}
```

Again substitute the plain-Makefile form if needed:

```bash
make -j$(nproc) \
    CC=i686-linux-gnu-gcc \
    LDFLAGS="-static --sysroot=${BLUEYOS_SYSROOT} -L${BLUEYOS_SYSROOT}/usr/lib" \
    PREFIX=/usr \
    DESTDIR=${BLUEYOS_SYSROOT} \
    install
```

### 0.12 Self-hosted GCC in the sysroot (future / aspirational)

A compiler that runs *on* BlueyOS requires a nearly complete POSIX surface
(`fork`, `exec`, `waitpid`, `mmap`, full `signal` semantics, a working
`/tmp`, and a writable file system).  BlueyOS does not yet meet all of these
requirements, so this step is documented as forward-looking only.

When the syscall surface is complete the build sequence will be:

1. Use the cross-compiler from §0.8 to build a first-stage GCC targeted
   at `i686-elf` with `--host=i686-elf` (canadian cross or native cross
   depending on the build machine).
2. Boot BlueyOS with the sysroot disk, run the first-stage compiler inside
   QEMU to rebuild itself, producing a native `gcc` binary.
3. Install the native `gcc` into `/usr/bin` on the live image.

This is roughly the same three-stage bootstrap used by Linux From Scratch.

---

## 1. Prerequisites

### 1.1 Cross-compiler toolchain

Build a bare-metal i386 cross-compiler targeting BlueyOS:

```bash
# Install dependencies (Debian/Ubuntu)
sudo apt-get install build-essential bison flex texinfo wget

# Download GCC and binutils sources
wget https://ftp.gnu.org/gnu/binutils/binutils-2.41.tar.xz
wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.xz
tar xf binutils-2.41.tar.xz && tar xf gcc-13.2.0.tar.xz

# Build binutils (i386-blueyos-elf)
mkdir build-binutils && cd build-binutils
../binutils-2.41/configure --target=i386-blueyos-elf \
    --prefix=/opt/blueyos-cross --with-sysroot --disable-nls --disable-werror
make -j$(nproc) && sudo make install
cd ..

# Build GCC (C only first)
mkdir build-gcc && cd build-gcc
../gcc-13.2.0/configure --target=i386-blueyos-elf \
    --prefix=/opt/blueyos-cross --disable-nls --enable-languages=c \
    --without-headers
make -j$(nproc) all-gcc all-target-libgcc
sudo make install-gcc install-target-libgcc
cd ..
export PATH="/opt/blueyos-cross/bin:$PATH"
```

### 1.2 Root filesystem boot parameters

The i386 kernel now parses a Multiboot command line and understands:

- `root=/dev/hda1`
- `rootfstype=biscuitfs`
- `safe`

Examples for GRUB:

```grub
menuentry "BlueyOS root on BiscuitFS" {
    multiboot /boot/blueyos.elf root=/dev/hda1 rootfstype=biscuitfs
    boot
}

menuentry "BlueyOS auto-probe root" {
    multiboot /boot/blueyos.elf root=/dev/hda1
    boot
}

menuentry "BlueyOS diskless" {
    multiboot /boot/blueyos.elf root=none
    boot
}
```

At boot, the kernel currently:

- maps a small set of root device aliases such as `/dev/hda1`, `/dev/sda1`,
  and `/dev/ata0p1` to an LBA offset,
- mounts `/` from either a requested `rootfstype` or an auto-probe path,
- best-effort creates the base directories below when the filesystem is
  writable.

### 1.3 Target root layout

The intended on-disk layout is deliberately Linux-like:

```text
/
|-- bin/
|-- boot/
|-- etc/
|-- lib/
|-- root/
|-- tmp/
|-- usr/
|   |-- bin/
|   `-- lib/
`-- var/
    |-- log/
    `-- pid/
```

Expected usage:

- `/bin`: essential single-user binaries and rescue tools
- `/boot`: kernel and bootloader-visible payloads
- `/etc`: system-wide configuration
- `/lib`: system libraries and dynamic loader support later on
- `/root`: home for uid 0
- `/tmp`: disposable scratch space
- `/usr`: user-space programs and non-essential shared data
- `/var`: writable runtime state such as logs and pid files

BlueyOS does not yet implement a separate loopback mount for `/boot`; today it
is just a normal directory in the root filesystem roadmap.

---

## 2. Porting musl libc (recommended)

[musl](https://musl.libc.org/) is a clean, portable C standard library.
It is much easier to port than glibc.

### 2.1 Obtain musl

```bash
wget https://musl.libc.org/releases/musl-1.2.4.tar.gz
tar xf musl-1.2.4.tar.gz && cd musl-1.2.4
```

### 2.2 Add BlueyOS architecture support

musl uses architecture-specific assembly for syscall entry.  BlueyOS uses
Linux-compatible syscall numbers on `int 0x80` (i386), so the existing
`arch/i386` directory works with minor changes:

**`arch/i386/syscall_arch.h`** — verify these numbers match `kernel/syscall.h`:

```c
/* BlueyOS syscall numbers (int 0x80, i386 ABI - current kernel subset) */
#define SYS_read       0
#define SYS_write      1
#define SYS_open       2
#define SYS_close      3
#define SYS_getpid    20
#define SYS_getuid    24
#define SYS_getgid    47
#define SYS_brk       45   /* heap growth */
#define SYS_execve    11   /* in-place image replacement for ring-3 callers */
#define SYS_kill      62   /* minimal signal send path */
#define SYS_exit      60
#define SYS_uname     63
#define SYS_gethostname 125
```

Today, `open`, `close`, `read`, `write`, `getpid`, `getuid`, `getgid`,
`uname`, `gethostname`, `waitpid`, and a ring-3 `execve` image-replacement
path are implemented to varying degrees. Many other syscalls still return
`ENOSYS` placeholders and must be filled in before musl can act as a complete
userspace libc.

### 2.3 Configure and build musl

```bash
CC=i386-blueyos-elf-gcc \
./configure --target=i386-blueyos-elf \
            --prefix=/opt/blueyos-sysroot/usr \
            --syslibdir=/opt/blueyos-sysroot/lib \
            --disable-shared   # static only for now

make -j$(nproc)
sudo make install
```

### 2.4 Extend the BlueyOS kernel with required syscalls

For a shell and libc port to be useful, the kernel needs at least:

| Syscall | Number | Description |
|---------|--------|-------------|
| `read`  | 0      | Read from fd (fd=0 = keyboard) ✓ |
| `write` | 1      | Write to fd (fd=1 = VGA) ✓ |
| `open`  | 2      | Open a VFS path ✓ |
| `close` | 3      | Close fd ✓ |
| `stat`  | 4      | File metadata ✓ |
| `lstat` | 107    | Like stat; no symlinks so identical ✓ |
| `lseek` | 19     | Seek within a file ✓ |
| `fork`  | 57     | Clone process and address space ✓ |
| `execve`| 11     | Replace process image from a VFS path ✓ |
| `waitpid`| 61   | Wait for child ✓ |
| `wait4` | 114    | waitpid with rusage pointer ✓ |
| `brk`   | 45     | Expand heap ✓ |
| `mmap`  | 90     | Memory-mapped I/O / anonymous pages ✓ |
| `munmap`| 91     | Unmap pages ✓ |
| `mprotect`| 92   | Change page protection ✓ |
| `ioctl` | 54     | Device control (TIOCGWINSZ, TCGETS, TIOCGPGRP) ✓ |
| `chdir` | 80     | Change directory ✓ |
| `getcwd`| 183    | Get current directory ✓ |
| `pipe`  | 42     | Create pipe for IPC ✓ |
| `dup`   | 41     | Duplicate file descriptor ✓ |
| `dup2`  | 33     | Duplicate file descriptor to specific fd ✓ |
| `fcntl` | 55     | File control (F_DUPFD, F_GETFD, F_SETFD, F_GETFL, F_SETFL) ✓ |
| `access`| 85     | Check file accessibility ✓ |
| `unlink`| 10     | Remove a file ✓ |
| `mkdir` | 39     | Create directory ✓ |
| `rmdir` | 40     | Remove directory ✓ |
| `getdents`| 141  | Read directory entries ✓ |
| `getpid`| 20     | Get process ID ✓ |
| `getppid`| 64    | Get parent process ID ✓ |
| `kill`  | 62     | Send a signal ✓ |
| `rt_sigaction` | 174 | Install a signal handler ✓ |
| `rt_sigprocmask` | 175 | Block/unblock signals ✓ |
| `sigreturn` | 15 | Return from signal trampoline ✓ |
| `nanosleep` | 162 | Sleep for a duration ✓ |
| `sched_yield` | 158 | Yield the CPU ✓ |
| `exit_group` | 252 | Exit all threads (same as exit) ✓ |
| `set_tid_address` | 258 | Thread-local pointer (stub) ✓ |
| `getrandom` | 355 | Fill buffer with random bytes ✓ |
| `clock_gettime` | 265 | Get monotonic time ✓ |
| `gettimeofday` | 78 | Get time of day ✓ |

Current status by bucket:

- Implemented: `read`, `write`, `open`, `close`, `stat`, `fstat`, `lstat`,
    `lseek`, `getpid`, `getppid`, `getuid`, `getgid`, `uname`, `gethostname`,
    `gettimeofday`, `clock_gettime`, `waitpid`, `wait4`, `fork`, `execve`,
    `rt_sigaction`, `rt_sigprocmask`, `sigreturn`, `brk`, `mmap`, `mmap2`,
    `munmap`, `mprotect`, `kill`, `exit`, `exit_group`, `ioctl`, `chdir`,
    `getcwd`, `pipe`, `dup`, `dup2`, `fcntl`, `access`, `unlink`, `mkdir`,
    `rmdir`, `getdents`, `nanosleep`, `sched_yield`, `set_tid_address`,
    `getrandom`, `setpgid`, `getpgid`, `getpgrp`, `mount`, `umount2`,
    `poll`, `reboot`.
- Partially scaffolded: `kill` plus kernel signal numbers and pending-signal
    bits on each process; `execve` now copies `argv`/`envp`, loads into a fresh
    per-process address space, and returns to ring 3 through the syscall `iret`
    path; the kernel can also bootstrap `/bin/init` or `/bin/bash` directly at
    boot.
- Still missing for full POSIX completeness: a fully blocking `waitpid` path
    (currently polls/degrades to EAGAIN for non-WNOHANG with live children),
    `select`/`pselect6`, socket syscalls, `readlink`, `symlink`, `rename`,
    `link`, `chown`, `chmod`, `truncate`, `ftruncate`, `setuid`/`setgid`,
    full `getdents64`, TLS/thread setup (`set_thread_area`), `getrlimit`,
    and alternate signal stacks.

The short-term musl goal should be:

1. Complete file-descriptor syscalls around the existing VFS — **done**.
2. Add process-image replacement from a path-based ELF loader — **done**.
3. Add `brk` and a minimal `mmap` for libc allocation and loader support — **done**.
4. Tighten signal semantics around restart rules, ignored handlers across
    `execve`, and uncatchable defaults.
5. Replace the current polling-style `waitpid` fallback with a true blocking
    wait that can resume an in-kernel continuation after child exit.

Add each in `kernel/syscall.c` following the existing pattern:

```c
// In syscall_handler():
case SYS_OPEN:
    regs->eax = (uint32_t)vfs_open((const char *)regs->ebx,
                                    (int)regs->ecx);
    break;
case SYS_CLOSE:
    regs->eax = (uint32_t)vfs_close((int)regs->ebx);
    break;
case SYS_BRK:
    regs->eax = (uint32_t)kheap_brk(regs->ebx);
    break;
```

### 2.5 Signals roadmap

Signals are now part of the kernel roadmap and can no longer be deferred if a
real shell is the target.

Minimum useful signal set:

- `SIGHUP`
- `SIGINT`
- `SIGQUIT`
- `SIGKILL`
- `SIGTERM`
- `SIGCHLD`
- `SIGSTOP`
- `SIGCONT`
- `SIGALRM`

Kernel stages:

1. Signal numbers and per-process pending masks.
2. `kill(pid, sig)` and default actions.
3. Delivery on return to user mode.
4. `sigaction` / `rt_sigaction`.
5. `sigprocmask` and inherited masks across `fork`.
6. User-space trampoline and `sigreturn`.

Current kernel state:

- signal numbers are defined,
- processes track pending and blocked masks,
- `kill` has a minimal kernel implementation,
- fatal default actions drive zombie exit state and queue `SIGCHLD` for the parent,
- user-installed handlers are now installable through `rt_sigaction`,
- pending signals can be delivered on user-mode return,
- a shared user-space trampoline invokes `sigreturn`,
- full restart semantics and alternate stacks are not implemented yet.

---

## 3. Porting Bash 5.x

### 3.1 Obtain Bash

```bash
wget https://ftp.gnu.org/gnu/bash/bash-5.2.21.tar.gz
tar xf bash-5.2.21.tar.gz && cd bash-5.2.21
```

### 3.2 Create a BlueyOS host config

Create `config/blueyos.cache` to tell `configure` what the target supports:

```bash
cat > config/blueyos.cache << 'EOF'
ac_cv_func_getpgrp_void=yes
ac_cv_func_setpgrp_void=yes
ac_cv_func_getcwd_malloc=no
ac_cv_have_decl_alarm=no
ac_cv_func_lstat=no
ac_cv_func_readlink=no
ac_cv_sys_restartable_syscalls=yes
bash_cv_job_control_missing=missing
bash_cv_sys_named_pipes=missing
bash_cv_func_sigsetjmp=missing
bash_cv_must_reinstall_sighandlers=no
bash_cv_func_ctype_nonascii=no
bash_cv_wcontinued_broken=yes
EOF
```

### 3.3 Configure Bash

```bash
CC=i386-blueyos-elf-gcc \
AR=i386-blueyos-elf-ar \
RANLIB=i386-blueyos-elf-ranlib \
CFLAGS="-O2 -ffreestanding -I/opt/blueyos-sysroot/usr/include" \
LDFLAGS="-static -L/opt/blueyos-sysroot/usr/lib" \
./configure --host=i386-blueyos-elf \
            --target=i386-blueyos-elf \
            --without-bash-malloc \
            --disable-nls \
            --disable-readline \
            --cache-file=config/blueyos.cache \
            --prefix=/

make -j$(nproc)
```

### 3.4 Reduce dependencies

Bash by default requires `readline`, `termcap`/`ncurses`, and many POSIX
features. For an initial port:

```c
/* In config.h, disable these: */
#undef HAVE_READLINE
#undef HAVE_HISTORY
#undef HAVE_TERMCAP
#undef HAVE_SIGACTION   /* until BlueyOS implements it */
#undef HAVE_TIMES
#undef HAVE_GETRUSAGE
```

Also expect to keep job control and asynchronous signal handling disabled until
the kernel grows real process groups, sessions, and `sigaction` delivery.

### 3.5 Package and install to BiscuitFS image

```bash
# Copy the bash binary into a BiscuitFS image
# First, create the image:
tools/mkfs.biscuitfs -L "BlueyRoot" -s 128 blueyos-root.img

# Mount the image (Linux host):
sudo mount -o loop,offset=0 blueyos-root.img /mnt/blueyos

# Create directory structure
sudo mkdir -p /mnt/blueyos/{bin,boot,etc,lib,root,tmp,usr/bin,usr/lib,var/log,var/pid,dev,proc,home/bandit}

# Copy bash
sudo cp bash /mnt/blueyos/bin/sh
sudo cp bash /mnt/blueyos/bin/bash

# Copy musl shared library (if using dynamic linking)
# sudo cp /opt/blueyos-sysroot/lib/libc.so /mnt/blueyos/lib/

# Minimal /etc/passwd (BlueyOS reads this from the kernel, but bash wants it)
sudo bash -c 'cat > /mnt/blueyos/etc/passwd << EOF
bandit:x:0:0:Bandit Heeler:/home/bandit:/bin/bash
bluey:x:1:1:Bluey Heeler:/home/bluey:/bin/sh
bingo:x:2:1:Bingo Heeler:/home/bingo:/bin/sh
EOF'

sudo bash -c 'echo "TERM=vt100" > /mnt/blueyos/etc/environment'
sudo bash -c 'echo "/bin/bash" > /mnt/blueyos/etc/shells'

sudo umount /mnt/blueyos
```

---

## 4. Launching Bash from the BlueyOS kernel

The current ELF loader can now read an ELF image from a VFS path, copy in
`argv` and `envp`, build a minimal initial user stack image, load segments into
a fresh per-process address space, and return to ring 3 through the syscall
`iret` path. The kernel also has a first-user bootstrap path that tries
`/bin/init` and then `/bin/bash` before falling back to the built-in shell.

What still needs to exist before `/bin/bash` can be started like a normal Unix
program:

1. A fully blocking `waitpid()` path that can sleep in-kernel without losing
    its continuation.
2. `brk`/`mmap` and the rest of the libc memory-management surface.
3. Better signal semantics: restartable syscalls, `SA_RESTART`, and alternate stacks.
4. A cleaner fallback path once the last user process exits.
5. File-descriptor inheritance and the rest of the shell-facing POSIX surface.

Current kernel behavior:

- ring-3 callers may invoke `SYS_EXECVE`
- the kernel copies `path`, `argv`, and `envp` into kernel memory first
- the kernel loads the ELF into a fresh page directory and replaces the current
    process image in place
- the syscall return frame is rewritten so `iret` enters the new image in ring 3
- the kernel can bootstrap the first user process directly from `kernel_main`
- `fork()` now clones the current user address space and trap frame
- `SYS_WAITPID` now reaps real child zombies and enforces parent/child matching,
    but non-`WNOHANG` waits still degrade to an `EAGAIN`-style polling path

The eventual shape is still roughly:

```c
// Future direction once the first user bootstrap exists:
execve("/bin/bash", argv, envp);
```

Treat that snippet as a roadmap target, not as something the current kernel
can already do.

---

## 5. Alternatively: port ash (Busybox sh)

If Bash is too large, [BusyBox](https://busybox.net/) provides `ash`
(a minimal POSIX shell) with far fewer dependencies:

```bash
wget https://busybox.net/downloads/busybox-1.36.1.tar.bz2
tar xf busybox-1.36.1.tar.bz2 && cd busybox-1.36.1

# Use the minimal config
make allnoconfig
make menuconfig  # Enable: Shells -> ash, Core Utilities -> ls/cat/echo/mkdir/rm

CROSS_COMPILE=i386-blueyos-elf- \
CFLAGS="-I/opt/blueyos-sysroot/usr/include" \
LDFLAGS="-static -L/opt/blueyos-sysroot/usr/lib" \
make -j$(nproc)

# The result is a single statically-linked binary
cp busybox /path/to/blueyos-root.img/bin/ash
```

---

## 6. BiscuitFS disk image for QEMU

To create a complete bootable QEMU disk with BlueyOS + Bash:

```bash
# 1. Build the kernel
make iso                      # produces blueyos.iso

# 2. Create the data disk with BiscuitFS
tools/mkfs.biscuitfs -L "BlueyRoot" -s 128 disk.img

# 3. Populate the disk (see §3.5 above)

# 4. Create a swap partition image
dd if=/dev/zero of=swap.img bs=1M count=64
# Write swap header (BlueyOS mkswap is planned; for now use the kernel API)
# kernel: swap_format(start_lba, num_pages, "blueyswap");

# 5. Run in QEMU
qemu-system-i386 \
    -cdrom blueyos.iso \
    -hda  disk.img \
    -hdb  swap.img \
    -m 128 \
    -serial stdio \
    -display sdl

# The kernel will:
#   - Boot from the ISO (GRUB)
#   - Mount disk.img as BiscuitFS at /  (hda, detected by ata_init)
#   - Mount swap.img as swap space      (hdb, auto-detected)
#   - Launch /bin/bash if present, else fall back to built-in shell
```

---

## 7. Current BlueyOS syscall numbers (kernel/syscall.h)

```c
/* File I/O */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSEEK       19
#define SYS_UNLINK      10
#define SYS_MKDIR       39
#define SYS_RMDIR       40
#define SYS_DUP         41
#define SYS_PIPE        42
#define SYS_DUP2        33
#define SYS_FCNTL       55
#define SYS_IOCTL       54
#define SYS_ACCESS      85
#define SYS_LSTAT       107
#define SYS_GETDENTS    141
#define SYS_CHDIR       80
#define SYS_GETCWD      183
/* Process */
#define SYS_EXECVE      11
#define SYS_FORK        57
#define SYS_CLONE       120
#define SYS_WAITPID     61
#define SYS_WAIT4       114
#define SYS_EXIT        60
#define SYS_EXIT_GROUP  252
#define SYS_GETPID      20
#define SYS_GETPPID     64
#define SYS_GETUID      24
#define SYS_GETGID      47
#define SYS_KILL        62
#define SYS_SETPGID     200
#define SYS_GETPGID     201
#define SYS_GETPGRP     202
#define SYS_SCHED_YIELD 158
/* Memory */
#define SYS_BRK         45
#define SYS_MMAP        90
#define SYS_MMAP2       192
#define SYS_MUNMAP      91
#define SYS_MPROTECT    92
/* Signals */
#define SYS_RT_SIGACTION   174
#define SYS_RT_SIGPROCMASK 175
#define SYS_SIGRETURN      15
/* Time */
#define SYS_GETTIMEOFDAY   78
#define SYS_CLOCK_GETTIME  265
#define SYS_NANOSLEEP      162
/* Misc */
#define SYS_UNAME           63
#define SYS_GETHOSTNAME     125
#define SYS_SET_TID_ADDRESS 258
#define SYS_GETRANDOM       355
/* Mount / boot */
#define SYS_MOUNT       21
#define SYS_UMOUNT2     52
#define SYS_REBOOT      88
/* BlueyOS extensions */
#define SYS_POLL        168
#define SYS_DEVEV_OPEN  203
```

All of these are now implemented in `kernel/syscall.c`. The implementation
details are in `fs/vfs.c` (VFS layer), `kernel/process.c` (process management),
and `kernel/syscall.c` (dispatch and glue).

## 8. glibc roadmap

glibc is a much larger target than musl and should be treated as a second-pass
 project only after musl, BusyBox, or a small shell is working.

Why glibc is harder:

- stronger assumptions around `fork`, `execve`, `clone`, TLS, and signals,
- dynamic linker and ELF relocation expectations,
- NSS, locale, and thread runtime complexity,
- more aggressive use of `mmap`, `mprotect`, and process metadata syscalls.

Suggested glibc order of work:

1. Finish a stable musl port first.
2. Add a real dynamic linker plan for `/lib/ld-*`.
3. Implement TLS setup on i386.
4. Provide robust signal frames and restart semantics.
5. Add `mmap`, `munmap`, `mprotect`, `set_tid_address`, and thread support.
6. Only then begin glibc sysdeps work for `i386-blueyos`.

Pragmatically, musl or BusyBox `ash` should be the first success criterion;
glibc should not be on the critical path for the first usable userland.

## 9. Terminal emulation

BlueyOS currently outputs directly to the VGA text-mode buffer (80×25).
Bash requires a terminal line discipline (`termios`). For a proper experience:

1. **Implement `termios` stubs** in the keyboard driver — at minimum
   `TCSAFLUSH`, `ECHO`, `ICANON`.
2. **Set `TERM=vt100`** in Bash's environment so readline doesn't attempt
   advanced escape sequences.
3. **Map VGA escape sequences** from Bash's output to VGA cursor moves.

A minimal `vt100` driver sits between `vga.c` and the shell output:

```c
// In drivers/vt100.c (future work):
void vt100_putchar(char c) {
    static int esc_state = 0;
    // Parse ESC [ n m sequences for color, cursor movement, etc.
    // Delegate to vga_set_color() / vga_set_cursor()
}
```

---

## 10. Exception handling and crash visibility

BlueyOS now benefits from explicit exception reporting in the ISR path. Fatal
CPU traps produce a visible `PANIC` or `OOPS` banner, register dump, page-fault
decode, and a syslog entry tagged `TRAP`.

This matters for libc and shell bring-up because early userland failures often
look like silent hangs if the kernel fault path is opaque.

Still recommended:

1. Persist the last trap summary to a fixed memory buffer or crash log file.
2. Add a watchdog/heartbeat indicator for long hangs without traps.
3. Distinguish user-mode faults from kernel-mode faults and kill only the
    offending process when user mode exists.

## 11. M68K porting todo

The m68k port needs a separate staged todo because it does not yet share the
full i386 kernel feature set.

Phase 1: storage and rootfs

- bring up an m68k block-device layer, likely SCSI or Macintosh-specific disk I/O
- port the VFS mount path so `root=` works on m68k too
- ensure BiscuitFS or FAT access is stable on real m68k media geometry

Phase 2: process and syscall substrate

- define the m68k syscall ABI and trap entry path
- implement user/kernel mode transitions
- add per-process stacks and address-space rules suitable for 68030 MMU use
- make `read`, `write`, `open`, `close`, `getpid`, and `exit` work first

Phase 3: executable loading

- decide whether the first target is ELF for m68k or a temporary flat binary ABI
- add an m68k loader that does not assume i386 ELF machine ids or paging rules
- construct user stacks for argv/envp on m68k

Phase 4: signals and libc

- define m68k signal frame layout
- implement `kill`, `sigaction`, `sigprocmask`, `sigreturn`
- port musl sysdeps for the chosen m68k ABI

Phase 5: shell bring-up

- validate `sh` or BusyBox `ash` before attempting Bash
- confirm terminal control, `ioctl`, and canonical input on the Mac console path

## 12. Summary

| Step | Status | Notes |
|------|--------|-------|
| Root device via `root=` | △ | i386 boot parsing and mount selection now scaffolded |
| Linux-like root layout | △ | kernel creates base dirs best-effort on writable root |
| Build cross-compiler | ☐ | gcc + binutils targeting i386-blueyos-elf |
| Port musl libc | △ | syscall numbering and target gaps are now clearer |
| Extend kernel syscalls | △ | `open`/`close` implemented; many others still `ENOSYS` |
| Signals | △ | handlers, pending delivery, shared trampoline, and `sigreturn` exist; restart semantics and alt stacks absent |
| Real `execve` path loader | ✓ | VFS-backed load, per-process address space, ring-3 return, and first-user bootstrap are present |
| glibc | ☐ | defer until musl and dynamic-loader groundwork exist |
| m68k userland port | ☐ | needs storage, syscall ABI, loader, signals |
| Configure & build Bash | ☐ | disable readline/job control initially |
| Create BiscuitFS image | ✓ | use `tools/mkfs.biscuitfs` |
| QEMU boot test | ☐ | verify rootfs mount and shell before Bash |
| VT100 terminal driver | ☐ | needed for full readline-style interaction |

---

*"This is the best shell EVER!" — Bluey Heeler*

*Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
licensed by BBC Studios. BlueyOS is an unofficial fan/research project
with no affiliation to Ludo Studio or the BBC.*

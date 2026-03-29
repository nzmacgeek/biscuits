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
/* BlueyOS syscall numbers (int 0x80, i386 ABI - Linux-compatible subset) */
#define SYS_read       0
#define SYS_write      1
#define SYS_open       2
#define SYS_close      3
#define SYS_getpid    20
#define SYS_getuid    24
#define SYS_brk       45   /* heap growth */
#define SYS_exit      60
#define SYS_uname     63
#define SYS_gethostname 125
```

Syscalls not yet implemented by BlueyOS (e.g. `fork`, `execve`, `waitpid`)
need stub implementations that return `ENOSYS` until the kernel adds them.

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

For a shell to be useful, the kernel needs at least:

| Syscall | Number | Description |
|---------|--------|-------------|
| `read`  | 0      | Read from fd (fd=0 = keyboard) ✓ |
| `write` | 1      | Write to fd (fd=1 = VGA) ✓ |
| `open`  | 2      | Open a VFS path |
| `close` | 3      | Close fd |
| `fork`  | 57     | Clone process (requires full process support) |
| `execve`| 59     | Replace process image (ELF loader) |
| `waitpid`| 61   | Wait for child |
| `brk`   | 45     | Expand heap |
| `mmap`  | 90     | Memory-mapped I/O / anonymous pages |
| `ioctl` | 54     | Device control |
| `stat`  | 106    | File metadata |
| `chdir` | 80     | Change directory |
| `getcwd`| 183    | Get current directory |
| `pipe`  | 42     | Create pipe for IPC |
| `dup2`  | 63     | Duplicate file descriptor |

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

### 3.5 Package and install to BiscuitFS image

```bash
# Copy the bash binary into a BiscuitFS image
# First, create the image:
tools/mkfs.biscuitfs -L "BlueyRoot" -s 128 blueyos-root.img

# Mount the image (Linux host):
sudo mount -o loop,offset=0 blueyos-root.img /mnt/blueyos

# Create directory structure
sudo mkdir -p /mnt/blueyos/{bin,etc,lib,dev,proc,tmp,home/bandit}

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

The BlueyOS ELF loader (`kernel/elf.c`) already supports loading ELF32
executables. To exec `/bin/bash` at startup, modify `kernel_main()`:

```c
// After shell_init() in kernel/kernel.c:
// Attempt to exec /bin/bash from the mounted filesystem
int bash_elf = vfs_open("/bin/bash", VFS_O_RDONLY);
if (bash_elf >= 0) {
    vfs_close(bash_elf);
    process_t *bash_proc = elf_exec("/bin/bash", 1 /* uid = bandit */);
    if (bash_proc) {
        scheduler_add(bash_proc);
        kprintf("[SHL]  Launched /bin/bash (PID %d)\n", bash_proc->pid);
        // Fall through - the built-in shell is still the fallback
    }
} else {
    kprintf("[SHL]  /bin/bash not found - using built-in shell\n");
    shell_run();  /* built-in shell, never returns */
}
```

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
#define SYS_READ       0
#define SYS_WRITE      1
#define SYS_GETPID    20
#define SYS_GETUID    24
#define SYS_EXIT      60
#define SYS_UNAME     63
#define SYS_GETHOSTNAME 125
```

These are Linux-compatible i386 numbers. Syscalls not listed return `ENOSYS`.
Add new syscalls in `kernel/syscall.c` as needed.

---

## 8. Terminal emulation

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

## 9. Summary

| Step | Status | Notes |
|------|--------|-------|
| Build cross-compiler | ☐ | gcc + binutils targeting i386-blueyos-elf |
| Port musl libc | ☐ | `arch/i386` works; stub missing syscalls |
| Extend kernel syscalls | ☐ | At minimum: open/close/brk/fork/exec |
| Configure & build Bash | ☐ | Disable readline/termcap initially |
| Create BiscuitFS image | ✓ | Use `tools/mkfs.biscuitfs` |
| QEMU boot test | ☐ | Verify built-in shell works first |
| VT100 terminal driver | ☐ | Needed for full Bash readline support |

---

*"This is the best shell EVER!" — Bluey Heeler*

*Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
licensed by BBC Studios. BlueyOS is an unofficial fan/research project
with no affiliation to Ludo Studio or the BBC.*

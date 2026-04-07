**Musl Sysroot Packaging**

- **Purpose:** how to build musl for BlueyOS, produce a sysroot, and package runtime artifacts into the disk image.

- **Build musl for BlueyOS**:

  - Ensure you have an i686 multilib toolchain on the host (e.g. `i686-linux-gnu-gcc`/`gcc -m32`).
  - The supported repo path is:

    ```bash
    make build-musl-blueyos
    ```

  - That installs musl into three destinations:
    - `build/userspace/musl` for repo-local builds like `make musl-init`
    - `/opt/blueyos-sysroot` for the runtime sysroot packaged by `make disk`
    - `/opt/blueyos-cross/musl` for musl wrapper/tools alongside the cross toolchain

  - If `/opt/blueyos-cross` is root-owned, either adjust its permissions or override the cross destination when invoking `tools/build-musl.sh`.

- **Compile user programs against the musl sysroot**:

  - Use the installed include/lib layout under `/tmp/blueyos-musl`.
  - Example (static `init` used by the Makefile):

    ```bash
    gcc -m32 -static -no-pie -isystem /tmp/blueyos-musl/include \
        -L/tmp/blueyos-musl/lib -Wl,-Ttext,0x00400000 -o build/user/init-musl.elf tests/musl/init/init.c tests/musl/init/syscalls.c -lc
    ```

- **Packaging musl into the disk image**:

  - For dynamic programs you would copy `/tmp/blueyos-musl/lib/ld-musl-*.so` and relevant libraries into the image's `/lib` or `/usr/lib` and set up `/etc/ld.so.conf` as needed. The musl init used in this project is statically linked, so only the binary is added to the image.

- **Makefile integration**:

  - The repository's `Makefile` contains `build-musl-blueyos` (which runs `tools/build-musl.sh`) and `disk-musl` which uses the musl-built `init`. Run:

    ```bash
    make build-musl-blueyos
    make disk-musl
    ```

- **Next steps (building bash)**:

  - Bash depends on readline/ncurses; build those with the same cross/musl sysroot first (static builds recommended).
  - Prefer iterative builds: build readline → build ncurses → build bash, linking against `/tmp/blueyos-musl`.

This document is a short howto; if you want I can add step-by-step commands for building `ncurses`/`readline` and producing a static `bash` binary suitable for inclusion in the image.

---

**Building bash (step-by-step)**

- **Overview:** `bash` depends on `readline` (or `libedit`) and optional `ncurses` for terminal handling. The recommended sequence is:
  1. Build and install `readline` into the musl sysroot.
  2. Build and install `ncurses` (if you want full tty capabilities).
  3. Configure and build `bash` with static linking against musl and the installed libs.

- **Example commands (manual):**

  ```bash
  # variables
  MUSL_PREFIX=/tmp/blueyos-musl
  WORKDIR=/tmp/blueyos-bld
  mkdir -p $WORKDIR/src $WORKDIR/build
  cd $WORKDIR/src

  # readline
  wget https://ftp.gnu.org/gnu/readline/readline-8.2.tar.gz
  tar xzf readline-8.2.tar.gz
  cd readline-8.2
  CFLAGS='-m32 -static -isystem ${MUSL_PREFIX}/include' \
    LDFLAGS='-L${MUSL_PREFIX}/lib' \
    ./configure --host=i686-linux-gnu --prefix=${MUSL_PREFIX}
  make -j$(nproc)
  make install

  # ncurses (optional)
  cd $WORKDIR/src
  wget https://invisible-mirror.net/archives/ncurses/ncurses-6.3.tar.gz
  tar xzf ncurses-6.3.tar.gz
  cd ncurses-6.3
  CFLAGS='-m32 -static -isystem ${MUSL_PREFIX}/include' \
    LDFLAGS='-L${MUSL_PREFIX}/lib' \
    ./configure --host=i686-linux-gnu --prefix=${MUSL_PREFIX} --with-shared=no --with-normal --without-debug
  make -j$(nproc)
  make install

  # bash
  cd $WORKDIR/src
  wget https://ftp.gnu.org/gnu/bash/bash-5.1.tar.gz
  tar xzf bash-5.1.tar.gz
  cd bash-5.1
  CFLAGS='-m32 -static -isystem ${MUSL_PREFIX}/include' \
    LDFLAGS='-L${MUSL_PREFIX}/lib' \
    ./configure --host=i686-linux-gnu --prefix=/usr --without-bash-malloc --enable-static-link
  make -j$(nproc)
  # copy the resulting 'bash' into your image; consider stripping: strip --strip-all bash
  ```

- **Automation:** see `tools/build-bash.sh` for an opinionated helper to automate downloads/configure/make steps.

# Porting musl libc to BlueyOS

## Goal

Get BlueyOS to run statically linked musl user programs on i386 first.
Do not aim for `ld-musl`, shared libraries, or pthreads until the static path is solid.

This guide reflects the current repository state as of the `build/` output layout,
the current `int 0x80` syscall surface, and the existing disk-image workflow.

## Current state

BlueyOS is in a reasonable place to start a static musl bring-up:

- The kernel exposes a Linux-like i386 syscall ABI through `int 0x80`.
- The ELF loader can boot `/bin/init` from the BlueyFS root image.
- The root filesystem build already installs one userspace payload into `/bin/init`.
- The kernel and disk image build cleanly into `build/`.
- The current in-tree userspace tests boot successfully from BlueyFS under QEMU.

What is not done yet:

- there is no BlueyOS-specific musl target upstream or in-tree
- there is no dynamic loader support
- thread-local storage and thread syscalls are not complete
- the VFS and process model are still short of full POSIX behavior

## Recommended strategy

Use four stages.

1. Build musl for the existing Linux i386 syscall ABI assumptions.
2. Prove that a trivial static binary can execute as `/bin/init`.
3. Expand syscall and libc compatibility until a slightly larger program works.
4. Only then decide whether to carry a real `i386-blueyos` target or keep a Linux-compatible shim layer.

The important design choice is this: BlueyOS already uses many Linux syscall numbers on i386, so the fastest path is to make the kernel good enough for musl's static i386/Linux expectations before inventing a brand new libc target name.

## What already exists

The current syscall numbering is declared in [kernel/syscall.h](../kernel/syscall.h).
The most relevant pieces for a first musl bring-up are already present:

- File I/O: `read`, `write`, `open`, `close`, `lseek`, `stat`, `fstat`, `lstat`, `access`, `getdents`, `unlink`, `mkdir`, `rmdir`
- File descriptor operations: `dup`, `dup2`, `pipe`, `fcntl`, `ioctl`
- Process control: `fork`, `execve`, `waitpid`, `wait4`, `getpid`, `getppid`, `kill`, `exit`, `exit_group`
- Memory: `brk`, `mmap`, `mmap2`, `munmap`, `mprotect`
- Signals: `rt_sigaction`, `rt_sigprocmask`, `sigreturn`
- Time: `gettimeofday`, `clock_gettime`, `nanosleep`, `sched_yield`
- Misc: `uname`, `getcwd`, `chdir`, `getrandom`, `set_tid_address`

That is enough to justify trying a static musl hello-world or a tiny init replacement.

## Known gaps that still matter

For a real musl port, these remain the main blockers:

- `fstatat` and broader `*at` syscalls such as `openat`
- more complete `waitpid` semantics for blocking waits and restart behavior
- fuller `ioctl` coverage for terminal behavior
- missing path and metadata syscalls such as `readlink`, `rename`, `link`, `symlink`, `chmod`, `chown`, `truncate`, `ftruncate`
- TLS and thread setup such as `set_thread_area`, usable `clone`, and eventually `futex`
- alternate signal stacks and more Linux-accurate signal restart rules
- sockets and the rest of the network syscall surface

For static single-process programs, thread and socket gaps are acceptable initially.
For BusyBox-scale userspace, they stop being optional quickly.

## Build helper in this repo

The current helper is [tools/build-musl.sh](../tools/build-musl.sh).
It does one thing: download musl source, configure it, build it, and install it.

It does not:

- build a BlueyOS cross-compiler
- patch musl for a custom `i386-blueyos` target
- build a sysroot automatically from kernel headers

Treat it as a source-build convenience script, not a full porting pipeline.

## Suggested toolchain setup

The simplest first pass is to reuse a hosted i386 toolchain and point musl at it.

Example:

```bash
export CC=i686-linux-gnu-gcc
export TARGET=i386-linux-gnu
./tools/build-musl.sh --prefix=/tmp/blueyos-musl
```

Or, if you prefer a cross prefix:

```bash
export CROSS_COMPILE=i686-linux-gnu-
export TARGET=i386-linux-gnu
./tools/build-musl.sh --prefix=/tmp/blueyos-musl
```

Why `i386-linux-gnu` for now:

- musl already knows how to build for it
- BlueyOS deliberately uses many Linux-compatible syscall numbers
- it reduces the amount of musl-internal patching you need before the first test binary

Move to a true `i386-blueyos` target later if the compatibility layer becomes too awkward.

## First success criterion

Do not start with a shell.
Start with a trivial static binary that only needs the syscalls BlueyOS already supports.

Good first targets:

- a musl-linked `hello`
- a musl-linked test program that prints, opens a file, and exits
- a musl-linked replacement `/bin/init`

The current disk-image builder in [tools/mkbluey_disk.py](../tools/mkbluey_disk.py) installs exactly one init payload via `--init`, defaulting to `build/user/init.elf`.

That means the easiest smoke test is:

1. build a static musl binary that can serve as `/bin/init`
2. place it at a known path, for example `build/user/init-musl.elf`
3. create the disk image with a custom init path

Example:

```bash
make DEBUG=1 BUILD_DIR=build musl-init
python3 tools/mkbluey_disk.py \
	--image build/blueyos-disk.img \
	--kernel build/blueyos.elf \
	--init build/user/init-musl.elf \
	--mkfs-tool build/tools/mkfs_blueyfs \
	--mkswap-tool build/tools/mkswap_blueyfs
```

Or use the convenience target:

```bash
make DEBUG=1 BUILD_DIR=build disk-musl
```

Then boot it:

```bash
BUILD_DIR=build bash tools/qemu-run.sh -display none -monitor none
```

If the musl binary reaches userspace, prints to stdout, and exits cleanly, the port is real enough to continue.

## Kernel worklist for static musl

This is the practical checklist for the next round of kernel work.

### Must-have

- verify `brk`, anonymous `mmap`, and file-backed `mmap` behavior against musl's allocator expectations
- make blocking `waitpid` and `wait4` behavior reliable
- add `fstatat` or provide equivalent compatibility where musl expects it
- tighten `execve` argument, environment, and signal-reset semantics
- validate `fcntl` and `ioctl` return values against Linux behavior, not just success-shaped stubs

### Likely next

- `openat`
- `readlink`
- `rename`
- `truncate` and `ftruncate`
- `getrlimit` and related resource limit queries

### Defer until after static success

- `clone` for real threading
- `set_thread_area`
- `futex`
- dynamic loader support
- shared-library search paths such as `/lib` and `/usr/lib`

## Smoke-test workflow

Use this exact order.

1. Build host tools and kernel artifacts:

```bash
make DEBUG=1 BUILD_DIR=build disk -j"$(nproc)"
```

2. Build a static musl test binary outside the kernel tree or into `build/user/`.

3. Rebuild the BlueyOS disk image with that musl binary as `--init`.

4. Boot under QEMU and look for these outcomes:

- program reaches ring 3
- stdout works
- file open/read/write works
- process exits with code `0`

5. If it fails, inspect the serial log first, then compare the missing behavior against the syscall list in [kernel/syscall.h](../kernel/syscall.h) and the implementations in `kernel/syscall.c`.

## Dynamic musl comes later

Do not work on `ld-musl` yet unless all of the following are true:

- static musl programs run reliably
- path-based exec is stable
- `/lib` and `/usr/lib` conventions are settled
- the ELF loader can map interpreter segments and apply relocations correctly

Until then, `--disable-shared` is the correct choice.

## Practical next step

The next concrete milestone should be:

1. build musl with `tools/build-musl.sh` using an i386 Linux toolchain
2. compile one static `hello` or minimal musl init
3. boot it as `/bin/init`
4. record the first missing syscall or ABI mismatch and fix that specific kernel gap

That is the shortest path to turning the musl effort into measured progress instead of a large speculative port.

## References

- musl project: https://musl.libc.org
- musl source releases: https://musl.libc.org/releases/
- current syscall numbers: [kernel/syscall.h](../kernel/syscall.h)
- related shell-porting notes: [docs/porting-bash.md](./porting-bash.md)

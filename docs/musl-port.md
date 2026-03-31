# Porting musl libc to BlueyOS

Goal
----
Prepare BlueyOS to run programs built against musl libc. Start with static-linked musl binaries (simpler)
and iterate toward supporting dynamic `ld-musl` later.

Scope
-----
- Target architecture: i386 (BlueyOS kernel + QEMU test harness)
- Initial goal: run statically-linked musl programs (no dynamic loader) inside QEMU
- Medium-term: enable dynamic musl (`ld-musl`) and package `libc.so`

High-level plan
---------------
1. Audit and document the syscall surface musl expects (minimal list below).
2. Implement any missing/partial syscalls in the kernel and VFS (or extend existing ones).
3. Provide small device nodes and minimal /proc-like helpers if needed.
4. Add a deterministic cross-build script for musl and a CI job to build+smoke-test.
5. Create a minimal rootfs with an `init` built against musl and verify under QEMU.

Minimal syscall/features checklist (initial static musl)
-----------------------------------------------------
- File I/O: `open`, `read`, `write`, `close`, `lseek`, `stat`/`fstat`/`lstat`, `fstatat`
- File control: `fcntl` (dup/dup2), `ioctl` (minimal support), `pipe`
- Memory: `mmap`, `munmap`, `mprotect`, `brk`/`sbrk` (musl may use `brk`)
- Process: `exit`, `_exit`, `execve`, `wait4`, `fork`/`vfork` (for basic exec semantics)
- Time: `clock_gettime`, `gettimeofday`, `nanosleep`
- Networking (optional initially): `socket`, `bind`, `listen`, `accept`, `connect`, `sendto`, `recvfrom`
- Misc: `uname`, `getpid`, `getppid`, `getcwd`, `chdir`, `access`, `unlink`, `mkdir`, `rmdir`, `dup`, `set_tid_address` (threads), `rt_sigaction`, `rt_sigprocmask`, `getrandom`/`syscall` fallback if needed

Notes
-----
- Start with static builds to avoid implementing dynamic loader/linker semantics.
- When adding `mmap` support, ensure file-backed mappings and anonymous mappings are present.
- `fstat` and `stat` are already partially covered by the new VFS `stat` path — expand as needed.

Build & test
------------
1. Use `tools/build-musl.sh` (in repo) to build a cross toolchain and musl for `i386`.
2. Build a static `hello` and copy to a test rootfs (e.g., `qemu-root`).
3. Boot BlueyOS in QEMU with that rootfs and confirm the program runs.

References
----------
- musl documentation: https://musl.libc.org
- musl build options: `./configure --prefix=/some/dir --target=i386-linux-gnu`

Next steps
----------
- Run a syscall audit: capture the syscall list musl actually uses for simple programs.
- Implement kernel-side stubs for any missing syscalls flagged by the audit.

# BlueyOS Ecosystem Defect Analysis

> Generated from boot log analysis — kernel.log dated 2026-04-10.
> Update this file whenever a defect is fixed or newly discovered.

---

## Legend

| Severity | Meaning |
|----------|---------|
| 🔴 CRITICAL | System crash / data corruption / boot failure |
| 🟠 HIGH | Feature broken / syscall missing / misleading errors |
| 🟡 MEDIUM | Degraded experience / non-fatal errors at boot |
| 🟢 LOW | Code quality / verbosity / minor UX |

---

## Kernel (nzmacgeek/biscuits)

### K-1 🔴 Heap initialised AFTER first module load
**Status:** FIXED (this PR)

`kheap_init()` was called at step 6 but `module_load("keyboard")` was called
at step 5.  `module_load()` calls `kheap_alloc()` internally; if the heap is
not yet set up this either crashes or silently corrupts memory.

**Fix:** moved `kheap_init()` above `module_load("keyboard")` in
`kernel/kernel.c`.

---

### K-2 🔴 vfork + execve failure → EIP=0x33 page fault crash
**Status:** OPEN — needs investigation

From the log:
```
[PGF]  Page Fault! Faulting address: 0x33
[PGF]  Error code: 0x5 (protection violation, read, user)
[PGF]  EIP: 0x33
[PGF]  User fault outside stack growth region pid=19
```

PID 19 was a `vforked` child of claw (pid 2) attempting to exec
`dimsim-postinst-walkies` (`/bin/bash`).  After `execve` was logged as
"preserving shared vfork address space", execution jumped to address `0x33`
(= the x86 user-mode CS segment selector in typical GDT layouts).

**Root cause hypothesis:** a return-address slot on the process's user stack
is being read as `0x33` — either:
- A `SIGCHLD` signal is delivered to pid 2 (claw) at the same instant the
  kernel is setting up the new process's initial stack frame for pid 19, and
  the signal delivery path corrupts the saved user `EIP`.
- The vfork `execve` error path (when execve fails to locate a binary) does
  not cleanly unwind and leaves a stale register value.

**Next steps:**
1. Audit `kernel/signal.c` — ensure signal delivery only modifies the target
   process's saved registers, never `proc_current` when the target differs.
2. Check that `execve` error paths restore the vfork-shared address space
   pointer before returning to the child, so the child's `_exit(127)` call
   succeeds.
3. Add a guard in the page-fault handler: if EIP < 0x1000 and
   `syslog_get_verbose() >= 1`, dump the full register state and process
   credentials before killing the process.

---

### K-3 🟠 `pipe2` syscall (NR 331) not implemented
**Status:** FIXED (this PR)

Bash uses `pipe2(fds, O_CLOEXEC)` for shell pipes when it's available.
The kernel was logging:
```
[SYS] Unknown syscall 331 from pid=9 name=bash
```

**Fix:** added `sys_pipe2()` in `kernel/syscall.c` and `SYS_PIPE2=331` in
`kernel/syscall.h`.  Flags (`O_CLOEXEC`, `O_NONBLOCK`) are accepted but not
yet enforced — that is a follow-up task.

---

### K-4 🟠 `faccessat2` syscall (NR 439) not implemented
**Status:** FIXED (this PR)

Bash uses `faccessat2` for command existence checks in PATH.  The kernel was
logging:
```
[SYS] Unknown syscall 439 from pid=9 name=bash
```

**Fix:** added `sys_faccessat2()` in `kernel/syscall.c` and
`SYS_FACCESSAT2=439` in `kernel/syscall.h`.  Current implementation does a
VFS stat existence check; full DAC permission checking is a future task.

---

### K-5 🟠 `clone(exit_signal=0)` rejected with ENOSYS
**Status:** FIXED (this PR)

The kernel was rejecting `clone()` calls where the exit signal field is 0
(meaning "no signal on child exit"), logging:
```
[SYS] clone unsupported exit signal=0 flags=0x0
```

Linux accepts `exit_signal=0`.  Musl and bash can issue such calls when
spawning sub-processes that should not deliver SIGCHLD.

**Fix:** `sys_clone()` in `kernel/syscall.c` now treats `exit_signal=0` the
same as `SIGCHLD` for the purposes of routing to `sys_fork()`.

---

### K-6 🟡 `getcwd` ERANGE / EFAULT for some calling patterns
**Status:** PARTIALLY FIXED (this PR)

The original `sys_getcwd` returned `EFAULT` when `size==0`.  POSIX/Linux
returns `EINVAL` for `size==0`.  Also clarified that `buf==NULL` is always
`EFAULT` (musl should never pass NULL, but the error mapping was wrong).

**Remaining issue:** Bash prints `getcwd: cannot access parent directories:
No such file or directory` on every invocation.  This is likely musl's
`getcwd()` falling back to manual directory traversal when the kernel syscall
fails, and the VFS not returning `.` / `..` entries for the root directory.

**Next steps:**
- Ensure `vfs_readdir("/")` (and all directories) returns `.` and `..` entries.
- Verify the kernel returns the correct errno and buffer length for `getcwd`
  on the root directory.

---

### K-7 🟡 `[ROOTDBG]` / `[VFS DBG]` / `[ELF DBG]` always printed at boot
**Status:** FIXED (this PR)

Diagnostic dump blocks (VFS mount table, root dir listing, ELF segment maps,
BiscuitFS read traces) were unconditional, flooding the VGA console on every
boot even in production.

**Fix:** `[ROOTDBG]` block in `kernel/kernel.c` gated behind
`syslog_get_verbose() >= VERBOSE_INFO`.  The `[VFS DBG]`, `[BISCUITFS DBG]`,
and `[ELF DBG]` log lines visible in the boot log still need to be gated;
see **K-8** below.

---

### K-8 🟡 `[VFS DBG]`, `[BISCUITFS DBG]`, `[ELF DBG]` not gated on verbose
**Status:** OPEN

From the log, every `open()` call produces two kernel debug lines:
```
[VFS DBG] open request path=/etc/claw/claw.conf -> ...
[BISCUITFS DBG] open enter path_ptr=0x... flags=0x0 ...
```
Every ELF load produces multiple `[ELF DBG]` stack-mapping lines.

These should be gated on `verbose >= VERBOSE_DEBUG` (2) in:
- `fs/vfs.c` — `[VFS DBG]` lines
- `fs/biscuitfs.c` — `[BISCUITFS DBG]` lines
- `kernel/elf_loader.c` — `[ELF DBG]` lines

**Next steps:** wrap each `kprintf("[VFS DBG]...")` / `kprintf("[BISCUITFS DBG]...")`
/ `kprintf("[ELF DBG]...")` with `if (syslog_get_verbose() >= VERBOSE_DEBUG)`.

---

### K-9 🟡 No `verbose=` boot argument support
**Status:** FIXED (this PR)

Added `verbose` field to `boot_args_t`, parsing in `boot_args_init()`, and
`syslog_set_verbose()` / `syslog_get_verbose()` API.  VGA console echoing
in `syslog_write()` now respects the verbosity level.

**GRUB cmdline usage:**
```
linux /kernel root=/dev/hda verbose=1   # show INFO messages
linux /kernel root=/dev/hda verbose=2   # show DEBUG messages
```

---

### K-10 🟠 Signal delivery may corrupt wrong process state
**Status:** OPEN — see K-2

`[SIG] Sent CHLD to pid=2` appears immediately before the EIP=0x33 crash in
pid=19.  Investigate `kernel/signal.c` to ensure the signal delivery path
saves/restores `proc_current` correctly when the signal target is not the
currently-scheduled process.

---

## claw (nzmacgeek/claw)

### C-1 🟡 No `chdir("/")` at startup
**Status:** OPEN

Claw does not call `chdir("/")` before starting services.  When services
inherit claw's working directory and the kernel's `getcwd` is broken (K-6),
all service scripts fail to resolve their working directory.

**Fix:** add `chdir("/")` near the top of `claw`'s `main()`, before entering
the boot sequence.

---

### C-2 🟡 `load_boot_options` silently fails if `/proc/cmdline` not mounted
**Status:** OPEN

`/proc` may not be mounted when claw starts (the kernel mounts it but there
is a timing window).  The silent failure means `claw.single=1` and
`claw.target=` boot args are ignored.

**Fix:** retry the open with a short delay, or read the cmdline from an
environment variable set by the kernel (e.g., `BOOTCMDLINE`).

---

### C-3 🟡 `vfork()` + parent modification race
**Status:** OPEN

Claw uses `vfork()` for all service spawns.  After `vfork()` returns in the
parent, claw continues to open files and modify memory while the child has
not yet called `execve()`.  If the kernel's `vfork()` does not correctly
suspend the parent until the child exec/exits, the child's argv/env pointers
may be corrupted.

Confirmed in the log: claw's parent opens `/etc/passwd` (for uid resolution)
while a vfork child is still executing.

**Fix:** resolve uid/gid (as claw already does) **before** vfork — do not
open any files between `vfork()` and `execve()` in the child.  Review all
`vfork` call sites in `supervisor.c`.

---

## dimsim (nzmacgeek/dimsim)

### D-1 🔴 `run-postinst` file for `matey` contains `/etc/passwd` content
**Status:** OPEN — must fix in dimsim

From the log:
```
/var/lib/dimsim/firstboot/matey/run-postinst: line 1:
  root:x:0:0:root:/root:/bin/sh: No such file or directory
```

The file `/var/lib/dimsim/firstboot/matey/run-postinst` was written with
`/etc/passwd` content on its first line instead of a shell script.

**Likely cause:** the `dimsim` package installation writes the `run-postinst`
file using an incorrect template or a buffer that was still holding `/etc/passwd`
data.

**Next steps:**
1. Inspect `dimsim/template/` and `dimsim/internal/` for the firstboot file
   generation logic.
2. Ensure all `run-preinst` / `run-postinst` templates start with `#!/bin/sh`
   and contain the correct package-specific commands.
3. Add a validation step in `dimsim` that checks the first bytes of generated
   scripts for a valid shebang before writing to disk.

---

### D-2 🟠 `getent` not available on BlueyOS — preinst scripts fail
**Status:** OPEN — fix in dimsim

From the log:
```
/var/lib/dimsim/firstboot/yap/preinst.sh: line 7: getent: command not found
```

The `yap` preinst script calls `getent passwd` / `getent group` to check
for existing users/groups.  `getent` is not part of the BlueyOS base system.

**Fix:** replace `getent` calls with direct `grep` against `/etc/passwd` and
`/etc/group`:
```sh
# Instead of: getent passwd yap
grep -q '^yap:' /etc/passwd
# Instead of: getent group syslog
grep -q '^syslog:' /etc/group
```
Apply this fix to all dimsim firstboot scripts.

---

### D-3 🟡 `shell-init: getcwd` failure in all bash service scripts
**Status:** OPEN — depends on K-6

Every bash process spawned by claw fails with:
```
shell-init: error retrieving current directory: getcwd: cannot access parent directories: No such file or directory
```
This is non-fatal for script execution but causes bash to operate without a
known working directory, which breaks any script using relative paths or `pwd`.

Depends on fixing K-6 (VFS `.`/`..` entries) and C-1 (claw chdir).

---

### D-4 🟡 Interleaved log output from concurrent services
**Status:** OPEN

The boot log shows log lines from multiple concurrent bash/claw processes
interleaved in a single stream.  This makes debugging very difficult.

**Fix:** the kernel's VGA output (`kprintf`) is not serialised against
user-mode writes to `/var/log/kernel.log`.  For the immediate term, add a
kernel spinlock around the VGA write path.  Long term, route all
service stdout/stderr through `yap` (the syslog daemon) with a sequence
number prefix.

---

## yap (nzmacgeek/yap)

### Y-1 🟡 yap triggers a stack-growth page fault on startup
**Status:** INFORMATIONAL (handled correctly by kernel)

From the log:
```
[PGF]  Page Fault! Faulting address: 0x704fbe8c
[PGF]  Error code: 0x4 (not present, read, user)
[PGF]  EIP: 0x401641
[PGF]  Growing user stack: pid=4 va=0x704fb000 -> phys=0x00613000
```

This is a **legitimate** on-demand stack growth page fault that the kernel
handled correctly (EIP=0x401641 is valid yap code; error code 0x4 = not
present + read + user).  No action required — but it confirms that yap's
startup stack usage exceeds the initial 4-page allocation.

Consider increasing the initial stack allocation from 4 to 8 pages in the
ELF loader for large binaries to reduce page fault overhead.

---

### Y-2 🟡 yap loaded at non-standard ELF addresses
**Status:** OPEN — investigation needed

The yap ELF has an unusual segment layout:
```
[ELF] Stream-loaded segment 0: vaddr=0x3ff000 size=340
[ELF] Stream-loaded segment 4: vaddr=0x8048154 size=68
[ELF] Stream-loaded segment 5: vaddr=0x8049000 size=32
```
Segments 4 and 5 are at 0x8048xxx while segments 0–3 are at 0x3ff000–0x411xxx.
This is an unusual layout that could indicate a linker script issue in
`musl-blueyos` or `blueyos-bash` builds.  Verify the yap build uses the
correct linker script.

---

## matey (nzmacgeek/matey)

### M-1 🟡 `LOGIN_PROGRAM` path may not exist at first boot
**Status:** OPEN

`matey` hard-codes `LOGIN_PROGRAM="/sbin/login"`.  If `login` is not yet
installed (first boot, before dimsim-postinst-login-tools completes), matey
will fall back to `/bin/sh` — which may not support password authentication.

**Fix:** matey should check for the existence of `/sbin/login` before
attempting exec, and display a clear message if it is missing.

---

### M-2 🟡 matey not yet started in this boot log
**Status:** INFORMATIONAL

The boot log ends before reaching the `matey@tty1` / `matey@tty2` / `matey@tty3`
service starts.  The boot was still executing firstboot scripts when the log
was captured.  Once firstboot completes, matey should be started.

---

## musl-blueyos (nzmacgeek/musl-blueyos)

### ML-1 🟠 getcwd fallback traverses `.` / `..` and fails on BlueyOS VFS
**Status:** OPEN — depends on K-6

musl's `getcwd()` calls `SYS_getcwd` and, if that fails, falls back to
manually traversing the directory tree via `openat(AT_FDCWD, ".", ...)` +
`fstat` + `getdents` + `openat(fd, "..", ...)`.

The BlueyOS VFS does not yet return `.` and `..` entries from `vfs_readdir`,
so the fallback always fails with ENOENT.

**Fix in kernel:** make `vfs_readdir` include `.` (inode = current dir) and
`..` (inode = parent dir) as the first two entries for all directories.

---

### ML-2 🟡 Missing syscall wrappers for pipe2 / faccessat2
**Status:** FIXED upstream (kernel now implements NR 331 and NR 439)

The kernel now handles these calls; no musl change needed unless musl-blueyos
has explicit `ENOSYS` stubs that bypass the kernel.  Verify with a rebuild.

---

## blueyos-bash (nzmacgeek/blueyos-bash)

### BB-1 🟠 Bash uses syscalls not implemented in the kernel
**Status:** FIXED (kernel side — this PR)

Bash 5 calls `pipe2(NR=331)` and `faccessat2(NR=439)`.  Both are now
implemented in the kernel.  Verify bash behaves correctly after kernel update.

---

### BB-2 🟡 Bash receives "Unknown signal" for signal 18 (SIGCONT)
**Status:** OPEN

From the log:
```
Unknown signal  ...  18 Unknown signal
```
Bash handles `SIGCONT` for job control.  If the kernel's signal numbering
or delivery mechanism doesn't match what bash expects (Linux signal 18 =
SIGCONT on i386), bash prints "Unknown signal".

**Next steps:** verify that `kernel/signal.c` uses the standard Linux i386
signal numbering (SIGCONT = 18) and that the signal name table in blueyos-bash
includes an entry for signal 18.

---

## Summary — priority fix order

| # | Component | Defect | Severity |
|---|-----------|--------|----------|
| 1 | kernel | K-2: EIP=0x33 crash after vfork+execve | 🔴 CRITICAL |
| 2 | dimsim | D-1: run-postinst written with passwd content | 🔴 CRITICAL |
| 3 | kernel | K-8: VFS/ELF debug lines not gated on verbose | 🟠 HIGH |
| 4 | kernel | K-6: getcwd fails → bash can't find cwd | 🟡 MEDIUM |
| 5 | dimsim | D-2: getent not available | 🟠 HIGH |
| 6 | claw | C-1: no chdir("/") at startup | 🟡 MEDIUM |
| 7 | kernel | K-10: signal delivery may corrupt wrong process | 🟠 HIGH |
| 8 | musl | ML-1: getcwd fallback fails without . / .. entries | 🟡 MEDIUM |
| 9 | bash | BB-2: SIGCONT (18) logged as "Unknown signal" | 🟡 MEDIUM |
| 10 | matey | M-1: login not available at first boot | 🟡 MEDIUM |

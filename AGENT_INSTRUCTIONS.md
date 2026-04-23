# Agent Instructions for BlueyOS / biscuits

> These instructions apply to **every** Copilot coding agent working in this
> repository.  Read this file before starting any task.

---

## 1. Ecosystem overview — look at ALL nzmacgeek repos

BlueyOS is a multi-repo operating system project.  Before making any change
you must check whether the change touches an interface that spans multiple
repos and consult the relevant repo:

| Repo | Role | Key files |
|------|------|-----------|
| **nzmacgeek/biscuits** | i386 kernel, VFS, syscalls, drivers | `kernel/`, `drivers/`, `fs/`, `net/` |
| **nzmacgeek/claw** | PID 1 init daemon (service manager) | `src/claw/main.c`, `src/core/service/supervisor.c` |
| **nzmacgeek/matey** | getty / login prompt | `matey.c` |
| **nzmacgeek/walkies** | Network configuration tool (netctl) | `src/walkies.c`, or see WALKIES_PROMPT.md in biscuits |
| **nzmacgeek/scout** | DHCP and DNS client daemon | `src/`, key files TBD |
| **nzmacgeek/yap** | Syslog daemon / log rotation | `yap.c` (or equivalent) |
| **nzmacgeek/dimsim** | Package manager / firstboot scripts | `cmd/`, `internal/`, `template/` |
| **nzmacgeek/musl-blueyos** | musl libc patched for BlueyOS syscalls | `arch/i386/`, `src/` |
| **nzmacgeek/blueyos-bash** | Bash 5 patched for BlueyOS | `configure.ac`, patches |

**When working on a kernel syscall, always check musl-blueyos and blueyos-bash
to understand the caller expectations.**

**When working on DHCP/DNS client, coordinate with scout**. Scout is a userspace daemon that uses the netctl control plane (AF_BLUEY_NETCTL sockets) to implement DHCP and DNS resolution for BlueyOS.

---

## 2. Verbosity control — standard practice across all BlueyOS software

### Kernel (biscuits)

The kernel reads a `verbose=N` boot argument (default 0) and exposes it via:

```c
#include "kernel/syslog.h"

syslog_get_verbose()          // query current level
syslog_set_verbose(int level) // set from boot_args.verbose
```

Level semantics (defined as `VERBOSE_QUIET`, `VERBOSE_INFO`, `VERBOSE_DEBUG`):

| `verbose=` | Console output | Use case |
|-----------|----------------|----------|
| 0 (default) | EMERG / ALERT / CRIT / ERR only | Production boot |
| 1 | + WARNING / NOTICE / INFO | Debugging init failures |
| 2 | All messages including DEBUG | Deep kernel/driver debugging |

**Rules:**
- All **kernel diagnostic blocks** (`[ROOTDBG]`, `[VFS DBG]`, `[BISCUITFS DBG]`,
  `[ELF DBG]`, `[SYS]` detail dumps) must be gated behind
  `if (syslog_get_verbose() >= VERBOSE_INFO)` or `VERBOSE_DEBUG` as appropriate.
- Use `syslog_write(LOG_DEBUG, ...)` / `syslog_debug()` macros for debug detail.
  These are stored in the ring buffer regardless; gating only affects the VGA
  console echo.
- `kprintf()` for plain boot status messages is fine unconditionally.

### Userspace daemons (claw, yap, walkies, matey, …)

All userspace daemons must honour a `--verbose` / `-v` flag **and** the
`VERBOSE` environment variable (set by claw from the kernel `verbose=` arg):

```
VERBOSE=0  quiet (default) — errors + lifecycle events only
VERBOSE=1  info — detailed operational messages
VERBOSE=2  debug — all trace messages
```

**Retrofit rule:** when modifying any logging call in a userspace daemon,
check that the log level used is appropriate for the message content, and
add a verbosity guard if the message is debug-only.

---

## 2b. Kernel debug flags (`kdbg`) — per-subsystem runtime control

For subsystem-level tracing (paging, process lifecycle, syscall trace, signal
delivery, filesystem ops), the kernel uses a **bitmask flag system** via
`kernel/kdbg.h` / `kernel/kdbg.c`.  This is separate from the syslog
verbosity level: `kdbg` is about *what* is traced, `verbose=` is about *how
much* the syslog layer echoes.

### Defined flags

| Constant | Bit | Subsystem covered |
|---|---|---|
| `KDBG_PAGING`  | 0 | `paging.c` — page table clone/destroy/map |
| `KDBG_PROCESS` | 1 | `process.c` — fork/exec/exit lifecycle |
| `KDBG_SYSCALL` | 2 | `syscall.c` — per-call entry/exit traces |
| `KDBG_SIGNAL`  | 3 | `signal.c` — signal delivery and trampolines |
| `KDBG_FS`      | 4 | `vfs.c`, `devfs.c` — open/read/write/ioctl |
| `KDBG_SCHED`   | 5 | `scheduler.c` — task switch decisions |
| `KDBG_ALL`     | 0xFFFFFFFF | Enable everything |

### Usage in kernel code

```c
#include "kernel/kdbg.h"

// Cheap: no output unless the flag is set at runtime
kdbg(KDBG_PAGING,  "[PGE] cloning pd=0x%08x\n", pd);
kdbg(KDBG_PROCESS, "[PRC] fork pid=%u → child pid=%u\n", p->pid, child->pid);
kdbg(KDBG_SYSCALL, "[SYS] execve path=%s\n", path);
```

**Rule: every new debug `kprintf` in kernel/drivers/fs code MUST use `kdbg()`
with the appropriate flag.  Unconditional `kprintf` is reserved for boot-time
init messages, error conditions, and fault handlers (panic, OOPS, page fault).**

### Enabling flags at boot

Pass `kdbg=0xN` on the kernel command line (GRUB or QEMU `-append`):

```
# Enable paging + process debug:
kdbg=0x3

# Enable everything:
kdbg=0xFFFFFFFF
```

Parsed in `kernel/bootargs.c` at startup; applies before the first process runs.

### Enabling flags at runtime (live system)

Write a hex value to `/dev/kdbg` from the shell:

```sh
# Enable syscall tracing at runtime:
echo 0x4 > /dev/kdbg

# Enable all flags:
echo 0xFFFFFFFF > /dev/kdbg

# Disable everything:
echo 0 > /dev/kdbg

# Read current flags:
cat /dev/kdbg
```

`/dev/kdbg` is a character device backed by `DEVNODE_KDBG` in `fs/devfs.c`.
Writes set `kdbg_flags`; reads return the current value as a hex string.

### What stays unconditional (never gated by kdbg)

- `kprintf` in `kernel/isr.c`, `kernel/gdt.c`, `kernel/idt.c` (hardware setup)
- Boot banner lines (`[BISCUIT]`, `[PMM]`, `[HEAP]`, `[VFS] Mounted at …`)
- `[OOPS]` / panic / page-fault dump lines
- Driver init one-liners (`[KBD] Keyboard initialised`, `[ATA] …`)
- Any `kprintf` guarded by `syslog_get_verbose() >= VERBOSE_INFO`

---

## 3. Coding conventions

- Kernel is C11 (`-std=gnu11`), freestanding, no libc.
- Userspace daemons are C11 (`-std=gnu11`) with musl libc, statically linked.
- 4-space indentation.  No tabs except in Makefiles.
- All new kernel syscalls must be added to **both** `kernel/syscall.h`
  (the `#define SYS_*` number) **and** the `syscall_dispatch` switch in
  `kernel/syscall.c`.
- Always add a corresponding entry in `docs/DEFECT_ANALYSIS.md` when you fix
  a defect that was catalogued there.

---

## 4. Build and test

```bash
# Check if cross-toolchain is available:
make check-tools

# Build the kernel ELF:
make

# Build a bootable disk image (requires musl toolchain + dimsim packages):
make disk-musl

# Run in QEMU:
make run
```

No automated unit-test runner exists yet.  Validate kernel changes by booting
in QEMU and inspecting the console / `/var/log/kernel.log`.

---

## 5. Repo memory hygiene

When you discover a new fact about the codebase that would help future agents,
call `store_memory` with:
- `subject`: short topic (e.g., "syscall implementation")
- `fact`: one-sentence statement
- `citations`: file:line where the fact is verified
- `reason`: why it matters for future tasks

Refresh existing memories you find to be accurate by storing them again (only
recent memories are kept).

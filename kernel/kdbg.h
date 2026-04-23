#pragma once
// BlueyOS Kernel Debug Flags — "Bandit's Toolbelt Tracer"
// Runtime-toggleable per-subsystem debug output.
//
// Usage in kernel code:
//   #include "kernel/kdbg.h"
//   kdbg(KDBG_PAGING, "[PGE] cloning pd=0x%08x\n", pd);
//
// Enable at boot via cmdline:  kdbg=0x3   (paging + process)
// Enable at runtime:           echo 0x4 > /dev/kdbg
// Read current flags:          cat /dev/kdbg

#include "../include/types.h"

// ---------------------------------------------------------------------------
// Per-subsystem flag bits
// ---------------------------------------------------------------------------
#define KDBG_PAGING   (1u << 0)   // paging.c — page table clone/destroy/map
#define KDBG_PROCESS  (1u << 1)   // process.c — fork/exec/exit lifecycle
#define KDBG_SYSCALL  (1u << 2)   // syscall.c — per-call entry/exit traces
#define KDBG_SIGNAL   (1u << 3)   // signal.c — delivery and trampolines
#define KDBG_FS       (1u << 4)   // vfs.c/devfs.c — open/read/write/ioctl
#define KDBG_SCHED    (1u << 5)   // scheduler.c — task switch decisions
#define KDBG_ALL      (0xFFFFFFFFu)

// ---------------------------------------------------------------------------
// Global flags variable — defined in kdbg.c
// ---------------------------------------------------------------------------
extern uint32_t kdbg_flags;

// ---------------------------------------------------------------------------
// Debug output macro — zero cost when flag is not set
// ---------------------------------------------------------------------------
#define kdbg(flag, fmt, ...) \
    do { if (kdbg_flags & (flag)) kprintf(fmt, ##__VA_ARGS__); } while (0)

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
void     kdbg_init(uint32_t initial_flags);
uint32_t kdbg_get(void);
void     kdbg_set(uint32_t flags);
void     kdbg_enable(uint32_t flags);
void     kdbg_disable(uint32_t flags);

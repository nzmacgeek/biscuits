// BlueyOS Kernel Debug Flags — "Bandit's Toolbelt Tracer"
// Runtime-toggleable per-subsystem debug tracing.
//
// Write hex value to /dev/kdbg to set flags at runtime.
// Provide kdbg=0xN on kernel cmdline for boot-time activation.
#include "../include/types.h"
#include "../lib/stdio.h"
#include "kdbg.h"

uint32_t kdbg_flags = 0;

void kdbg_init(uint32_t initial_flags) {
    kdbg_flags = initial_flags;
}

uint32_t kdbg_get(void) {
    return kdbg_flags;
}

void kdbg_set(uint32_t flags) {
    kdbg_flags = flags;
}

void kdbg_enable(uint32_t flags) {
    kdbg_flags |= flags;
}

void kdbg_disable(uint32_t flags) {
    kdbg_flags &= ~flags;
}

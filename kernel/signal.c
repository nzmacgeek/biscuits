#include "signal.h"

#include "../lib/stdio.h"
#include "../lib/string.h"
#include "paging.h"
#include "process.h"
#include "syscall.h"

#define SIGNAL_TRAMPOLINE_ADDR 0x7FFF0000u
#define SIGNAL_FRAME_MAGIC     0x53494746u

typedef struct {
    uint32_t    magic;
    uint32_t    old_blocked;
    registers_t saved_regs;
} signal_frame_t;

typedef struct {
    int         sig;
    const char *name;
} signal_name_map_t;

static const signal_name_map_t signal_names[] = {
    { SIGHUP,  "HUP"  },
    { SIGINT,  "INT"  },
    { SIGQUIT, "QUIT" },
    { SIGILL,  "ILL"  },
    { SIGABRT, "ABRT" },
    { SIGKILL, "KILL" },
    { SIGALRM, "ALRM" },
    { SIGTERM, "TERM" },
    { SIGCHLD, "CHLD" },
    { SIGCONT, "CONT" },
    { SIGSTOP, "STOP" },
};

static int signal_trampoline_ready = 0;
static const uint8_t signal_trampoline_code[] = {
    0x8B, 0x5C, 0x24, 0x04,
    0xB8, SYS_SIGRETURN, 0x00, 0x00, 0x00,
    0xCD, 0x80,
    0xF4,
    0xEB, 0xFE
};

static uint32_t signal_bit(int sig) {
    return 1u << (sig - 1);
}

static int signal_is_fatal_by_default(int sig) {
    return sig == SIGHUP || sig == SIGINT || sig == SIGQUIT ||
           sig == SIGILL || sig == SIGABRT || sig == SIGKILL || sig == SIGTERM;
}

static int signal_is_ignored_by_default(int sig) {
    return sig == SIGCHLD || sig == SIGCONT;
}

static void signal_ensure_trampoline(void) {
    uint32_t phys;

    if (signal_trampoline_ready) return;

    phys = pmm_alloc_frame();
    if (!phys) {
        kprintf("[SIG]  Failed to allocate trampoline page\n");
        return;
    }

    paging_map(SIGNAL_TRAMPOLINE_ADDR, phys, PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE);
    memset((void*)SIGNAL_TRAMPOLINE_ADDR, 0, PAGE_SIZE);
    memcpy((void*)SIGNAL_TRAMPOLINE_ADDR, signal_trampoline_code, sizeof(signal_trampoline_code));
    signal_trampoline_ready = 1;
}

static int signal_next_pending(process_t *process) {
    uint32_t pending;

    if (!process) return 0;
    pending = process->pending_signals & ~process->blocked_signals;
    if (!pending) return 0;

    for (int sig = 1; sig <= 31; sig++) {
        if (pending & signal_bit(sig)) return sig;
    }
    return 0;
}

static int signal_apply_default_action(process_t *process, int sig) {
    if (!process) return -1;

    if (signal_is_ignored_by_default(sig)) return 0;

    if (sig == SIGSTOP) {
        process->state = PROC_STOPPED;
        return 1;
    }

    if (sig == SIGCONT) {
        if (process->state == PROC_STOPPED) process->state = PROC_READY;
        return 0;
    }

    if (signal_is_fatal_by_default(sig)) {
        process_mark_exited(process, 128 + sig);
        return 1;
    }

    return 0;
}

const char *signal_name(int sig) {
    for (size_t i = 0; i < ARRAY_SIZE(signal_names); i++) {
        if (signal_names[i].sig == sig) return signal_names[i].name;
    }
    return "UNKNOWN";
}

bool signal_is_valid(int sig) {
    return sig > 0 && sig <= 31;
}

void signal_init(void) {
    signal_ensure_trampoline();
}

static void signal_mark_pending(process_t *process, int sig) {
    if (!process || !signal_is_valid(sig)) return;
    process->pending_signals |= signal_bit(sig);
}

int signal_send_pid(uint32_t pid, int sig) {
    process_t *process;

    if (!signal_is_valid(sig)) return -1;
    process = process_get_by_pid(pid);
    if (!process) return -1;

    if (sig == SIGKILL) {
        process_mark_exited(process, 128 + sig);
    } else if (sig == SIGSTOP) {
        process->state = PROC_STOPPED;
    } else if (sig == SIGCONT) {
        process->pending_signals &= ~signal_bit(sig);
        if (process->state == PROC_STOPPED || process->state == PROC_WAITING) {
            process->state = PROC_READY;
        }
    } else {
        signal_mark_pending(process, sig);
    }

    kprintf("[SIG]  Sent %s to pid=%u\n", signal_name(sig), pid);
    return 0;
}

int signal_dispatch_pending(process_t *process, registers_t *regs) {
    int sig;
    process_sigaction_t *action;
    uint32_t stack_ptr;
    signal_frame_t *frame;

    if (!process || !regs) return 0;
    if ((regs->cs & 0x3u) != 0x3u) return 0;
    if (process->flags & PROC_FLAG_SIGNAL_ACTIVE) return 0;

    sig = signal_next_pending(process);
    if (!sig) return 0;

    process->pending_signals &= ~signal_bit(sig);
    action = &process->signal_actions[sig - 1];

    if (sig == SIGKILL || sig == SIGSTOP || action->handler == SIG_DFL) {
        return signal_apply_default_action(process, sig);
    }
    if (action->handler == SIG_IGN) {
        return 0;
    }

    signal_ensure_trampoline();
    if (!signal_trampoline_ready) return signal_apply_default_action(process, sig);

    stack_ptr = regs->useresp & ~0x3u;
    stack_ptr -= sizeof(signal_frame_t);
    frame = (signal_frame_t*)(uintptr_t)stack_ptr;
    frame->magic = SIGNAL_FRAME_MAGIC;
    frame->old_blocked = process->blocked_signals;
    frame->saved_regs = *regs;

    stack_ptr -= sizeof(uint32_t);
    *(uint32_t*)(uintptr_t)stack_ptr = (uint32_t)(uintptr_t)frame;
    stack_ptr -= sizeof(uint32_t);
    *(uint32_t*)(uintptr_t)stack_ptr = (uint32_t)sig;
    stack_ptr -= sizeof(uint32_t);
    *(uint32_t*)(uintptr_t)stack_ptr = SIGNAL_TRAMPOLINE_ADDR;

    process->blocked_signals |= action->mask | signal_bit(sig);
    process->flags |= PROC_FLAG_SIGNAL_ACTIVE;
    regs->useresp = stack_ptr;
    regs->eip = action->handler;
    regs->eax = 0;
    process->saved_regs = *regs;
    return 0;
}

int signal_sigaction(process_t *process, int sig,
                     const bluey_sigaction_t *act,
                     bluey_sigaction_t *oldact) {
    process_sigaction_t *slot;

    if (!process || !signal_is_valid(sig)) return -1;
    if (sig == SIGKILL || sig == SIGSTOP) return -1;

    slot = &process->signal_actions[sig - 1];
    if (oldact) {
        oldact->sa_handler = slot->handler;
        oldact->sa_flags = slot->flags;
        oldact->sa_mask = slot->mask;
    }
    if (act) {
        slot->handler = act->sa_handler;
        slot->flags = act->sa_flags;
        slot->mask = act->sa_mask & ~(signal_bit(SIGKILL) | signal_bit(SIGSTOP));
    }
    return 0;
}

int signal_sigprocmask(process_t *process, int how,
                       const uint32_t *set,
                       uint32_t *oldset) {
    uint32_t mask;

    if (!process) return -1;
    if (oldset) *oldset = process->blocked_signals;
    if (!set) return 0;

    mask = *set & ~(signal_bit(SIGKILL) | signal_bit(SIGSTOP));
    switch (how) {
        case SIG_BLOCK:
            process->blocked_signals |= mask;
            break;
        case SIG_UNBLOCK:
            process->blocked_signals &= ~mask;
            break;
        case SIG_SETMASK:
            process->blocked_signals = mask;
            break;
        default:
            return -1;
    }
    return 0;
}

int signal_sigreturn(process_t *process, registers_t *regs, void *frame_ptr) {
    signal_frame_t *frame = (signal_frame_t*)frame_ptr;

    if (!process || !regs || !frame) return -1;
    if (!(process->flags & PROC_FLAG_SIGNAL_ACTIVE)) return -1;
    if (frame->magic != SIGNAL_FRAME_MAGIC) return -1;

    process->blocked_signals = frame->old_blocked;
    process->flags &= ~PROC_FLAG_SIGNAL_ACTIVE;
    *regs = frame->saved_regs;
    process->saved_regs = *regs;
    return 0;
}

void signal_reset_on_exec(process_t *process) {
    if (!process) return;
    process->pending_signals = 0;
    process->flags &= ~PROC_FLAG_SIGNAL_ACTIVE;
    memset(process->signal_actions, 0, sizeof(process->signal_actions));
}
// BlueyOS Process Management
// "Everyone gets a turn!" - Bandit (Bluey S1E1)
// Episode ref: "Takeaway" - Bandit juggles multiple things at once
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "process.h"
#include "scheduler.h"
#include "kheap.h"
#include "paging.h"
#include "signal.h"
#include "timer.h"
#include "gdt.h"
#include "multiuser.h"
#include "devev.h"
#include "../fs/vfs.h"
#include "kdbg.h"

#define BLUEY_ECHILD 10
#define BLUEY_EAGAIN 11
#define BLUEY_ENOMEM 12
#define BLUEY_EPERM   1
#define PROCESS_RLIMIT_NOFILE_DEFAULT_CUR 256u
#define PROCESS_RLIMIT_NOFILE_DEFAULT_MAX 1024u

#define PROCESS_USER_MMAP_BASE 0x50000000u

static process_t *proc_list  = NULL;   // head of process linked list
static process_t *proc_current = NULL; // currently running process
static uint32_t   next_pid   = 1;
static process_t *proc_deferred_reap = NULL;
static uint8_t    proc_kernel_stacks[MAX_PROCESSES][PROC_STACK_SIZE] __attribute__((aligned(16)));
static uint8_t    proc_kernel_stack_used[MAX_PROCESSES];

static void process_reap(process_t *process);

static int process_has_shared_page_dir(process_t *process) {
    if (!process || !process->page_dir) return 0;

    for (process_t *node = proc_list; node; node = node->next) {
        if (node == process) continue;
        if (node->page_dir == process->page_dir && node->state != PROC_DEAD) {
            return 1;
        }
    }

    return 0;
}

static int process_user_u32_accessible(const process_t *process, uint32_t addr, int writable) {
    uint32_t end_addr;
    uint32_t first_page;
    uint32_t last_page;
    uint32_t *page_dir;

    /* Keep NULL inaccessible for copyin/copyout hardening. */
    if (!process || !process->page_dir || !addr) return 0;

    end_addr = addr + sizeof(uint32_t) - 1u;
    if (end_addr < addr) return 0;

    first_page = addr & ~(PAGE_SIZE - 1u);
    last_page = end_addr & ~(PAGE_SIZE - 1u);
    page_dir = (uint32_t *)(uintptr_t)process->page_dir;

    for (uint32_t page = first_page;; page += PAGE_SIZE) {
        uint32_t pd_idx = page >> 22;
        uint32_t pt_idx = (page >> 12) & 0x3FFu;
        uint32_t pd_entry = page_dir[pd_idx];
        uint32_t *page_table;
        uint32_t pt_entry;

        if (!(pd_entry & PAGE_PRESENT) || !(pd_entry & PAGE_USER)) return 0;
        page_table = (uint32_t *)(uintptr_t)(pd_entry & ~0xFFFu);
        pt_entry = page_table[pt_idx];
        if (!(pt_entry & PAGE_PRESENT) || !(pt_entry & PAGE_USER)) return 0;
        if (writable && !(pt_entry & PAGE_WRITABLE)) return 0;

        if (page == last_page) break;
    }

    return 1;
}

int process_read_user_u32(process_t *process, uint32_t addr, uint32_t *value_out) {
    uint32_t old_page_dir;

    if (!process || !addr || !value_out) return -1;
    if (!process_user_u32_accessible(process, addr, 0)) return -1;

    old_page_dir = paging_current_directory();
    paging_switch_directory(process->page_dir);
    *value_out = *(uint32_t *)(uintptr_t)addr;
    paging_switch_directory(old_page_dir);
    return 0;
}

int process_write_user_u32(process_t *process, uint32_t addr, uint32_t value) {
    uint32_t old_page_dir;

    if (!process || !addr) return -1;
    if (!process_user_u32_accessible(process, addr, 1)) return -1;

    old_page_dir = paging_current_directory();
    paging_switch_directory(process->page_dir);
    *(uint32_t *)(uintptr_t)addr = value;
    paging_switch_directory(old_page_dir);
    return 0;
}

static void process_free_kernel_stack(process_t *process) {
    if (!process || !process->stack_base) return;

    for (uint32_t index = 0; index < MAX_PROCESSES; index++) {
        if ((uint32_t)(uintptr_t)proc_kernel_stacks[index] == process->stack_base) {
            proc_kernel_stack_used[index] = 0;
            process->stack_base = 0;
            return;
        }
    }

    kheap_free((void*)process->stack_base);
    process->stack_base = 0;
}

static void process_release_vfork_parent(process_t *process, int clear_shared_vm) {
    process_t *parent;

    if (!process || !(process->flags & PROC_FLAG_VFORK_SHARED_VM)) return;

    parent = process->parent_pid ? process_get_by_pid(process->parent_pid) : NULL;
    if (parent && parent->vfork_child_pid == process->pid) {
        parent->vfork_child_pid = 0;
        if (parent->state == PROC_WAITING) {
            parent->state = PROC_READY;
        }
    }

    if (clear_shared_vm) {
        process->flags &= ~PROC_FLAG_VFORK_SHARED_VM;
    }
}

uint32_t process_next_pid(void) { return next_pid++; }

static void process_set_credentials(process_t *process, uint32_t uid, uint32_t gid) {
    if (!process) return;
    process->uid = uid;
    process->gid = gid;
    process->euid = uid;
    process->egid = gid;
    process->group_count = multiuser_get_groups(uid, gid, process->groups, PROC_MAX_GROUPS);
    if (process->group_count == 0) {
        if (PROC_MAX_GROUPS > 0) {
            process->groups[0] = gid;
            process->group_count = 1;
        }
    }

    process->rlimit_nofile_cur = PROCESS_RLIMIT_NOFILE_DEFAULT_CUR;
    process->rlimit_nofile_max = PROCESS_RLIMIT_NOFILE_DEFAULT_MAX;
}

static void process_init_kernel_frame(process_t *process, uint32_t entry) {
    memset(&process->saved_regs, 0, sizeof(process->saved_regs));
    process->saved_regs.ds = GDT_KERNEL_DATA;
    process->saved_regs.eip = entry;
    process->saved_regs.cs = GDT_KERNEL_CODE;
    process->saved_regs.eflags = 0x202u;
    process->saved_regs.ss = GDT_KERNEL_DATA;
    process->eip = entry;
    process->esp = process->stack_base + PROC_STACK_SIZE;
}

static void process_init_user_frame(process_t *process, uint32_t entry, uint32_t user_esp) {
    memset(&process->saved_regs, 0, sizeof(process->saved_regs));
    process->saved_regs.gs = GDT_USER_DATA;
    process->saved_regs.ds = GDT_USER_DATA;
    process->saved_regs.eip = entry;
    process->saved_regs.cs = GDT_USER_CODE;
    process->saved_regs.eflags = 0x202u;
    process->saved_regs.useresp = user_esp;
    process->saved_regs.ss = GDT_USER_DATA;
    process->eip = entry;
    process->esp = user_esp;
}

static void process_remove_from_list(process_t *process) {
    process_t *prev = NULL;

    for (process_t *node = proc_list; node; node = node->next) {
        if (node != process) {
            prev = node;
            continue;
        }

        if (prev) prev->next = node->next;
        else proc_list = node->next;
        node->next = NULL;
        return;
    }
}

static void process_reap_deferred(void) {
    process_t *prev = NULL;
    process_t *node = proc_deferred_reap;

    while (node) {
        process_t *next = node->sched_next;
        if (node == proc_current) {
            prev = node;
            node = next;
            continue;
        }

        if (prev) prev->sched_next = next;
        else proc_deferred_reap = next;

        node->sched_next = NULL;
        process_reap(node);
        node = next;
    }
}

static void process_enqueue_deferred_reap(process_t *process) {
    if (!process) return;
    process->sched_next = proc_deferred_reap;
    proc_deferred_reap = process;
}

static int process_wait_matches(process_t *parent, process_t *child) {
    if (!parent || !child) return 0;
    if (child->flags & PROC_FLAG_THREAD) return 0;
    if (parent->state != PROC_WAITING) return 0;
    /* If the parent is blocked waiting for a vfork child (vfork_child_pid != 0),
     * do NOT wake it via the normal waitpid-style path.  The parent is only
     * released when the specific vfork child calls exec() or _exit(), which
     * goes through process_release_vfork_parent().  Waking the parent early
     * lets it run concurrently with its live vfork child, corrupting the
     * shared address space (stack, fd table, etc.). */
    if (parent->vfork_child_pid != 0) return 0;
    if (parent->wait_pid > 0 && parent->wait_pid != (int32_t)child->pid) return 0;
    return child->parent_pid == parent->pid;
}

static void process_write_wait_status(process_t *parent, int status) {
    uint32_t old_page_dir;

    if (!parent || !parent->wait_status_ptr) return;

    old_page_dir = paging_current_directory();
    paging_switch_directory(parent->page_dir);
    *(int*)(uintptr_t)parent->wait_status_ptr = status;
    paging_switch_directory(old_page_dir);
}

static void process_complete_wait(process_t *parent, process_t *child, int reap_now) {
    if (!parent || !child) return;

    process_write_wait_status(parent, child->exit_code);
    parent->saved_regs.eax = (int32_t)child->pid;
    parent->state = PROC_READY;
    parent->wait_pid = 0;
    parent->wait_status_ptr = 0;
    parent->wait_options = 0;

    if (reap_now) {
        process_reap(child);
    } else {
        child->state = PROC_DEAD;
        scheduler_remove(child->pid);
        process_remove_from_list(child);
        process_enqueue_deferred_reap(child);
    }
}

/*
 * When a child enters PROC_STOPPED, check if its parent is blocked in
 * waitpid() with WUNTRACED and wake it if so.  Uses stop_signal as a
 * one-shot flag: set by the stopper, cleared here after delivery so a
 * second waitpid() call for the same stop event is not reported again.
 */
void process_notify_stopped_parent(process_t *child) {
    process_t *parent;
    if (!child || !child->stop_signal) return;
    parent = child->parent_pid ? process_get_by_pid(child->parent_pid) : NULL;
    if (!parent) return;
    if (!(parent->wait_options & WUNTRACED)) return;
    if (!process_wait_matches(parent, child)) return;

    process_write_wait_status(parent, ((int)child->stop_signal << 8) | 0x7f);
    child->stop_signal = 0; /* consumed — one-shot */
    parent->saved_regs.eax = (int32_t)child->pid;
    parent->state = PROC_READY;
    parent->wait_pid = 0;
    parent->wait_status_ptr = 0;
    parent->wait_options = 0;
}

static void process_reap(process_t *process) {
    if (!process) return;

    scheduler_remove(process->pid);
    process_remove_from_list(process);

    if ((process->flags & PROC_FLAG_USER_MODE) && process->page_dir &&
        !process_has_shared_page_dir(process)) {
        paging_destroy_address_space(process->page_dir);
    }
    process_free_kernel_stack(process);
    kheap_free(process);
}

static process_t *process_alloc_common(const char *name, uint32_t uid, uint32_t gid) {
    process_t *process = (process_t*)kheap_alloc(sizeof(process_t), 0);
    if (!process) {
        kdbg(KDBG_PROCESS, "[PRC] ERROR: out of memory for process!\n");
        return NULL;
    }

    memset(process, 0, sizeof(process_t));
    strncpy(process->name, name, sizeof(process->name) - 1);
    process->pid = process_next_pid();
    process->thread_group_id = process->pid;
    process->parent_pid = proc_current ? proc_current->pid : 0;
    process_set_credentials(process, uid, gid);
    process->uid = uid;
    process->gid = gid;
    process->pgid = process->pid;  // new process starts in its own group
    process->state = PROC_READY;
    process->priority = 5;
    process->exit_code = 0;
    process->sleep_until = 0;
    process->page_dir = paging_current_directory();
    process->cwd[0] = '/';
    process->cwd[1] = '\0';

    process->next = proc_list;
    proc_list = process;
    /* Initialize CPU accounting */
    process->cpu_ticks = 0;
    process->cpu_last_tick = 0;
    return process;
}

static uint8_t *process_alloc_kernel_stack(process_t *process) {
    if (!process) return NULL;

    for (uint32_t index = 0; index < MAX_PROCESSES; index++) {
        if (proc_kernel_stack_used[index]) continue;

        proc_kernel_stack_used[index] = 1;
        process->stack_base = (uint32_t)(uintptr_t)proc_kernel_stacks[index];
        return proc_kernel_stacks[index];
    }

    kdbg(KDBG_PROCESS, "[PRC] ERROR: no kernel stacks available! max=%u\n", MAX_PROCESSES);
    return NULL;
}

void process_init(void) {
    proc_list    = NULL;
    proc_current = NULL;
    proc_deferred_reap = NULL;
    memset(proc_kernel_stack_used, 0, sizeof(proc_kernel_stack_used));
    kprintf("%s\n", MSG_PROC_INIT);
}

process_t *process_create(const char *name, void (*entry)(void),
                          uint32_t uid, uint32_t gid) {
    process_t *process = process_alloc_common(name, uid, gid);
    uint8_t *stack;

    if (!process) return NULL;

    stack = process_alloc_kernel_stack(process);
    if (!stack) {
        process->state = PROC_DEAD;
        return NULL;
    }

    process_init_kernel_frame(process, (uint32_t)entry);

    kdbg(KDBG_PROCESS, "[PRC]  Created process '%s' (pid=%d uid=%d)\n",
            process->name, process->pid, process->uid);
    return process;
}

process_t *process_create_image(const char *name, uint32_t entry, uint32_t user_esp,
                                uint32_t user_stack_base, uint32_t user_stack_top,
                                uint32_t page_dir,
                                uint32_t uid, uint32_t gid) {
    process_t *process = process_alloc_common(name, uid, gid);
    uint8_t *stack;

    if (!process) return NULL;

    stack = process_alloc_kernel_stack(process);
    if (!stack) {
        process->state = PROC_DEAD;
        return NULL;
    }

    process->flags |= PROC_FLAG_USER_MODE;
    process->user_stack_base = user_stack_base;
    process->user_stack_top = user_stack_top;
    /* Default RLIMIT: usable stack = (stack_size - 1 page) (guard page at base). */
    if (user_stack_top > user_stack_base + PAGE_SIZE) {
        process->rlimit_stack_cur = (user_stack_top - user_stack_base) - PAGE_SIZE;
    } else {
        process->rlimit_stack_cur = 0;
    }
    process->rlimit_stack_max = process->rlimit_stack_cur;
    process->tls_base = 0;
    process->page_dir = page_dir;
    process_init_user_frame(process, entry, user_esp);

    kdbg(KDBG_PROCESS, "[PRC]  Created image '%s' (pid=%d uid=%d eip=0x%x esp=0x%x)\n",
            process->name, process->pid, process->uid, process->eip, process->esp);
    return process;
}

process_t *process_fork_current(const registers_t *regs, int32_t *error_out) {
    process_t *parent = proc_current;
    process_t *child;
    uint8_t *stack;
    uint32_t page_dir;

    if (error_out) *error_out = -BLUEY_EAGAIN;

    if (!parent || !regs || !(parent->flags & PROC_FLAG_USER_MODE)) {
        if (error_out) *error_out = -BLUEY_EPERM;
        kdbg(KDBG_PROCESS, "[PRC] fork failed: invalid parent/regs/user-mode state (parent=%p regs=%p flags=0x%x)\n",
                (void*)parent, (void*)regs, parent ? parent->flags : 0u);
        return NULL;
    }

    page_dir = paging_clone_address_space(parent->page_dir);
    if (!page_dir) {
        if (error_out) *error_out = -BLUEY_ENOMEM;
        kdbg(KDBG_PROCESS, "[PRC] fork failed: paging_clone_address_space for pid=%u (used_frames=%u total_frames=%u)\n",
                parent->pid, pmm_used_frames(), pmm_total_frames());
        return NULL;
    }

    child = process_alloc_common(parent->name, parent->uid, parent->gid);
    if (!child) {
        if (error_out) *error_out = -BLUEY_ENOMEM;
        kdbg(KDBG_PROCESS, "[PRC] fork failed: process_alloc_common for parent pid=%u\n", parent->pid);
        paging_destroy_address_space(page_dir);
        return NULL;
    }

    stack = process_alloc_kernel_stack(child);
    if (!stack) {
        if (error_out) *error_out = -BLUEY_ENOMEM;
        kdbg(KDBG_PROCESS, "[PRC] fork failed: process_alloc_kernel_stack for child pid=%u parent pid=%u\n",
                child->pid, parent->pid);
        process_remove_from_list(child);
        kheap_free(child);
        paging_destroy_address_space(page_dir);
        return NULL;
    }

    child->flags = parent->flags & ~PROC_FLAG_SIGNAL_ACTIVE;
    child->euid = parent->euid;
    child->egid = parent->egid;
    child->group_count = parent->group_count;
    memcpy(child->groups, parent->groups, sizeof(child->groups));
    child->pgid = parent->pgid;
    child->user_stack_base = parent->user_stack_base;
    child->user_stack_top = parent->user_stack_top;
    child->rlimit_stack_cur = parent->rlimit_stack_cur;
    child->rlimit_stack_max = parent->rlimit_stack_max;
    child->rlimit_nofile_cur = parent->rlimit_nofile_cur;
    child->rlimit_nofile_max = parent->rlimit_nofile_max;
    child->tls_base = parent->tls_base;
    child->page_dir = page_dir;
    child->brk_base = parent->brk_base;
    child->brk_current = parent->brk_current;
    child->mmap_base = parent->mmap_base;
    child->blocked_signals = parent->blocked_signals;
    memcpy(child->signal_actions, parent->signal_actions, sizeof(child->signal_actions));
    memcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    vfs_inherit_fd_table(parent, child);
    child->saved_regs = *regs;
    child->saved_regs.eax = 0;
    child->eip = child->saved_regs.eip;
    child->esp = child->saved_regs.useresp;
    /* Child starts with zeroed CPU accounting (do not inherit parent's usage) */
    child->cpu_ticks = 0;
    child->cpu_last_tick = 0;
    if (error_out) *error_out = 0;
    return child;
}

process_t *process_clone_current(const registers_t *regs, uint32_t child_stack,
                                 int share_vm, int32_t *error_out) {
    process_t *parent = proc_current;
    process_t *child;
    uint8_t *stack;
    uint32_t page_dir;

    if (error_out) *error_out = -BLUEY_EAGAIN;

    if (!parent || !regs || !(parent->flags & PROC_FLAG_USER_MODE)) {
        if (error_out) *error_out = -BLUEY_EPERM;
        return NULL;
    }

    if (share_vm) {
        page_dir = parent->page_dir;
    } else {
        page_dir = paging_clone_address_space(parent->page_dir);
        if (!page_dir) {
            if (error_out) *error_out = -BLUEY_ENOMEM;
            return NULL;
        }
    }

    child = process_alloc_common(parent->name, parent->uid, parent->gid);
    if (!child) {
        if (!share_vm && page_dir) paging_destroy_address_space(page_dir);
        if (error_out) *error_out = -BLUEY_ENOMEM;
        return NULL;
    }

    stack = process_alloc_kernel_stack(child);
    if (!stack) {
        process_remove_from_list(child);
        kheap_free(child);
        if (!share_vm && page_dir) paging_destroy_address_space(page_dir);
        if (error_out) *error_out = -BLUEY_ENOMEM;
        return NULL;
    }

    child->flags = parent->flags & ~PROC_FLAG_SIGNAL_ACTIVE;
    if (share_vm) child->flags |= PROC_FLAG_SHARED_VM;
    child->euid = parent->euid;
    child->egid = parent->egid;
    child->group_count = parent->group_count;
    memcpy(child->groups, parent->groups, sizeof(child->groups));
    child->pgid = parent->pgid;
    child->user_stack_base = parent->user_stack_base;
    child->user_stack_top = parent->user_stack_top;
    child->rlimit_stack_cur = parent->rlimit_stack_cur;
    child->rlimit_stack_max = parent->rlimit_stack_max;
    child->rlimit_nofile_cur = parent->rlimit_nofile_cur;
    child->rlimit_nofile_max = parent->rlimit_nofile_max;
    child->tls_base = parent->tls_base;
    child->page_dir = page_dir;
    child->brk_base = parent->brk_base;
    child->brk_current = parent->brk_current;
    child->mmap_base = parent->mmap_base;
    child->blocked_signals = parent->blocked_signals;
    memcpy(child->signal_actions, parent->signal_actions, sizeof(child->signal_actions));
    memcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    child->saved_regs = *regs;
    child->saved_regs.eax = 0;
    if (child_stack) child->saved_regs.useresp = child_stack;
    child->eip = child->saved_regs.eip;
    child->esp = child->saved_regs.useresp;
    child->cpu_ticks = 0;
    child->cpu_last_tick = 0;

    if (error_out) *error_out = 0;
    return child;
}

process_t *process_vfork_current(const registers_t *regs, int32_t *error_out) {
    process_t *parent = proc_current;
    process_t *child;
    uint8_t *stack;

    if (error_out) *error_out = -BLUEY_EAGAIN;

    if (!parent || !regs || !(parent->flags & PROC_FLAG_USER_MODE)) {
        if (error_out) *error_out = -BLUEY_EPERM;
        kdbg(KDBG_PROCESS, "[PRC] vfork failed: invalid parent/regs/user-mode state (parent=%p regs=%p flags=0x%x)\n",
                (void*)parent, (void*)regs, parent ? parent->flags : 0u);
        return NULL;
    }

    if (parent->vfork_child_pid != 0) {
        if (error_out) *error_out = -BLUEY_EAGAIN;
        kdbg(KDBG_PROCESS, "[PRC] vfork failed: parent pid=%u already has active child pid=%u\n",
                parent->pid, parent->vfork_child_pid);
        return NULL;
    }

    child = process_alloc_common(parent->name, parent->uid, parent->gid);
    if (!child) {
        if (error_out) *error_out = -BLUEY_ENOMEM;
        kdbg(KDBG_PROCESS, "[PRC] vfork failed: process_alloc_common for parent pid=%u\n", parent->pid);
        return NULL;
    }

    stack = process_alloc_kernel_stack(child);
    if (!stack) {
        if (error_out) *error_out = -BLUEY_ENOMEM;
        kdbg(KDBG_PROCESS, "[PRC] vfork failed: process_alloc_kernel_stack for child pid=%u parent pid=%u\n",
                child->pid, parent->pid);
        process_remove_from_list(child);
        kheap_free(child);
        return NULL;
    }

    child->flags = (parent->flags & ~PROC_FLAG_SIGNAL_ACTIVE) | PROC_FLAG_VFORK_SHARED_VM;
    child->euid = parent->euid;
    child->egid = parent->egid;
    child->group_count = parent->group_count;
    memcpy(child->groups, parent->groups, sizeof(child->groups));
    child->pgid = parent->pgid;
    child->user_stack_base = parent->user_stack_base;
    child->user_stack_top = parent->user_stack_top;
    child->rlimit_stack_cur = parent->rlimit_stack_cur;
    child->rlimit_stack_max = parent->rlimit_stack_max;
    child->rlimit_nofile_cur = parent->rlimit_nofile_cur;
    child->rlimit_nofile_max = parent->rlimit_nofile_max;
    child->tls_base = parent->tls_base;
    child->page_dir = parent->page_dir;
    child->brk_base = parent->brk_base;
    child->brk_current = parent->brk_current;
    child->mmap_base = parent->mmap_base;
    child->blocked_signals = parent->blocked_signals;
    memcpy(child->signal_actions, parent->signal_actions, sizeof(child->signal_actions));
    memcpy(child->cwd, parent->cwd, sizeof(child->cwd));
    child->saved_regs = *regs;
    child->saved_regs.eax = 0;
    child->eip = child->saved_regs.eip;
    child->esp = child->saved_regs.useresp;
    child->cpu_ticks = 0;
    child->cpu_last_tick = 0;

    parent->vfork_child_pid = child->pid;
    parent->state = PROC_WAITING;
    parent->wait_pid = 0;
    parent->wait_status_ptr = 0;
    parent->wait_options = 0;

    if (error_out) *error_out = 0;
    return child;
}

void process_vfork_execve_failed(process_t *child) {
    /* Called when execve() fails for a vfork child before process_exec_replace
     * has had a chance to run.  The parent is still blocked in PROC_WAITING;
     * we must unblock it here so it does not wait forever.  We do NOT clear
     * PROC_FLAG_VFORK_SHARED_VM because the child is about to call _exit(),
     * and we need the flag to prevent the shared page directory from being
     * destroyed when the child is reaped. */
    process_release_vfork_parent(child, 0);
}

void process_exec_replace(process_t *process, const char *name,
                          uint32_t entry, uint32_t user_esp,
                          uint32_t user_stack_base, uint32_t user_stack_top,
                          uint32_t page_dir) {
    int release_vfork_parent;

    if (!process) return;

    release_vfork_parent = (process->flags & PROC_FLAG_VFORK_SHARED_VM) != 0;

    if (name && name[0]) {
        strncpy(process->name, name, sizeof(process->name) - 1);
        process->name[sizeof(process->name) - 1] = '\0';
    }

    process->flags |= PROC_FLAG_USER_MODE;
    process->state = PROC_READY;
    process->user_stack_base = user_stack_base;
    process->user_stack_top = user_stack_top;
    if (user_stack_top > user_stack_base + PAGE_SIZE) {
        process->rlimit_stack_cur = (user_stack_top - user_stack_base) - PAGE_SIZE;
    } else {
        process->rlimit_stack_cur = 0;
    }
    process->rlimit_stack_max = process->rlimit_stack_cur;
    process->tls_base = 0;
    process->page_dir = page_dir;
    process->exit_code = 0;
    process_init_user_frame(process, entry, user_esp);

    if (release_vfork_parent) {
        process_release_vfork_parent(process, 1);
    }

    signal_reset_on_exec(process);
}

void process_set_memory_layout(process_t *process, uint32_t image_end) {
    uint32_t aligned_end;

    if (!process) return;

    aligned_end = (image_end + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);
    if (aligned_end < 0x100000u) aligned_end = 0x100000u;

    process->brk_base = aligned_end;
    process->brk_current = aligned_end;
    process->mmap_base = PROCESS_USER_MMAP_BASE;
}

void process_mark_exited(process_t *process, int code) {
    process_t *parent;
    devev_event_t ev;

    if (!process || process->state == PROC_ZOMBIE || process->state == PROC_DEAD) return;

    kdbg(KDBG_PROCESS, "[PRC]  mark_exited: pid=%u code=%d parent_pid=%u\n",
         process->pid, code, process->parent_pid);

    process->state = PROC_ZOMBIE;
    process->exit_code = code;
    process->pending_signals = 0;

    /* Close all open file descriptors — release pipe/socket refcounts. */
    vfs_close_all_fds_for_process(process);

    if (process->clear_child_tid) {
        process_write_user_u32(process, process->clear_child_tid, 0);
        process_wake_futex(process, process->clear_child_tid, 1, 1, 0);
        process->clear_child_tid = 0;
    }

    process_release_vfork_parent(process, 0);

    if (process->flags & PROC_FLAG_THREAD) {
        process->state = PROC_DEAD;
        scheduler_remove(process->pid);
        process_remove_from_list(process);
        process_enqueue_deferred_reap(process);
        return;
    }

    /* Notify the device event channel so supervisors like claw can react */
    ev.type = DEV_EV_CHILD_EXIT;
    ev._pad[0] = ev._pad[1] = ev._pad[2] = 0;
    ev.pid = process->pid;
    ev.code = (uint32_t)code;
    ev.reserved = 0;
    devev_push(&ev);

    parent = process->parent_pid ? process_get_by_pid(process->parent_pid) : NULL;
    if (parent) {
        signal_send_pid(parent->pid, SIGCHLD);
        if (process_wait_matches(parent, process)) {
            process_complete_wait(parent, process, 0);
        } else if (parent->state == PROC_WAITING && parent->vfork_child_pid == 0) {
            /* Wake the parent only if it is not blocked for a vfork child.
             * Vfork-waiting parents are released exclusively through
             * process_release_vfork_parent(). */
            parent->state = PROC_READY;
        }
    }
}

void process_exit(int code) {
    process_t *dying;
    if (!proc_current) return;
    dying = proc_current;
    process_mark_exited(dying, code);
    kdbg(KDBG_PROCESS, "[PRC]  Process '%s' (pid=%d) exited with code %d\n",
            dying->name, dying->pid, code);
    /* The process is now a zombie; rescheduling occurs on trap return. */
}

process_t *process_current(void) { return proc_current; }
void process_set_current(process_t *p) {
    proc_current = p;
    if (p && p->stack_base) {
        tss_set_kernel_stack(p->stack_base + PROC_STACK_SIZE);
    }
    process_reap_deferred();
}
void process_set_waiting(process_t *p) {
    if (p) p->state = PROC_WAITING;
}

void process_set_futex_wait(process_t *process, uint32_t addr, uint32_t deadline, int priv) {
    if (!process) return;

    process->state = PROC_WAITING;
    process->futex_wait_addr = addr;
    process->futex_wait_deadline = deadline;
    process->futex_wait_result = 0;
    process->futex_wait_private = priv ? 1u : 0u;
    process->saved_regs.eax = 0;
}

int process_wake_futex(process_t *caller, uint32_t addr, int count, int priv, int32_t result) {
    int woken = 0;
    uint8_t wait_priv = priv ? 1u : 0u;

    if (!caller || !addr || count == 0) return 0;
    if (count < 0) count = MAX_PROCESSES;

    for (process_t *node = proc_list; node; node = node->next) {
        if (node->state != PROC_WAITING || node->futex_wait_addr != addr) continue;
        if (node->futex_wait_private != wait_priv) continue;
        if (priv && node->page_dir != caller->page_dir) continue;

        node->state = PROC_READY;
        node->futex_wait_addr = 0;
        node->futex_wait_deadline = 0;
        node->futex_wait_result = result;
        node->futex_wait_private = 0;
        node->saved_regs.eax = (uint32_t)result;
        woken++;
        if (woken >= count) break;
    }

    return woken;
}

int process_requeue_futex(process_t *caller, uint32_t addr, int wake_count,
                          int requeue_count, uint32_t new_addr, int priv) {
    int moved = 0;
    uint8_t wait_priv = priv ? 1u : 0u;
    int woken = process_wake_futex(caller, addr, wake_count, priv, 0);

    for (process_t *node = proc_list; node && moved < requeue_count; node = node->next) {
        if (node->state != PROC_WAITING || node->futex_wait_addr != addr) continue;
        if (node->futex_wait_private != wait_priv) continue;
        if (priv && node->page_dir != caller->page_dir) continue;

        node->futex_wait_addr = new_addr;
        moved++;
    }

    return woken + moved;
}
process_t *process_first(void) { return proc_list; }
process_t *process_next(process_t *p) { return p ? p->next : NULL; }

process_t *process_get_by_pid(uint32_t pid) {
    for (process_t *p = proc_list; p; p = p->next)
        if (p->pid == pid) return p;
    return NULL;
}

int32_t process_waitpid(int32_t pid, int *status, int options) {
    process_t *current = proc_current;
    int found_child = 0;

    process_reap_deferred();

    for (process_t *process = proc_list; process; ) {
        process_t *next = process->next;

        if (current && !(process->flags & PROC_FLAG_THREAD) &&
            process->parent_pid == current->pid &&
            (pid <= 0 || (int32_t)process->pid == pid)) {
            found_child = 1;
            if (process->state == PROC_ZOMBIE) {
                int32_t reaped_pid = (int32_t)process->pid;
                if (status) *status = process->exit_code;
                process_complete_wait(current, process, 1);
                return reaped_pid;
            }
            /* Return immediately for a child with a pending stop report when
             * WUNTRACED is set. stop_signal acts as a one-shot flag: we clear
             * it on delivery so a subsequent waitpid() for the same stop event
             * is not returned, even if the child has since been continued. */
            if ((options & WUNTRACED) && process->stop_signal) {
                if (status) *status = ((int)process->stop_signal << 8) | 0x7f;
                process->stop_signal = 0;
                return (int32_t)process->pid;
            }
        }

        process = next;
    }

    if (!found_child) return -BLUEY_ECHILD;
    if (options & WNOHANG) return 0;

    current->state = PROC_WAITING;
    current->wait_pid = pid;
    current->wait_status_ptr = (uint32_t)(uintptr_t)status;
    current->wait_options = (uint32_t)options;
    return -BLUEY_EAGAIN;
}

uint32_t process_getpid(void) {
    return proc_current ? proc_current->thread_group_id : 0;
}

uint32_t process_get_uid(void) {
    return proc_current ? proc_current->uid : 0;
}

uint32_t process_get_gid(void) {
    return proc_current ? proc_current->gid : 0;
}

uint32_t process_get_euid(void) {
    return proc_current ? proc_current->euid : 0;
}

uint32_t process_get_egid(void) {
    return proc_current ? proc_current->egid : 0;
}

int process_in_group(const process_t *process, uint32_t gid) {
    if (!process) return 0;
    if (process->egid == gid) return 1;
    for (uint32_t i = 0; i < process->group_count; i++) {
        if (process->groups[i] == gid) return 1;
    }
    return 0;
}

void process_set_effective_ids(process_t *process, uint32_t euid, uint32_t egid) {
    if (!process) return;
    process->euid = euid;
    process->egid = egid;
}

void process_sleep(uint32_t ms) {
    if (!proc_current) return;
    proc_current->state       = PROC_SLEEPING;
    proc_current->sleep_until = timer_get_ticks() + ms;  // timer runs at 1000Hz → 1 tick = 1ms
    // Yield to scheduler
    scheduler_yield();
}

void process_wake(uint32_t pid) {
    process_t *p = process_get_by_pid(pid);
    if (p && p->state == PROC_SLEEPING) p->state = PROC_READY;
}

uint32_t process_getpgid(uint32_t pid) {
    process_t *p = pid ? process_get_by_pid(pid) : proc_current;
    return p ? p->pgid : 0;
}

int process_setpgid(uint32_t pid, uint32_t pgid) {
    process_t *p = pid ? process_get_by_pid(pid) : proc_current;
    if (!p) return -1;
    p->pgid = pgid ? pgid : p->pid;
    return 0;
}

uint32_t process_getppid(void) {
    return proc_current ? proc_current->parent_pid : 0;
}

const char *process_get_cwd(void) {
    return proc_current ? proc_current->cwd : "/";
}

void process_set_cwd(const char *path) {
    if (!proc_current || !path) return;
    strncpy(proc_current->cwd, path, sizeof(proc_current->cwd) - 1);
    proc_current->cwd[sizeof(proc_current->cwd) - 1] = '\0';
}

void process_enter_first_user(process_t *process) {
    registers_t *regs;

    if (!process || !(process->flags & PROC_FLAG_USER_MODE)) {
        PANIC("process_enter_first_user requires a user image");
    }

    process->state = PROC_RUNNING;
    process_set_current(process);
    paging_switch_directory(process->page_dir);
    regs = &process->saved_regs;

    /* Move segment selector into 16-bit register from a memory operand
     * to avoid register-size mismatches, then push the user-mode stack
     * frame and iret into user space. Using local temporaries ensures
     * GCC can generate valid memory/register operands for the asm. */
    uint16_t ds_val = (uint16_t)regs->ds;
    uint16_t gs_val = (uint16_t)regs->gs;
    uint32_t ss_val = regs->ss;
    uint32_t esp_val = regs->useresp;
    uint32_t eflags_val = regs->eflags;
    uint32_t cs_val = regs->cs;
    uint32_t eip_val = regs->eip;

    __asm__ volatile (
        "movw %0, %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%fs\n\t"
        : /* no outputs */
        : "m" (ds_val)
        : "ax", "memory"
    );

    __asm__ volatile (
        "movw %0, %%ax\n\t"
        "movw %%ax, %%gs\n\t"
        : /* no outputs */
        : "m" (gs_val)
        : "ax", "memory"
    );

    __asm__ volatile (
        "pushl %0\n\t"
        "pushl %1\n\t"
        "pushl %2\n\t"
        "pushl %3\n\t"
        "pushl %4\n\t"
        "iret\n\t"
        :
        : "r" (ss_val), "r" (esp_val), "r" (eflags_val), "r" (cs_val), "r" (eip_val)
        : "memory"
    );

    for (;;) __asm__ volatile ("hlt");
}

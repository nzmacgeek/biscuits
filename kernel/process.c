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
#include "timer.h"

static process_t *proc_list  = NULL;   // head of process linked list
static process_t *proc_current = NULL; // currently running process
static uint32_t   next_pid   = 1;

uint32_t process_next_pid(void) { return next_pid++; }

const char *process_mode_name(proc_mode_t mode) {
    return (mode == PROC_MODE_USER) ? "user" : "kernel";
}

void process_init(void) {
    proc_list    = NULL;
    proc_current = NULL;
    kprintf("%s\n", MSG_PROC_INIT);
}

static process_t *process_create_with_mode(const char *name, void (*entry)(void),
                                           uint32_t uid, uint32_t gid,
                                           proc_mode_t mode) {
    process_t *p = (process_t*)kheap_alloc(sizeof(process_t), 0);
    if (!p) { kprintf("[PRC] ERROR: out of memory for process!\n"); return NULL; }
    memset(p, 0, sizeof(process_t));

    strncpy(p->name, name, sizeof(p->name) - 1);
    p->pid        = process_next_pid();
    p->uid        = uid;
    p->gid        = gid;
    p->state      = PROC_READY;
    p->mode       = mode;
    p->priority   = 5;
    p->exit_code  = 0;
    p->sleep_until = 0;

    // Allocate a kernel stack for this process
    uint8_t *stack = (uint8_t*)kheap_alloc(PROC_STACK_SIZE, 1);
    if (!stack) {
        kheap_free(p);
        kprintf("[PRC] ERROR: out of memory for stack!\n");
        return NULL;
    }
    p->stack_base = (uint32_t)stack;

    // Set up a minimal stack frame for the first context switch
    uint32_t *sp = (uint32_t*)(stack + PROC_STACK_SIZE);
    *--sp = (uint32_t)entry;   // return address = entry point
    p->esp = (uint32_t)sp;
    p->eip = (uint32_t)entry;

    // Link into process list
    p->next = proc_list;
    proc_list = p;

    kprintf("[PRC]  Created %s process '%s' (pid=%d uid=%d)\n",
            process_mode_name(p->mode), p->name, p->pid, p->uid);
    return p;
}

process_t *process_create(const char *name, void (*entry)(void),
                          uint32_t uid, uint32_t gid) {
    return process_create_with_mode(name, entry, uid, gid, PROC_MODE_KERNEL);
}

process_t *process_create_kernel(const char *name, void (*entry)(void),
                                 uint32_t uid, uint32_t gid) {
    return process_create_with_mode(name, entry, uid, gid, PROC_MODE_KERNEL);
}

process_t *process_create_user(const char *name, void (*entry)(void),
                               uint32_t uid, uint32_t gid) {
    return process_create_with_mode(name, entry, uid, gid, PROC_MODE_USER);
}

void process_exit(int code) {
    if (!proc_current) return;
    proc_current->state     = PROC_ZOMBIE;
    proc_current->exit_code = code;
    kprintf("[PRC]  Process '%s' (pid=%d) exited with code %d\n",
            proc_current->name, proc_current->pid, code);
    // Yield; scheduler will pick next ready process
    __asm__ volatile("int $0x80" :: "a"(60), "b"(code));
}

process_t *process_current(void) { return proc_current; }
void process_set_current(process_t *p) { proc_current = p; }

process_t *process_get_by_pid(uint32_t pid) {
    for (process_t *p = proc_list; p; p = p->next)
        if (p->pid == pid) return p;
    return NULL;
}

uint32_t process_getpid(void) {
    return proc_current ? proc_current->pid : 0;
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

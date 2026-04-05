// BlueyOS Round-Robin Scheduler
// "Bandit's Homework Scheduler - fair turns for everyone!"
// Episode ref: "Takeaway" - managing everything at once, somehow
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "scheduler.h"
#include "process.h"
#include "paging.h"
#include "signal.h"
#include "gdt.h"
#include "timer.h"

static process_t *run_queue   = NULL;   // circular linked list of runnable processes
static process_t *sched_current = NULL;

static void scheduler_idle_fallback(void) {
    for (;;) __asm__ volatile("sti; hlt");
}

static process_t *scheduler_pick_next_user(process_t *current, int rotate) {
    process_t *start;
    process_t *process;

    if (!run_queue) return NULL;

    if (current && current->sched_next) {
        start = rotate ? current->sched_next : current;
    } else {
        start = run_queue;
    }

    process = start;
    do {
        int runnable = (process->state == PROC_READY) || (!rotate && process == current && process->state == PROC_RUNNING);
        if ((process->flags & PROC_FLAG_USER_MODE) && runnable) return process;
        process = process->sched_next;
    } while (process && process != start);

    return NULL;
}

static void scheduler_prepare_fallback_frame(registers_t *regs) {
    memset(regs, 0, sizeof(*regs));
    regs->ds = GDT_KERNEL_DATA;
    regs->eip = (uint32_t)scheduler_idle_fallback;
    regs->cs = GDT_KERNEL_CODE;
    regs->eflags = 0x202u;
    regs->ss = GDT_KERNEL_DATA;
}

void scheduler_init(void) {
    run_queue     = NULL;
    sched_current = NULL;
    kprintf("%s\n", MSG_SCHED_INIT);
}

void scheduler_add(process_t *p) {
    if (!p) return;
    p->state = PROC_READY;
    if (!run_queue) {
        run_queue = p;
        p->sched_next = p;  // circular list
    } else {
        // Insert at end: find tail (node whose next = run_queue)
        process_t *tail = run_queue;
        while (tail->sched_next != run_queue) tail = tail->sched_next;
        tail->sched_next = p;
        p->sched_next    = run_queue;
    }
}

void scheduler_remove(uint32_t pid) {
    if (!run_queue) return;
    // Find and unlink the process with given pid
    process_t *prev = run_queue;
    process_t *cur  = run_queue->sched_next;
    // If only one element
    if (run_queue->pid == pid) {
        if (run_queue->sched_next == run_queue) {
            if (sched_current == run_queue) sched_current = NULL;
            run_queue->sched_next = NULL;
            run_queue = NULL;
            return;
        }
        // Find last node
        process_t *tail = run_queue;
        while (tail->sched_next != run_queue) tail = tail->sched_next;
        tail->sched_next = run_queue->sched_next;
        if (sched_current == run_queue) sched_current = run_queue->sched_next;
        run_queue->sched_next = NULL;
        run_queue  = tail->sched_next;
        return;
    }
    while (cur != run_queue) {
        if (cur->pid == pid) {
            prev->sched_next = cur->sched_next;
            if (sched_current == cur) sched_current = cur->sched_next;
            cur->sched_next = NULL;
            return;
        }
        prev = cur; cur = cur->sched_next;
    }
}

// Called from timer IRQ (IRQ0) every tick
// Simple round-robin: advance to next READY process
void scheduler_tick(void) {
    if (!run_queue) return;

    // Wake sleeping processes whose time has come
    process_t *p = run_queue;
    do {
        if (p->state == PROC_SLEEPING &&
            timer_get_ticks() >= p->sleep_until) {
            p->state = PROC_READY;
        }
        p = p->sched_next;
    } while (p != run_queue);

    // Trap-frame switching happens in scheduler_handle_trap(), which only
    // switches on user-mode return paths.
}

process_t *scheduler_current(void) { return sched_current; }

void scheduler_handle_trap(registers_t *regs, int rotate) {
    process_t *current = process_current();
    process_t *next;
    int user_trap;

    if (!regs) return;

    user_trap = current &&
                (current->flags & PROC_FLAG_USER_MODE) &&
                ((regs->cs & 0x3u) == 0x3u);

    if (!user_trap) {
        return;
    }

    if (current) {
        current->saved_regs = *regs;
        current->eip = regs->eip;
        current->esp = regs->useresp;
        if (current->state == PROC_RUNNING) current->state = PROC_READY;
    }

    next = scheduler_pick_next_user(current,
                                    rotate || (current && current->state != PROC_RUNNING && current->state != PROC_READY));
    if (!next) {
        if (current && (current->flags & PROC_FLAG_USER_MODE) &&
            (current->state == PROC_READY || current->state == PROC_RUNNING)) {
            next = current;
        } else {
            process_set_current(NULL);
            scheduler_prepare_fallback_frame(regs);
            return;
        }
    }

    while (next) {
        registers_t frame = next->saved_regs;

        paging_switch_directory(next->page_dir);
        /* Update per-process TLS GDT entry before returning to user space. */
        extern void gdt_set_tls_base(uint32_t);
        gdt_set_tls_base(next->tls_base);
        if (signal_dispatch_pending(next, &frame) != 0) {
            next = scheduler_pick_next_user(next, 1);
            continue;
        }

        /* Sanitize EFLAGS: clear TF (bit 8) and ensure IF (bit 9) is set. */
        frame.eflags = (frame.eflags & ~0x100u) | 0x200u;

        next->saved_regs = frame;
        next->state = PROC_RUNNING;
        sched_current = next;
        process_set_current(next);
        *regs = frame;
        return;
    }

    process_set_current(NULL);
    scheduler_prepare_fallback_frame(regs);
}

void scheduler_yield(void) {
    // Trigger a software timer tick by calling scheduler_tick directly
    // In a real OS this would be a context switch via task switching or iret
    scheduler_tick();
}

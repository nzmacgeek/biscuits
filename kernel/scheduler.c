// BlueyOS CFS Scheduler
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
/* Set to 1 when the scheduler has entered the idle fallback loop.
 * Only when this flag is set is it safe to replace the interrupt frame
 * with a user-mode context (the idle loop has nothing to clean up). */
static volatile int sched_in_idle_fallback = 0;

/* ---------------------------------------------------------------------------
 * Completely Fair Scheduler (CFS)
 *
 * Each process has a vruntime that increments at a rate inversely proportional
 * to its weight.  Priority 1..10 maps to weights derived from Linux's nice
 * table centred on nice=0 (priority 5, weight 1024 = NICE_0_WEIGHT).
 *
 * vruntime_delta = delta_ticks * NICE_0_WEIGHT / task_weight
 *
 * The scheduler always picks the runnable process with the smallest vruntime.
 * ---------------------------------------------------------------------------*/
#define CFS_NICE0_WEIGHT  1024u

/* Weights for priority 1..10 sampled from Linux's sched_prio_to_weight table
 * (nice +4 down to nice -5). */
static const uint32_t cfs_weight_table[10] = {
    423u,   /* priority  1  (nice +4) */
    526u,   /* priority  2  (nice +3) */
    655u,   /* priority  3  (nice +2) */
    820u,   /* priority  4  (nice +1) */
    1024u,  /* priority  5  (nice  0) — baseline */
    1277u,  /* priority  6  (nice -1) */
    1586u,  /* priority  7  (nice -2) */
    1991u,  /* priority  8  (nice -3) */
    2501u,  /* priority  9  (nice -4) */
    3121u,  /* priority 10  (nice -5) */
};

static uint32_t cfs_get_weight(const process_t *p) {
    uint32_t pri = p->priority;
    if (pri < 1u)  pri = 1u;
    if (pri > 10u) pri = 10u;
    return cfs_weight_table[pri - 1u];
}

/* Smallest vruntime among all runnable user processes (wrapping-safe). */
static uint64_t cfs_min_vruntime(void) {
    process_t *p = run_queue;
    uint64_t min_vr = 0;
    int first = 1;

    if (!p) return 0;
    do {
        if ((p->flags & PROC_FLAG_USER_MODE) &&
            (p->state == PROC_READY || p->state == PROC_RUNNING)) {
            if (first || p->vruntime < min_vr) {
                min_vr = p->vruntime;
                first = 0;
            }
        }
        p = p->sched_next;
    } while (p && p != run_queue);

    return min_vr;
}

static void scheduler_idle_fallback(void) {
    sched_in_idle_fallback = 1;
    for (;;) __asm__ volatile("sti; hlt");
}

/* Pick the runnable user process with the smallest vruntime.
 * When rotate=0 the current process is also a candidate (voluntary yield
 * only switches if another process has strictly smaller vruntime). */
static process_t *scheduler_pick_next_user(process_t *current, int rotate) {
    process_t *p = run_queue;
    process_t *best = NULL;

    if (!p) return NULL;

    do {
        int runnable = (p->state == PROC_READY) ||
                       (!rotate && p == current && p->state == PROC_RUNNING);
        if ((p->flags & PROC_FLAG_USER_MODE) && runnable) {
            if (!best || p->vruntime < best->vruntime) {
                best = p;
            }
        }
        p = p->sched_next;
    } while (p && p != run_queue);

    return best;
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

// Called from timer IRQ (IRQ0) every tick.
// Wake sleeping processes; normalise vruntime of newly-woken processes so
// they do not get a huge burst of CPU time after sleeping.
void scheduler_tick(void) {
    uint64_t min_vr;
    process_t *p;
    uint32_t now;

    if (!run_queue) return;

    min_vr = cfs_min_vruntime();
    now = timer_get_ticks();

    p = run_queue;
    do {
        if (p->state == PROC_SLEEPING &&
            now >= p->sleep_until) {
            p->state = PROC_READY;
            /* Clamp the waking process's vruntime to at least min_vruntime so
             * it does not immediately starve everyone else. */
            if (p->vruntime < min_vr)
                p->vruntime = min_vr;
        } else if (p->state == PROC_WAITING &&
                   p->futex_wait_addr &&
                   p->futex_wait_deadline &&
                   now >= p->futex_wait_deadline) {
            p->state = PROC_READY;
            p->futex_wait_addr = 0;
            p->futex_wait_deadline = 0;
            p->futex_wait_result = -(int32_t)BLUEY_ETIMEDOUT;
            p->saved_regs.eax = (uint32_t)(-(int32_t)BLUEY_ETIMEDOUT);
        }
        p = p->sched_next;
    } while (p != run_queue);

    // Trap-frame switching happens in scheduler_handle_trap(), which only
    // switches on user-mode return paths.
}

process_t *scheduler_current(void) { return sched_current; }
void scheduler_set_current(process_t *p) { sched_current = p; }

void scheduler_handle_trap(registers_t *regs, int rotate) {
    process_t *current = process_current();
    process_t *next;
    int user_trap;

    if (!regs) return;

    user_trap = current &&
                (current->flags & PROC_FLAG_USER_MODE) &&
                ((regs->cs & 0x3u) == 0x3u);

    /*
     * Allow scheduling from the idle fallback (current == NULL and
     * sched_in_idle_fallback == 1).  When all user processes were sleeping,
     * scheduler_prepare_fallback_frame() set current to NULL and redirected
     * execution to scheduler_idle_fallback.  The next timer tick calls
     * scheduler_tick() which wakes any process whose sleep_until has elapsed.
     * Without this exception the early-return would prevent those freshly-woken
     * processes from ever being dispatched.
     *
     * The flag check prevents accidentally hijacking kernel bootstrap code that
     * runs with current==NULL before any user process is created.
     */
    if (!user_trap && !(current == NULL && sched_in_idle_fallback)) {
        return;
    }

    if (current) {
        /* Update CPU accounting and CFS vruntime for the outgoing process. */
        uint32_t now = timer_get_ticks();
        if (current->state == PROC_RUNNING) {
            if (now >= current->cpu_last_tick) {
                uint32_t delta = now - current->cpu_last_tick;
                current->cpu_ticks += delta;
                /* vruntime += delta * NICE_0_WEIGHT / task_weight
                 * Compute in 32-bit to avoid __udivdi3 (unavailable in
                 * freestanding kernel).  delta is small (1-20 ticks), so
                 * delta * NICE_0_WEIGHT fits comfortably in uint32_t. */
                uint32_t delta_vr = (delta * CFS_NICE0_WEIGHT) /
                                    cfs_get_weight(current);
                current->vruntime += (uint64_t)delta_vr;
            }
            current->state = PROC_READY;
        }
        current->saved_regs = *regs;
        current->eip = regs->eip;
        current->esp = regs->useresp;
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
        if (next->tls_base) frame.gs = GDT_TLS_SEL;
        else if (!frame.gs) frame.gs = GDT_USER_DATA;
        if (signal_dispatch_pending(next, &frame) != 0) {
            next = scheduler_pick_next_user(next, 1);
            continue;
        }

        /* Sanitize EFLAGS: clear TF (bit 8) and ensure IF (bit 9) is set. */
        frame.eflags = (frame.eflags & ~0x100u) | 0x200u;

        /* Record CPU accounting: mark this process as running and remember
         * the tick when it started so we can accumulate on subsequent switch-out. */
        next->saved_regs = frame;
        next->state = PROC_RUNNING;
        sched_current = next;
        process_set_current(next);
        /* start accounting from current tick */
        next->cpu_last_tick = timer_get_ticks();
        sched_in_idle_fallback = 0;   /* leaving idle fallback, if we were in it */
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

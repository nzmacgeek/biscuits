// BlueyOS Round-Robin Scheduler
// "Bandit's Homework Scheduler - fair turns for everyone!"
// Episode ref: "Takeaway" - managing everything at once, somehow
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "scheduler.h"
#include "process.h"
#include "timer.h"

static process_t *run_queue   = NULL;   // circular linked list of runnable processes
static process_t *sched_current = NULL;

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
        p->next   = p;  // circular list
    } else {
        // Insert at end: find tail (node whose next = run_queue)
        process_t *tail = run_queue;
        while (tail->next != run_queue) tail = tail->next;
        tail->next = p;
        p->next    = run_queue;
    }
}

void scheduler_remove(uint32_t pid) {
    if (!run_queue) return;
    // Find and unlink the process with given pid
    process_t *prev = run_queue;
    process_t *cur  = run_queue->next;
    // If only one element
    if (run_queue->pid == pid) {
        if (run_queue->next == run_queue) { run_queue = NULL; return; }
        // Find last node
        process_t *tail = run_queue;
        while (tail->next != run_queue) tail = tail->next;
        tail->next = run_queue->next;
        run_queue  = run_queue->next;
        return;
    }
    while (cur != run_queue) {
        if (cur->pid == pid) { prev->next = cur->next; return; }
        prev = cur; cur = cur->next;
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
        p = p->next;
    } while (p != run_queue);

    // Find next READY process
    if (!sched_current) {
        sched_current = run_queue;
    } else {
        sched_current = sched_current->next;
    }

    // Skip non-READY processes
    int tries = 0;
    while (sched_current->state != PROC_READY && tries++ < 64) {
        sched_current = sched_current->next;
    }

    if (sched_current->state == PROC_READY) {
        sched_current->state = PROC_RUNNING;
        process_set_current(sched_current);
    }
    // If nothing is runnable, the idle HLT loop in kernel_main handles it
}

process_t *scheduler_current(void) { return sched_current; }

void scheduler_yield(void) {
    // Trigger a software timer tick by calling scheduler_tick directly
    // In a real OS this would be a context switch via task switching or iret
    scheduler_tick();
}

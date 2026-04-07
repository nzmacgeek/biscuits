#pragma once
// BlueyOS Round-Robin Scheduler
// "Bandit's Homework Scheduler - fair turns for everyone!"
// Episode ref: "Takeaway" - Bandit manages everything at once (barely)
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "process.h"

void       scheduler_init(void);
void       scheduler_tick(void);          // called from IRQ0 (timer)
void       scheduler_add(process_t *p);
void       scheduler_remove(uint32_t pid);
process_t *scheduler_current(void);
void       scheduler_set_current(process_t *p);
void       scheduler_handle_trap(registers_t *regs, int rotate);
void       scheduler_yield(void);         // voluntarily give up the CPU
void       process_set_current(process_t *p); // set current process pointer

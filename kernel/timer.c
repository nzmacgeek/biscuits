// BlueyOS Timer - "Bingo's Tick Tock Timer" - IRQ0 / PIT
// Episode ref: "Baby Race" - counting every tick like a milestone
#include "../include/types.h"
#include "../include/ports.h"
#include "../lib/stdio.h"
#include "idt.h"
#include "irq.h"
#include "timer.h"
#include "scheduler.h"

static volatile uint32_t ticks = 0;
static uint32_t timer_freq = 0;

static void timer_callback(registers_t *regs) {
    (void)regs;
    ticks++;
    scheduler_tick();
}

void timer_init(uint32_t freq) {
    timer_freq = freq;
    uint32_t divisor = 1193180 / freq;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
    irq_install_handler(0, timer_callback);
    kprintf("%s\n", "[TMR]  Bingo's Tick Tock Timer is ticking!");
}

uint32_t timer_get_ticks(void) { return ticks; }

void timer_sleep(uint32_t ms) {
    uint32_t target = ticks + (ms * timer_freq / 1000);
    while (ticks < target) __asm__ volatile ("hlt");
}

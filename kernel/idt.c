// BlueyOS IDT - "Chilli configured the Interrupts - she's got it sorted!"
#include "../include/types.h"
#include "../lib/stdio.h"
#include "idt.h"
#include "isr.h"
#include "irq.h"

static idt_entry_t idt[256];
static idt_ptr_t   idt_ptr;

void idt_set_gate(uint8_t n, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[n].base_lo = base & 0xFFFF;
    idt[n].base_hi = (base >> 16) & 0xFFFF;
    idt[n].sel     = sel;
    idt[n].zero    = 0;
    idt[n].flags   = flags;
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt) - 1;
    idt_ptr.base  = (uint32_t)&idt;
    __builtin_memset(&idt, 0, sizeof(idt));
    isr_init();
    irq_init();
    idt_flush(&idt_ptr);
    kprintf("%s\n", "[IDT]  Chilli configured the Interrupts - she's got it sorted!");
}

// BlueyOS ISR - CPU exception handlers with Bluey flair
// Episode ref: "The Creek" - sometimes you fall in, that's a fault!
#include "../include/types.h"
#include "../lib/stdio.h"
#include "idt.h"
#include "isr.h"

static const char *exception_msgs[] = {
    "Division by Zero (Bingo tripped!)",
    "Debug (Bandit's debugging session)",
    "Non-Maskable Interrupt",
    "Breakpoint (Bluey set a trap!)",
    "Overflow (too many snacks)",
    "Bound Range Exceeded (out of the backyard!)",
    "Invalid Opcode (that's not how you play!)",
    "Device Not Available",
    "Double Fault (Bluey AND Bingo both tripped!)",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present (Bandit lost the map!)",
    "Stack-Segment Fault (the cubby house fell!)",
    "General Protection Fault (No rules, no fun!)",
    "Page Fault (Bandit forgot to map the page!)",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating-Point Exception",
    "Virtualisation Exception",
    "Control Protection Exception",
    "Reserved","Reserved","Reserved","Reserved","Reserved","Reserved",
    "Hypervisor Injection","VMM Communication","Security Exception","Reserved"
};

void isr_init(void) {
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);
}

void isr_handler(registers_t regs) {
    kprintf("\n\n*** Oh no! [Bandit voice]: KERNEL PANIC! ***\n");
    kprintf("Exception %d: %s\n", regs.int_no,
        (regs.int_no < 32) ? exception_msgs[regs.int_no] : "Unknown");
    kprintf("EIP=0x%x  CS=0x%x  EFLAGS=0x%x\n", regs.eip, regs.cs, regs.eflags);
    kprintf("EAX=0x%x  EBX=0x%x  ECX=0x%x  EDX=0x%x\n", regs.eax, regs.ebx, regs.ecx, regs.edx);
    kprintf("Error code: 0x%x\n", regs.err_code);
    kprintf("System halted. Time to pack up and go home.\n");
    __asm__ volatile ("cli; hlt");
    for(;;);
}

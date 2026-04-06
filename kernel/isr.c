// BlueyOS ISR - CPU exception handlers with Bluey flair
// Episode ref: "The Creek" - sometimes you fall in, that's a fault!
#include "../include/types.h"
#include "../drivers/vga.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "idt.h"
#include "isr.h"
#include "process.h"
#include "syslog.h"

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

static const char *isr_fault_class(uint32_t int_no) {
    switch (int_no) {
        case 2:
        case 8:
        case 18:
            return "PANIC";
        default:
            return "OOPS";
    }
}

static const char *isr_fault_phrase(const char *fault_class) {
    return (strcmp(fault_class, "PANIC") == 0)
        ? "Oh biscuits!"
        : "Righto, that's not ideal.";
}

static uint32_t isr_read_cr2(void) {
    uint32_t value = 0;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(value));
    return value;
}

static void isr_dump_page_fault(uint32_t err_code) {
    uint32_t cr2 = isr_read_cr2();

    kprintf("Fault addr : 0x%x\n", cr2);
    kprintf("PF decode  : %s %s %s %s %s\n",
            (err_code & 0x1u) ? "protection" : "not-present",
            (err_code & 0x2u) ? "write" : "read",
            (err_code & 0x4u) ? "user" : "kernel",
            (err_code & 0x8u) ? "reserved-bit" : "normal-bits",
            (err_code & 0x10u) ? "instruction-fetch" : "data-access");
}

static void isr_dump_process_context(void) {
    process_t *process = process_current();

    if (!process) {
        kprintf("Process    : none\n");
        return;
    }

    kprintf("Process    : pid=%u uid=%u name=%s\n",
            process->pid, process->uid, process->name);
}

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

void isr_handler(registers_t *regs) {
    const char *fault_class;
    const char *fault_msg;
    uint32_t cpl;

    if (!regs) return;

    fault_class = isr_fault_class(regs->int_no);
    fault_msg = (regs->int_no < 32) ? exception_msgs[regs->int_no] : "Unknown";
    cpl = regs->cs & 0x3u;

    /* For user-mode page faults, try on-demand handling first. */
    if (regs->int_no == 14 && (regs->err_code & 0x4u)) {
        extern void page_fault_handler(registers_t *regs);
        page_fault_handler(regs);
        /* If page_fault_handler returns, the page was mapped; iret retries
         * the faulting instruction.  If it does not return (sti+hlt path),
         * the dead process will be context-switched away by the timer IRQ. */
        return;
    }

    __asm__ volatile ("cli");
    vga_set_color(VGA_WHITE, strcmp(fault_class, "PANIC") == 0 ? VGA_RED : VGA_BLUE);
    kprintf("\n\n*** %s: %s ***\n", fault_class, isr_fault_phrase(fault_class));
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("Trap       : #%u %s\n", regs->int_no, fault_msg);
    kprintf("Privilege  : %s mode (CPL=%u)\n", cpl ? "user" : "kernel", cpl);
    isr_dump_process_context();
    kprintf("EIP/CS     : 0x%x / 0x%x\n", regs->eip, regs->cs);
    kprintf("EFLAGS     : 0x%x\n", regs->eflags);
    kprintf("ESP/EBP    : 0x%x / 0x%x\n", regs->esp, regs->ebp);
    if (cpl) kprintf("UserESP    : 0x%x\n", regs->useresp);
    kprintf("EAX/EBX    : 0x%x / 0x%x\n", regs->eax, regs->ebx);
    kprintf("ECX/EDX    : 0x%x / 0x%x\n", regs->ecx, regs->edx);
    kprintf("ESI/EDI    : 0x%x / 0x%x\n", regs->esi, regs->edi);
    kprintf("Error code : 0x%x\n", regs->err_code);
    if (regs->int_no == 14) {
        isr_dump_page_fault(regs->err_code);
    }
    syslog_crit("TRAP", "%s #%u eip=0x%x err=0x%x proc=%u", fault_class,
                regs->int_no, regs->eip, regs->err_code,
                process_current() ? process_current()->pid : 0);
    kprintf("System halting. Capture this screen and check dmesg after reboot.\n");
    __asm__ volatile ("cli; hlt");
    for(;;);
}

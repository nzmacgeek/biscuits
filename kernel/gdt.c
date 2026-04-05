// BlueyOS GDT - "Bandit set up the Descriptor Table - like building a cubby house!"
// Episode ref: "Hammerbarn" - Bandit loves a good building project
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "gdt.h"

#define GDT_ENTRIES 7
static gdt_entry_t gdt[GDT_ENTRIES];
static gdt_ptr_t   gdt_ptr;
static tss_entry_t tss;

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low  = base & 0xFFFF;
    gdt[i].base_mid  = (base >> 16) & 0xFF;
    gdt[i].base_hi   = (base >> 24) & 0xFF;
    gdt[i].limit_low = limit & 0xFFFF;
    gdt[i].gran      = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access    = access;
}

static void tss_init(uint32_t idx, uint16_t ss0, uint32_t esp0) {
    uint32_t base  = (uint32_t)&tss;
    uint32_t limit = sizeof(tss_entry_t) - 1;  // GDT limit = size - 1, NOT an end address
    // Access byte 0x89: Present=1, DPL=0 (kernel), S=0 (system), Type=9 (32-bit TSS available)
    gdt_set(idx, base, limit, 0x89, 0x00);
    memset(&tss, 0, sizeof(tss));
    tss.ss0  = ss0;
    tss.esp0 = esp0;
    tss.cs   = GDT_KERNEL_CODE | 0x3;
    tss.ss = tss.ds = tss.es = tss.fs = tss.gs = GDT_KERNEL_DATA | 0x3;
    tss.iomap_base = sizeof(tss_entry_t);
}

void tss_set_kernel_stack(uint32_t stack) { tss.esp0 = stack; }

void gdt_set_tls_base(uint32_t base) {
    gdt_set(6, base, 0xFFFFF, 0xF2, 0xCF);
}

void gdt_init(void) {
    gdt_set(0, 0, 0, 0, 0);                        // null
    gdt_set(1, 0, 0xFFFFFFFF, 0x9A, 0xCF);         // ring0 code
    gdt_set(2, 0, 0xFFFFFFFF, 0x92, 0xCF);         // ring0 data
    gdt_set(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);         // ring3 code
    gdt_set(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);         // ring3 data
    tss_init(5, GDT_KERNEL_DATA, 0);
    gdt_set(6, 0, 0xFFFFF, 0xF2, 0xCF);            // ring3 TLS (base updated per-process)
    gdt_ptr.limit = sizeof(gdt) - 1;
    gdt_ptr.base  = (uint32_t)&gdt;
    gdt_flush(&gdt_ptr);
    tss_flush();
    kprintf("%s\n", "[GDT]  Bandit set up the Descriptor Table - like building a cubby house!");
}

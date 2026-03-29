#pragma once
#include "../include/types.h"
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x1B
#define GDT_USER_DATA   0x23
#define GDT_TSS_SEL     0x28
typedef struct { uint16_t limit_low,base_low; uint8_t base_mid,access,gran,base_hi; } __attribute__((packed)) gdt_entry_t;
typedef struct { uint16_t limit; uint32_t base; } __attribute__((packed)) gdt_ptr_t;
typedef struct {
    uint32_t prev_tss,esp0,ss0,esp1,ss1,esp2,ss2,cr3,eip,eflags;
    uint32_t eax,ecx,edx,ebx,esp,ebp,esi,edi,es,cs,ss,ds,fs,gs,ldt;
    uint16_t trap,iomap_base;
} __attribute__((packed)) tss_entry_t;
void gdt_init(void);
void gdt_flush(gdt_ptr_t *p);
void tss_flush(void);
void tss_set_kernel_stack(uint32_t stack);

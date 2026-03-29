#pragma once
#include "../include/types.h"
typedef struct { uint16_t base_lo,sel; uint8_t zero,flags; uint16_t base_hi; } __attribute__((packed)) idt_entry_t;
typedef struct { uint16_t limit; uint32_t base; } __attribute__((packed)) idt_ptr_t;
typedef struct {
    uint32_t ds;
    uint32_t edi,esi,ebp,esp,ebx,edx,ecx,eax;
    uint32_t int_no,err_code;
    uint32_t eip,cs,eflags,useresp,ss;
} registers_t;
void idt_init(void);
void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);
void idt_flush(idt_ptr_t *p);

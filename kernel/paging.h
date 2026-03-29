#pragma once
// BlueyOS Paging - Virtual Memory Management
// "Bandit mapped all the rooms in the house!" - Bluey
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "idt.h"

// Page flags
#define PAGE_PRESENT   0x001   // page is present in memory
#define PAGE_WRITABLE  0x002   // page is writable
#define PAGE_USER      0x004   // user-mode accessible (ring3 - Bluey's domain)
#define PAGE_SIZE      4096    // 4 KiB pages

void     paging_init(void);
void     paging_map(uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t paging_virt_to_phys(uint32_t virt);

// Physical frame allocator
uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t phys);
void     pmm_get_stats(uint32_t *total_frames, uint32_t *used_frames, uint32_t *free_frames);

// Called from isr.c as page fault handler (ISR 14)
void page_fault_handler(registers_t regs);

// ASM stub: load CR3 and set CR0 bit 31
extern void paging_enable(uint32_t page_dir_phys);

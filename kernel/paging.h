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
void     paging_map_in_directory(uint32_t page_dir_phys, uint32_t virt, uint32_t phys, uint32_t flags);
uint32_t paging_virt_to_phys(uint32_t virt);
uint32_t paging_create_address_space(void);
uint32_t paging_clone_address_space(uint32_t src_page_dir_phys);
void     paging_destroy_address_space(uint32_t page_dir_phys);
uint32_t paging_current_directory(void);
void     paging_switch_directory(uint32_t page_dir_phys);

// Physical frame allocator
uint32_t pmm_alloc_frame(void);
void     pmm_free_frame(uint32_t phys);
uint32_t pmm_total_frames(void);
uint32_t pmm_used_frames(void);

// Called from isr.c as page fault handler (ISR 14)
void page_fault_handler(registers_t *regs);

// ASM stub: load CR3 and set CR0 bit 31
extern void paging_enable(uint32_t page_dir_phys);

// Unmap a single virtual page in a page directory and free its frame.
// Returns 0 on success, -1 on failure or if page not present.
int      paging_unmap_in_directory(uint32_t page_dir_phys, uint32_t virt);

// Change the flags for an existing mapped page in the given page directory.
// `flags` should contain PAGE_WRITABLE / PAGE_USER as desired; PAGE_PRESENT
// is preserved by the implementation. Returns 0 on success, -1 on failure.
int      paging_set_page_flags_in_directory(uint32_t page_dir_phys, uint32_t virt, uint32_t flags);

// Invalidate a single TLB entry for the given virtual address in the current
// address space.  Much cheaper than a full CR3 reload for point mappings.
static inline void paging_invlpg(uint32_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

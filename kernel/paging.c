// BlueyOS Paging - Virtual Memory Management
// "Bandit mapped all the rooms in the house!" - Bluey
// Episode ref: "Flat Pack" - building a whole world from scratch
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "paging.h"
#include "kheap.h"
#include "isr.h"

// Physical memory manager: bitmap of frames (each bit = one 4KB page)
// We support up to 128MB (32768 frames)
#define PMM_FRAMES  32768
#define PMM_WORDS   (PMM_FRAMES / 32)

static uint32_t pmm_bitmap[PMM_WORDS];

static void pmm_set(uint32_t frame)   { pmm_bitmap[frame/32] |=  (1u << (frame%32)); }
static void pmm_clear(uint32_t frame) { pmm_bitmap[frame/32] &= ~(1u << (frame%32)); }
static int  pmm_test(uint32_t frame)  { return (pmm_bitmap[frame/32] >> (frame%32)) & 1; }

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 0; i < PMM_FRAMES; i++) {
        if (!pmm_test(i)) { pmm_set(i); return i * PAGE_SIZE; }
    }
    return 0; // out of memory - "Bandit, we need more room!"
}

void pmm_free_frame(uint32_t phys) {
    pmm_clear(phys / PAGE_SIZE);
}

uint32_t pmm_total_frames(void) {
    return PMM_FRAMES;
}

uint32_t pmm_used_frames(void) {
    uint32_t used = 0;
    for (uint32_t i = 0; i < PMM_FRAMES; i++) {
        if (pmm_test(i)) used++;
    }
    return used;
}

// ---------------------------------------------------------------------------
// Page directory and tables
// We keep one kernel page directory, identity-mapped for the first 4MB.
// ---------------------------------------------------------------------------
static uint32_t kernel_page_dir[1024] __attribute__((aligned(4096)));
static uint32_t first_page_table[1024] __attribute__((aligned(4096)));

// Page fault handler - registered as ISR 14
// "Bandit forgot to map the page!" - from isr.c exception messages
void page_fault_handler(registers_t regs) {
    uint32_t faulting_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_addr));

    kprintf("\n[PGF]  Page Fault! Faulting address: 0x%x\n", faulting_addr);
    kprintf("[PGF]  Error code: 0x%x (", regs.err_code);
    if (regs.err_code & 0x1) kprintf("protection violation"); else kprintf("not present");
    if (regs.err_code & 0x2) kprintf(", write");  else kprintf(", read");
    if (regs.err_code & 0x4) kprintf(", user");   else kprintf(", supervisor");
    kprintf(")\n[PGF]  EIP: 0x%x\n", regs.eip);
    kprintf("[PGF]  Bandit forgot to map that page! Halting.\n");
    __asm__ volatile("cli; hlt");
    for(;;);
}

void paging_init(void) {
    // Zero the page directory
    memset(kernel_page_dir, 0, sizeof(kernel_page_dir));

    // Identity-map first 4MB (1024 pages × 4KB) using first_page_table
    for (uint32_t i = 0; i < 1024; i++) {
        first_page_table[i] = (i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITABLE;
        pmm_set(i); // mark as used
    }

    // Install the first page table into the page directory
    kernel_page_dir[0] = (uint32_t)first_page_table | PAGE_PRESENT | PAGE_WRITABLE;

    // Register page fault handler (ISR 14)
    extern void isr14(void);
    extern void idt_set_gate(uint8_t, uint32_t, uint16_t, uint8_t);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);

    // Enable paging
    paging_enable((uint32_t)kernel_page_dir);
    kprintf("%s\n", "[PGE]  Paging enabled - Bandit mapped all the rooms!");
}

void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    uint32_t *page_table;
    if (!(kernel_page_dir[pd_idx] & PAGE_PRESENT)) {
        // Allocate a new page table
        uint32_t new_pt_phys = pmm_alloc_frame();
        if (!new_pt_phys) { kprintf("[PGE] ERROR: out of frames!\n"); return; }
        page_table = (uint32_t*)new_pt_phys;
        memset(page_table, 0, PAGE_SIZE);
        kernel_page_dir[pd_idx] = new_pt_phys | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
    } else {
        page_table = (uint32_t*)(kernel_page_dir[pd_idx] & ~0xFFF);
    }
    page_table[pt_idx] = (phys & ~0xFFF) | flags | PAGE_PRESENT;
}

uint32_t paging_virt_to_phys(uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    if (!(kernel_page_dir[pd_idx] & PAGE_PRESENT)) return 0;
    uint32_t *pt = (uint32_t*)(kernel_page_dir[pd_idx] & ~0xFFF);
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFF) + (virt & 0xFFF);
}

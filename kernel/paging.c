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
#include "process.h"
#include "signal.h"
#include "gdt.h"
#include "syslog.h"

// Physical memory manager: bitmap of frames (each bit = one 4KB page)
// We support up to 128MB (32768 frames)
#define PMM_FRAMES  32768
#define PMM_WORDS   (PMM_FRAMES / 32)

static uint32_t pmm_bitmap[PMM_WORDS];

static void pmm_set(uint32_t frame)   { pmm_bitmap[frame/32] |=  (1u << (frame%32)); }
static void pmm_clear(uint32_t frame) { pmm_bitmap[frame/32] &= ~(1u << (frame%32)); }
static int  pmm_test(uint32_t frame)  { return (pmm_bitmap[frame/32] >> (frame%32)) & 1; }

uint32_t pmm_alloc_frame(void) {
    for (uint32_t i = 1; i < PMM_FRAMES; i++) {
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
static uint32_t *current_page_dir = kernel_page_dir;

static void paging_refresh_active_directory(uint32_t *page_dir) {
    if (!page_dir || page_dir != current_page_dir) return;
    paging_enable((uint32_t)page_dir);
}

static uint32_t *paging_directory_ptr(uint32_t page_dir_phys) {
    return (uint32_t*)(uintptr_t)page_dir_phys;
}

static uint32_t *paging_ensure_page_table(uint32_t *page_dir, uint32_t pd_idx, uint32_t flags) {
    uint32_t *page_table;
    if (!(page_dir[pd_idx] & PAGE_PRESENT)) {
        page_table = (uint32_t*)kheap_alloc(PAGE_SIZE, 1);
        if (!page_table) {
            kprintf("[PGE] ERROR: out of memory for page table!\n");
            return NULL;
        }
        memset(page_table, 0, PAGE_SIZE);
        page_dir[pd_idx] = (uint32_t)page_table | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        return page_table;
    }

    /* If the PDE already exists it may point to a shared kernel page table
     * (copied into a new address space via memcpy). In that case we must
     * allocate a private page table for this address space before modifying
     * entries or toggling the USER bit, to avoid changing the kernel's
     * mappings. */
    uint32_t existing_pt = page_dir[pd_idx] & ~0xFFFu;
    uint32_t kernel_pt = kernel_page_dir[pd_idx] & ~0xFFFu;
    if (existing_pt == kernel_pt) {
        /* Duplicate kernel page table for this address space */
        page_table = (uint32_t*)kheap_alloc(PAGE_SIZE, 1);
        if (!page_table) {
            kprintf("[PGE] ERROR: out of memory duplicating page table!\n");
            return NULL;
        }
        memcpy(page_table, (void*)(uintptr_t)existing_pt, PAGE_SIZE);
        page_dir[pd_idx] = (uint32_t)page_table | PAGE_PRESENT | PAGE_WRITABLE | (flags & PAGE_USER);
        return page_table;
    }

    return (uint32_t*)(uintptr_t)existing_pt;
}

// Page fault handler - registered as ISR 14
// "Bandit forgot to map the page!" - from isr.c exception messages
void page_fault_handler(registers_t *regs) {
    uint32_t faulting_addr;
    __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_addr));

    if (!regs) return;

    /* Attempt grow-on-demand for user stack faults BEFORE printing anything.
     * If the fault is a normal stack-growth event we return silently so the
     * kernel log stays clean. */
    process_t *proc = process_current();
    uint32_t page_aligned = faulting_addr & ~0xFFFu;

    if (proc && (regs->err_code & 0x4) && proc->page_dir &&
        !(regs->err_code & 0x1)) {  /* not-present fault only */
        /* Stack grows downward. Allow on-demand mapping for pages within the
         * reserved stack region, but do not map the bottom-most guard page. */
        uint32_t stack_base = proc->user_stack_base;
        uint32_t stack_top  = proc->user_stack_top;
        if (faulting_addr >= stack_base + PAGE_SIZE && faulting_addr < stack_top) {
            uint32_t offset_from_top = stack_top - page_aligned;
            if (offset_from_top <= proc->rlimit_stack_cur) {
                uint32_t phys = pmm_alloc_frame();
                if (!phys) {
                    /* Fall through to fatal handling below. */
                    goto fatal_fault;
                }
                paging_map_in_directory(proc->page_dir, page_aligned, phys,
                                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
                memset((void*)page_aligned, 0, PAGE_SIZE);
                if (proc->tls_base) regs->gs = GDT_TLS_SEL;
                else if (!regs->gs) regs->gs = GDT_USER_DATA;
                return; /* silently handled: no [PGF] spam */
            }
        }
    }

fatal_fault:
    /* Only now print the fault details — this is a real problem. */
    kprintf("\n[PGF]  Page Fault! pid=%u '%s' faulting_addr=0x%08x EIP=0x%08x\n",
            proc ? proc->pid : 0u,
            proc ? proc->name : "?",
            faulting_addr, regs->eip);
    kprintf("[PGF]  Error code: 0x%x (", regs->err_code);
    if (regs->err_code & 0x1) kprintf("protection violation"); else kprintf("not present");
    if (regs->err_code & 0x2) kprintf(", write");  else kprintf(", read");
    if (regs->err_code & 0x4) kprintf(", user");   else kprintf(", supervisor");
    kprintf(")\n");

    /* Always dump registers for fatal user faults so we can diagnose the
     * root cause (corrupted EIP, stack overflow, etc.) without needing to
     * increase the verbose level. */
    if (regs->err_code & 0x4) {
        kprintf("[PGF]  EAX=0x%08x EBX=0x%08x ECX=0x%08x EDX=0x%08x\n",
                regs->eax, regs->ebx, regs->ecx, regs->edx);
        kprintf("[PGF]  ESI=0x%08x EDI=0x%08x EBP=0x%08x ESP=0x%08x\n",
                regs->esi, regs->edi, regs->ebp, regs->useresp);
        kprintf("[PGF]  CS=0x%04x DS=0x%04x GS=0x%04x EFLAGS=0x%08x\n",
                regs->cs, regs->ds, regs->gs, regs->eflags);
        if (proc)
            kprintf("[PGF]  stack_base=0x%08x stack_top=0x%08x rlimit=0x%08x vfork=%u\n",
                    proc->user_stack_base, proc->user_stack_top,
                    proc->rlimit_stack_cur,
                    (proc->flags & PROC_FLAG_VFORK_SHARED_VM) ? 1u : 0u);
    }

    /* Supervisor-mode fault (should never happen after kernel stabilises). */
    if (!(regs->err_code & 0x4)) {
        kprintf("[PGF]  Supervisor fault — halting.\n");
        __asm__ volatile("cli; hlt");
        for(;;);
    }

    /* User-mode protectio-violation in kernel-mapped page: definitely fatal. */
    if (regs->err_code & 0x1) {
        kprintf("[PGF]  Protection violation in user process pid=%u — SIGSEGV\n",
                proc ? proc->pid : 0u);
        if (proc) process_mark_exited(proc, 128 + SIGSEGV);
        __asm__ volatile("sti");
        for(;;) __asm__ volatile("hlt");
    }

    /* Not-present user fault outside the allowed grow region. */
    if (proc) {
        kprintf("[PGF]  User not-present fault outside stack region pid=%u — SIGSEGV\n",
                proc->pid);
        if (!proc->page_dir) {
            kprintf("[PGF]  No page directory! Halting.\n");
            __asm__ volatile("cli; hlt");
            for(;;);
        }
        /* Try to handle PMM OOM reported from the growth path above. */
        kprintf("[PGF]  Out of physical frames (pid=%u) — SIGSEGV\n", proc->pid);
        process_mark_exited(proc, 128 + SIGSEGV);
        __asm__ volatile("sti");
        for(;;) __asm__ volatile("hlt");
    }

    kprintf("[PGF]  Unhandled supervisor not-present fault — halting.\n");
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
    current_page_dir = kernel_page_dir;
    kprintf("%s\n", "[PGE]  Paging enabled - Bandit mapped all the rooms!");
}

void paging_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    paging_map_in_directory((uint32_t)current_page_dir, virt, phys, flags);
}

void paging_map_in_directory(uint32_t page_dir_phys, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t *page_dir = paging_directory_ptr(page_dir_phys);
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    uint32_t *page_table = paging_ensure_page_table(page_dir, pd_idx, flags);
    if (!page_table) return;

    uint32_t old_entry = page_table[pt_idx];
    uint32_t new_entry = (phys & ~0xFFF) | flags | PAGE_PRESENT;

    /* If nothing changes, avoid both the write and any TLB activity. */
    if (old_entry == new_entry) {
        return;
    }

    page_table[pt_idx] = new_entry;

    /* Use invlpg to shoot down only this TLB entry when the mapping is in
     * the currently-active address space.  A full CR3 reload is unnecessary
     * and flushes all global entries. */
    if (page_dir == current_page_dir) {
        paging_invlpg(virt);
    }
}

uint32_t paging_virt_to_phys(uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    if (!(current_page_dir[pd_idx] & PAGE_PRESENT)) return 0;
    uint32_t *pt = (uint32_t*)(uintptr_t)(current_page_dir[pd_idx] & ~0xFFFu);
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & ~0xFFF) + (virt & 0xFFF);
}

int paging_unmap_in_directory(uint32_t page_dir_phys, uint32_t virt) {
    uint32_t *page_dir = paging_directory_ptr(page_dir_phys);
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    if (!(page_dir[pd_idx] & PAGE_PRESENT)) return -1;
    uint32_t *pt = (uint32_t*)(uintptr_t)(page_dir[pd_idx] & ~0xFFFu);
    if (!(pt[pt_idx] & PAGE_PRESENT)) return -1;

    pmm_free_frame(pt[pt_idx] & ~0xFFFu);
    pt[pt_idx] = 0;

    if (page_dir == current_page_dir) paging_refresh_active_directory(page_dir);
    return 0;
}

int paging_set_page_flags_in_directory(uint32_t page_dir_phys, uint32_t virt, uint32_t flags) {
    uint32_t *page_dir = paging_directory_ptr(page_dir_phys);
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    if (!(page_dir[pd_idx] & PAGE_PRESENT)) return -1;
    uint32_t *pt = (uint32_t*)(uintptr_t)(page_dir[pd_idx] & ~0xFFFu);
    if (!(pt[pt_idx] & PAGE_PRESENT)) return -1;

    uint32_t phys = pt[pt_idx] & ~0xFFFu;
    uint32_t new_entry = phys | (flags & 0xFFFu) | PAGE_PRESENT;
    pt[pt_idx] = new_entry;

    if (page_dir == current_page_dir) paging_refresh_active_directory(page_dir);
    return 0;
}

uint32_t paging_create_address_space(void) {
    uint32_t *page_dir = (uint32_t*)kheap_alloc(PAGE_SIZE, 1);
    if (!page_dir) return 0;

    memset(page_dir, 0, PAGE_SIZE);
    memcpy(page_dir, kernel_page_dir, PAGE_SIZE);
    return (uint32_t)page_dir;
}

/*
 * Physical-frame copy using two scratch window slots in first_page_table.
 *
 * All page directories share first_page_table for the 0–4 MB identity window
 * (PDE 0 is always a copy of kernel_page_dir[0], which points here).  We
 * temporarily remap entries 1022 and 1023 to reach arbitrary physical frames
 * without a CR3 switch.  Interrupts are masked for the duration so no other
 * code sees the transient mapping.
 *
 * The slots are reserved: user space is never mapped below 4 MB in this OS,
 * so nothing else modifies first_page_table[1022/1023] at runtime.
 */
#define PAGING_TMP_SRC_IDX 1022u
#define PAGING_TMP_DST_IDX 1023u

static void paging_copy_frame(uint32_t src_phys, uint32_t dst_phys)
{
    uint32_t eflags;
    uint32_t old_src = first_page_table[PAGING_TMP_SRC_IDX];
    uint32_t old_dst = first_page_table[PAGING_TMP_DST_IDX];
    void *src_va = (void*)((uintptr_t)(PAGING_TMP_SRC_IDX << 12));
    void *dst_va = (void*)((uintptr_t)(PAGING_TMP_DST_IDX << 12));

    __asm__ volatile("pushfl; pop %0; cli" : "=r"(eflags));

    first_page_table[PAGING_TMP_SRC_IDX] = (src_phys & ~0xFFFu) | PAGE_PRESENT | PAGE_WRITABLE;
    first_page_table[PAGING_TMP_DST_IDX] = (dst_phys & ~0xFFFu) | PAGE_PRESENT | PAGE_WRITABLE;
    __asm__ volatile("invlpg (%0)" :: "r"(src_va) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(dst_va) : "memory");

    memcpy(dst_va, src_va, PAGE_SIZE);

    first_page_table[PAGING_TMP_SRC_IDX] = old_src;
    first_page_table[PAGING_TMP_DST_IDX] = old_dst;
    __asm__ volatile("invlpg (%0)" :: "r"(src_va) : "memory");
    __asm__ volatile("invlpg (%0)" :: "r"(dst_va) : "memory");

    __asm__ volatile("push %0; popfl" :: "r"(eflags));
}

uint32_t paging_clone_address_space(uint32_t src_page_dir_phys) {
    uint32_t child_page_dir = paging_create_address_space();
    uint32_t *src_page_dir;
    uint32_t *dst_page_dir;

    if (!child_page_dir) return 0;

    kprintf("[PGE] paging_clone_address_space: src=0x%08x child=0x%08x\n",
            src_page_dir_phys, child_page_dir);

    src_page_dir = paging_directory_ptr(src_page_dir_phys);
    dst_page_dir = paging_directory_ptr(child_page_dir);

    for (uint32_t pd_idx = 0; pd_idx < 1024; pd_idx++) {
        uint32_t src_pde = src_page_dir[pd_idx];
        uint32_t *src_page_table;
        uint32_t *dst_page_table;

        if (!(src_pde & PAGE_PRESENT)) continue;
        if (kernel_page_dir[pd_idx] == src_pde) continue;  /* shared kernel mapping */

        src_page_table = (uint32_t*)(uintptr_t)(src_pde & ~0xFFFu);
        dst_page_table = (uint32_t*)kheap_alloc(PAGE_SIZE, 1);
        if (!dst_page_table) {
            paging_destroy_address_space(child_page_dir);
            return 0;
        }

        memset(dst_page_table, 0, PAGE_SIZE);
        dst_page_dir[pd_idx] = (uint32_t)dst_page_table | (src_pde & 0xFFFu);

        for (uint32_t pt_idx = 0; pt_idx < 1024; pt_idx++) {
            uint32_t src_pte = src_page_table[pt_idx];
            uint32_t new_phys;

            if (!(src_pte & PAGE_PRESENT)) continue;
            if (!(src_pte & PAGE_USER))    continue;  /* skip kernel-only mappings */

            new_phys = pmm_alloc_frame();
            if (!new_phys) {
                paging_destroy_address_space(child_page_dir);
                return 0;
            }

            dst_page_table[pt_idx] = new_phys | (src_pte & 0xFFFu);
            paging_copy_frame(src_pte & ~0xFFFu, new_phys);
        }
    }

    kprintf("[PGE] paging_clone_address_space: done (used_frames=%u)\n",
            pmm_used_frames());
    return child_page_dir;
}

void paging_destroy_address_space(uint32_t page_dir_phys) {
    uint32_t *page_dir;

    if (!page_dir_phys || page_dir_phys == (uint32_t)kernel_page_dir) return;

    page_dir = paging_directory_ptr(page_dir_phys);
    for (uint32_t pd_idx = 0; pd_idx < 1024; pd_idx++) {
        uint32_t pde = page_dir[pd_idx];
        uint32_t *page_table;

        if (!(pde & PAGE_PRESENT)) continue;
        if (kernel_page_dir[pd_idx] == pde) continue;

        page_table = (uint32_t*)(uintptr_t)(pde & ~0xFFFu);
        for (uint32_t pt_idx = 0; pt_idx < 1024; pt_idx++) {
            if (page_table[pt_idx] & PAGE_PRESENT) {
                pmm_free_frame(page_table[pt_idx] & ~0xFFFu);
            }
        }

        kheap_free(page_table);
        page_dir[pd_idx] = 0;
    }

    kheap_free(page_dir);
}

uint32_t paging_current_directory(void) {
    return (uint32_t)current_page_dir;
}

void paging_switch_directory(uint32_t page_dir_phys) {
    if (!page_dir_phys) return;
    current_page_dir = paging_directory_ptr(page_dir_phys);
    paging_enable(page_dir_phys);
}

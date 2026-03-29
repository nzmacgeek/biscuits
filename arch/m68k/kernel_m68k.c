// BlueyOS M68K Kernel Entry Point — Macintosh LC III
// "It was the 80s!" — Bandit Heeler
//
// This file provides the C-level entry point for the M68K port.
// It is called from arch/m68k/startup.S after BSS is zeroed and the
// stack is initialised.
//
// The LC III has no VGA text buffer — output is via the SCC serial port
// (channel A, connected to the modem port) until the DAFB frame buffer
// driver is ready.
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

#include "../../include/types.h"
#include "bootinfo.h"
#include "mac_lc3.h"
#include "../../drivers/vga.h"
#include "../../lib/stdio.h"

static uint32_t m68k_read_u32(uintptr_t addr) {
    return *(volatile uint32_t *)addr;
}

// ---------------------------------------------------------------------------
// kernel_main_m68k — C entry point for the M68K port
//
// Called from arch/m68k/startup.S with:
//   - BSS zeroed
//   - SP = top of first 512 KB
//   - IPL = 6 (VBL interrupts allowed; NMI still masked)
//
// Returns: never
// ---------------------------------------------------------------------------
void kernel_main_m68k(void) {
    const m68k_bootinfo_t *boot = m68k_bootinfo_get();

    // Step 1: Bring up the shared serial-backed console immediately.
    vga_init();
    vga_puts("\n\nBlueyOS M68K - Macintosh bootstrap\n");
    vga_puts("\"It was the 80s!\" - Bandit Heeler\n");
    vga_puts("(C) Ludo Studio Pty Ltd / BBC Studios. AI research project.\n\n");

    // Step 2: Read physical memory size from Mac low-memory globals.
    uint32_t memsize = m68k_read_u32((uintptr_t)MAC_LMG_MEMSIZE);
    if (boot->mac_memsize_mb != 0) {
        memsize = boot->mac_memsize_mb * 1024u * 1024u;
    } else if (boot->mem_chunk_size != 0) {
        memsize = boot->mem_chunk_size;
    }

    if (boot->valid) {
        kprintf("[BOOT] Mac model=%u SCC=0x%x video=0x%x %ux%ux%u stride=%u\n",
                boot->mac_model,
                boot->mac_scc_base,
                boot->mac_video_base,
                m68k_bootinfo_video_width(),
                m68k_bootinfo_video_height(),
                boot->mac_video_depth,
                boot->mac_video_row);
    } else {
        kprintf("[BOOT] No bootinfo detected, using LC-style platform defaults\n");
    }

    kprintf("[MEM]  Physical RAM: %u MB\n", memsize / (1024 * 1024));

    // Step 3: TODO — initialise VIA1 timer for kernel ticks (60 Hz VBL or
    //         T1 in free-running mode at 1 ms for a scheduler tick).

    // Step 4: TODO — initialise SCSI driver for hard drive access.

    // Step 5: TODO — mount BiscuitFS / FAT16 from SCSI disk.

    // Step 6: TODO — launch built-in shell over SCC (no VGA text mode on M68K).
    kprintf("[OK]   BlueyOS M68K stub complete. Initialising framebuffer...\n");

    // Initialise minimal DAFB driver and draw a test pattern so the
    // QEMU VNC/framebuffer shows pixels. Implemented as a best-effort
    // helper (may be emulator-dependent).
    extern void dafb_init(void);
    extern void dafb_draw_test(void);
    dafb_init();
    dafb_draw_test();

    kprintf("[OK]   Framebuffer test drawn. Halting (work in progress).\n");
    kprintf("       \"This is the best day EVER!\" - Bluey Heeler\n\n");

    // Halt — future work will loop here running the scheduler.
    __asm__ volatile("stop #0x2700");
    for (;;) {}
}

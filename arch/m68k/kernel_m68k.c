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
#include "dafb.h"
#include "mac_lc3.h"
#include "../../drivers/vga.h"
#include "../../lib/stdio.h"
#include "../../kernel/bootui.h"
#include "../../kernel/rtc.h"

#define MAC_LC3_RAM_DEFAULT (32u * 1024u * 1024u)

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
    vga_init();

    // Step 2: Read physical memory size from Mac low-memory globals.
    uint32_t memsize = m68k_read_u32((uintptr_t)MAC_LMG_MEMSIZE);
    if (boot->mac_memsize_mb != 0) {
        memsize = boot->mac_memsize_mb * 1024u * 1024u;
    } else if (boot->mem_chunk_size != 0) {
        memsize = boot->mem_chunk_size;
    } else if (memsize == 0) {
        memsize = MAC_LC3_RAM_DEFAULT;
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
    rtc_init();

    // Step 3: TODO — initialise VIA1 timer for kernel ticks (60 Hz VBL or
    //         T1 in free-running mode at 1 ms for a scheduler tick).

    // Step 4: TODO — initialise SCSI driver for hard drive access.

    // Step 5: TODO — mount BiscuitFS / FAT16 from SCSI disk.

    kprintf("[BOOT] M68K console online, bringing up framebuffer\n");
    dafb_init();
    if (dafb_console_enable()) {
        bluey_boot_show_splash("M68K", memsize / (1024 * 1024));
    }
    dafb_draw_test();

    kprintf("[OK]   BlueyOS M68K kernel path entered. Waiting for next bring-up stage.\n");

    for (;;) {
        __asm__ volatile("nop");
    }
}

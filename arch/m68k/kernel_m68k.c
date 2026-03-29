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
#include "mac_lc3.h"

// ---------------------------------------------------------------------------
// Minimal SCC serial output (channel A, polling)
// Used for early boot messages before the DAFB driver is ready.
// ---------------------------------------------------------------------------

// SCC channel A: write register 0 to select, then write data.
static void scc_write_reg(uint8_t reg, uint8_t val) {
    SCC_CHAN_A_CTRL = reg;
    mmio_barrier();
    SCC_CHAN_A_CTRL = val;
    mmio_barrier();
}

// Initialise SCC channel A for 9600,8N1 (clock source = BR generator).
// This is a minimal setup — a production driver would use the full Z8530 init.
static void scc_init(void) {
    // Reset channel A
    SCC_CHAN_A_CTRL = 0x00;
    mmio_barrier();
    SCC_CHAN_A_CTRL = 0x00;
    mmio_barrier();

    scc_write_reg(9, 0x80);  // reset channel A
    scc_write_reg(4, 0x44);  // x16 clock, 1 stop bit, no parity
    scc_write_reg(3, 0xC1);  // RX 8 bits, RX enable
    scc_write_reg(5, 0x68);  // TX 8 bits, TX enable, RTS
    scc_write_reg(11, 0x50); // clock = BR generator
    scc_write_reg(12, 0x0A); // BR low byte  (9600 @ 3.6864 MHz PCLK)
    scc_write_reg(13, 0x00); // BR high byte
    scc_write_reg(14, 0x01); // BR generator enable
}

// Poll TBE (transmit buffer empty) then send a character.
static void scc_putchar(char c) {
    uint8_t status;
    // Wait for TX buffer empty (bit 2 of RR0)
    do {
        SCC_CHAN_A_CTRL = 0x00;
        mmio_barrier();
        status = SCC_CHAN_A_CTRL;
    } while (!(status & 0x04));

    SCC_CHAN_A_DATA = (uint8_t)c;
    mmio_barrier();

    // Carriage-return after newline (serial convention)
    if (c == '\n') scc_putchar('\r');
}

static void scc_puts(const char *s) {
    while (*s) scc_putchar(*s++);
}

// ---------------------------------------------------------------------------
// Very simple decimal printer (no kprintf yet at this stage)
// ---------------------------------------------------------------------------
static void scc_put_uint32(uint32_t v) {
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    if (v == 0) { scc_putchar('0'); return; }
    while (v && i >= 0) {
        buf[i--] = (char)('0' + v % 10);
        v /= 10;
    }
    scc_puts(&buf[i + 1]);
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
    // Step 1: Early serial console so we have some output immediately.
    scc_init();
    scc_puts("\n\nBlueyOS M68K - Macintosh LC III\n");
    scc_puts("\"It was the 80s!\" - Bandit Heeler\n");
    scc_puts("(C) Ludo Studio Pty Ltd / BBC Studios. AI research project.\n\n");

    // Step 2: Read physical memory size from Mac low-memory globals.
    uint32_t memsize = *(volatile uint32_t *)MAC_LMG_MEMSIZE;
    scc_puts("[MEM]  Physical RAM: ");
    scc_put_uint32(memsize / (1024 * 1024));
    scc_puts(" MB\n");

    // Step 3: TODO — initialise VIA1 timer for kernel ticks (60 Hz VBL or
    //         T1 in free-running mode at 1 ms for a scheduler tick).

    // Step 4: TODO — initialise SCSI driver for hard drive access.

    // Step 5: TODO — mount BiscuitFS / FAT16 from SCSI disk.

    // Step 6: TODO — launch built-in shell over SCC (no VGA text mode on M68K).

    scc_puts("[OK]   BlueyOS M68K stub complete. Halting (work in progress).\n");
    scc_puts("       \"This is the best day EVER!\" - Bluey Heeler\n\n");

    // Halt — future work will loop here running the scheduler.
    __asm__ volatile("stop #0x2700");
    for (;;) {}
}

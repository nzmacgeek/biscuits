// BlueyOS PowerPC Kernel Entry Point — iMac G4 "Sunflower"
// "That's a ripper machine!" — Bandit Heeler
//
// Called from arch/ppc/startup.S after:
//   - OF device tree pointer saved
//   - BSS zeroed
//   - Stack established below 0x00600000
//   - Caches flushed
//
// All output goes through the SCC serial port (or OF client interface)
// until the ATI Radeon frame buffer driver is ready.
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

#include "../../include/types.h"
#include "imac_g4.h"

// ---------------------------------------------------------------------------
// SCC serial output — channel A (internal modem debug UART when enabled)
// Write a character via the Z85C30 compatible interface.
// ---------------------------------------------------------------------------
static void scc_write_reg(uint8_t reg, uint8_t val) {
    if (reg != 0) {
        SCC_A_CTRL = reg;
        mmio_barrier();
    }
    SCC_A_CTRL = val;
    mmio_barrier();
}

static void scc_init(void) {
    // Reset channel A
    SCC_A_CTRL = 0x00; mmio_barrier();
    SCC_A_CTRL = 0x00; mmio_barrier();

    scc_write_reg(9,  0x80);   // channel A reset
    scc_write_reg(4,  0x44);   // x16 clock, 1 stop, no parity
    scc_write_reg(3,  0xC1);   // RX 8 bits, RX enable
    scc_write_reg(5,  0x68);   // TX 8 bits, TX enable
    scc_write_reg(11, 0x50);   // BR generator as clock
    scc_write_reg(12, 0x04);   // 57600 baud @ 3.6864 MHz PCLK
    scc_write_reg(13, 0x00);
    scc_write_reg(14, 0x01);   // BR generator enable
}

static void scc_putchar(char c) {
    // Wait for TX buffer empty (RR0 bit 2)
    uint8_t rr0;
    do {
        SCC_A_CTRL = 0x00;
        mmio_barrier();
        rr0 = SCC_A_CTRL;
    } while (!(rr0 & 0x04));

    SCC_A_DATA = (uint8_t)c;
    mmio_barrier();
    if (c == '\n') scc_putchar('\r');
}

static void scc_puts(const char *s) {
    while (*s) scc_putchar(*s++);
}

static void scc_put_uint32(uint32_t v) {
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    if (v == 0) { scc_putchar('0'); return; }
    while (v && i >= 0) { buf[i--] = (char)('0' + v % 10); v /= 10; }
    scc_puts(&buf[i + 1]);
}

// ---------------------------------------------------------------------------
// PowerPC timebase read (64-bit: upper||lower)
// ---------------------------------------------------------------------------
static uint32_t ppc_timebase_low(void) {
    uint32_t v;
    __asm__ volatile("mftb %0" : "=r"(v));
    return v;
}

// Busy-wait approximately n milliseconds using the timebase.
static void ppc_delay_ms(uint32_t ms) {
    uint32_t start = ppc_timebase_low();
    uint32_t ticks = ms * (IMAC_G4_TB_FREQ_HZ / 1000);
    while ((ppc_timebase_low() - start) < ticks)
        __asm__ volatile("nop");
}

// ---------------------------------------------------------------------------
// Probe physical RAM size from the OF device tree.
// BootX passes the flat OF tree at of_tree_ptr.  We do a very minimal
// parse here — a full OF client interface call would be cleaner, but
// requires the OF services pointer that BootX may have already invalidated.
//
// Fallback: assume 128 MB.
// ---------------------------------------------------------------------------
extern uint32_t of_tree_ptr;

static uint32_t probe_ram_size(void) {
    // Minimal heuristic: walk the first 512 MB in 8 MB steps and check
    // whether writes are reflected (bus error detection not possible here
    // without an exception handler).  Return first bad address.
    // This is NOT reliable; a production kernel reads the OF memory node.
    (void)of_tree_ptr;   /* TODO: parse OF flat device tree */
    return (uint32_t)IMAC_G4_RAM_MIN;  /* safe default: 128 MB */
}

// ---------------------------------------------------------------------------
// kernel_main_ppc — C entry point for the PowerPC port
//
// Arguments:
//   of_ptr  — physical pointer to the Open Firmware flat device tree
//             (also stored in the of_tree_ptr global by startup.S)
//
// Returns: never
// ---------------------------------------------------------------------------
void kernel_main_ppc(uint32_t of_ptr) {
    (void)of_ptr;

    // Step 1: Early serial console
    scc_init();
    scc_puts("\n\nBlueyOS PowerPC - iMac G4 \"Sunflower\"\n");
    scc_puts("\"That's a ripper machine!\" - Bandit Heeler\n");
    scc_puts("(C) Ludo Studio Pty Ltd / BBC Studios. AI research project.\n\n");

    // Step 2: Read physical RAM size
    uint32_t ramsize = probe_ram_size();
    scc_puts("[MEM]  Physical RAM: ");
    scc_put_uint32(ramsize / (1024 * 1024));
    scc_puts(" MB\n");

    // Step 3: Print CPU info
    scc_puts("[CPU]  " IMAC_G4_CPU "\n");

    // Step 4: Enable the decrementer for a 1 ms scheduler tick.
    // Decrementer fires when it wraps past 0, generating interrupt 0x900.
    // We need an exception handler installed first (TODO).
    scc_puts("[TMR]  Decrementer tick = ");
    scc_put_uint32(IMAC_G4_DECR_1MS);
    scc_puts(" timebase ticks per ms (bus @ 133 MHz)\n");

    // Step 5: Small self-test — verify the timebase is ticking
    uint32_t tb0 = ppc_timebase_low();
    ppc_delay_ms(1);
    uint32_t tb1 = ppc_timebase_low();
    scc_puts("[TB]   Timebase delta (1 ms delay): ");
    scc_put_uint32(tb1 - tb0);
    scc_puts(" ticks\n");

    // Step 6: Enable Machine Check exceptions (MSR[ME] = 1)
    uint32_t msr = mfmsr();
    msr |= MSR_ME;
    mtmsr(msr);
    isync();

    // Step 7: TODO — relocate exception vectors to 0x00000000
    // Step 8: TODO — initialise KeyLargo ATA for hard drive access
    // Step 9: TODO — mount BiscuitFS / FAT16 from ATA disk
    // Step 10: TODO — ATI Radeon frame buffer console
    // Step 11: TODO — USB HID (keyboard/mouse) via OHCI
    // Step 12: TODO — launch built-in shell

    scc_puts("\n[OK]   BlueyOS PPC stub complete. Halting (work in progress).\n");
    scc_puts("       \"This is the best day EVER!\" - Bluey Heeler\n\n");

    // Halt — enable external interrupts so the debugger can break in.
    msr = mfmsr();
    msr |= MSR_EE;
    mtmsr(msr);
    for (;;) {
        __asm__ volatile("nop");
    }
}

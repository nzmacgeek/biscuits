// Minimal platform support for M68K build: console and tiny heap
#include "mac_lc3.h"
#include "../../include/types.h"

extern char kernel_end;

void vga_putchar(char c) {
    uint8_t status;
    /* Try SCC channel A first: poll TX buffer empty (bit 2 of RR0). */
    /* Note: this is a conservative, best-effort poll for early boot. */
    int tries = 1000;
    do {
        /* Touch control register to latch status */
        (void)SCC_CHAN_A_CTRL;
        asm volatile ("" ::: "memory");
        status = SCC_CHAN_A_CTRL;
        if (--tries <= 0) break;
    } while (!(status & 0x04));

    /* Write to SCC data register */
    SCC_CHAN_A_DATA = (uint8_t)c;
    asm volatile ("" ::: "memory");

    /* Also write to VIA1 ORB (parallel/printer) as a fallback/test path */
    VIA1_DDRB = 0xFF; /* make PORTB outputs */
    asm volatile ("" ::: "memory");
    VIA1_ORB = (uint8_t)c;

    if (c == '\n') vga_putchar('\r');
}

/* Minimal VGA wrappers so arch/m68k can use vga_puts / vga_set_color */
void vga_puts(const char *s) {
    if (!s) return;
    while (*s) vga_putchar(*s++);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    (void)fg; (void)bg; /* no-op on serial/parallel fallback */
}

void vga_init(void) {
    /* Ensure VIA1 PORTB is output so parallel writes are visible in emulator */
    VIA1_DDRB = 0xFF;
    asm volatile ("" ::: "memory");
}

static uintptr_t heap_ptr = 0;

void *kheap_alloc(size_t size) {
    if (!heap_ptr) heap_ptr = (uintptr_t)&kernel_end;
    uintptr_t p = (heap_ptr + 7) & ~((uintptr_t)7);
    heap_ptr = p + size;
    return (void *)p;
}

void kheap_free(void *p) { (void)p; }

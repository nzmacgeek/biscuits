// Minimal platform support for M68K build: console and tiny heap
#include "bootinfo.h"
#include "dafb.h"
#include "mac_lc3.h"
#include "../../include/types.h"
#include "../../drivers/vga.h"

extern char kernel_end;

static int console_initialised = 0;

static uintptr_t m68k_scc_base(void) {
    const m68k_bootinfo_t *boot = m68k_bootinfo_get();

    if (boot->mac_scc_base != 0) {
        return (uintptr_t)boot->mac_scc_base;
    }

    return (uintptr_t)MAC_LC3_SCC_BASE;
}

static volatile uint8_t *scc_ctrl(int channel) {
    return (volatile uint8_t *)(m68k_scc_base() + (channel ? 2 : 0));
}

static volatile uint8_t *scc_data(int channel) {
    return (volatile uint8_t *)(m68k_scc_base() + (channel ? 6 : 4));
}

static void scc_write_reg(int channel, uint8_t reg, uint8_t val) {
    volatile uint8_t *ctrl = scc_ctrl(channel);

    *ctrl = reg;
    mmio_barrier();
    *ctrl = val;
    mmio_barrier();
}

static void scc_init_channel(int channel) {
    volatile uint8_t *ctrl = scc_ctrl(channel);

    *ctrl = 0x00;
    mmio_barrier();
    *ctrl = 0x00;
    mmio_barrier();

    scc_write_reg(channel, 9, 0x80);
    scc_write_reg(channel, 4, 0x44);
    scc_write_reg(channel, 3, 0xC1);
    scc_write_reg(channel, 5, 0x68);
    scc_write_reg(channel, 11, 0x50);
    scc_write_reg(channel, 12, 0x0A);
    scc_write_reg(channel, 13, 0x00);
    scc_write_reg(channel, 14, 0x01);
}

static int scc_wait_tx_ready(int channel) {
    volatile uint8_t *ctrl = scc_ctrl(channel);
    int tries = 100000;

    while (tries-- > 0) {
        *ctrl = 0x00;
        mmio_barrier();
        if ((*ctrl & 0x04) != 0) {
            return 1;
        }
    }

    return 0;
}

static void m68k_console_init(void) {
    if (console_initialised) {
        return;
    }

    scc_init_channel(0);
    scc_init_channel(1);

    VIA1_DDRB = 0xFF;
    mmio_barrier();

    console_initialised = 1;
}

static void m68k_console_putchar_raw(char c) {
    m68k_console_init();

    if (scc_wait_tx_ready(0)) {
        *scc_data(0) = (uint8_t)c;
        mmio_barrier();
    }

    if (scc_wait_tx_ready(1)) {
        *scc_data(1) = (uint8_t)c;
        mmio_barrier();
    }
}

void vga_putchar(char c) {
    if (dafb_console_mirror_enabled()) {
        dafb_console_putchar(c);
    }

    if (c == '\n') {
        m68k_console_putchar_raw('\r');
    }

    m68k_console_putchar_raw(c);
}

/* Minimal VGA wrappers so arch/m68k can use vga_puts / vga_set_color */
void vga_clear(void) {
    if (dafb_console_ready()) {
        dafb_console_clear(VGA_BLACK);
    }
}

void vga_puts(const char *s) {
    if (!s) return;
    while (*s) vga_putchar(*s++);
    vga_flush();
}

void vga_flush(void) {
    if (!console_initialised) {
        return;
    }

    (void)scc_wait_tx_ready(0);
    (void)scc_wait_tx_ready(1);
    mmio_barrier();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    if (dafb_console_ready()) {
        dafb_console_set_color(fg, bg);
    }
}

void vga_set_cursor(int x, int y) {
    if (dafb_console_ready()) {
        dafb_console_set_cursor(x, y);
    }
}

void vga_init(void) {
    m68k_console_init();
}

static uintptr_t heap_ptr = 0;

void *kheap_alloc(size_t sz, int align) {
    (void)align; /* alignment parameter currently ignored; allocations are 8-byte aligned */
    if (!heap_ptr) heap_ptr = (uintptr_t)&kernel_end;
    uintptr_t p = (heap_ptr + 7) & ~((uintptr_t)7);
    heap_ptr = p + sz;
    return (void *)p;
}

void kheap_free(void *p) { (void)p; }
void kheap_init(uint32_t start, uint32_t size) { (void)start; (void)size; }

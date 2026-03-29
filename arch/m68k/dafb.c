// Minimal DAFB framebuffer helper for Macintosh LC III (M68K)
// Uses best-effort timing constants (from provided pixel clock) and
// performs register readback diagnostics. Writes a simple 8bpp test
// pattern into the frame buffer using the LC3 4MB RAM base.

#include "bootinfo.h"
#include "mac_lc3.h"
#include "../../include/types.h"
#include "../../drivers/vga.h"
#include "../../lib/stdio.h"

// Timing constants (derived from provided values)
// Pixel clock: 15.6672 MHz
#define DAFB_PIXELCLK_HZ    15667200u
// Derived per-pixel period in nanoseconds (~63.84 ns)
#define DAFB_PIXEL_PERIOD_NS 64u
// Horizontal active pixels and total pixels (approx)
#define DAFB_H_ACTIVE       512
#define DAFB_H_TOTAL        704
#define DAFB_H_BLANK        (DAFB_H_TOTAL - DAFB_H_ACTIVE)
// Vertical total lines (approx)
#define DAFB_V_TOTAL        370

// Helper: small busy-wait for mmio to settle
static void dafb_delay(void) {
    for (volatile int i = 0; i < 20000; i++) asm volatile ("nop");
}

static int m68k_bootinfo_has_framebuffer(void) {
    const m68k_bootinfo_t *boot = m68k_bootinfo_get();

    return boot->mac_video_base != 0 &&
           boot->mac_video_row != 0 &&
           boot->mac_video_depth != 0 &&
           m68k_bootinfo_video_width() != 0 &&
           m68k_bootinfo_video_height() != 0;
}

// Initialise DAFB (best-effort; real hardware needs detailed setup).
void dafb_init(void) {
    const m68k_bootinfo_t *boot = m68k_bootinfo_get();

    if (m68k_bootinfo_has_framebuffer()) {
        kprintf("[FB]   Bootinfo framebuffer base=0x%x %ux%ux%u stride=%u\n",
                boot->mac_video_base,
                m68k_bootinfo_video_width(),
                m68k_bootinfo_video_height(),
                boot->mac_video_depth,
                boot->mac_video_row);
        return;
    }

    // Construct a reasonable mode word (best-effort placeholder):
    // bit0 = enable, bits[8:15] = bpp (8), bits[16:31] = reserved
    uint32_t wanted_mode = (1u << 0) | (8u << 8);

    // For the pixel clock register we write the target frequency in Hz.
    // Exact encoding on real hardware may differ; write a readable value
    // so diagnostic readback is easy during hardware bring-up.
    uint32_t wanted_pixclk = DAFB_PIXELCLK_HZ;

    DAFB_PIXEL_CLK = wanted_pixclk;
    mmio_barrier();
    DAFB_MODE_CTRL = wanted_mode;
    mmio_barrier();

    // Let HW settle
    dafb_delay();

    // Diagnostic: read back registers and print status over serial (kprintf -> SCC)
    uint32_t rmode = DAFB_MODE_CTRL;
    uint32_t rclk  = DAFB_PIXEL_CLK;
    kprintf("[DAFB] MODE_CTRL wrote=0x%x read=0x%x\n", wanted_mode, rmode);
    kprintf("[DAFB] PIXCLK   wrote=%u read=%u\n", wanted_pixclk, rclk);

    if (rmode != wanted_mode) kprintf("[DAFB] WARNING: MODE mismatch\n");
    if (rclk != wanted_pixclk) kprintf("[DAFB] WARNING: PIXCLK mismatch\n");

    // Report timing-derived values
    kprintf("[DAFB] H_ACTIVE=%d H_TOTAL=%d H_BLANK=%d V_TOTAL=%d\n",
            DAFB_H_ACTIVE, DAFB_H_TOTAL, DAFB_H_BLANK, DAFB_V_TOTAL);
}

// Draw a test pattern into the framebuffer (8bpp assumed).
void dafb_draw_test(void) {
    const m68k_bootinfo_t *boot = m68k_bootinfo_get();
    volatile uint8_t *fb;
    int width;
    int height;
    int stride;

    if (m68k_bootinfo_has_framebuffer()) {
        fb = (volatile uint8_t *)(uintptr_t)boot->mac_video_base;
        width = (int)m68k_bootinfo_video_width();
        height = (int)m68k_bootinfo_video_height();
        stride = (int)boot->mac_video_row;

        if (boot->mac_video_depth != 8) {
            kprintf("[FB]   Unsupported bootinfo depth %u, expected 8bpp\n",
                    boot->mac_video_depth);
            return;
        }
    } else {
        fb = (volatile uint8_t *)MAC_LC3_FB_BASE_4MB;
        width = DAFB_H_ACTIVE;
        height = DAFB_V_TOTAL;
        stride = DAFB_H_ACTIVE;
    }

    // Defensive: if fb pointer seems invalid, bail out
    if ((uintptr_t)fb < 0x100) {
        kprintf("[DAFB] No framebuffer base configured (0x%p)\n", fb);
        // Fallback to VGA text so QEMU shows something
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_puts("BlueyOS M68K - framebuffer fallback (VGA)\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        return;
    }

    // Simple pixel fill - 8bpp
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            uint8_t v = (uint8_t)((x + (y * 3)) & 0xFF);
            fb[y * stride + x] = v;
        }
    }

    // Ensure writes reach memory (best-effort barrier)
    mmio_barrier();

    // Verify first few bytes and print diagnostic
    kprintf("[FB]   test pattern written: fb[0]=0x%x fb[1]=0x%x fb[2]=0x%x\n",
            fb[0], fb[1], fb[2]);

    // Heartbeat: also directly emit a short ASCII banner via low-level putchar
    vga_puts("[FB]   framebuffer write complete - check display or hardware\n");
}

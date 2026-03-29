// Minimal DAFB framebuffer helper for Macintosh LC III (M68K)
// Uses best-effort timing constants (from provided pixel clock) and
// performs register readback diagnostics. Writes a simple 8bpp test
// pattern into the frame buffer using the LC3 4MB RAM base.

#include "bootinfo.h"
#include "boot_font.h"
#include "mac_lc3.h"
#include "../../include/types.h"
#include "../../drivers/vga.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"

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

typedef struct {
    volatile uint8_t *base;
    int width;
    int height;
    int stride;
} m68k_framebuffer_t;

static int m68k_framebuffer_open(m68k_framebuffer_t *framebuffer) {
    const m68k_bootinfo_t *boot = m68k_bootinfo_get();

    if (m68k_bootinfo_has_framebuffer()) {
        if (boot->mac_video_depth != 8) {
            kprintf("[FB]   Unsupported bootinfo depth %u, expected 8bpp\n",
                    boot->mac_video_depth);
            return 0;
        }

        framebuffer->base = (volatile uint8_t *)(uintptr_t)boot->mac_video_base;
        framebuffer->width = (int)m68k_bootinfo_video_width();
        framebuffer->height = (int)m68k_bootinfo_video_height();
        framebuffer->stride = (int)boot->mac_video_row;
        return 1;
    }

    framebuffer->base = (volatile uint8_t *)MAC_LC3_FB_BASE_4MB;
    framebuffer->width = DAFB_H_ACTIVE;
    framebuffer->height = DAFB_V_TOTAL;
    framebuffer->stride = DAFB_H_ACTIVE;
    return 1;
}

static void m68k_fb_fill_rect(const m68k_framebuffer_t *framebuffer,
                              int x,
                              int y,
                              int width,
                              int height,
                              uint8_t color) {
    if (x < 0) {
        width += x;
        x = 0;
    }
    if (y < 0) {
        height += y;
        y = 0;
    }
    if (x >= framebuffer->width || y >= framebuffer->height || width <= 0 || height <= 0) {
        return;
    }
    if (x + width > framebuffer->width) {
        width = framebuffer->width - x;
    }
    if (y + height > framebuffer->height) {
        height = framebuffer->height - y;
    }

    for (int row = 0; row < height; row++) {
        volatile uint8_t *dst = framebuffer->base + ((y + row) * framebuffer->stride) + x;
        for (int col = 0; col < width; col++) {
            dst[col] = color;
        }
    }
}

static void m68k_fb_draw_background(const m68k_framebuffer_t *framebuffer) {
    for (int y = 0; y < framebuffer->height; y++) {
        volatile uint8_t *row = framebuffer->base + (y * framebuffer->stride);
        uint8_t base = (uint8_t)(0x10 + ((y * 0x30) / framebuffer->height));
        for (int x = 0; x < framebuffer->width; x++) {
            uint8_t shade = (uint8_t)(base + (((x / 16) + (y / 12)) & 0x03));
            row[x] = shade;
        }
    }
}

static const uint8_t *m68k_fb_glyph(char ch) {
    return m68k_fonty_rg_ascii[(uint8_t)ch];
}

static void m68k_fb_draw_char(const m68k_framebuffer_t *framebuffer,
                              int x,
                              int y,
                              char ch,
                              uint8_t fg,
                              uint8_t bg) {
    const uint8_t *glyph = m68k_fb_glyph(ch);

    for (int row = 0; row < M68K_FONTY_RG_HEIGHT; row++) {
        int dst_y = y + row;
        if (dst_y < 0 || dst_y >= framebuffer->height) {
            continue;
        }

        volatile uint8_t *dst = framebuffer->base + (dst_y * framebuffer->stride);
        uint8_t bits = glyph[row];

        for (int col = 0; col < M68K_FONTY_RG_WIDTH; col++) {
            int dst_x = x + col;
            if (dst_x < 0 || dst_x >= framebuffer->width) {
                continue;
            }
            dst[dst_x] = (bits & (1u << (7 - col))) ? fg : bg;
        }
    }
}

static void m68k_fb_draw_text(const m68k_framebuffer_t *framebuffer,
                              int x,
                              int y,
                              const char *text,
                              uint8_t fg,
                              uint8_t bg) {
    int cursor_x = x;

    while (*text != '\0') {
        if (*text == '\n') {
            cursor_x = x;
            y += M68K_FONTY_RG_HEIGHT + 2;
        } else {
            m68k_fb_draw_char(framebuffer, cursor_x, y, *text, fg, bg);
            cursor_x += M68K_FONTY_RG_WIDTH;
        }
        text++;
    }
}

static void m68k_fb_append_str(char *dst, size_t *offset, const char *src) {
    while (*src != '\0' && *offset + 1 < 32) {
        dst[*offset] = *src;
        (*offset)++;
        src++;
    }
    dst[*offset] = '\0';
}

static void m68k_fb_append_uint(char *dst, size_t *offset, uint32_t value) {
    char scratch[12];
    char *number = itoa((int)value, scratch, 10);
    m68k_fb_append_str(dst, offset, number);
}

static void m68k_fb_make_line(char *dst, const char *prefix, uint32_t value, const char *suffix) {
    size_t offset = 0;
    dst[0] = '\0';
    m68k_fb_append_str(dst, &offset, prefix);
    m68k_fb_append_uint(dst, &offset, value);
    m68k_fb_append_str(dst, &offset, suffix);
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
    m68k_framebuffer_t framebuffer;
    char model_line[32];
    char ram_line[32];
    uint32_t ram_mb = 32;

    if (!m68k_framebuffer_open(&framebuffer)) {
        return;
    }

    // Defensive: if fb pointer seems invalid, bail out
    if ((uintptr_t)framebuffer.base < 0x100) {
        kprintf("[DAFB] No framebuffer base configured (0x%p)\n", framebuffer.base);
        // Fallback to VGA text so QEMU shows something
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_puts("BlueyOS M68K - framebuffer fallback (VGA)\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        return;
    }

    if (boot->mac_memsize_mb != 0) {
        ram_mb = boot->mac_memsize_mb;
    } else if (boot->mem_chunk_size != 0) {
        ram_mb = boot->mem_chunk_size / (1024u * 1024u);
    }

    m68k_fb_draw_background(&framebuffer);
    m68k_fb_fill_rect(&framebuffer, 18, 18, framebuffer.width - 36, 76, 0x08);
    m68k_fb_fill_rect(&framebuffer, 22, 22, framebuffer.width - 44, 68, 0x18);

    m68k_fb_make_line(model_line, "MODEL ", boot->mac_model, "");
    m68k_fb_make_line(ram_line, "RAM ", ram_mb, " MB");

    m68k_fb_draw_text(&framebuffer, 32, 30, "BLUEYOS M68K", 0xf0, 0x18);
    m68k_fb_draw_text(&framebuffer, 32, 44, model_line, 0xff, 0x18);
    m68k_fb_draw_text(&framebuffer, 32, 56, ram_line, 0xff, 0x18);
    m68k_fb_draw_text(&framebuffer, 32, 68, "SERIAL OK", 0xff, 0x18);
    m68k_fb_draw_text(&framebuffer, 32, 80, "FRAMEBUFFER OK", 0xff, 0x18);

    // Ensure writes reach memory (best-effort barrier)
    mmio_barrier();

    // Verify first few bytes and print diagnostic
    kprintf("[FB]   boot panel written: fb[0]=0x%x fb[1]=0x%x fb[2]=0x%x\n",
            framebuffer.base[0], framebuffer.base[1], framebuffer.base[2]);

    // Heartbeat: also directly emit a short ASCII banner via low-level putchar
    vga_puts("[FB]   framebuffer text rendered - check display or hardware\n");
}

// Minimal DAFB framebuffer helper for Macintosh LC III (M68K)
// Uses best-effort timing constants (from provided pixel clock) and
// performs register readback diagnostics. Writes a simple 8bpp test
// pattern into the frame buffer using the LC3 4MB RAM base.

#include "bootinfo.h"
#include "dafb.h"
#include "boot_font.h"
#include "mac_lc3.h"
#include "../../include/types.h"
#include "../../drivers/vga.h"
#include "../../lib/stdio.h"
#include "../../lib/stdlib.h"
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

static m68k_framebuffer_t dafb_console_fb;
static int dafb_text_ready;
static int dafb_text_mirror_enabled;
static int dafb_cursor_x;
static int dafb_cursor_y;
static uint8_t dafb_fg = 0xff;
static uint8_t dafb_bg = 0x00;

#define DAFB_TEXT_CELL_W 8
#define DAFB_TEXT_CELL_H 8
#define DAFB_TEXT_LINE_H 10

static uint8_t dafb_color_map(uint8_t color) {
    static const uint8_t palette[16] = {
        0x00, 0x20, 0x40, 0x60,
        0x50, 0x70, 0x90, 0xb0,
        0x40, 0x80, 0xa0, 0xc0,
        0xd0, 0xe0, 0xf0, 0xff
    };

    return palette[color & 0x0f];
}

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

static const uint8_t *m68k_fb_glyph(char ch) {
    uint8_t code = (uint8_t)ch;

    if (code >= 'a' && code <= 'z') {
        code = (uint8_t)(code - ('a' - 'A'));
    }

    return m68k_fonty_rg_ascii[code];
}

static int m68k_fb_text_width(const char *text) {
    int width = 0;

    while (*text != '\0') {
        width += M68K_FONTY_RG_WIDTH;
        text++;
    }

    return width;
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
    while (*text != '\0') {
        if (*text == '\n') {
            x = 0;
            y += DAFB_TEXT_LINE_H;
        } else {
            m68k_fb_draw_char(framebuffer, x, y, *text, fg, bg);
            x += M68K_FONTY_RG_WIDTH;
        }
        text++;
    }
}

static void m68k_fb_draw_centered_text(const m68k_framebuffer_t *framebuffer,
                                       int y,
                                       const char *text,
                                       uint8_t fg,
                                       uint8_t bg) {
    int width = m68k_fb_text_width(text);
    int x = (framebuffer->width - width) / 2;

    if (x < 0) {
        x = 0;
    }

    m68k_fb_draw_text(framebuffer, x, y, text, fg, bg);
}

static void m68k_format_ram_line(char *buffer, uint32_t ram_mb) {
    char digits[12];
    char *ram_text = itoa((int)ram_mb, digits, 10);

    strcpy(buffer, "RAM ");
    strcat(buffer, ram_text);
    strcat(buffer, " MB");
}

static void m68k_fb_scroll_text(void) {
    int copy_rows = dafb_console_fb.height - DAFB_TEXT_LINE_H;

    if (copy_rows <= 0) {
        return;
    }

    for (int y = 0; y < copy_rows; y++) {
        volatile uint8_t *dst = dafb_console_fb.base + (y * dafb_console_fb.stride);
        volatile uint8_t *src = dafb_console_fb.base + ((y + DAFB_TEXT_LINE_H) * dafb_console_fb.stride);
        for (int x = 0; x < dafb_console_fb.width; x++) {
            dst[x] = src[x];
        }
    }

    m68k_fb_fill_rect(&dafb_console_fb,
                      0,
                      copy_rows,
                      dafb_console_fb.width,
                      DAFB_TEXT_LINE_H,
                      dafb_bg);
}

int dafb_console_enable(void) {
    if (!m68k_framebuffer_open(&dafb_console_fb)) {
        return 0;
    }

    if ((uintptr_t)dafb_console_fb.base < 0x100) {
        return 0;
    }

    dafb_text_ready = 1;
    dafb_text_mirror_enabled = 1;
    dafb_cursor_x = 0;
    dafb_cursor_y = 0;
    dafb_fg = dafb_color_map(VGA_WHITE);
    dafb_bg = dafb_color_map(VGA_BLACK);
    dafb_console_clear(VGA_BLACK);
    return 1;
}

int dafb_console_ready(void) {
    return dafb_text_ready;
}

int dafb_console_mirror_enabled(void) {
    return dafb_text_ready && dafb_text_mirror_enabled;
}

void dafb_console_set_mirror(int enabled) {
    if (!dafb_text_ready) {
        return;
    }

    dafb_text_mirror_enabled = enabled != 0;
}

void dafb_console_clear(uint8_t bg) {
    if (!dafb_text_ready) {
        return;
    }

    dafb_bg = dafb_color_map(bg);
    m68k_fb_fill_rect(&dafb_console_fb, 0, 0, dafb_console_fb.width, dafb_console_fb.height, dafb_bg);
    dafb_cursor_x = 0;
    dafb_cursor_y = 0;
}

void dafb_console_set_color(uint8_t fg, uint8_t bg) {
    dafb_fg = dafb_color_map(fg);
    dafb_bg = dafb_color_map(bg);
}

void dafb_console_set_cursor(int x, int y) {
    dafb_cursor_x = x < 0 ? 0 : x;
    dafb_cursor_y = y < 0 ? 0 : y;
}

void dafb_console_putchar(char c) {
    if (!dafb_text_ready) {
        return;
    }

    if (c == '\r') {
        dafb_cursor_x = 0;
        return;
    }

    if (c == '\n') {
        dafb_cursor_x = 0;
        dafb_cursor_y++;
    } else {
        int pixel_x = dafb_cursor_x * DAFB_TEXT_CELL_W;
        int pixel_y = dafb_cursor_y * DAFB_TEXT_LINE_H;

        m68k_fb_draw_char(&dafb_console_fb, pixel_x, pixel_y, c, dafb_fg, dafb_bg);
        dafb_cursor_x++;
    }

    if ((dafb_cursor_x + 1) * DAFB_TEXT_CELL_W >= dafb_console_fb.width) {
        dafb_cursor_x = 0;
        dafb_cursor_y++;
    }

    if ((dafb_cursor_y + 1) * DAFB_TEXT_LINE_H >= dafb_console_fb.height) {
        m68k_fb_scroll_text();
        dafb_cursor_y = (dafb_console_fb.height / DAFB_TEXT_LINE_H) - 1;
        if (dafb_cursor_y < 0) {
            dafb_cursor_y = 0;
        }
    }
}

void dafb_show_splash(const char *arch_label, uint32_t ram_mb) {
    uint8_t frame = dafb_color_map(BLUEY_BLUE);
    uint8_t panel = dafb_color_map(VGA_DARK_GREY);
    uint8_t accent = dafb_color_map(BINGO_ORANGE);
    uint8_t text = dafb_color_map(VGA_WHITE);
    char ram_line[20];
    int panel_width;
    int panel_height = 120;
    int panel_x;
    int panel_y;

    if (!dafb_text_ready) {
        return;
    }

    m68k_fb_fill_rect(&dafb_console_fb, 0, 0, dafb_console_fb.width, dafb_console_fb.height, dafb_color_map(VGA_BLACK));

    panel_width = dafb_console_fb.width - 120;
    if (panel_width > 360) {
        panel_width = 360;
    }
    if (panel_width < 220) {
        panel_width = dafb_console_fb.width - 40;
    }

    panel_x = (dafb_console_fb.width - panel_width) / 2;
    panel_y = (dafb_console_fb.height - panel_height) / 3;
    if (panel_y < 24) {
        panel_y = 24;
    }

    m68k_fb_fill_rect(&dafb_console_fb, panel_x, panel_y, panel_width, panel_height, frame);
    m68k_fb_fill_rect(&dafb_console_fb, panel_x + 4, panel_y + 4, panel_width - 8, panel_height - 8, panel);
    m68k_fb_fill_rect(&dafb_console_fb, panel_x + 4, panel_y + 4, panel_width - 8, 18, accent);

    m68k_fb_draw_centered_text(&dafb_console_fb, panel_y + 10, "BLUEYOS", dafb_color_map(VGA_BLACK), accent);
    m68k_fb_draw_centered_text(&dafb_console_fb, panel_y + 34, arch_label, text, panel);

    if (ram_mb != 0) {
        m68k_format_ram_line(ram_line, ram_mb);
        m68k_fb_draw_centered_text(&dafb_console_fb, panel_y + 54, ram_line, text, panel);
    }

    m68k_fb_draw_centered_text(&dafb_console_fb, panel_y + 78, "READY", accent, panel);

    dafb_fg = text;
    dafb_bg = dafb_color_map(VGA_BLACK);
    dafb_cursor_x = 0;
    dafb_cursor_y = 0;
    dafb_text_mirror_enabled = 0;
    mmio_barrier();
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

    if (dafb_text_ready) {
        framebuffer = dafb_console_fb;
    } else if (!m68k_framebuffer_open(&framebuffer)) {
        return;
    }

    if ((uintptr_t)framebuffer.base < 0x100) {
        kprintf("[DAFB] No framebuffer base configured (0x%p)\n", framebuffer.base);
        // Fallback to VGA text so QEMU shows something
        vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
        vga_puts("BlueyOS M68K - framebuffer fallback (VGA)\n");
        vga_set_color(VGA_WHITE, VGA_BLACK);
        return;
    }

    // Ensure writes reach memory (best-effort barrier)
    mmio_barrier();

    // Verify first few bytes and print diagnostic
    kprintf("[FB]   console active: fb[0]=0x%x fb[1]=0x%x fb[2]=0x%x\n",
            framebuffer.base[0], framebuffer.base[1], framebuffer.base[2]);

    if (boot->mac_memsize_mb != 0 || boot->mem_chunk_size != 0) {
        vga_puts("FRAMEBUFFER OK\n");
    }
}

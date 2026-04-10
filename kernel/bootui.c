#include "bootui.h"

#include "../drivers/bootfb.h"
#include "../drivers/vga.h"
#include "../lib/stdio.h"

#ifdef BLUEYOS_ARCH_M68K
#include "../arch/m68k/dafb.h"
#endif

#define BLUEY_BOOT_LOGO_WIDTH 31
#define BLUEY_BOOT_LOGO_PIXEL_ROWS 22
#define BLUEY_BOOT_LOGO_TEXT_ROWS ((BLUEY_BOOT_LOGO_PIXEL_ROWS + 1) / 2)
#define BLUEY_BOOT_LOGO_SPACING_ROWS 1

static const uint8_t bluey_boot_logo_pixels[BLUEY_BOOT_LOGO_PIXEL_ROWS][BLUEY_BOOT_LOGO_WIDTH] = {
    {0, 0, 0, 0, 0, 0, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 8, 7, 7, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 7, 8, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 8, 0, 8, 15, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 15, 7, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 8, 8, 8, 15, 8, 0, 8, 0, 8, 8, 8, 8, 0, 8, 8, 8, 8, 8, 0, 8, 7, 7, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 8, 8, 8, 15, 8, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 0, 0, 0, 0, 0, 0, 0},
    {0, 8, 8, 8, 8, 7, 8, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 0, 0, 0, 0, 0, 0, 0},
    {0, 8, 8, 8, 8, 7, 8, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 0, 0, 0, 0, 0, 0, 0},
    {0, 8, 8, 8, 8, 15, 8, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 0, 8, 0, 0, 0, 0, 0},
    {0, 8, 8, 8, 8, 15, 8, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 8, 8, 8, 0, 0, 0, 0},
    {0, 8, 8, 8, 8, 15, 8, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 8, 8, 8, 0, 0, 0, 0},
    {0, 8, 8, 8, 8, 15, 7, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 7, 7, 8, 8, 8, 0, 0, 0, 0},
    {0, 8, 7, 8, 8, 15, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 7, 7, 0, 0, 0, 0, 0, 0, 0},
    {0, 8, 7, 8, 0, 15, 15, 15, 15, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 0, 0, 0, 0, 0, 0, 0},
    {0, 8, 8, 0, 0, 8, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0},
    {0, 8, 8, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 8, 0, 8, 8, 8, 0, 0, 0, 0, 0, 0},
    {0, 8, 8, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0, 0, 0, 0},
    {0, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 8, 0, 0, 0, 0, 0, 0},
    {8, 8, 8, 7, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 8, 8, 8, 7, 8, 8, 8, 8, 8, 0, 0},
    {8, 8, 8, 7, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 8, 7, 7, 7, 7, 8, 8, 8, 8, 7, 8, 7, 15, 15, 8, 0},
    {8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 8, 8, 8, 8, 8, 8, 8, 8, 7, 8, 8, 8, 8, 8, 0, 0, 8, 8, 8, 0},
    {8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8},
    {0, 0, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 0, 0},
};

static void bluey_boot_draw_vga_logo(void) {
    int start_x = (VGA_TEXT_WIDTH - BLUEY_BOOT_LOGO_WIDTH) / 2;
    int pixel_row;

    for (pixel_row = 0; pixel_row < BLUEY_BOOT_LOGO_PIXEL_ROWS; pixel_row += 2) {
        int text_row = pixel_row / 2;
        int col;

        for (col = 0; col < BLUEY_BOOT_LOGO_WIDTH; col++) {
            uint8_t top = bluey_boot_logo_pixels[pixel_row][col];
            uint8_t bottom = VGA_BLACK;
            uint8_t fg = VGA_WHITE;
            uint8_t bg = VGA_BLACK;
            char glyph = ' ';

            if (pixel_row + 1 < BLUEY_BOOT_LOGO_PIXEL_ROWS) {
                bottom = bluey_boot_logo_pixels[pixel_row + 1][col];
            }

            if (top == bottom) {
                if (top != VGA_BLACK) {
                    glyph = '\xDB';
                    fg = top;
                }
            } else if (top == VGA_BLACK) {
                glyph = '\xDC';
                fg = bottom;
            } else if (bottom == VGA_BLACK) {
                glyph = '\xDF';
                fg = top;
            } else {
                glyph = '\xDF';
                fg = top;
                bg = bottom;
            }

            vga_write_cell(start_x + col, text_row, glyph, fg, bg);
        }
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_set_protected_rows(BLUEY_BOOT_LOGO_TEXT_ROWS + BLUEY_BOOT_LOGO_SPACING_ROWS);
    vga_set_cursor(0, BLUEY_BOOT_LOGO_TEXT_ROWS + BLUEY_BOOT_LOGO_SPACING_ROWS);
}

void bluey_boot_show_splash(const char *arch_label, uint32_t ram_mb, const bootfb_mode_t *framebuffer) {
#ifdef BLUEYOS_ARCH_M68K
    if (dafb_console_ready()) {
        dafb_show_splash(arch_label, ram_mb);
        return;
    }
#endif

    if (framebuffer && bootfb_init(framebuffer)) {
        bootfb_draw_splash();

        vga_set_color(VGA_WHITE, VGA_BLACK);
        vga_set_protected_rows(0);
        vga_set_cursor(0, 0);

        if (arch_label && arch_label[0]) {
            kprintf("BlueyOS %s", arch_label);
            if (ram_mb != 0) {
                kprintf(" | RAM %u MB", ram_mb);
            }
            kprintf("\n\n");
        }
        return;
    }

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
    bluey_boot_draw_vga_logo();

    if (arch_label && arch_label[0]) {
        kprintf("BlueyOS %s", arch_label);
        if (ram_mb != 0) {
            kprintf(" | RAM %u MB", ram_mb);
        }
        kprintf("\n\n");
    }
}
#include "bootfb.h"

#include "bootfb_font.h"
#include "bootfb_logo.h"

#define BLUEY_BOOTFB_TYPE_INDEXED 0
#define BLUEY_BOOTFB_TYPE_RGB     1

#define BOOTFB_CONSOLE_COLS 80
#define BOOTFB_CONSOLE_ROWS 25
#define BOOTFB_CELL_WIDTH   BLUEY_BOOT_FONT_WIDTH
#define BOOTFB_CELL_HEIGHT  BLUEY_BOOT_FONT_HEIGHT
#define BOOTFB_SPLASH_TOP_MARGIN 16
#define BOOTFB_SPLASH_BOTTOM_MARGIN 16

typedef struct {
    volatile uint8_t *base;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t red_field_position;
    uint8_t red_mask_size;
    uint8_t green_field_position;
    uint8_t green_mask_size;
    uint8_t blue_field_position;
    uint8_t blue_mask_size;
    int ready;
    int console_x;
    int console_y;
} bootfb_state_t;

static bootfb_state_t bootfb;

static const uint8_t bootfb_vga_palette[16][3] = {
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0xAA},
    {0x00, 0xAA, 0x00},
    {0x00, 0xAA, 0xAA},
    {0xAA, 0x00, 0x00},
    {0xAA, 0x00, 0xAA},
    {0xAA, 0x55, 0x00},
    {0xAA, 0xAA, 0xAA},
    {0x55, 0x55, 0x55},
    {0x55, 0x55, 0xFF},
    {0x55, 0xFF, 0x55},
    {0x55, 0xFF, 0xFF},
    {0xFF, 0x55, 0x55},
    {0xFF, 0x55, 0xFF},
    {0xFF, 0xFF, 0x55},
    {0xFF, 0xFF, 0xFF},
};

static uint32_t bootfb_pack_rgb(uint8_t red, uint8_t green, uint8_t blue) {
    uint32_t value = 0;

    if (bootfb.red_mask_size) {
        uint32_t max = (1u << bootfb.red_mask_size) - 1u;
        value |= (((uint32_t)red * max) / 255u) << bootfb.red_field_position;
    }
    if (bootfb.green_mask_size) {
        uint32_t max = (1u << bootfb.green_mask_size) - 1u;
        value |= (((uint32_t)green * max) / 255u) << bootfb.green_field_position;
    }
    if (bootfb.blue_mask_size) {
        uint32_t max = (1u << bootfb.blue_mask_size) - 1u;
        value |= (((uint32_t)blue * max) / 255u) << bootfb.blue_field_position;
    }

    return value;
}

static void bootfb_write_pixel(int x, int y, uint8_t red, uint8_t green, uint8_t blue) {
    volatile uint8_t *pixel;
    uint32_t value;

    if (!bootfb.ready) return;
    if (x < 0 || y < 0) return;
    if ((uint32_t)x >= bootfb.width || (uint32_t)y >= bootfb.height) return;

    pixel = bootfb.base + ((uint32_t)y * bootfb.pitch);
    value = bootfb_pack_rgb(red, green, blue);

    switch (bootfb.bpp) {
        case 32:
            *(volatile uint32_t *)(pixel + ((uint32_t)x * 4u)) = value;
            break;
        case 24:
            pixel += (uint32_t)x * 3u;
            pixel[0] = (uint8_t)(value & 0xFFu);
            pixel[1] = (uint8_t)((value >> 8) & 0xFFu);
            pixel[2] = (uint8_t)((value >> 16) & 0xFFu);
            break;
        case 16:
        case 15:
            *(volatile uint16_t *)(pixel + ((uint32_t)x * 2u)) = (uint16_t)value;
            break;
        default:
            break;
    }
}

static void bootfb_fill_rect(int x, int y, int width, int height, uint8_t red, uint8_t green, uint8_t blue) {
    int row;
    int col;

    if (!bootfb.ready || width <= 0 || height <= 0) return;

    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            bootfb_write_pixel(x + col, y + row, red, green, blue);
        }
    }
}

static void bootfb_reset_console_origin(void) {
    int console_width = BOOTFB_CONSOLE_COLS * BOOTFB_CELL_WIDTH;

    bootfb.console_x = 0;
    if ((uint32_t)console_width < bootfb.width) {
        bootfb.console_x = (int)(bootfb.width - (uint32_t)console_width) / 2;
    }
    bootfb.console_y = 0;
}

static void bootfb_draw_char_pixels(int px, int py, char ch, uint8_t fg, uint8_t bg) {
    const uint8_t *glyph = NULL;
    int row;
    int col;

    (void)bg;

    if (ch >= BLUEY_BOOT_FONT_FIRST && ch <= BLUEY_BOOT_FONT_LAST) {
        glyph = bluey_boot_font[(uint8_t)ch - BLUEY_BOOT_FONT_FIRST];
    } else if (ch == '\xDB' || ch == '\xDC' || ch == '\xDF') {
        glyph = NULL;
    } else {
        glyph = bluey_boot_font['?' - BLUEY_BOOT_FONT_FIRST];
    }

    bootfb_fill_rect(px, py, BOOTFB_CELL_WIDTH, BOOTFB_CELL_HEIGHT, 0x00, 0x00, 0x00);

    if (ch == '\xDB') {
        bootfb_fill_rect(px, py, BOOTFB_CELL_WIDTH, BOOTFB_CELL_HEIGHT,
                         bootfb_vga_palette[fg][0],
                         bootfb_vga_palette[fg][1],
                         bootfb_vga_palette[fg][2]);
        return;
    }
    if (ch == '\xDF') {
        bootfb_fill_rect(px, py, BOOTFB_CELL_WIDTH, BOOTFB_CELL_HEIGHT / 2,
                         bootfb_vga_palette[fg][0],
                         bootfb_vga_palette[fg][1],
                         bootfb_vga_palette[fg][2]);
        return;
    }
    if (ch == '\xDC') {
        bootfb_fill_rect(px, py + (BOOTFB_CELL_HEIGHT / 2), BOOTFB_CELL_WIDTH, BOOTFB_CELL_HEIGHT / 2,
                         bootfb_vga_palette[fg][0],
                         bootfb_vga_palette[fg][1],
                         bootfb_vga_palette[fg][2]);
        return;
    }

    if (!glyph) return;

    for (row = 0; row < BOOTFB_CELL_HEIGHT; row++) {
        uint8_t bits = glyph[row];
        for (col = 0; col < BOOTFB_CELL_WIDTH; col++) {
            if ((bits & (1u << (7 - col))) == 0) continue;
            bootfb_write_pixel(px + col, py + row,
                               bootfb_vga_palette[fg][0],
                               bootfb_vga_palette[fg][1],
                               bootfb_vga_palette[fg][2]);
        }
    }
}

int bootfb_init(const bootfb_mode_t *mode) {
    if (!mode) return 0;
    if (mode->address == 0 || mode->pitch == 0 || mode->width == 0 || mode->height == 0) return 0;
    if (mode->type != BLUEY_BOOTFB_TYPE_RGB) return 0;
    if (mode->bpp != 15 && mode->bpp != 16 && mode->bpp != 24 && mode->bpp != 32) return 0;

    bootfb.base = (volatile uint8_t *)mode->address;
    bootfb.pitch = mode->pitch;
    bootfb.width = mode->width;
    bootfb.height = mode->height;
    bootfb.bpp = mode->bpp;
    bootfb.red_field_position = mode->red_field_position;
    bootfb.red_mask_size = mode->red_mask_size;
    bootfb.green_field_position = mode->green_field_position;
    bootfb.green_mask_size = mode->green_mask_size;
    bootfb.blue_field_position = mode->blue_field_position;
    bootfb.blue_mask_size = mode->blue_mask_size;
    bootfb.ready = 1;
    bootfb_reset_console_origin();
    return 1;
}

int bootfb_ready(void) {
    return bootfb.ready;
}

int bootfb_draw_splash(void) {
    int start_x;
    int start_y = BOOTFB_SPLASH_TOP_MARGIN;
    int y;
    int console_y;

    if (!bootfb.ready) return 0;

    bootfb_fill_rect(0, 0, (int)bootfb.width, (int)bootfb.height, 0x0C, 0x11, 0x17);

    start_x = 0;
    if (bootfb.width > BLUEY_BOOT_LOGO_WIDTH) {
        start_x = (int)(bootfb.width - BLUEY_BOOT_LOGO_WIDTH) / 2;
    }

    for (y = 0; y < BLUEY_BOOT_LOGO_HEIGHT; y++) {
        uint16_t begin = bluey_boot_logo_row_offsets[y];
        uint16_t end = bluey_boot_logo_row_offsets[y + 1];
        int x = 0;
        uint16_t index;

        for (index = begin; index + 1 < end; index += 2) {
            uint8_t count = bluey_boot_logo_rle[index];
            uint8_t color = bluey_boot_logo_rle[index + 1];
            int span;

            for (span = 0; span < count && x < BLUEY_BOOT_LOGO_WIDTH; span++, x++) {
                bootfb_write_pixel(start_x + x, start_y + y,
                                   bluey_boot_logo_palette[color][0],
                                   bluey_boot_logo_palette[color][1],
                                   bluey_boot_logo_palette[color][2]);
            }
        }
    }

    bootfb.console_x = 0;
    if ((uint32_t)(BOOTFB_CONSOLE_COLS * BOOTFB_CELL_WIDTH) < bootfb.width) {
        bootfb.console_x = (int)(bootfb.width - (uint32_t)(BOOTFB_CONSOLE_COLS * BOOTFB_CELL_WIDTH)) / 2;
    }

    console_y = start_y + BLUEY_BOOT_LOGO_HEIGHT + BOOTFB_SPLASH_BOTTOM_MARGIN;
    if (console_y + (BOOTFB_CONSOLE_ROWS * BOOTFB_CELL_HEIGHT) > (int)bootfb.height) {
        console_y = (int)bootfb.height - (BOOTFB_CONSOLE_ROWS * BOOTFB_CELL_HEIGHT);
    }
    if (console_y < 0) {
        console_y = 0;
    }
    bootfb.console_y = console_y;

    return (bootfb.console_y + BOOTFB_CELL_HEIGHT - 1) / BOOTFB_CELL_HEIGHT;
}

void bootfb_clear_console(void) {
    if (!bootfb.ready) return;

    bootfb_fill_rect(0, 0, (int)bootfb.width, (int)bootfb.height, 0x00, 0x00, 0x00);
    bootfb_reset_console_origin();
}

void bootfb_sync_vga_cell(int x, int y, char c, uint8_t fg, uint8_t bg) {
    int px;
    int py;

    if (!bootfb.ready) return;
    if (x < 0 || y < 0 || x >= BOOTFB_CONSOLE_COLS || y >= BOOTFB_CONSOLE_ROWS) return;

    px = bootfb.console_x + (x * BOOTFB_CELL_WIDTH);
    py = bootfb.console_y + (y * BOOTFB_CELL_HEIGHT);
    bootfb_draw_char_pixels(px, py, c, fg, bg);
}

void bootfb_sync_vga_buffer(const uint16_t *text_buffer, int width, int height) {
    int row;
    int col;

    if (!bootfb.ready || !text_buffer) return;

    for (row = 0; row < height; row++) {
        for (col = 0; col < width; col++) {
            uint16_t entry = text_buffer[row * width + col];
            char ch = (char)(entry & 0xFFu);
            uint8_t attr = (uint8_t)(entry >> 8);
            uint8_t fg = attr & 0x0Fu;
            uint8_t bg = (attr >> 4) & 0x0Fu;
            bootfb_sync_vga_cell(col, row, ch, fg, bg);
        }
    }
}
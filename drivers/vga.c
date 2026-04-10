// BlueyOS VGA Driver - "Mum! The screen works!" - Bluey
// Episode ref: "Flat Pack" - everything starts from nothing, even pixels
#include "../include/types.h"
#include "../include/ports.h"
#include "vga.h"

#define VGA_MEM    ((uint16_t*)0xB8000)

static int vga_row, vga_col;
static uint8_t vga_color;
static int vga_protected_rows;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)(uint8_t)c | ((uint16_t)color << 8);
}

static void vga_update_cursor(void) {
    uint16_t pos = vga_row * VGA_TEXT_WIDTH + vga_col;
    outb(0x3D4, 14); outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15); outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void vga_scroll(void) {
    uint16_t blank = vga_entry(' ', vga_color);
    int start_row = vga_protected_rows;
    int i;
    if (start_row < 0) start_row = 0;
    if (start_row >= VGA_TEXT_HEIGHT) start_row = VGA_TEXT_HEIGHT - 1;
    for (i = start_row * VGA_TEXT_WIDTH; i < (VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH; i++)
        VGA_MEM[i] = VGA_MEM[i + VGA_TEXT_WIDTH];
    for (i = (VGA_TEXT_HEIGHT - 1) * VGA_TEXT_WIDTH; i < VGA_TEXT_HEIGHT * VGA_TEXT_WIDTH; i++)
        VGA_MEM[i] = blank;
    vga_row = VGA_TEXT_HEIGHT - 1;
}

void vga_init(void) {
    vga_color = (VGA_BLACK << 4) | VGA_LIGHT_GREY;
    vga_protected_rows = 0;
    vga_row = 0; vga_col = 0;
    vga_clear();
}

void vga_clear(void) {
    uint16_t blank = vga_entry(' ', vga_color);
    int i;
    for (i = 0; i < VGA_TEXT_WIDTH * VGA_TEXT_HEIGHT; i++) VGA_MEM[i] = blank;
    vga_protected_rows = 0;
    vga_row = 0; vga_col = 0;
    vga_update_cursor();
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = (bg << 4) | (fg & 0x0F);
}

void vga_set_cursor(int x, int y) {
    vga_col = x; vga_row = y;
    vga_update_cursor();
}

void vga_write_cell(int x, int y, char c, uint8_t fg, uint8_t bg) {
    uint8_t color;

    if (x < 0 || x >= VGA_TEXT_WIDTH || y < 0 || y >= VGA_TEXT_HEIGHT) return;
    color = (uint8_t)((bg << 4) | (fg & 0x0F));
    VGA_MEM[y * VGA_TEXT_WIDTH + x] = vga_entry(c, color);
}

void vga_set_protected_rows(int rows) {
    if (rows < 0) rows = 0;
    if (rows >= VGA_TEXT_HEIGHT) rows = VGA_TEXT_HEIGHT - 1;
    vga_protected_rows = rows;
    if (vga_row < vga_protected_rows) {
        vga_row = vga_protected_rows;
        vga_col = 0;
    }
    vga_update_cursor();
}

void vga_putchar(char c) {
    if (c == '\n') { vga_col = 0; vga_row++; }
    else if (c == '\r') { vga_col = 0; }
    else if (c == '\t') { vga_col = (vga_col + 8) & ~7; }
    else if (c == '\b') { if (vga_col > 0) vga_col--; }
    else {
        VGA_MEM[vga_row * VGA_TEXT_WIDTH + vga_col] = vga_entry(c, vga_color);
        vga_col++;
    }
    if (vga_col >= VGA_TEXT_WIDTH) { vga_col = 0; vga_row++; }
    if (vga_row >= VGA_TEXT_HEIGHT) vga_scroll();
    vga_update_cursor();
}

void vga_puts(const char *s) {
    while (*s) vga_putchar(*s++);
    vga_flush();
}

void vga_flush(void) {
}

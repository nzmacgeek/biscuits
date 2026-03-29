// BlueyOS VGA Driver - "Mum! The screen works!" - Bluey
// Episode ref: "Flat Pack" - everything starts from nothing, even pixels
#include "../include/types.h"
#include "../include/ports.h"
#include "vga.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_MEM    ((uint16_t*)0xB8000)

static int vga_row, vga_col;
static uint8_t vga_color;

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

static void vga_update_cursor(void) {
    uint16_t pos = vga_row * VGA_WIDTH + vga_col;
    outb(0x3D4, 14); outb(0x3D5, (uint8_t)(pos >> 8));
    outb(0x3D4, 15); outb(0x3D5, (uint8_t)(pos & 0xFF));
}

static void vga_scroll(void) {
    uint16_t blank = vga_entry(' ', vga_color);
    int i;
    for (i = 0; i < (VGA_HEIGHT-1)*VGA_WIDTH; i++)
        VGA_MEM[i] = VGA_MEM[i + VGA_WIDTH];
    for (i = (VGA_HEIGHT-1)*VGA_WIDTH; i < VGA_HEIGHT*VGA_WIDTH; i++)
        VGA_MEM[i] = blank;
    vga_row = VGA_HEIGHT - 1;
}

void vga_init(void) {
    vga_color = (VGA_BLACK << 4) | VGA_LIGHT_GREY;
    vga_row = 0; vga_col = 0;
    vga_clear();
}

void vga_clear(void) {
    uint16_t blank = vga_entry(' ', vga_color);
    int i;
    for (i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) VGA_MEM[i] = blank;
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

void vga_putchar(char c) {
    if (c == '\n') { vga_col = 0; vga_row++; }
    else if (c == '\r') { vga_col = 0; }
    else if (c == '\t') { vga_col = (vga_col + 8) & ~7; }
    else if (c == '\b') { if (vga_col > 0) vga_col--; }
    else {
        VGA_MEM[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_color);
        vga_col++;
    }
    if (vga_col >= VGA_WIDTH) { vga_col = 0; vga_row++; }
    if (vga_row >= VGA_HEIGHT) vga_scroll();
    vga_update_cursor();
}

void vga_puts(const char *s) {
    while (*s) vga_putchar(*s++);
    vga_flush();
}

void vga_flush(void) {
}

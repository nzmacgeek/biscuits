#pragma once
#include "../include/types.h"
#define VGA_BLACK         0
#define VGA_BLUE          1
#define VGA_GREEN         2
#define VGA_CYAN          3
#define VGA_RED           4
#define VGA_MAGENTA       5
#define VGA_BROWN         6
#define VGA_LIGHT_GREY    7
#define VGA_DARK_GREY     8
#define VGA_LIGHT_BLUE    9
#define VGA_LIGHT_GREEN   10
#define VGA_LIGHT_CYAN    11
#define VGA_LIGHT_RED     12
#define VGA_LIGHT_MAGENTA 13
#define VGA_LIGHT_BROWN   14
#define VGA_WHITE         15
#define VGA_TEXT_WIDTH    80
#define VGA_TEXT_HEIGHT   25
#define BLUEY_BLUE   VGA_LIGHT_BLUE
#define BINGO_ORANGE VGA_LIGHT_BROWN
void vga_init(void);
void vga_clear(void);
void vga_putchar(char c);
void vga_puts(const char *s);
void vga_flush(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_set_cursor(int x, int y);
void vga_write_cell(int x, int y, char c, uint8_t fg, uint8_t bg);
void vga_set_protected_rows(int rows);

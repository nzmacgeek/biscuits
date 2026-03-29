#pragma once

#include "../../include/types.h"

void dafb_init(void);
void dafb_draw_test(void);
int dafb_console_enable(void);
int dafb_console_ready(void);
int dafb_console_mirror_enabled(void);
void dafb_console_set_mirror(int enabled);
void dafb_console_clear(uint8_t bg);
void dafb_console_set_color(uint8_t fg, uint8_t bg);
void dafb_console_set_cursor(int x, int y);
void dafb_console_putchar(char c);
void dafb_show_splash(const char *arch_label, uint32_t ram_mb);
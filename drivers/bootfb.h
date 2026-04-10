#pragma once

#include "../include/types.h"

typedef struct {
    uintptr_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t type;
    uint8_t red_field_position;
    uint8_t red_mask_size;
    uint8_t green_field_position;
    uint8_t green_mask_size;
    uint8_t blue_field_position;
    uint8_t blue_mask_size;
} bootfb_mode_t;

int bootfb_init(const bootfb_mode_t *mode);
int bootfb_ready(void);
int bootfb_draw_splash(void);
void bootfb_clear_console(void);
void bootfb_sync_vga_cell(int x, int y, char c, uint8_t fg, uint8_t bg);
void bootfb_sync_vga_buffer(const uint16_t *text_buffer, int width, int height);
#pragma once

#include "../include/types.h"

#include "../drivers/bootfb.h"

void bluey_boot_show_splash(const char *arch_label, uint32_t ram_mb, const bootfb_mode_t *framebuffer);
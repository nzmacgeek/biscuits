#include "bootui.h"

#include "../drivers/vga.h"
#include "../lib/stdio.h"

void bluey_boot_show_splash(const char *arch_label, uint32_t ram_mb) {
    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
    vga_set_cursor(0, 0);

    if (arch_label && arch_label[0]) {
        kprintf("BlueyOS %s", arch_label);
        if (ram_mb != 0) {
            kprintf(" | RAM %u MB", ram_mb);
        }
        kprintf("\n\n");
    }
}
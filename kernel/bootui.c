#include "bootui.h"

#include "../drivers/vga.h"
#include "../lib/stdio.h"

#ifdef BLUEYOS_ARCH_M68K
#include "../arch/m68k/dafb.h"
#endif

void bluey_boot_show_splash(const char *arch_label, uint32_t ram_mb) {
#ifdef BLUEYOS_ARCH_M68K
    if (dafb_console_ready()) {
        dafb_show_splash(arch_label, ram_mb);
        return;
    }
#endif

    vga_set_color(VGA_WHITE, VGA_BLACK);
    vga_clear();
    vga_set_cursor(2, 2);

    kprintf("BLUEYOS\n");
    kprintf("%s\n", arch_label);
    if (ram_mb != 0) {
        kprintf("RAM %u MB\n", ram_mb);
    }
    kprintf("READY\n\n");
}
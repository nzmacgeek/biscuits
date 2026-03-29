// BlueyOS TTY - "Bingo's Chatterbox"
// Simple text console path for kernel/user communication.
#include "../include/types.h"
#include "../drivers/keyboard.h"
#include "../drivers/vga.h"
#include "../drivers/vt100.h"
#include "tty.h"

static int tty_ready = 0;
#define TTY_MAX_CHAR 0xFF

void tty_init(void) {
    vt100_init();
    vt100_set_enabled(1);
    tty_ready = 1;
}

int tty_is_ready(void) {
    return tty_ready;
}

void tty_putchar(char c) {
    if (tty_ready) vt100_putchar(c);
    else           vga_putchar(c);
}

void tty_write(const char *buf, size_t len) {
    if (!buf) return;
    for (size_t i = 0; i < len; i++) tty_putchar(buf[i]);
}

char tty_getchar(void) {
    return keyboard_getchar();
}

int tty_read(char *buf, size_t len) {
    if (!buf || len == 0) return 0;

    size_t nread = 0;
    buf[nread] = tty_getchar();
    nread++;

    while (nread < len && keyboard_available()) {
        int ch = keyboard_poll();
        if (ch < 0 || ch > TTY_MAX_CHAR) break;
        buf[nread] = (char)ch;
        nread++;
    }

    return (int)nread;
}

void tty_flush(void) {
    vga_flush();
}

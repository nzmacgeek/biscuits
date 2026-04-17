// BlueyOS TTY - "Bingo's Chatterbox"
// Simple text console path for kernel/user communication.
#include "../include/types.h"
#include "../include/ports.h"
#include "../drivers/keyboard.h"
#include "../drivers/vga.h"
#include "../drivers/vt100.h"
#include "process.h"
#include "signal.h"
#include "tty.h"
#include "../lib/string.h"

static int tty_ready = 0;
#define TTY_SERIAL_PORT 0x3F8
#define TTY_MAX_CHAR 0xFF
#define TTY_INPUT_BUF_SIZE 1024u
#define TTY_LINE_BUF_SIZE 256u

#define TTY_IFLAG_ICRNL  0x00000100u
#define TTY_OFLAG_ONLCR  0x00000004u
#define TTY_LFLAG_ISIG   0x00000001u
#define TTY_LFLAG_ICANON 0x00000002u
#define TTY_LFLAG_ECHO   0x00000008u
#define TTY_EFLAGS_IF    0x00000200u

typedef struct {
    char input_buf[TTY_INPUT_BUF_SIZE];
    uint32_t input_head;
    uint32_t input_tail;
    char line_buf[TTY_LINE_BUF_SIZE];
    uint32_t line_len;
    tty_termios_t termios;
    tty_winsize_t winsize;
    uint32_t fg_pgid;
} tty_console_t;

static tty_console_t tty_console;

static int tty_input_available(void) {
    return tty_console.input_head != tty_console.input_tail;
}

static void tty_input_push(char c) {
    uint32_t next_head = (tty_console.input_head + 1u) % TTY_INPUT_BUF_SIZE;
    if (next_head == tty_console.input_tail) return;
    tty_console.input_buf[tty_console.input_head] = c;
    tty_console.input_head = next_head;
}

static int tty_input_pop(void) {
    char c;

    if (!tty_input_available()) return -1;
    c = tty_console.input_buf[tty_console.input_tail];
    tty_console.input_tail = (tty_console.input_tail + 1u) % TTY_INPUT_BUF_SIZE;
    return (int)(unsigned char)c;
}

static void tty_serial_init(void) {
    outb(TTY_SERIAL_PORT + 1, 0x00);
    outb(TTY_SERIAL_PORT + 3, 0x80);
    outb(TTY_SERIAL_PORT + 0, 0x01);
    outb(TTY_SERIAL_PORT + 1, 0x00);
    outb(TTY_SERIAL_PORT + 3, 0x03);
    outb(TTY_SERIAL_PORT + 2, 0xC7);
    outb(TTY_SERIAL_PORT + 4, 0x0B);
}

static void tty_serial_putchar(char c) {
    while ((inb(TTY_SERIAL_PORT + 5) & 0x20u) == 0) {
    }
    outb(TTY_SERIAL_PORT, (uint8_t)c);
}

static int tty_serial_input_available(void) {
    return (inb(TTY_SERIAL_PORT + 5) & 0x01u) != 0;
}

static void tty_poll_input_sources(void) {
    while (tty_serial_input_available()) {
        tty_input_char((char)inb(TTY_SERIAL_PORT));
    }
}

static void tty_output_char(char c) {
    outb(0xE9, (uint8_t)c);
    tty_serial_putchar(c);
    if (tty_ready) vt100_putchar(c);
    else           vga_putchar(c);
}

static void tty_signal_foreground_group(int sig) {
    if (!tty_console.fg_pgid) return;
    signal_send_pgrp(tty_console.fg_pgid, sig);
}

void tty_init(void) {
    tty_serial_init();
    vt100_init();
    vt100_set_enabled(1);
    tty_console.input_head = 0;
    tty_console.input_tail = 0;
    tty_console.line_len = 0;
    tty_console.termios.c_iflag = TTY_IFLAG_ICRNL;
    tty_console.termios.c_oflag = TTY_OFLAG_ONLCR;
    tty_console.termios.c_cflag = 0;
    tty_console.termios.c_lflag = TTY_LFLAG_ISIG | TTY_LFLAG_ICANON | TTY_LFLAG_ECHO;
    tty_console.termios.c_line = 0;
    tty_console.winsize.ws_row = 25;
    tty_console.winsize.ws_col = 80;
    tty_console.winsize.ws_xpixel = 0;
    tty_console.winsize.ws_ypixel = 0;
    tty_console.fg_pgid = 0;
    tty_ready = 1;
}

int tty_is_ready(void) {
    return tty_ready;
}

void tty_putchar(char c) {
    if (c == '\n' && (tty_console.termios.c_oflag & TTY_OFLAG_ONLCR)) {
        tty_output_char('\r');
    }
    tty_output_char(c);
}

void tty_write(const char *buf, size_t len) {
    if (!buf) return;
    for (size_t i = 0; i < len; i++) tty_putchar(buf[i]);
}

char tty_getchar(void) {
    char ch;
    uint32_t flags;

    while (!tty_input_available()) {
        tty_poll_input_sources();
        if (tty_input_available()) break;
        __asm__ volatile("pushf; pop %0" : "=r"(flags) : : "memory");
        if ((flags & TTY_EFLAGS_IF) != 0u) {
            __asm__ volatile("sti; hlt" : : : "memory");
        } else {
            __asm__ volatile("sti; hlt; cli" : : : "memory");
        }
    }

    tty_poll_input_sources();
    ch = (char)tty_input_pop();
    return ch;
}

int tty_read(char *buf, size_t len) {
    if (!buf || len == 0) return 0;

    size_t nread = 0;
    while (nread < len) {
        char ch = tty_getchar();
        buf[nread++] = ch;
        if ((tty_console.termios.c_lflag & TTY_LFLAG_ICANON) && ch == '\n') break;
    }

    return (int)nread;
}

void tty_flush(void) {
    vga_flush();
}

void tty_input_char(char c) {
    if (tty_console.termios.c_iflag & TTY_IFLAG_ICRNL) {
        if (c == '\r') c = '\n';
    }

    if ((tty_console.termios.c_lflag & TTY_LFLAG_ISIG) && c == 3) {
        if (tty_console.termios.c_lflag & TTY_LFLAG_ECHO) tty_write("^C\n", 3);
        tty_console.line_len = 0;
        tty_signal_foreground_group(SIGINT);
        return;
    }

    if (tty_console.termios.c_lflag & TTY_LFLAG_ICANON) {
        if (c == '\b' || c == 127) {
            if (tty_console.line_len > 0) {
                tty_console.line_len--;
                if (tty_console.termios.c_lflag & TTY_LFLAG_ECHO) {
                    tty_write("\b \b", 3);
                }
            }
            return;
        }

        if (c == '\n') {
            for (uint32_t i = 0; i < tty_console.line_len; i++) {
                tty_input_push(tty_console.line_buf[i]);
            }
            tty_input_push('\n');
            tty_console.line_len = 0;
            if (tty_console.termios.c_lflag & TTY_LFLAG_ECHO) tty_putchar('\n');
            return;
        }

        if (tty_console.line_len + 1u < TTY_LINE_BUF_SIZE) {
            tty_console.line_buf[tty_console.line_len++] = c;
            if (tty_console.termios.c_lflag & TTY_LFLAG_ECHO) tty_putchar(c);
        }
        return;
    }

    tty_input_push(c);
    if (tty_console.termios.c_lflag & TTY_LFLAG_ECHO) tty_putchar(c);
}

int tty_device_path_kind(const char *path) {
    if (!path) return TTY_PATH_NONE;
    if (!strcmp(path, "/dev/console")) return TTY_PATH_CONSOLE;
    if (!strcmp(path, "/dev/tty")) return TTY_PATH_TTY;
    if (!strcmp(path, "/dev/tty0")) return TTY_PATH_CONSOLE;
    if (!strcmp(path, "/dev/tty1")) return TTY_PATH_CONSOLE;
    if (!strcmp(path, "/dev/ttyS0")) return TTY_PATH_CONSOLE;
    return TTY_PATH_NONE;
}

int tty_ioctl(uint32_t request, void *arg) {
    switch (request) {
        case 0x5413: {
            if (!arg) return -1;
            tty_winsize_t *ws = (tty_winsize_t*)arg;
            *ws = tty_console.winsize;
            return 0;
        }
        case 0x5401:
            if (arg) memcpy(arg, &tty_console.termios, sizeof(tty_console.termios));
            return 0;
        case 0x5402:
        case 0x5403:
        case 0x5404:
            if (arg) memcpy(&tty_console.termios, arg, sizeof(tty_console.termios));
            return 0;
        case 0x540F:
            if (!arg) return -1;
            *(uint32_t*)arg = tty_console.fg_pgid;
            return 0;
        case 0x5410:
            if (!arg) return -1;
            tty_console.fg_pgid = *(uint32_t*)arg;
            return 0;
        case 0x540E:
            return 0;
        case 0x5422:
            return 0;
        default:
            return -1;
    }
}

int tty_get_pgrp(void) {
    return (int)tty_console.fg_pgid;
}

int tty_set_pgrp(uint32_t pgid) {
    tty_console.fg_pgid = pgid;
    return 0;
}

void tty_get_termios(tty_termios_t *termios) {
    if (!termios) return;
    *termios = tty_console.termios;
}

void tty_set_termios(const tty_termios_t *termios) {
    if (!termios) return;
    tty_console.termios = *termios;
}

void tty_get_winsize(tty_winsize_t *winsize) {
    if (!winsize) return;
    *winsize = tty_console.winsize;
}

int tty_input_pending(void) {
    tty_poll_input_sources();
    return tty_input_available();
}

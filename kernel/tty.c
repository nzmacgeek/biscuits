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
#include "../lib/stdio.h"

static int tty_ready = 0;
#define TTY_SERIAL_PORT 0x3F8
#define TTY_MAX_CHAR 0xFF
#define TTY_INPUT_BUF_SIZE 1024u
#define TTY_LINE_BUF_SIZE 256u
#define NUM_VTYS 3

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

static tty_console_t tty_consoles[NUM_VTYS];
static int active_vt = 0;

/* Per-VT off-screen VGA frame buffers (80*25 cells). */
static uint16_t vt_vga_buf[NUM_VTYS][80 * 25];

/* Per-VT vt100+vga rendering contexts. */
static vt100_context_t vt_ctx[NUM_VTYS];

static void tty_console_init(tty_console_t *c) {
    c->input_head = 0;
    c->input_tail = 0;
    c->line_len   = 0;
    c->termios.c_iflag = TTY_IFLAG_ICRNL;
    c->termios.c_oflag = TTY_OFLAG_ONLCR;
    c->termios.c_cflag = 0;
    c->termios.c_lflag = TTY_LFLAG_ISIG | TTY_LFLAG_ICANON | TTY_LFLAG_ECHO;
    c->termios.c_line  = 0;
    c->winsize.ws_row    = 25;
    c->winsize.ws_col    = 80;
    c->winsize.ws_xpixel = 0;
    c->winsize.ws_ypixel = 0;
    c->fg_pgid = 0;
}

static int tty_input_available_vt(int vt) {
    return tty_consoles[vt].input_head != tty_consoles[vt].input_tail;
}

static int tty_input_available(void) {
    return tty_input_available_vt(active_vt);
}

static void tty_input_push_vt(int vt, char c) {
    tty_console_t *con = &tty_consoles[vt];
    uint32_t next_head = (con->input_head + 1u) % TTY_INPUT_BUF_SIZE;
    if (next_head == con->input_tail) return;
    con->input_buf[con->input_head] = c;
    con->input_head = next_head;
}

static void tty_input_push(char c) {
    tty_input_push_vt(active_vt, c);
}

static int tty_input_pop_vt(int vt) {
    tty_console_t *con = &tty_consoles[vt];
    char c;

    if (!tty_input_available_vt(vt)) return -1;
    c = con->input_buf[con->input_tail];
    con->input_tail = (con->input_tail + 1u) % TTY_INPUT_BUF_SIZE;
    return (int)(unsigned char)c;
}

static int tty_input_pop(void) {
    return tty_input_pop_vt(active_vt);
}

static void tty_serial_init(void) {
    outb(TTY_SERIAL_PORT + 1, 0x00);
    outb(TTY_SERIAL_PORT + 3, 0x80);
    outb(TTY_SERIAL_PORT + 0, 0x01);
    outb(TTY_SERIAL_PORT + 1, 0x00);
    outb(TTY_SERIAL_PORT + 3, 0x03);
    /* FCR = 0xC5: enable FIFO (bit0), clear TX only (bit2), 14-byte trigger (bits6-7).
     * Bit1 (clear RX) is NOT set so bytes that arrived during GRUB's countdown
     * are preserved in the FIFO for early userspace (matey) to read. */
    outb(TTY_SERIAL_PORT + 2, 0xC5);
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
        /* Serial input always goes to the active VT */
        tty_input_char((char)inb(TTY_SERIAL_PORT));
    }
}

/* Low-level output character — goes to serial (VT0 only) + VGA (active VT). */
static void tty_output_char_vt(int vt, char c) {
    if (vt == 0) {
        outb(0xE9, (uint8_t)c);  /* bochs/qemu debug port */
        tty_serial_putchar(c);
    }
    if (vt == active_vt) {
        if (tty_ready) vt100_putchar(c);
        else           vga_putchar(c);
    } else {
        /* Write to this VT's off-screen buffer with context switch. */
        uint32_t flags;
        __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
        vt100_save_context(&vt_ctx[active_vt]);
        vga_set_target(vt_vga_buf[vt]);
        vt100_restore_context(&vt_ctx[vt]);
        if (tty_ready) vt100_putchar(c);
        else           vga_putchar(c);
        vt100_save_context(&vt_ctx[vt]);
        vga_set_target(NULL);
        vt100_restore_context(&vt_ctx[active_vt]);
        __asm__ volatile("push %0; popf" : : "r"(flags) : "memory");
    }
}

static void tty_output_char(char c) {
    tty_output_char_vt(active_vt, c);
}

static void tty_signal_foreground_group_vt(int vt, int sig) {
    if (!tty_consoles[vt].fg_pgid) return;
    signal_send_pgrp(tty_consoles[vt].fg_pgid, sig);
}

static void tty_signal_foreground_group(int sig) {
    tty_signal_foreground_group_vt(active_vt, sig);
}

void tty_init(void) {
    int i;
    tty_serial_init();
    for (i = 0; i < NUM_VTYS; i++) {
        tty_console_init(&tty_consoles[i]);
        memset(vt_vga_buf[i], 0, sizeof(vt_vga_buf[i]));
        memset(&vt_ctx[i], 0, sizeof(vt_ctx[i]));
        vt_ctx[i].sgr_fg = VGA_WHITE;
        vt_ctx[i].sgr_bg = VGA_BLACK;
        vt_ctx[i].vga_color = (uint8_t)((VGA_BLACK << 4) | VGA_LIGHT_GREY);
        vt_ctx[i].vga_row = 0;
        vt_ctx[i].vga_col = 0;
    }
    vt100_init();
    vt100_set_enabled(1);
    active_vt = 0;
    tty_ready = 1;
}

int tty_is_ready(void) {
    return tty_ready;
}

/* Atomically switch the visible virtual console to new_vt. */
void tty_switch_vt(int new_vt) {
    uint32_t flags;
    if (new_vt < 0 || new_vt >= NUM_VTYS || new_vt == active_vt) return;

    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");

    /* Save active VT state and snapshot VGA_MEM into its buffer. */
    vt100_save_context(&vt_ctx[active_vt]);
    memcpy(vt_vga_buf[active_vt], (void*)0xB8000, sizeof(vt_vga_buf[0]));

    /* Switch to new VT: blit its buffer to screen, restore its context. */
    memcpy((void*)0xB8000, vt_vga_buf[new_vt], sizeof(vt_vga_buf[0]));
    vga_set_target(NULL);  /* ensure target is VGA_MEM */
    active_vt = new_vt;
    vt100_restore_context(&vt_ctx[active_vt]);

    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory");
}

int tty_get_active_vt(void) {
    return active_vt;
}

void tty_putchar(char c) {
    tty_console_t *con = &tty_consoles[active_vt];
    if (c == '\n' && (con->termios.c_oflag & TTY_OFLAG_ONLCR)) {
        tty_output_char('\r');
    }
    tty_output_char(c);
}

void tty_write(const char *buf, size_t len) {
    if (!buf) return;
    for (size_t i = 0; i < len; i++) tty_putchar(buf[i]);
}

/* Write to a specific VT (used when a background VT has process output). */
void tty_write_vt(int vt, const char *buf, size_t len) {
    if (!buf || vt < 0 || vt >= NUM_VTYS) return;
    tty_console_t *con = &tty_consoles[vt];
    for (size_t i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n' && (con->termios.c_oflag & TTY_OFLAG_ONLCR)) {
            tty_output_char_vt(vt, '\r');
        }
        tty_output_char_vt(vt, c);
    }
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

/* Non-blocking variant: polls once then returns immediately.
 * Returns 1 and sets *out if a character was available, 0 otherwise. */
int tty_getchar_nb(char *out) {
    tty_poll_input_sources();
    if (!tty_input_available()) return 0;
    *out = (char)tty_input_pop();
    return 1;
}

int tty_read(char *buf, size_t len) {
    if (!buf || len == 0) return 0;

    tty_console_t *con = &tty_consoles[active_vt];
    size_t nread = 0;
    while (nread < len) {
        char ch = tty_getchar();
        buf[nread++] = ch;
        if ((con->termios.c_lflag & TTY_LFLAG_ICANON) && ch == '\n') break;
    }

    return (int)nread;
}

/* Read from a specific VT. Active VT blocks (keyboard + serial input);
 * background VTs sleep-poll until data arrives or they become active. */
int tty_read_vt(int vt, char *buf, size_t len) {
    if (!buf || len == 0 || vt < 0 || vt >= NUM_VTYS) return 0;
    if (vt == active_vt) return tty_read(buf, len);
    /* Background VT: block until input arrives or VT becomes active. */
    tty_console_t *con = &tty_consoles[vt];
    for (;;) {
        if (vt == active_vt) return tty_read(buf, len);
        if (tty_input_available_vt(vt)) break;
        process_sleep(10);
    }
    size_t nread = 0;
    while (nread < len) {
        if (!tty_input_available_vt(vt)) break;
        char ch = (char)tty_input_pop_vt(vt);
        buf[nread++] = ch;
        if ((con->termios.c_lflag & TTY_LFLAG_ICANON) && ch == '\n') break;
    }
    return (int)nread;
}

/* Non-blocking read from a specific VT.
 * Polls serial input first (so data arriving via serial is captured).
 * Returns 0 if no input is ready, otherwise the number of bytes read. */
int tty_read_vt_nb(int vt, char *buf, size_t len) {
    if (!buf || len == 0 || vt < 0 || vt >= NUM_VTYS) return 0;
    /* Always poll serial so we don't miss data that arrived since last poll. */
    tty_poll_input_sources();
    tty_console_t *con = &tty_consoles[vt];
    size_t nread = 0;
    while (nread < len) {
        if (!tty_input_available_vt(vt)) break;
        char ch = (char)tty_input_pop_vt(vt);
        buf[nread++] = ch;
        if ((con->termios.c_lflag & TTY_LFLAG_ICANON) && ch == '\n') break;
    }
    return (int)nread;
}

/* Non-blocking read: tries to fill buf with whatever is immediately available.
 * Returns 0 if no input is ready, otherwise returns the number of bytes read. */
int tty_read_nb(char *buf, size_t len) {
    if (!buf || len == 0) return 0;

    tty_console_t *con = &tty_consoles[active_vt];
    size_t nread = 0;
    while (nread < len) {
        char ch;
        if (!tty_getchar_nb(&ch)) break;
        buf[nread++] = ch;
        if ((con->termios.c_lflag & TTY_LFLAG_ICANON) && ch == '\n') break;
    }

    return (int)nread;
}

void tty_flush(void) {
    vga_flush();
}

void tty_input_char(char c) {
    tty_console_t *con = &tty_consoles[active_vt];

    if (con->termios.c_iflag & TTY_IFLAG_ICRNL) {
        if (c == '\r') c = '\n';
    }

    if ((con->termios.c_lflag & TTY_LFLAG_ISIG) && c == 3) {
        if (con->termios.c_lflag & TTY_LFLAG_ECHO) tty_write("^C\n", 3);
        con->line_len = 0;
        tty_signal_foreground_group(SIGINT);
        return;
    }

    if (con->termios.c_lflag & TTY_LFLAG_ICANON) {
        if (c == '\b' || c == 127) {
            if (con->line_len > 0) {
                con->line_len--;
                if (con->termios.c_lflag & TTY_LFLAG_ECHO) {
                    tty_write("\b \b", 3);
                }
            }
            return;
        }

        if (c == '\n') {
            for (uint32_t i = 0; i < con->line_len; i++) {
                tty_input_push(con->line_buf[i]);
            }
            tty_input_push('\n');
            con->line_len = 0;
            if (con->termios.c_lflag & TTY_LFLAG_ECHO) tty_putchar('\n');
            return;
        }

        if (con->line_len + 1u < TTY_LINE_BUF_SIZE) {
            con->line_buf[con->line_len++] = c;
            if (con->termios.c_lflag & TTY_LFLAG_ECHO) tty_putchar(c);
        }
        return;
    }

    tty_input_push(c);
    if (con->termios.c_lflag & TTY_LFLAG_ECHO) tty_putchar(c);
}

int tty_device_path_kind(const char *path) {
    if (!path) return TTY_PATH_NONE;
    if (!strcmp(path, "/dev/console")) return TTY_PATH_CONSOLE;
    if (!strcmp(path, "/dev/tty")) return TTY_PATH_TTY;
    if (!strcmp(path, "/dev/tty0")) return TTY_PATH_CONSOLE;
    if (!strcmp(path, "/dev/tty1")) return TTY_PATH_CONSOLE;
    if (!strcmp(path, "/dev/ttyS0")) return TTY_PATH_CONSOLE;
    if (!strcmp(path, "/dev/tty2")) return TTY_PATH_VT2;
    if (!strcmp(path, "/dev/tty3")) return TTY_PATH_VT3;
    return TTY_PATH_NONE;
}

/* ioctl on a specific VT (VT index 0-based). */
int tty_ioctl_vt(int vt, uint32_t request, void *arg) {
    if (vt < 0 || vt >= NUM_VTYS) vt = 0;
    tty_console_t *con = &tty_consoles[vt];
    switch (request) {
        case 0x5413: {
            if (!arg) return -1;
            tty_winsize_t *ws = (tty_winsize_t*)arg;
            *ws = con->winsize;
            return 0;
        }
        case 0x5401:
            if (arg) memcpy(arg, &con->termios, sizeof(con->termios));
            return 0;
        case 0x5402:
        case 0x5403:
        case 0x5404:
            if (arg) memcpy(&con->termios, arg, sizeof(con->termios));
            return 0;
        case 0x540F: {  /* TIOCGPGRP */
            if (!arg) return -1;
            *(uint32_t*)arg = con->fg_pgid;
            return 0;
        }
        case 0x5410: {  /* TIOCSPGRP */
            if (!arg) return -1;
            con->fg_pgid = *(uint32_t*)arg;
            return 0;
        }
        case 0x540E:
            return 0;
        case 0x5422: {  /* TIOCSCTTY */
            process_t *p = process_current();
            if (p) {
                con->fg_pgid = p->pgid;
            }
            return 0;
        }
        default:
            return -1;
    }
}

int tty_ioctl(uint32_t request, void *arg) {
    return tty_ioctl_vt(active_vt, request, arg);
}

int tty_get_pgrp(void) {
    return (int)tty_consoles[active_vt].fg_pgid;
}

int tty_set_pgrp(uint32_t pgid) {
    tty_consoles[active_vt].fg_pgid = pgid;
    return 0;
}

void tty_get_termios(tty_termios_t *termios) {
    if (!termios) return;
    *termios = tty_consoles[active_vt].termios;
}

void tty_set_termios(const tty_termios_t *termios) {
    if (!termios) return;
    tty_consoles[active_vt].termios = *termios;
}

void tty_get_winsize(tty_winsize_t *winsize) {
    if (!winsize) return;
    *winsize = tty_consoles[active_vt].winsize;
}

int tty_input_pending(void) {
    tty_poll_input_sources();
    return tty_input_available();
}

void tty_inject_raw(const char *buf, int len) {
    if (!buf || len <= 0) return;
    for (int i = 0; i < len; i++)
        tty_input_push(buf[i]);
}


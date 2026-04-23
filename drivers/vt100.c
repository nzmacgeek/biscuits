// BlueyOS VT100 Terminal Emulator — "Bandit's TV Remote"
// "Who changed the channel?!" — Bandit Heeler
// Episode ref: "Camping" — adventure through the terminal jungle
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

#include "../include/types.h"
#include "vga.h"
#include "vt100.h"
#include "../kernel/tty.h"
#include "../lib/stdio.h"

// ---------------------------------------------------------------------------
// State machine states
// ---------------------------------------------------------------------------
typedef enum {
    VT_NORMAL = 0,    // not inside an escape sequence
    VT_ESC,           // received ESC (0x1B)
    VT_CSI,           // received ESC [  (CSI — Control Sequence Introducer)
} vt_state_t;

// ---------------------------------------------------------------------------
// Private state
// ---------------------------------------------------------------------------

#define VT_PARAM_MAX   8    // maximum CSI parameters (e.g. ESC[1;32m)
#define VT_WIDTH       80
#define VT_HEIGHT      25

static vt_state_t  vt_state   = VT_NORMAL;
static int         vt_params[VT_PARAM_MAX];
static int         vt_nparam  = 0;
static int         vt_enabled = 1;

// Current logical cursor position (0-based, clamped to VGA grid)
static int vt_cur_row = 0;
static int vt_cur_col = 0;

// Saved cursor position (ESC 7 / ESC 8)
static int vt_saved_row = 0;
static int vt_saved_col = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static inline int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// Translate an ANSI SGR colour index (30-37 / 40-47) to a VGA colour nibble.
// ANSI order: Black, Red, Green, Yellow, Blue, Magenta, Cyan, White
static const uint8_t ansi_to_vga[8] = {
    VGA_BLACK,        // 0: Black
    VGA_RED,          // 1: Red
    VGA_GREEN,        // 2: Green
    VGA_BROWN,        // 3: Yellow/Brown
    VGA_BLUE,         // 4: Blue
    VGA_MAGENTA,      // 5: Magenta
    VGA_CYAN,         // 6: Cyan
    VGA_LIGHT_GREY,   // 7: White
};

// Current SGR state
static uint8_t sgr_fg = VGA_WHITE;
static uint8_t sgr_bg = VGA_BLACK;
static int     sgr_bold = 0;

static void apply_sgr(void) {
    uint8_t fg = sgr_bold ? (sgr_fg | 0x08) : sgr_fg;
    vga_set_color(fg, sgr_bg);
}

// Write a single space at an absolute (row, col) position in the VGA buffer
// using the current SGR-derived colours. Does not move the cursor or trigger
// scrolling.
static inline void vga_write_space_at(int row, int col) {
    if (row < 0 || row >= VT_HEIGHT || col < 0 || col >= VT_WIDTH) {
        return;
    }
    uint8_t fg = sgr_bold ? (sgr_fg | 0x08) : sgr_fg;
    vga_write_cell(col, row, ' ', fg, sgr_bg);
}

// Erase from (r0,c0) inclusive to (r1,c1) exclusive with current background.
// Writes spaces directly to the VGA buffer so erase operations do not disturb
// cursor state or trigger scrolling.
static void vga_erase_region(int r0, int c0, int r1, int c1) {
    r0 = clamp(r0, 0, VT_HEIGHT);
    r1 = clamp(r1, 0, VT_HEIGHT);
    c0 = clamp(c0, 0, VT_WIDTH);
    c1 = clamp(c1, 0, VT_WIDTH);

    if (r1 <= r0) return;

    for (int r = r0; r < r1; r++) {
        int cs = (r == r0) ? c0 : 0;
        int ce = (r == r1 - 1) ? c1 : VT_WIDTH;
        for (int c = cs; c < ce; c++) {
            vga_write_space_at(r, c);
        }
    }
}

// ---------------------------------------------------------------------------
// CSI final-byte handler
// ---------------------------------------------------------------------------

static void vt_handle_csi(char cmd) {
    int p0 = (vt_nparam > 0) ? vt_params[0] : 0;
    int p1 = (vt_nparam > 1) ? vt_params[1] : 0;

    switch (cmd) {

        /* --- Cursor Movement ----------------------------------------------- */

        case 'A':   // Cursor Up
            vt_cur_row = clamp(vt_cur_row - (p0 ? p0 : 1), 0, VT_HEIGHT - 1);
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        case 'B':   // Cursor Down
            vt_cur_row = clamp(vt_cur_row + (p0 ? p0 : 1), 0, VT_HEIGHT - 1);
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        case 'C':   // Cursor Forward (right)
            vt_cur_col = clamp(vt_cur_col + (p0 ? p0 : 1), 0, VT_WIDTH - 1);
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        case 'D':   // Cursor Back (left)
            vt_cur_col = clamp(vt_cur_col - (p0 ? p0 : 1), 0, VT_WIDTH - 1);
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        case 'E':   // Cursor Next Line
            vt_cur_row = clamp(vt_cur_row + (p0 ? p0 : 1), 0, VT_HEIGHT - 1);
            vt_cur_col = 0;
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        case 'F':   // Cursor Previous Line
            vt_cur_row = clamp(vt_cur_row - (p0 ? p0 : 1), 0, VT_HEIGHT - 1);
            vt_cur_col = 0;
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        case 'G':   // Cursor Horizontal Absolute (column, 1-based)
            vt_cur_col = clamp((p0 ? p0 : 1) - 1, 0, VT_WIDTH - 1);
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        case 'H':   // Cursor Position  ESC [ row ; col H
        case 'f':   // same as H
            vt_cur_row = clamp((p0 ? p0 : 1) - 1, 0, VT_HEIGHT - 1);
            vt_cur_col = clamp((p1 ? p1 : 1) - 1, 0, VT_WIDTH  - 1);
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        /* --- Erase --------------------------------------------------------- */

        case 'J':   // Erase in Display
            switch (p0) {
                case 0:   // cursor to end of screen
                    vga_erase_region(vt_cur_row, vt_cur_col,
                                     VT_HEIGHT, VT_WIDTH);
                    break;
                case 1:   // beginning of screen to cursor
                    vga_erase_region(0, 0, vt_cur_row, vt_cur_col + 1);
                    break;
                case 2:   // whole screen — same as clear
                    vga_clear();
                    vt_cur_row = 0; vt_cur_col = 0;
                    break;
                default: break;
            }
            break;

        case 'K':   // Erase in Line
            switch (p0) {
                case 0:   // cursor to end of line
                    vga_erase_region(vt_cur_row, vt_cur_col,
                                     vt_cur_row + 1, VT_WIDTH);
                    break;
                case 1:   // beginning of line to cursor
                    vga_erase_region(vt_cur_row, 0,
                                     vt_cur_row + 1, vt_cur_col + 1);
                    break;
                case 2:   // entire line
                    vga_erase_region(vt_cur_row, 0,
                                     vt_cur_row + 1, VT_WIDTH);
                    break;
                default: break;
            }
            break;

        /* --- SGR — Select Graphic Rendition -------------------------------- */

        case 'm':
            if (vt_nparam == 0) {
                // ESC [ m == ESC [ 0 m == reset
                sgr_fg   = VGA_WHITE;
                sgr_bg   = VGA_BLACK;
                sgr_bold = 0;
                apply_sgr();
                break;
            }
            for (int i = 0; i < vt_nparam; i++) {
                int v = vt_params[i];
                if (v == 0) {
                    sgr_fg = VGA_WHITE; sgr_bg = VGA_BLACK; sgr_bold = 0;
                } else if (v == 1) {
                    sgr_bold = 1;
                } else if (v == 22) {
                    sgr_bold = 0;
                } else if (v >= 30 && v <= 37) {
                    sgr_fg = ansi_to_vga[v - 30];
                } else if (v == 39) {
                    sgr_fg = VGA_WHITE;   // default foreground
                } else if (v >= 40 && v <= 47) {
                    sgr_bg = ansi_to_vga[v - 40];
                } else if (v == 49) {
                    sgr_bg = VGA_BLACK;   // default background
                } else if (v >= 90 && v <= 97) {
                    // Bright (high-intensity) foreground
                    sgr_fg = ansi_to_vga[v - 90] | 0x08;
                } else if (v >= 100 && v <= 107) {
                    // Bright background (VGA doesn't really support it — map anyway)
                    sgr_bg = ansi_to_vga[v - 100];
                }
            }
            apply_sgr();
            break;

        /* --- DSR — Device Status Report ------------------------------------ */

        case 'n':
            // ESC[6n — report cursor position as ESC[row;colR (1-based).
            // Only inject when ICANON is disabled (raw/readline mode).  When
            // ICANON is active the terminal is in cooked login mode and injecting
            // CPR bytes would corrupt the username read by matey.
            if (p0 == 6) {
                tty_termios_t cur_termios;
                tty_get_termios(&cur_termios);
                int row = vt_cur_row + 1;   /* 1-based */
                int col = vt_cur_col + 1;   /* 1-based */
                kprintf("[VT100] ESC[6n rcvd row=%d col=%d icanon=%d\n",
                        row, col,
                        (cur_termios.c_lflag & TTY_LFLAG_ICANON) ? 1 : 0);
                if (!(cur_termios.c_lflag & TTY_LFLAG_ICANON)) {
                    char resp[16];
                    int pos = 0;

                    resp[pos++] = '\x1b';
                    resp[pos++] = '[';
                    if (row >= 10) resp[pos++] = '0' + (row / 10);
                    resp[pos++] = '0' + (row % 10);
                    resp[pos++] = ';';
                    if (col >= 10) resp[pos++] = '0' + (col / 10);
                    resp[pos++] = '0' + (col % 10);
                    resp[pos++] = 'R';
                    tty_inject_raw(resp, pos);
                }
            }
            break;

        /* --- Scroll -------------------------------------------------------- */

        case 'S':   // Scroll Up by n lines (no-op stub)
        case 'T':   // Scroll Down by n lines (no-op stub)
            break;

        /* --- Misc ---------------------------------------------------------- */

        case 's':   // Save cursor
            vt_saved_row = vt_cur_row;
            vt_saved_col = vt_cur_col;
            break;

        case 'u':   // Restore cursor
            vt_cur_row = vt_saved_row;
            vt_cur_col = vt_saved_col;
            vga_set_cursor(vt_cur_col, vt_cur_row);
            break;

        default:
            // Unknown CSI sequence — silently discard.
            break;
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void vt100_init(void) {
    vt_state   = VT_NORMAL;
    vt_nparam  = 0;
    vt_cur_row = 0;
    vt_cur_col = 0;
    vt_enabled = 1;
    sgr_fg     = VGA_WHITE;
    sgr_bg     = VGA_BLACK;
    sgr_bold   = 0;
}

void vt100_set_enabled(int enabled) {
    vt_enabled = enabled;
}

void vt100_putchar(char c) {
    if (!vt_enabled) {
        vga_putchar(c);
        return;
    }

    switch (vt_state) {

        case VT_NORMAL:
            if (c == '\x1B') {          // ESC
                vt_state  = VT_ESC;
            } else if (c == '\a') {     // BEL (terminal bell)
                /* Beep is emitted on the serial/debug output path.
                 * Keep BEL non-printing on the VGA terminal. */
            } else if (c == '\n' || c == '\r' || c == '\b' || c == '\t' ||
                       (((uint8_t)c) >= 0x20 && c != 0x7F)) {
                // Printable or supported control character — pass to VGA
                vga_putchar(c);
                // Track cursor position
                if (c == '\n') {
                    vt_cur_col = 0;
                    vt_cur_row++;
                    if (vt_cur_row >= VT_HEIGHT) vt_cur_row = VT_HEIGHT - 1;
                } else if (c == '\r') {
                    vt_cur_col = 0;
                } else if (c == '\b') {
                    if (vt_cur_col > 0) vt_cur_col--;
                } else if (c == '\t') {
                    vt_cur_col = (vt_cur_col + 8) & ~7;
                    if (vt_cur_col >= VT_WIDTH) {
                        vt_cur_col = 0;
                        vt_cur_row++;
                        if (vt_cur_row >= VT_HEIGHT) vt_cur_row = VT_HEIGHT - 1;
                    }
                } else {
                    vt_cur_col++;
                    if (vt_cur_col >= VT_WIDTH) {
                        vt_cur_col = 0;
                        vt_cur_row++;
                        if (vt_cur_row >= VT_HEIGHT) vt_cur_row = VT_HEIGHT - 1;
                    }
                }
            } else {
                // Other C0 controls are non-printing in terminal mode.
            }
            break;

        case VT_ESC:
            if (c == '[') {
                // CSI — begin collecting parameters
                vt_state  = VT_CSI;
                vt_nparam = 0;
                for (int i = 0; i < VT_PARAM_MAX; i++) vt_params[i] = 0;
            } else if (c == 'c') {
                // RIS — Reset to Initial State
                vga_clear();
                vga_set_color(VGA_WHITE, VGA_BLACK);
                vt_cur_row = 0; vt_cur_col = 0;
                sgr_fg = VGA_WHITE; sgr_bg = VGA_BLACK; sgr_bold = 0;
                vt_state = VT_NORMAL;
            } else if (c == '7') {
                // DECSC — save cursor
                vt_saved_row = vt_cur_row;
                vt_saved_col = vt_cur_col;
                vt_state = VT_NORMAL;
            } else if (c == '8') {
                // DECRC — restore cursor
                vt_cur_row = vt_saved_row;
                vt_cur_col = vt_saved_col;
                vga_set_cursor(vt_cur_col, vt_cur_row);
                vt_state = VT_NORMAL;
            } else {
                // Unknown ESC sequence — discard and return to normal
                vt_state = VT_NORMAL;
            }
            break;

        case VT_CSI:
            if (c >= '0' && c <= '9') {
                // Accumulate numeric parameter
                if (vt_nparam == 0) vt_nparam = 1;
                vt_params[vt_nparam - 1] =
                    vt_params[vt_nparam - 1] * 10 + (c - '0');
            } else if (c == ';') {
                // Parameter separator
                if (vt_nparam < VT_PARAM_MAX) vt_nparam++;
            } else if (c == '?' || c == '>' || c == '=') {
                // Private / DEC parameters — absorb prefix, still collect digits
            } else {
                // Final byte — execute and return to normal state
                // (Adjust nparam: if we had at least one digit, count is correct;
                //  if the first char was ';', nparam might be 0.)
                if (vt_nparam == 0 && (c != 'm')) vt_nparam = 0;
                vt_handle_csi(c);
                vt_state = VT_NORMAL;
            }
            break;
    }
}

void vt100_puts(const char *s) {
    while (*s) vt100_putchar(*s++);
}

void vt100_write(const char *buf, int n) {
    for (int i = 0; i < n; i++) vt100_putchar(buf[i]);
}

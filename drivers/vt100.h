#pragma once
// BlueyOS VT100 Terminal Emulator — "Bandit's TV Remote"
// "Who changed the channel?!" — Bandit Heeler
// Episode ref: "Camping" — the Heeler kids discover what channels really mean
//
// This driver sits between the VGA driver and any higher-level output.
// It parses a subset of VT100/ANSI escape sequences so that bash (and
// readline) can control cursor position and colours on the VGA text display.
//
// Supported sequences:
//   ESC [ 2 J            — erase display (clear screen)
//   ESC [ H              — cursor home (1,1)
//   ESC [ row ; col H    — set cursor position (1-based)
//   ESC [ n A/B/C/D      — cursor up/down/forward/back by n
//   ESC [ n J            — erase in display (0=to end, 1=to start, 2=all)
//   ESC [ n K            — erase in line   (0=to end, 1=to start, 2=all)
//   ESC [ n m            — SGR: colours & attributes (bold, reset, fg, bg)
//   ESC [ 6 n            — DSR: report cursor position as ESC [ row ; col R
//   ESC c                — full reset (RIS)
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

// Initialise the VT100 emulator (call after vga_init).
void vt100_init(void);

// Process a single character through the VT100 state machine.
// Printable characters are written directly to VGA; escape sequences
// are decoded and translated to VGA driver calls.
void vt100_putchar(char c);

// Process a NUL-terminated string.
void vt100_puts(const char *s);

// Feed n bytes from a buffer through the emulator.
void vt100_write(const char *buf, int n);

// Enable or disable the VT100 layer.  When disabled, all output goes
// straight to vga_putchar() without escape-sequence processing.
// Default: enabled.
void vt100_set_enabled(int enabled);

/* Saved VT100+VGA state for a single virtual console. */
typedef struct {
    /* vt100 state machine */
    int     vt_state;
    int     vt_params[8];
    int     vt_nparam;
    int     vt_cur_row, vt_cur_col;
    int     vt_saved_row, vt_saved_col;
    uint8_t sgr_fg, sgr_bg;
    int     sgr_bold;
    /* vga driver state */
    int     vga_row, vga_col;
    uint8_t vga_color;
    int     vga_protected_rows;
} vt100_context_t;

/* Save / restore the complete vt100+vga rendering state. */
void vt100_save_context(vt100_context_t *ctx);
void vt100_restore_context(const vt100_context_t *ctx);

// BlueyOS Keyboard Driver - "Bingo's Keyboard - Tap tap tap!"
// Episode ref: "Stories" - Bingo types all her best stories
// Circular ring buffer (256 chars) with hard bounds checking.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/ports.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "keyboard.h"
#include "../kernel/irq.h"
#include "../kernel/tty.h"
#include "../kernel/devev.h"

#define KB_DATA_PORT  0x60
#define KB_BUF_SIZE   256  // must be power of 2

// Ring buffer - bounds hard-enforced
static volatile char kb_buf[KB_BUF_SIZE];
static volatile uint32_t kb_head = 0;  // write index
static volatile uint32_t kb_tail = 0;  // read index

// US QWERTY scancode map (set 1 make codes 0x00-0x58)
// Index = scancode, value = ASCII char (0 = no mapping)
static const char scancode_map_lower[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',  '\b',
    '\t','q',  'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a',  's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',  0,
    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0
};

static const char scancode_map_upper[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',  '\b',
    '\t','Q',  'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A',  'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|',  'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',  0,
    '*', 0,    ' ', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,   0,   0,   0,   '-', 0,   0,   0,   '+', 0,   0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0
};

static int shift_held  = 0;
static int caps_lock   = 0;
static int ctrl_held   = 0;
static int alt_held    = 0;
static int e0_prefix   = 0;
static int cad_latched = 0;

static void keyboard_emit_cad_event(void) {
    devev_event_t ev = {
        .type = DEV_EV_CTRL_ALT_DEL,
        ._pad = {0, 0, 0},
        .pid = 0,
        .code = 0,
        .reserved = 0,
    };
    devev_push(&ev);
    kprintf("[KBD] Ctrl+Alt+Del detected - notifying PID 1 via device event\n");
}

static void kb_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t sc = inb(KB_DATA_PORT);

    if (sc == 0xE0) {
        e0_prefix = 1;
        return;
    }

    int release = (sc & 0x80) != 0;
    uint8_t code = sc & 0x7F;
    int extended = e0_prefix;
    e0_prefix = 0;

    // Key release (bit 7 set)
    if (release) {
        if (code == 0x2A || code == 0x36) shift_held = 0;  // left/right shift up
        if (code == 0x1D) {                                 // left/right ctrl up
            ctrl_held = 0;
            cad_latched = 0;
        }
        if (code == 0x38) {                                 // left/right alt up
            alt_held = 0;
            cad_latched = 0;
        }
        if (extended && code == 0x53) cad_latched = 0;     // delete released
        return;
    }

    // Key press
    if (code == 0x2A || code == 0x36) { shift_held = 1; return; }  // shift down
    if (code == 0x1D) { ctrl_held = 1; return; }                   // ctrl down
    if (code == 0x38) { alt_held = 1; return; }                    // alt down
    if (!extended && code == 0x3A) { caps_lock ^= 1; return; }     // caps lock toggle

    if (extended && code == 0x53) {
        if (ctrl_held && alt_held && !cad_latched) {
            keyboard_emit_cad_event();
            cad_latched = 1;
        }
        return;
    }

    if (extended || code >= 128) return;

    int upper = shift_held ^ caps_lock;
    char c = upper ? scancode_map_upper[code] : scancode_map_lower[code];
    if (!c) return;

    // Write to ring buffer with bounds check - drop if full
    uint32_t next_head = (kb_head + 1) & (KB_BUF_SIZE - 1);
    if (next_head != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next_head;
    }
    tty_input_char(c);
    // If buffer is full, key is silently dropped - "Bingo, slow down!" - Bandit
}

void keyboard_init(void) {
    kb_head = kb_tail = 0;
    shift_held = 0;
    caps_lock = 0;
    ctrl_held = 0;
    alt_held = 0;
    e0_prefix = 0;
    cad_latched = 0;
    irq_install_handler(1, kb_irq_handler);
    kprintf("%s\n", MSG_KB_INIT);
}

int keyboard_available(void) {
    return kb_head != kb_tail;
}

int keyboard_poll(void) {
    if (!keyboard_available()) return -1;
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) & (KB_BUF_SIZE - 1);
    return (int)(unsigned char)c;
}

char keyboard_getchar(void) {
    while (!keyboard_available())
        __asm__ volatile("hlt");
    return (char)keyboard_poll();
}

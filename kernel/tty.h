#pragma once
#include "../include/types.h"

#define TTY_NCCS 19

typedef struct {
	uint32_t c_iflag;
	uint32_t c_oflag;
	uint32_t c_cflag;
	uint32_t c_lflag;
	uint8_t  c_line;
	uint8_t  c_cc[TTY_NCCS];
} tty_termios_t;

typedef struct {
	uint16_t ws_row;
	uint16_t ws_col;
	uint16_t ws_xpixel;
	uint16_t ws_ypixel;
} tty_winsize_t;

enum {
	TTY_PATH_NONE = 0,
	TTY_PATH_CONSOLE = 1,
	TTY_PATH_TTY = 2,
};

void tty_init(void);
int  tty_is_ready(void);
void tty_putchar(char c);
void tty_write(const char *buf, size_t len);
char tty_getchar(void);
int  tty_read(char *buf, size_t len);
void tty_flush(void);
void tty_input_char(char c);
int  tty_device_path_kind(const char *path);
int  tty_ioctl(uint32_t request, void *arg);
int  tty_get_pgrp(void);
int  tty_set_pgrp(uint32_t pgid);
void tty_get_termios(tty_termios_t *termios);
void tty_set_termios(const tty_termios_t *termios);
void tty_get_winsize(tty_winsize_t *winsize);

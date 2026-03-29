#pragma once
#include "../include/types.h"

void tty_init(void);
int  tty_is_ready(void);
void tty_putchar(char c);
void tty_write(const char *buf, size_t len);
char tty_getchar(void);
int  tty_read(char *buf, size_t len);
void tty_flush(void);

// BlueyOS stdio - kprintf backed by VGA
// "Dad, can you read me a story?" - Bluey  "Sure, but first let me format it." - Bandit
#include "../include/types.h"
#include "../drivers/vga.h"
#include "string.h"

// variadic args without stdarg.h
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,t)   __builtin_va_arg(v,t)

static void kprintf_puts(const char *s) { if (s) { while (*s) vga_putchar(*s++); } }
static void kprintf_putc(char c) { vga_putchar(c); }

static void kprintf_uint(unsigned int v, int base, int upper) {
    char buf[32]; int i = 31; buf[31] = 0;
    if (v == 0) { kprintf_putc('0'); return; }
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    while (v) { buf[--i] = digits[v % base]; v /= base; }
    kprintf_puts(buf + i);
}

void kprintf(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (*fmt != '%') { kprintf_putc(*fmt); continue; }
        fmt++;
        switch (*fmt) {
            case 's': kprintf_puts(va_arg(ap, const char*)); break;
            case 'c': kprintf_putc((char)va_arg(ap, int)); break;
            case 'd': case 'i': {
                int v = va_arg(ap, int);
                if (v < 0) { kprintf_putc('-'); kprintf_uint((unsigned)-v, 10, 0); }
                else kprintf_uint((unsigned)v, 10, 0);
                break;
            }
            case 'u': kprintf_uint(va_arg(ap, unsigned), 10, 0); break;
            case 'x': kprintf_uint(va_arg(ap, unsigned), 16, 0); break;
            case 'X': kprintf_uint(va_arg(ap, unsigned), 16, 1); break;
            case 'p': kprintf_puts("0x"); kprintf_uint(va_arg(ap, unsigned), 16, 0); break;
            case '%': kprintf_putc('%'); break;
            default: kprintf_putc('%'); kprintf_putc(*fmt); break;
        }
    }
    va_end(ap);
}

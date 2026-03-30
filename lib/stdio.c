// BlueyOS stdio - kprintf backed by VGA
// "Dad, can you read me a story?" - Bluey  "Sure, but first let me format it." - Bandit
#include "stdio.h"
#if defined(BLUEYOS_ARCH_I386)
#include "../kernel/tty.h"
#elif defined(BLUEYOS_ARCH_PPC)
#include "../arch/ppc/console.h"
#elif defined(BLUEYOS_ARCH_M68K)
/* arch/m68k/platform.c provides the vga_* console shim on this port. */
#include "../drivers/vga.h"
#else
#error "kprintf backend is not defined for this architecture"
#endif
#include "string.h"

// variadic args without stdarg.h
typedef __builtin_va_list va_list;
#define va_start(v,l) __builtin_va_start(v,l)
#define va_end(v)     __builtin_va_end(v)
#define va_arg(v,t)   __builtin_va_arg(v,t)

typedef void (*kprintf_emit_fn_t)(char c, void *ctx);

static kprintf_output_state_t kprintf_output_state = { 0, 0 };

static void kprintf_backend_putc(char c) {
#if defined(BLUEYOS_ARCH_I386)
    tty_putchar(c);
#elif defined(BLUEYOS_ARCH_PPC)
    ppc_console_putchar(c);
#else
    vga_putchar(c);
#endif
}

static void kprintf_backend_flush(void) {
#if defined(BLUEYOS_ARCH_I386)
    tty_flush();
#elif defined(BLUEYOS_ARCH_PPC)
    ppc_console_flush();
#else
    vga_flush();
#endif
}

static void kprintf_emit_backend(char c, void *ctx) {
    (void)ctx;
    kprintf_backend_putc(c);
}

static void kprintf_emit_hooked(char c, void *ctx) {
    (void)ctx;
    if (kprintf_output_state.hook) {
        kprintf_output_state.hook(c, kprintf_output_state.ctx);
        return;
    }
    kprintf_backend_putc(c);
}

static void kprintf_emit_repeat(kprintf_emit_fn_t emit, void *ctx, char c, int count) {
    while (count-- > 0) emit(c, ctx);
}

static int kprintf_parse_number(const char **fmt) {
    int value = 0;
    while (**fmt >= '0' && **fmt <= '9') {
        value = (value * 10) + (**fmt - '0');
        (*fmt)++;
    }
    return value;
}

static int kprintf_parse_length(const char **fmt) {
    if (**fmt == 'h') {
        (*fmt)++;
        if (**fmt == 'h') {
            (*fmt)++;
            return 1;
        }
        return 2;
    }
    if (**fmt == 'l') {
        (*fmt)++;
        if (**fmt == 'l') {
            (*fmt)++;
            /* ll (long long) is not supported; treat as l (long) to avoid
             * silently reading the wrong argument width. */
        }
        return 3;
    }
    if (**fmt == 'z') {
        (*fmt)++;
        return 5;
    }
    return 0;
}

static int kprintf_format_unsigned(char *buf, unsigned long value, int base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int len = 0;

    if (value == 0) {
        buf[len++] = '0';
        return len;
    }

    while (value && len < 32) {
        buf[len++] = digits[value % (unsigned)base];
        value /= (unsigned)base;
    }
    return len;
}

static void kprintf_emit_string(kprintf_emit_fn_t emit, void *ctx,
                                const char *str, int width, int precision,
                                int left_justify, char pad_char) {
    int len = 0;
    const char *s = str ? str : "(null)";

    while (s[len] && (precision < 0 || len < precision)) len++;
    if (!left_justify && width > len) {
        kprintf_emit_repeat(emit, ctx, pad_char, width - len);
    }
    for (int i = 0; i < len; i++) emit(s[i], ctx);
    if (left_justify && width > len) {
        kprintf_emit_repeat(emit, ctx, ' ', width - len);
    }
}

static unsigned long kprintf_read_unsigned_arg(va_list *ap, int length) {
    switch (length) {
        case 1: return (unsigned char)va_arg(*ap, unsigned int);
        case 2: return (unsigned short)va_arg(*ap, unsigned int);
        case 3: return va_arg(*ap, unsigned long);
        case 5: return va_arg(*ap, size_t);
        default: return va_arg(*ap, unsigned int);
    }
}

static long kprintf_read_signed_arg(va_list *ap, int length) {
    switch (length) {
        case 1: return (signed char)va_arg(*ap, int);
        case 2: return (short)va_arg(*ap, int);
        case 3: return va_arg(*ap, long);
        case 5: return (long)va_arg(*ap, size_t);
        default: return va_arg(*ap, int);
    }
}

static void kprintf_emit_number(kprintf_emit_fn_t emit, void *ctx,
                                unsigned long value, int negative,
                                int base, int upper, int width,
                                int precision, int left_justify,
                                int zero_pad, int alternate_form,
                                char spec) {
    char digits[32];
    const char *prefix = "";
    int digit_len = 0;
    int prefix_len = 0;
    int zeroes = 0;
    int total_len = 0;
    char sign_char = 0;
    char pad_char = (zero_pad && !left_justify && precision < 0) ? '0' : ' ';

    if (negative) sign_char = '-';
    if (alternate_form && (value != 0 || spec == 'p')) {
        if (spec == 'x' || spec == 'p') prefix = "0x";
        else if (spec == 'X') prefix = "0X";
    }
    prefix_len = (int)strlen(prefix);

    if (precision == 0 && value == 0) {
        digit_len = 0;
    } else {
        digit_len = kprintf_format_unsigned(digits, value, base, upper);
    }

    if (precision > digit_len) zeroes = precision - digit_len;
    total_len = digit_len + zeroes + prefix_len + (sign_char ? 1 : 0);

    if (pad_char == '0' && width > total_len) {
        zeroes += width - total_len;
        total_len = width;
    }

    if (!left_justify && width > total_len) {
        kprintf_emit_repeat(emit, ctx, ' ', width - total_len);
    }
    if (sign_char) emit(sign_char, ctx);
    for (int i = 0; i < prefix_len; i++) emit(prefix[i], ctx);
    kprintf_emit_repeat(emit, ctx, '0', zeroes);
    for (int i = digit_len - 1; i >= 0; i--) emit(digits[i], ctx);
    if (left_justify && width > total_len) {
        kprintf_emit_repeat(emit, ctx, ' ', width - total_len);
    }
}

static void kvprintf_impl(kprintf_emit_fn_t emit, void *ctx, const char *fmt, va_list ap) {
    for (; *fmt; fmt++) {
        int left_justify = 0;
        int zero_pad = 0;
        int alternate_form = 0;
        int width = 0;
        int precision = -1;
        int length = 0;

        if (*fmt != '%') {
            emit(*fmt, ctx);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            emit('%', ctx);
            continue;
        }

        while (*fmt == '-' || *fmt == '0' || *fmt == '#') {
            if (*fmt == '-') left_justify = 1;
            else if (*fmt == '0') zero_pad = 1;
            else if (*fmt == '#') alternate_form = 1;
            fmt++;
        }

        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left_justify = 1;
                width = -width;
            }
            fmt++;
        } else {
            width = kprintf_parse_number(&fmt);
        }

        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                precision = va_arg(ap, int);
                if (precision < 0) precision = -1;
                fmt++;
            } else {
                precision = kprintf_parse_number(&fmt);
            }
            zero_pad = 0;
        }

        length = kprintf_parse_length(&fmt);

        switch (*fmt) {
            case 's':
                kprintf_emit_string(emit, ctx, va_arg(ap, const char *), width,
                                    precision, left_justify, ' ');
                break;
            case 'c': {
                char ch = (char)va_arg(ap, int);
                if (!left_justify && width > 1) kprintf_emit_repeat(emit, ctx, ' ', width - 1);
                emit(ch, ctx);
                if (left_justify && width > 1) kprintf_emit_repeat(emit, ctx, ' ', width - 1);
                break;
            }
            case 'd':
            case 'i': {
                long value = kprintf_read_signed_arg(&ap, length);
                unsigned long magnitude = (value < 0)
                    ? ((unsigned long)(-(value + 1)) + 1UL)
                    : (unsigned long)value;
                kprintf_emit_number(emit, ctx, magnitude, value < 0, 10, 0,
                                    width, precision, left_justify, zero_pad,
                                    0, *fmt);
                break;
            }
            case 'u':
                kprintf_emit_number(emit, ctx, kprintf_read_unsigned_arg(&ap, length),
                                    0, 10, 0, width, precision, left_justify,
                                    zero_pad, 0, *fmt);
                break;
            case 'x':
            case 'X':
                kprintf_emit_number(emit, ctx, kprintf_read_unsigned_arg(&ap, length),
                                    0, 16, *fmt == 'X', width, precision,
                                    left_justify, zero_pad, alternate_form, *fmt);
                break;
            case 'p': {
                unsigned long value = (uintptr_t)va_arg(ap, void *);
                if (precision < 0) precision = (int)(sizeof(uintptr_t) * 2u);
                kprintf_emit_number(emit, ctx, value, 0, 16, 0, width,
                                    precision, left_justify, 0, 1, 'p');
                break;
            }
            default:
                emit('%', ctx);
                emit(*fmt, ctx);
                break;
        }
    }
}

void kprintf_set_output_hook(kprintf_output_hook_t hook, void *ctx) {
    kprintf_output_state.hook = hook;
    kprintf_output_state.ctx = ctx;
}

kprintf_output_state_t kprintf_get_output_state(void) {
    return kprintf_output_state;
}

void kprintf_restore_output_state(kprintf_output_state_t state) {
    kprintf_output_state = state;
}

void kprintf_direct(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf_impl(kprintf_emit_backend, 0, fmt, ap);
    va_end(ap);
    kprintf_backend_flush();
}

void kprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    kvprintf_impl(kprintf_emit_hooked, 0, fmt, ap);
    va_end(ap);
    if (!kprintf_output_state.hook) kprintf_backend_flush();
}

// BlueyOS string library - "Words are important!" - Bluey
// Episode ref: "Stories" - Bluey loves a good story, we love good strings
#include "../include/types.h"
#include "string.h"

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int strncmp(const char *a, const char *b, size_t n) {
    size_t i = 0;
    while (i < n && a[i] && a[i] == b[i]) i++;
    if (i == n) return 0;
    return (unsigned char)a[i] - (unsigned char)b[i];
}
char *strcpy(char *dst, const char *src) {
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}
char *strncpy(char *dst, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}
char *strcat(char *dst, const char *src) {
    char *d = dst + strlen(dst);
    while ((*d++ = *src++));
    return dst;
}
char *strncat(char *dst, const char *src, size_t n) {
    char *d = dst + strlen(dst);
    while (n-- && *src) *d++ = *src++;
    *d = '\0';
    return dst;
}
char *strchr(const char *s, int c) {
    while (*s) { if (*s == (char)c) return (char*)s; s++; }
    return (c == 0) ? (char*)s : NULL;
}
char *strrchr(const char *s, int c) {
    const char *last = NULL;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char*)last;
}
char *strstr(const char *h, const char *n) {
    if (!*n) return (char*)h;
    for (; *h; h++) {
        const char *p = h, *q = n;
        while (*p && *q && *p == *q) { p++; q++; }
        if (!*q) return (char*)h;
    }
    return NULL;
}
void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}
void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst; const uint8_t *s = src;
    if (d < s) { while (n--) *d++ = *s++; }
    else { d += n; s += n; while (n--) *--d = *--s; }
    return dst;
}
int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *p = a, *q = b;
    while (n--) { if (*p != *q) return *p - *q; p++; q++; }
    return 0;
}
char *itoa(int val, char *buf, int base) {
    char tmp[32]; int i = 0, neg = 0;
    static char empty[1] = { 0 };
    if (!buf) return empty;
    /* Validate base to avoid divide-by-zero or infinite loop */
    if (base < 2 || base > 16) { buf[0] = '0'; buf[1] = 0; return buf; }
    if (val == 0) { buf[0]='0'; buf[1]=0; return buf; }
    /* Handle INT_MIN safely by working in unsigned arithmetic throughout */
    unsigned int uval;
    if (val < 0 && base == 10) {
        neg = 1;
        /* Cast through unsigned to avoid negating INT_MIN (UB) */
        uval = (unsigned int)(-(val + 1)) + 1u;
    } else {
        uval = (unsigned int)val;
    }
    while (uval) {
        int r = (int)(uval % (unsigned int)base);
        tmp[i++] = (r < 10) ? ('0'+r) : ('a'+r-10);
        uval /= (unsigned int)base;
    }
    if (neg) tmp[i++] = '-';
    int j = 0;
    while (i--) buf[j++] = tmp[i];
    buf[j] = 0;
    return buf;
}
int atoi(const char *s) {
    int r = 0, neg = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') r = r*10 + (*s++ - '0');
    return neg ? -r : r;
}

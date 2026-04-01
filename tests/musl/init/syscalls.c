#include <stddef.h>

long syscall0(long n) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

long syscall1(long n, long a1) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

long syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r4 __asm__("esi") = a4;
    register long r5 __asm__("edi") = a5;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(r4), "D"(r5)
                      : "memory");
    return ret;
}

typedef unsigned int uint32_t;
typedef signed int int32_t;
typedef unsigned int size_t;

#define SYS_WRITE   1
#define SYS_EXIT    60
#define SYS_FORK    57
#define SYS_WAITPID 61
#define SYS_BRK     45
#define SYS_MMAP    90
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_MUNMAP  91
#define SYS_MPROTECT 92

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_PRIVATE 0x02
#define MAP_ANON    0x20

static long syscall0(long n) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n) : "memory");
    return ret;
}

static long syscall1(long n, long a1) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a1) : "memory");
    return ret;
}

static long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2), "d"(a3) : "memory");
    return ret;
}

static long syscall5(long n, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r4 __asm__("esi") = a4;
    register long r5 __asm__("edi") = a5;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "b"(a1), "c"(a2), "d"(a3), "S"(r4), "D"(r5)
                      : "memory");
    return ret;
}

static size_t str_len(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

static void write_str(const char *s) {
    syscall3(SYS_WRITE, 1, (long)s, (long)str_len(s));
}

static void exit_now(int code) {
    syscall1(SYS_EXIT, code);
    for (;;) __asm__ volatile ("hlt");
}

static void write_hex(uint32_t value) {
    char buf[11];
    static const char hex[] = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[9 - i] = hex[value & 0xFu];
        value >>= 4;
    }
    buf[10] = '\0';
    write_str(buf);
}

static void test_heap_and_mmap(void) {
    uint32_t current_brk = (uint32_t)syscall1(SYS_BRK, 0);
    uint32_t *heap_ptr;
    uint32_t *map_ptr;
    long mmap_ret;

    write_str("[init] brk base=");
    write_hex(current_brk);
    write_str("\n");

    if ((uint32_t)syscall1(SYS_BRK, current_brk + 8192u) < current_brk + 8192u) {
        write_str("[init] brk grow failed\n");
        exit_now(21);
    }

    heap_ptr = (uint32_t*)current_brk;
    heap_ptr[0] = 0x1234BEEF;
    if (heap_ptr[0] != 0x1234BEEF) {
        write_str("[init] brk memory check failed\n");
        exit_now(22);
    }

    mmap_ret = syscall5(SYS_MMAP, 0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1);
    if (mmap_ret < 0) {
        write_str("[init] mmap failed\n");
        exit_now(23);
    }

    map_ptr = (uint32_t*)mmap_ret;
    map_ptr[0] = 0x0BADF00D;
    if (map_ptr[0] != 0x0BADF00D) {
        write_str("[init] mmap memory check failed\n");
        exit_now(24);
    }
}

static void test_fork_wait(void) {
    int status = -1;
    long pid = syscall0(SYS_FORK);

    if (pid < 0) {
        write_str("[init] fork failed\n");
        exit_now(31);
    }

    if (pid == 0) {
        write_str("[init-child] hello from child\n");
        exit_now(7);
    }

    write_str("[init-parent] waiting for child\n");
    if (syscall3(SYS_WAITPID, pid, (long)&status, 0) != pid) {
        write_str("[init-parent] waitpid failed\n");
        exit_now(32);
    }

    if (status != 7) {
        write_str("[init-parent] unexpected child status\n");
        exit_now(33);
    }

    write_str("[init-parent] waitpid ok\n");
}

static void test_file_mmap(void) {
    long fd;
    long fmap;

    write_str("[init] file-backed mmap test\n");
    fd = syscall3(SYS_OPEN, (long)"/bin/init", 0, 0);
    if (fd < 0) {
        write_str("[init] open /bin/init failed\n");
        return;
    }

    fmap = syscall5(SYS_MMAP, 0, 4096, PROT_READ, MAP_PRIVATE, fd);
    if (fmap < 0) {
        write_str("[init] file mmap failed\n");
        syscall1(SYS_CLOSE, fd);
        return;
    }

    /* print first 64 bytes of mapped file */
    syscall3(SYS_WRITE, 1, (long)fmap, 64);

    /* protect read-only (no-op if already) and then unmap */
    syscall3(SYS_MPROTECT, fmap, 4096, PROT_READ);
    syscall3(SYS_MUNMAP, fmap, 4096, 0);
    syscall1(SYS_CLOSE, fd);
}

int main(void) {
    write_str("[init] userspace bootstrap ok\n");
    test_heap_and_mmap();
    test_fork_wait();
    test_file_mmap();
    write_str("[init] all tests passed\n");
    exit_now(0);
    return 0;
}

__asm__(
    ".global _start\n"
    "_start:\n"
    "    call main\n"
    "    mov %eax, %ebx\n"
    "    mov $60, %eax\n"
    "    int $0x80\n"
    "1:  hlt\n"
    "    jmp 1b\n"
);
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdlib.h>

#define SYS_READ    0
#define SYS_WRITE   1
#define SYS_EXIT    60
#define SYS_FORK    57
#define SYS_WAITPID 61
#define SYS_BRK     45
#define SYS_MMAP    90
#define SYS_OPEN    2
#define SYS_CLOSE   3
#define SYS_UNLINK  10
#define SYS_MKDIR   39
#define SYS_MUNMAP  91
#define SYS_MPROTECT 92

#define VFS_O_RDONLY 0
#define VFS_O_WRONLY 1
#define VFS_O_CREAT  0x0040
#define VFS_O_TRUNC  0x0200

#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define MAP_PRIVATE 0x02
#define MAP_ANON    0x20

/*
 * The musl tree under /tmp/blueyos-musl was built for stock Linux i386
 * syscall numbers. BlueyOS still uses its own numbering, so keep the kernel
 * boundary explicit here and only rely on musl for pure libc routines.
 */

static void write_str(const char *s) {
    write(STDOUT_FILENO, s, strlen(s));
}

static void write_buf(const char *buf, size_t len) {
    write(STDOUT_FILENO, buf, len);
}

__attribute__((noreturn)) static void exit_now(int code) {
    _exit(code);
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
    write_buf(buf, sizeof(buf) - 1u);
}

static void fill_pattern(unsigned char *buf, size_t len, uint32_t seed) {
    size_t i;

    for (i = 0; i < len; i++) {
        buf[i] = (unsigned char)((seed + (uint32_t)(i * 17u)) & 0xffu);
    }
}

static void test_blueyfs_file_case(const char *path, size_t total_size, uint32_t seed) {
    unsigned char write_buf[257];
    unsigned char read_buf[257];
    size_t offset = 0;
    long fd;

    mkdir("/var", 0755);
    unlink(path);

    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        write_str("[init] blueyfs open-for-write failed: ");
        write_str(path);
        write_str(" ret=");
        write_hex((uint32_t)fd);
        write_str("\n");
        exit_now(51);
    }

        while (offset < total_size) {
        size_t chunk = total_size - offset;
        long written;

        if (chunk > sizeof(write_buf)) chunk = sizeof(write_buf);
        fill_pattern(write_buf, chunk, seed + (uint32_t)offset);
        written = write(fd, write_buf, chunk);
        if (written != (long)chunk) {
            write_str("[init] blueyfs short write: ");
            write_str(path);
            write_str(" ret=");
            write_hex((uint32_t)written);
            write_str("\n");
            exit_now(52);
        }
        offset += chunk;
    }

    close(fd);

    fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        write_str("[init] blueyfs open-for-read failed: ");
        write_str(path);
        write_str(" ret=");
        write_hex((uint32_t)fd);
        write_str("\n");
        exit_now(53);
    }

    offset = 0;
    while (offset < total_size) {
        size_t chunk = total_size - offset;
        long rd;

        if (chunk > sizeof(read_buf)) chunk = sizeof(read_buf);
        fill_pattern(write_buf, chunk, seed + (uint32_t)offset);
        rd = read(fd, read_buf, chunk);
        if (rd != (long)chunk) {
            write_str("[init] blueyfs short read: ");
            write_str(path);
            write_str(" ret=");
            write_hex((uint32_t)rd);
            write_str("\n");
            exit_now(54);
        }

        if (memcmp(read_buf, write_buf, chunk) != 0) {
            size_t mismatch = 0;

            while (mismatch < chunk && read_buf[mismatch] == write_buf[mismatch]) {
                mismatch++;
            }

            write_str("[init] blueyfs data mismatch: ");
            write_str(path);
            write_str(" offset=");
            write_hex((uint32_t)(offset + mismatch));
            write_str("\n");
            exit_now(55);
        }

        offset += chunk;
    }

    close(fd);
    if (unlink(path) < 0) {
        write_str("[init] blueyfs unlink failed: ");
        write_str(path);
        write_str("\n");
        exit_now(56);
    }
}

static void test_blueyfs_file_sizes(void) {
    static const char *paths[] = {
        "/var/blueyfs-001.bin",
        "/var/blueyfs-127.bin",
        "/var/blueyfs-4k.bin",
        "/var/blueyfs-5k.bin",
        "/var/blueyfs-16k.bin",
    };
    static const size_t sizes[] = { 1u, 127u, 4096u, 5000u, 16384u };
    size_t i;

    write_str("[init] blueyfs size tests\n");
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        test_blueyfs_file_case(paths[i], sizes[i], 0x40u + (uint32_t)i * 29u);
    }
    write_str("[init] blueyfs size tests ok\n");
}

static void test_heap_and_mmap(void) {
    void *current_brk = sbrk(0);
    uint32_t *heap_ptr;
    uint32_t *map_ptr;
    long mmap_ret;

    write_str("[init] brk base=");
    write_hex((uint32_t)(uintptr_t)current_brk);
    write_str("\n");

    if (brk((char*)current_brk + 8192u) != 0) {
        write_str("[init] brk grow failed\n");
        exit_now(21);
    }

    heap_ptr = (uint32_t*)current_brk;
    heap_ptr[0] = 0x1234BEEF;
    if (heap_ptr[0] != 0x1234BEEF) {
        write_str("[init] brk memory check failed\n");
        exit_now(22);
    }
    void *mmap_addr = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (mmap_addr == MAP_FAILED) {
        write_str("[init] mmap failed\n");
        exit_now(23);
    }
    map_ptr = (uint32_t*)mmap_addr;
    map_ptr[0] = 0x0BADF00D;
    if (map_ptr[0] != 0x0BADF00D) {
        write_str("[init] mmap memory check failed\n");
        exit_now(24);
    }
}

static void test_fork_wait(void) {
    int status = -1;
    pid_t pid = fork();

    if (pid < 0) {
        write_str("[init] fork failed\n");
        exit_now(31);
    }

    if (pid == 0) {
        write_str("[init-child] hello from child\n");
        _exit(7);
    }

    write_str("[init-parent] waiting for child\n");
    if (waitpid((pid_t)pid, &status, 0) != (pid_t)pid) {
        write_str("[init-parent] waitpid failed\n");
        exit_now(32);
    }

    /* BlueyOS currently reports the raw exit code, not POSIX wait bits. */
    if (status != 7) {
        write_str("[init-parent] unexpected child status\n");
        exit_now(33);
    }

    write_str("[init-parent] waitpid ok\n");
}

static void test_file_mmap(void) {
    long fd;
    long fmap;
    const unsigned char *hdr;

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

    hdr = (const unsigned char *)fmap;
    if (hdr[0] != 0x7f || hdr[1] != 'E' || hdr[2] != 'L' || hdr[3] != 'F') {
        write_str("[init] file mmap header check failed\n");
        syscall3(SYS_MUNMAP, fmap, 4096, 0);
        syscall1(SYS_CLOSE, fd);
        exit_now(41);
    }
    write_str("[init] file mmap header ok\n");

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
    test_blueyfs_file_sizes();
    write_str("[init] all tests passed\n");
    if (getenv("BLUEYOS_EXEC_BASH")) {
        execl("/bin/bash", "bash", NULL);
        write_str("[init] WARNING: execl /bin/bash failed\n");
    }
    exit_now(0);
    return 0;
}

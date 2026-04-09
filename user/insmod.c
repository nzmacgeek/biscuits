// BlueyOS insmod - "Load a kernel module"
// Similar to Linux insmod command
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#define SYS_READ         0
#define SYS_WRITE        1
#define SYS_OPEN         2
#define SYS_CLOSE        3
#define SYS_INIT_MODULE 128
#define SYS_EXIT        60

#define O_RDONLY 0

// System call wrapper
static inline int syscall1(int num, int arg1) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
    return ret;
}

// Simple strlen
static int my_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

// Simple write
static void my_write(int fd, const char *s) {
    syscall3(SYS_WRITE, fd, (int)s, my_strlen(s));
}

// Read entire file into buffer
static int read_file(const char *path, char *buffer, int max_size) {
    int fd = syscall3(SYS_OPEN, (int)path, O_RDONLY, 0);
    if (fd < 0) {
        my_write(2, "insmod: failed to open file\n");
        return -1;
    }

    int total = 0;
    while (total < max_size) {
        int remaining = max_size - total;
        int chunk = remaining < 4096 ? remaining : 4096;
        int n = syscall3(SYS_READ, fd, (int)(buffer + total), chunk);
        if (n <= 0) break;
        total += n;
    }

    if (total == max_size) {
        char extra = 0;
        int n = syscall3(SYS_READ, fd, (int)&extra, 1);
        if (n > 0) {
            my_write(2, "insmod: module file too large\n");
            syscall1(SYS_CLOSE, fd);
            return -1;
        }
    }

    syscall1(SYS_CLOSE, fd);
    return total;
}

// Entry point
void _start(int argc, char **argv) {
    if (argc < 2) {
        my_write(2, "Usage: insmod <module.ko>\n");
        syscall1(SYS_EXIT, 1);
    }

    const char *path = argv[1];

    // Allocate buffer for module (max 1MB)
    static char module_buffer[1024 * 1024];

    my_write(1, "Loading module from ");
    my_write(1, path);
    my_write(1, "\n");

    int size = read_file(path, module_buffer, sizeof(module_buffer));
    if (size < 0) {
        my_write(2, "insmod: failed to read module\n");
        syscall1(SYS_EXIT, 1);
    }

    // Call init_module syscall
    int result = syscall3(SYS_INIT_MODULE, (int)module_buffer, size, 0);
    if (result < 0) {
        my_write(2, "insmod: init_module failed\n");
        syscall1(SYS_EXIT, 1);
    }

    my_write(1, "Module loaded successfully\n");
    syscall1(SYS_EXIT, 0);
}

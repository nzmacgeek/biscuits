// BlueyOS rmmod - "Remove a kernel module"
// Similar to Linux rmmod command
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#define SYS_WRITE         4
#define SYS_DELETE_MODULE 129
#define SYS_EXIT          93

// System call wrapper
static inline int syscall1(int num, int arg1) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1));
    return ret;
}

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    asm volatile("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3));
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

// Entry point
void _start(int argc, char **argv) {
    if (argc < 2) {
        my_write(2, "Usage: rmmod <module_name>\n");
        syscall1(SYS_EXIT, 1);
    }

    const char *name = argv[1];

    my_write(1, "Unloading module ");
    my_write(1, name);
    my_write(1, "\n");

    // Call delete_module syscall
    int result = syscall3(SYS_DELETE_MODULE, (int)name, 0, 0);
    if (result < 0) {
        my_write(2, "rmmod: delete_module failed\n");
        syscall1(SYS_EXIT, 1);
    }

    my_write(1, "Module unloaded successfully\n");
    syscall1(SYS_EXIT, 0);
}

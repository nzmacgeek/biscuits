typedef unsigned int   uint32_t;
typedef signed int     int32_t;
typedef unsigned short uint16_t;
typedef unsigned int   size_t;

#define SYS_READ     0
#define SYS_WRITE    1
#define SYS_OPEN     2
#define SYS_CLOSE    3
#define SYS_EXECVE   11
#define SYS_FORK     57
#define SYS_EXIT     60
#define SYS_WAITPID  61
#define SYS_CHDIR    80
#define SYS_REBOOT   88
#define SYS_GETDENTS 141
#define SYS_GETCWD   183

#define VFS_O_RDONLY 0

#define REBOOT_MAGIC1        0xfee1deadU
#define REBOOT_MAGIC2        0x28121969U
#define REBOOT_CMD_RESTART   0x01234567U
#define REBOOT_CMD_POWER_OFF 0x4321fedcU

#define LINE_MAX     256
#define ARGS_MAX     16
#define READ_BUF_MAX 256
#define PATH_MAX     256

typedef struct {
    uint32_t d_ino;
    uint32_t d_off;
    uint16_t d_reclen;
    char     d_name[1];
} k_dirent_t;

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

static long syscall2(long n, long a1, long a2) {
    long ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(n), "b"(a1), "c"(a2) : "memory");
    return ret;
}

static long syscall3(long n, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(n), "b"(a1), "c"(a2), "d"(a3)
                      : "memory");
    return ret;
}

static size_t str_len(const char *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static int str_eq(const char *lhs, const char *rhs) {
    size_t i = 0;

    if (!lhs || !rhs) return 0;
    while (lhs[i] && rhs[i]) {
        if (lhs[i] != rhs[i]) return 0;
        i++;
    }
    return lhs[i] == rhs[i];
}

static int contains_char(const char *s, char needle) {
    while (*s) {
        if (*s == needle) return 1;
        s++;
    }
    return 0;
}

static void *mem_copy(void *dst, const void *src, size_t len) {
    unsigned char *out = (unsigned char *)dst;
    const unsigned char *in = (const unsigned char *)src;
    size_t i;

    for (i = 0; i < len; i++) out[i] = in[i];
    return dst;
}

static void write_len(int fd, const char *s, size_t len) {
    while (len > 0) {
        long rc = syscall3(SYS_WRITE, fd, (long)s, (long)len);
        if (rc <= 0) return;
        s += (size_t)rc;
        len -= (size_t)rc;
    }
}

static void write_str_fd(int fd, const char *s) {
    write_len(fd, s, str_len(s));
}

static void write_str(const char *s) {
    write_str_fd(1, s);
}

static void write_err(const char *s) {
    write_str_fd(2, s);
}

static void write_char(char c) {
    write_len(1, &c, 1);
}

static void write_hex(uint32_t value) {
    char buf[11];
    static const char digits[] = "0123456789abcdef";
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[9 - i] = digits[value & 0xfu];
        value >>= 4;
    }
    buf[10] = '\0';
    write_str(buf);
}

static void write_uint(uint32_t value) {
    char buf[11];
    size_t pos = sizeof(buf) - 1;

    buf[pos] = '\0';
    do {
        pos--;
        buf[pos] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value > 0);
    write_str(&buf[pos]);
}

static void exit_now(int code) {
    syscall1(SYS_EXIT, code);
    for (;;) __asm__ volatile ("hlt");
}

static int read_line(char *buf, size_t buf_len) {
    size_t used = 0;

    if (!buf || buf_len < 2) return -1;

    while (used + 1 < buf_len) {
        char ch = '\0';
        long rc = syscall3(SYS_READ, 0, (long)&ch, 1);

        if (rc < 0) return (int)rc;
        if (rc == 0) break;
        if (ch == '\r') continue;
        if (ch == '\n') break;
        buf[used++] = ch;
    }

    buf[used] = '\0';
    return (int)used;
}

static int tokenize(char *line, char **argv, int argv_max) {
    int argc = 0;
    char *cursor = line;

    while (*cursor && argc + 1 < argv_max) {
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (!*cursor) break;

        argv[argc++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') {
            cursor++;
        }
        if (!*cursor) break;
        *cursor++ = '\0';
    }

    argv[argc] = 0;
    return argc;
}

static void print_status(const char *label, long rc) {
    write_err("[rescue] ");
    write_err(label);
    write_err(": ");
    write_hex((uint32_t)rc);
    write_err("\n");
}

static void print_prompt(void) {
    char cwd[PATH_MAX];
    long rc = syscall2(SYS_GETCWD, (long)cwd, sizeof(cwd));

    write_str("rescue:");
    if (rc < 0) {
        write_str("?");
    } else {
        write_str(cwd);
    }
    write_str("# ");
}

static long try_exec_path(const char *path, char **argv) {
    return syscall3(SYS_EXECVE, (long)path, (long)argv, 0);
}

static long exec_with_search(char **argv) {
    static const char *prefixes[] = { "", "/bin/", "/sbin/", "/usr/bin/", 0 };
    char path[PATH_MAX];
    int i;
    long last_rc = -1;

    if (!argv || !argv[0] || !argv[0][0]) return -1;

    if (contains_char(argv[0], '/')) {
        return try_exec_path(argv[0], argv);
    }

    for (i = 0; prefixes[i]; i++) {
        size_t prefix_len = str_len(prefixes[i]);
        size_t cmd_len = str_len(argv[0]);

        if (prefix_len + cmd_len + 1 > sizeof(path)) continue;
        mem_copy(path, prefixes[i], prefix_len);
        mem_copy(path + prefix_len, argv[0], cmd_len);
        path[prefix_len + cmd_len] = '\0';

        last_rc = try_exec_path(path, argv);
        if (last_rc >= 0) return last_rc;
    }

    return last_rc;
}

static int spawn_and_wait(char **argv) {
    long pid;
    int status = -1;

    pid = syscall0(SYS_FORK);
    if (pid < 0) {
        print_status("fork failed", pid);
        return 1;
    }

    if (pid == 0) {
        long rc = exec_with_search(argv);
        write_err("[rescue] exec failed for ");
        write_err(argv[0]);
        write_err(": ");
        write_hex((uint32_t)rc);
        write_err("\n");
        exit_now(127);
    }

    if (syscall3(SYS_WAITPID, pid, (long)&status, 0) != pid) {
        write_err("[rescue] waitpid failed\n");
        return 1;
    }

    if (status != 0) {
        write_err("[rescue] child exited with status ");
        write_uint((uint32_t)status);
        write_err("\n");
    }

    return status;
}

static int cmd_pwd(void) {
    char cwd[PATH_MAX];
    long rc = syscall2(SYS_GETCWD, (long)cwd, sizeof(cwd));

    if (rc < 0) {
        print_status("getcwd failed", rc);
        return 1;
    }

    write_str(cwd);
    write_str("\n");
    return 0;
}

static int cmd_cd(int argc, char **argv) {
    const char *target = argc > 1 ? argv[1] : "/";
    long rc = syscall1(SYS_CHDIR, (long)target);

    if (rc < 0) {
        print_status("chdir failed", rc);
        return 1;
    }
    return 0;
}

static int cmd_cat(int argc, char **argv) {
    char buf[READ_BUF_MAX];
    long fd;

    if (argc < 2) {
        write_err("usage: cat <path>\n");
        return 1;
    }

    fd = syscall3(SYS_OPEN, (long)argv[1], VFS_O_RDONLY, 0);
    if (fd < 0) {
        print_status("open failed", fd);
        return 1;
    }

    for (;;) {
        long rd = syscall3(SYS_READ, fd, (long)buf, sizeof(buf));
        if (rd < 0) {
            print_status("read failed", rd);
            syscall1(SYS_CLOSE, fd);
            return 1;
        }
        if (rd == 0) break;
        write_len(1, buf, (size_t)rd);
    }

    syscall1(SYS_CLOSE, fd);
    return 0;
}

static int cmd_ls(int argc, char **argv) {
    char buf[512];
    const char *path = argc > 1 ? argv[1] : ".";
    long fd = syscall3(SYS_OPEN, (long)path, VFS_O_RDONLY, 0);

    if (fd < 0) {
        print_status("open directory failed", fd);
        return 1;
    }

    for (;;) {
        long rd = syscall3(SYS_GETDENTS, fd, (long)buf, sizeof(buf));
        size_t off = 0;

        if (rd < 0) {
            print_status("getdents failed", rd);
            syscall1(SYS_CLOSE, fd);
            return 1;
        }
        if (rd == 0) break;

        while (off < (size_t)rd) {
            k_dirent_t *de = (k_dirent_t *)(buf + off);
            if (!str_eq(de->d_name, ".") && !str_eq(de->d_name, "..")) {
                write_str(de->d_name);
                write_char('\n');
            }
            if (de->d_reclen == 0) break;
            off += de->d_reclen;
        }
    }

    syscall1(SYS_CLOSE, fd);
    return 0;
}

static int cmd_reboot(uint32_t cmd) {
    long rc = syscall3(SYS_REBOOT, REBOOT_MAGIC1, REBOOT_MAGIC2, cmd);
    if (rc < 0) {
        print_status("reboot syscall failed", rc);
        return 1;
    }
    return 0;
}

static void show_help(void) {
    write_str("BlueyOS rescue shell\n");
    write_str("builtins: help pwd cd ls cat reboot poweroff exit\n");
    write_str("other commands: login, /sbin/login, or any executable under /bin, /sbin, /usr/bin\n");
    write_str("note: arguments are split on spaces only; quotes are not supported here.\n");
}

int main(void) {
    char line[LINE_MAX];
    char *argv[ARGS_MAX];

    write_str("[rescue] single-user shell ready\n");
    write_str("[rescue] use 'login' to exercise the login binary without claw/matey\n");
    write_str("[rescue] use 'help' for builtins\n");

    for (;;) {
        int argc;
        int line_len;

        print_prompt();
        line_len = read_line(line, sizeof(line));
        if (line_len < 0) {
            print_status("read failed", line_len);
            continue;
        }
        if (line_len == 0) {
            write_str("\n");
            continue;
        }

        argc = tokenize(line, argv, ARGS_MAX);
        if (argc == 0) continue;

        if (str_eq(argv[0], "help")) {
            show_help();
            continue;
        }
        if (str_eq(argv[0], "pwd")) {
            cmd_pwd();
            continue;
        }
        if (str_eq(argv[0], "cd")) {
            cmd_cd(argc, argv);
            continue;
        }
        if (str_eq(argv[0], "ls")) {
            cmd_ls(argc, argv);
            continue;
        }
        if (str_eq(argv[0], "cat")) {
            cmd_cat(argc, argv);
            continue;
        }
        if (str_eq(argv[0], "reboot")) {
            cmd_reboot(REBOOT_CMD_RESTART);
            continue;
        }
        if (str_eq(argv[0], "poweroff")) {
            cmd_reboot(REBOOT_CMD_POWER_OFF);
            continue;
        }
        if (str_eq(argv[0], "exit")) {
            write_str("[rescue] pid1 stays alive; use reboot or poweroff instead.\n");
            continue;
        }

        spawn_and_wait(argv);
    }
}

// BlueyOS Shell - "Bluey's Command Post"
// "I'm in charge!" - Bluey Heeler
// Episode ref: "Camping" - Bluey runs the whole base camp operation
// Episode ref: "Dad Baby" - Bluey is completely in charge (for a while)
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../include/version.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../lib/stdlib.h"
#include "../drivers/keyboard.h"
#include "../drivers/vga.h"
#include "../drivers/vt100.h"
#include "../fs/vfs.h"
#include "../kernel/process.h"
#include "../kernel/rtc.h"
#include "../kernel/sysinfo.h"
#include "../kernel/kheap.h"
#include "../kernel/paging.h"
#include "../kernel/swap.h"
#include "../kernel/syslog.h"
#include "../kernel/tty.h"
#include "../drivers/net/network.h"
#include "../net/tcpip.h"
#include "../net/icmp.h"
#include "shell.h"

// ---------------------------------------------------------------------------
// Shell state
// ---------------------------------------------------------------------------
static char cwd[SHELL_CWD_MAX];

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
    int     truncated;
} shell_text_buffer_t;

#define SHELL_PIPE_SEGMENTS_MAX 8
#define SHELL_CAPTURE_INITIAL_CAP 1024u
#define SHELL_CAPTURE_LIMIT      (128u * 1024u)
#define SHELL_PAGER_ROWS         23

static const char *shell_pipe_input = NULL;
static size_t      shell_pipe_input_len = 0;
static int         shell_stage_consumed_output = 0;

static void make_abs_path(const char *rel, char *dst);

// ---------------------------------------------------------------------------
// Line input
// ---------------------------------------------------------------------------
static char linebuf[SHELL_LINE_MAX];
static int  linelen;
#define SHELL_PAGE_SIZE_KB (PAGE_SIZE / 1024u)
/* VT100/ANSI formatting used by the booted shell prompt/banner. */
#define ANSI_RESET         "\x1b[0m"
#define ANSI_BOLD_GREEN    "\x1b[1;32m"
#define ANSI_BOLD_BLUE     "\x1b[1;34m"

static uint32_t shell_ram_total_kb(uint32_t ram_boot_mb) {
    uint32_t managed_kb = pmm_total_frames() * SHELL_PAGE_SIZE_KB;

    if (ram_boot_mb == 0) return managed_kb;
    if (ram_boot_mb > (0xFFFFFFFFu / 1024u)) return managed_kb;

    uint32_t detected_kb = ram_boot_mb * 1024u;
    if (detected_kb > managed_kb) return managed_kb;

    return detected_kb;
}

static uint32_t shell_ram_free_kb(uint32_t total_kb, uint32_t used_kb, int *clamped) {
    if (used_kb >= total_kb) {
        if (clamped) *clamped = 1;
        return 0;
    }
    if (clamped) *clamped = 0;
    return total_kb - used_kb;
}

static void shell_readline(void) {
    linelen = 0;
    linebuf[0] = '\0';

    while (1) {
        char c = tty_getchar();

        if (c == '\n' || c == '\r') {
            kprintf("\n");
            break;
        } else if (c == '\b') {
            if (linelen > 0) {
                linelen--;
                linebuf[linelen] = '\0';
                kprintf("\b \b");
            }
        } else if (c >= 32 && c < 127) {
            if (linelen < SHELL_LINE_MAX - 1) {
                linebuf[linelen++] = c;
                linebuf[linelen]   = '\0';
                kprintf("%c", c);
            }
        }
    }
}

static void shell_direct_write(const char *buf, size_t len) {
    if (!buf || len == 0) return;
    tty_write(buf, len);
    tty_flush();
}

static void shell_direct_puts(const char *s) {
    if (!s) return;
    shell_direct_write(s, strlen(s));
}

static void shell_buffer_init(shell_text_buffer_t *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->truncated = 0;
}

static void shell_buffer_free(shell_text_buffer_t *buf) {
    if (!buf) return;
    if (buf->data) kfree(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
    buf->truncated = 0;
}

static int shell_buffer_reserve(shell_text_buffer_t *buf, size_t needed_len) {
    char *next;
    size_t next_cap;

    if (needed_len > SHELL_CAPTURE_LIMIT) {
        buf->truncated = 1;
        return 0;
    }

    if (buf->cap > needed_len) return 1;

    next_cap = buf->cap ? buf->cap : SHELL_CAPTURE_INITIAL_CAP;
    while (next_cap <= needed_len) {
        next_cap *= 2u;
        if (next_cap > (SHELL_CAPTURE_LIMIT + 1u)) {
            next_cap = SHELL_CAPTURE_LIMIT + 1u;
            break;
        }
    }

    if (next_cap <= needed_len) {
        buf->truncated = 1;
        return 0;
    }

    next = (char *)kmalloc(next_cap);
    if (!next) {
        buf->truncated = 1;
        return 0;
    }
    if (buf->data && buf->len) memcpy(next, buf->data, buf->len);
    if (buf->data) kfree(buf->data);
    buf->data = next;
    buf->cap = next_cap;
    buf->data[buf->len] = '\0';
    return 1;
}

static int shell_buffer_append(shell_text_buffer_t *buf, const char *data, size_t len) {
    if (!data || len == 0 || buf->truncated) return len == 0;
    if (!shell_buffer_reserve(buf, buf->len + len)) return 0;
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return 1;
}

static void shell_capture_putc(char c, void *ctx) {
    shell_text_buffer_t *buf = (shell_text_buffer_t *)ctx;
    shell_buffer_append(buf, &c, 1);
}

static void shell_emit_text(const char *data, size_t len) {
    size_t offset = 0;

    while (offset < len) {
        int chunk = (len - offset > 240u) ? 240 : (int)(len - offset);
        kprintf("%.*s", chunk, data + offset);
        offset += (size_t)chunk;
    }
}

static void shell_emit_pipe_input(void) {
    if (!shell_pipe_input || shell_pipe_input_len == 0) return;
    shell_emit_text(shell_pipe_input, shell_pipe_input_len);
}

static int shell_read_file_into_buffer(const char *path_arg,
                                       shell_text_buffer_t *buf,
                                       const char *cmd_name) {
    char path[SHELL_CWD_MAX];
    uint8_t chunk[256];
    int fd;
    int nread;

    make_abs_path(path_arg, path);
    fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) {
        kprintf("%s: %s: No such file\n", cmd_name, path_arg);
        return -1;
    }

    while ((nread = vfs_read(fd, chunk, sizeof(chunk))) > 0) {
        if (!shell_buffer_append(buf, (const char *)chunk, (size_t)nread)) {
            kprintf("%s: input too large\n", cmd_name);
            vfs_close(fd);
            return -1;
        }
    }

    vfs_close(fd);
    return 0;
}

static int shell_collect_text_input(int argc, char **argv, int argi,
                                    shell_text_buffer_t *buf,
                                    const char *cmd_name) {
    if (argi < argc) {
        if ((argi + 1) < argc) {
            kprintf("%s: only one input source is supported\n", cmd_name);
            return -1;
        }
        return shell_read_file_into_buffer(argv[argi], buf, cmd_name);
    }

    if (shell_pipe_input) {
        if (!shell_buffer_append(buf, shell_pipe_input, shell_pipe_input_len)) {
            kprintf("%s: input too large\n", cmd_name);
            return -1;
        }
        return 0;
    }

    kprintf("%s: missing input\n", cmd_name);
    return -1;
}

static int shell_parse_count_arg(int argc, char **argv, int *argi,
                                 int default_count, const char *cmd_name) {
    const char *arg;

    if (*argi >= argc) return default_count;
    arg = argv[*argi];

    if (strcmp(arg, "-n") == 0) {
        if ((*argi + 1) >= argc) {
            kprintf("%s: option '-n' requires a value\n", cmd_name);
            return -1;
        }
        *argi += 2;
        return atoi(argv[*argi - 1]) < 0 ? 0 : atoi(argv[*argi - 1]);
    }
    if (arg[0] == '-' && arg[1] >= '0' && arg[1] <= '9') {
        *argi += 1;
        return atoi(arg + 1) < 0 ? 0 : atoi(arg + 1);
    }

    return default_count;
}

static int shell_split_pipeline(char *line, char **segments, int max_segments) {
    int count = 0;
    int in_quotes = 0;
    char *segment_start = line;

    if (!line || max_segments <= 0) return 0;

    while (*line) {
        if (*line == '"') {
            in_quotes = !in_quotes;
        } else if (*line == '|' && !in_quotes) {
            if (count >= max_segments - 1) return -1;
            *line = '\0';
            segments[count++] = segment_start;
            segment_start = line + 1;
        }
        line++;
    }

    segments[count++] = segment_start;
    return count;
}

static size_t shell_find_line_start(const char *text, size_t len, int line_index) {
    int current = 0;
    size_t pos = 0;

    if (!text || line_index <= 0) return 0;
    while (pos < len && current < line_index) {
        if (text[pos++] == '\n') current++;
    }
    return pos;
}

static int shell_count_lines(const char *text, size_t len) {
    int lines = 0;
    size_t pos = 0;

    if (!text || len == 0) return 0;
    while (pos < len) {
        lines++;
        while (pos < len && text[pos] != '\n') pos++;
        if (pos < len && text[pos] == '\n') pos++;
    }
    return lines;
}

static void shell_pager_prompt(const char *prompt) {
    shell_direct_puts("\r");
    shell_direct_puts(prompt);
}

static void shell_pager_clear_prompt(void) {
    shell_direct_puts("\r                                                                                \r");
}

static void shell_run_more_pager(const char *text, size_t len) {
    size_t pos = 0;
    int lines_left = SHELL_PAGER_ROWS;

    while (pos < len) {
        size_t start = pos;

        while (pos < len && text[pos] != '\n') pos++;
        if (pos < len && text[pos] == '\n') pos++;
        shell_direct_write(text + start, pos - start);
        lines_left--;
        if (lines_left > 0 || pos >= len) continue;

        shell_pager_prompt("--More--  SPACE next page  ENTER next line  q quit");
        switch (tty_getchar()) {
            case 'q':
            case 'Q':
                shell_pager_clear_prompt();
                shell_direct_puts("\n");
                return;
            case '\n':
            case '\r':
                lines_left = 1;
                break;
            default:
                lines_left = SHELL_PAGER_ROWS;
                break;
        }
        shell_pager_clear_prompt();
    }
}

static void shell_render_less_page(const char *text, size_t len,
                                   int start_line, int total_lines) {
    size_t pos = shell_find_line_start(text, len, start_line);
    int shown = 0;

    kprintf_direct("\x1b[2J\x1b[H");
    while (shown < SHELL_PAGER_ROWS && pos < len) {
        size_t start = pos;
        while (pos < len && text[pos] != '\n') pos++;
        if (pos < len && text[pos] == '\n') pos++;
        shell_direct_write(text + start, pos - start);
        if (pos == len && text[pos - 1] != '\n') shell_direct_puts("\n");
        shown++;
    }
    while (shown < SHELL_PAGER_ROWS) {
        shell_direct_puts("~\n");
        shown++;
    }
    kprintf_direct(":%d/%d  q quit  j down  k up  space pgdn  b pgup  g top  G end",
                   total_lines == 0 ? 0 : (start_line + 1), total_lines);
}

static void shell_run_less_pager(const char *text, size_t len) {
    int total_lines = shell_count_lines(text, len);
    int start_line = 0;
    int max_start = total_lines > SHELL_PAGER_ROWS ? total_lines - SHELL_PAGER_ROWS : 0;

    for (;;) {
        char key;

        shell_render_less_page(text, len, start_line, total_lines);
        key = tty_getchar();
        if (key == 'q' || key == 'Q') break;
        if (key == 'j' || key == '\n' || key == '\r') {
            if (start_line < max_start) start_line++;
        } else if (key == 'k') {
            if (start_line > 0) start_line--;
        } else if (key == ' ') {
            start_line += SHELL_PAGER_ROWS;
            if (start_line > max_start) start_line = max_start;
        } else if (key == 'b' || key == 'u') {
            start_line -= SHELL_PAGER_ROWS;
            if (start_line < 0) start_line = 0;
        } else if (key == 'g') {
            start_line = 0;
        } else if (key == 'G') {
            start_line = max_start;
        }
    }
    kprintf_direct("\x1b[2J\x1b[H");
}

// ---------------------------------------------------------------------------
// Argument parsing - splits linebuf into argv[]; returns argc
// ---------------------------------------------------------------------------
static int shell_parse(char *line, char **argv, int max_args) {
    int   argc = 0;
    char *p    = line;

    while (*p && argc < max_args - 1) {
        // Skip whitespace
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;

        if (*p == '"') {
            // Quoted argument
            p++;
            argv[argc++] = p;
            while (*p && *p != '"') p++;
            if (*p) *p++ = '\0';
        } else {
            argv[argc++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

// Build an absolute path from cwd + relative path into dst (size SHELL_CWD_MAX)
static void make_abs_path(const char *rel, char *dst) {
    if (rel[0] == '/') {
        strncpy(dst, rel, SHELL_CWD_MAX - 1);
        dst[SHELL_CWD_MAX - 1] = '\0';
    } else {
        strncpy(dst, cwd, SHELL_CWD_MAX - 1);
        dst[SHELL_CWD_MAX - 1] = '\0';
        size_t n = strlen(dst);
        if (n < SHELL_CWD_MAX - 2 && dst[n - 1] != '/') {
            dst[n] = '/'; dst[n + 1] = '\0';
        }
        size_t rem = SHELL_CWD_MAX - strlen(dst) - 1;
        strncat(dst, rel, rem);
    }
}

// ---------------------------------------------------------------------------
// Built-in commands
// ---------------------------------------------------------------------------

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("BlueyOS Shell - built-in commands:\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);
    kprintf("  help / ?        show this help message\n");
    kprintf("  echo [text]     print text to the screen\n");
    kprintf("  clear           clear the screen\n");
    kprintf("  ls [path]       list directory contents\n");
    kprintf("  cat [file]      print file contents\n");
    kprintf("  mkdir [dir]     create a directory\n");
    kprintf("  rm [file]       remove a file\n");
    kprintf("  pwd             print working directory\n");
    kprintf("  cd [path]       change directory\n");
    kprintf("  uname           print system information\n");
    kprintf("  whoami          print current user name\n");
    kprintf("  ps              list running processes\n");
    kprintf("  mount           show mounted filesystems\n");
    kprintf("  ifconfig        show network interfaces\n");
    kprintf("  ping <ip>       send ICMP echo request\n");
    kprintf("  swapinfo        show swap statistics\n");
    kprintf("  meminfo         show kernel memory statistics\n");
    kprintf("  free            show a summary of memory usage\n");
    kprintf("  dmesg           show kernel log (ring buffer)\n");
    kprintf("  date            show current time\n");
    kprintf("  head [-n N] [f] show the first lines of input or a file\n");
    kprintf("  tail [-n N] [f] show the last lines of input or a file\n");
    kprintf("  more [file]     page through text a screen at a time\n");
    kprintf("  less [file]     scroll through text interactively\n");
    kprintf("  version         show BlueyOS version\n");
    kprintf("  reboot          restart the computer\n");
    kprintf("  halt            halt the computer\n");
    kprintf("  cmd1 | cmd2     pass command output into the next command\n");
    kprintf("\n\"It's the best day EVER!\" - Bluey Heeler\n");
}

static void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        kprintf("%s", argv[i]);
        if (i < argc - 1) kprintf(" ");
    }
    kprintf("\n");
}

static void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear();
}

static void cmd_ls(int argc, char **argv) {
    char path[SHELL_CWD_MAX];
    if (argc > 1) make_abs_path(argv[1], path);
    else          strncpy(path, cwd, SHELL_CWD_MAX - 1);

    vfs_dirent_t ents[64];
    int n = vfs_readdir(path, ents, 64);
    if (n < 0) {
        kprintf("ls: %s: No such directory\n", path);
        return;
    }
    if (n == 0) {
        kprintf("(empty)\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        if (ents[i].is_dir)
            vga_set_color(VGA_LIGHT_BLUE, VGA_BLACK);
        kprintf("%s%s\n", ents[i].is_dir ? "[DIR] " : "      ", ents[i].name);
        vga_set_color(VGA_WHITE, VGA_BLACK);
    }
}

static void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        if (shell_pipe_input) {
            shell_emit_pipe_input();
            return;
        }
        kprintf("cat: missing filename\n");
        return;
    }
    char path[SHELL_CWD_MAX];
    make_abs_path(argv[1], path);

    int fd = vfs_open(path, VFS_O_RDONLY);
    if (fd < 0) { kprintf("cat: %s: No such file\n", argv[1]); return; }

    uint8_t buf[512];
    int n;
    while ((n = vfs_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) kprintf("%c", (char)buf[i]);
    }
    if (n == 0) kprintf("\n");
    vfs_close(fd);
}

static void cmd_head(int argc, char **argv) {
    shell_text_buffer_t input;
    int argi = 1;
    int line_count;
    int emitted = 0;
    size_t pos = 0;

    shell_buffer_init(&input);
    line_count = shell_parse_count_arg(argc, argv, &argi, 10, "head");
    if (line_count < 0) {
        shell_buffer_free(&input);
        return;
    }
    if (shell_collect_text_input(argc, argv, argi, &input, "head") != 0) {
        shell_buffer_free(&input);
        return;
    }

    while (pos < input.len && emitted < line_count) {
        size_t start = pos;
        while (pos < input.len && input.data[pos] != '\n') pos++;
        if (pos < input.len && input.data[pos] == '\n') pos++;
        shell_emit_text(input.data + start, pos - start);
        emitted++;
    }

    shell_buffer_free(&input);
}

static void cmd_tail(int argc, char **argv) {
    shell_text_buffer_t input;
    int argi = 1;
    int line_count;
    int total_lines;
    int start_line;
    size_t start_pos;

    shell_buffer_init(&input);
    line_count = shell_parse_count_arg(argc, argv, &argi, 10, "tail");
    if (line_count < 0) {
        shell_buffer_free(&input);
        return;
    }
    if (shell_collect_text_input(argc, argv, argi, &input, "tail") != 0) {
        shell_buffer_free(&input);
        return;
    }

    total_lines = shell_count_lines(input.data, input.len);
    start_line = (total_lines > line_count) ? (total_lines - line_count) : 0;
    start_pos = shell_find_line_start(input.data, input.len, start_line);
    if (start_pos < input.len) shell_emit_text(input.data + start_pos, input.len - start_pos);

    shell_buffer_free(&input);
}

static void cmd_more(int argc, char **argv) {
    shell_text_buffer_t input;

    shell_buffer_init(&input);
    if (shell_collect_text_input(argc, argv, 1, &input, "more") != 0) {
        shell_buffer_free(&input);
        return;
    }

    shell_stage_consumed_output = 1;
    shell_run_more_pager(input.data ? input.data : "", input.len);
    shell_buffer_free(&input);
}

static void cmd_less(int argc, char **argv) {
    shell_text_buffer_t input;

    shell_buffer_init(&input);
    if (shell_collect_text_input(argc, argv, 1, &input, "less") != 0) {
        shell_buffer_free(&input);
        return;
    }

    shell_stage_consumed_output = 1;
    shell_run_less_pager(input.data ? input.data : "", input.len);
    shell_buffer_free(&input);
}

static void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) { kprintf("mkdir: missing directory name\n"); return; }
    char path[SHELL_CWD_MAX];
    make_abs_path(argv[1], path);
    if (vfs_mkdir(path) != 0)
        kprintf("mkdir: cannot create directory '%s'\n", argv[1]);
}

static void cmd_rm(int argc, char **argv) {
    if (argc < 2) { kprintf("rm: missing filename\n"); return; }
    char path[SHELL_CWD_MAX];
    make_abs_path(argv[1], path);
    if (vfs_unlink(path) != 0)
        kprintf("rm: cannot remove '%s'\n", argv[1]);
}

static void cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("%s\n", cwd);
}

static void cmd_cd(int argc, char **argv) {
    const char *target = (argc > 1) ? argv[1] : "/";

    if (strcmp(target, "..") == 0) {
        char *slash = strrchr(cwd, '/');
        if (slash && slash != cwd) {
            *slash = '\0';
        } else {
            cwd[0] = '/'; cwd[1] = '\0';
        }
        return;
    }

    if (strcmp(target, ".") == 0) return;

    char newpath[SHELL_CWD_MAX];
    make_abs_path(target, newpath);

    // Verify the path is a directory by trying readdir
    vfs_dirent_t dummy;
    int r = vfs_readdir(newpath, &dummy, 1);
    if (r < 0) {
        kprintf("cd: %s: No such directory\n", target);
        return;
    }
    strncpy(cwd, newpath, SHELL_CWD_MAX - 1);
    cwd[SHELL_CWD_MAX - 1] = '\0';
}

static void cmd_uname(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("BlueyOS %s %s %s i386 GNU/BlueyOS\n",
            BLUEYOS_VERSION_STRING, BLUEYOS_CODENAME, sysinfo_get_hostname());
}

static void cmd_whoami(int argc, char **argv) {
    (void)argc; (void)argv;
    process_t *p = process_current();
    if (!p) { kprintf("unknown\n"); return; }
    // Very simple mapping: UID 0 = bandit (root), 1 = bluey, 2 = bingo, 3 = chilli
    const char *names[] = { "bandit", "bluey", "bingo", "chilli", "jack", "judo" };
    if (p->uid < 6) kprintf("%s\n", names[p->uid]);
    else            kprintf("uid=%d\n", p->uid);
}

static void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("  PID  UID  STATE     NAME\n");
    for (process_t *p = process_first(); p; p = process_next(p)) {
        if (!p || p->state == PROC_DEAD) continue;
        const char *st = "UNKNOWN ";
        switch (p->state) {
            case PROC_READY:    st = "READY   "; break;
            case PROC_RUNNING:  st = "RUNNING "; break;
            case PROC_SLEEPING: st = "SLEEP   "; break;
            case PROC_ZOMBIE:   st = "ZOMBIE  "; break;
            default: break;
        }
        kprintf("  %3d  %3d  %s  %s\n", p->pid, p->uid, st, p->name);
    }
}

static void cmd_mount(int argc, char **argv) {
    (void)argc; (void)argv;
    vfs_print_mounts();
}

static void cmd_ifconfig(int argc, char **argv) {
    (void)argc; (void)argv;
    net_print_interfaces();
    const tcpip_config_t *cfg = tcpip_get_config();
    char ipstr[20], gwstr[20], maskstr[20];
    ip_to_str(cfg->ip,      ipstr);
    ip_to_str(cfg->gateway, gwstr);
    ip_to_str(cfg->netmask, maskstr);
    kprintf("[TCP/IP] IP: %s  GW: %s  Mask: %s\n", ipstr, gwstr, maskstr);
}

static void cmd_ping(int argc, char **argv) {
    if (argc < 2) { kprintf("ping: usage: ping <ip>\n"); return; }

    // Parse dotted-decimal IP
    const char *s = argv[1];
    uint8_t octets[4];
    int oi = 0;
    uint32_t acc = 0;
    for (int i = 0; s[i] && oi < 4; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            acc = acc * 10 + (uint32_t)(s[i] - '0');
        } else if (s[i] == '.') {
            octets[oi++] = (uint8_t)acc;
            acc = 0;
        } else {
            kprintf("ping: invalid address\n"); return;
        }
    }
    if (oi == 3) octets[oi++] = (uint8_t)acc;
    if (oi != 4) { kprintf("ping: invalid address\n"); return; }

    uint32_t dst = htonl(((uint32_t)octets[0] << 24) |
                         ((uint32_t)octets[1] << 16) |
                         ((uint32_t)octets[2] <<  8) |
                          (uint32_t)octets[3]);

    kprintf("PING %s: 32 bytes of data\n", argv[1]);
    for (int i = 0; i < 4; i++) {
        if (icmp_ping(dst, 1, (uint16_t)(i + 1)) == 0) {
            kprintf("  seq %d: sent\n", i + 1);
        } else {
            kprintf("  seq %d: send failed\n", i + 1);
        }
        // Small busy-wait so replies can be received before next ping
        for (volatile int w = 0; w < 5000000; w++);
        tcpip_poll();
    }
    kprintf("(Note: BlueyOS uses polling; check console for echo replies)\n");
}

static void cmd_swapinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    swap_print_info();
}

static void cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;

    uint32_t heap_total = 0, heap_used = 0, heap_free = 0;
    uint32_t ram_boot_mb  = sysinfo_get_ram_mb();
    uint32_t ram_total_kb = shell_ram_total_kb(ram_boot_mb);
    uint32_t mem_used_kb  = pmm_used_frames() * SHELL_PAGE_SIZE_KB;
    int mem_clamped = 0;
    uint32_t mem_free_kb  = shell_ram_free_kb(ram_total_kb, mem_used_kb, &mem_clamped);
    uint32_t swap_total = swap_total_pages();
    uint32_t swap_used  = swap_used_pages();

    kheap_get_stats(&heap_total, &heap_used, &heap_free);

    kprintf("MemTotal: %u kB\n", ram_total_kb);
    kprintf("MemUsed:  %u kB\n", mem_used_kb);
    kprintf("MemFree:  %u kB\n", mem_free_kb);
    if (ram_boot_mb) {
        kprintf("RAMBoot:  %u MB detected by bootloader\n", ram_boot_mb);
    }
    if (mem_clamped) {
        kprintf("MemWarn: tracked frame usage exceeds detected RAM size\n");
    }
    kprintf("HeapTotal:%u kB\n", heap_total / 1024);
    kprintf("HeapUsed: %u kB\n", heap_used / 1024);
    kprintf("HeapFree: %u kB\n", heap_free / 1024);
    kprintf("SwapTotal:%u kB\n", swap_total * SHELL_PAGE_SIZE_KB);
    kprintf("SwapUsed: %u kB\n", swap_used * SHELL_PAGE_SIZE_KB);
    kprintf("SwapFree: %u kB\n", (swap_total - swap_used) * SHELL_PAGE_SIZE_KB);
}

static void cmd_free(int argc, char **argv) {
    (void)argc; (void)argv;

    uint32_t heap_total = 0, heap_used = 0, heap_free = 0;
    uint32_t ram_boot_mb  = sysinfo_get_ram_mb();
    uint32_t ram_total_kb = shell_ram_total_kb(ram_boot_mb);
    uint32_t mem_used_kb  = pmm_used_frames() * SHELL_PAGE_SIZE_KB;
    int mem_clamped = 0;
    uint32_t mem_free_kb  = shell_ram_free_kb(ram_total_kb, mem_used_kb, &mem_clamped);
    uint32_t swap_total = swap_total_pages();
    uint32_t swap_used  = swap_used_pages();

    kheap_get_stats(&heap_total, &heap_used, &heap_free);

    kprintf("type  total_kB used_kB free_kB\n");
    kprintf("mem   %u %u %u\n", ram_total_kb, mem_used_kb, mem_free_kb);
    kprintf("heap  %u %u %u\n", heap_total / 1024, heap_used / 1024, heap_free / 1024);
    kprintf("swap  %u %u %u\n",
            swap_total * SHELL_PAGE_SIZE_KB,
            swap_used * SHELL_PAGE_SIZE_KB,
            (swap_total - swap_used) * SHELL_PAGE_SIZE_KB);
    if (mem_clamped) {
        kprintf("note  tracked frame usage exceeds detected RAM size\n");
    }
}

static void cmd_dmesg(int argc, char **argv) {
    (void)argc; (void)argv;
    syslog_dmesg();
}

static void cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    const timezone_t *tz = sysinfo_get_timezone();
    uint32_t unix_secs = 0;

    rtc_poll();

    kprintf("Timezone : %s (UTC+%d)\n", tz->name,
            (int)(tz->offset_seconds / 3600));
    kprintf("Epoch    : %s\n", BANDIT_EPOCH_NAME);

    if (!rtc_get_unix_time(&unix_secs)) {
        kprintf("Current  : unavailable\n");
        kprintf("Source   : %s\n", rtc_source_name());
        kprintf("Uptime   : %u s\n", rtc_get_uptime_seconds());
        return;
    }

    int year = 0, mon = 0, mday = 0;
    int hour = 0, min = 0, sec = 0;
    unix_to_datetime(unix_secs, &year, &mon, &mday, &hour, &min, &sec);

    kprintf("Current  : %04d-%02d-%02d %02d:%02d:%02d %s\n",
            year, mon, mday, hour, min, sec, tz->name);
    kprintf("Unix     : %u\n", unix_secs);
    kprintf("Bluey    : %u\n", unix_to_bluey(unix_secs));
    kprintf("Source   : %s\n", rtc_source_name());
}

static void cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("%s\n", BLUEYOS_VERSION_STRING);
    kprintf("Codename : %s\n", BLUEYOS_CODENAME);
    kprintf("Build    : #%d by %s@%s on %s %s\n",
            BLUEYOS_BUILD_NUMBER,
            BLUEYOS_BUILD_USER, BLUEYOS_BUILD_HOST,
            BLUEYOS_BUILD_DATE, BLUEYOS_BUILD_TIME);
}

static void cmd_halt(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Halting BlueyOS. G'bye from the Heelers!\n");
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

static void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    kprintf("Rebooting... \"See ya next time!\" - Bluey Heeler\n");
    // Keyboard controller CPU reset line (pulse bit 0 of port 0x64)
    __asm__ volatile("cli");
    uint8_t val;
    // Flush the keyboard controller input buffer
    do {
        __asm__ volatile("inb $0x64, %0" : "=a"(val));
    } while (val & 0x02);
    __asm__ volatile("outb %0, $0x64" : : "a"((uint8_t)0xFE));
    // Fallback: triple-fault via null IDT
    for (;;) __asm__ volatile("hlt");
}

// ---------------------------------------------------------------------------
// Command dispatch table
// ---------------------------------------------------------------------------
typedef struct {
    const char *name;
    void (*fn)(int, char **);
} shell_cmd_t;

static const shell_cmd_t commands[] = {
    { "help",     cmd_help     },
    { "?",        cmd_help     },
    { "echo",     cmd_echo     },
    { "clear",    cmd_clear    },
    { "ls",       cmd_ls       },
    { "cat",      cmd_cat      },
    { "mkdir",    cmd_mkdir    },
    { "rm",       cmd_rm       },
    { "pwd",      cmd_pwd      },
    { "cd",       cmd_cd       },
    { "uname",    cmd_uname    },
    { "whoami",   cmd_whoami   },
    { "ps",       cmd_ps       },
    { "mount",    cmd_mount    },
    { "ifconfig", cmd_ifconfig },
    { "ping",     cmd_ping     },
    { "swapinfo", cmd_swapinfo },
    { "meminfo",  cmd_meminfo  },
    { "free",     cmd_free     },
    { "dmesg",    cmd_dmesg    },
    { "date",     cmd_date     },
    { "head",     cmd_head     },
    { "tail",     cmd_tail     },
    { "more",     cmd_more     },
    { "less",     cmd_less     },
    { "version",  cmd_version  },
    { "halt",     cmd_halt     },
    { "reboot",   cmd_reboot   },
    { NULL, NULL }
};

static const shell_cmd_t *shell_find_command(const char *name) {
    for (int i = 0; commands[i].name; i++) {
        if (strcmp(name, commands[i].name) == 0) return &commands[i];
    }
    return NULL;
}

static int shell_execute_argv(int argc, char **argv) {
    const shell_cmd_t *cmd;

    if (argc == 0 || !argv[0]) return 0;
    cmd = shell_find_command(argv[0]);
    if (!cmd) {
        kprintf("blueyos: command not found: %s\n", argv[0]);
        kprintf("Type 'help' for available commands.\n");
        return -1;
    }
    cmd->fn(argc, argv);
    return 0;
}

static void shell_execute_pipeline(char *line) {
    char *segments[SHELL_PIPE_SEGMENTS_MAX];
    shell_text_buffer_t current;
    shell_text_buffer_t next;
    int segment_count;

    shell_buffer_init(&current);
    shell_buffer_init(&next);

    segment_count = shell_split_pipeline(line, segments, SHELL_PIPE_SEGMENTS_MAX);
    if (segment_count < 0) {
        kprintf("blueyos: pipeline too deep\n");
        return;
    }
    if (segment_count <= 1) {
        char *argv[SHELL_ARGS_MAX];
        int argc = shell_parse(line, argv, SHELL_ARGS_MAX);
        shell_execute_argv(argc, argv);
        return;
    }

    for (int i = 0; i < segment_count; i++) {
        char *argv[SHELL_ARGS_MAX];
        int argc;
        kprintf_output_state_t saved;

        argc = shell_parse(segments[i], argv, SHELL_ARGS_MAX);
        if (argc == 0) continue;
        if (!shell_find_command(argv[0])) {
            kprintf_direct("blueyos: command not found: %s\n", argv[0]);
            kprintf_direct("Type 'help' for available commands.\n");
            shell_buffer_free(&current);
            shell_buffer_free(&next);
            return;
        }

        shell_buffer_free(&next);
        shell_buffer_init(&next);
        shell_pipe_input = current.data;
        shell_pipe_input_len = current.len;
        shell_stage_consumed_output = 0;
        saved = kprintf_get_output_state();
        kprintf_set_output_hook(shell_capture_putc, &next);
        shell_execute_argv(argc, argv);
        kprintf_restore_output_state(saved);

        if (shell_stage_consumed_output && i != (segment_count - 1)) {
            kprintf_direct("blueyos: pager commands must appear at the end of a pipeline\n");
            shell_buffer_free(&current);
            shell_buffer_free(&next);
            shell_pipe_input = NULL;
            shell_pipe_input_len = 0;
            return;
        }

        shell_buffer_free(&current);
        current = next;
        shell_buffer_init(&next);
    }

    shell_pipe_input = NULL;
    shell_pipe_input_len = 0;
    if (!shell_stage_consumed_output && current.data && current.len) {
        shell_direct_write(current.data, current.len);
    }
    if (current.truncated) {
        kprintf_direct("\n[SHL] pipeline output truncated at %u bytes\n", (unsigned)SHELL_CAPTURE_LIMIT);
    }
    shell_buffer_free(&current);
    shell_buffer_free(&next);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void shell_init(void) {
    cwd[0] = '/'; cwd[1] = '\0';
    kprintf("[SHL]  Bluey's Command Post is open for business!\n");
}

void shell_run(void) {
    kprintf("\n" ANSI_BOLD_GREEN "Welcome to BlueyOS Shell!\n");
    kprintf("\"I'm in charge!\" - Bluey Heeler\n");
    kprintf("Type 'help' for a list of commands.\n" ANSI_RESET "\n");

    for (;;) {
        // Print prompt: "bluey@biscuit:/path$ "
        kprintf(ANSI_BOLD_GREEN "bluey@biscuit:" ANSI_BOLD_BLUE "%s" ANSI_RESET "$ ", cwd);

        shell_readline();
        if (linelen == 0) continue;

        // Poll network while we're awake
        tcpip_poll();
        rtc_poll();

        shell_execute_pipeline(linebuf);
    }
}

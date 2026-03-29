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
#include "../kernel/kheap.h"
#include "../kernel/sysinfo.h"
#include "../kernel/swap.h"
#include "../kernel/syslog.h"
#include "../drivers/net/network.h"
#include "../net/tcpip.h"
#include "../net/icmp.h"
#include "shell.h"

// ---------------------------------------------------------------------------
// Shell state
// ---------------------------------------------------------------------------
static char cwd[SHELL_CWD_MAX];

// ---------------------------------------------------------------------------
// Line input
// ---------------------------------------------------------------------------
static char linebuf[SHELL_LINE_MAX];
static int  linelen;

static void shell_readline(void) {
    linelen = 0;
    linebuf[0] = '\0';

    while (1) {
        char c = keyboard_getchar();

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
    kprintf("  mode            show the kernel/user-mode split for this shell\n");
    kprintf("  meminfo         show kernel heap and swap usage\n");
    kprintf("  mount           show mounted filesystems\n");
    kprintf("  ifconfig        show network interfaces\n");
    kprintf("  ping <ip>       send ICMP echo request\n");
    kprintf("  swapinfo        show swap statistics\n");
    kprintf("  dmesg           show kernel log (ring buffer)\n");
    kprintf("  date            show current time\n");
    kprintf("  version         show BlueyOS version\n");
    kprintf("  reboot          restart the computer\n");
    kprintf("  halt            halt the computer\n");
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
    if (argc < 2) { kprintf("cat: missing filename\n"); return; }
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
    kprintf("BlueyOS %s %s blueyos i386 GNU/BlueyOS\n",
            BLUEYOS_VERSION_STRING, BLUEYOS_CODENAME);
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
    kprintf("  PID  UID  MODE    STATE     NAME\n");
    for (uint32_t pid = 0; pid < MAX_PROCESSES; pid++) {
        process_t *p = process_get_by_pid(pid);
        if (!p || p->state == PROC_DEAD) continue;
        const char *st = "UNKNOWN ";
        switch (p->state) {
            case PROC_READY:    st = "READY   "; break;
            case PROC_RUNNING:  st = "RUNNING "; break;
            case PROC_SLEEPING: st = "SLEEP   "; break;
            case PROC_ZOMBIE:   st = "ZOMBIE  "; break;
            default: break;
        }
        kprintf("  %d  %d  %s  %s  %s\n",
                pid, p->uid, process_mode_name(p->mode), st, p->name);
    }
}

static void cmd_mode(int argc, char **argv) {
    (void)argc; (void)argv;
    process_t *p = process_current();
    kprintf("Interactive shell : kernel shell\n");
    kprintf("Current process   : %s\n", p ? p->name : "(none)");
    kprintf("Current mode      : %s\n", p ? process_mode_name(p->mode) : "unknown");
    kprintf("Kernel entry      : direct function calls in ring0\n");
    kprintf("User entry point  : int 0x80 syscalls + ELF loader groundwork\n");
}

static void cmd_meminfo(int argc, char **argv) {
    uint32_t total = 0, used = 0, free = 0;
    (void)argc; (void)argv;
    kheap_get_stats(&total, &used, &free);
    kprintf("Kernel heap total : %u bytes (%u KiB)\n", total, total / 1024);
    kprintf("Kernel heap used  : %u bytes (%u KiB)\n", used, used / 1024);
    kprintf("Kernel heap free  : %u bytes (%u KiB)\n", free, free / 1024);
    if (swap_is_active()) {
        uint32_t swap_total = swap_total_pages() * SWAP_PAGE_SIZE;
        uint32_t swap_used  = swap_used_pages() * SWAP_PAGE_SIZE;
        kprintf("Swap total        : %u bytes (%u KiB)\n", swap_total, swap_total / 1024);
        kprintf("Swap used         : %u bytes (%u KiB)\n", swap_used, swap_used / 1024);
    } else {
        kprintf("Swap              : inactive\n");
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

static void cmd_dmesg(int argc, char **argv) {
    (void)argc; (void)argv;
    syslog_dmesg();
}

static void cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    const timezone_t *tz = sysinfo_get_timezone();
    kprintf("Timezone : %s (UTC+%d)\n", tz->name,
            (int)(tz->offset_seconds / 3600));
    kprintf("Epoch    : %s\n", BANDIT_EPOCH_NAME);
    kprintf("(Hardware RTC not yet polled; BlueyOS tracks kernel ticks)\n");
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
    { "mode",     cmd_mode     },
    { "meminfo",  cmd_meminfo  },
    { "mount",    cmd_mount    },
    { "ifconfig", cmd_ifconfig },
    { "ping",     cmd_ping     },
    { "swapinfo", cmd_swapinfo },
    { "dmesg",    cmd_dmesg    },
    { "date",     cmd_date     },
    { "version",  cmd_version  },
    { "halt",     cmd_halt     },
    { "reboot",   cmd_reboot   },
    { NULL, NULL }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void shell_init(void) {
    cwd[0] = '/'; cwd[1] = '\0';
    kprintf("[SHL]  Bluey's kernel shell is open for business!\n");
}

void shell_run(void) {
    char *argv[SHELL_ARGS_MAX];

    vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("\nWelcome to BlueyOS Kernel Shell!\n");
    kprintf("\"I'm in charge!\" - Bluey Heeler\n");
    kprintf("This console runs in kernel mode; user programs cross via int 0x80.\n");
    kprintf("Type 'help' for a list of commands.\n\n");
    vga_set_color(VGA_WHITE, VGA_BLACK);

    for (;;) {
        // Print prompt: "bluey@biscuit:/path$ "
        vga_set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("bluey@biscuit:");
        vga_set_color(VGA_LIGHT_BLUE, VGA_BLACK);
        kprintf("%s", cwd);
        vga_set_color(VGA_WHITE, VGA_BLACK);
        kprintf("$ ");

        shell_readline();
        if (linelen == 0) continue;

        int argc = shell_parse(linebuf, argv, SHELL_ARGS_MAX);
        if (argc == 0 || !argv[0]) continue;

        // Poll network while we're awake
        tcpip_poll();

        // Find and execute command
        int found = 0;
        for (int i = 0; commands[i].name; i++) {
            if (strcmp(argv[0], commands[i].name) == 0) {
                commands[i].fn(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) {
            kprintf("blueyos: command not found: %s\n", argv[0]);
            kprintf("Type 'help' for available commands.\n");
        }
    }
}

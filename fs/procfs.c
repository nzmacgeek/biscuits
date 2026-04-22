// BlueyOS procfs - "Bluey's Report Card"
// Virtual filesystem exposing kernel state at /proc, similar to Linux procfs.
// "Write it all down, Bluey — that's how you remember the important stuff!" - Bandit
// Episode ref: "The Show" - Bluey presents everything clearly and in order
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

#include "procfs.h"

#include "../include/types.h"
#include "../include/version.h"
#include "../kernel/bootargs.h"
#include "../kernel/kheap.h"
#include "../kernel/netdev.h"
#include "../kernel/process.h"
#include "../kernel/rtc.h"
#include "../kernel/sysinfo.h"
#include "../kernel/timer.h"
#include "../lib/string.h"
#include "vfs.h"

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

#define PROCFS_MAX_OPEN  1024
#define PROCFS_BUF_MAX   2048   /* max bytes for any pre-rendered proc file */

// Node type constants
#define PROCFS_NODE_NONE        0
#define PROCFS_NODE_CMDLINE     1   /* /proc/cmdline  — boot kernel command line   */
#define PROCFS_NODE_UPTIME      2   /* /proc/uptime   — seconds since boot         */
#define PROCFS_NODE_MEMINFO     3   /* /proc/meminfo  — memory statistics          */
#define PROCFS_NODE_VERSION     4   /* /proc/version  — kernel version string      */
#define PROCFS_NODE_LOADAVG     5   /* /proc/loadavg  — load averages (stub)       */
#define PROCFS_NODE_PID_STATUS  6   /* /proc/<pid>/status  — per-process status    */
#define PROCFS_NODE_PID_CMDLINE 7   /* /proc/<pid>/cmdline — per-process cmd line  */
#define PROCFS_NODE_NET_DEV     8   /* /proc/net/dev  — network interface stats    */

// ---------------------------------------------------------------------------
// Per-fd state
// ---------------------------------------------------------------------------

typedef struct {
    int      used;
    uint8_t  node;
    uint8_t  _pad[3];
    uint32_t pid;       /* PID for PROCFS_NODE_PID_* nodes; 0 otherwise  */
    char    *buf;       /* kheap_alloc'd pre-rendered content; NULL for CMDLINE */
    uint32_t buf_len;   /* length of valid bytes in buf                   */
} procfs_file_t;

static procfs_file_t procfs_files[PROCFS_MAX_OPEN];

// ---------------------------------------------------------------------------
// Minimal buffer formatter (no snprintf in freestanding kernel)
// ---------------------------------------------------------------------------

typedef struct {
    char *buf;
    int   pos;
    int   max;
} pbuf_t;

static void pbuf_init(pbuf_t *b, char *buf, int max) {
    b->buf = buf;
    b->pos = 0;
    b->max = max;
    if (max > 0) buf[0] = '\0';
}

static void pbuf_char(pbuf_t *b, char c) {
    if (b->pos < b->max - 1) {
        b->buf[b->pos++] = c;
        b->buf[b->pos] = '\0';
    }
}

static void pbuf_str(pbuf_t *b, const char *s) {
    if (!s) return;
    while (*s) pbuf_char(b, *s++);
}

static void pbuf_uint32(pbuf_t *b, uint32_t v) {
    char tmp[12];
    int i = 0;
    if (v == 0) { pbuf_char(b, '0'); return; }
    while (v > 0 && i < 11) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    while (i > 0) pbuf_char(b, tmp[--i]);
}

/* Print exactly two decimal digits (for sub-second fractions). */
static void pbuf_2digits(pbuf_t *b, uint32_t v) {
    v %= 100u;
    pbuf_char(b, (char)('0' + v / 10u));
    pbuf_char(b, (char)('0' + v % 10u));
}

/* Convert uint32_t to a NUL-terminated decimal string in caller-provided buf. */
static void u32_to_str(uint32_t v, char *buf, int bufsz) {
    char tmp[12];
    int ti = 0, pi = 0;
    if (bufsz <= 0) return;
    if (v == 0) { if (bufsz > 1) { buf[0] = '0'; buf[1] = '\0'; } return; }
    while (v > 0 && ti < 11) { tmp[ti++] = (char)('0' + v % 10); v /= 10; }
    while (ti > 0 && pi < bufsz - 1) buf[pi++] = tmp[--ti];
    buf[pi] = '\0';
}

// ---------------------------------------------------------------------------
// Path-checking helpers
// ---------------------------------------------------------------------------

static int procfs_is_root(const char *path) {
    return strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0;
}

static int procfs_is_net_dir(const char *path) {
    return strcmp(path, "/proc/net") == 0 || strcmp(path, "/proc/net/") == 0;
}

/*
 * Parse /proc/<N>[/subpath] or /proc/self[/subpath].
 * Returns the PID (>0) and sets *subpath to the part after the slash
 * (e.g. "status", "cmdline", or "" for the directory itself).
 * Returns 0 if the path is not a numeric/self PID path.
 */
static uint32_t procfs_parse_pid(const char *path, const char **subpath) {
    if (!path || strncmp(path, "/proc/", 6) != 0) return 0;

    const char *p = path + 6;  /* skip "/proc/" */

    /* /proc/self — alias for the current process */
    if (strncmp(p, "self", 4) == 0 && (p[4] == '/' || p[4] == '\0')) {
        process_t *cur = process_current();
        if (!cur) return 0;
        p += 4;
        if (*p == '/') p++;
        if (subpath) *subpath = p;
        return cur->pid;
    }

    /* Numeric PID */
    if (*p < '0' || *p > '9') return 0;
    uint32_t pid = 0;
    while (*p >= '0' && *p <= '9') {
        pid = pid * 10u + (uint32_t)(*p - '0');
        p++;
    }
    if (pid == 0) return 0;
    if (*p == '/') p++;
    if (subpath) *subpath = p;
    return pid;
}

// ---------------------------------------------------------------------------
// Per-process state helpers
// ---------------------------------------------------------------------------

static char procfs_state_char(proc_state_t s) {
    switch (s) {
        case PROC_RUNNING:  return 'R';
        case PROC_READY:    return 'R';
        case PROC_SLEEPING: return 'S';
        case PROC_WAITING:  return 'D';
        case PROC_STOPPED:  return 'T';
        case PROC_ZOMBIE:   return 'Z';
        default:            return '?';
    }
}

static const char *procfs_state_name(proc_state_t s) {
    switch (s) {
        case PROC_RUNNING:  return "running";
        case PROC_READY:    return "sleeping";
        case PROC_SLEEPING: return "sleeping";
        case PROC_WAITING:  return "disk sleep";
        case PROC_STOPPED:  return "stopped";
        case PROC_ZOMBIE:   return "zombie";
        default:            return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Content render functions
// ---------------------------------------------------------------------------

static void render_uptime(pbuf_t *b) {
    uint32_t secs       = rtc_get_uptime_seconds();
    uint32_t hundredths = (timer_get_ticks() % 1000u) / 10u;
    pbuf_uint32(b, secs);
    pbuf_char(b, '.');
    pbuf_2digits(b, hundredths);
    /* We do not track per-CPU idle time; report 0.00 */
    pbuf_str(b, " 0.00\n");
}

static void render_meminfo(pbuf_t *b) {
    uint32_t total_bytes, used_bytes, free_bytes;
    kheap_get_stats(&total_bytes, &used_bytes, &free_bytes);

    uint32_t ram_mb       = sysinfo_get_ram_mb();
    uint32_t ram_kb       = ram_mb * 1024u;
    uint32_t heap_free_kb = free_bytes / 1024u;
    uint32_t heap_used_kb = used_bytes / 1024u;

    /* Standard Linux /proc/meminfo fields (subset) */
    pbuf_str(b, "MemTotal:        "); pbuf_uint32(b, ram_kb);       pbuf_str(b, " kB\n");
    pbuf_str(b, "MemFree:         "); pbuf_uint32(b, heap_free_kb); pbuf_str(b, " kB\n");
    pbuf_str(b, "MemAvailable:    "); pbuf_uint32(b, heap_free_kb); pbuf_str(b, " kB\n");
    pbuf_str(b, "Buffers:         0 kB\n");
    pbuf_str(b, "Cached:          0 kB\n");
    pbuf_str(b, "SwapCached:      0 kB\n");
    pbuf_str(b, "SwapTotal:       0 kB\n");
    pbuf_str(b, "SwapFree:        0 kB\n");
    /* BlueyOS-specific: kernel heap detail */
    pbuf_str(b, "KernelHeap:      "); pbuf_uint32(b, total_bytes / 1024u); pbuf_str(b, " kB\n");
    pbuf_str(b, "KernelHeapUsed:  "); pbuf_uint32(b, heap_used_kb);        pbuf_str(b, " kB\n");
    pbuf_str(b, "KernelHeapFree:  "); pbuf_uint32(b, heap_free_kb);        pbuf_str(b, " kB\n");
}

static void render_version(pbuf_t *b) {
    pbuf_str(b, BLUEYOS_VERSION_STRING);
    pbuf_str(b, " (");
    pbuf_str(b, BLUEYOS_BUILD_USER);
    pbuf_char(b, '@');
    pbuf_str(b, BLUEYOS_BUILD_HOST);
    pbuf_str(b, ") #");
    pbuf_str(b, BLUEYOS_BUILD_NUMBER_STR);
    pbuf_char(b, ' ');
    pbuf_str(b, BLUEYOS_BUILD_DATE);
    pbuf_char(b, ' ');
    pbuf_str(b, BLUEYOS_BUILD_TIME);
    pbuf_char(b, '\n');
}

static void render_loadavg(pbuf_t *b) {
    /* We do not implement load tracking; report all zeroes. */
    int total = 0;
    process_t *p = process_first();
    while (p) {
        if (p->state != PROC_DEAD) total++;
        p = process_next(p);
    }

    uint32_t cur_pid = 0;
    process_t *cur = process_current();
    if (cur) cur_pid = cur->pid;

    pbuf_str(b, "0.00 0.00 0.00 1/");
    pbuf_uint32(b, (uint32_t)total);
    pbuf_char(b, ' ');
    pbuf_uint32(b, cur_pid);
    pbuf_char(b, '\n');
}

static void render_pid_status(pbuf_t *b, uint32_t pid) {
    process_t *p = process_get_by_pid(pid);
    if (!p) {
        pbuf_str(b, "Name:\t(unknown)\n");
        return;
    }

    /* Approximate virtual memory size from heap + stack regions */
    uint32_t heap_sz = (p->brk_current > p->brk_base)
                       ? p->brk_current - p->brk_base : 0u;
    uint32_t stk_sz  = (p->user_stack_top > p->user_stack_base)
                       ? p->user_stack_top - p->user_stack_base : 0u;
    uint32_t vm_kb   = (heap_sz + stk_sz) / 1024u;

    pbuf_str(b, "Name:\t"); pbuf_str(b, p->name);           pbuf_char(b, '\n');
    pbuf_str(b, "State:\t"); pbuf_char(b, procfs_state_char(p->state));
    pbuf_str(b, " ("); pbuf_str(b, procfs_state_name(p->state)); pbuf_str(b, ")\n");
    pbuf_str(b, "Pid:\t");   pbuf_uint32(b, p->pid);         pbuf_char(b, '\n');
    pbuf_str(b, "PPid:\t");  pbuf_uint32(b, p->parent_pid);  pbuf_char(b, '\n');
    pbuf_str(b, "Uid:\t");
        pbuf_uint32(b, p->uid);  pbuf_char(b, '\t');
        pbuf_uint32(b, p->uid);  pbuf_char(b, '\t');
        pbuf_uint32(b, p->euid); pbuf_char(b, '\t');
        pbuf_uint32(b, p->euid); pbuf_char(b, '\n');
    pbuf_str(b, "Gid:\t");
        pbuf_uint32(b, p->gid);  pbuf_char(b, '\t');
        pbuf_uint32(b, p->gid);  pbuf_char(b, '\t');
        pbuf_uint32(b, p->egid); pbuf_char(b, '\t');
        pbuf_uint32(b, p->egid); pbuf_char(b, '\n');
    pbuf_str(b, "VmSize:\t"); pbuf_uint32(b, vm_kb); pbuf_str(b, " kB\n");
    pbuf_str(b, "VmRSS:\t");  pbuf_uint32(b, vm_kb); pbuf_str(b, " kB\n");
    pbuf_str(b, "Threads:\t1\n");
}

/*
 * /proc/<pid>/cmdline — NUL-separated argument list (Linux-compatible).
 * We store only the process name since we do not keep full argv[].
 */
static void render_pid_cmdline(pbuf_t *b, uint32_t pid) {
    process_t *p = process_get_by_pid(pid);
    if (p) {
        pbuf_str(b, p->name);
        pbuf_char(b, '\0');   /* Linux: args are NUL-separated, final NUL */
    }
}

static void render_net_dev(pbuf_t *b) {
    pbuf_str(b, "Inter-|   Receive                                                |  Transmit\n");
    pbuf_str(b, " face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");

    netdev_device_t *devs[NETDEV_MAX_DEVICES];
    int count = 0;
    netdev_list_all(devs, &count, NETDEV_MAX_DEVICES);

    for (int i = 0; i < count; i++) {
        netdev_device_t *dev = devs[i];
        if (!dev) continue;

        /* Right-justify the interface name in a 6-char field */
        int nlen = (int)strlen(dev->name);
        for (int j = nlen; j < 6; j++) pbuf_char(b, ' ');
        pbuf_str(b, dev->name);
        pbuf_str(b, ": ");

        /* Receive counters */
        pbuf_uint32(b, dev->rx_bytes);   pbuf_char(b, ' ');
        pbuf_uint32(b, dev->rx_packets); pbuf_char(b, ' ');
        pbuf_uint32(b, dev->rx_errors);
        pbuf_str(b, " 0 0 0 0 0 ");

        /* Transmit counters */
        pbuf_uint32(b, dev->tx_bytes);   pbuf_char(b, ' ');
        pbuf_uint32(b, dev->tx_packets); pbuf_char(b, ' ');
        pbuf_uint32(b, dev->tx_errors);
        pbuf_str(b, " 0 0 0 0 0\n");
    }
}

// ---------------------------------------------------------------------------
// Filesystem vtable callbacks
// ---------------------------------------------------------------------------

static int procfs_mount_cb(const char *mountpoint, uint32_t start_lba) {
    (void)mountpoint;
    (void)start_lba;
    memset(procfs_files, 0, sizeof(procfs_files));
    return 0;
}

static int procfs_alloc_fd(void) {
    for (int i = 0; i < PROCFS_MAX_OPEN; i++) {
        if (!procfs_files[i].used) return i;
    }
    return -1;
}

static int procfs_open_cb(const char *path, int flags) {
    if (!path) return -1;

    /* procfs is read-only; reject any write-mode flags */
    if ((flags & (VFS_O_WRONLY | VFS_O_RDWR | VFS_O_CREAT |
                  VFS_O_TRUNC  | VFS_O_APPEND)) != 0) return -1;

    int fd = procfs_alloc_fd();
    if (fd < 0) return -1;

    uint8_t  node    = PROCFS_NODE_NONE;
    uint32_t pid     = 0;
    char    *buf     = NULL;
    uint32_t buf_len = 0;

    /* --- Static system-wide files ---------------------------------------- */
    if (strcmp(path, "/proc/cmdline") == 0) {
        node = PROCFS_NODE_CMDLINE;   /* rendered on-the-fly in read_at */

    } else if (strcmp(path, "/proc/uptime")  == 0) { node = PROCFS_NODE_UPTIME;
    } else if (strcmp(path, "/proc/meminfo") == 0) { node = PROCFS_NODE_MEMINFO;
    } else if (strcmp(path, "/proc/version") == 0) { node = PROCFS_NODE_VERSION;
    } else if (strcmp(path, "/proc/loadavg") == 0) { node = PROCFS_NODE_LOADAVG;
    } else if (strcmp(path, "/proc/net/dev") == 0) { node = PROCFS_NODE_NET_DEV;

    /* --- Per-process files ------------------------------------------------ */
    } else {
        const char *sub = NULL;
        pid = procfs_parse_pid(path, &sub);
        if (!pid) return -1;
        if (!process_get_by_pid(pid)) return -1;

        if (!sub || sub[0] == '\0') {
            /* /proc/<pid>/ is a directory — not a regular file */
            return -1;
        } else if (strcmp(sub, "status")  == 0) {
            node = PROCFS_NODE_PID_STATUS;
        } else if (strcmp(sub, "cmdline") == 0) {
            node = PROCFS_NODE_PID_CMDLINE;
        } else {
            return -1;
        }
    }

    if (node == PROCFS_NODE_NONE) return -1;

    /* Pre-render content into a heap-allocated buffer for all nodes except
     * CMDLINE (which uses boot_args_cmdline() directly on every read). */
    if (node != PROCFS_NODE_CMDLINE) {
        buf = (char *)kheap_alloc(PROCFS_BUF_MAX, 0);
        if (!buf) return -1;

        pbuf_t pb;
        pbuf_init(&pb, buf, PROCFS_BUF_MAX);

        switch (node) {
            case PROCFS_NODE_UPTIME:      render_uptime(&pb);           break;
            case PROCFS_NODE_MEMINFO:     render_meminfo(&pb);          break;
            case PROCFS_NODE_VERSION:     render_version(&pb);          break;
            case PROCFS_NODE_LOADAVG:     render_loadavg(&pb);          break;
            case PROCFS_NODE_PID_STATUS:  render_pid_status(&pb, pid);  break;
            case PROCFS_NODE_PID_CMDLINE: render_pid_cmdline(&pb, pid); break;
            case PROCFS_NODE_NET_DEV:     render_net_dev(&pb);          break;
            default: break;
        }
        buf_len = (uint32_t)pb.pos;
    }

    procfs_files[fd].used    = 1;
    procfs_files[fd].node    = node;
    procfs_files[fd].pid     = pid;
    procfs_files[fd].buf     = buf;
    procfs_files[fd].buf_len = buf_len;
    return fd;
}

static int procfs_read_at_cb(int fd, uint8_t *buf, size_t len, uint32_t offset) {
    if (fd < 0 || fd >= PROCFS_MAX_OPEN || !procfs_files[fd].used) return -1;
    if (!buf) return -1;
    if (len == 0) return 0;

    /* /proc/cmdline: regenerate on every read (boot cmdline is static anyway) */
    if (procfs_files[fd].node == PROCFS_NODE_CMDLINE) {
        const char *cmdline     = boot_args_cmdline();
        size_t      cmdline_len = strlen(cmdline);
        size_t      total       = cmdline_len > 0 ? cmdline_len + 1u : 1u;

        if (offset >= (uint32_t)total) return 0;

        size_t copied = 0;
        if (offset < cmdline_len) {
            size_t remaining = cmdline_len - offset;
            size_t chunk     = len < remaining ? len : remaining;
            memcpy(buf, cmdline + offset, chunk);
            copied = chunk;
        }
        if (copied < len && offset + copied < total) {
            buf[copied++] = '\n';
        }
        return (int)copied;
    }

    /* All other nodes: serve from the pre-rendered buffer */
    const char *src     = procfs_files[fd].buf;
    uint32_t    src_len = procfs_files[fd].buf_len;

    if (!src || offset >= src_len) return 0;
    size_t avail = src_len - offset;
    size_t chunk = len < avail ? len : avail;
    memcpy(buf, src + offset, chunk);
    return (int)chunk;
}

static int procfs_close_cb(int fd) {
    if (fd < 0 || fd >= PROCFS_MAX_OPEN) return -1;
    if (procfs_files[fd].buf) kheap_free(procfs_files[fd].buf);
    memset(&procfs_files[fd], 0, sizeof(procfs_files[fd]));
    return 0;
}

static int procfs_readdir_cb(const char *path, vfs_dirent_t *out, int max) {
    if (!out || max <= 0) return -1;

    /* --- /proc root directory -------------------------------------------- */
    if (procfs_is_root(path)) {
        static const struct { const char *name; uint32_t inode; uint8_t is_dir; }
        static_entries[] = {
            { "cmdline", 1, 0 },
            { "uptime",  2, 0 },
            { "meminfo", 3, 0 },
            { "version", 4, 0 },
            { "loadavg", 5, 0 },
            { "net",     6, 1 },
        };
        static const int N_STATIC = (int)(sizeof(static_entries)/sizeof(static_entries[0]));

        int n = 0;
        for (int i = 0; i < N_STATIC && n < max; i++) {
            memset(&out[n], 0, sizeof(out[n]));
            strncpy(out[n].name, static_entries[i].name, sizeof(out[n].name) - 1);
            out[n].inode  = static_entries[i].inode;
            out[n].is_dir = static_entries[i].is_dir;
            n++;
        }

        /* Per-process directories (one entry per live process) */
        process_t *p = process_first();
        while (p && n < max) {
            if (p->state != PROC_DEAD) {
                char pid_str[12];
                u32_to_str(p->pid, pid_str, sizeof(pid_str));
                memset(&out[n], 0, sizeof(out[n]));
                strncpy(out[n].name, pid_str, sizeof(out[n].name) - 1);
                out[n].inode  = 100u + p->pid;   /* offset to avoid clash with static inodes */
                out[n].is_dir = 1;
                n++;
            }
            p = process_next(p);
        }
        return n;
    }

    /* --- /proc/net directory --------------------------------------------- */
    if (procfs_is_net_dir(path)) {
        if (max < 1) return 0;
        memset(&out[0], 0, sizeof(out[0]));
        strncpy(out[0].name, "dev", sizeof(out[0].name) - 1);
        out[0].inode  = 1;
        out[0].is_dir = 0;
        return 1;
    }

    /* --- /proc/<pid> directory ------------------------------------------- */
    {
        const char *sub = NULL;
        uint32_t pid = procfs_parse_pid(path, &sub);
        if (pid && process_get_by_pid(pid) && (!sub || sub[0] == '\0')) {
            int n = 0;
            static const char *pid_entries[] = { "status", "cmdline" };
            for (int i = 0; i < 2 && n < max; i++) {
                memset(&out[n], 0, sizeof(out[n]));
                strncpy(out[n].name, pid_entries[i], sizeof(out[n].name) - 1);
                out[n].inode  = pid * 10u + (uint32_t)(i + 1u);
                out[n].is_dir = 0;
                n++;
            }
            return n;
        }
    }

    return -1;
}

static int procfs_stat_cb(const char *path, vfs_stat_t *out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    /* /proc root */
    if (procfs_is_root(path)) {
        out->mode   = VFS_S_IFDIR | VFS_S_IRUSR | VFS_S_IXUSR |
                      VFS_S_IRGRP | VFS_S_IXGRP |
                      VFS_S_IROTH | VFS_S_IXOTH;
        out->is_dir = 1;
        out->inode  = 1;
        return 0;
    }

    /* /proc/net directory */
    if (procfs_is_net_dir(path)) {
        out->mode   = VFS_S_IFDIR | VFS_S_IRUSR | VFS_S_IXUSR |
                      VFS_S_IRGRP | VFS_S_IXGRP |
                      VFS_S_IROTH | VFS_S_IXOTH;
        out->is_dir = 1;
        out->inode  = 6;
        return 0;
    }

    /* Static regular files */
    if (strcmp(path, "/proc/cmdline") == 0) {
        size_t len = strlen(boot_args_cmdline());
        out->mode  = VFS_S_IFREG | VFS_S_IRUSR | VFS_S_IRGRP | VFS_S_IROTH;
        out->size  = (uint32_t)(len > 0 ? len + 1u : 1u);
        out->inode = 1;
        return 0;
    }
    if (strcmp(path, "/proc/uptime")    == 0 ||
        strcmp(path, "/proc/meminfo")   == 0 ||
        strcmp(path, "/proc/version")   == 0 ||
        strcmp(path, "/proc/loadavg")   == 0 ||
        strcmp(path, "/proc/net/dev")   == 0) {
        out->mode  = VFS_S_IFREG | VFS_S_IRUSR | VFS_S_IRGRP | VFS_S_IROTH;
        out->size  = 0;   /* dynamic; unknown without rendering */
        out->inode = 0;
        return 0;
    }

    /* /proc/<pid> and /proc/<pid>/{status,cmdline} */
    {
        const char *sub = NULL;
        uint32_t pid = procfs_parse_pid(path, &sub);
        if (pid) {
            process_t *p = process_get_by_pid(pid);
            if (!p) return -1;

            if (!sub || sub[0] == '\0') {
                /* /proc/<pid>/ — directory owned by the process's uid/gid */
                out->mode   = VFS_S_IFDIR | VFS_S_IRUSR | VFS_S_IXUSR |
                              VFS_S_IRGRP | VFS_S_IXGRP |
                              VFS_S_IROTH | VFS_S_IXOTH;
                out->uid    = p->uid;
                out->gid    = p->gid;
                out->inode  = 100u + pid;
                out->is_dir = 1;
                return 0;
            }
            if (strcmp(sub, "status") == 0 || strcmp(sub, "cmdline") == 0) {
                out->mode  = VFS_S_IFREG | VFS_S_IRUSR | VFS_S_IRGRP | VFS_S_IROTH;
                out->uid   = p->uid;
                out->gid   = p->gid;
                out->inode = pid * 10u + (strcmp(sub, "status") == 0 ? 1u : 2u);
                return 0;
            }
        }
    }

    return -1;
}

static filesystem_t procfs_vtable = {
    .name     = "procfs",
    .mount    = procfs_mount_cb,
    .open     = procfs_open_cb,
    .read     = NULL,
    .read_at  = procfs_read_at_cb,
    .write    = NULL,
    .close    = procfs_close_cb,
    .readdir  = procfs_readdir_cb,
    .mkdir    = NULL,
    .unlink   = NULL,
    .stat     = procfs_stat_cb,
    .link     = NULL,
    .symlink  = NULL,
    .readlink = NULL,
    .chmod    = NULL,
    .chown    = NULL,
};

filesystem_t *procfs_get_filesystem(void) {
    return &procfs_vtable;
}

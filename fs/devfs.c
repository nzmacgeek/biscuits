// BlueyOS devfs — "Bandit's Toolbelt"
// An in-memory virtual filesystem mounted at /dev.
//
// Episode ref: "Dad Baby" — every tool has its place.
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

#include "devfs.h"
#include "../include/types.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "../kernel/tty.h"
#include "../kernel/timer.h"
#include "../kernel/kdbg.h"
#include "vfs.h"

// ---------------------------------------------------------------------------
// Node types
// ---------------------------------------------------------------------------
#define DEVNODE_NULL     1   // /dev/null — reads return 0, writes discard
#define DEVNODE_ZERO     2   // /dev/zero — reads return 0x00 bytes
#define DEVNODE_RANDOM   3   // /dev/random, /dev/urandom — PRNG bytes
#define DEVNODE_TTY_CON  4   // /dev/console, /dev/tty0, /dev/tty1, /dev/ttyS0
#define DEVNODE_TTY_CTL  5   // /dev/tty — process controlling terminal
#define DEVNODE_DISK     6   // /dev/disk* — whole-disk block nodes (stat only)
#define DEVNODE_PART     7   // /dev/disk*s* — partition block nodes (stat only)
#define DEVNODE_FLOPPY   8   // /dev/fd* — floppy block nodes (stat only)
#define DEVNODE_NET      9   // /dev/eth*, /dev/wifi* — net char nodes (stat only)
#define DEVNODE_STDIN    10  // /dev/stdin  — alias to TTY_CTL
#define DEVNODE_STDOUT   11  // /dev/stdout — alias to TTY_CTL
#define DEVNODE_STDERR   12  // /dev/stderr — alias to TTY_CTL
#define DEVNODE_TTY_VT   13  // /dev/tty2, /dev/tty3 — additional VTs (stat/readdir; open intercepted by tty_device_path_kind)
#define DEVNODE_KDBG     14  // /dev/kdbg — runtime debug flag control

// ---------------------------------------------------------------------------
// Static device node table
// ---------------------------------------------------------------------------
typedef struct {
    const char *name;    // basename under /dev/
    uint8_t     type;    // DEVNODE_* constant
    uint16_t    mode;    // VFS mode bits
} devfs_node_t;

static const devfs_node_t devfs_nodes[] = {
    // character devices
    { "null",    DEVNODE_NULL,    VFS_S_IFCHR | 0666 },
    { "zero",    DEVNODE_ZERO,    VFS_S_IFCHR | 0666 },
    { "random",  DEVNODE_RANDOM,  VFS_S_IFCHR | 0444 },
    { "urandom", DEVNODE_RANDOM,  VFS_S_IFCHR | 0444 },
    // tty devices
    { "console", DEVNODE_TTY_CON, VFS_S_IFCHR | 0620 },
    { "tty",     DEVNODE_TTY_CTL, VFS_S_IFCHR | 0666 },
    { "tty0",    DEVNODE_TTY_CON, VFS_S_IFCHR | 0620 },
    { "tty1",    DEVNODE_TTY_CON, VFS_S_IFCHR | 0620 },
    { "tty2",    DEVNODE_TTY_VT,  VFS_S_IFCHR | 0620 },
    { "tty3",    DEVNODE_TTY_VT,  VFS_S_IFCHR | 0620 },
    { "ttyS0",   DEVNODE_TTY_CON, VFS_S_IFCHR | 0620 },
    // stdio aliases
    { "stdin",   DEVNODE_STDIN,   VFS_S_IFCHR | 0666 },
    { "stdout",  DEVNODE_STDOUT,  VFS_S_IFCHR | 0666 },
    { "stderr",  DEVNODE_STDERR,  VFS_S_IFCHR | 0666 },
    // floppy drives
    { "fd0",     DEVNODE_FLOPPY,  VFS_S_IFBLK | 0660u },
    { "fd1",     DEVNODE_FLOPPY,  VFS_S_IFBLK | 0660u },
    // ATA/SATA disks (BSD-style: disk0, disk1, …)
    { "disk0",   DEVNODE_DISK,    VFS_S_IFBLK | 0660u },
    { "disk1",   DEVNODE_DISK,    VFS_S_IFBLK | 0660u },
    // Partitions/slices (BSD-style: disk0s1, disk0s2, disk0s3)
    { "disk0s1", DEVNODE_PART,    VFS_S_IFBLK | 0660u },
    { "disk0s2", DEVNODE_PART,    VFS_S_IFBLK | 0660u },
    { "disk0s3", DEVNODE_PART,    VFS_S_IFBLK | 0660u },
    { "disk1s1", DEVNODE_PART,    VFS_S_IFBLK | 0660u },
    { "disk1s2", DEVNODE_PART,    VFS_S_IFBLK | 0660u },
    { "disk1s3", DEVNODE_PART,    VFS_S_IFBLK | 0660u },
    // Ethernet interfaces
    { "eth0",    DEVNODE_NET,     VFS_S_IFCHR | 0640u },
    { "eth1",    DEVNODE_NET,     VFS_S_IFCHR | 0640u },
    { "eth2",    DEVNODE_NET,     VFS_S_IFCHR | 0640u },
    { "eth3",    DEVNODE_NET,     VFS_S_IFCHR | 0640u },
    // Wireless interfaces
    { "wifi0",   DEVNODE_NET,     VFS_S_IFCHR | 0640u },
    { "wifi1",   DEVNODE_NET,     VFS_S_IFCHR | 0640u },
    // debug control
    { "kdbg",    DEVNODE_KDBG,   VFS_S_IFCHR | 0600 },
};
#define DEVFS_NODE_COUNT ((int)(sizeof(devfs_nodes) / sizeof(devfs_nodes[0])))

// ---------------------------------------------------------------------------
// Open fd table
// ---------------------------------------------------------------------------
#define DEVFS_MAX_OPEN 1024

typedef struct {
    int     used;
    uint8_t node_type;
} devfs_fd_t;

static devfs_fd_t devfs_fds[DEVFS_MAX_OPEN];

// ---------------------------------------------------------------------------
// PRNG — simple XorShift32, seeded lazily from the timer tick counter
// ---------------------------------------------------------------------------
static uint32_t devfs_prng_state = 0;

static uint32_t devfs_rand(void) {
    if (devfs_prng_state == 0) {
        devfs_prng_state = timer_get_ticks();
        if (devfs_prng_state == 0) devfs_prng_state = 0xDEADBEEFu;
    }
    uint32_t x = devfs_prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    devfs_prng_state = x;
    return x;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/* Return a pointer to the node entry matching path, or NULL. */
static const devfs_node_t *devfs_lookup(const char *path) {
    if (!path) return NULL;

    /* Strip the /dev/ prefix — accept both "/dev/foo" and "foo" */
    const char *name = path;
    if (name[0] == '/' && name[1] == 'd' && name[2] == 'e' &&
        name[3] == 'v' && name[4] == '/') {
        name = path + 5;  /* skip "/dev/" */
    } else if (name[0] == '/') {
        return NULL;  /* some other absolute path — not ours */
    }

    for (int i = 0; i < DEVFS_NODE_COUNT; i++) {
        if (strcmp(devfs_nodes[i].name, name) == 0)
            return &devfs_nodes[i];
    }
    return NULL;
}

/* Allocate a devfs-local fd slot. Returns index in devfs_fds or -1. */
static int devfs_alloc_fd(uint8_t node_type) {
    for (int i = 0; i < DEVFS_MAX_OPEN; i++) {
        if (!devfs_fds[i].used) {
            devfs_fds[i].used      = 1;
            devfs_fds[i].node_type = node_type;
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Filesystem vtable callbacks
// ---------------------------------------------------------------------------

static int devfs_mount_cb(const char *mountpoint, uint32_t start_lba) {
    (void)mountpoint;
    (void)start_lba;
    memset(devfs_fds, 0, sizeof(devfs_fds));
    devfs_prng_state = 0;
    kprintf("[DEVFS] Mounted at %s — %d devices registered\n",
            mountpoint ? mountpoint : "/dev", DEVFS_NODE_COUNT);
    return 0;
}

static int devfs_open_cb(const char *path, int flags) {
    const devfs_node_t *node = devfs_lookup(path);
    if (!node) return -1;

    /* devfs is structurally read-only (no dynamic node creation), but opening
     * existing nodes with O_CREAT/O_TRUNC is common from shell redirections
     * (e.g. ">/dev/null"). Linux accepts this for device nodes, so do too. */
    (void)flags;

    /* Block devices and net nodes are not directly readable/writable via
     * open() — they are accessed through the rootfs/VFS mount layer or the
     * network stack respectively. */
    if (node->type == DEVNODE_DISK || node->type == DEVNODE_PART ||
        node->type == DEVNODE_FLOPPY || node->type == DEVNODE_NET) return -1;

    /* tty2/tty3: open is intercepted by tty_device_path_kind in vfs_open before
     * devfs is consulted. If we somehow get here, return a generic fd slot. */
    return devfs_alloc_fd(node->type);
}

static int devfs_read_cb(int fd, uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= DEVFS_MAX_OPEN || !devfs_fds[fd].used) return -1;
    if (!buf || len == 0) return 0;

    switch (devfs_fds[fd].node_type) {
        case DEVNODE_NULL:
            return 0;   /* EOF immediately */

        case DEVNODE_ZERO:
            memset(buf, 0, len);
            return (int)len;

        case DEVNODE_RANDOM:
            for (size_t i = 0; i < len; ) {
                uint32_t r = devfs_rand();
                size_t chunk = len - i < 4 ? len - i : 4;
                for (size_t b = 0; b < chunk; b++, i++)
                    buf[i] = (uint8_t)(r >> (b * 8));
            }
            return (int)len;

        case DEVNODE_TTY_CON:
        case DEVNODE_TTY_CTL:
        case DEVNODE_TTY_VT:
        case DEVNODE_STDIN:
            return tty_read((char*)buf, len);

        case DEVNODE_STDOUT:
        case DEVNODE_STDERR:
            /* These aren't sensible to read from, but return 0 gracefully */
            return 0;

        case DEVNODE_KDBG: {
            /* Read returns current kdbg_flags as "0xNNNNNNNN\n" */
            uint32_t flags = kdbg_get();
            char tmp[12];
            int pos = 0;
            tmp[pos++] = '0'; tmp[pos++] = 'x';
            for (int shift = 28; shift >= 0; shift -= 4) {
                uint32_t nibble = (flags >> shift) & 0xF;
                tmp[pos++] = (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
            }
            tmp[pos++] = '\n';
            size_t out = len < (size_t)pos ? len : (size_t)pos;
            memcpy(buf, tmp, out);
            return (int)out;
        }

        default:
            return -1;
    }
}

static int devfs_write_cb(int fd, const uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= DEVFS_MAX_OPEN || !devfs_fds[fd].used) return -1;
    if (!buf || len == 0) return 0;

    switch (devfs_fds[fd].node_type) {
        case DEVNODE_NULL:
            return (int)len;   /* discard all writes */

        case DEVNODE_ZERO:
            return (int)len;   /* also discard */

        case DEVNODE_RANDOM:
            /* Writing to /dev/random adds entropy — seed our PRNG */
            if (len >= 4) {
                uint32_t seed;
                memcpy(&seed, buf, 4);
                devfs_prng_state ^= seed ^ timer_get_ticks();
            }
            return (int)len;

        case DEVNODE_TTY_CON:
        case DEVNODE_TTY_CTL:
        case DEVNODE_STDOUT:
        case DEVNODE_STDERR:
            tty_write((const char*)buf, len);
            tty_flush();
            return (int)len;

        case DEVNODE_STDIN:
            return -1;   /* stdin is not writable */

        case DEVNODE_KDBG: {
            /* Write hex value to set kdbg_flags, e.g. "echo 0x3 > /dev/kdbg" */
            if (len == 0) return 0;
            uint32_t v = 0;
            const char *p = (const char *)buf;
            size_t i = 0;
            while (i < len && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) { p++; i++; }
            if (i < len && p[0] == '0' && i + 1 < len && (p[1] == 'x' || p[1] == 'X')) { p += 2; i += 2; }
            while (i < len) {
                char c = *p++;
                i++;
                uint32_t nibble;
                if (c >= '0' && c <= '9')      nibble = (uint32_t)(c - '0');
                else if (c >= 'a' && c <= 'f') nibble = (uint32_t)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') nibble = (uint32_t)(c - 'A' + 10);
                else break;
                v = (v << 4) | nibble;
            }
            kdbg_set(v);
            return (int)len;
        }

        case DEVNODE_DISK:
        case DEVNODE_PART:
        case DEVNODE_FLOPPY:
        case DEVNODE_NET:
            return -1;

        default:
            return -1;
    }
}

static int devfs_read_at_cb(int fd, uint8_t *buf, size_t len, uint32_t offset) {
    /* Character devices have no meaningful offset — delegate to sequential read */
    (void)offset;
    return devfs_read_cb(fd, buf, len);
}

static int devfs_close_cb(int fd) {
    if (fd < 0 || fd >= DEVFS_MAX_OPEN) return -1;
    devfs_fds[fd].used      = 0;
    devfs_fds[fd].node_type = 0;
    return 0;
}

static int devfs_stat_cb(const char *path, vfs_stat_t *out) {
    if (!out) return -1;

    /* stat("/dev") or stat("/dev/") — report as directory */
    if (strcmp(path, "/dev") == 0 || strcmp(path, "/dev/") == 0) {
        memset(out, 0, sizeof(*out));
        out->mode   = VFS_S_IFDIR | 0755;
        out->uid    = 0;
        out->gid    = 0;
        out->is_dir = 1;
        return 0;
    }

    const devfs_node_t *node = devfs_lookup(path);
    if (!node) return -1;

    memset(out, 0, sizeof(*out));
    out->mode   = node->mode;
    out->uid    = 0;
    out->gid    = 0;
    out->size   = 0;
    out->is_dir = 0;
    return 0;
}

static int devfs_readdir_cb(const char *path, vfs_dirent_t *out, int max) {
    /* Only the root /dev directory is listable */
    if (!out || max <= 0) return -1;
    if (strcmp(path, "/dev") != 0 && strcmp(path, "/dev/") != 0) return -1;

    int count = DEVFS_NODE_COUNT < max ? DEVFS_NODE_COUNT : max;
    for (int i = 0; i < count; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        strncpy(out[i].name, devfs_nodes[i].name, sizeof(out[i].name) - 1);
        out[i].name[sizeof(out[i].name) - 1] = '\0';
        out[i].size   = 0;
        out[i].inode  = (uint32_t)(i + 1);
        out[i].is_dir = 0;
    }
    return count;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static filesystem_t devfs_vtable = {
    .name     = "devfs",
    .mount    = devfs_mount_cb,
    .open     = devfs_open_cb,
    .read     = devfs_read_cb,
    .read_at  = devfs_read_at_cb,
    .write    = devfs_write_cb,
    .close    = devfs_close_cb,
    .readdir  = devfs_readdir_cb,
    .mkdir    = NULL,
    .unlink   = NULL,
    .stat     = devfs_stat_cb,
    .link     = NULL,
    .symlink  = NULL,
    .readlink = NULL,
    .chmod    = NULL,
    .chown    = NULL,
};

filesystem_t *devfs_get_filesystem(void) {
    return &devfs_vtable;
}

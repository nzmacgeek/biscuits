// BlueyOS VFS - Virtual Filesystem Layer
// "Bingo's Backpack Filesystem - everything has a place!"
// Episode ref: "Sleepytime" - an organised dream world
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../kernel/devev.h"
#include "vfs.h"

static filesystem_t *fs_registry[VFS_MAX_MOUNTS];
static int           fs_count = 0;

static vfs_mount_t   mounts[VFS_MAX_MOUNTS];
static int           mount_count = 0;

// Open file descriptor table
typedef struct {
    int    used;
    int    fs_idx;       // which mount point
    int    fs_fd;        // fd returned by the underlying fs
    char   path[VFS_PATH_LEN];
    uint8_t fd_type;     // VFS_FD_TYPE_FILE or VFS_FD_TYPE_DEVEV
} vfs_fd_t;

static vfs_fd_t fd_table[VFS_MAX_OPEN];

void vfs_init(void) {
    fs_count    = 0;
    mount_count = 0;
    for (int i = 0; i < VFS_MAX_OPEN;   i++) fd_table[i].used = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        fs_registry[i] = NULL;
        mounts[i].active = 0;
    }
    kprintf("%s\n", MSG_VFS_INIT);
}

void vfs_register_fs(filesystem_t *fs) {
    if (fs_count >= VFS_MAX_MOUNTS) {
        kprintf("[VFS]  Too many filesystems!\n"); return;
    }
    fs_registry[fs_count++] = fs;
    kprintf("[VFS]  Registered filesystem: %s\n", fs->name);
}

int vfs_mount(const char *path, const char *fs_name, uint32_t start_lba) {
    if (mount_count >= VFS_MAX_MOUNTS) return -1;

    // Find registered filesystem by name
    filesystem_t *fs = NULL;
    for (int i = 0; i < fs_count; i++) {
        if (fs_registry[i] && strcmp(fs_registry[i]->name, fs_name) == 0) {
            fs = fs_registry[i]; break;
        }
    }
    if (!fs) { kprintf("[VFS]  Unknown filesystem: %s\n", fs_name); return -1; }

    if (fs->mount && fs->mount(path, start_lba) != 0) {
        kprintf("[VFS]  Mount failed for %s at %s\n", fs_name, path);
        return -1;
    }

    vfs_mount_t *m = &mounts[mount_count++];
    strncpy(m->mountpoint, path, VFS_PATH_LEN - 1);
    m->fs        = fs;
    m->start_lba = start_lba;
    m->active    = 1;

    kprintf("[VFS]  Mounted %s at %s (LBA %d)\n", fs_name, path, start_lba);

    /* Notify the device event channel */
    devev_event_t ev = { DEV_EV_MOUNT, {0,0,0}, 0, 0, 0 };
    devev_push(&ev);

    return 0;
}

int vfs_umount(const char *path) {
    devev_event_t ev;

    if (!path) return -1;

    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;
        if (strcmp(mounts[i].mountpoint, path) != 0) continue;

        mounts[i].active = 0;
        kprintf("[VFS]  Unmounted %s\n", path);

        ev.type = DEV_EV_UMOUNT;
        ev._pad[0] = ev._pad[1] = ev._pad[2] = 0;
        ev.pid = 0;
        ev.code = 0;
        ev.reserved = 0;
        devev_push(&ev);
        return 0;
    }
    kprintf("[VFS]  Unmount: path not found: %s\n", path);
    return -1;
}

// Find the best (longest prefix) mount point for a path
static vfs_mount_t *vfs_find_mount(const char *path) {
    vfs_mount_t *best = NULL;
    size_t best_len = 0;
    for (int i = 0; i < mount_count; i++) {
        if (!mounts[i].active) continue;
        size_t mlen = strlen(mounts[i].mountpoint);
        if (strncmp(path, mounts[i].mountpoint, mlen) == 0 && mlen > best_len) {
            best     = &mounts[i];
            best_len = mlen;
        }
    }
    return best;
}

// Allocate a VFS file descriptor
static int vfs_alloc_fd(void) {
    for (int i = 3; i < VFS_MAX_OPEN; i++) {  // 0,1,2 = stdin/stdout/stderr
        if (!fd_table[i].used) return i;
    }
    return -1;
}

int vfs_devev_open(void) {
    int fd = vfs_alloc_fd();
    if (fd < 0) { kprintf("[VFS]  Out of file descriptors!\n"); return -1; }

    fd_table[fd].used    = 1;
    fd_table[fd].fd_type = VFS_FD_TYPE_DEVEV;
    fd_table[fd].fs_idx  = -1;
    fd_table[fd].fs_fd   = -1;
    fd_table[fd].path[0] = '\0';
    return fd;
}

int vfs_fd_is_devev(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return 0;
    return fd_table[fd].fd_type == VFS_FD_TYPE_DEVEV;
}

int vfs_open(const char *path, int flags) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->open) return -1;

    int fs_fd = m->fs->open(path, flags);
    if (fs_fd < 0) return -1;

    int fd = vfs_alloc_fd();
    if (fd < 0) { kprintf("[VFS]  Out of file descriptors!\n"); return -1; }

    fd_table[fd].used    = 1;
    fd_table[fd].fd_type = VFS_FD_TYPE_FILE;
    fd_table[fd].fs_fd   = fs_fd;
    strncpy(fd_table[fd].path, path, VFS_PATH_LEN - 1);
    // find mount index
    for (int i = 0; i < mount_count; i++) {
        if (&mounts[i] == m) { fd_table[fd].fs_idx = i; break; }
    }
    return fd;
}

int vfs_read(int fd, uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    if (fd_table[fd].fd_type == VFS_FD_TYPE_DEVEV)
        return devev_read_bytes(buf, len);
    vfs_mount_t *m = &mounts[fd_table[fd].fs_idx];
    if (!m->fs->read) return -1;
    return m->fs->read(fd_table[fd].fs_fd, buf, len);
}

int vfs_write(int fd, const uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    vfs_mount_t *m = &mounts[fd_table[fd].fs_idx];
    if (!m->fs->write) return -1;
    return m->fs->write(fd_table[fd].fs_fd, buf, len);
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    if (fd_table[fd].fd_type == VFS_FD_TYPE_FILE) {
        vfs_mount_t *m = &mounts[fd_table[fd].fs_idx];
        if (m->fs->close) m->fs->close(fd_table[fd].fs_fd);
    }
    fd_table[fd].used = 0;
    return 0;
}

int vfs_readdir(const char *path, vfs_dirent_t *out, int max) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->readdir) return -1;
    return m->fs->readdir(path, out, max);
}

int vfs_mkdir(const char *path) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->mkdir) return -1;
    return m->fs->mkdir(path);
}

int vfs_unlink(const char *path) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->unlink) return -1;
    return m->fs->unlink(path);
}

void vfs_print_mounts(void) {
    kprintf("[VFS]  Mount table:\n");
    for (int i = 0; i < mount_count; i++) {
        if (mounts[i].active)
            kprintf("  %s  fs=%s  lba=%d\n",
                    mounts[i].mountpoint, mounts[i].fs->name, mounts[i].start_lba);
    }
    if (!mount_count) kprintf("  (none)\n");
}

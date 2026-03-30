// BlueyOS VFS - Virtual Filesystem Layer
// "Bingo's Backpack Filesystem - everything has a place!"
// Episode ref: "Sleepytime" - an organised dream world
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "vfs.h"
#include "../kernel/process.h"

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
} vfs_fd_t;

static vfs_fd_t fd_table[VFS_MAX_OPEN];

static int vfs_cred_in_group(const vfs_cred_t *cred, uint32_t gid) {
    if (!cred) return 0;
    if (cred->gid == gid) return 1;
    for (uint32_t i = 0; i < cred->group_count; i++) {
        if (cred->groups && cred->groups[i] == gid) return 1;
    }
    return 0;
}

static int vfs_check_mode(const vfs_stat_t *st, uint8_t access, const vfs_cred_t *cred) {
    if (!st) return 0;
    if (!cred || cred->uid == 0) return 1; // root bypass

    uint16_t perms = 0;
    if (cred->uid == st->uid) perms = (uint16_t)((st->mode >> 6) & 0x7);
    else if (vfs_cred_in_group(cred, st->gid)) perms = (uint16_t)((st->mode >> 3) & 0x7);
    else perms = (uint16_t)(st->mode & 0x7);

    return ((perms & access) == access);
}

static void vfs_fill_cred_from_process(vfs_cred_t *cred) {
    process_t *process = process_current();
    if (!cred) return;
    if (!process) {
        cred->uid = 0;
        cred->gid = 0;
        cred->groups = NULL;
        cred->group_count = 0;
        return;
    }
    cred->uid = process->euid;
    cred->gid = process->egid;
    cred->groups = process->groups;
    cred->group_count = process->group_count;
}

static int vfs_parent_path(const char *path, char *out, size_t out_len) {
    if (!path || !out || out_len == 0) return -1;
    size_t len = strlen(path);
    if (len == 0 || len >= out_len) return -1;

    strncpy(out, path, out_len - 1);
    out[out_len - 1] = '\0';
    char *slash = strrchr(out, '/');
    if (!slash) return -1;
    if (slash == out) {
        slash[1] = '\0';
        return 0;
    }
    *slash = '\0';
    return 0;
}

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
    return 0;
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

int vfs_open(const char *path, int flags) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->open) {
        if (path) kprintf("[VFS] Open miss path=%s mount=%d\n", path, !!m);
        return -1;
    }

    vfs_stat_t stat;
    vfs_cred_t cred;
    memset(&stat, 0, sizeof(stat));
    vfs_fill_cred_from_process(&cred);

    if (m->fs->stat && m->fs->stat(path, &stat) == 0) {
        uint8_t need = 0;
        if ((flags & VFS_O_RDWR) == VFS_O_RDWR) need = VFS_ACCESS_READ | VFS_ACCESS_WRITE;
        else if (flags & VFS_O_WRONLY) need = VFS_ACCESS_WRITE;
        else need = VFS_ACCESS_READ;

        if (!vfs_check_mode(&stat, need, &cred)) return -1;
        if (stat.is_dir) return -1;
    } else if (flags & VFS_O_CREAT) {
        char parent[VFS_PATH_LEN];
        vfs_stat_t parent_stat;
        if (vfs_parent_path(path, parent, sizeof(parent)) != 0) return -1;
        if (vfs_stat(parent, &parent_stat) != 0 || !parent_stat.is_dir) return -1;
        if (!vfs_check_mode(&parent_stat, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC, &cred)) {
            return -1;
        }
    }

    int fs_fd = m->fs->open(path, flags);
    if (fs_fd < 0) {
        kprintf("[VFS] FS open failed path=%s fs=%s\n", path, m->fs->name);
        return -1;
    }

    int fd = vfs_alloc_fd();
    if (fd < 0) {
        kprintf("[VFS]  Out of file descriptors for %s!\n", path);
        return -1;
    }

    fd_table[fd].used   = 1;
    fd_table[fd].fs_fd  = fs_fd;
    strncpy(fd_table[fd].path, path, VFS_PATH_LEN - 1);
    // find mount index
    for (int i = 0; i < mount_count; i++) {
        if (&mounts[i] == m) { fd_table[fd].fs_idx = i; break; }
    }
    return fd;
}

int vfs_read(int fd, uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
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
    vfs_mount_t *m = &mounts[fd_table[fd].fs_idx];
    if (m->fs->close) m->fs->close(fd_table[fd].fs_fd);
    fd_table[fd].used = 0;
    return 0;
}

int vfs_readdir(const char *path, vfs_dirent_t *out, int max) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->readdir) return -1;
    if (vfs_access(path, VFS_ACCESS_READ | VFS_ACCESS_EXEC) != 0) return -1;
    return m->fs->readdir(path, out, max);
}

int vfs_mkdir(const char *path) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->mkdir) return -1;
    char parent[VFS_PATH_LEN];
    if (vfs_parent_path(path, parent, sizeof(parent)) != 0 ||
        vfs_access(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC) != 0) {
        return -1;
    }
    return m->fs->mkdir(path);
}

int vfs_unlink(const char *path) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->unlink) return -1;
    char parent[VFS_PATH_LEN];
    if (vfs_parent_path(path, parent, sizeof(parent)) != 0 ||
        vfs_access(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC) != 0) {
        return -1;
    }
    return m->fs->unlink(path);
}

int vfs_stat(const char *path, vfs_stat_t *out) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->stat) return -1;
    return m->fs->stat(path, out);
}

int vfs_access(const char *path, uint8_t access) {
    vfs_cred_t cred;
    vfs_fill_cred_from_process(&cred);
    return vfs_access_cred(path, access, &cred);
}

int vfs_access_cred(const char *path, uint8_t access, const vfs_cred_t *cred) {
    vfs_stat_t stat;
    if (vfs_stat(path, &stat) != 0) return -1;
    return vfs_check_mode(&stat, access, cred) ? 0 : -1;
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

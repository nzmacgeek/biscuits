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
#include "../kernel/syslog.h"
#include "../kernel/process.h"

#define BLUEY_EAGAIN 11

static filesystem_t *fs_registry[VFS_MAX_MOUNTS];
static int           fs_count = 0;

static vfs_mount_t   mounts[VFS_MAX_MOUNTS];
static int           mount_count = 0;

static int vfs_dbg_log_limited(int *counter, int limit) {
    if (*counter >= limit) return 0;
    (*counter)++;
    return 1;
}

// Open file descriptor table
typedef struct {
    int      used;
    int      fs_idx;        // which mount point
    int      fs_fd;         // fd returned by the underlying fs / pipe index
    char     path[VFS_PATH_LEN];
    uint8_t  fd_type;       // VFS_FD_TYPE_FILE, VFS_FD_TYPE_DEVEV, or VFS_FD_TYPE_PIPE
    uint8_t  pipe_is_write; // 1 = write end of a pipe, 0 = read end
    uint32_t offset;        // current seek position (for lseek / sequential reads)
} vfs_fd_t;

static vfs_fd_t fd_table[VFS_MAX_OPEN];

// Pipe buffer pool
#define VFS_PIPE_BUF_SIZE 4096
#define VFS_MAX_PIPES     8

typedef struct {
    uint8_t  buf[VFS_PIPE_BUF_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;          // bytes available to read
    int      read_refcount;  // number of open read-end fds
    int      write_refcount; // number of open write-end fds
    int      used;
} vfs_pipe_t;

static vfs_pipe_t pipe_pool[VFS_MAX_PIPES];

// Allocate a free pipe slot; returns index or -1
static int vfs_pipe_alloc(void) {
    for (int i = 0; i < VFS_MAX_PIPES; i++) {
        if (!pipe_pool[i].used) {
            pipe_pool[i].read_pos      = 0;
            pipe_pool[i].write_pos     = 0;
            pipe_pool[i].count         = 0;
            pipe_pool[i].read_refcount = 0;
            pipe_pool[i].write_refcount = 0;
            pipe_pool[i].used          = 1;
            return i;
        }
    }
    return -1;
}

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
    if (len == 0 || len >= out_len - 1) return -1;

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
    for (int i = 0; i < VFS_MAX_PIPES; i++) pipe_pool[i].used = 0;
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
    static int dbg_calls;
    if (mount_count >= VFS_MAX_MOUNTS) return -1;

    if (vfs_dbg_log_limited(&dbg_calls, 32)) {
        kprintf("[VFS DBG] mount enter path_ptr=%p fs_name_ptr=%p start_lba=%u caller=%p\n",
                path, fs_name, start_lba, __builtin_return_address(0));
        if (path) kprintf("[VFS DBG] mount path='%s'\n", path);
        if (fs_name) kprintf("[VFS DBG] mount fs='%s'\n", fs_name);
    }

    // Find registered filesystem by name
    filesystem_t *fs = NULL;
    for (int i = 0; i < fs_count; i++) {
        if (fs_registry[i] && strcmp(fs_registry[i]->name, fs_name) == 0) {
            fs = fs_registry[i]; break;
        }
    }
    if (!fs) { kprintf("[VFS]  Unknown filesystem: %s\n", fs_name); return -1; }

    if (vfs_dbg_log_limited(&dbg_calls, 32)) {
        kprintf("[VFS DBG] mount dispatch fs=%p mount_cb=%p path_ptr=%p start_lba=%u\n",
                (void *)fs, (void *)fs->mount, path, start_lba);
    }

    if (fs->mount && fs->mount(path, start_lba) != 0) {
        kprintf("[VFS]  Mount failed for %s at %s\n", fs_name, path);
        return -1;
    }

    if (vfs_dbg_log_limited(&dbg_calls, 32)) {
        kprintf("[VFS DBG] mount return fs='%s' path='%s' start_lba=%u\n",
                fs_name, path, start_lba);
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
    fd_table[fd].offset  = 0;
    fd_table[fd].path[0] = '\0';
    return fd;
}

int vfs_fd_is_devev(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return 0;
    return fd_table[fd].fd_type == VFS_FD_TYPE_DEVEV;
}

int vfs_open(const char *path, int flags) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m) {
        if (path) kprintf("[VFS] Open miss path=%s mount=0\n", path);
        return -1;
    }
    /* Debug: show which filesystem and whether it exposes an open() callback */
    if (m->fs) {
        kprintf("[VFS DBG] open request path=%s -> mount=%s fs=%p open=%p\n",
                path, m->mountpoint, (void *)m->fs, (void *)m->fs->open);
    } else {
        kprintf("[VFS DBG] open request path=%s -> mount=%s fs=NULL\n",
                path, m->mountpoint);
    }
    if (!m->fs->open) {
        kprintf("[VFS] Open miss path=%s mount=1 (no open callback)\n", path);
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
        // Creating a child entry requires write + execute (search) on the parent directory.
        if (!vfs_check_mode(&parent_stat, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC, &cred)) {
            return -1;
        }
        // Filesystem create callbacks are responsible for setting ownership/mode.
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

    fd_table[fd].used    = 1;
    fd_table[fd].fd_type = VFS_FD_TYPE_FILE;
    fd_table[fd].fs_fd   = fs_fd;
    fd_table[fd].offset  = 0;
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
    if (fd_table[fd].fd_type == VFS_FD_TYPE_PIPE) {
        int pipe_idx = fd_table[fd].fs_fd;
        if (pipe_idx < 0 || pipe_idx >= VFS_MAX_PIPES) return -1;
        vfs_pipe_t *p = &pipe_pool[pipe_idx];
        size_t avail = p->count < len ? p->count : len;
        if (avail == 0) {
            /* No data available. Return 0 (EOF) if all write ends are closed;
             * return -EAGAIN if writers still exist (non-blocking behaviour:
             * callers that require blocking must retry on EAGAIN). */
            return (p->write_refcount == 0) ? 0 : -BLUEY_EAGAIN;
        }
        for (size_t i = 0; i < avail; i++) {
            buf[i] = p->buf[p->read_pos];
            p->read_pos = (p->read_pos + 1) % VFS_PIPE_BUF_SIZE;
        }
        p->count -= (uint32_t)avail;
        return (int)avail;
    }
    vfs_mount_t *m = &mounts[fd_table[fd].fs_idx];
    if (!m->fs->read_at) {
        if (!m->fs->read) return -1;
        int r = m->fs->read(fd_table[fd].fs_fd, buf, len);
        if (r > 0) fd_table[fd].offset += (uint32_t)r;
        return r;
    }
    int r = m->fs->read_at(fd_table[fd].fs_fd, buf, len, fd_table[fd].offset);
    if (r > 0) fd_table[fd].offset += (uint32_t)r;
    return r;
}

int vfs_read_at(int fd, uint8_t *buf, size_t len, uint32_t offset) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    if (fd_table[fd].fd_type == VFS_FD_TYPE_DEVEV) return -1;
    if (fd_table[fd].fd_type == VFS_FD_TYPE_PIPE) return -1;
    vfs_mount_t *m = &mounts[fd_table[fd].fs_idx];
    if (!m->fs->read_at) {
        /* Fallback: try to emulate by reading from current offset (best-effort) */
        if (!m->fs->read) return -1;
        return -1; /* explicit failure to avoid surprising side-effects */
    }
    return m->fs->read_at(fd_table[fd].fs_fd, buf, len, offset);
}

int vfs_write(int fd, const uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    if (fd_table[fd].fd_type == VFS_FD_TYPE_PIPE) {
        int pipe_idx = fd_table[fd].fs_fd;
        if (pipe_idx < 0 || pipe_idx >= VFS_MAX_PIPES) return -1;
        vfs_pipe_t *p = &pipe_pool[pipe_idx];
        size_t space = VFS_PIPE_BUF_SIZE - p->count;
        size_t towrite = len < space ? len : space;
        for (size_t i = 0; i < towrite; i++) {
            p->buf[p->write_pos] = buf[i];
            p->write_pos = (p->write_pos + 1) % VFS_PIPE_BUF_SIZE;
        }
        p->count += (uint32_t)towrite;
        return (int)towrite;
    }
    if (fd_table[fd].fd_type == VFS_FD_TYPE_DEVEV) return -1;
    vfs_mount_t *m = &mounts[fd_table[fd].fs_idx];
    if (!m->fs->write) return -1;
#ifdef DEBUG
    /* Lightweight instrumentation: record the caller of vfs_write so we can
     * correlate which subsystems are invoking writes (helps find memory
     * corruption sources). Pass the return address from the caller. */
    void *caller = __builtin_return_address(0);
    syslog_record_caller(caller);
    kprintf("[VFS DBG] vfs_write caller=%p fd=%d len=%u\n", caller, fd, (unsigned)len);
#endif

    int r = m->fs->write(fd_table[fd].fs_fd, buf, len);
    if (r > 0) fd_table[fd].offset += (uint32_t)r;
    return r;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    if (fd_table[fd].fd_type == VFS_FD_TYPE_FILE) {
        vfs_mount_t *m = &mounts[fd_table[fd].fs_idx];
        if (m->fs->close) m->fs->close(fd_table[fd].fs_fd);
    } else if (fd_table[fd].fd_type == VFS_FD_TYPE_PIPE) {
        int pipe_idx = fd_table[fd].fs_fd;
        if (pipe_idx >= 0 && pipe_idx < VFS_MAX_PIPES) {
            vfs_pipe_t *p = &pipe_pool[pipe_idx];
            if (fd_table[fd].pipe_is_write) {
                if (p->write_refcount > 0) p->write_refcount--;
            } else {
                if (p->read_refcount > 0) p->read_refcount--;
            }
            if (p->read_refcount == 0 && p->write_refcount == 0) p->used = 0;
        }
    }
    fd_table[fd].used = 0;
    return 0;
}

int32_t vfs_lseek(int fd, int32_t offset, int whence) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    if (fd_table[fd].fd_type != VFS_FD_TYPE_FILE) return -1;

    uint32_t new_offset;
    if (whence == VFS_SEEK_SET) {
        if (offset < 0) return -1;
        new_offset = (uint32_t)offset;
    } else if (whence == VFS_SEEK_CUR) {
        int32_t cur = (int32_t)fd_table[fd].offset;
        int32_t result = cur + offset;
        if (result < 0) return -1; /* would underflow */
        new_offset = (uint32_t)result;
    } else if (whence == VFS_SEEK_END) {
        /* Need the file size */
        vfs_stat_t st;
        if (vfs_fstat(fd, &st) != 0) return -1;
        int32_t result = (int32_t)st.size + offset;
        if (result < 0) return -1; /* would underflow */
        new_offset = (uint32_t)result;
    } else {
        return -1;
    }
    fd_table[fd].offset = new_offset;
    return (int32_t)new_offset;
}

int vfs_dup(int oldfd) {
    if (oldfd < 0 || oldfd >= VFS_MAX_OPEN || !fd_table[oldfd].used) return -1;
    int newfd = vfs_alloc_fd();
    if (newfd < 0) return -1;
    fd_table[newfd] = fd_table[oldfd];
    /* For pipes, increment the appropriate reference count */
    if (fd_table[newfd].fd_type == VFS_FD_TYPE_PIPE) {
        int pipe_idx = fd_table[newfd].fs_fd;
        if (pipe_idx >= 0 && pipe_idx < VFS_MAX_PIPES) {
            if (fd_table[newfd].pipe_is_write)
                pipe_pool[pipe_idx].write_refcount++;
            else
                pipe_pool[pipe_idx].read_refcount++;
        }
    }
    return newfd;
}

int vfs_dup2(int oldfd, int newfd) {
    if (oldfd < 0 || oldfd >= VFS_MAX_OPEN || !fd_table[oldfd].used) return -1;
    if (newfd < 0 || newfd >= VFS_MAX_OPEN) return -1;
    if (oldfd == newfd) return newfd;
    if (fd_table[newfd].used) vfs_close(newfd);
    fd_table[newfd] = fd_table[oldfd];
    /* For pipes, increment the appropriate reference count */
    if (fd_table[newfd].fd_type == VFS_FD_TYPE_PIPE) {
        int pipe_idx = fd_table[newfd].fs_fd;
        if (pipe_idx >= 0 && pipe_idx < VFS_MAX_PIPES) {
            if (fd_table[newfd].pipe_is_write)
                pipe_pool[pipe_idx].write_refcount++;
            else
                pipe_pool[pipe_idx].read_refcount++;
        }
    }
    return newfd;
}

int vfs_pipe(int fds[2]) {
    int pipe_idx = vfs_pipe_alloc();
    if (pipe_idx < 0) return -1;

    int rfd = vfs_alloc_fd();
    if (rfd < 0) { pipe_pool[pipe_idx].used = 0; return -1; }

    /* Temporarily mark rfd used so vfs_alloc_fd skips it for wfd */
    fd_table[rfd].used = 1;
    int wfd = vfs_alloc_fd();
    if (wfd < 0) {
        fd_table[rfd].used = 0;
        pipe_pool[pipe_idx].used = 0;
        return -1;
    }

    fd_table[rfd].used         = 1;
    fd_table[rfd].fd_type      = VFS_FD_TYPE_PIPE;
    fd_table[rfd].pipe_is_write = 0;
    fd_table[rfd].fs_fd        = pipe_idx;
    fd_table[rfd].fs_idx       = -1;
    fd_table[rfd].offset       = 0;
    fd_table[rfd].path[0]      = '\0';

    fd_table[wfd].used         = 1;
    fd_table[wfd].fd_type      = VFS_FD_TYPE_PIPE;
    fd_table[wfd].pipe_is_write = 1;
    fd_table[wfd].fs_fd        = pipe_idx;
    fd_table[wfd].fs_idx       = -1;
    fd_table[wfd].offset       = 0;
    fd_table[wfd].path[0]      = '\0';

    pipe_pool[pipe_idx].read_refcount  = 1;
    pipe_pool[pipe_idx].write_refcount = 1;

    fds[0] = rfd;
    fds[1] = wfd;
    return 0;
}

const char *vfs_fd_get_path(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return NULL;
    if (fd_table[fd].fd_type != VFS_FD_TYPE_FILE) return NULL;
    return fd_table[fd].path;
}

/* Allocate the lowest free fd >= min_fd; used for F_DUPFD semantics */
int vfs_dup_above(int oldfd, int min_fd) {
    if (oldfd < 0 || oldfd >= VFS_MAX_OPEN || !fd_table[oldfd].used) return -1;
    for (int i = min_fd; i < VFS_MAX_OPEN; i++) {
        if (!fd_table[i].used) {
            fd_table[i] = fd_table[oldfd];
            if (fd_table[i].fd_type == VFS_FD_TYPE_PIPE) {
                int pipe_idx = fd_table[i].fs_fd;
                if (pipe_idx >= 0 && pipe_idx < VFS_MAX_PIPES) {
                    if (fd_table[i].pipe_is_write)
                        pipe_pool[pipe_idx].write_refcount++;
                    else
                        pipe_pool[pipe_idx].read_refcount++;
                }
            }
            return i;
        }
    }
    return -1; /* EMFILE */
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

int vfs_rmdir(const char *path) {
    /* Reuse the unlink callback: underlying FS can check if entry is a dir */
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->unlink) return -1;
    vfs_stat_t st;
    if (vfs_stat(path, &st) != 0 || !st.is_dir) return -1;
    char parent[VFS_PATH_LEN];
    if (vfs_parent_path(path, parent, sizeof(parent)) != 0 ||
        vfs_access(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC) != 0) {
        return -1;
    }
    return m->fs->unlink(path);
}

int vfs_unlink(const char *path) {
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->unlink) return -1;
    vfs_stat_t st;
    if (vfs_stat(path, &st) == 0 && st.is_dir) return -1;
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

int vfs_fstat(int fd, vfs_stat_t *out) {
    if (fd < 0 || fd >= VFS_MAX_OPEN) return -1;
    if (!fd_table[fd].used) return -1;
    if (!out) return -1;
    return vfs_stat(fd_table[fd].path, out);
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

int vfs_link(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) return -1;
    vfs_mount_t *m = vfs_find_mount(oldpath);
    if (!m || !m->fs->link) return -1;
    /* newpath must live on the same filesystem */
    vfs_mount_t *mn = vfs_find_mount(newpath);
    if (mn != m) return -1;
    /* caller needs write + exec permission on the parent of newpath */
    char parent[VFS_PATH_LEN];
    vfs_stat_t parent_stat;
    if (vfs_parent_path(newpath, parent, sizeof(parent)) != 0) return -1;
    if (vfs_stat(parent, &parent_stat) != 0 || !parent_stat.is_dir) return -1;
    if (vfs_access(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC) != 0) return -1;
    return m->fs->link(oldpath, newpath);
}

int vfs_symlink(const char *target, const char *linkpath) {
    if (!target || !linkpath) return -1;
    vfs_mount_t *m = vfs_find_mount(linkpath);
    if (!m || !m->fs->symlink) return -1;
    /* caller needs write + exec permission on the parent directory */
    char parent[VFS_PATH_LEN];
    vfs_stat_t parent_stat;
    if (vfs_parent_path(linkpath, parent, sizeof(parent)) != 0) return -1;
    if (vfs_stat(parent, &parent_stat) != 0 || !parent_stat.is_dir) return -1;
    if (vfs_access(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC) != 0) return -1;
    return m->fs->symlink(target, linkpath);
}

int vfs_readlink(const char *path, char *buf, size_t bufsz) {
    if (!path || !buf || bufsz == 0) return -1;
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->readlink) return -1;
    return m->fs->readlink(path, buf, bufsz);
}

int vfs_chmod(const char *path, uint16_t mode) {
    if (!path) return -1;
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->chmod) return -1;
    /* only owner or root may change mode */
    vfs_stat_t st;
    vfs_cred_t cred;
    vfs_fill_cred_from_process(&cred);
    if (vfs_stat(path, &st) != 0) return -1;
    if (cred.uid != 0 && cred.uid != st.uid) return -1;
    return m->fs->chmod(path, mode & 07777u);
}

int vfs_fchmod(int fd, uint16_t mode) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    const char *path = fd_table[fd].path;
    if (!path || !path[0]) return -1;
    return vfs_chmod(path, mode);
}

int vfs_chown(const char *path, uint32_t uid, uint32_t gid) {
    if (!path) return -1;
    vfs_mount_t *m = vfs_find_mount(path);
    if (!m || !m->fs->chown) return -1;
    /* only root may change owner; owner may change group to own group */
    vfs_stat_t st;
    vfs_cred_t cred;
    vfs_fill_cred_from_process(&cred);
    if (vfs_stat(path, &st) != 0) return -1;
    if (cred.uid != 0) {
        if (uid != (uint32_t)-1 && uid != st.uid) return -1;
        if (gid != (uint32_t)-1 && !vfs_cred_in_group(&cred, gid)) return -1;
    }
    return m->fs->chown(path, uid, gid);
}

int vfs_lchown(const char *path, uint32_t uid, uint32_t gid) {
    /* Without symlink traversal, lchown and chown behave identically */
    return vfs_chown(path, uid, gid);
}

int vfs_fchown(int fd, uint32_t uid, uint32_t gid) {
    if (fd < 0 || fd >= VFS_MAX_OPEN || !fd_table[fd].used) return -1;
    const char *path = fd_table[fd].path;
    if (!path || !path[0]) return -1;
    return vfs_chown(path, uid, gid);
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

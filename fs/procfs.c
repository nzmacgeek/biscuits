#include "procfs.h"

#include "../include/types.h"
#include "../kernel/bootargs.h"
#include "../lib/string.h"

#define PROCFS_MAX_OPEN 1024
#define PROCFS_NODE_NONE 0
#define PROCFS_NODE_CMDLINE 1

typedef struct {
    int used;
    uint8_t node;
} procfs_file_t;

static procfs_file_t procfs_files[PROCFS_MAX_OPEN];

static int procfs_is_root_path(const char *path) {
    if (!path) return 0;
    return strcmp(path, "/proc") == 0 || strcmp(path, "/proc/") == 0;
}

static int procfs_is_cmdline_path(const char *path) {
    if (!path) return 0;
    return strcmp(path, "/proc/cmdline") == 0;
}

static size_t procfs_cmdline_size(void) {
    size_t len = strlen(boot_args_cmdline());

    return len > 0 ? len + 1 : 1;
}

static int procfs_mount_cb(const char *mountpoint, uint32_t start_lba) {
    (void)mountpoint;
    (void)start_lba;

    memset(procfs_files, 0, sizeof(procfs_files));
    return 0;
}

static int procfs_open_cb(const char *path, int flags) {
    if (!procfs_is_cmdline_path(path)) return -1;
    if ((flags & (VFS_O_WRONLY | VFS_O_RDWR | VFS_O_CREAT | VFS_O_TRUNC | VFS_O_APPEND)) != 0) {
        return -1;
    }

    for (int index = 0; index < PROCFS_MAX_OPEN; index++) {
        if (procfs_files[index].used) continue;
        procfs_files[index].used = 1;
        procfs_files[index].node = PROCFS_NODE_CMDLINE;
        return index;
    }

    return -1;
}

static int procfs_read_at_cb(int fd, uint8_t *buf, size_t len, uint32_t offset) {
    const char *cmdline;
    size_t cmdline_len;
    size_t total_len;
    size_t copied = 0;

    if (fd < 0 || fd >= PROCFS_MAX_OPEN || !procfs_files[fd].used) return -1;
    if (procfs_files[fd].node != PROCFS_NODE_CMDLINE) return -1;
    if (!buf) return -1;

    cmdline = boot_args_cmdline();
    cmdline_len = strlen(cmdline);
    total_len = procfs_cmdline_size();
    if (offset >= total_len) return 0;

    if (offset < cmdline_len) {
        size_t remaining = cmdline_len - offset;
        size_t chunk = len < remaining ? len : remaining;
        memcpy(buf, cmdline + offset, chunk);
        copied = chunk;
    }

    if (copied < len && offset + copied < total_len) {
        buf[copied++] = '\n';
    }

    return (int)copied;
}

static int procfs_close_cb(int fd) {
    if (fd < 0 || fd >= PROCFS_MAX_OPEN) return -1;
    procfs_files[fd].used = 0;
    procfs_files[fd].node = PROCFS_NODE_NONE;
    return 0;
}

static int procfs_readdir_cb(const char *path, vfs_dirent_t *out, int max) {
    if (!procfs_is_root_path(path) || !out || max <= 0) return -1;

    memset(&out[0], 0, sizeof(out[0]));
    strncpy(out[0].name, "cmdline", sizeof(out[0].name) - 1);
    out[0].size = (uint32_t)procfs_cmdline_size();
    out[0].inode = PROCFS_NODE_CMDLINE;
    out[0].is_dir = 0;
    return 1;
}

static int procfs_stat_cb(const char *path, vfs_stat_t *out) {
    if (!out) return -1;

    memset(out, 0, sizeof(*out));
    out->uid = 0;
    out->gid = 0;

    if (procfs_is_root_path(path)) {
        out->mode = VFS_S_IFDIR | VFS_S_IRUSR | VFS_S_IXUSR |
                    VFS_S_IRGRP | VFS_S_IXGRP |
                    VFS_S_IROTH | VFS_S_IXOTH;
        out->is_dir = 1;
        return 0;
    }

    if (procfs_is_cmdline_path(path)) {
        out->mode = VFS_S_IFREG | VFS_S_IRUSR | VFS_S_IRGRP | VFS_S_IROTH;
        out->size = (uint32_t)procfs_cmdline_size();
        out->is_dir = 0;
        return 0;
    }

    return -1;
}

static filesystem_t procfs_fs = {
    .name = "procfs",
    .mount = procfs_mount_cb,
    .open = procfs_open_cb,
    .read = NULL,
    .read_at = procfs_read_at_cb,
    .write = NULL,
    .close = procfs_close_cb,
    .readdir = procfs_readdir_cb,
    .mkdir = NULL,
    .unlink = NULL,
    .stat = procfs_stat_cb,
    .link = NULL,
    .symlink = NULL,
    .readlink = NULL,
    .chmod = NULL,
    .chown = NULL,
};

filesystem_t *procfs_get_filesystem(void) {
    return &procfs_fs;
}
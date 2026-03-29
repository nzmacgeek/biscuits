#pragma once
// BlueyOS Virtual Filesystem - "Bingo's Backpack Filesystem"
// "Everything has its place in the backpack!" - Bingo
// Episode ref: "Sleepytime" - Bingo organises her dream world perfectly
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

#define VFS_MAX_MOUNTS   8
#define VFS_MAX_OPEN     32
#define VFS_NAME_LEN     64
#define VFS_PATH_LEN     256

// Open flags (POSIX-compatible)
#define VFS_O_RDONLY  0
#define VFS_O_WRONLY  1
#define VFS_O_RDWR    2
#define VFS_O_CREAT   0x0040
#define VFS_O_TRUNC   0x0200
#define VFS_O_APPEND  0x0400

// Directory entry
typedef struct {
    char     name[VFS_NAME_LEN];
    uint32_t size;
    uint32_t inode;
    uint8_t  is_dir;
} vfs_dirent_t;

// Filesystem driver vtable - every filesystem registers one of these
typedef struct filesystem {
    char name[16];
    int (*mount)(const char *mountpoint, uint32_t start_lba);
    int (*open)(const char *path, int flags);
    int (*read)(int fd, uint8_t *buf, size_t len);
    int (*write)(int fd, const uint8_t *buf, size_t len);
    int (*close)(int fd);
    int (*readdir)(const char *path, vfs_dirent_t *out, int max);
    int (*mkdir)(const char *path);
    int (*unlink)(const char *path);
} filesystem_t;

// Mount point descriptor
typedef struct {
    char         mountpoint[VFS_PATH_LEN];
    filesystem_t *fs;
    uint32_t     start_lba;
    int          active;
} vfs_mount_t;

void vfs_init(void);
void vfs_register_fs(filesystem_t *fs);
int  vfs_mount(const char *path, const char *fs_name, uint32_t start_lba);
int  vfs_open(const char *path, int flags);
int  vfs_read(int fd, uint8_t *buf, size_t len);
int  vfs_write(int fd, const uint8_t *buf, size_t len);
int  vfs_close(int fd);
int  vfs_readdir(const char *path, vfs_dirent_t *out, int max);
int  vfs_mkdir(const char *path);
int  vfs_unlink(const char *path);
void vfs_print_mounts(void);

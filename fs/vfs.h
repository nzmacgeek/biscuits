#pragma once
// BlueyOS Virtual Filesystem - "Bingo's Backpack Filesystem"
// "Everything has its place in the backpack!" - Bingo
// Episode ref: "Sleepytime" - Bingo organises her dream world perfectly
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

#define VFS_MAX_MOUNTS   8
#define VFS_MAX_OPEN     32
#define VFS_MAX_OPEN_HARD 4096
#define VFS_NAME_LEN     64
#define VFS_PATH_LEN     256

#define VFS_ERR_NONE   0
#define VFS_ERR_EBADF  9
#define VFS_ERR_ENFILE 23
#define VFS_ERR_EMFILE 24

// Open flags (POSIX-compatible)
#define VFS_O_RDONLY  0
#define VFS_O_WRONLY  1
#define VFS_O_RDWR    2
#define VFS_O_CREAT   0x0040
#define VFS_O_TRUNC   0x0200
#define VFS_O_APPEND  0x0400

// Stat mode bits (mirrors POSIX)
#define VFS_S_IFMT   0xF000
#define VFS_S_IFREG  0x8000
#define VFS_S_IFDIR  0x4000
#define VFS_S_IFCHR  0x2000
#define VFS_S_IFBLK  0x6000
#define VFS_S_ISUID  04000
#define VFS_S_ISGID  02000
#define VFS_S_IRUSR  0400
#define VFS_S_IWUSR  0200
#define VFS_S_IXUSR  0100
#define VFS_S_IRGRP  0040
#define VFS_S_IWGRP  0020
#define VFS_S_IXGRP  0010
#define VFS_S_IROTH  0004
#define VFS_S_IWOTH  0002
#define VFS_S_IXOTH  0001

// Access mask bits (R/W/X aligned with mode bits)
#define VFS_ACCESS_READ   4
#define VFS_ACCESS_WRITE  2
#define VFS_ACCESS_EXEC   1

// Directory entry
typedef struct {
    char     name[VFS_NAME_LEN];
    uint32_t size;
    uint32_t inode;
    uint8_t  is_dir;
} vfs_dirent_t;

typedef struct {
    uint16_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t inode;
    uint8_t  is_dir;
} vfs_stat_t;

typedef struct {
    uint32_t uid;
    uint32_t gid;
    const uint32_t *groups;
    uint32_t group_count;
} vfs_cred_t;

// Filesystem driver vtable - every filesystem registers one of these
typedef struct filesystem {
    char name[16];
    int (*mount)(const char *mountpoint, uint32_t start_lba);
    int (*open)(const char *path, int flags);
    int (*read)(int fd, uint8_t *buf, size_t len);
    int (*read_at)(int fd, uint8_t *buf, size_t len, uint32_t offset);
    int (*write)(int fd, const uint8_t *buf, size_t len);
    int (*close)(int fd);
    int (*addref)(int fd);   /* increment refcount of an open backend fd (for fork/dup) */
    int (*readdir)(const char *path, vfs_dirent_t *out, int max);
    int (*mkdir)(const char *path);
    int (*unlink)(const char *path);
    int (*stat)(const char *path, vfs_stat_t *out);
    int (*link)(const char *oldpath, const char *newpath);
    int (*symlink)(const char *target, const char *linkpath);
    int (*readlink)(const char *path, char *buf, size_t bufsz);
    int (*chmod)(const char *path, uint16_t mode);
    int (*chown)(const char *path, uint32_t uid, uint32_t gid);
} filesystem_t;

// Mount point descriptor
typedef struct {
    char         mountpoint[VFS_PATH_LEN];
    filesystem_t *fs;
    uint32_t     start_lba;
    int          active;
} vfs_mount_t;

// fd type tag — stored inside every open VFS file descriptor
#define VFS_FD_TYPE_FILE  0   // regular file on a mounted filesystem
#define VFS_FD_TYPE_DEVEV 1   // device event channel (kernel ring buffer)
#define VFS_FD_TYPE_PIPE  2   // pipe (in-kernel ring buffer)
#define VFS_FD_TYPE_TTY   3   // kernel console/tty device
#define VFS_FD_TYPE_SOCKET 4  // in-kernel local socket endpoint

// Per-process file descriptor entry (one slot in a process's fd table)
typedef struct {
    int      used;
    int      fs_idx;        // which mount point (-1 for non-file types)
    int      fs_fd;         // fd returned by underlying fs, pipe index, or socket id
    char     path[VFS_PATH_LEN];
    uint8_t  fd_type;       // VFS_FD_TYPE_*
    uint8_t  pipe_is_write; // 1 = write end of a pipe, 0 = read end
    uint32_t offset;        // current seek position
} vfs_fd_t;

// lseek whence values (POSIX-compatible)
#define VFS_SEEK_SET 0
#define VFS_SEEK_CUR 1
#define VFS_SEEK_END 2

/* Forward declaration so vfs_inherit_fd_table / vfs_close_all_fds_for_process
 * can take process_t * without creating a circular header dependency. */
struct process;

void vfs_init(void);
void vfs_register_fs(filesystem_t *fs);
int  vfs_mount(const char *path, const char *fs_name, uint32_t start_lba);
int  vfs_umount(const char *path);
int  vfs_open(const char *path, int flags);
int  vfs_devev_open(void);           // open a device event channel fd
int  vfs_socket_open(int socket_id); // attach a kernel socket to a VFS fd
int  vfs_fd_is_devev(int fd);        // 1 if the fd is a device event channel
int  vfs_fd_is_tty(int fd);          // 1 if the fd is a tty/console device
int  vfs_fd_get_tty_vt(int fd);      // VT index (0-based) for a TTY fd; -1 if not TTY
int  vfs_fd_is_socket(int fd);       // 1 if the fd is a socket endpoint
int  vfs_fd_is_pipe(int fd);         // 1 if the fd is a pipe
int  vfs_pipe_readable(int fd);      // 1 if pipe has data (or EOF)
int  vfs_pipe_writable(int fd);      // 1 if pipe has write space
int  vfs_fd_is_open(int fd);         // 1 if descriptor exists and is open
int  vfs_socket_id(int fd);          // backend socket id for a socket fd
int  vfs_get_last_error(void);       // last VFS fd-allocation related errno code
int  vfs_read(int fd, uint8_t *buf, size_t len);
int  vfs_read_at(int fd, uint8_t *buf, size_t len, uint32_t offset);
int  vfs_write(int fd, const uint8_t *buf, size_t len);
int  vfs_close(int fd);
int32_t vfs_lseek(int fd, int32_t offset, int whence);
int  vfs_dup(int oldfd);
int  vfs_dup2(int oldfd, int newfd);
int  vfs_dup_above(int oldfd, int min_fd); // F_DUPFD: lowest free fd >= min_fd
int  vfs_pipe(int fds[2]);
const char *vfs_fd_get_path(int fd);  // return path stored for fd (NULL if not a file)
int  vfs_readdir(const char *path, vfs_dirent_t *out, int max);
int  vfs_mkdir(const char *path);
int  vfs_rmdir(const char *path);
int  vfs_unlink(const char *path);
int  vfs_stat(const char *path, vfs_stat_t *out);
int  vfs_fstat(int fd, vfs_stat_t *out);
int  vfs_access(const char *path, uint8_t access);
int  vfs_access_cred(const char *path, uint8_t access, const vfs_cred_t *cred);
int  vfs_link(const char *oldpath, const char *newpath);
int  vfs_symlink(const char *target, const char *linkpath);
int  vfs_readlink(const char *path, char *buf, size_t bufsz);
int  vfs_chmod(const char *path, uint16_t mode);
int  vfs_fchmod(int fd, uint16_t mode);
int  vfs_chown(const char *path, uint32_t uid, uint32_t gid);
int  vfs_lchown(const char *path, uint32_t uid, uint32_t gid);
int  vfs_fchown(int fd, uint32_t uid, uint32_t gid);
void vfs_print_mounts(void);

/* Per-process fd-table lifecycle — called from process management. */
void vfs_inherit_fd_table(struct process *parent, struct process *child);
void vfs_close_all_fds_for_process(struct process *p);

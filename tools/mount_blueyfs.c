#define FUSE_USE_VERSION 31

#include <errno.h>
#include <fcntl.h>
#include <fuse3/fuse.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define HOST_SECTOR_SIZE 512u
#define BISCUITFS_MAGIC 0xB15C0001u
#define BISCUITFS_BLOCK_SIZE 4096u
#define BISCUITFS_INODE_SIZE 256u
#define BISCUITFS_INODES_PER_GRP 8192u
#define BISCUITFS_BLOCKS_PER_GRP 8192u
#define BISCUITFS_ROOT_INO 2u
#define BISCUITFS_N_DIRECT 12u
#define BISCUITFS_IFMT 0xF000u
#define BISCUITFS_IFREG 0x8000u
#define BISCUITFS_IFDIR 0x4000u
#define BISCUITFS_IFLNK 0xA000u

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t block_count;
    uint32_t reserved_blocks;
    uint32_t free_blocks;
    uint32_t inode_count;
    uint32_t free_inodes;
    uint32_t first_data_block;
    uint32_t block_size_log;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t mtime;
    uint32_t wtime;
    uint16_t mount_count;
    uint16_t max_mount_count;
    uint16_t state;
    uint16_t errors;
    uint32_t features;
    uint32_t journal_inode;
    uint32_t first_ino;
    uint16_t inode_size;
    uint8_t uuid[16];
    uint8_t volume_name[16];
    uint8_t reserved[404];
} biscuitfs_super_t;

typedef struct __attribute__((packed)) {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks;
    uint16_t free_inodes;
    uint16_t used_dirs;
    uint16_t pad;
    uint8_t reserved[12];
} biscuitfs_bgd_t;

typedef struct __attribute__((packed)) {
    uint16_t mode;
    uint16_t uid;
    uint32_t size_lo;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint16_t gid;
    uint16_t links_count;
    uint32_t blocks_lo;
    uint32_t flags;
    uint32_t osd1;
    uint32_t block[15];
    uint32_t generation;
    uint32_t file_acl;
    uint32_t size_hi;
    uint32_t obso_faddr;
    uint8_t osd2[12];
    uint16_t extra_isize;
    uint16_t checksum_hi;
    uint32_t ctime_extra;
    uint32_t mtime_extra;
    uint32_t atime_extra;
    uint32_t crtime;
    uint32_t crtime_extra;
    uint32_t version_hi;
    uint32_t projid;
    uint8_t _pad[60];
} biscuitfs_inode_t;

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t name_len;
    uint8_t file_type;
    char name[255];
} biscuitfs_dirent_t;

typedef struct {
    int image_fd;
    uint64_t fs_offset;
    biscuitfs_super_t super;
    biscuitfs_bgd_t *bgd_table;
    uint32_t num_groups;
    char image_path[PATH_MAX];
} blueyfs_mount_t;

typedef struct {
    biscuitfs_inode_t inode;
    uint32_t inode_no;
} blueyfs_handle_t;

static blueyfs_mount_t g_mount;

static void blueyfs_destroy_handle(uint64_t handle_value) {
    blueyfs_handle_t *handle = (blueyfs_handle_t *)(uintptr_t)handle_value;
    free(handle);
}

static int blueyfs_read_exact(void *buf, size_t size, uint64_t offset) {
    uint8_t *dst = (uint8_t *)buf;
    size_t total = 0;

    while (total < size) {
        ssize_t nread = pread(g_mount.image_fd, dst + total, size - total, (off_t)(offset + total));
        if (nread < 0) {
            return -errno;
        }
        if (nread == 0) {
            return -EIO;
        }
        total += (size_t)nread;
    }

    return 0;
}

static int blueyfs_read_block(uint32_t block_no, void *buf) {
    return blueyfs_read_exact(buf, BISCUITFS_BLOCK_SIZE,
                              g_mount.fs_offset + (uint64_t)block_no * BISCUITFS_BLOCK_SIZE);
}

static int blueyfs_locate_inode(uint32_t inode_no, uint32_t *block_out, uint32_t *offset_out) {
    uint32_t inode_index;
    uint32_t group;
    uint32_t local;
    uint32_t inodes_per_block;

    if (inode_no == 0 || inode_no > g_mount.super.inode_count) {
        return -ENOENT;
    }

    inode_index = inode_no - 1u;
    group = inode_index / BISCUITFS_INODES_PER_GRP;
    local = inode_index % BISCUITFS_INODES_PER_GRP;
    if (group >= g_mount.num_groups) {
        return -ENOENT;
    }

    inodes_per_block = BISCUITFS_BLOCK_SIZE / BISCUITFS_INODE_SIZE;
    *block_out = g_mount.bgd_table[group].inode_table + (local / inodes_per_block);
    *offset_out = (local % inodes_per_block) * BISCUITFS_INODE_SIZE;
    return 0;
}

static int blueyfs_read_inode(uint32_t inode_no, biscuitfs_inode_t *inode) {
    uint32_t block_no;
    uint32_t offset;
    uint8_t block[BISCUITFS_BLOCK_SIZE];
    int rc;

    rc = blueyfs_locate_inode(inode_no, &block_no, &offset);
    if (rc != 0) {
        return rc;
    }

    rc = blueyfs_read_block(block_no, block);
    if (rc != 0) {
        return rc;
    }

    memcpy(inode, block + offset, sizeof(*inode));
    return 0;
}

static uint32_t blueyfs_inode_get_block(const biscuitfs_inode_t *inode, uint32_t block_index) {
    uint32_t ptrs_per_block = BISCUITFS_BLOCK_SIZE / sizeof(uint32_t);

    if (block_index < BISCUITFS_N_DIRECT) {
        return inode->block[block_index];
    }

    block_index -= BISCUITFS_N_DIRECT;
    if (block_index < ptrs_per_block) {
        uint8_t indirect[BISCUITFS_BLOCK_SIZE];
        if (!inode->block[12] || blueyfs_read_block(inode->block[12], indirect) != 0) {
            return 0;
        }
        return ((const uint32_t *)indirect)[block_index];
    }

    block_index -= ptrs_per_block;
    if (block_index < ptrs_per_block * ptrs_per_block) {
        uint8_t level1[BISCUITFS_BLOCK_SIZE];
        uint8_t level2[BISCUITFS_BLOCK_SIZE];
        uint32_t level1_index = block_index / ptrs_per_block;
        uint32_t level2_index = block_index % ptrs_per_block;
        uint32_t level2_block;

        if (!inode->block[13] || blueyfs_read_block(inode->block[13], level1) != 0) {
            return 0;
        }
        level2_block = ((const uint32_t *)level1)[level1_index];
        if (!level2_block || blueyfs_read_block(level2_block, level2) != 0) {
            return 0;
        }
        return ((const uint32_t *)level2)[level2_index];
    }

    return 0;
}

static int blueyfs_dir_lookup(uint32_t dir_inode_no, const char *name, uint32_t *inode_out) {
    biscuitfs_inode_t dir_inode;
    uint32_t name_len;
    uint32_t offset = 0;
    int rc;

    rc = blueyfs_read_inode(dir_inode_no, &dir_inode);
    if (rc != 0) {
        return rc;
    }
    if ((dir_inode.mode & BISCUITFS_IFMT) != BISCUITFS_IFDIR) {
        return -ENOTDIR;
    }

    name_len = (uint32_t)strlen(name);
    while (offset < dir_inode.size_lo) {
        uint32_t logical_block = offset / BISCUITFS_BLOCK_SIZE;
        uint32_t block_offset = offset % BISCUITFS_BLOCK_SIZE;
        uint32_t phys_block;
        uint8_t block[BISCUITFS_BLOCK_SIZE];
        bool advanced = false;

        phys_block = blueyfs_inode_get_block(&dir_inode, logical_block);
        if (!phys_block) {
            return -ENOENT;
        }
        rc = blueyfs_read_block(phys_block, block);
        if (rc != 0) {
            return rc;
        }

        while (block_offset + 8u <= BISCUITFS_BLOCK_SIZE && offset < dir_inode.size_lo) {
            biscuitfs_dirent_t *dirent = (biscuitfs_dirent_t *)(block + block_offset);
            if (dirent->rec_len < 8u) {
                break;
            }
            if (dirent->inode != 0 && dirent->name_len == name_len &&
                memcmp(dirent->name, name, name_len) == 0) {
                *inode_out = dirent->inode;
                return 0;
            }
            block_offset += dirent->rec_len;
            offset += dirent->rec_len;
            advanced = true;
        }

        if (!advanced) {
            offset += BISCUITFS_BLOCK_SIZE;
        }
    }

    return -ENOENT;
}

static int blueyfs_lookup_path(const char *path, uint32_t *inode_no_out, biscuitfs_inode_t *inode_out) {
    char path_copy[PATH_MAX];
    char *saveptr = NULL;
    char *component;
    uint32_t current_ino = BISCUITFS_ROOT_INO;
    biscuitfs_inode_t current_inode;
    int rc;

    if (!path || !path[0]) {
        return -ENOENT;
    }

    rc = blueyfs_read_inode(current_ino, &current_inode);
    if (rc != 0) {
        return rc;
    }

    if (strcmp(path, "/") == 0) {
        if (inode_no_out) *inode_no_out = current_ino;
        if (inode_out) *inode_out = current_inode;
        return 0;
    }

    if (strlen(path) >= sizeof(path_copy)) {
        return -ENAMETOOLONG;
    }
    strcpy(path_copy, path);

    component = strtok_r(path_copy, "/", &saveptr);
    while (component) {
        if ((current_inode.mode & BISCUITFS_IFMT) != BISCUITFS_IFDIR) {
            return -ENOTDIR;
        }
        rc = blueyfs_dir_lookup(current_ino, component, &current_ino);
        if (rc != 0) {
            return rc;
        }
        rc = blueyfs_read_inode(current_ino, &current_inode);
        if (rc != 0) {
            return rc;
        }
        component = strtok_r(NULL, "/", &saveptr);
    }

    if (inode_no_out) *inode_no_out = current_ino;
    if (inode_out) *inode_out = current_inode;
    return 0;
}

static mode_t blueyfs_host_mode(const biscuitfs_inode_t *inode) {
    mode_t mode = 0;

    switch (inode->mode & BISCUITFS_IFMT) {
        case BISCUITFS_IFDIR:
            mode |= S_IFDIR;
            break;
        case BISCUITFS_IFLNK:
            mode |= S_IFLNK;
            break;
        case BISCUITFS_IFREG:
        default:
            mode |= S_IFREG;
            break;
    }

    mode |= (inode->mode & 07777u);
    return mode;
}

static int blueyfs_fuse_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {
    biscuitfs_inode_t inode;
    int rc;

    (void)fi;
    memset(stbuf, 0, sizeof(*stbuf));

    rc = blueyfs_lookup_path(path, NULL, &inode);
    if (rc != 0) {
        return rc;
    }

    stbuf->st_mode = blueyfs_host_mode(&inode);
    stbuf->st_nlink = inode.links_count ? inode.links_count : 1;
    stbuf->st_uid = inode.uid;
    stbuf->st_gid = inode.gid;
    stbuf->st_size = ((off_t)inode.size_hi << 32) | inode.size_lo;
    stbuf->st_blksize = BISCUITFS_BLOCK_SIZE;
    stbuf->st_blocks = inode.blocks_lo;
    stbuf->st_atim.tv_sec = inode.atime;
    stbuf->st_mtim.tv_sec = inode.mtime;
    stbuf->st_ctim.tv_sec = inode.ctime;
    return 0;
}

static int blueyfs_fuse_readdir(const char *path,
                                void *buf,
                                fuse_fill_dir_t filler,
                                off_t offset,
                                struct fuse_file_info *fi,
                                enum fuse_readdir_flags flags) {
    biscuitfs_inode_t inode;
    uint32_t cursor = 0;
    int rc;

    (void)offset;
    (void)fi;
    (void)flags;

    rc = blueyfs_lookup_path(path, NULL, &inode);
    if (rc != 0) {
        return rc;
    }
    if ((inode.mode & BISCUITFS_IFMT) != BISCUITFS_IFDIR) {
        return -ENOTDIR;
    }

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    while (cursor < inode.size_lo) {
        uint32_t logical_block = cursor / BISCUITFS_BLOCK_SIZE;
        uint32_t block_offset = cursor % BISCUITFS_BLOCK_SIZE;
        uint32_t phys_block;
        uint8_t block[BISCUITFS_BLOCK_SIZE];
        bool advanced = false;

        phys_block = blueyfs_inode_get_block(&inode, logical_block);
        if (!phys_block) {
            break;
        }
        rc = blueyfs_read_block(phys_block, block);
        if (rc != 0) {
            return rc;
        }

        while (block_offset + 8u <= BISCUITFS_BLOCK_SIZE && cursor < inode.size_lo) {
            biscuitfs_dirent_t *dirent = (biscuitfs_dirent_t *)(block + block_offset);
            char name[256];

            if (dirent->rec_len < 8u) {
                break;
            }
            if (dirent->inode != 0 && dirent->name_len > 0 && dirent->name_len < sizeof(name)) {
                memcpy(name, dirent->name, dirent->name_len);
                name[dirent->name_len] = '\0';
                if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                    filler(buf, name, NULL, 0, 0);
                }
            }
            block_offset += dirent->rec_len;
            cursor += dirent->rec_len;
            advanced = true;
        }

        if (!advanced) {
            cursor += BISCUITFS_BLOCK_SIZE;
        }
    }

    return 0;
}

static int blueyfs_fuse_open(const char *path, struct fuse_file_info *fi) {
    blueyfs_handle_t *handle;
    int rc;

    if ((fi->flags & O_ACCMODE) != O_RDONLY) {
        return -EROFS;
    }

    handle = calloc(1, sizeof(*handle));
    if (!handle) {
        return -ENOMEM;
    }

    rc = blueyfs_lookup_path(path, &handle->inode_no, &handle->inode);
    if (rc != 0) {
        free(handle);
        return rc;
    }
    if ((handle->inode.mode & BISCUITFS_IFMT) != BISCUITFS_IFREG) {
        free(handle);
        return -EISDIR;
    }

    fi->fh = (uint64_t)(uintptr_t)handle;
    return 0;
}

static int blueyfs_fuse_release(const char *path, struct fuse_file_info *fi) {
    (void)path;
    blueyfs_destroy_handle(fi->fh);
    fi->fh = 0;
    return 0;
}

static int blueyfs_fuse_read(const char *path,
                             char *buf,
                             size_t size,
                             off_t offset,
                             struct fuse_file_info *fi) {
    blueyfs_handle_t *handle = (blueyfs_handle_t *)(uintptr_t)fi->fh;
    uint64_t file_size;
    size_t total = 0;

    (void)path;
    if (!handle) {
        return -EBADF;
    }

    file_size = ((uint64_t)handle->inode.size_hi << 32) | handle->inode.size_lo;
    if ((uint64_t)offset >= file_size) {
        return 0;
    }
    if ((uint64_t)offset + size > file_size) {
        size = (size_t)(file_size - (uint64_t)offset);
    }

    while (total < size) {
        uint32_t logical_block = (uint32_t)(((uint64_t)offset + total) / BISCUITFS_BLOCK_SIZE);
        uint32_t block_offset = (uint32_t)(((uint64_t)offset + total) % BISCUITFS_BLOCK_SIZE);
        uint32_t phys_block;
        uint8_t block[BISCUITFS_BLOCK_SIZE];
        size_t chunk;
        int rc;

        phys_block = blueyfs_inode_get_block(&handle->inode, logical_block);
        if (!phys_block) {
            break;
        }

        rc = blueyfs_read_block(phys_block, block);
        if (rc != 0) {
            return rc;
        }

        chunk = BISCUITFS_BLOCK_SIZE - block_offset;
        if (chunk > size - total) {
            chunk = size - total;
        }
        memcpy(buf + total, block + block_offset, chunk);
        total += chunk;
    }

    return (int)total;
}

static int blueyfs_fuse_statfs(const char *path, struct statvfs *stbuf) {
    (void)path;
    memset(stbuf, 0, sizeof(*stbuf));
    stbuf->f_bsize = BISCUITFS_BLOCK_SIZE;
    stbuf->f_frsize = BISCUITFS_BLOCK_SIZE;
    stbuf->f_blocks = g_mount.super.block_count;
    stbuf->f_bfree = g_mount.super.free_blocks;
    stbuf->f_bavail = g_mount.super.free_blocks;
    stbuf->f_files = g_mount.super.inode_count;
    stbuf->f_ffree = g_mount.super.free_inodes;
    stbuf->f_favail = g_mount.super.free_inodes;
    stbuf->f_namemax = 255;
    return 0;
}

static const struct fuse_operations blueyfs_fuse_ops = {
    .getattr = blueyfs_fuse_getattr,
    .readdir = blueyfs_fuse_readdir,
    .open = blueyfs_fuse_open,
    .read = blueyfs_fuse_read,
    .release = blueyfs_fuse_release,
    .statfs = blueyfs_fuse_statfs,
};

static void blueyfs_usage(const char *prog) {
    fprintf(stderr,
            "Usage: %s [--start-sector N] <image> <mountpoint> [FUSE options]\n"
            "\n"
            "Examples:\n"
            "  %s --start-sector 67584 build/blueyos-disk.img /mnt/blueyos -f\n"
            "  %s build/rootfs.img /mnt/blueyos -o ro\n",
            prog, prog, prog);
}

static int blueyfs_load_filesystem(const char *image_path, uint32_t start_sector) {
    uint8_t super_block[BISCUITFS_BLOCK_SIZE];
    size_t bgd_bytes;
    int rc;

    memset(&g_mount, 0, sizeof(g_mount));
    g_mount.fs_offset = (uint64_t)start_sector * HOST_SECTOR_SIZE;
    strncpy(g_mount.image_path, image_path, sizeof(g_mount.image_path) - 1);
    g_mount.image_fd = open(image_path, O_RDONLY);
    if (g_mount.image_fd < 0) {
        return -errno;
    }

    rc = blueyfs_read_exact(super_block, sizeof(super_block), g_mount.fs_offset);
    if (rc != 0) {
        close(g_mount.image_fd);
        g_mount.image_fd = -1;
        return rc;
    }

    memcpy(&g_mount.super, super_block + 1024, sizeof(g_mount.super));
    if (g_mount.super.magic != BISCUITFS_MAGIC) {
        close(g_mount.image_fd);
        g_mount.image_fd = -1;
        return -EINVAL;
    }

    g_mount.num_groups = (g_mount.super.block_count + BISCUITFS_BLOCKS_PER_GRP - 1u) / BISCUITFS_BLOCKS_PER_GRP;
    bgd_bytes = (size_t)g_mount.num_groups * sizeof(biscuitfs_bgd_t);
    g_mount.bgd_table = calloc(g_mount.num_groups, sizeof(biscuitfs_bgd_t));
    if (!g_mount.bgd_table) {
        close(g_mount.image_fd);
        g_mount.image_fd = -1;
        return -ENOMEM;
    }

    rc = blueyfs_read_exact(g_mount.bgd_table, bgd_bytes,
                            g_mount.fs_offset + BISCUITFS_BLOCK_SIZE);
    if (rc != 0) {
        free(g_mount.bgd_table);
        g_mount.bgd_table = NULL;
        close(g_mount.image_fd);
        g_mount.image_fd = -1;
        return rc;
    }

    return 0;
}

int main(int argc, char **argv) {
    const char *image_path = NULL;
    const char *mountpoint = NULL;
    uint32_t start_sector = 0;
    char start_sector_arg[64];
    char *fuse_argv[64];
    int fuse_argc = 0;
    int rc;

    if (argc < 3) {
        blueyfs_usage(argv[0]);
        return 1;
    }

    for (int index = 1; index < argc; index++) {
        if ((strcmp(argv[index], "--start-sector") == 0 || strcmp(argv[index], "-s") == 0) && index + 1 < argc) {
            start_sector = (uint32_t)strtoul(argv[++index], NULL, 10);
            continue;
        }

        if (!image_path) {
            image_path = argv[index];
            continue;
        }
        if (!mountpoint) {
            mountpoint = argv[index];
            continue;
        }
        if (fuse_argc + 1 >= (int)(sizeof(fuse_argv) / sizeof(fuse_argv[0]))) {
            fprintf(stderr, "too many FUSE arguments\n");
            return 1;
        }
        fuse_argv[fuse_argc++] = argv[index];
    }

    if (!image_path || !mountpoint) {
        blueyfs_usage(argv[0]);
        return 1;
    }

    rc = blueyfs_load_filesystem(image_path, start_sector);
    if (rc != 0) {
        fprintf(stderr, "failed to open BiscuitFS image %s: %s\n", image_path, strerror(-rc));
        return 1;
    }

    memmove(&fuse_argv[6], &fuse_argv[0], (size_t)fuse_argc * sizeof(fuse_argv[0]));
    fuse_argc += 6;
    fuse_argv[0] = argv[0];
    fuse_argv[1] = (char *)mountpoint;
    snprintf(start_sector_arg, sizeof(start_sector_arg), "fsname=blueyfs:%s@%u", image_path, start_sector);
    fuse_argv[2] = "-o";
    fuse_argv[3] = "ro";
    fuse_argv[4] = "-o";
    fuse_argv[5] = start_sector_arg;

    rc = fuse_main(fuse_argc, fuse_argv, &blueyfs_fuse_ops, NULL);

    if (g_mount.image_fd >= 0) {
        close(g_mount.image_fd);
    }
    free(g_mount.bgd_table);
    return rc;
}
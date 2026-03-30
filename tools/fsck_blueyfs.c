#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define HOST_SECTOR_SIZE       512u
#define BISCUITFS_MAGIC        0xB15C0001u
#define BISCUITFS_BLOCK_SIZE   4096u
#define BISCUITFS_INODE_SIZE   256u
#define BISCUITFS_INODES_PER_GRP 8192u
#define BISCUITFS_ROOT_INO     2u
#define BISCUITFS_IFDIR        0x4000u

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
    uint8_t  uuid[16];
    uint8_t  volume_name[16];
    uint8_t  reserved[404];
} biscuitfs_super_t;

typedef struct __attribute__((packed)) {
    uint32_t block_bitmap;
    uint32_t inode_bitmap;
    uint32_t inode_table;
    uint16_t free_blocks;
    uint16_t free_inodes;
    uint16_t used_dirs;
    uint16_t pad;
    uint8_t  reserved[12];
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
    uint8_t  osd2[12];
    uint16_t extra_isize;
    uint16_t checksum_hi;
    uint32_t ctime_extra;
    uint32_t mtime_extra;
    uint32_t atime_extra;
    uint32_t crtime;
    uint32_t crtime_extra;
    uint32_t version_hi;
    uint32_t projid;
    uint8_t  _pad[60];
} biscuitfs_inode_t;

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-o start-sector] <image>\n", prog);
}

int main(int argc, char *argv[]) {
    const char *target = NULL;
    uint32_t offset_sectors = 0;
    FILE *fp;
    uint8_t blk[BISCUITFS_BLOCK_SIZE];
    biscuitfs_super_t sb;
    biscuitfs_bgd_t bgd;
    biscuitfs_inode_t root;
    uint32_t root_blk;
    uint32_t root_off;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            offset_sectors = (uint32_t)atoi(argv[++i]);
        } else if (argv[i][0] != '-') {
            target = argv[i];
        } else {
            usage(argv[0]);
            return 1;
        }
    }

    if (!target) {
        usage(argv[0]);
        return 1;
    }

    fp = fopen(target, "rb");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    if (fseek(fp, (long)((uint64_t)offset_sectors * HOST_SECTOR_SIZE), SEEK_SET) != 0 ||
        fread(blk, sizeof(blk), 1, fp) != 1) {
        perror("read superblock");
        fclose(fp);
        return 1;
    }

    memcpy(&sb, blk + 1024, sizeof(sb));
    if (sb.magic != BISCUITFS_MAGIC) {
        fprintf(stderr, "[FSCK] Muffin found nonsense: bad magic 0x%08x\n", sb.magic);
        fclose(fp);
        return 2;
    }

    if (fseek(fp, (long)((uint64_t)offset_sectors * HOST_SECTOR_SIZE + BISCUITFS_BLOCK_SIZE), SEEK_SET) != 0 ||
        fread(blk, sizeof(blk), 1, fp) != 1) {
        perror("read bgd");
        fclose(fp);
        return 1;
    }
    memcpy(&bgd, blk, sizeof(bgd));

    root_blk = bgd.inode_table + ((BISCUITFS_ROOT_INO - 1u) / (BISCUITFS_BLOCK_SIZE / BISCUITFS_INODE_SIZE));
    root_off = ((BISCUITFS_ROOT_INO - 1u) % (BISCUITFS_BLOCK_SIZE / BISCUITFS_INODE_SIZE)) * BISCUITFS_INODE_SIZE;
    if (fseek(fp, (long)((uint64_t)offset_sectors * HOST_SECTOR_SIZE + (uint64_t)root_blk * BISCUITFS_BLOCK_SIZE), SEEK_SET) != 0 ||
        fread(blk, sizeof(blk), 1, fp) != 1) {
        perror("read root inode");
        fclose(fp);
        return 1;
    }
    memcpy(&root, blk + root_off, sizeof(root));

    if ((root.mode & BISCUITFS_IFDIR) != BISCUITFS_IFDIR) {
        fprintf(stderr, "[FSCK] Bingo says the root inode is not a directory\n");
        fclose(fp);
        return 3;
    }

    printf("[FSCK] BlueyFS looks tidy: label='%.16s' blocks=%u free=%u state=%u mount_count=%u\n",
           sb.volume_name, sb.block_count, sb.free_blocks, sb.state, sb.mount_count);
    fclose(fp);
    return 0;
}
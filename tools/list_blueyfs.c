#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define HOST_SECTOR_SIZE       512u
#define BISCUITFS_BLOCK_SIZE   4096u
#define BISCUITFS_INODE_SIZE   256u
#define BISCUITFS_ROOT_INO     2u

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} biscuitfs_dirent_t;

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
    uint8_t  _pad[60];
} biscuitfs_inode_t;

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

static FILE *fp = NULL;
static uint64_t fs_offset = 0;

static void read_block(uint32_t blk, void *buf) {
    long off = (long)(fs_offset + (uint64_t)blk * BISCUITFS_BLOCK_SIZE);
    if (fseek(fp, off, SEEK_SET) != 0) { perror("fseek"); exit(1); }
    if (fread(buf, BISCUITFS_BLOCK_SIZE, 1, fp) != 1) { memset(buf, 0, BISCUITFS_BLOCK_SIZE); }
}

static int locate_inode(uint32_t inode_no, uint32_t *blk_out, uint32_t *off_out) {
    if (inode_no == 0) return -1;
    /* Use BGD at block 1 */
    uint8_t bgdbuf[BISCUITFS_BLOCK_SIZE];
    read_block(1, bgdbuf);
    biscuitfs_bgd_t *table = (biscuitfs_bgd_t *)bgdbuf;
    uint32_t idx = inode_no - 1;
    uint32_t group = idx / 8192u; /* INODES_PER_GRP */
    uint32_t local = idx % 8192u;
    uint32_t inodes_per_block = BISCUITFS_BLOCK_SIZE / BISCUITFS_INODE_SIZE;
    uint32_t blk = table[group].inode_table + (local / inodes_per_block);
    uint32_t off = (local % inodes_per_block) * BISCUITFS_INODE_SIZE;
    *blk_out = blk;
    *off_out = off;
    return 0;
}

static int read_inode(uint32_t inode_no, biscuitfs_inode_t *inode) {
    uint32_t blk, off;
    uint8_t buf[BISCUITFS_BLOCK_SIZE];
    if (locate_inode(inode_no, &blk, &off) != 0) return -1;
    read_block(blk, buf);
    memcpy(inode, buf + off, sizeof(*inode));
    return 0;
}

static void list_dir(uint32_t ino) {
    biscuitfs_inode_t inode;
    if (read_inode(ino, &inode) != 0) {
        printf("[LIST] failed to read inode %u\n", ino);
        return;
    }

    printf("[LIST] inode %u size=%u links=%u\n", ino, inode.size_lo, inode.links_count);

    for (int i = 0; i < 15; i++) {
        uint32_t b = inode.block[i];
        if (!b) continue;
        uint8_t blk[BISCUITFS_BLOCK_SIZE];
        read_block(b, blk);
        uint32_t off = 0;
        while (off + 8 <= BISCUITFS_BLOCK_SIZE) {
            biscuitfs_dirent_t *de = (biscuitfs_dirent_t *)(blk + off);
            if (de->rec_len == 0) break;
            if (de->inode) {
                char name[256];
                int n = de->name_len < 255 ? de->name_len : 255;
                memcpy(name, de->name, n);
                name[n] = '\0';
                printf("  %s (ino=%u type=%u)\n", name, de->inode, de->file_type);
            }
            off += de->rec_len;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <image> [-o start-sector]\n", argv[0]);
        return 1;
    }

    const char *img = NULL;
    uint32_t offset_sectors = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            offset_sectors = (uint32_t)atoi(argv[++i]);
        } else if (!img) {
            img = argv[i];
        }
    }

    if (!img) { fprintf(stderr, "No image\n"); return 1; }

    fp = fopen(img, "rb");
    if (!fp) { perror("fopen"); return 1; }

    fs_offset = (uint64_t)offset_sectors * HOST_SECTOR_SIZE;

    printf("[LIST] Opening image %s at sector offset %u\n", img, offset_sectors);

    printf("[LIST] Root (/):\n");
    list_dir(BISCUITFS_ROOT_INO);

    /* Find /bin entry */
    biscuitfs_inode_t root;
    if (read_inode(BISCUITFS_ROOT_INO, &root) != 0) return 1;
    uint32_t bin_ino = 0;
    for (int i = 0; i < 15; i++) {
        uint32_t b = root.block[i];
        if (!b) continue;
        uint8_t blk[BISCUITFS_BLOCK_SIZE];
        read_block(b, blk);
        uint32_t off = 0;
        while (off + 8 <= BISCUITFS_BLOCK_SIZE) {
            biscuitfs_dirent_t *de = (biscuitfs_dirent_t *)(blk + off);
            if (de->rec_len == 0) break;
            if (de->inode && de->name_len == 3 && memcmp(de->name, "bin", 3) == 0) {
                bin_ino = de->inode;
                break;
            }
            off += de->rec_len;
        }
        if (bin_ino) break;
    }

    if (bin_ino) {
        printf("[LIST] /bin inode=%u contents:\n", bin_ino);
        list_dir(bin_ino);
    } else {
        printf("[LIST] /bin not found in root directory\n");
    }

    fclose(fp);
    return 0;
}

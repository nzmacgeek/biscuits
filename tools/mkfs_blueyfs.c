/*
 * mkfs.biscuitfs - Format a disk image or device with BiscuitFS
 * (BlueyOS ext4-inspired filesystem)
 *
 * Episode ref: "Camping" - "Let's set up camp properly!"
 *
 * ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
 *
 * This is a HOST-SIDE tool compiled with the standard C library.
 * It does NOT use the freestanding kernel build flags.
 *
 * Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
 * licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
 *
 * Usage:
 *   mkfs.biscuitfs [options] <device-or-image>
 *
 * Options:
 *   -L <label>        volume label (up to 16 chars)
 *   -b <block-size>   block size in bytes (must be 4096; default: 4096)
 *   -s <size-MB>      image size in MB for new image files (default: 64)
 *   -j <journal-blks> journal size in blocks (default: 2048 = 8 MB)
 *   -F                force overwrite without prompting
 *   -q                quiet: suppress informational messages
 *
 * Example:
 *   # Create a 128 MB image
 *   mkfs.biscuitfs -L "BlueyRoot" -s 128 disk.img
 *
 *   # Boot with QEMU:
 *   qemu-system-i386 -cdrom blueyos.iso -hda disk.img -m 128
 *
 *   # To use as swap:
 *   mkswap.biscuitfs swap.img   (a future companion tool)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <time.h>

/* -------------------------------------------------------------------------
 * On-disk structures (kept in sync with fs/blueyfs.h)
 * -------------------------------------------------------------------------*/

#define BISCUITFS_MAGIC           0xB15C0001U
#define BISCUITFS_VERSION         1
#define BISCUITFS_BLOCK_SIZE      4096
#define BISCUITFS_INODE_SIZE      256
#define BISCUITFS_INODES_PER_GRP  8192
#define BISCUITFS_BLOCKS_PER_GRP  8192
#define BISCUITFS_FIRST_INO       11
#define BISCUITFS_ROOT_INO        2
#define BISCUITFS_JOURNAL_BLOCKS  2048

#define BISCUITFS_FEAT_JOURNAL    0x0001
#define BISCUITFS_FEAT_EXTENTS    0x0002
#define BISCUITFS_FEAT_ACL        0x0004
#define BISCUITFS_FEAT_XATTR      0x0008

#define BISCUITFS_STATE_CLEAN     1
#define BISCUITFS_STATE_ERROR     2

#define BISCUITFS_IFDIR           0x4000
#define BISCUITFS_IFREG           0x8000
#define BISCUITFS_FT_DIR          2
#define BISCUITFS_FT_REG_FILE     1
#define BISCUITFS_DIRENT_MIN      8
#define BISCUITFS_JNL_MAGIC       0xB15CDA7AU

/* Bandit's Birthday Epoch: Oct 15 1980 00:00 AEST = Oct 14 1980 14:00 UTC */
#define BANDIT_BIRTHDAY_UNIX      340405200UL

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

typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[255];
} biscuitfs_dirent_t;

typedef struct __attribute__((packed)) {
    uint32_t h_magic;
    uint32_t h_blocktype;
    uint32_t h_sequence;
} biscuitfs_jnl_block_hdr_t;

/* -------------------------------------------------------------------------
 * Globals
 * -------------------------------------------------------------------------*/

static FILE    *g_fp          = NULL;
static uint32_t g_block_size  = BISCUITFS_BLOCK_SIZE;
static uint32_t g_blocks_total= 0;
static int      g_quiet       = 0;

#define MSG(...) do { if (!g_quiet) printf(__VA_ARGS__); } while(0)

/* -------------------------------------------------------------------------
 * Block I/O helpers
 * -------------------------------------------------------------------------*/

static void write_block(uint32_t blk, const void *data) {
    long off = (long)blk * (long)g_block_size;
    if (fseek(g_fp, off, SEEK_SET) != 0) {
        perror("fseek"); exit(1);
    }
    if (fwrite(data, g_block_size, 1, g_fp) != 1) {
        perror("fwrite"); exit(1);
    }
}

static void read_block(uint32_t blk, void *data) {
    long off = (long)blk * (long)g_block_size;
    if (fseek(g_fp, off, SEEK_SET) != 0) {
        perror("fseek"); exit(1);
    }
    if (fread(data, g_block_size, 1, g_fp) != 1) {
        memset(data, 0, g_block_size);
    }
}

static void zero_block(uint32_t blk) {
    uint8_t buf[BISCUITFS_BLOCK_SIZE];
    memset(buf, 0, sizeof(buf));
    write_block(blk, buf);
}

/* -------------------------------------------------------------------------
 * UUID generator (simple LCG for a research project)
 * -------------------------------------------------------------------------*/
static void gen_uuid(uint8_t *uuid) {
    uint32_t seed = (uint32_t)time(NULL);
    for (int i = 0; i < 16; i++) {
        seed = seed * 1664525 + 1013904223;
        uuid[i] = (uint8_t)(seed >> 24);
    }
    /* Set version 4, variant bits */
    uuid[6] = (uint8_t)((uuid[6] & 0x0F) | 0x40);
    uuid[8] = (uint8_t)((uuid[8] & 0x3F) | 0x80);
}

/* -------------------------------------------------------------------------
 * mkfs logic
 * -------------------------------------------------------------------------*/

static void do_mkfs(const char *label, uint32_t journal_blocks) {
    uint32_t now = (uint32_t)(time(NULL) - (time_t)BANDIT_BIRTHDAY_UNIX);

    uint32_t num_groups = (g_blocks_total + BISCUITFS_BLOCKS_PER_GRP - 1)
                          / BISCUITFS_BLOCKS_PER_GRP;

    /* Per-group overhead (block bitmap + inode bitmap + inode table) */
    uint32_t inodes_per_block  = g_block_size / BISCUITFS_INODE_SIZE;
    uint32_t inode_table_blks  = (BISCUITFS_INODES_PER_GRP + inodes_per_block - 1)
                                  / inodes_per_block;
    uint32_t overhead_per_group = 1 + 1 + inode_table_blks + 1; /* +1 BGD block */

    uint32_t total_inodes       = num_groups * BISCUITFS_INODES_PER_GRP;
    uint32_t total_overhead     = num_groups * overhead_per_group
                                  + 1                   /* superblock */
                                  + journal_blocks;

    uint32_t free_blocks        = g_blocks_total - total_overhead;
    uint32_t reserved_blocks    = free_blocks / 20;    /* 5% reserved for root */
    free_blocks                -= reserved_blocks;

    MSG("BiscuitFS mkfs\n");
    MSG("  Volume label : %s\n", label[0] ? label : "(none)");
    MSG("  Block size   : %d bytes\n", g_block_size);
    MSG("  Total blocks : %d (%d MB)\n", g_blocks_total,
            g_blocks_total * g_block_size / 1024 / 1024);
    MSG("  Block groups : %d\n", num_groups);
    MSG("  Inodes       : %d\n", total_inodes);
    MSG("  Journal      : %d blocks (%d KB)\n",
            journal_blocks, journal_blocks * g_block_size / 1024);
    MSG("  Features     : journal, acl, xattr\n\n");

    /* ---- Zero the image ---- */
    MSG("Zeroing image...\n");
    for (uint32_t b = 0; b < g_blocks_total; b++) zero_block(b);

    /* ---- Block 0: reserved (boot code area) ---- */
    MSG("Writing boot block...\n");
    {
        uint8_t boot[BISCUITFS_BLOCK_SIZE];
        memset(boot, 0, sizeof(boot));
        /* Write a recognisable "not bootable" stub at offset 0 */
        const char *msg = "BlueyOS BiscuitFS - not directly bootable\r\n";
        memcpy(boot, msg, strlen(msg));
        /* MBR boot signature */
        boot[510] = 0x55;
        boot[511] = 0xAA;
        write_block(0, boot);
    }

    /* ---- Superblock (at byte offset 1024 = block 0, offset 1024) ---- */
    MSG("Writing superblock...\n");
    {
        biscuitfs_super_t sb;
        memset(&sb, 0, sizeof(sb));

        sb.magic            = BISCUITFS_MAGIC;
        sb.version          = BISCUITFS_VERSION;
        sb.block_count      = g_blocks_total;
        sb.reserved_blocks  = reserved_blocks;
        sb.free_blocks      = free_blocks;
        sb.inode_count      = total_inodes;
        sb.free_inodes      = total_inodes - BISCUITFS_FIRST_INO;
        sb.first_data_block = 0;
        sb.block_size_log   = 2;    /* log2(4096) - 10 = 2 */
        sb.blocks_per_group = BISCUITFS_BLOCKS_PER_GRP;
        sb.inodes_per_group = BISCUITFS_INODES_PER_GRP;
        sb.mtime            = now;
        sb.wtime            = now;
        sb.mount_count      = 0;
        sb.max_mount_count  = (uint16_t)-1;
        sb.state            = BISCUITFS_STATE_CLEAN;
        sb.errors           = 1;    /* continue on error */
        sb.features         = BISCUITFS_FEAT_JOURNAL
                            | BISCUITFS_FEAT_ACL
                            | BISCUITFS_FEAT_XATTR;
        sb.journal_inode    = 8;
        sb.first_ino        = BISCUITFS_FIRST_INO;
        sb.inode_size       = BISCUITFS_INODE_SIZE;
        gen_uuid(sb.uuid);
        strncpy((char *)sb.volume_name, label, sizeof(sb.volume_name) - 1);
        sb.volume_name[sizeof(sb.volume_name) - 1] = '\0';

        /* Write the superblock into block 0 at byte offset 1024 */
        uint8_t blk0[BISCUITFS_BLOCK_SIZE];
        read_block(0, blk0);
        memcpy(blk0 + 1024, &sb, sizeof(sb));
        write_block(0, blk0);
    }

    /* ---- Block 1: Block Group Descriptor Table ---- */
    MSG("Writing block group descriptors...\n");
    {
        uint8_t bgd_blk[BISCUITFS_BLOCK_SIZE];
        memset(bgd_blk, 0, sizeof(bgd_blk));

        biscuitfs_bgd_t *table = (biscuitfs_bgd_t *)bgd_blk;

        uint32_t cur_block = 2;   /* start allocating after superblock+BGD */

        for (uint32_t g = 0; g < num_groups; g++) {
            uint32_t group_start = g * BISCUITFS_BLOCKS_PER_GRP;

            table[g].block_bitmap = (g == 0) ? cur_block++ : group_start + 2;
            table[g].inode_bitmap = (g == 0) ? cur_block++ : group_start + 3;
            table[g].inode_table  = (g == 0) ? cur_block   : group_start + 4;
            if (g == 0) cur_block += inode_table_blks;

            uint32_t avail = BISCUITFS_BLOCKS_PER_GRP - overhead_per_group;
            if (g + 1 == num_groups) {
                avail = g_blocks_total - g * BISCUITFS_BLOCKS_PER_GRP
                      - overhead_per_group;
            }
            table[g].free_blocks  = (uint16_t)avail;
            table[g].free_inodes  = BISCUITFS_INODES_PER_GRP;
            table[g].used_dirs    = 0;
        }

        write_block(1, bgd_blk);

        /* ---- Write bitmaps for each group ---- */
        for (uint32_t g = 0; g < num_groups; g++) {
            /* Block bitmap: mark the overhead blocks as used */
            uint8_t bbm[BISCUITFS_BLOCK_SIZE];
            memset(bbm, 0, sizeof(bbm));

            /* Mark bits for superblock (block 0), BGD (block 1) in group 0 */
            if (g == 0) {
                for (uint32_t b = 0; b < overhead_per_group + 2 + journal_blocks; b++) {
                    if (b < BISCUITFS_BLOCKS_PER_GRP)
                        bbm[b / 8] |= (1U << (b % 8));
                }
            } else {
                /* Mark just the local overhead */
                for (uint32_t b = 0; b < overhead_per_group; b++)
                    bbm[b / 8] |= (1U << (b % 8));
            }
            write_block(table[g].block_bitmap, bbm);

            /* Inode bitmap: mark reserved inodes 0-10 as used */
            uint8_t ibm[BISCUITFS_BLOCK_SIZE];
            memset(ibm, 0, sizeof(ibm));
            if (g == 0) {
                for (uint32_t i = 0; i < BISCUITFS_FIRST_INO; i++)
                    ibm[i / 8] |= (1U << (i % 8));
            }
            write_block(table[g].inode_bitmap, ibm);
        }

        /* Update BGD with correct positions now that we know them */
        read_block(1, bgd_blk);  /* Re-read in case we overwrote above */
        /* (for simplicity the positions are already correct) */
    }

    /* ---- Root directory inode (inode #2) ---- */
    MSG("Creating root directory...\n");
    {
        biscuitfs_bgd_t bgd;
        uint8_t bgd_blk[BISCUITFS_BLOCK_SIZE];
        read_block(1, bgd_blk);
        memcpy(&bgd, bgd_blk, sizeof(bgd));

        uint32_t ino_idx     = BISCUITFS_ROOT_INO - 1;   /* 0-based */
        uint32_t ino_per_blk = g_block_size / BISCUITFS_INODE_SIZE;
        uint32_t ino_blk     = bgd.inode_table + ino_idx / ino_per_blk;
        uint32_t ino_off     = (ino_idx % ino_per_blk) * BISCUITFS_INODE_SIZE;

        uint8_t iblk[BISCUITFS_BLOCK_SIZE];
        read_block(ino_blk, iblk);

        biscuitfs_inode_t *root = (biscuitfs_inode_t *)(iblk + ino_off);
        memset(root, 0, sizeof(*root));
        root->mode        = BISCUITFS_IFDIR | 0755;
        root->uid         = 0;
        root->gid         = 0;
        root->links_count = 2;   /* "." + parent reference */
        root->atime       = now;
        root->ctime       = now;
        root->mtime       = now;
        root->crtime      = now;

        /* Allocate first data block for the root directory */
        /* First free block after overhead in group 0 */
        uint32_t first_free = 2 + 1 + 1 + inode_table_blks + journal_blocks;
        root->block[0]   = first_free;
        root->size_lo    = g_block_size;
        root->blocks_lo  = g_block_size / 512;

        write_block(ino_blk, iblk);

        /* Mark the data block as used in the block bitmap */
        uint8_t bbm[BISCUITFS_BLOCK_SIZE];
        read_block(bgd.block_bitmap, bbm);
        bbm[first_free / 8] |= (1U << (first_free % 8));
        write_block(bgd.block_bitmap, bbm);

        /* Write root directory data block */
        uint8_t dir_blk[BISCUITFS_BLOCK_SIZE];
        memset(dir_blk, 0, sizeof(dir_blk));

        /* "." entry */
        biscuitfs_dirent_t *dot = (biscuitfs_dirent_t *)dir_blk;
        dot->inode     = BISCUITFS_ROOT_INO;
        dot->rec_len   = 12;
        dot->name_len  = 1;
        dot->file_type = BISCUITFS_FT_DIR;
        dot->name[0]   = '.';

        /* ".." entry (also root for the root directory) */
        biscuitfs_dirent_t *dotdot = (biscuitfs_dirent_t *)(dir_blk + 12);
        dotdot->inode     = BISCUITFS_ROOT_INO;
        dotdot->rec_len   = (uint16_t)(g_block_size - 12);
        dotdot->name_len  = 2;
        dotdot->file_type = BISCUITFS_FT_DIR;
        dotdot->name[0]   = '.';
        dotdot->name[1]   = '.';

        write_block(first_free, dir_blk);

        /* Update BGD free_blocks count */
        biscuitfs_bgd_t *tbl = (biscuitfs_bgd_t *)bgd_blk;
        tbl[0].free_blocks--;
        tbl[0].free_inodes = (uint16_t)(tbl[0].free_inodes
                             - (BISCUITFS_FIRST_INO - 1));
        tbl[0].used_dirs   = 1;
        write_block(1, bgd_blk);
    }

    /* ---- Journal superblock (first journal block) ---- */
    MSG("Writing journal...\n");
    {
        /* Journal starts after: block0 + BGD + block_bitmap + inode_bitmap
         *                       + inode_table blocks */
        uint32_t jnl_start = 2 + 1 + 1 + (BISCUITFS_INODES_PER_GRP
                             * BISCUITFS_INODE_SIZE / BISCUITFS_BLOCK_SIZE);

        uint8_t jblk[BISCUITFS_BLOCK_SIZE];
        memset(jblk, 0, sizeof(jblk));

        biscuitfs_jnl_block_hdr_t *jhdr = (biscuitfs_jnl_block_hdr_t *)jblk;
        jhdr->h_magic     = BISCUITFS_JNL_MAGIC;
        jhdr->h_blocktype = 3;    /* superblock type for journal */
        jhdr->h_sequence  = 0;

        write_block(jnl_start, jblk);
    }

    MSG("\nBiscuitFS created successfully!\n");
    MSG("  Magic  : 0x%08X\n", BISCUITFS_MAGIC);
    MSG("\nTo boot with QEMU:\n");
    MSG("  qemu-system-i386 -cdrom blueyos.iso -hda <image> -m 128\n");
    MSG("\n\"This is the BEST filesystem EVER!\" - Bluey Heeler\n");
}

/* -------------------------------------------------------------------------
 * main
 * -------------------------------------------------------------------------*/

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options] <device-or-image>\n"
        "\n"
        "Options:\n"
        "  -L <label>    volume label (max 16 chars)\n"
        "  -s <size-MB>  image size in MB for new image files (default: 64)\n"
        "  -j <blocks>   journal size in blocks (default: 2048 = 8 MB)\n"
        "  -F            force: do not prompt\n"
        "  -q            quiet output\n"
        "  -h            show this help\n"
        "\n"
        "Examples:\n"
        "  %s -L BlueyRoot -s 128 disk.img\n"
        "  %s -F /dev/sdb\n"
        "\n"
        "Boot with QEMU:\n"
        "  qemu-system-i386 -cdrom blueyos.iso -hda disk.img -m 128\n",
        prog, prog, prog);
}

int main(int argc, char *argv[]) {
    char     label[17]      = "";
    uint32_t size_mb        = 64;
    uint32_t journal_blocks = BISCUITFS_JOURNAL_BLOCKS;
    int      force          = 0;
    const char *target      = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-L") == 0 && i + 1 < argc) {
            strncpy(label, argv[++i], 16);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            size_mb = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-j") == 0 && i + 1 < argc) {
            journal_blocks = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "-F") == 0) {
            force = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            g_quiet = 1;
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage(argv[0]);
            return 0;
        } else if (argv[i][0] != '-') {
            target = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    if (!target) {
        fprintf(stderr, "Error: no target device or image specified\n\n");
        usage(argv[0]);
        return 1;
    }

    if (size_mb == 0 || size_mb > 65536) {
        fprintf(stderr, "Error: size must be between 1 and 65536 MB\n");
        return 1;
    }

    if (!force) {
        printf("This will overwrite '%s' with BiscuitFS.\n", target);
        printf("Are you sure? (type 'yes' to continue): ");
        fflush(stdout);
        char ans[32];
        if (!fgets(ans, sizeof(ans), stdin) || strncmp(ans, "yes", 3) != 0) {
            printf("Aborted.\n");
            return 0;
        }
    }

    /* Open or create the file */
    g_fp = fopen(target, "r+b");
    if (!g_fp) {
        /* Try creating it */
        g_fp = fopen(target, "w+b");
        if (!g_fp) {
            fprintf(stderr, "Cannot open '%s': %s\n", target, strerror(errno));
            return 1;
        }

        /* Size it to size_mb MB */
        uint64_t total_bytes = (uint64_t)size_mb * 1024 * 1024;
        if (fseek(g_fp, (long)(total_bytes - 1), SEEK_SET) != 0 ||
            fputc(0, g_fp) == EOF) {
            fprintf(stderr, "Cannot create image of size %u MB\n", size_mb);
            fclose(g_fp);
            return 1;
        }
        rewind(g_fp);
        MSG("Created image file: %s (%u MB)\n\n", target, size_mb);
    }

    /* Determine total blocks */
    if (fseek(g_fp, 0, SEEK_END) != 0) {
        perror("fseek"); fclose(g_fp); return 1;
    }
    long file_size = ftell(g_fp);
    rewind(g_fp);

    g_blocks_total = (uint32_t)(file_size / BISCUITFS_BLOCK_SIZE);
    if (g_blocks_total < 64) {
        fprintf(stderr, "Error: image too small (need at least 64 blocks = 256 KB)\n");
        fclose(g_fp);
        return 1;
    }

    if (journal_blocks >= g_blocks_total / 2) {
        journal_blocks = g_blocks_total / 4;
        fprintf(stderr, "Warning: journal size reduced to %u blocks\n",
                journal_blocks);
    }

    do_mkfs(label, journal_blocks);

    fclose(g_fp);
    return 0;
}

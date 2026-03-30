// BlueyOS "BiscuitFS" - ext4-inspired filesystem implementation
// "Everything in its place!" - Bingo Heeler
// Episode ref: "Sleepytime" - an organised dream world with structure
// Episode ref: "Camping" - every campsite needs a proper layout
//
// On-disk layout (4096-byte blocks):
//   Block 0      : Reserved (boot/MBR code)
//   Bytes 1024-  : Superblock (512 bytes, within block 0 for 4K-block fs)
//   Block 1      : Block group descriptor table
//   Block 2+     : Block groups (bitmap, inode bitmap, inode table, data)
//
// Journal:  fixed-size region of BISCUITFS_JOURNAL_BLOCKS blocks at the
//           end of block group 0, using a simple write-ahead log.
//
// ACL:      stored as POSIX extended attributes in the inode's EA block
//           (inode.file_acl points to a block containing xattr data).
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/ata.h"
#include "../kernel/kheap.h"
#include "blueyfs.h"
#include "vfs.h"

// ---------------------------------------------------------------------------
// Runtime state
// ---------------------------------------------------------------------------
static biscuitfs_super_t sb;
static uint32_t          fs_start_lba;
static uint32_t          fs_block_size   = BISCUITFS_BLOCK_SIZE;
static uint32_t          fs_sectors_per_block;  /* = block_size / 512 */
static int               fs_mounted      = 0;
static uint32_t          fs_num_groups;

// One cached block group descriptor (enough for a simple single-group FS)
#define MAX_GROUPS  16
static biscuitfs_bgd_t   bgd_table[MAX_GROUPS];

// Journal state
#define JOURNAL_BUF_BLOCKS  16
typedef struct {
    uint32_t block_no;
    uint8_t  data[BISCUITFS_BLOCK_SIZE];
} jnl_buf_entry_t;

static jnl_buf_entry_t jnl_buf[JOURNAL_BUF_BLOCKS];
static int             jnl_count     = 0;
static uint32_t        jnl_seq       = 1;
static uint32_t        jnl_start_blk = 0;  /* first block of journal region */

// Open file table
#define BISCUITFS_MAX_OPEN  16
typedef struct {
    int      used;
    uint32_t inode_no;
    uint32_t offset;
    uint32_t size;
    uint32_t flags;
} biscuitfs_fd_t;

static biscuitfs_fd_t fd_table[BISCUITFS_MAX_OPEN];

// ---------------------------------------------------------------------------
// Block I/O helpers
// ---------------------------------------------------------------------------

// Read a filesystem block into buf (buf must be fs_block_size bytes)
static int read_block(uint32_t blk, uint8_t *buf) {
    uint32_t lba = fs_start_lba + blk * fs_sectors_per_block;
    for (uint32_t s = 0; s < fs_sectors_per_block; s++) {
        if (ata_read_sector(lba + s, buf + s * 512) != 0) return -1;
    }
    return 0;
}

// Write a filesystem block from buf
static int write_block(uint32_t blk, const uint8_t *buf) {
    uint32_t lba = fs_start_lba + blk * fs_sectors_per_block;
    for (uint32_t s = 0; s < fs_sectors_per_block; s++) {
        if (ata_write_sector(lba + s, buf + s * 512) != 0) return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Journal implementation (minimal write-ahead log)
// ---------------------------------------------------------------------------

void biscuitfs_journal_begin(void) {
    jnl_count = 0;
}

void biscuitfs_journal_write_block(uint32_t block_no, const uint8_t *data) {
    if (!fs_mounted || jnl_count >= JOURNAL_BUF_BLOCKS) return;
    jnl_buf[jnl_count].block_no = block_no;
    memcpy(jnl_buf[jnl_count].data, data, BISCUITFS_BLOCK_SIZE);
    jnl_count++;
}

void biscuitfs_journal_commit(void) {
    if (!fs_mounted || jnl_count == 0) return;

    // Write descriptor block to journal
    uint8_t desc_blk[BISCUITFS_BLOCK_SIZE];
    memset(desc_blk, 0, sizeof(desc_blk));

    biscuitfs_jnl_block_hdr_t *hdr = (biscuitfs_jnl_block_hdr_t *)desc_blk;
    hdr->h_magic     = BISCUITFS_JNL_MAGIC;
    hdr->h_blocktype = BISCUITFS_JNL_DESCRIPTOR;
    hdr->h_sequence  = jnl_seq;

    // Write tags after the header
    biscuitfs_jnl_block_tag_t *tags = (biscuitfs_jnl_block_tag_t *)
                                      (desc_blk + sizeof(biscuitfs_jnl_block_hdr_t));
    for (int i = 0; i < jnl_count; i++) {
        tags[i].t_blocknr = jnl_buf[i].block_no;
        tags[i].t_flags   = (i == jnl_count - 1) ? BISCUITFS_JNL_FLAG_LAST : 0;
    }

    uint32_t jblk = jnl_start_blk;
    write_block(jblk++, desc_blk);

    // Write each journalled block
    for (int i = 0; i < jnl_count; i++) {
        write_block(jblk++, jnl_buf[i].data);
    }

    // Write commit block
    uint8_t commit_blk[BISCUITFS_BLOCK_SIZE];
    memset(commit_blk, 0, sizeof(commit_blk));
    biscuitfs_jnl_block_hdr_t *chdr = (biscuitfs_jnl_block_hdr_t *)commit_blk;
    chdr->h_magic     = BISCUITFS_JNL_MAGIC;
    chdr->h_blocktype = BISCUITFS_JNL_COMMIT;
    chdr->h_sequence  = jnl_seq;
    write_block(jblk, commit_blk);

    // Now apply the changes to the real filesystem blocks
    for (int i = 0; i < jnl_count; i++) {
        write_block(jnl_buf[i].block_no, jnl_buf[i].data);
    }

    jnl_seq++;
    jnl_count = 0;
}

void biscuitfs_journal_abort(void) {
    jnl_count = 0;
}

void biscuitfs_journal_replay(void) {
    if (!jnl_start_blk) return;

    uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];
    if (read_block(jnl_start_blk, blkbuf) != 0) return;

    biscuitfs_jnl_block_hdr_t *hdr = (biscuitfs_jnl_block_hdr_t *)blkbuf;
    if (hdr->h_magic != BISCUITFS_JNL_MAGIC) return;
    if (hdr->h_blocktype != BISCUITFS_JNL_DESCRIPTOR) return;

    kprintf("[BISCUITFS] Replaying journal (seq %d)...\n", hdr->h_sequence);

    // Read tags to get the list of blocks to replay
    biscuitfs_jnl_block_tag_t *tags = (biscuitfs_jnl_block_tag_t *)
                                      (blkbuf + sizeof(biscuitfs_jnl_block_hdr_t));
    uint32_t data_blk = jnl_start_blk + 1;

    for (int i = 0; ; i++) {
        uint32_t flags = tags[i].t_flags;
        uint8_t  dbuf[BISCUITFS_BLOCK_SIZE];

        if (read_block(data_blk, dbuf)        == 0 &&
            write_block(tags[i].t_blocknr, dbuf) == 0) {
            kprintf("[BISCUITFS] Replayed block %d\n", tags[i].t_blocknr);
        }
        data_blk++;
        if (flags & BISCUITFS_JNL_FLAG_LAST) break;
    }
}

// ---------------------------------------------------------------------------
// Inode helpers
// ---------------------------------------------------------------------------

// Read the group descriptor for a given block group
static void read_bgd(uint32_t group) {
    if (group >= MAX_GROUPS) return;
    uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];
    uint32_t bgd_block = 1;  /* BGD table starts at block 1 */

    if (read_block(bgd_block, blkbuf) != 0) return;

    // Each BGD is 32 bytes; pack them into one block (up to 128 groups per block)
    biscuitfs_bgd_t *table = (biscuitfs_bgd_t *)blkbuf;
    memcpy(&bgd_table[group], &table[group], sizeof(biscuitfs_bgd_t));
}

// Write the group descriptor for a group back to disk (journalled)
static void write_bgd(uint32_t group) {
    if (group >= MAX_GROUPS) return;
    uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];
    if (read_block(1, blkbuf) != 0) return;
    biscuitfs_bgd_t *table = (biscuitfs_bgd_t *)blkbuf;
    memcpy(&table[group], &bgd_table[group], sizeof(biscuitfs_bgd_t));
    biscuitfs_journal_write_block(1, blkbuf);
}

// Return the block number of the inode table block containing inode_no
// and the offset within that block.
static int locate_inode(uint32_t inode_no, uint32_t *blk_out, uint32_t *off_out) {
    if (inode_no == 0) return -1;
    uint32_t idx     = inode_no - 1;   /* inodes are 1-based */
    uint32_t group   = idx / BISCUITFS_INODES_PER_GRP;
    uint32_t local   = idx % BISCUITFS_INODES_PER_GRP;

    if (group >= fs_num_groups) return -1;

    uint32_t inodes_per_block = fs_block_size / BISCUITFS_INODE_SIZE;
    uint32_t blk_in_table     = local / inodes_per_block;
    uint32_t offset_in_blk    = (local % inodes_per_block) * BISCUITFS_INODE_SIZE;

    *blk_out = bgd_table[group].inode_table + blk_in_table;
    *off_out = offset_in_blk;
    return 0;
}

// Read an inode from disk into *out
static int read_inode(uint32_t inode_no, biscuitfs_inode_t *out) {
    uint32_t blk, off;
    if (locate_inode(inode_no, &blk, &off) != 0) return -1;

    uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];
    if (read_block(blk, blkbuf) != 0) return -1;

    memcpy(out, blkbuf + off, sizeof(biscuitfs_inode_t));
    return 0;
}

// Write an inode back to disk (journalled)
static int write_inode(uint32_t inode_no, const biscuitfs_inode_t *in) {
    uint32_t blk, off;
    if (locate_inode(inode_no, &blk, &off) != 0) return -1;

    uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];
    if (read_block(blk, blkbuf) != 0) return -1;

    memcpy(blkbuf + off, in, sizeof(biscuitfs_inode_t));
    biscuitfs_journal_write_block(blk, blkbuf);
    return 0;
}

// ---------------------------------------------------------------------------
// Block allocation / deallocation
// ---------------------------------------------------------------------------

// Allocate a free data block in the given group; returns block number or 0.
static uint32_t alloc_block_in_group(uint32_t group) {
    if (bgd_table[group].free_blocks == 0) return 0;

    uint8_t bitmap[BISCUITFS_BLOCK_SIZE];
    if (read_block(bgd_table[group].block_bitmap, bitmap) != 0) return 0;

    uint32_t blocks = BISCUITFS_BLOCKS_PER_GRP;
    for (uint32_t w = 0; w < blocks / 8; w++) {
        if (bitmap[w] == 0xFF) continue;
        for (int b = 0; b < 8; b++) {
            if (!(bitmap[w] & (1U << b))) {
                bitmap[w] |= (1U << b);
                uint32_t blk = group * BISCUITFS_BLOCKS_PER_GRP + w * 8 + (uint32_t)b;

                // Update bitmap on disk (journalled)
                biscuitfs_journal_write_block(bgd_table[group].block_bitmap, bitmap);

                bgd_table[group].free_blocks--;
                sb.free_blocks--;
                write_bgd(group);
                return blk;
            }
        }
    }
    return 0;
}

static uint32_t alloc_block(void) {
    for (uint32_t g = 0; g < fs_num_groups; g++) {
        uint32_t blk = alloc_block_in_group(g);
        if (blk) return blk;
    }
    return 0;   /* no free blocks */
}

static void free_block(uint32_t blk) {
    uint32_t group  = blk / BISCUITFS_BLOCKS_PER_GRP;
    uint32_t local  = blk % BISCUITFS_BLOCKS_PER_GRP;
    if (group >= fs_num_groups) return;

    uint8_t bitmap[BISCUITFS_BLOCK_SIZE];
    if (read_block(bgd_table[group].block_bitmap, bitmap) != 0) return;
    bitmap[local / 8] &= ~(1U << (local % 8));
    biscuitfs_journal_write_block(bgd_table[group].block_bitmap, bitmap);

    bgd_table[group].free_blocks++;
    sb.free_blocks++;
    write_bgd(group);
}

// ---------------------------------------------------------------------------
// Inode allocation / deallocation
// ---------------------------------------------------------------------------

static uint32_t alloc_inode_in_group(uint32_t group) {
    if (bgd_table[group].free_inodes == 0) return 0;

    uint8_t bitmap[BISCUITFS_BLOCK_SIZE];
    if (read_block(bgd_table[group].inode_bitmap, bitmap) != 0) return 0;

    uint32_t inodes = BISCUITFS_INODES_PER_GRP;
    for (uint32_t w = 0; w < inodes / 8; w++) {
        if (bitmap[w] == 0xFF) continue;
        for (int b = 0; b < 8; b++) {
            if (!(bitmap[w] & (1U << b))) {
                bitmap[w] |= (1U << b);
                uint32_t ino = group * BISCUITFS_INODES_PER_GRP
                             + w * 8 + (uint32_t)b + 1;  /* 1-based */

                biscuitfs_journal_write_block(bgd_table[group].inode_bitmap, bitmap);
                bgd_table[group].free_inodes--;
                sb.free_inodes--;
                write_bgd(group);
                return ino;
            }
        }
    }
    return 0;
}

static uint32_t alloc_inode(void) {
    for (uint32_t g = 0; g < fs_num_groups; g++) {
        uint32_t ino = alloc_inode_in_group(g);
        if (ino) return ino;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Block pointer resolver (direct/indirect/double-indirect)
// ---------------------------------------------------------------------------

// Get the n-th data block number for an inode (0-based)
static uint32_t inode_get_block(biscuitfs_inode_t *inode, uint32_t n) {
    if (n < BISCUITFS_N_DIRECT) return inode->block[n];

    // Singly indirect
    n -= BISCUITFS_N_DIRECT;
    uint32_t ptrs_per_block = fs_block_size / sizeof(uint32_t);
    if (n < ptrs_per_block) {
        uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];
        if (!inode->block[12] || read_block(inode->block[12], blkbuf) != 0)
            return 0;
        return ((uint32_t *)blkbuf)[n];
    }

    // Doubly indirect
    n -= ptrs_per_block;
    if (n < ptrs_per_block * ptrs_per_block) {
        uint8_t l1[BISCUITFS_BLOCK_SIZE];
        memset(l1, 0, sizeof(l1));
        if (!inode->block[13] || read_block(inode->block[13], l1) != 0) return 0;
        uint32_t l1_idx = n / ptrs_per_block;
        uint32_t l2_idx = n % ptrs_per_block;
        uint32_t l2_blk = ((uint32_t *)l1)[l1_idx];
        if (!l2_blk) return 0;
        uint8_t l2[BISCUITFS_BLOCK_SIZE];
        if (read_block(l2_blk, l2) != 0) return 0;
        return ((uint32_t *)l2)[l2_idx];
    }

    /* Triple indirect not implemented (handles files up to ~4GB already) */
    return 0;
}

// Set the n-th data block pointer for an inode (allocates indirect blocks)
static int inode_set_block(biscuitfs_inode_t *inode, uint32_t n, uint32_t blk) {
    uint32_t ptrs_per_block = fs_block_size / sizeof(uint32_t);

    if (n < BISCUITFS_N_DIRECT) {
        inode->block[n] = blk;
        return 0;
    }

    n -= BISCUITFS_N_DIRECT;
    if (n < ptrs_per_block) {
        // Allocate indirect block if needed
        if (!inode->block[12]) {
            uint32_t ib = alloc_block();
            if (!ib) return -1;
            uint8_t empty[BISCUITFS_BLOCK_SIZE];
            memset(empty, 0, sizeof(empty));
            write_block(ib, empty);
            inode->block[12] = ib;
        }
        uint8_t ibuf[BISCUITFS_BLOCK_SIZE];
        read_block(inode->block[12], ibuf);
        ((uint32_t *)ibuf)[n] = blk;
        biscuitfs_journal_write_block(inode->block[12], ibuf);
        return 0;
    }

    /* Double indirect allocation is similar but omitted for brevity */
    return -1;
}

// ---------------------------------------------------------------------------
// Directory operations
// ---------------------------------------------------------------------------

// Look up a name in a directory inode; return inode number or 0
static uint32_t dir_lookup(uint32_t dir_ino, const char *name) {
    biscuitfs_inode_t inode;
    if (read_inode(dir_ino, &inode) != 0) return 0;
    if ((inode.mode & BISCUITFS_IFMT) != BISCUITFS_IFDIR) return 0;

    uint32_t namelen = (uint32_t)strlen(name);
    uint32_t offset  = 0;
    uint32_t size    = inode.size_lo;
    uint8_t  blkbuf[BISCUITFS_BLOCK_SIZE];

    while (offset < size) {
        uint32_t blk_n = offset / fs_block_size;
        uint32_t blk_off = offset % fs_block_size;

        if (blk_off == 0) {
            uint32_t phys_blk = inode_get_block(&inode, blk_n);
            if (!phys_blk) break;
            if (read_block(phys_blk, blkbuf) != 0) break;
        }

        biscuitfs_dirent_t *de = (biscuitfs_dirent_t *)(blkbuf + blk_off);
        if (de->inode && de->name_len == namelen &&
            memcmp(de->name, name, namelen) == 0) {
            return de->inode;
        }

        if (de->rec_len == 0) break;
        offset += de->rec_len;
    }
    return 0;
}

// Resolve an absolute path to an inode number
static uint32_t path_to_inode(const char *path) {
    if (!path || path[0] != '/') return 0;

    uint32_t cur_ino = BISCUITFS_ROOT_INO;

    if (path[1] == '\0') return cur_ino;  /* root */

    // Walk each component
    char component[256];
    const char *p = path + 1;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len;
        if (slash) {
            len = (size_t)(slash - p);
            strncpy(component, p, len);
            component[len] = '\0';
            p = slash + 1;
        } else {
            strncpy(component, p, sizeof(component) - 1);
            p += strlen(p);
        }
        if (!component[0]) continue;  /* skip consecutive slashes */

        cur_ino = dir_lookup(cur_ino, component);
        if (!cur_ino) return 0;
    }
    return cur_ino;
}

// Add a directory entry (name -> inode) to a directory
static int dir_add_entry(uint32_t dir_ino, const char *name,
                         uint32_t ino, uint8_t ftype) {
    biscuitfs_inode_t dir_inode;
    if (read_inode(dir_ino, &dir_inode) != 0) return -1;

    uint32_t namelen = (uint32_t)strlen(name);
    // Align record length to 4 bytes
    uint16_t needed = (uint16_t)((BISCUITFS_DIRENT_MIN + namelen + 3) & ~3U);

    uint8_t  blkbuf[BISCUITFS_BLOCK_SIZE];
    uint32_t size     = dir_inode.size_lo;
    int      inserted = 0;

    // Scan existing directory blocks for a hole
    for (uint32_t b = 0; b * fs_block_size < size; b++) {
        uint32_t phys = inode_get_block(&dir_inode, b);
        if (!phys || read_block(phys, blkbuf) != 0) continue;

        uint32_t off = 0;
        while (off < fs_block_size) {
            biscuitfs_dirent_t *de = (biscuitfs_dirent_t *)(blkbuf + off);
            if (de->rec_len == 0) break;

            uint16_t actual = (uint16_t)((BISCUITFS_DIRENT_MIN
                                         + de->name_len + 3) & ~3U);
            if (!de->inode) actual = 0;
            int16_t slack = (int16_t)(de->rec_len - actual);

            if (slack >= (int16_t)needed) {
                // Shrink current entry and append new one
                uint16_t old_rec = de->rec_len;
                if (de->inode) de->rec_len = actual;

                biscuitfs_dirent_t *ne = (biscuitfs_dirent_t *)(blkbuf + off + (de->inode ? actual : 0));
                ne->inode     = ino;
                ne->rec_len   = (uint16_t)(old_rec - (de->inode ? actual : 0));
                ne->name_len  = (uint8_t)namelen;
                ne->file_type = ftype;
                memcpy(ne->name, name, namelen);

                biscuitfs_journal_begin();
                biscuitfs_journal_write_block(phys, blkbuf);
                biscuitfs_journal_commit();
                inserted = 1;
                break;
            }
            off += de->rec_len;
        }
        if (inserted) break;
    }

    if (!inserted) {
        // Allocate a new block for the directory
        uint32_t new_blk = alloc_block();
        if (!new_blk) return -1;

        memset(blkbuf, 0, sizeof(blkbuf));
        biscuitfs_dirent_t *de = (biscuitfs_dirent_t *)blkbuf;
        de->inode     = ino;
        de->rec_len   = (uint16_t)fs_block_size;
        de->name_len  = (uint8_t)namelen;
        de->file_type = ftype;
        memcpy(de->name, name, namelen);

        uint32_t blk_n = dir_inode.size_lo / fs_block_size;
        inode_set_block(&dir_inode, blk_n, new_blk);

        dir_inode.size_lo += fs_block_size;
        dir_inode.blocks_lo += fs_sectors_per_block;

        biscuitfs_journal_begin();
        biscuitfs_journal_write_block(new_blk, blkbuf);
        write_inode(dir_ino, &dir_inode);
        biscuitfs_journal_commit();
    }
    return 0;
}

// ---------------------------------------------------------------------------
// VFS driver callbacks
// ---------------------------------------------------------------------------

static int biscuitfs_mount_cb(const char *mountpoint, uint32_t start_lba) {
    (void)mountpoint;
    fs_start_lba = start_lba;
    fs_block_size = BISCUITFS_BLOCK_SIZE;
    fs_sectors_per_block = fs_block_size / 512;

    // Read superblock (at byte 1024 = LBA offset 2 from start for 512B sectors)
    uint8_t sb_buf[512];
    uint32_t sb_lba = start_lba + 2;   /* byte 1024 */
    if (ata_read_sector(sb_lba, sb_buf) != 0) return -1;

    memcpy(&sb, sb_buf, sizeof(sb));
    if (sb.magic != BISCUITFS_MAGIC) {
        kprintf("[BISCUITFS] Bad magic: 0x%x (expected 0x%x)\n",
                sb.magic, BISCUITFS_MAGIC);
        return -1;
    }

    fs_num_groups = (sb.block_count + BISCUITFS_BLOCKS_PER_GRP - 1)
                    / BISCUITFS_BLOCKS_PER_GRP;
    if (fs_num_groups > MAX_GROUPS) fs_num_groups = MAX_GROUPS;

    // Read block group descriptors
    for (uint32_t g = 0; g < fs_num_groups; g++) read_bgd(g);

    // Find journal start block (stored in superblock journal_inode field;
    // here we use a fixed location at the end of group 0's inode table region)
    jnl_start_blk = bgd_table[0].inode_table
                  + (BISCUITFS_INODES_PER_GRP * BISCUITFS_INODE_SIZE
                     / BISCUITFS_BLOCK_SIZE);

    // Mark filesystem as mounted (dirty)
    sb.state       = BISCUITFS_STATE_ERROR;
    sb.mount_count = (uint16_t)(sb.mount_count + 1);

    // Replay any uncommitted journal transactions
    biscuitfs_journal_replay();

    fs_mounted = 1;
    kprintf("[BISCUITFS] Mounted '%.*s' (%d blocks, %d inodes free)\n",
            (int)sizeof(sb.volume_name), sb.volume_name,
            sb.free_blocks, sb.free_inodes);
    return 0;
}

static int biscuitfs_open_cb(const char *path, int flags) {
    uint32_t ino = path_to_inode(path);
    if (!ino) {
        if (strcmp(path, "/bin/init") == 0 || strcmp(path, "/bin/bash") == 0 ||
            strcmp(path, "/etc/fstab") == 0) {
            kprintf("[BISCUITFS] open miss path=%s bin=%u etc=%u init=%u fstab=%u\n",
                    path,
                    path_to_inode("/bin"),
                    path_to_inode("/etc"),
                    path_to_inode("/bin/init"),
                    path_to_inode("/etc/fstab"));
        }
        if (!(flags & VFS_O_CREAT)) return -1;

        // Create the file
        biscuitfs_journal_begin();
        ino = alloc_inode();
        if (!ino) { biscuitfs_journal_abort(); return -1; }

        biscuitfs_inode_t inode;
        memset(&inode, 0, sizeof(inode));
        inode.mode       = BISCUITFS_IFREG | 0644;
        inode.links_count = 1;

        write_inode(ino, &inode);

        // Add to parent directory
        char parent[256], *slash;
        strncpy(parent, path, sizeof(parent) - 1);
        parent[sizeof(parent) - 1] = '\0';
        slash = strrchr(parent, '/');
        const char *basename = path;
        if (slash) {
            *slash   = '\0';
            basename = slash + 1;
        }
        uint32_t dir_ino = slash ? path_to_inode(parent[0] ? parent : "/")
                                 : BISCUITFS_ROOT_INO;
        if (!dir_ino) { biscuitfs_journal_abort(); return -1; }

        dir_add_entry(dir_ino, basename, ino, BISCUITFS_FT_REG_FILE);
        biscuitfs_journal_commit();
    }

    // Find free fd slot
    for (int i = 0; i < BISCUITFS_MAX_OPEN; i++) {
        if (!fd_table[i].used) {
            biscuitfs_inode_t inode;
            read_inode(ino, &inode);
            fd_table[i].used     = 1;
            fd_table[i].inode_no = ino;
            fd_table[i].offset   = 0;
            fd_table[i].size     = inode.size_lo;
            fd_table[i].flags    = (uint32_t)flags;
            if (flags & VFS_O_TRUNC) {
                /* Would free data blocks and reset size; omitted for brevity */
                fd_table[i].size = 0;
            }
            return i;
        }
    }
    kprintf("[BISCUITFS] open fd-table full for %s\n", path);
    return -1;
}

static int biscuitfs_read_cb(int fd, uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= BISCUITFS_MAX_OPEN || !fd_table[fd].used) return -1;
    biscuitfs_fd_t *f = &fd_table[fd];

    if (f->offset >= f->size) return 0;   /* EOF */

    uint32_t remaining = f->size - f->offset;
    if (len > remaining) len = remaining;

    uint32_t done = 0;
    uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];

    biscuitfs_inode_t inode;
    read_inode(f->inode_no, &inode);

    while (done < len) {
        uint32_t blk_n   = f->offset / fs_block_size;
        uint32_t blk_off = f->offset % fs_block_size;
        uint32_t phys    = inode_get_block(&inode, blk_n);
        if (!phys) break;
        if (read_block(phys, blkbuf) != 0) break;

        uint32_t chunk = (uint32_t)(len - done);
        if (chunk > fs_block_size - blk_off)
            chunk = fs_block_size - blk_off;

        memcpy(buf + done, blkbuf + blk_off, chunk);
        done      += chunk;
        f->offset += chunk;
    }
    return (int)done;
}

static int biscuitfs_write_cb(int fd, const uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= BISCUITFS_MAX_OPEN || !fd_table[fd].used) return -1;
    biscuitfs_fd_t *f = &fd_table[fd];

    uint32_t done = 0;
    uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];

    biscuitfs_inode_t inode;
    read_inode(f->inode_no, &inode);

    biscuitfs_journal_begin();

    while (done < len) {
        uint32_t blk_n   = f->offset / fs_block_size;
        uint32_t blk_off = f->offset % fs_block_size;

        uint32_t phys = inode_get_block(&inode, blk_n);
        if (!phys) {
            phys = alloc_block();
            if (!phys) break;
            uint8_t empty[BISCUITFS_BLOCK_SIZE];
            memset(empty, 0, sizeof(empty));
            write_block(phys, empty);
            inode_set_block(&inode, blk_n, phys);
            inode.blocks_lo += fs_sectors_per_block;
        }

        if (read_block(phys, blkbuf) != 0) break;

        uint32_t chunk = (uint32_t)(len - done);
        if (chunk > fs_block_size - blk_off)
            chunk = fs_block_size - blk_off;

        memcpy(blkbuf + blk_off, buf + done, chunk);
        biscuitfs_journal_write_block(phys, blkbuf);

        done      += chunk;
        f->offset += chunk;
        if (f->offset > inode.size_lo) inode.size_lo = f->offset;
    }

    f->size = inode.size_lo;
    write_inode(f->inode_no, &inode);
    biscuitfs_journal_commit();

    return (int)done;
}

static int biscuitfs_close_cb(int fd) {
    if (fd < 0 || fd >= BISCUITFS_MAX_OPEN) return -1;
    fd_table[fd].used = 0;
    return 0;
}

static int biscuitfs_readdir_cb(const char *path, vfs_dirent_t *out, int max) {
    uint32_t dir_ino = path_to_inode(path);
    if (!dir_ino) return -1;

    biscuitfs_inode_t inode;
    if (read_inode(dir_ino, &inode) != 0) return -1;
    if ((inode.mode & BISCUITFS_IFMT) != BISCUITFS_IFDIR) return -1;

    uint8_t  blkbuf[BISCUITFS_BLOCK_SIZE];
    uint32_t offset = 0;
    uint32_t size   = inode.size_lo;
    int      count  = 0;

    while (offset < size && count < max) {
        uint32_t blk_n   = offset / fs_block_size;
        uint32_t blk_off = offset % fs_block_size;

        if (blk_off == 0) {
            uint32_t phys = inode_get_block(&inode, blk_n);
            if (!phys) break;
            if (read_block(phys, blkbuf) != 0) break;
        }

        biscuitfs_dirent_t *de = (biscuitfs_dirent_t *)(blkbuf + blk_off);
        if (de->rec_len == 0) break;

        if (de->inode && de->name_len > 0) {
            uint32_t nlen = de->name_len;
            if (nlen >= VFS_NAME_LEN) nlen = VFS_NAME_LEN - 1;
            memcpy(out[count].name, de->name, nlen);
            out[count].name[nlen] = '\0';
            out[count].inode    = de->inode;
            out[count].is_dir   = (de->file_type == BISCUITFS_FT_DIR) ? 1 : 0;

            // Read inode for size
            biscuitfs_inode_t child_inode;
            if (read_inode(de->inode, &child_inode) == 0)
                out[count].size = child_inode.size_lo;

            count++;
        }
        offset += de->rec_len;
    }
    return count;
}

static int biscuitfs_mkdir_cb(const char *path) {
    if (path_to_inode(path)) return -1;

    // Check if parent exists
    char parent_path[256];
    strncpy(parent_path, path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';
    char *slash = strrchr(parent_path, '/');
    if (!slash) return -1;

    const char *name = slash + 1;
    if (*slash == '\0') return -1;
    *slash = '\0';

    uint32_t parent_ino = path_to_inode(parent_path[0] ? parent_path : "/");
    if (!parent_ino) return -1;

    // Allocate new inode + initialise directory
    biscuitfs_journal_begin();

    uint32_t ino = alloc_inode();
    if (!ino) { biscuitfs_journal_abort(); return -1; }

    biscuitfs_inode_t inode;
    memset(&inode, 0, sizeof(inode));
    inode.mode        = BISCUITFS_IFDIR | 0755;
    inode.links_count = 2;   /* itself + "." */

    // Allocate one data block for the new directory's initial entries
    uint32_t data_blk = alloc_block();
    if (!data_blk) {
        biscuitfs_journal_abort();
        return -1;
    }

    uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];
    memset(blkbuf, 0, sizeof(blkbuf));

    // "." entry
    biscuitfs_dirent_t *dot = (biscuitfs_dirent_t *)blkbuf;
    dot->inode     = ino;
    dot->rec_len   = 12;
    dot->name_len  = 1;
    dot->file_type = BISCUITFS_FT_DIR;
    dot->name[0]   = '.';

    // ".." entry
    biscuitfs_dirent_t *dotdot = (biscuitfs_dirent_t *)(blkbuf + 12);
    dotdot->inode     = parent_ino;
    dotdot->rec_len   = (uint16_t)(fs_block_size - 12);
    dotdot->name_len  = 2;
    dotdot->file_type = BISCUITFS_FT_DIR;
    dotdot->name[0]   = '.';
    dotdot->name[1]   = '.';

    inode.block[0]   = data_blk;
    inode.size_lo    = fs_block_size;
    inode.blocks_lo  = fs_sectors_per_block;

    biscuitfs_journal_write_block(data_blk, blkbuf);
    write_inode(ino, &inode);

    // Add entry to parent
    dir_add_entry(parent_ino, name, ino, BISCUITFS_FT_DIR);

    biscuitfs_journal_commit();
    return 0;
}

static int biscuitfs_unlink_cb(const char *path) {
    // Minimal: locate the inode and decrement its link count
    uint32_t ino = path_to_inode(path);
    if (!ino) return -1;

    biscuitfs_inode_t inode;
    if (read_inode(ino, &inode) != 0) return -1;

    biscuitfs_journal_begin();

    inode.links_count--;
    if (inode.links_count == 0) {
        // Free all data blocks
        for (int i = 0; i < BISCUITFS_N_DIRECT; i++) {
            if (inode.block[i]) { free_block(inode.block[i]); inode.block[i] = 0; }
        }
        inode.size_lo = 0;
        // Mark inode as deleted in inode bitmap
        uint32_t group = (ino - 1) / BISCUITFS_INODES_PER_GRP;
        uint32_t local = (ino - 1) % BISCUITFS_INODES_PER_GRP;
        uint8_t  bitmap[BISCUITFS_BLOCK_SIZE];
        read_block(bgd_table[group].inode_bitmap, bitmap);
        bitmap[local / 8] &= ~(1U << (local % 8));
        biscuitfs_journal_write_block(bgd_table[group].inode_bitmap, bitmap);
        bgd_table[group].free_inodes++;
        sb.free_inodes++;
        write_bgd(group);
    }
    write_inode(ino, &inode);

    // Remove directory entry (simplified: mark entry inode as 0)
    char parent_path[256];
    strncpy(parent_path, path, sizeof(parent_path) - 1);
    parent_path[sizeof(parent_path) - 1] = '\0';
    char *slash = strrchr(parent_path, '/');
    const char *name = slash ? slash + 1 : path;
    if (slash) *slash = '\0';
    uint32_t dir_ino = path_to_inode(parent_path[0] ? parent_path : "/");

    if (dir_ino) {
        biscuitfs_inode_t dir_inode;
        read_inode(dir_ino, &dir_inode);
        uint8_t blkbuf[BISCUITFS_BLOCK_SIZE];
        uint32_t size = dir_inode.size_lo;
        uint32_t namelen = (uint32_t)strlen(name);

        for (uint32_t b = 0; b * fs_block_size < size; b++) {
            uint32_t phys = inode_get_block(&dir_inode, b);
            if (!phys || read_block(phys, blkbuf) != 0) continue;
            uint32_t off = 0;
            while (off < fs_block_size) {
                biscuitfs_dirent_t *de = (biscuitfs_dirent_t *)(blkbuf + off);
                if (de->rec_len == 0) break;
                if (de->inode == ino && de->name_len == namelen &&
                    memcmp(de->name, name, namelen) == 0) {
                    de->inode = 0;
                    biscuitfs_journal_write_block(phys, blkbuf);
                    break;
                }
                off += de->rec_len;
            }
        }
    }

    biscuitfs_journal_commit();
    return 0;
}

// ---------------------------------------------------------------------------
// VFS registration
// ---------------------------------------------------------------------------

static filesystem_t biscuitfs_driver = {
    .name    = "biscuitfs",
    .mount   = biscuitfs_mount_cb,
    .open    = biscuitfs_open_cb,
    .read    = biscuitfs_read_cb,
    .write   = biscuitfs_write_cb,
    .close   = biscuitfs_close_cb,
    .readdir = biscuitfs_readdir_cb,
    .mkdir   = biscuitfs_mkdir_cb,
    .unlink  = biscuitfs_unlink_cb,
};

filesystem_t *biscuitfs_get_filesystem(void) {
    return &biscuitfs_driver;
}

int biscuitfs_init(uint32_t start_lba) {
    for (int i = 0; i < BISCUITFS_MAX_OPEN; i++) fd_table[i].used = 0;
    return biscuitfs_mount_cb("/", start_lba);
}

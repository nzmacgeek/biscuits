#pragma once
// BlueyOS "BiscuitFS" - ext4-inspired Filesystem
// "Everything in its place!" - Bingo Heeler
// Episode ref: "Sleepytime" - Bingo organises her dream world perfectly
//
// On-disk layout (all sizes assume 4096-byte blocks):
//
//   Block  0          : Boot block (reserved / MBR partition boot code)
//   Bytes 1024-2047   : Superblock (in block 0 for small fs, block 1 for large)
//   Block  1+         : Block group descriptors (1 per 8192 blocks)
//   Per block group:
//     Block 0 of group : Block usage bitmap
//     Block 1 of group : Inode usage bitmap
//     Blocks 2..N      : Inode table (BISCUITFS_INODES_PER_GROUP * inode_size)
//     Blocks N+1..     : Data blocks
//
// Fixed inode numbers:
//   0  = unused (inode 0 is never valid)
//   1  = defective block list
//   2  = root directory "/"
//   11 = first non-reserved inode (like ext4)
//
// Journal: a dedicated block group region using a "bluey journal" format
// that records before-images of metadata blocks before committing.
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "vfs.h"

// ---------------------------------------------------------------------------
// Superblock constants
// ---------------------------------------------------------------------------
#define BISCUITFS_MAGIC           0xB15C0001   /* "BISC" inspired */
#define BISCUITFS_VERSION         1
#define BISCUITFS_BLOCK_SIZE      4096
#define BISCUITFS_INODE_SIZE      256
#define BISCUITFS_INODES_PER_GRP  8192
#define BISCUITFS_BLOCKS_PER_GRP  8192
#define BISCUITFS_FIRST_INO       11           /* first non-reserved inode */
#define BISCUITFS_ROOT_INO        2
#define BISCUITFS_JOURNAL_BLOCKS  2048         /* 8 MB journal */

// Feature flags (stored in superblock)
#define BISCUITFS_FEAT_JOURNAL    0x0001
#define BISCUITFS_FEAT_EXTENTS    0x0002
#define BISCUITFS_FEAT_ACL        0x0004
#define BISCUITFS_FEAT_XATTR      0x0008

// ---------------------------------------------------------------------------
// Superblock (stored at byte offset 1024 from the start of the filesystem)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint32_t magic;              /* BISCUITFS_MAGIC                          */
    uint32_t version;            /* filesystem version                        */
    uint32_t block_count;        /* total blocks in filesystem                */
    uint32_t reserved_blocks;    /* blocks reserved for root                  */
    uint32_t free_blocks;        /* free block count                          */
    uint32_t inode_count;        /* total inodes                              */
    uint32_t free_inodes;        /* free inode count                          */
    uint32_t first_data_block;   /* block containing superblock (0 for 4K)   */
    uint32_t block_size_log;     /* log2(block_size) - 10  (2 → 4096)        */
    uint32_t blocks_per_group;   /* BISCUITFS_BLOCKS_PER_GRP                 */
    uint32_t inodes_per_group;   /* BISCUITFS_INODES_PER_GRP                 */
    uint32_t mtime;              /* last mount time (Bandit epoch)            */
    uint32_t wtime;              /* last write time (Bandit epoch)            */
    uint16_t mount_count;        /* number of mounts since last fsck          */
    uint16_t max_mount_count;    /* max mounts before forced fsck (-1 = none) */
    uint16_t state;              /* 1=clean, 2=errors                         */
    uint16_t errors;             /* what to do on errors: 1=continue,2=ro    */
    uint32_t features;           /* feature bitmask                           */
    uint32_t journal_inode;      /* inode of the journal (usually 8)          */
    uint32_t first_ino;          /* first non-reserved inode                  */
    uint16_t inode_size;         /* size of each inode in bytes               */
    uint8_t  uuid[16];           /* filesystem UUID                           */
    uint8_t  volume_name[16];    /* volume label                              */
    uint8_t  reserved[404];      /* pad to 512 bytes total                    */
} biscuitfs_super_t;

// State values
#define BISCUITFS_STATE_CLEAN   1
#define BISCUITFS_STATE_ERROR   2

// ---------------------------------------------------------------------------
// Block group descriptor (32 bytes, stored in the block after the superblock)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint32_t block_bitmap;       /* block number of the block usage bitmap    */
    uint32_t inode_bitmap;       /* block number of the inode usage bitmap    */
    uint32_t inode_table;        /* block number of the first inode table blk */
    uint16_t free_blocks;        /* free blocks in this group                 */
    uint16_t free_inodes;        /* free inodes in this group                 */
    uint16_t used_dirs;          /* directories in this group                 */
    uint16_t pad;
    uint8_t  reserved[12];
} biscuitfs_bgd_t;

// ---------------------------------------------------------------------------
// Inode (256 bytes)
// ---------------------------------------------------------------------------
#define BISCUITFS_N_DIRECT   12
#define BISCUITFS_N_INDIRECT  1
#define BISCUITFS_N_DINDIRECT 1
#define BISCUITFS_N_TINDIRECT 1

// File type bits (stored in inode.mode top 4 bits, like ext2/4)
#define BISCUITFS_IFMT   0xF000
#define BISCUITFS_IFREG  0x8000   /* regular file */
#define BISCUITFS_IFDIR  0x4000   /* directory    */
#define BISCUITFS_IFLNK  0xA000   /* symlink      */
#define BISCUITFS_IFBLK  0x6000   /* block device */
#define BISCUITFS_IFCHR  0x2000   /* char device  */
#define BISCUITFS_IFIFO  0x1000   /* FIFO/pipe    */

// Permission bits
#define BISCUITFS_ISUID  04000
#define BISCUITFS_ISGID  02000
#define BISCUITFS_IRUSR  0400
#define BISCUITFS_IWUSR  0200
#define BISCUITFS_IXUSR  0100
#define BISCUITFS_IRGRP  0040
#define BISCUITFS_IWGRP  0020
#define BISCUITFS_IXGRP  0010
#define BISCUITFS_IROTH  0004
#define BISCUITFS_IWOTH  0002
#define BISCUITFS_IXOTH  0001

typedef struct __attribute__((packed)) {
    uint16_t mode;               /* file type + permissions (like st_mode)    */
    uint16_t uid;                /* owner user ID                             */
    uint32_t size_lo;            /* file size in bytes (low 32 bits)          */
    uint32_t atime;              /* access time (Bandit epoch)                */
    uint32_t ctime;              /* inode change time (Bandit epoch)          */
    uint32_t mtime;              /* modification time (Bandit epoch)          */
    uint32_t dtime;              /* deletion time (0 if not deleted)          */
    uint16_t gid;                /* group ID                                  */
    uint16_t links_count;        /* hard link count                           */
    uint32_t blocks_lo;          /* number of 512-byte blocks allocated       */
    uint32_t flags;              /* inode flags                               */
    uint32_t osd1;               /* OS-specific field 1 (BlueyOS: reserved)   */
    uint32_t block[15];          /* 12 direct + 1 indirect + 1 dbl + 1 triple */
    uint32_t generation;         /* file version for NFS                      */
    uint32_t file_acl;           /* block with extended attributes (ACL)      */
    uint32_t size_hi;            /* file size high 32 bits (for >4GB files)   */
    uint32_t obso_faddr;         /* obsolete fragment address                 */
    uint8_t  osd2[12];           /* OS-specific field 2                       */
    uint16_t extra_isize;        /* extra inode size (for inline xattr)       */
    uint16_t checksum_hi;        /* CRC32c checksum (high 16 bits)            */
    uint32_t ctime_extra;        /* creation time sub-second component        */
    uint32_t mtime_extra;        /* modification time extra                   */
    uint32_t atime_extra;        /* access time extra                         */
    uint32_t crtime;             /* creation time (Bandit epoch)              */
    uint32_t crtime_extra;
    uint32_t version_hi;
    uint32_t projid;
    uint8_t  _pad[60];           /* pad to 256 bytes total                    */
} biscuitfs_inode_t;

// Inode flags
#define BISCUITFS_INODE_SECRM      0x00000001
#define BISCUITFS_INODE_UNRM       0x00000002
#define BISCUITFS_INODE_SYNC       0x00000008
#define BISCUITFS_INODE_IMMUTABLE  0x00000010
#define BISCUITFS_INODE_APPEND     0x00000020
#define BISCUITFS_INODE_NOATIME    0x00000080
#define BISCUITFS_INODE_JOURNAL    0x00040000
#define BISCUITFS_INODE_INLINE     0x10000000

// ---------------------------------------------------------------------------
// Directory entry (variable length, 4-byte aligned)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint32_t inode;              /* inode number (0 = unused entry)           */
    uint16_t rec_len;            /* length of this directory entry            */
    uint8_t  name_len;           /* length of the file name                   */
    uint8_t  file_type;          /* file type (1=reg, 2=dir, 7=symlink…)     */
    char     name[255];          /* file name (not NUL-terminated on disk)    */
} biscuitfs_dirent_t;

// File type codes for directory entries (same as ext4)
#define BISCUITFS_FT_UNKNOWN   0
#define BISCUITFS_FT_REG_FILE  1
#define BISCUITFS_FT_DIR       2
#define BISCUITFS_FT_CHRDEV    3
#define BISCUITFS_FT_BLKDEV    4
#define BISCUITFS_FT_FIFO      5
#define BISCUITFS_FT_SOCK      6
#define BISCUITFS_FT_SYMLINK   7

// Minimum size of a directory entry (inode+rec_len+name_len+file_type = 8)
#define BISCUITFS_DIRENT_MIN   8

// ---------------------------------------------------------------------------
// ACL on-disk format (stored in an EA block or inline)
// POSIX ACL (like ext4 acl_ea_header + entries)
// ---------------------------------------------------------------------------
#define BISCUITFS_ACL_VERSION   0x0002
#define POSIX_ACL_USER_OBJ      0x01
#define POSIX_ACL_USER          0x02
#define POSIX_ACL_GROUP_OBJ     0x04
#define POSIX_ACL_GROUP         0x08
#define POSIX_ACL_MASK          0x10
#define POSIX_ACL_OTHER         0x20

typedef struct __attribute__((packed)) {
    uint16_t e_tag;              /* ACL_USER_OBJ, ACL_USER, ACL_GROUP, etc.  */
    uint16_t e_perm;             /* RWX permission bitmask                    */
    uint32_t e_id;               /* UID or GID (undefined for *_OBJ entries)  */
} biscuitfs_acl_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t a_version;          /* BISCUITFS_ACL_VERSION                     */
} biscuitfs_acl_header_t;

// ---------------------------------------------------------------------------
// Extended attributes block header (EA block pointed to by inode.file_acl)
// ---------------------------------------------------------------------------
#define BISCUITFS_XATTR_MAGIC   0xEA020000

typedef struct __attribute__((packed)) {
    uint32_t h_magic;
    uint32_t h_refcount;
    uint32_t h_blocks;           /* number of blocks used by the EA block     */
    uint32_t h_hash;
    uint32_t h_checksum;
    uint32_t h_reserved[3];
} biscuitfs_xattr_hdr_t;

typedef struct __attribute__((packed)) {
    uint8_t  e_name_len;         /* length of name                            */
    uint8_t  e_name_index;       /* attribute name index (2=posix.acl_access) */
    uint16_t e_value_offs;       /* offset to value within block              */
    uint32_t e_value_inum;       /* inode storing value (0 = inline)          */
    uint32_t e_value_size;       /* size of value in bytes                    */
    uint32_t e_hash;
    char     e_name[0];          /* attribute name (without null terminator)  */
} biscuitfs_xattr_entry_t;

// ---------------------------------------------------------------------------
// Journal (minimal write-ahead logging)
// ---------------------------------------------------------------------------
#define BISCUITFS_JNL_MAGIC      0xB15CDA7A   /* "BISC DATA" */
#define BISCUITFS_JNL_BLOCK_SIZE 4096
#define BISCUITFS_JNL_DESCRIPTOR 1
#define BISCUITFS_JNL_COMMIT     2
#define BISCUITFS_JNL_ABORT      3
#define BISCUITFS_JNL_REVOKE     4

// Journal block header (12 bytes; remainder of the 4096-byte block is data)
typedef struct __attribute__((packed)) {
    uint32_t h_magic;
    uint32_t h_blocktype;
    uint32_t h_sequence;
} biscuitfs_jnl_block_hdr_t;

// Journal descriptor tag (one per block being journalled)
typedef struct __attribute__((packed)) {
    uint32_t t_blocknr;
    uint32_t t_flags;
} biscuitfs_jnl_block_tag_t;

#define BISCUITFS_JNL_FLAG_LAST      0x8   /* last tag in descriptor */
#define BISCUITFS_JNL_FLAG_ESCAPE    0x2   /* data block has journal magic */
#define BISCUITFS_JNL_FLAG_SAME_UUID 0x4

// ---------------------------------------------------------------------------
// VFS integration
// ---------------------------------------------------------------------------
filesystem_t *biscuitfs_get_filesystem(void);
int           biscuitfs_init(uint32_t start_lba);

// Journal API (internal use, but exposed for mkfs/fsck tools)
void biscuitfs_journal_begin(void);
void biscuitfs_journal_write_block(uint32_t block_no, const uint8_t *data);
void biscuitfs_journal_commit(void);
void biscuitfs_journal_abort(void);
void biscuitfs_journal_replay(void);

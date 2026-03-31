// BlueyOS FAT16 Filesystem Driver
// "Bingo's Backpack Filesystem - everything fits in FAT16!"
// Episode ref: "The Pool" - sometimes you have to wait for a lane
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "fat.h"
#include "vfs.h"
#include "../kernel/syslog.h"
#include "../drivers/ata.h"
#include "../kernel/kheap.h"

// ---------------------------------------------------------------------------
// FAT16 runtime state
// ---------------------------------------------------------------------------
static fat16_bpb_t bpb;
static uint32_t    fat_start_lba;
static uint32_t    root_dir_lba;
static uint32_t    data_start_lba;
static uint32_t    bytes_per_cluster;
static uint16_t   *fat_cache = NULL;    // in-memory FAT (up to 64KB FAT = 32K entries)
static int         fat_loaded = 0;

// Simple open file table for FAT
#define FAT_MAX_OPEN 8
typedef struct {
    int      used;
    uint16_t first_cluster;
    uint32_t file_size;
    uint32_t offset;        // current read position
    uint16_t cur_cluster;
} fat_file_t;

static fat_file_t fat_files[FAT_MAX_OPEN];

// Read a sector into a 512-byte buffer
static int read_sector(uint32_t lba, uint8_t *buf) {
    return ata_read_sector(lba, buf);
}

// Follow the FAT chain: given cluster, return next cluster
static uint16_t fat_next_cluster(uint16_t cluster) {
    if (!fat_cache || cluster >= 0xFFF0) return 0xFFFF;
    return fat_cache[cluster];
}

// Convert cluster number to LBA
static uint32_t cluster_to_lba(uint16_t cluster) {
    return data_start_lba + (uint32_t)(cluster - 2) * bpb.sectors_per_cluster;
}

int fat_init(uint32_t start_lba) {
    uint8_t sector[512];
    fat_start_lba = start_lba;

    if (read_sector(start_lba, sector) != 0) {
        kprintf("[FAT]  Failed to read BPB sector!\n");
        return -1;
    }
    memcpy(&bpb, sector, sizeof(fat16_bpb_t));

    // Verify this looks like a FAT16 volume
    if (bpb.bytes_per_sector != 512) {
        kprintf("[FAT]  Unexpected sector size: %d\n", bpb.bytes_per_sector);
        return -1;
    }

    uint32_t fat_lba   = start_lba + bpb.reserved_sectors;
    root_dir_lba       = fat_lba + (uint32_t)bpb.num_fats * bpb.fat_size_16;
    uint32_t root_secs = ((uint32_t)bpb.root_entry_count * 32 + 511) / 512;
    data_start_lba     = root_dir_lba + root_secs;
    bytes_per_cluster  = (uint32_t)bpb.sectors_per_cluster * 512;

    // Cache the first FAT (up to 64KB = 32768 entries)
    uint32_t fat_bytes = (uint32_t)bpb.fat_size_16 * 512;
    if (fat_bytes > 65536) fat_bytes = 65536;
    fat_cache = (uint16_t*)kheap_alloc(fat_bytes, 0);
    if (fat_cache) {
        uint8_t *fc = (uint8_t*)fat_cache;
        for (uint32_t s = 0; s < bpb.fat_size_16 && s*512 < fat_bytes; s++) {
            read_sector(fat_lba + s, fc + s * 512);
        }
        fat_loaded = 1;
    }

    kprintf("[FAT]  FAT16 volume: %.11s  cluster=%dB  root@LBA=%d\n",
            bpb.volume_label, bytes_per_cluster, root_dir_lba);
    return 0;
}

// Look up a file in the root directory, return directory entry
static int fat_find_root(const char *name83, fat16_dirent_t *out) {
    uint8_t sector[512];
    uint32_t entries_per_sector = 512 / sizeof(fat16_dirent_t);
    uint32_t sectors = ((uint32_t)bpb.root_entry_count * 32 + 511) / 512;

    for (uint32_t s = 0; s < sectors; s++) {
        if (read_sector(root_dir_lba + s, sector) != 0) return -1;
        fat16_dirent_t *ents = (fat16_dirent_t*)sector;
        for (uint32_t i = 0; i < entries_per_sector; i++) {
            if (ents[i].name[0] == 0x00) return -1; // end of dir
            if ((uint8_t)ents[i].name[0] == 0xE5)   continue; // deleted
            if (ents[i].attributes == FAT_ATTR_LFN)  continue; // LFN
            // Convert name to 8.3 uppercase for comparison
            char entry_name[12];
            memcpy(entry_name, ents[i].name, 8);
            entry_name[8] = '\0';
            // Trim trailing spaces
            for (int k = 7; k >= 0 && entry_name[k] == ' '; k--) entry_name[k] = '\0';
            if (memcmp(entry_name, name83, strlen(name83)) == 0) {
                *out = ents[i];
                return 0;
            }
        }
    }
    return -1;
}

// VFS interface functions

static int fat_vfs_mount(const char *path, uint32_t start_lba) {
    (void)path;
    return fat_init(start_lba);
}

static int fat_vfs_open(const char *path, int flags) {
    (void)flags;
    // Extract filename from path (assume root dir for now)
    const char *fname = path;
    const char *slash = strrchr(path, '/');
    if (slash) fname = slash + 1;

    // Convert to 8.3 uppercase (simplified)
    char name83[12];
    int ni = 0;
    for (int i = 0; fname[i] && ni < 8; i++) {
        char c = fname[i];
        if (c == '.') break;
        if (c >= 'a' && c <= 'z') c -= 32;  // uppercase
        name83[ni++] = c;
    }
    name83[ni] = '\0';

    fat16_dirent_t ent;
    if (fat_find_root(name83, &ent) != 0) return -1;

    // Find free file slot
    for (int i = 0; i < FAT_MAX_OPEN; i++) {
        if (!fat_files[i].used) {
            fat_files[i].used          = 1;
            fat_files[i].first_cluster = ent.first_cluster;
            fat_files[i].file_size     = ent.file_size;
            fat_files[i].offset        = 0;
            fat_files[i].cur_cluster   = ent.first_cluster;
            return i;
        }
    }
    return -1; // no free slot
}

static int fat_vfs_read(int fd, uint8_t *buf, size_t len) {
    if (fd < 0 || fd >= FAT_MAX_OPEN || !fat_files[fd].used) return -1;
    fat_file_t *f = &fat_files[fd];

    uint32_t remaining = f->file_size - f->offset;
    if (len > remaining) len = remaining;
    if (len == 0) return 0;

    uint8_t sector[512];
    size_t  read_total = 0;

    while (read_total < len && f->cur_cluster < 0xFFF8) {
        uint32_t cluster_offset = f->offset % bytes_per_cluster;
        uint32_t sector_in_cluster = cluster_offset / 512;
        uint32_t byte_in_sector    = cluster_offset % 512;
        uint32_t lba = cluster_to_lba(f->cur_cluster) + sector_in_cluster;

        if (read_sector(lba, sector) != 0) break;

        uint32_t avail = 512 - byte_in_sector;
        uint32_t want  = (uint32_t)(len - read_total);
        if (avail > want) avail = want;

        memcpy(buf + read_total, sector + byte_in_sector, avail);
        read_total   += avail;
        f->offset    += avail;

        // Advance to next cluster if needed
        if ((f->offset % bytes_per_cluster) == 0) {
            f->cur_cluster = fat_next_cluster(f->cur_cluster);
        }
    }
    return (int)read_total;
}

static int fat_vfs_read_at(int fd, uint8_t *buf, size_t len, uint32_t offset) {
    if (fd < 0 || fd >= FAT_MAX_OPEN || !fat_files[fd].used) return -1;

    uint32_t remaining = fat_files[fd].file_size;
    if (offset >= remaining) return 0;
    if (len > remaining - offset) len = remaining - offset;
    if (len == 0) return 0;

    uint8_t sector[512];
    size_t read_total = 0;

    uint32_t local_offset = offset;
    while (read_total < len) {
        uint32_t cluster = fat_files[fd].first_cluster;
        uint32_t cluster_index = local_offset / bytes_per_cluster;
        // advance cluster chain cluster_index times
        for (uint32_t c = 0; c < cluster_index && cluster < 0xFFF8; c++) cluster = fat_next_cluster(cluster);
        if (cluster >= 0xFFF8) break;

        uint32_t cluster_offset = local_offset % bytes_per_cluster;
        uint32_t sector_in_cluster = cluster_offset / 512;
        uint32_t byte_in_sector = cluster_offset % 512;
        uint32_t lba = cluster_to_lba(cluster) + sector_in_cluster;

        if (read_sector(lba, sector) != 0) break;

        uint32_t avail = 512 - byte_in_sector;
        uint32_t want = (uint32_t)(len - read_total);
        if (avail > want) avail = want;

        memcpy(buf + read_total, sector + byte_in_sector, avail);
        read_total += avail;
        local_offset += avail;
    }
    return (int)read_total;
}

static int fat_vfs_write(int fd, const uint8_t *buf, size_t len) {
    // Write support is beyond scope for a basic research OS - Bingo is working on it!
    (void)fd; (void)buf; (void)len;
    /* Record the caller so we can see which code paths attempted FAT writes. */
    syslog_record_caller(__builtin_return_address(0));
    kprintf("[FAT]  Write not yet implemented - Bingo's backpack is read-only!\n");
    return -1;
}

static int fat_vfs_close(int fd) {
    if (fd >= 0 && fd < FAT_MAX_OPEN) fat_files[fd].used = 0;
    return 0;
}

static int fat_vfs_readdir(const char *path, vfs_dirent_t *out, int max) {
    (void)path;
    uint8_t sector[512];
    int count = 0;
    uint32_t entries_per_sector = 512 / sizeof(fat16_dirent_t);
    uint32_t sectors = ((uint32_t)bpb.root_entry_count * 32 + 511) / 512;

    for (uint32_t s = 0; s < sectors && count < max; s++) {
        if (read_sector(root_dir_lba + s, sector) != 0) break;
        fat16_dirent_t *ents = (fat16_dirent_t*)sector;
        for (uint32_t i = 0; i < entries_per_sector && count < max; i++) {
            if (ents[i].name[0] == 0x00) goto done;
            if ((uint8_t)ents[i].name[0] == 0xE5) continue;
            if (ents[i].attributes == FAT_ATTR_LFN) continue;

            // Copy to vfs_dirent_t
            memset(&out[count], 0, sizeof(vfs_dirent_t));
            memcpy(out[count].name, ents[i].name, 8);
            out[count].size   = ents[i].file_size;
            out[count].inode  = ents[i].first_cluster;
            out[count].is_dir = (ents[i].attributes & FAT_ATTR_DIRECTORY) ? 1 : 0;
            count++;
        }
    }
done:
    return count;
}

static int fat_vfs_stat(const char *path, vfs_stat_t *out) {
    if (!path || !out) return -1;
    const char *fname = path;
    const char *slash = strrchr(path, '/');
    if (slash) fname = slash + 1;

    char name83[12];
    int ni = 0;
    for (int i = 0; fname[i] && ni < 8; i++) {
        char c = fname[i];
        if (c == '.') break;
        if (c >= 'a' && c <= 'z') c -= 32;
        name83[ni++] = c;
    }
    name83[ni] = '\0';

    fat16_dirent_t ent;
    if (fat_find_root(name83, &ent) != 0) return -1;

    memset(out, 0, sizeof(*out));
    out->uid = 0;
    out->gid = 0;
    out->size = ent.file_size;
    out->is_dir = (ent.attributes & FAT_ATTR_DIRECTORY) ? 1 : 0;
    out->mode = out->is_dir ? (VFS_S_IFDIR | 0777) : (VFS_S_IFREG | 0777);
    return 0;
}

static filesystem_t fat16_fs = {
    .name    = "fat16",
    .mount   = fat_vfs_mount,
    .open    = fat_vfs_open,
    .read    = fat_vfs_read,
    .read_at = fat_vfs_read_at,
    .write   = fat_vfs_write,
    .close   = fat_vfs_close,
    .readdir = fat_vfs_readdir,
    .mkdir   = NULL,
    .unlink  = NULL,
    .stat    = fat_vfs_stat,
};

filesystem_t *fat_get_filesystem(void) {
    return &fat16_fs;
}

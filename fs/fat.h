#pragma once
// BlueyOS FAT16 Filesystem Driver
// "Bingo's Backpack Filesystem - everything fits in FAT16!"
// Episode ref: "The Pool" - sometimes the directory is full, but you find a way
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "vfs.h"

// FAT16 BIOS Parameter Block
typedef struct __attribute__((packed)) {
    uint8_t  jmp_boot[3];
    uint8_t  oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entry_count;
    uint16_t total_sectors_16;
    uint8_t  media_type;
    uint16_t fat_size_16;         // sectors per FAT
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    // FAT16 Extended BPB
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_signature;
    uint32_t volume_id;
    uint8_t  volume_label[11];
    uint8_t  fs_type[8];          // "FAT16   "
} fat16_bpb_t;

// FAT16 directory entry (32 bytes)
typedef struct __attribute__((packed)) {
    uint8_t  name[8];
    uint8_t  ext[3];
    uint8_t  attributes;
    uint8_t  reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_hi;   // always 0 for FAT16
    uint16_t write_time;
    uint16_t write_date;
    uint16_t first_cluster;
    uint32_t file_size;
} fat16_dirent_t;

// FAT attributes
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LFN        0x0F   // Long File Name entry

// FAT16 special cluster values
#define FAT16_FREE     0x0000
#define FAT16_RESERVED 0xFFF0
#define FAT16_BAD      0xFFF7
#define FAT16_EOC      0xFFF8   // End of Chain (0xFFF8..0xFFFF)

filesystem_t *fat_get_filesystem(void);
int fat_init(uint32_t start_lba);

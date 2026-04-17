#pragma once
// BlueyOS ATA PIO Disk Driver - "Let's find some data!"
// Episode ref: "Camping" - sometimes you have to dig for what you need
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

// ATA primary channel register base addresses
#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6
#define ATA_SECONDARY_BASE  0x170
#define ATA_SECONDARY_CTRL  0x376

// ATA register offsets from base
#define ATA_REG_DATA       0x00
#define ATA_REG_ERROR      0x01
#define ATA_REG_FEATURES   0x01
#define ATA_REG_SECCOUNT   0x02
#define ATA_REG_LBA_LO     0x03
#define ATA_REG_LBA_MID    0x04
#define ATA_REG_LBA_HI     0x05
#define ATA_REG_DRIVE      0x06
#define ATA_REG_STATUS     0x07
#define ATA_REG_CMD        0x07

// ATA status bits
#define ATA_SR_BSY   0x80   // busy
#define ATA_SR_DRQ   0x08   // data request ready
#define ATA_SR_ERR   0x01   // error

// ATA commands
#define ATA_CMD_READ_PIO   0x20
#define ATA_CMD_WRITE_PIO  0x30
#define ATA_CMD_IDENTIFY   0xEC
#define ATA_CMD_FLUSH_CACHE 0xE7

// Drive/head register bits (LBA mode, master device)
#define ATA_DRIVE_MASTER_LBA 0xE0

#define ATA_SECTOR_SIZE    512

int  ata_init(void);
int  ata_read_sector(uint32_t lba, uint8_t *buf);
int  ata_write_sector(uint32_t lba, const uint8_t *buf);
int  ata_identify(void);
int  ata_flush_cache(void);

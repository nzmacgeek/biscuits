// BlueyOS ATA PIO Disk Driver - "Let's find some data!"
// Episode ref: "Camping" - sometimes you have to dig for treasure
// LBA28 PIO mode - primary channel, master drive
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/ports.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "ata.h"

static int ata_present = 0;

// Wait for BSY to clear and DRQ to set (or error)
static int ata_wait_ready(void) {
    uint8_t status;
    int timeout = 100000;
    do {
        status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (--timeout == 0)      return -1;  // timeout - "Come ON!" - Bluey
    } while (status & ATA_SR_BSY);
    return 0;
}

static int ata_wait_drq(void) {
    uint8_t status;
    int timeout = 100000;
    do {
        status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -1;
        if (--timeout == 0)      return -1;
    } while (!(status & ATA_SR_DRQ));
    return 0;
}

// 400ns delay by reading alt status 4 times
static void ata_delay(void) {
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
    inb(ATA_PRIMARY_CTRL);
}

int ata_identify(void) {
    // Select master drive on primary channel
    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE, 0xA0);
    ata_delay();

    // Zero the LBA/count registers
    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LO, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID, 0);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HI, 0);

    // Send IDENTIFY command
    outb(ATA_PRIMARY_BASE + ATA_REG_CMD, ATA_CMD_IDENTIFY);
    ata_delay();

    uint8_t status = inb(ATA_PRIMARY_BASE + ATA_REG_STATUS);
    if (status == 0) return -1;   // no drive

    if (ata_wait_ready() != 0) return -1;
    if (ata_wait_drq()   != 0) return -1;

    // Read 256 words of identify data (we don't really need them)
    for (int i = 0; i < 256; i++) inw(ATA_PRIMARY_BASE + ATA_REG_DATA);
    return 0;
}

int ata_init(void) {
    if (ata_identify() != 0) {
        ata_present = 0;
        kprintf("[ATA]  No ATA device found (Jack forgot the disk!)\n");
        return -1;
    }
    ata_present = 1;
    kprintf("%s\n", MSG_ATA_INIT);
    return 0;
}

int ata_read_sector(uint32_t lba, uint8_t *buf) {
    if (ata_wait_ready() != 0) return -1;

    // LBA28: drive = 0xE0 | (LBA bits 24-27)
    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE,    ATA_DRIVE_MASTER_LBA | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, 1);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LO,   (uint8_t)(lba));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HI,   (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_BASE + ATA_REG_CMD,       ATA_CMD_READ_PIO);
    ata_delay();

    if (ata_wait_ready() != 0) return -1;
    if (ata_wait_drq()   != 0) return -1;

    uint16_t *w = (uint16_t*)buf;
    for (int i = 0; i < 256; i++) w[i] = inw(ATA_PRIMARY_BASE + ATA_REG_DATA);
    return 0;
}

int ata_write_sector(uint32_t lba, const uint8_t *buf) {
    if (ata_wait_ready() != 0) return -1;

    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE,    ATA_DRIVE_MASTER_LBA | ((lba >> 24) & 0x0F));
    outb(ATA_PRIMARY_BASE + ATA_REG_SECCOUNT, 1);
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_LO,   (uint8_t)(lba));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_MID,  (uint8_t)(lba >> 8));
    outb(ATA_PRIMARY_BASE + ATA_REG_LBA_HI,   (uint8_t)(lba >> 16));
    outb(ATA_PRIMARY_BASE + ATA_REG_CMD,       ATA_CMD_WRITE_PIO);
    ata_delay();

    if (ata_wait_ready() != 0) return -1;
    if (ata_wait_drq()   != 0) return -1;

    const uint16_t *w = (const uint16_t*)buf;
    for (int i = 0; i < 256; i++) outw(ATA_PRIMARY_BASE + ATA_REG_DATA, w[i]);

    // Flush write cache
    outb(ATA_PRIMARY_BASE + ATA_REG_CMD, ATA_CMD_FLUSH_CACHE);
    ata_delay();
    if (ata_wait_ready() != 0) return -1;
    return 0;
}

int ata_flush_cache(void) {
    if (!ata_present) return 0;
    if (ata_wait_ready() != 0) return -1;

    outb(ATA_PRIMARY_BASE + ATA_REG_DRIVE, ATA_DRIVE_MASTER_LBA);
    outb(ATA_PRIMARY_BASE + ATA_REG_CMD, ATA_CMD_FLUSH_CACHE);
    ata_delay();
    return ata_wait_ready();
}

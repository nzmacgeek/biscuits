// BlueyOS Swap - "Bandit's Storage Shed"
// Episode ref: "Hammerbarn" - when the house is full, use the shed
// Episode ref: "Markets" - store it, retrieve it, everything in its place
// Linux-inspired swap subsystem adapted for BlueyOS's simple paging model.
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
#include "swap.h"

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

// Bitmap: 1 bit per swap page (1 = used, 0 = free)
// Supports up to SWAP_MAX_PAGES = 4096 pages = 16 MB swap
#define BITMAP_WORDS  ((SWAP_MAX_PAGES + 31) / 32)

static uint32_t swap_bitmap[BITMAP_WORDS];
static uint32_t swap_start_lba;
static uint32_t swap_total;
static uint32_t swap_used;
static int      swap_active = 0;

// ---------------------------------------------------------------------------
// Bitmap helpers
// ---------------------------------------------------------------------------

static void bitmap_set(int bit) {
    swap_bitmap[bit >> 5] |=  (1U << (bit & 31));
}

static void bitmap_clr(int bit) {
    swap_bitmap[bit >> 5] &= ~(1U << (bit & 31));
}

static int bitmap_get(int bit) {
    return (swap_bitmap[bit >> 5] >> (bit & 31)) & 1;
}

static int bitmap_find_free(void) {
    for (uint32_t w = 0; w < BITMAP_WORDS; w++) {
        if (swap_bitmap[w] == 0xFFFFFFFF) continue;
        for (int b = 0; b < 32; b++) {
            int bit = (int)(w * 32 + (uint32_t)b);
            if ((uint32_t)bit >= swap_total) return -1;
            if (!bitmap_get(bit)) return bit;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Sector I/O helpers (one swap page = SWAP_SECTORS_PAGE sectors = 4 KiB)
// ---------------------------------------------------------------------------

// Convert a swap page slot to its starting LBA on disk.
// Slot 0 is the header page; user data starts at slot 1.
static uint32_t slot_to_lba(int slot) {
    return swap_start_lba + (uint32_t)(slot + 1) * SWAP_SECTORS_PAGE;
}

static int write_page(int slot, const uint8_t *src) {
    uint32_t lba = slot_to_lba(slot);
    for (int s = 0; s < SWAP_SECTORS_PAGE; s++) {
        if (ata_write_sector(lba + (uint32_t)s, src + s * 512) != 0) return -1;
    }
    return 0;
}

static int read_page(int slot, uint8_t *dst) {
    uint32_t lba = slot_to_lba(slot);
    for (int s = 0; s < SWAP_SECTORS_PAGE; s++) {
        if (ata_read_sector(lba + (uint32_t)s, dst + s * 512) != 0) return -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void swap_init(uint32_t start_lba, uint32_t num_pages) {
    swap_start_lba = start_lba;
    swap_total     = (num_pages > SWAP_MAX_PAGES) ? SWAP_MAX_PAGES : num_pages;
    swap_used      = 0;
    swap_active    = 0;

    memset(swap_bitmap, 0, sizeof(swap_bitmap));

    // Read and validate the swap header (at LBA start_lba, 8 sectors)
    uint8_t hdr_buf[SWAP_PAGE_SIZE];
    if (ata_read_sector(start_lba, hdr_buf) != 0) {
        kprintf("[SWAP] Failed to read swap header\n");
        return;
    }

    swap_header_t *hdr = (swap_header_t *)hdr_buf;
    if (hdr->magic != SWAP_MAGIC) {
        kprintf("[SWAP] No valid swap signature at LBA %d\n", start_lba);
        kprintf("[SWAP] Run mkswap to initialise a swap partition\n");
        return;
    }

    // Trust the header's page count if smaller than our compiled-in limit
    if (hdr->total_pages < swap_total) swap_total = hdr->total_pages;

    swap_active = 1;
    kprintf("[SWAP] Swap space ready: %d pages (%d KB) at LBA %d  label='%.16s'\n",
            swap_total, swap_total * 4, start_lba, hdr->label);
}

int swap_is_active(void) {
    return swap_active;
}

int swap_out(uint32_t phys_addr) {
    if (!swap_active) return -1;
    if (swap_used >= swap_total) {
        kprintf("[SWAP] Swap space full! Out of memory.\n");
        return -1;
    }

    int slot = bitmap_find_free();
    if (slot < 0) return -1;

    // Write the physical page (4 KiB) to disk
    if (write_page(slot, (const uint8_t *)phys_addr) != 0) return -1;

    bitmap_set(slot);
    swap_used++;
    return slot;
}

int swap_in(int slot, uint32_t phys_addr) {
    if (!swap_active) return -1;
    if (slot < 0 || (uint32_t)slot >= swap_total) return -1;
    if (!bitmap_get(slot)) return -1;   /* slot was not in use */

    if (read_page(slot, (uint8_t *)phys_addr) != 0) return -1;

    bitmap_clr(slot);
    swap_used--;
    return 0;
}

void swap_free(int slot) {
    if (!swap_active) return;
    if (slot < 0 || (uint32_t)slot >= swap_total) return;
    if (bitmap_get(slot)) {
        bitmap_clr(slot);
        swap_used--;
    }
}

uint32_t swap_total_pages(void) { return swap_total; }
uint32_t swap_used_pages(void)  { return swap_used;  }

void swap_print_info(void) {
    if (!swap_active) {
        kprintf("[SWAP] Swap is not active (no valid swap signature found)\n");
        return;
    }
    kprintf("[SWAP] Swap statistics:\n");
    kprintf("  Total  : %d pages (%d KB)\n", swap_total, swap_total * 4);
    kprintf("  Used   : %d pages (%d KB)\n", swap_used,  swap_used  * 4);
    kprintf("  Free   : %d pages (%d KB)\n",
            swap_total - swap_used, (swap_total - swap_used) * 4);
    kprintf("  LBA    : %d\n", swap_start_lba);
}

int swap_format(uint32_t start_lba, uint32_t num_pages, const char *label) {
    uint8_t hdr_buf[SWAP_PAGE_SIZE];
    memset(hdr_buf, 0, sizeof(hdr_buf));

    swap_header_t *hdr = (swap_header_t *)hdr_buf;
    hdr->magic       = SWAP_MAGIC;
    hdr->version     = 1;
    hdr->total_pages = (num_pages > SWAP_MAX_PAGES) ? SWAP_MAX_PAGES : num_pages;
    hdr->used_pages  = 0;

    if (label) {
        strncpy((char *)hdr->label, label, sizeof(hdr->label) - 1);
        hdr->label[sizeof(hdr->label) - 1] = '\0';
    }

    // Write header sectors
    for (int s = 0; s < SWAP_SECTORS_PAGE; s++) {
        if (ata_write_sector(start_lba + (uint32_t)s, hdr_buf + s * 512) != 0)
            return -1;
    }
    kprintf("[SWAP] Swap space formatted: %d pages at LBA %d\n",
            hdr->total_pages, start_lba);
    return 0;
}

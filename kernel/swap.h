#pragma once
// BlueyOS Swap - "Bandit's Storage Shed"
// Episode ref: "Hammerbarn" - when the house is full, use the shed
// Swap extends physical RAM by paging cold data to disk.
// Implementation borrows concepts from Linux swap subsystem.
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

// Swap slot = one physical page (4 KiB = 8 ATA sectors)
#define SWAP_PAGE_SIZE     4096
#define SWAP_SECTORS_PAGE  8          /* 512 * 8 = 4096 */
#define SWAP_MAX_PAGES     4096       /* 16 MB swap space */
#define SWAP_MAGIC         0x53574150 /* "SWAP" */

// Header written at the start of the swap partition/file (one 4K page)
typedef struct __attribute__((packed)) {
    uint32_t magic;              /* SWAP_MAGIC */
    uint32_t version;            /* 1 */
    uint32_t total_pages;        /* total usable swap pages */
    uint32_t used_pages;         /* how many pages are in use */
    uint8_t  label[16];          /* optional label */
    uint8_t  reserved[4060];     /* pad to 4096 bytes */
} swap_header_t;

void     swap_init(uint32_t start_lba, uint32_t num_pages);
int      swap_is_active(void);

// Write a 4K physical page to swap; returns slot number or -1 on full/error
int      swap_out(uint32_t phys_addr);

// Read a previously swapped page back into a 4K physical frame
int      swap_in(int slot, uint32_t phys_addr);

// Mark a slot as free without reading it (e.g. after process exit)
void     swap_free(int slot);

uint32_t swap_total_pages(void);
uint32_t swap_used_pages(void);
void     swap_print_info(void);

// Write a swap header to disk (called by mkswap tool or on first use)
int      swap_format(uint32_t start_lba, uint32_t num_pages, const char *label);

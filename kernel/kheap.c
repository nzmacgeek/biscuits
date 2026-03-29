// BlueyOS Kernel Heap - "There's always room for one more!" - Nana
// Block magic 0xB10EB10E = BLUE BLUE (loosely) - "Magic Xylophone" episode ref
// Bounds checked: no buffer overflows at this playdate!
#include "../include/types.h"
#include "../lib/stdio.h"
#include "kheap.h"

typedef struct block_hdr {
    uint32_t magic;
    uint32_t size;
    uint8_t  free;
    struct block_hdr *next;
} block_hdr_t;

static block_hdr_t *heap_start = NULL;
static uint32_t    heap_end   = 0;
static uint32_t    heap_size  = 0;

void kheap_init(uint32_t start, uint32_t size) {
    heap_start = (block_hdr_t*)start;
    heap_start->magic = BLUEY_HEAP_MAGIC;
    heap_start->size  = size - sizeof(block_hdr_t);
    heap_start->free  = 1;
    heap_start->next  = NULL;
    heap_end = start + size;
    heap_size = size - sizeof(block_hdr_t);
    kprintf("%s\n", "[HEP]  Kernel heap ready - plenty of room to play!");
}

void *kheap_alloc(size_t sz, int align) {
    if (!heap_start || sz == 0) return NULL;
    block_hdr_t *cur = heap_start;
    while (cur) {
        if (!cur->free) { cur = cur->next; continue; }

        uint32_t base        = (uint32_t)cur + sizeof(block_hdr_t);
        uint32_t alloc_start = base;
        uint32_t total_needed = sz;

        if (align) {
            uint32_t aligned = (base + 0xFFF) & ~0xFFF;
            uint32_t padding = aligned - base;
            total_needed = padding + sz;
            if (total_needed > cur->size) { cur = cur->next; continue; }
            alloc_start = aligned;
        } else {
            if (sz > cur->size) { cur = cur->next; continue; }
        }

        // Split block: place new header immediately after the allocated region.
        // alloc_start + sz = end of the allocated bytes = start of remaining free space.
        if (cur->size > total_needed + sizeof(block_hdr_t) + 16) {
            block_hdr_t *newblk = (block_hdr_t*)(alloc_start + sz);
            newblk->magic = BLUEY_HEAP_MAGIC;
            newblk->size  = cur->size - total_needed - sizeof(block_hdr_t);
            newblk->free  = 1;
            newblk->next  = cur->next;
            cur->next = newblk;
            cur->size = total_needed;
        }
        cur->free = 0;
        return (void*)alloc_start;
    }
    return NULL;
}

void kheap_free(void *ptr) {
    if (!ptr) return;
    block_hdr_t *blk = (block_hdr_t*)((uint32_t)ptr - sizeof(block_hdr_t));
    if (blk->magic != BLUEY_HEAP_MAGIC) {
        kprintf("[HEP] WARN: heap magic mismatch! Heap corruption?\n");
        return;
    }
    blk->free = 1;
    // Coalesce adjacent free blocks
    block_hdr_t *cur = heap_start;
    while (cur && cur->next) {
        if (cur->free && cur->next->free) {
            cur->size += sizeof(block_hdr_t) + cur->next->size;
            cur->next  = cur->next->next;
        } else cur = cur->next;
    }
}

uint32_t kheap_total_bytes(void) {
    return heap_size;
}

uint32_t kheap_free_bytes(void) {
    uint32_t free_bytes = 0;
    block_hdr_t *cur = heap_start;
    while (cur) {
        if (cur->free) free_bytes += cur->size;
        cur = cur->next;
    }
    return free_bytes;
}

uint32_t kheap_used_bytes(void) {
    uint32_t total = kheap_total_bytes();
    uint32_t free  = kheap_free_bytes();
    return (free <= total) ? (total - free) : 0;
}

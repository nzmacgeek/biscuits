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

void kheap_init(uint32_t start, uint32_t size) {
    heap_start = (block_hdr_t*)start;
    heap_start->magic = BLUEY_HEAP_MAGIC;
    heap_start->size  = size - sizeof(block_hdr_t);
    heap_start->free  = 1;
    heap_start->next  = NULL;
    heap_end = start + size;
    kprintf("%s\n", "[HEP]  Kernel heap ready - plenty of room to play!");
}

void *kheap_alloc(size_t sz, int align) {
    if (!heap_start || sz == 0) return NULL;
    block_hdr_t *cur = heap_start;
    while (cur) {
        if (!cur->free) { cur = cur->next; continue; }

        uint32_t raw_start = (uint32_t)cur;
        uint32_t payload_start = raw_start + sizeof(block_hdr_t);
        uint32_t alloc_hdr_addr = raw_start;
        uint32_t alloc_start = payload_start;
        uint32_t prefix_bytes = 0;
        uint32_t total_bytes = sizeof(block_hdr_t) + cur->size;
        uint32_t used_bytes;
        uint32_t suffix_bytes;
        uint32_t alloc_size = (uint32_t)sz;
        block_hdr_t *next = cur->next;
        block_hdr_t *alloc_blk;

        if (align) {
            alloc_start = (payload_start + 0xFFFu) & ~0xFFFu;
            alloc_hdr_addr = alloc_start - sizeof(block_hdr_t);
            prefix_bytes = alloc_hdr_addr - raw_start;

            if (prefix_bytes != 0 && prefix_bytes < sizeof(block_hdr_t) + 16u) {
                alloc_start = (alloc_start + 0x1000u) & ~0xFFFu;
                alloc_hdr_addr = alloc_start - sizeof(block_hdr_t);
                prefix_bytes = alloc_hdr_addr - raw_start;
            }
        }

        used_bytes = (alloc_start + alloc_size) - raw_start;
        if (used_bytes > total_bytes) {
            cur = cur->next;
            continue;
        }

        suffix_bytes = total_bytes - used_bytes;

        if (prefix_bytes >= sizeof(block_hdr_t) + 16u) {
            block_hdr_t *prefix_blk = cur;
            alloc_blk = (block_hdr_t*)alloc_hdr_addr;

            prefix_blk->magic = BLUEY_HEAP_MAGIC;
            prefix_blk->size = prefix_bytes - sizeof(block_hdr_t);
            prefix_blk->free = 1;
            prefix_blk->next = alloc_blk;
        } else {
            alloc_blk = cur;
            alloc_hdr_addr = raw_start;
            alloc_start = payload_start;
            prefix_bytes = 0;
        }

        alloc_blk->magic = BLUEY_HEAP_MAGIC;
        alloc_blk->free = 0;
        alloc_blk->size = alloc_size;

        if (suffix_bytes >= sizeof(block_hdr_t) + 16u) {
            block_hdr_t *suffix_blk = (block_hdr_t*)(alloc_start + alloc_size);
            suffix_blk->magic = BLUEY_HEAP_MAGIC;
            suffix_blk->size  = suffix_bytes - sizeof(block_hdr_t);
            suffix_blk->free  = 1;
            suffix_blk->next  = next;
            alloc_blk->next = suffix_blk;
        } else {
            alloc_blk->size += suffix_bytes;
            alloc_blk->next = next;
        }

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

void kheap_get_stats(uint32_t *total_bytes, uint32_t *used_bytes, uint32_t *free_bytes) {
    uint32_t total = 0;
    uint32_t used  = 0;
    uint32_t free  = 0;

    for (block_hdr_t *cur = heap_start; cur; cur = cur->next) {
        total += cur->size;
        if (cur->free) free += cur->size;
        else           used += cur->size;
    }

    if (total_bytes) *total_bytes = total;
    if (used_bytes)  *used_bytes  = used;
    if (free_bytes)  *free_bytes  = free;
}

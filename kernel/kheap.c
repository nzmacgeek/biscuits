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
        if (cur->free && cur->size >= sz) {
            uint32_t addr = (uint32_t)cur + sizeof(block_hdr_t);
            if (align) {
                uint32_t aligned = (addr + 0xFFF) & ~0xFFF;
                if (aligned + sz <= (uint32_t)cur + sizeof(block_hdr_t) + cur->size) {
                    addr = aligned;
                }
            }
            // Split block if large enough
            if (cur->size > sz + sizeof(block_hdr_t) + 16) {
                block_hdr_t *newblk = (block_hdr_t*)((uint32_t)cur + sizeof(block_hdr_t) + sz);
                newblk->magic = BLUEY_HEAP_MAGIC;
                newblk->size  = cur->size - sz - sizeof(block_hdr_t);
                newblk->free  = 1;
                newblk->next  = cur->next;
                cur->next = newblk;
                cur->size = sz;
            }
            cur->free = 0;
            return (void*)((uint32_t)cur + sizeof(block_hdr_t));
        }
        cur = cur->next;
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

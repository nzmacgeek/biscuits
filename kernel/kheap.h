#pragma once
#include "../include/types.h"
#define BLUEY_HEAP_MAGIC 0xB10EB10E
void  kheap_init(uint32_t start, uint32_t size);
void *kheap_alloc(size_t sz, int align);
void  kheap_free(void *ptr);

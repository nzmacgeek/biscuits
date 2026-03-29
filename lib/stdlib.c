// BlueyOS stdlib wrappers - delegate to kheap
#include "../include/types.h"
#include "../kernel/kheap.h"
void *kmalloc(size_t sz)   { return kheap_alloc(sz, 0); }
void *kmalloc_a(size_t sz) { return kheap_alloc(sz, 1); }
void  kfree(void *ptr)     { kheap_free(ptr); }

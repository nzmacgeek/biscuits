#pragma once
#include "../include/types.h"
void *kmalloc(size_t sz);
void *kmalloc_a(size_t sz);
void  kfree(void *ptr);

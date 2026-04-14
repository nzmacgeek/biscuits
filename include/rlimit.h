#pragma once
// Resource limit definitions (minimal)
#include "types.h"

typedef struct {
    uint32_t rlim_cur; /* soft limit */
    uint32_t rlim_max; /* hard limit */
} rlimit_t;

#define RLIMIT_STACK 3
#define RLIMIT_NOFILE 7

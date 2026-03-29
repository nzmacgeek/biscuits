#pragma once

#include "../../include/types.h"

typedef struct {
    uint32_t machine_type;
    uint32_t cpu_type;
    uint32_t fpu_type;
    uint32_t mmu_type;
    uint32_t mem_chunk_addr;
    uint32_t mem_chunk_size;
    uint32_t mac_model;
    uint32_t mac_video_base;
    uint32_t mac_video_depth;
    uint32_t mac_video_row;
    uint32_t mac_video_dim;
    uint32_t mac_video_logical;
    uint32_t mac_scc_base;
    uint32_t mac_memsize_mb;
    int valid;
} m68k_bootinfo_t;

const m68k_bootinfo_t *m68k_bootinfo_get(void);
int m68k_bootinfo_present(void);
uint32_t m68k_bootinfo_video_width(void);
uint32_t m68k_bootinfo_video_height(void);
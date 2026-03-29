#include "bootinfo.h"

#include "../../include/types.h"

#define BI_LAST          0x0000
#define BI_MACHTYPE      0x0001
#define BI_CPUTYPE       0x0002
#define BI_FPUTYPE       0x0003
#define BI_MMUTYPE       0x0004
#define BI_MEMCHUNK      0x0005

#define MACH_MAC         3

#define BI_MAC_MODEL     0x8000
#define BI_MAC_VADDR     0x8001
#define BI_MAC_VDEPTH    0x8002
#define BI_MAC_VROW      0x8003
#define BI_MAC_VDIM      0x8004
#define BI_MAC_VLOGICAL  0x8005
#define BI_MAC_SCCBASE   0x8006
#define BI_MAC_MEMSIZE   0x8009

typedef struct {
    uint16_t tag;
    uint16_t size;
    uint32_t data[];
} bi_record_t;

extern char kernel_end;

static m68k_bootinfo_t bootinfo;
static int bootinfo_parsed;

static const bi_record_t *m68k_bootinfo_start(void) {
    uintptr_t ptr = ((uintptr_t)&kernel_end + 3u) & ~((uintptr_t)3u);
    return (const bi_record_t *)ptr;
}

static int m68k_bootinfo_size_valid(uint16_t size) {
    return size >= sizeof(bi_record_t) && size <= 4096 && (size & 1u) == 0;
}

static int m68k_bootinfo_has_useful_mac_data(void) {
    return bootinfo.machine_type == MACH_MAC ||
           bootinfo.mac_model != 0 ||
           bootinfo.mac_scc_base != 0 ||
           bootinfo.mac_video_base != 0;
}

static void m68k_bootinfo_parse(void) {
    const bi_record_t *record;
    int seen_records = 0;

    if (bootinfo_parsed) {
        return;
    }

    bootinfo_parsed = 1;
    record = m68k_bootinfo_start();

    for (int record_index = 0; record_index < 128; record_index++) {
        uint16_t tag = record->tag;
        uint16_t size = record->size;
        const uint32_t *data = record->data;

        if (tag == BI_LAST) {
            bootinfo.valid = seen_records > 0 && m68k_bootinfo_has_useful_mac_data();
            return;
        }

        if (!m68k_bootinfo_size_valid(size)) {
            return;
        }

        seen_records = 1;

        switch (tag) {
            case BI_MACHTYPE:
                bootinfo.machine_type = data[0];
                break;
            case BI_CPUTYPE:
                bootinfo.cpu_type = data[0];
                break;
            case BI_FPUTYPE:
                bootinfo.fpu_type = data[0];
                break;
            case BI_MMUTYPE:
                bootinfo.mmu_type = data[0];
                break;
            case BI_MEMCHUNK:
                if (bootinfo.mem_chunk_size == 0) {
                    bootinfo.mem_chunk_addr = data[0];
                    bootinfo.mem_chunk_size = data[1];
                }
                break;
            case BI_MAC_MODEL:
                bootinfo.mac_model = data[0];
                break;
            case BI_MAC_VADDR:
                bootinfo.mac_video_base = data[0];
                break;
            case BI_MAC_VDEPTH:
                bootinfo.mac_video_depth = data[0];
                break;
            case BI_MAC_VROW:
                bootinfo.mac_video_row = data[0];
                break;
            case BI_MAC_VDIM:
                bootinfo.mac_video_dim = data[0];
                break;
            case BI_MAC_VLOGICAL:
                bootinfo.mac_video_logical = data[0];
                break;
            case BI_MAC_SCCBASE:
                bootinfo.mac_scc_base = data[0];
                break;
            case BI_MAC_MEMSIZE:
                bootinfo.mac_memsize_mb = data[0];
                break;
            default:
                break;
        }

        record = (const bi_record_t *)((const uint8_t *)record + size);
    }
}

const m68k_bootinfo_t *m68k_bootinfo_get(void) {
    m68k_bootinfo_parse();
    return &bootinfo;
}

int m68k_bootinfo_present(void) {
    return m68k_bootinfo_get()->valid;
}

uint32_t m68k_bootinfo_video_width(void) {
    return m68k_bootinfo_get()->mac_video_dim & 0xFFFFu;
}

uint32_t m68k_bootinfo_video_height(void) {
    return m68k_bootinfo_get()->mac_video_dim >> 16;
}
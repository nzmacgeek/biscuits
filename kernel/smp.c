// BlueyOS SMP topology discovery - "We've got a whole team here!" - Bluey
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "smp.h"

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t config_table;
    uint8_t  length;
    uint8_t  spec_rev;
    uint8_t  checksum;
    uint8_t  feature1;
    uint8_t  feature2;
    uint8_t  feature3;
    uint8_t  feature4;
    uint8_t  feature5;
} mp_floating_ptr_t;

typedef struct __attribute__((packed)) {
    char     signature[4];
    uint16_t base_table_length;
    uint8_t  spec_rev;
    uint8_t  checksum;
    char     oem_id[8];
    char     product_id[12];
    uint32_t oem_table_ptr;
    uint16_t oem_table_size;
    uint16_t entry_count;
    uint32_t lapic_addr;
    uint16_t extended_table_length;
    uint8_t  extended_table_checksum;
    uint8_t  reserved;
} mp_config_table_t;

typedef struct __attribute__((packed)) {
    uint8_t  entry_type;
    uint8_t  local_apic_id;
    uint8_t  local_apic_version;
    uint8_t  cpu_flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t reserved_low;
    uint32_t reserved_high;
} mp_processor_entry_t;

#define BIOS_BDA_EBDA_SEGMENT_ADDR 0x040Eu
#define BIOS_BDA_BASE_MEM_KB_ADDR  0x0413u

static smp_info_t smp_info = {
    .detected_cpus = 1,
    .booted_cpus   = 1,
    .topology_source = "bootstrap"
};

// Keep this out-of-line so GCC does not try to reason about the fixed BIOS
// address as if it were a normal C array access.
static __attribute__((noinline)) uint16_t smp_read_bios_word(uint32_t addr) {
    uint16_t value;
    __asm__ volatile("movw (%1), %0" : "=r"(value) : "r"(addr));
    return value;
}

static uint8_t smp_checksum_ok(const void *base, uint32_t length) {
    const uint8_t *bytes = (const uint8_t *)base;
    uint8_t sum = 0;

    for (uint32_t i = 0; i < length; i++) {
        sum = (uint8_t)(sum + bytes[i]);
    }
    return sum == 0;
}

static const mp_floating_ptr_t *smp_find_mp_floating(const uint8_t *base, uint32_t length) {
    for (uint32_t off = 0; off + sizeof(mp_floating_ptr_t) <= length; off += 16u) {
        const mp_floating_ptr_t *mp = (const mp_floating_ptr_t *)(base + off);
        if (memcmp(mp->signature, "_MP_", 4) != 0) continue;
        if (mp->length == 0) continue;
        if (!smp_checksum_ok(mp, (uint32_t)mp->length * 16u)) continue;
        return mp;
    }
    return NULL;
}

static const mp_floating_ptr_t *smp_locate_mp_floating(void) {
    uint16_t ebda_segment = smp_read_bios_word(BIOS_BDA_EBDA_SEGMENT_ADDR);
    uint16_t base_mem_kb  = smp_read_bios_word(BIOS_BDA_BASE_MEM_KB_ADDR);
    const mp_floating_ptr_t *mp;

    if (ebda_segment != 0) {
        mp = smp_find_mp_floating((const uint8_t *)((uint32_t)ebda_segment << 4), 1024u);
        if (mp) return mp;
    }

    if (base_mem_kb >= 1) {
        uint32_t top_of_base_mem = (uint32_t)base_mem_kb * 1024u;
        mp = smp_find_mp_floating((const uint8_t *)(top_of_base_mem - 1024u), 1024u);
        if (mp) return mp;
    }

    return smp_find_mp_floating((const uint8_t *)0xF0000u, 0x10000u);
}

static uint32_t smp_count_mp_cpus(const mp_config_table_t *config) {
    const uint8_t *entry = (const uint8_t *)(config + 1);
    const uint8_t *end = ((const uint8_t *)config) + config->base_table_length;
    uint32_t count = 0;

    while (entry < end) {
        switch (entry[0]) {
            case 0: {
                const mp_processor_entry_t *cpu = (const mp_processor_entry_t *)entry;
                if (cpu->cpu_flags & 0x01u) count++;
                entry += sizeof(mp_processor_entry_t);
                break;
            }
            case 1:
            case 2:
            case 3:
            case 4:
                entry += 8u;
                break;
            default:
                return count;
        }
    }

    return count;
}

void smp_init(void) {
    const mp_floating_ptr_t *mp;

    memset(&smp_info, 0, sizeof(smp_info));
    smp_info.detected_cpus = 1;
    smp_info.booted_cpus = 1;
    strcpy(smp_info.topology_source, "bootstrap");

    mp = smp_locate_mp_floating();
    if (mp) {
        smp_info.mp_table_present = 1;
        smp_info.apic_supported = 1;

        if (mp->config_table != 0) {
            const mp_config_table_t *config = (const mp_config_table_t *)mp->config_table;
            if (memcmp(config->signature, "PCMP", 4) == 0 &&
                smp_checksum_ok(config, config->base_table_length)) {
                uint32_t cpu_count = smp_count_mp_cpus(config);
                if (cpu_count > 0) smp_info.detected_cpus = cpu_count;
                strcpy(smp_info.topology_source, "mp-table");
            }
        } else if (mp->feature1 >= 1 && mp->feature1 <= 7) {
            smp_info.detected_cpus = (mp->feature1 == 1) ? 1u : 2u;
            strcpy(smp_info.topology_source, "mp-default");
        }
    }

    if (smp_info.detected_cpus > 1) {
        kprintf("[SMP]  Detected %u CPUs via %s - bootstrap CPU online\n",
                smp_info.detected_cpus, smp_info.topology_source);
    } else {
        kprintf("[SMP]  Single CPU online (%s)\n", smp_info.topology_source);
    }
}

const smp_info_t *smp_get_info(void) {
    return &smp_info;
}

void smp_print_info(void) {
    kprintf("Topology source   : %s\n", smp_info.topology_source);
    kprintf("Detected CPUs     : %u\n", smp_info.detected_cpus);
    kprintf("Online CPUs       : %u\n", smp_info.booted_cpus);
    kprintf("Local APIC        : %s\n", smp_info.apic_supported ? "yes" : "no");
    kprintf("MP table          : %s\n", smp_info.mp_table_present ? "present" : "not found");
    if (smp_info.detected_cpus > smp_info.booted_cpus) {
        kprintf("Mode              : bootstrap CPU online (secondary CPU startup pending)\n");
    } else {
        kprintf("Mode              : single-core bootstrap mode\n");
    }
}

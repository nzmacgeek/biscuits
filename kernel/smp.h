// BlueyOS SMP topology discovery - "We've got a whole team here!" - Bluey
#pragma once
#include "../include/types.h"

typedef struct {
    uint8_t  mp_table_present;
    uint8_t  apic_supported;
    uint32_t detected_cpus;
    uint32_t booted_cpus;
    char     topology_source[16];
} smp_info_t;

void smp_init(void);
const smp_info_t *smp_get_info(void);
void smp_print_info(void);

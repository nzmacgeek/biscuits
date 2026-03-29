#pragma once
#include "../include/types.h"

typedef struct {
    uint8_t  cpuid_supported;
    uint8_t  apic_supported;
    uint8_t  x2apic_supported;
    uint8_t  htt_supported;
    uint32_t detected_cpus;
    uint32_t booted_cpus;
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    char     vendor[13];
} smp_info_t;

void              smp_init(void);
const smp_info_t *smp_get_info(void);
void              smp_print_info(void);

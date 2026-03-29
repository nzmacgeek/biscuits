// BlueyOS SMP Discovery - "We've got a whole team here!" - Bluey
#include "../include/types.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "smp.h"

static smp_info_t smp_info = {
    .detected_cpus = 1,
    .booted_cpus   = 1,
    .vendor        = "unknown"
};

static uint8_t smp_has_cpuid(void) {
    uint32_t before, after, toggled;

    __asm__ volatile("pushfl; popl %0" : "=r"(before));
    toggled = before ^ (1u << 21);
    __asm__ volatile("pushl %0; popfl" : : "r"(toggled));
    __asm__ volatile("pushfl; popl %0" : "=r"(after));
    __asm__ volatile("pushl %0; popfl" : : "r"(before));

    return ((before ^ after) & (1u << 21)) != 0;
}

static void smp_cpuid(uint32_t leaf, uint32_t subleaf,
                      uint32_t *eax, uint32_t *ebx,
                      uint32_t *ecx, uint32_t *edx) {
    __asm__ volatile("cpuid"
                     : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
                     : "a"(leaf), "c"(subleaf));
}

void smp_init(void) {
    uint32_t eax, ebx, ecx, edx;
    uint32_t max_leaf;

    memset(&smp_info, 0, sizeof(smp_info));
    smp_info.detected_cpus = 1;
    smp_info.booted_cpus = 1;
    strcpy(smp_info.vendor, "unknown");

    if (!smp_has_cpuid()) {
        kprintf("[SMP]  CPUID unavailable - assuming one bootstrap CPU\n");
        return;
    }

    smp_info.cpuid_supported = 1;
    smp_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    max_leaf = eax;
    memcpy(&smp_info.vendor[0], &ebx, sizeof(ebx));
    memcpy(&smp_info.vendor[4], &edx, sizeof(edx));
    memcpy(&smp_info.vendor[8], &ecx, sizeof(ecx));
    smp_info.vendor[12] = '\0';

    if (max_leaf >= 1) {
        uint32_t family, model, ext_family, ext_model;
        smp_cpuid(1, 0, &eax, &ebx, &ecx, &edx);

        smp_info.apic_supported = (edx & (1u << 9)) != 0;
        smp_info.htt_supported = (edx & (1u << 28)) != 0;
        smp_info.x2apic_supported = (ecx & (1u << 21)) != 0;
        smp_info.stepping = eax & 0xFu;

        family = (eax >> 8) & 0xFu;
        model = (eax >> 4) & 0xFu;
        ext_family = (eax >> 20) & 0xFFu;
        ext_model = (eax >> 16) & 0xFu;

        if (family == 0xFu) {
            family += ext_family;
        }
        if (family == 0x6u || family == 0xFu) {
            model |= (ext_model << 4);
        }

        smp_info.family = family;
        smp_info.model = model;

        if (smp_info.htt_supported) {
            uint32_t logical_cpus = (ebx >> 16) & 0xFFu;
            if (logical_cpus > 1) {
                smp_info.detected_cpus = logical_cpus;
            }
        }
    }

    if (smp_info.detected_cpus > 1) {
        kprintf("[SMP]  Detected %u logical CPUs (%s APIC) - bootstrap CPU online\n",
                smp_info.detected_cpus,
                smp_info.apic_supported ? "with" : "without");
    } else {
        kprintf("[SMP]  Single CPU detected (%s)\n", smp_info.vendor);
    }
}

const smp_info_t *smp_get_info(void) {
    return &smp_info;
}

void smp_print_info(void) {
    kprintf("Vendor            : %s\n", smp_info.vendor);
    if (!smp_info.cpuid_supported) {
        kprintf("CPUID             : unavailable\n");
    } else {
        kprintf("Family/Model/Step : %u/%u/%u\n",
                smp_info.family, smp_info.model, smp_info.stepping);
        kprintf("CPUID             : available\n");
    }
    kprintf("Detected CPUs     : %u\n", smp_info.detected_cpus);
    kprintf("Online CPUs       : %u\n", smp_info.booted_cpus);
    kprintf("Local APIC        : %s\n", smp_info.apic_supported ? "yes" : "no");
    kprintf("x2APIC            : %s\n", smp_info.x2apic_supported ? "yes" : "no");
    if (smp_info.detected_cpus > smp_info.booted_cpus) {
        kprintf("Mode              : bootstrap CPU online (secondary CPU startup pending)\n");
    } else {
        kprintf("Mode              : single-core bootstrap mode\n");
    }
}

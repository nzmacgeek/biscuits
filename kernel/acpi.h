#pragma once
// BlueyOS ACPI Support
// "Someone's gotta keep the lights on!" - Bandit Heeler
//
// Minimal ACPI table parsing for proper reboot/poweroff.
// acpi_init() must be called BEFORE paging_init() so all physical
// memory is accessible in flat mode without range restrictions.

#include "../include/types.h"

// ACPI Generic Address Structure (12 bytes, ACPI 2.0+)
typedef struct __attribute__((packed)) {
    uint8_t  address_space;  // 0=memory, 1=I/O
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} acpi_gas_t;

#define ACPI_GAS_MEMORY  0
#define ACPI_GAS_IO      1

// Root System Description Pointer (20 bytes for ACPI 1.0)
typedef struct __attribute__((packed)) {
    char     signature[8];   // "RSD PTR "
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;       // 0=ACPI 1.0, 2=ACPI 2.0+
    uint32_t rsdt_address;
    // ACPI 2.0+ extension:
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} acpi_rsdp_t;

// Standard System Description Table header (36 bytes)
typedef struct __attribute__((packed)) {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} acpi_sdt_hdr_t;

// Fixed ACPI Description Table — "FACP"
typedef struct __attribute__((packed)) {
    acpi_sdt_hdr_t hdr;              // offset   0 (36 bytes)
    uint32_t firmware_ctrl;          // offset  36
    uint32_t dsdt;                   // offset  40
    uint8_t  reserved0;             // offset  44
    uint8_t  preferred_pm_profile;  // offset  45
    uint16_t sci_int;                // offset  46
    uint32_t smi_cmd;                // offset  48
    uint8_t  acpi_enable;           // offset  52
    uint8_t  acpi_disable;          // offset  53
    uint8_t  s4bios_req;            // offset  54
    uint8_t  pstate_cnt;            // offset  55
    uint32_t pm1a_evt_blk;          // offset  56
    uint32_t pm1b_evt_blk;          // offset  60
    uint32_t pm1a_cnt_blk;          // offset  64
    uint32_t pm1b_cnt_blk;          // offset  68
    uint32_t pm2_cnt_blk;           // offset  72
    uint32_t pm_tmr_blk;            // offset  76
    uint32_t gpe0_blk;              // offset  80
    uint32_t gpe1_blk;              // offset  84
    uint8_t  pm1_evt_len;           // offset  88
    uint8_t  pm1_cnt_len;           // offset  89
    uint8_t  pm2_cnt_len;           // offset  90
    uint8_t  pm_tmr_len;            // offset  91
    uint8_t  gpe0_blk_len;          // offset  92
    uint8_t  gpe1_blk_len;          // offset  93
    uint8_t  gpe1_base;             // offset  94
    uint8_t  cst_cnt;               // offset  95
    uint16_t p_lvl2_lat;            // offset  96
    uint16_t p_lvl3_lat;            // offset  98
    uint16_t flush_size;            // offset 100
    uint16_t flush_stride;          // offset 102
    uint8_t  duty_offset;           // offset 104
    uint8_t  duty_width;            // offset 105
    uint8_t  day_alrm;              // offset 106
    uint8_t  mon_alrm;              // offset 107
    uint8_t  century;               // offset 108
    uint16_t iapc_boot_arch;        // offset 109
    uint8_t  reserved1;             // offset 111
    uint32_t flags;                  // offset 112 (4 bytes)
    acpi_gas_t reset_reg;            // offset 116 (12 bytes)
    uint8_t  reset_value;           // offset 128
    uint8_t  reserved2[3];          // offset 129
    // ACPI 2.0+ extended fields start at offset 132 (not used here)
} acpi_fadt_t;

// FADT flags: bit 10 = RESET_REG_SUP
#define FADT_FLAG_RESET_REG_SUP  (1u << 10)

void acpi_init(void);
int  acpi_available(void);
void acpi_reset(void);
void acpi_poweroff(void);

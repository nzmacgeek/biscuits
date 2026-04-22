// BlueyOS ACPI Support
// "Someone's gotta keep the lights on!" - Bandit Heeler
//
// Minimal ACPI table parsing: find RSDP, walk RSDT to FADT, extract
// the reset register and PM1 control ports for proper poweroff/reboot.
//
// IMPORTANT: acpi_init() must be called before paging_init().
// In flat protected mode all physical addresses are directly accessible,
// so ACPI tables at any location (including high RAM) can be read safely.
// After paging is on, only I/O-space reset/poweroff operations are used.

#include "acpi.h"
#include "../include/ports.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

// Memory-mapped reset register is only safe to write post-paging if
// the address falls within the kernel's 4 MB identity map.
#define ACPI_MMIO_SAFE_MAX  0x00400000u

static int      g_acpi_ok       = 0;
static int      g_has_reset_reg = 0;
static uint8_t  g_reset_space   = 0;    // ACPI_GAS_IO or ACPI_GAS_MEMORY
static uint32_t g_reset_addr    = 0;
static uint8_t  g_reset_value   = 0;
static uint16_t g_pm1a_cnt_port = 0;
static uint16_t g_pm1b_cnt_port = 0;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static uint8_t acpi_checksum(const uint8_t *p, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += p[i];
    return sum;
}

static acpi_rsdp_t *find_rsdp_in_range(uint32_t start, uint32_t end) {
    for (uint32_t addr = start; addr < end; addr += 16) {
        acpi_rsdp_t *r = (acpi_rsdp_t *)(uintptr_t)addr;
        if (memcmp(r->signature, "RSD PTR ", 8) == 0 &&
            acpi_checksum((const uint8_t *)r, 20) == 0)
            return r;
    }
    return NULL;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void acpi_init(void) {
    // Search Extended BIOS Data Area (first 1 KB)
    uint32_t ebda_seg = (uint32_t)(*(volatile uint16_t *)(uintptr_t)0x040Eu);
    uint32_t ebda_base = ebda_seg << 4;

    acpi_rsdp_t *rsdp = NULL;
    if (ebda_base >= 0x80000u && ebda_base < 0xA0000u)
        rsdp = find_rsdp_in_range(ebda_base, ebda_base + 1024u);

    // Search BIOS ROM area (0xE0000 – 0xFFFFF)
    if (!rsdp)
        rsdp = find_rsdp_in_range(0xE0000u, 0x100000u);

    if (!rsdp) {
        kprintf("[ACPI] RSDP not found — using legacy reset/poweroff paths\n");
        return;
    }
    kprintf("[ACPI] RSDP at 0x%x (rev=%d oem=%.6s)\n",
            (uint32_t)(uintptr_t)rsdp, rsdp->revision, rsdp->oem_id);

    uint32_t rsdt_addr = rsdp->rsdt_address;
    if (!rsdt_addr) {
        kprintf("[ACPI] RSDT address is zero — skipping table parse\n");
        g_acpi_ok = 1;
        return;
    }

    acpi_sdt_hdr_t *rsdt = (acpi_sdt_hdr_t *)(uintptr_t)rsdt_addr;
    if (memcmp(rsdt->signature, "RSDT", 4) != 0) {
        kprintf("[ACPI] RSDT signature invalid at 0x%x\n", rsdt_addr);
        g_acpi_ok = 1;
        return;
    }
    if (acpi_checksum((const uint8_t *)rsdt, rsdt->length) != 0) {
        kprintf("[ACPI] RSDT checksum bad — skipping\n");
        g_acpi_ok = 1;
        return;
    }

    // Walk RSDT: each entry is a uint32_t physical address
    uint32_t *entries = (uint32_t *)((uint8_t *)rsdt + sizeof(acpi_sdt_hdr_t));
    uint32_t  nentries = (rsdt->length - (uint32_t)sizeof(acpi_sdt_hdr_t)) / 4u;

    for (uint32_t i = 0; i < nentries; i++) {
        uint32_t entry_addr = entries[i];
        if (!entry_addr) continue;

        acpi_sdt_hdr_t *sdt = (acpi_sdt_hdr_t *)(uintptr_t)entry_addr;
        if (memcmp(sdt->signature, "FACP", 4) != 0) continue;

        kprintf("[ACPI] FADT at 0x%x (len=%u rev=%d)\n",
                entry_addr, sdt->length, sdt->revision);

        acpi_fadt_t *fadt = (acpi_fadt_t *)(uintptr_t)entry_addr;

        // PM1 control ports for poweroff (legacy I/O addresses in FADT)
        if (fadt->pm1a_cnt_blk) {
            g_pm1a_cnt_port = (uint16_t)fadt->pm1a_cnt_blk;
            kprintf("[ACPI] PM1a_CNT port=0x%x\n", g_pm1a_cnt_port);
        }
        if (fadt->pm1b_cnt_blk) {
            g_pm1b_cnt_port = (uint16_t)fadt->pm1b_cnt_blk;
            kprintf("[ACPI] PM1b_CNT port=0x%x\n", g_pm1b_cnt_port);
        }

        // Reset register: ACPI 2.0+, requires RESET_REG_SUP flag set
        if (sdt->revision >= 2 && sdt->length >= 129u &&
            (fadt->flags & FADT_FLAG_RESET_REG_SUP)) {
            acpi_gas_t *rreg = &fadt->reset_reg;
            uint32_t rraddr;
            memcpy(&rraddr, &rreg->address, 4);  // safe unaligned read of low 32 bits

            if (rreg->address_space == ACPI_GAS_IO && rraddr <= 0xFFFFu) {
                g_reset_space   = ACPI_GAS_IO;
                g_reset_addr    = rraddr;
                g_reset_value   = fadt->reset_value;
                g_has_reset_reg = 1;
                kprintf("[ACPI] Reset reg: I/O port 0x%x value=0x%x\n",
                        g_reset_addr, g_reset_value);
            } else if (rreg->address_space == ACPI_GAS_MEMORY &&
                       rraddr < ACPI_MMIO_SAFE_MAX) {
                g_reset_space   = ACPI_GAS_MEMORY;
                g_reset_addr    = rraddr;
                g_reset_value   = fadt->reset_value;
                g_has_reset_reg = 1;
                kprintf("[ACPI] Reset reg: MMIO 0x%x value=0x%x\n",
                        g_reset_addr, g_reset_value);
            } else {
                kprintf("[ACPI] Reset reg space=%d addr=0x%x — not usable post-paging\n",
                        rreg->address_space, rraddr);
            }
        }

        g_acpi_ok = 1;
        break;
    }

    if (!g_acpi_ok)
        kprintf("[ACPI] FADT not found in RSDT\n");
}

int acpi_available(void) {
    return g_acpi_ok;
}

// acpi_reset - CPU reset via ACPI reset register, then legacy KBC, then CF9.
void acpi_reset(void) {
    // 1. ACPI reset register (if found)
    if (g_has_reset_reg) {
        if (g_reset_space == ACPI_GAS_IO) {
            outb((uint16_t)g_reset_addr, g_reset_value);
        } else {
            // Memory-mapped: only safe within 4 MB identity map
            volatile uint8_t *p = (volatile uint8_t *)(uintptr_t)g_reset_addr;
            *p = g_reset_value;
        }
        // Brief spin to let the hardware respond before trying fallbacks
        for (volatile int i = 0; i < 100000; i++);
    }

    // 2. Legacy keyboard controller pulse (works on real PC and QEMU):
    //    wait for KBC input buffer empty (bit 1 of status port 0x64 = IBF),
    //    then write the CPU reset pulse command 0xFE to command port 0x64.
    {
        int timeout = 100000;
        while ((inb(0x64u) & 0x02u) && --timeout > 0);
        outb(0x64u, 0xFEu);
        for (volatile int i = 0; i < 100000; i++);
    }

    // 3. Port CF9 hard reset (PCI reset line)
    outb(0xCF9u, 0x06u);
}

// acpi_poweroff - ACPI S5 sleep (soft power off) via PM1_CNT registers.
//
// SLP_TYP encoding in PM1_CNT: bits [12:10], SLP_EN = bit 13.
// QEMU/SeaBIOS S5 SLP_TYP is typically 5 (0b101), giving value 0x3400
// when combined with SLP_EN.  Older QEMU used SLP_TYP=0 (value 0x2000).
// We try both, plus Bochs's PM port at 0xB004.
void acpi_poweroff(void) {
    // 1. Try via parsed PM1a_CNT port (read-modify-write to preserve other bits)
    if (g_pm1a_cnt_port) {
        uint16_t val = inw(g_pm1a_cnt_port);
        val &= ~(uint16_t)0x1C00u;                           // clear SLP_TYP [12:10]
        val |= (uint16_t)((5u << 10) | (1u << 13));          // SLP_TYP=5, SLP_EN
        outw(g_pm1a_cnt_port, val);
        if (g_pm1b_cnt_port) {
            val = inw(g_pm1b_cnt_port);
            val &= ~(uint16_t)0x1C00u;
            val |= (uint16_t)((5u << 10) | (1u << 13));
            outw(g_pm1b_cnt_port, val);
        }
        for (volatile int i = 0; i < 100000; i++);
    }

    // 2. Hardcoded QEMU/Bochs/SeaBIOS fallback ports (tried in order)
    outw(0x604u,  0x3400u);   // QEMU PIIX4 PM, SLP_TYP=5
    outw(0x604u,  0x2000u);   // QEMU PIIX4 PM, SLP_TYP=0 (older QEMU)
    outw(0xB004u, 0x2000u);   // Bochs
    outw(0x4004u, 0x3400u);   // SeaBIOS alternate
}

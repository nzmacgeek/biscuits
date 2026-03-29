#pragma once
// BlueyOS — iMac G4 "Sunflower" (PowerPC) Platform Definitions
// "It was the 80s!" said Bandit, but the sunflower iMac was *very* 2002.
//
// Target: PowerPC G4 (MPC7450 "Warthog") @ 700/800/1000 MHz
//         Apple iMac G4 (flat-panel, arm-mounted display) — codename "Q54"
//
// Hardware references:
//   - Apple Developer Note: iMac (Flat Panel) (January 2002)
//   - MPC7450 RISC Microprocessor Family Reference Manual (Freescale)
//   - Uninorth2/KeyLargo register descriptions (OpenFirmware device tree)
//   - "Inside Macintosh: PowerPC System Software" (Apple, 1994)
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------
#define IMAC_G4_CPU         "PowerPC G4 (MPC7450)"
#define IMAC_G4_CPU_MHZ_MIN 700
#define IMAC_G4_CPU_MHZ_MAX 1000
#define IMAC_G4_CACHE_L1_KB 32      /* 32 KB I-cache + 32 KB D-cache */
#define IMAC_G4_CACHE_L2_KB 256     /* 256 KB on-chip L2 (backside) */
#define IMAC_G4_ALTIVEC     1       /* AltiVec SIMD unit present */

// ---------------------------------------------------------------------------
// Physical Memory
// ---------------------------------------------------------------------------
// RAM starts at 0x00000000.  The iMac G4 shipped with 128 MB or 256 MB,
// with two SO-DIMM slots supporting up to 1 GB (512 MB per slot, PC133).
#define IMAC_G4_RAM_BASE        0x00000000UL
#define IMAC_G4_RAM_MIN         0x08000000UL   /*  128 MB */
#define IMAC_G4_RAM_MAX         0x40000000UL   /* 1024 MB */

// ---------------------------------------------------------------------------
// ROM / Open Firmware
// ---------------------------------------------------------------------------
// The G4 ROM contains Open Firmware (IEEE 1275-1994) which is responsible
// for hardware initialisation, device probing, and loading the bootloader.
// After OF hands control to our kernel via "load-base" we run from RAM.
#define IMAC_G4_ROM_BASE        0xFF800000UL   /* 4 MB ROM window */
#define IMAC_G4_ROM_SIZE        0x00400000UL

// Load address used by Open Firmware (BootX loads kernel here by default)
#define IMAC_G4_LOAD_BASE       0x00600000UL

// ---------------------------------------------------------------------------
// North Bridge — UniNorth 2 (U3-Lite on later models)
// The UniNorth chip integrates: PCI bridge (3 buses), AGP, memory controller.
// Its configuration space is at 0xF0000000.
// ---------------------------------------------------------------------------
#define UNINORTH2_CFG_BASE      0xF0000000UL

// PCI buses behind UniNorth 2
#define UNINORTH2_PCI0_BASE     0xF2000000UL   /* AGP bus */
#define UNINORTH2_PCI1_BASE     0xF4000000UL   /* PCI bus 1 (internal devices) */

// ---------------------------------------------------------------------------
// South Bridge — KeyLargo I/O ASIC
// KeyLargo handles: ATA, USB (2 ports), FireWire (400 Mbps), ADB (over USB
// on G4 iMac), I2S audio, GPIO, and the PMU interface.
// KeyLargo is on PCI bus 1.  Its MMIO base is in the OF device tree.
// ---------------------------------------------------------------------------
#define KEYLARGO_BASE           0x80000000UL   /* typical mapping by OF */
#define KEYLARGO_SIZE           0x00100000UL

// KeyLargo internal register offsets (from KEYLARGO_BASE)
#define KL_FCRBASE              0x00038000UL   /* Feature Control Registers */
#define KL_FCR0                 (KEYLARGO_BASE + KL_FCRBASE + 0x00)
#define KL_FCR1                 (KEYLARGO_BASE + KL_FCRBASE + 0x04)
#define KL_FCR2                 (KEYLARGO_BASE + KL_FCRBASE + 0x08)
#define KL_FCR3                 (KEYLARGO_BASE + KL_FCRBASE + 0x0C)
#define KL_FCR4                 (KEYLARGO_BASE + KL_FCRBASE + 0x10)
#define KL_FCR5                 (KEYLARGO_BASE + KL_FCRBASE + 0x14)

// FCR0 bit: ATA/IDE bus enable
#define KL_FCR0_ATAENA          (1 << 0)
// FCR0 bit: USB controller enable
#define KL_FCR0_USBENA          (1 << 28)

// GPIO controller (KeyLargo)
#define KL_GPIO_BASE            (KEYLARGO_BASE + 0x0000A000UL)
#define KL_GPIO(n)              (*(volatile uint8_t *)(KL_GPIO_BASE + (n)))

// ---------------------------------------------------------------------------
// ATA / IDE — Primary channel (hard drive)
// The iMac G4 has a single internal ATA-100 channel (Kauai ATA).
// On the iMac G4 flat panel (2002), ATA is implemented inside KeyLargo.
// OF maps it at a bus-dependent address; we use the typical Linux value.
// ---------------------------------------------------------------------------
#define IMAC_G4_ATA_BASE        0x80038000UL   /* typical OF mapping */
#define ATA_DATA                (*(volatile uint16_t *)(IMAC_G4_ATA_BASE + 0x00))
#define ATA_ERROR               (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x02))
#define ATA_FEATURES            (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x02))
#define ATA_SECTOR_COUNT        (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x04))
#define ATA_LBA_LOW             (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x06))
#define ATA_LBA_MID             (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x08))
#define ATA_LBA_HIGH            (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x0A))
#define ATA_DEVICE              (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x0C))
#define ATA_STATUS              (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x0E))
#define ATA_COMMAND             (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x0E))
#define ATA_ALT_STATUS          (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x16))
#define ATA_DEV_CTRL            (*(volatile uint8_t  *)(IMAC_G4_ATA_BASE + 0x16))

// ATA status bits
#define ATA_SR_BSY              0x80
#define ATA_SR_DRDY             0x40
#define ATA_SR_DRQ              0x08
#define ATA_SR_ERR              0x01

// ---------------------------------------------------------------------------
// Serial — Z85C30 SCC (KeyLargo implements this for the modem port)
// The iMac G4 has an internal 56K modem; no external serial DE-9 port.
// For development output we use the "serial" debug port exposed by OF:
//   NVRAM:   "serial-debug=1"  (set in OF with: setenv serial-debug 1)
// When enabled, OF and the bootloader output to the internal UART at
// 57600,8N1.  Our kernel can use the same path for early boot messages.
// ---------------------------------------------------------------------------
#define IMAC_G4_SCC_BASE        0x80013000UL   /* KeyLargo SCC MMIO */
#define SCC_A_CTRL              (*(volatile uint8_t *)(IMAC_G4_SCC_BASE + 0x00))
#define SCC_A_DATA              (*(volatile uint8_t *)(IMAC_G4_SCC_BASE + 0x04))

// ---------------------------------------------------------------------------
// Video — ATI Radeon Mobility (7500 or 9200, model-dependent)
// The G4 iMac uses an AGP ATI Radeon for the flat-panel LCD.
// Frame buffer address is provided by Open Firmware in the device tree.
// We use the typical value; the real driver should read "assigned-addresses"
// from the OF node for /pci/AGP/ATY,RV200.
// ---------------------------------------------------------------------------
#define IMAC_G4_FB_BASE         0x90000000UL   /* typical ATI MMIO base */
#define IMAC_G4_FB_SIZE         0x04000000UL   /* 64 MB aperture */

// ---------------------------------------------------------------------------
// Power Management — PMU (Power Management Unit, 68HC11-based)
// The PMU is connected to KeyLargo over I2C; it handles:
//   - Sleep/wake
//   - Battery / DC power sense
//   - ADB (keyboard, mouse) via USB-to-ADB bridge on G4 iMac
//   - Real-time clock (RTC)
// ---------------------------------------------------------------------------
#define PMU_I2C_ADDR            0xD0    /* 7-bit: 0x68 << 1 */

// ---------------------------------------------------------------------------
// Interrupt controller — KeyLargo GPIO interrupt dispatcher
// PowerPC uses the external interrupt line (INTR) from KeyLargo.
// KeyLargo presents an 8-level priority interrupt controller to the PPC core.
// ---------------------------------------------------------------------------
#define KL_INT_BASE             (KEYLARGO_BASE + 0x00020000UL)
#define KL_INT_EVENTS           (*(volatile uint32_t *)(KL_INT_BASE + 0x00))
#define KL_INT_ENABLE           (*(volatile uint32_t *)(KL_INT_BASE + 0x04))
#define KL_INT_LEVEL            (*(volatile uint32_t *)(KL_INT_BASE + 0x08))

// KeyLargo interrupt sources
#define KL_INT_ATA              (1 << 0)
#define KL_INT_USB0             (1 << 1)
#define KL_INT_USB1             (1 << 2)
#define KL_INT_FIREWIRE         (1 << 3)
#define KL_INT_AUDIO            (1 << 4)
#define KL_INT_SCC_A            (1 << 5)
#define KL_INT_SCC_B            (1 << 6)

// ---------------------------------------------------------------------------
// TimeBase / Decrementer
// The PPC timebase runs at bus_frequency / 4.  On the iMac G4 the bus
// runs at 133 MHz, so timebase = 33.25 MHz.
// The decrementer generates an interrupt when it wraps below zero.
// ---------------------------------------------------------------------------
#define IMAC_G4_BUS_FREQ_HZ     133000000UL
#define IMAC_G4_TB_FREQ_HZ      (IMAC_G4_BUS_FREQ_HZ / 4)   /* ~33.25 MHz */
#define IMAC_G4_DECR_1MS        (IMAC_G4_TB_FREQ_HZ / 1000)  /* ticks per ms */

// PowerPC SPR numbers for timebase and decrementer
#define SPR_TBL         268   /* Time Base Lower (read) */
#define SPR_TBU         269   /* Time Base Upper (read) */
#define SPR_DEC         22    /* Decrementer */
#define SPR_HID0        1008  /* Hardware Implementation Dependent 0 */

// Read Special Purpose Register
#define mfspr(spr)      ({ uint32_t _v; \
                           __asm__ volatile("mfspr %0," #spr : "=r"(_v)); _v; })

// Write Special Purpose Register
#define mtspr(spr, val) __asm__ volatile("mtspr " #spr ",%0" :: "r"((uint32_t)(val)))

// Read/write Machine State Register
#define mfmsr()         ({ uint32_t _v; __asm__ volatile("mfmsr %0" : "=r"(_v)); _v; })
#define mtmsr(val)      __asm__ volatile("mtmsr %0" :: "r"((uint32_t)(val)))

// PowerPC MSR bits
#define MSR_EE          (1 << 15)   /* External interrupt enable */
#define MSR_PR          (1 << 14)   /* Problem state (user mode) */
#define MSR_FP          (1 << 13)   /* Floating point available */
#define MSR_ME          (1 << 12)   /* Machine check enable */
#define MSR_IR          (1 << 4)    /* Instruction address translation */
#define MSR_DR          (1 << 3)    /* Data address translation */

// ---------------------------------------------------------------------------
// Open Firmware helpers
// ---------------------------------------------------------------------------
// When our kernel is loaded by BootX (Apple's second-stage loader), it
// receives the Open Firmware device tree in memory.  The device tree root
// is passed as the first argument to our entry point.
// These constants describe the OF property names we read.
// ---------------------------------------------------------------------------
#define OF_PROP_MEMSIZE     "memory/reg"
#define OF_PROP_CPU_SPEED   "cpus/PowerPC,G4/@0/clock-frequency"
#define OF_PROP_BUS_SPEED   "cpus/PowerPC,G4/@0/bus-frequency"

// ---------------------------------------------------------------------------
// Useful macros
// ---------------------------------------------------------------------------
#define MB(n)    ((n) * 1024UL * 1024UL)
#define KB(n)    ((n) * 1024UL)

// Synchronise (eieio — Enforce In-order Execution of I/O) before MMIO reads
#define mmio_barrier()  __asm__ volatile("eieio" ::: "memory")
#define sync()          __asm__ volatile("sync"  ::: "memory")
#define isync()         __asm__ volatile("isync" ::: "memory")

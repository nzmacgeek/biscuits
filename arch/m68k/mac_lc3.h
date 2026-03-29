#pragma once
// BlueyOS - Macintosh LC III (M68K) Platform Definitions
// "It was the 80s!" — Bandit Heeler
//
// Target: Motorola 68030 @ 25 MHz, Macintosh LC III
// Memory map and hardware registers as documented in:
//   - Inside Macintosh: Devices
//   - Guide to Macintosh Family Hardware (2nd edition, Apple 1990)
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project
// with no affiliation to Ludo Studio or the BBC.

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------
#define MAC_LC3_CPU         "Motorola MC68030"
#define MAC_LC3_CPU_MHZ     25
#define MAC_LC3_FPU         "MC68882 (optional)"

// ---------------------------------------------------------------------------
// RAM
// ---------------------------------------------------------------------------
// LC III supports 4 MB built-in + two 72-pin SIMM slots (max 36 MB total).
// Physical RAM starts at address 0 in the 68030 address space.
#define MAC_LC3_RAM_BASE    0x00000000UL
#define MAC_LC3_RAM_BUILTIN 0x00400000UL   /* 4 MB built-in */
#define MAC_LC3_RAM_MAX     0x02400000UL   /* 36 MB maximum */

// ---------------------------------------------------------------------------
// ROM
// ---------------------------------------------------------------------------
// 512 KB ROM mapped to 0x40800000; also mirrored at 0x00400000 during boot.
#define MAC_LC3_ROM_BASE    0x40800000UL
#define MAC_LC3_ROM_SIZE    0x00080000UL   /* 512 KB */

// ---------------------------------------------------------------------------
// VIA1 — Versatile Interface Adapter (Synertek SY6522A)
// Handles keyboard, real-time clock, PRAM, interrupts, and sound.
// Base: 0x50000000, register stride: 0x200 bytes (A9 selects the register).
// ---------------------------------------------------------------------------
#define MAC_LC3_VIA1_BASE   0x50000000UL
#define MAC_LC3_VIA1_STRIDE 0x200

#define VIA1_REG(n)         (*(volatile uint8_t *)(MAC_LC3_VIA1_BASE + (n) * MAC_LC3_VIA1_STRIDE))

#define VIA1_ORB    VIA1_REG(0)   /* Output register B */
#define VIA1_ORA    VIA1_REG(1)   /* Output register A (with handshake) */
#define VIA1_DDRB   VIA1_REG(2)   /* Data direction register B */
#define VIA1_DDRA   VIA1_REG(3)   /* Data direction register A */
#define VIA1_T1CL   VIA1_REG(4)   /* Timer 1 counter low */
#define VIA1_T1CH   VIA1_REG(5)   /* Timer 1 counter high */
#define VIA1_T1LL   VIA1_REG(6)   /* Timer 1 latch low */
#define VIA1_T1LH   VIA1_REG(7)   /* Timer 1 latch high */
#define VIA1_T2CL   VIA1_REG(8)   /* Timer 2 counter low */
#define VIA1_T2CH   VIA1_REG(9)   /* Timer 2 counter high */
#define VIA1_SR     VIA1_REG(10)  /* Shift register (ADB data) */
#define VIA1_ACR    VIA1_REG(11)  /* Auxiliary control register */
#define VIA1_PCR    VIA1_REG(12)  /* Peripheral control register */
#define VIA1_IFR    VIA1_REG(13)  /* Interrupt flag register */
#define VIA1_IER    VIA1_REG(14)  /* Interrupt enable register */
#define VIA1_ORAH   VIA1_REG(15)  /* Output register A (no handshake) */

// VIA1 ORB bits
#define VIA1_ORB_SOUND_ENABLE   (1 << 7)  /* 0 = sound enabled */
#define VIA1_ORB_RTCCLK         (1 << 1)  /* RTC serial clock */
#define VIA1_ORB_RTCDATA        (1 << 0)  /* RTC serial data */
#define VIA1_ORB_RTCENB         (1 << 2)  /* 0 = RTC chip enabled */

// VIA1 IFR / IER bits
#define VIA1_INT_CA2            (1 << 0)  /* keyboard clock */
#define VIA1_INT_CA1            (1 << 1)  /* keyboard data */
#define VIA1_INT_SR             (1 << 2)  /* shift register */
#define VIA1_INT_CB2            (1 << 3)  /* (unused) */
#define VIA1_INT_CB1            (1 << 4)  /* (unused) */
#define VIA1_INT_T2             (1 << 5)  /* timer 2 */
#define VIA1_INT_T1             (1 << 6)  /* timer 1 */
#define VIA1_INT_SET            (1 << 7)  /* set bit in IER */

// ---------------------------------------------------------------------------
// VIA2 — Second VIA (handles NuBus, SCSI, Ethernet interrupts on LC III)
// Base: 0x50002000
// ---------------------------------------------------------------------------
#define MAC_LC3_VIA2_BASE   0x50002000UL
#define VIA2_REG(n)         (*(volatile uint8_t *)(MAC_LC3_VIA2_BASE + (n) * MAC_LC3_VIA1_STRIDE))

#define VIA2_IFR    VIA2_REG(13)
#define VIA2_IER    VIA2_REG(14)

#define VIA2_INT_SCSI_DRQ   (1 << 0)
#define VIA2_INT_SCSI_IRQ   (1 << 1)
#define VIA2_INT_NUBUS_9    (1 << 2)
#define VIA2_INT_NUBUS_A    (1 << 3)
#define VIA2_INT_NUBUS_B    (1 << 4)
#define VIA2_INT_NUBUS_C    (1 << 5)
#define VIA2_INT_NUBUS_D    (1 << 6)
#define VIA2_INT_NUBUS_E    (1 << 7)

// ---------------------------------------------------------------------------
// SCSI — NCR 5380 controller
// The LC III has an internal SCSI bus (for the hard drive and CD-ROM).
// Base: 0x50010000
// ---------------------------------------------------------------------------
#define MAC_LC3_SCSI_BASE   0x50010000UL
#define SCSI_REG(n)         (*(volatile uint8_t *)(MAC_LC3_SCSI_BASE + (n) * 0x200))

// NCR 5380 registers (read/write differ for some)
#define SCSI_OUTPUT_DATA        SCSI_REG(0)   /* W: output data */
#define SCSI_CURRENT_DATA       SCSI_REG(0)   /* R: current SCSI data bus */
#define SCSI_INITIATOR_CMD      SCSI_REG(1)
#define SCSI_MODE               SCSI_REG(2)
#define SCSI_TARGET_CMD         SCSI_REG(3)
#define SCSI_STATUS             SCSI_REG(4)   /* R: bus status */
#define SCSI_SELECT_ENABLE      SCSI_REG(4)   /* W: select enable */
#define SCSI_BUS_AND_STATUS     SCSI_REG(5)   /* R: bus and status */
#define SCSI_START_DMA          SCSI_REG(5)   /* W: start DMA */
#define SCSI_INPUT_DATA         SCSI_REG(6)   /* R: input data */
#define SCSI_RESET_PARITY_INT   SCSI_REG(7)   /* R: reset parity/interrupt */

// ---------------------------------------------------------------------------
// Serial (SCC) — Zilog Z8530 Dual Async/Sync Serial Controller
// Two channels: A (modem/printer) and B (printer/LocalTalk).
// Base: 0x50004000  (channel A control), 0x50004002 (channel B control)
// ---------------------------------------------------------------------------
#define MAC_LC3_SCC_BASE    0x50004000UL
#define SCC_CHAN_A_CTRL     (*(volatile uint8_t *)(MAC_LC3_SCC_BASE + 0))
#define SCC_CHAN_A_DATA     (*(volatile uint8_t *)(MAC_LC3_SCC_BASE + 4))
#define SCC_CHAN_B_CTRL     (*(volatile uint8_t *)(MAC_LC3_SCC_BASE + 2))
#define SCC_CHAN_B_DATA     (*(volatile uint8_t *)(MAC_LC3_SCC_BASE + 6))

// ---------------------------------------------------------------------------
// Sound / Video ASIC (DAFB) — Display Address and Frame Buffer
// The LC III uses a proprietary DAFB chip for video control.
// Base: 0x50024000
// ---------------------------------------------------------------------------
#define MAC_LC3_DAFB_BASE       0x50024000UL
#define DAFB_REG(off)           (*(volatile uint32_t *)(MAC_LC3_DAFB_BASE + (off)))

// Common DAFB register offsets (32-bit access)
#define DAFB_MODE_CTRL          DAFB_REG(0x00)  /* video mode control */
#define DAFB_VBL_COUNT          DAFB_REG(0x04)  /* vertical blanking counter */
#define DAFB_PIXEL_CLK          DAFB_REG(0x08)  /* pixel clock control */

// Frame buffer physical base (depends on RAM size and video RAM allocation).
// On LC III, video RAM is in main RAM starting at the top of physical memory.
// With 4 MB RAM and 512K video: frame buffer at 0x003C0000
#define MAC_LC3_FB_BASE_4MB     0x003C0000UL

// ---------------------------------------------------------------------------
// ADB — Apple Desktop Bus
// The LC III uses the VIA1 shift register for ADB communication.
// ADB is the interface for keyboard and mouse.
// Protocol is handled via VIA1_SR and associated interrupts.
// ---------------------------------------------------------------------------
#define ADB_STATE_FREE          0x00
#define ADB_STATE_EVEN          0x01
#define ADB_STATE_ODD           0x02
#define ADB_STATE_IDLE          0x03

// ---------------------------------------------------------------------------
// Interrupt levels (68030 IPL)
// ---------------------------------------------------------------------------
#define MAC_LC3_IPL_NUBUS   1   /* NuBus slots (via VIA2) */
#define MAC_LC3_IPL_VIA2    2   /* VIA2 */
#define MAC_LC3_IPL_VIA1    3   /* VIA1 (ADB, RTC, sound) */
#define MAC_LC3_IPL_SCC     4   /* SCC serial */
#define MAC_LC3_IPL_SCSI    5   /* SCSI */
#define MAC_LC3_IPL_VBL     6   /* vertical blanking */
#define MAC_LC3_IPL_NMI     7   /* NMI (programmer's button) */

// ---------------------------------------------------------------------------
// Boot ROM entrypoints (Toolbox ROM)
// These are the standard Macintosh low-memory globals and ROM vectors.
// See Inside Macintosh: Operating System Utilities for details.
// ---------------------------------------------------------------------------
#define MAC_ROM_BASE        0x40800000UL

// Key low-memory globals (physical addresses used during boot)
#define MAC_LMG_BOOTDRIVE   0x0210     /* BootDrive: which SCSI ID we booted from */
#define MAC_LMG_MEMSIZE     0x0108     /* Physical memory size (bytes) */
#define MAC_LMG_SP          0x0908     /* Stack pointer after ROM init */

// ---------------------------------------------------------------------------
// Useful macros
// ---------------------------------------------------------------------------
#define MB(n)   ((n) * 1024UL * 1024UL)
#define KB(n)   ((n) * 1024UL)

// Barrier to prevent compiler reordering of MMIO accesses
#define mmio_barrier()  __asm__ volatile("" ::: "memory")

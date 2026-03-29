#pragma once
// BlueyOS NE2000 Network Driver
// "Jack's Network Snorkel - diving into ethernet packets!"
// The NE2000 is a classic ISA NIC used in many older PCs and emulators.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"

// Common NE2000 probe addresses (ISA card)
#define NE2000_IO_BASE1   0x300
#define NE2000_IO_BASE2   0x320
#define NE2000_IO_BASE3   0x340
#define NE2000_IO_BASE4   0x360

// DP8390 register offsets (page 0)
#define NE_P0_CR    0x00   // Command Register
#define NE_P0_PSTART 0x01  // Page Start Register
#define NE_P0_PSTOP  0x02  // Page Stop Register
#define NE_P0_BNRY  0x03   // Boundary Pointer
#define NE_P0_TSR   0x04   // Transmit Status (read)
#define NE_P0_TPSR  0x04   // Transmit Page Start (write)
#define NE_P0_TBCR0 0x05   // Transmit Byte Count 0
#define NE_P0_TBCR1 0x06   // Transmit Byte Count 1
#define NE_P0_ISR   0x07   // Interrupt Status Register
#define NE_P0_CURR  0x07   // Current Page (page 1, read)
#define NE_P0_RSARH 0x08   // Remote Start Address High
#define NE_P0_RSAR0 0x08   // Remote Start Address 0
#define NE_P0_RSAR1 0x09   // Remote Start Address 1
#define NE_P0_RBCR0 0x0A   // Remote Byte Count 0
#define NE_P0_RBCR1 0x0B   // Remote Byte Count 1
#define NE_P0_RSR   0x0C   // Receive Status (read)
#define NE_P0_RCR   0x0C   // Receive Configuration Register (write)
#define NE_P0_TCR   0x0D   // Transmit Configuration Register
#define NE_P0_DCR   0x0E   // Data Configuration Register
#define NE_P0_IMR   0x0F   // Interrupt Mask Register
#define NE_DATAPORT 0x10   // NE2000 data port (16-bit)
#define NE_RESET    0x1F   // NE2000 reset port

// CR bits
#define CR_STOP  0x01
#define CR_START 0x02
#define CR_TXP   0x04
#define CR_RD0   0x08
#define CR_RD1   0x10
#define CR_RD2   0x20
#define CR_PS0   0x40
#define CR_PS1   0x80

void ne2000_init(void);
int  ne2000_send_packet(const uint8_t *data, uint16_t len);
int  ne2000_recv_packet(uint8_t *buf, uint16_t *len);
int  ne2000_ioctl(uint32_t cmd, void *arg);

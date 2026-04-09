#pragma once
// BlueyOS RTL8139 Network Driver - "Bingo's Fast Racer"
// Episode ref: "Dash Cam" - Bingo races through the living room
// The RTL8139 is a popular PCI NIC widely supported in QEMU and real hardware.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"

// RTL8139 PCI Vendor/Device IDs
#define RTL8139_VENDOR_ID  0x10EC
#define RTL8139_DEVICE_ID  0x8139

// RTL8139 Register offsets
#define RTL8139_REG_IDR0      0x00  // MAC address
#define RTL8139_REG_MAR0      0x08  // Multicast filter
#define RTL8139_REG_TXSTATUS0 0x10  // Transmit status (4 descriptors)
#define RTL8139_REG_TXADDR0   0x20  // Transmit buffer addresses
#define RTL8139_REG_RXBUF     0x30  // Receive buffer start address
#define RTL8139_REG_CMD       0x37  // Command register
#define RTL8139_REG_CAPR      0x38  // Current address of packet read
#define RTL8139_REG_CBR       0x3A  // Current buffer address
#define RTL8139_REG_IMR       0x3C  // Interrupt mask register
#define RTL8139_REG_ISR       0x3E  // Interrupt status register
#define RTL8139_REG_TCR       0x40  // Transmit configuration register
#define RTL8139_REG_RCR       0x44  // Receive configuration register
#define RTL8139_REG_CONFIG1   0x52  // Configuration register 1

// Command register bits
#define RTL8139_CMD_RESET     0x10
#define RTL8139_CMD_RX_ENABLE 0x08
#define RTL8139_CMD_TX_ENABLE 0x04
#define RTL8139_CMD_BUF_EMPTY 0x01

// Interrupt bits
#define RTL8139_INT_ROK       0x0001  // Receive OK
#define RTL8139_INT_TOK       0x0004  // Transmit OK
#define RTL8139_INT_RXOVW     0x0010  // Rx buffer overflow

// Transmit configuration
#define RTL8139_TCR_IFG_STD   0x03000000  // Standard inter-frame gap

// Receive configuration
#define RTL8139_RCR_AAP       0x00000001  // Accept all packets
#define RTL8139_RCR_APM       0x00000002  // Accept physical match
#define RTL8139_RCR_AM        0x00000004  // Accept multicast
#define RTL8139_RCR_AB        0x00000008  // Accept broadcast
#define RTL8139_RCR_WRAP      0x00000080  // Wrap at end of buffer
#define RTL8139_RCR_MXDMA     0x00000700  // Max DMA burst (unlimited)

void rtl8139_init(void);
int  rtl8139_send_packet(const uint8_t *data, uint16_t len);
int  rtl8139_recv_packet(uint8_t *buf, uint16_t *len);
int  rtl8139_ioctl(uint32_t cmd, void *arg);

#define MSG_RTL8139_INIT "[NET]  RTL8139 ready - Bingo's racer is zooming!"

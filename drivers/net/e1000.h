#pragma once
// BlueyOS Intel e1000 Network Driver - "Chilli's Express Lane"
// Episode ref: "Takeaway" - Fast and reliable network delivery
// The e1000 is Intel's Gigabit Ethernet controller widely supported in QEMU
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#include "../../include/types.h"

// e1000 PCI Device/Vendor IDs
#define E1000_VENDOR_ID         0x8086
#define E1000_DEVICE_ID_82540EM 0x100E
#define E1000_DEVICE_ID_82545EM 0x100F
#define E1000_DEVICE_ID_82574L  0x10D3

// Register offsets
#define E1000_REG_CTRL      0x00000  // Device Control
#define E1000_REG_STATUS    0x00008  // Device Status
#define E1000_REG_EECD      0x00010  // EEPROM Control
#define E1000_REG_EERD      0x00014  // EEPROM Read
#define E1000_REG_CTRL_EXT  0x00018  // Extended Device Control
#define E1000_REG_ICR       0x000C0  // Interrupt Cause Read
#define E1000_REG_IMS       0x000D0  // Interrupt Mask Set
#define E1000_REG_IMC       0x000D8  // Interrupt Mask Clear
#define E1000_REG_RCTL      0x00100  // Receive Control
#define E1000_REG_TCTL      0x00400  // Transmit Control
#define E1000_REG_TIPG      0x00410  // Transmit IPG
#define E1000_REG_RDBAL     0x02800  // RX Descriptor Base Low
#define E1000_REG_RDBAH     0x02804  // RX Descriptor Base High
#define E1000_REG_RDLEN     0x02808  // RX Descriptor Length
#define E1000_REG_RDH       0x02810  // RX Descriptor Head
#define E1000_REG_RDT       0x02818  // RX Descriptor Tail
#define E1000_REG_TDBAL     0x03800  // TX Descriptor Base Low
#define E1000_REG_TDBAH     0x03804  // TX Descriptor Base High
#define E1000_REG_TDLEN     0x03808  // TX Descriptor Length
#define E1000_REG_TDH       0x03810  // TX Descriptor Head
#define E1000_REG_TDT       0x03818  // TX Descriptor Tail
#define E1000_REG_MTA       0x05200  // Multicast Table Array
#define E1000_REG_RAL       0x05400  // Receive Address Low
#define E1000_REG_RAH       0x05404  // Receive Address High

// Control Register bits
#define E1000_CTRL_RST      (1 << 26)  // Device Reset
#define E1000_CTRL_SLU      (1 << 6)   // Set Link Up
#define E1000_CTRL_ASDE     (1 << 5)   // Auto-Speed Detection Enable

// Receive Control bits
#define E1000_RCTL_EN       (1 << 1)   // Receiver Enable
#define E1000_RCTL_SBP      (1 << 2)   // Store Bad Packets
#define E1000_RCTL_UPE      (1 << 3)   // Unicast Promiscuous Enable
#define E1000_RCTL_MPE      (1 << 4)   // Multicast Promiscuous Enable
#define E1000_RCTL_BAM      (1 << 15)  // Broadcast Accept Mode
#define E1000_RCTL_BSIZE_2K (0 << 16)  // Buffer Size 2048
#define E1000_RCTL_BSEX     (1 << 25)  // Buffer Size Extension
#define E1000_RCTL_SECRC    (1 << 26)  // Strip Ethernet CRC

// Transmit Control bits
#define E1000_TCTL_EN       (1 << 1)   // Transmit Enable
#define E1000_TCTL_PSP      (1 << 3)   // Pad Short Packets
#define E1000_TCTL_CT_SHIFT 4          // Collision Threshold shift
#define E1000_TCTL_COLD_SHIFT 12       // Collision Distance shift

// Descriptor command bits
#define E1000_TXD_CMD_EOP   (1 << 0)   // End of Packet
#define E1000_TXD_CMD_IFCS  (1 << 1)   // Insert FCS
#define E1000_TXD_CMD_RS    (1 << 3)   // Report Status
#define E1000_TXD_STAT_DD   (1 << 0)   // Descriptor Done

#define E1000_RXD_STAT_DD   (1 << 0)   // Descriptor Done
#define E1000_RXD_STAT_EOP  (1 << 1)   // End of Packet

// Descriptor counts
#define E1000_NUM_RX_DESC   32
#define E1000_NUM_TX_DESC   8

// MMIO region size (BAR0): 128KB covers all e1000 registers
#define E1000_MMIO_SIZE     0x20000

// Receive Descriptor
typedef struct {
    uint64_t buffer;   // Physical address of buffer
    uint16_t length;   // Length of data in buffer
    uint16_t checksum; // Packet checksum
    uint8_t  status;   // Descriptor status
    uint8_t  errors;   // Descriptor errors
    uint16_t special;  // VLAN info
} __attribute__((packed)) e1000_rx_desc_t;

// Transmit Descriptor
typedef struct {
    uint64_t buffer;   // Physical address of buffer
    uint16_t length;   // Length of data to transmit
    uint8_t  cso;      // Checksum offset
    uint8_t  cmd;      // Command field
    uint8_t  status;   // Status field
    uint8_t  css;      // Checksum start
    uint16_t special;  // VLAN info
} __attribute__((packed)) e1000_tx_desc_t;

// Driver API
void e1000_init(void);
int e1000_ioctl(uint32_t cmd, void *arg);

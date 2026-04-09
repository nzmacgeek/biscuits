#pragma once
// BlueyOS 3Com EtherLink III (3c509) Network Driver - "Chilli's Classic Car"
// Episode ref: "Grandad" - Chilli's old car that's reliable and classic
// The 3c509 is a classic ISA NIC commonly found in older hardware.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"

// 3c509 uses an ID port for activation (0x100-0x1F0)
#define EL3_ID_PORT      0x100

// 3c509 typical I/O base addresses (after activation)
#define EL3_IO_BASE1     0x300
#define EL3_IO_BASE2     0x310
#define EL3_IO_BASE3     0x320
#define EL3_IO_BASE4     0x330

// ID sequence for 3c509 activation
#define EL3_ID_SEQUENCE_START  0x00

// Window selection commands
#define EL3_CMD_SELECT_WINDOW  0x01

// Register offsets (depend on active window)
// Window 0 - Configuration
#define EL3_W0_ADDR_CFG    0x06
#define EL3_W0_RESOURCE    0x08
#define EL3_W0_EEPROM_CMD  0x0A
#define EL3_W0_EEPROM_DATA 0x0C

// Window 2 - Station address
#define EL3_W2_ADDR0       0x00  // MAC bytes 0-1
#define EL3_W2_ADDR1       0x02  // MAC bytes 2-3
#define EL3_W2_ADDR2       0x04  // MAC bytes 4-5

// Window 4 - Diagnostics
#define EL3_W4_MEDIA       0x0A

// Common registers (available in all windows)
#define EL3_COMMAND        0x0E
#define EL3_STATUS         0x0E
#define EL3_TX_FREE        0x0C
#define EL3_TX_STATUS      0x0B
#define EL3_DATA_PORT      0x00

// Commands (written to command register)
#define EL3_CMD_RESET           0x0000
#define EL3_CMD_SELECT_WIN      0x0800
#define EL3_CMD_RX_ENABLE       0x2000
#define EL3_CMD_RX_DISABLE      0x1800
#define EL3_CMD_TX_ENABLE       0x4800
#define EL3_CMD_TX_DISABLE      0x5000
#define EL3_CMD_STATS_ENABLE    0x7000
#define EL3_CMD_SET_TX_START    0x9800

// Status bits
#define EL3_STATUS_CMD_IN_PROGRESS  0x1000
#define EL3_STATUS_INT_LATCH        0x0001
#define EL3_STATUS_TX_COMPLETE      0x0004
#define EL3_STATUS_RX_COMPLETE      0x0010

// TX/RX packet sizes
#define EL3_MAX_PACKET_LEN  1518

void el3_init(void);
int  el3_send_packet(const uint8_t *data, uint16_t len);
int  el3_recv_packet(uint8_t *buf, uint16_t *len);
int  el3_ioctl(uint32_t cmd, void *arg);

#define MSG_EL3_INIT "[NET]  3Com EtherLink III ready - Chilli's classic is running!"

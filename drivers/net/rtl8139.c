// BlueyOS RTL8139 Network Driver - "Bingo's Fast Racer"
// Episode ref: "Dash Cam" - Bingo races through the living room
// The RTL8139 is a popular PCI NIC widely supported in QEMU and real hardware.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"
#include "../../include/ports.h"
#include "../../include/bluey.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "rtl8139.h"
#include "network.h"

static uint16_t rtl_base = 0;   // I/O base, 0 if no card found
static uint8_t  rtl_mac[6];
static int      rtl_tx_current = 0;

// For simplicity, we'll use statically allocated buffers
#define RTL8139_RX_BUF_SIZE  (8192 + 16 + 1500)
#define RTL8139_TX_BUF_SIZE  1536

static uint8_t rtl_rx_buf[RTL8139_RX_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t rtl_tx_bufs[4][RTL8139_TX_BUF_SIZE] __attribute__((aligned(4)));
static uint16_t rtl_rx_offset = 0;

static void rtl8139_write8(uint16_t reg, uint8_t val) { outb(rtl_base + reg, val); }
static void rtl8139_write16(uint16_t reg, uint16_t val) { outw(rtl_base + reg, val); }
static void rtl8139_write32(uint16_t reg, uint32_t val) { outl(rtl_base + reg, val); }
static uint8_t rtl8139_read8(uint16_t reg) { return inb(rtl_base + reg); }
static uint16_t rtl8139_read16(uint16_t reg) { return inw(rtl_base + reg); }
static uint32_t rtl8139_read32(uint16_t reg) { return inl(rtl_base + reg); }

// Note: This is a simplified driver without full PCI support
// In a real implementation, we'd scan the PCI bus for the device
static int rtl8139_probe(uint16_t base) {
    // Try common I/O addresses used in QEMU configurations
    rtl_base = base;

    // Software reset
    rtl8139_write8(RTL8139_REG_CMD, RTL8139_CMD_RESET);

    // Wait for reset to complete (bit should clear)
    for (int i = 0; i < 1000; i++) {
        if (!(rtl8139_read8(RTL8139_REG_CMD) & RTL8139_CMD_RESET)) {
            return 1;  // Reset successful
        }
        // Small delay
        for (volatile int j = 0; j < 1000; j++);
    }

    return 0;  // Reset timeout
}

static void rtl8139_read_mac(void) {
    for (int i = 0; i < 6; i++) {
        rtl_mac[i] = rtl8139_read8(RTL8139_REG_IDR0 + i);
    }
}

static int rtl8139_send(const uint8_t *data, uint16_t len) {
    if (!rtl_base) return -1;
    if (len > 1518) return -1;  // Ethernet frame too large
    if (len < 60) len = 60;     // Minimum Ethernet frame size

    // Use next TX descriptor (round-robin)
    int desc = rtl_tx_current;
    rtl_tx_current = (rtl_tx_current + 1) % 4;

    // Copy packet to TX buffer
    memcpy(rtl_tx_bufs[desc], data, len);

    // Set TX address (physical address - for simplicity we assume identity mapping)
    rtl8139_write32(RTL8139_REG_TXADDR0 + (desc * 4), (uint32_t)rtl_tx_bufs[desc]);

    // Set TX status - length and "OWN" bit (bit 13 cleared = owned by card)
    rtl8139_write32(RTL8139_REG_TXSTATUS0 + (desc * 4), len & 0x1FFF);

    return 0;
}

static uint16_t rtl8139_rx_read16(uint16_t offset) {
    uint16_t next = (offset + 1) % RTL8139_RX_BUF_SIZE;
    return (uint16_t)rtl_rx_buf[offset] | ((uint16_t)rtl_rx_buf[next] << 8);
}

static void rtl8139_rx_copy(uint8_t *dst, uint16_t offset, uint16_t len) {
    uint16_t first_chunk;

    if (len == 0) return;

    offset %= RTL8139_RX_BUF_SIZE;
    first_chunk = RTL8139_RX_BUF_SIZE - offset;
    if (first_chunk > len) first_chunk = len;

    memcpy(dst, rtl_rx_buf + offset, first_chunk);
    if (first_chunk < len) {
        memcpy(dst + first_chunk, rtl_rx_buf, len - first_chunk);
    }
}

static int rtl8139_recv(uint8_t *buf, uint16_t *len) {
    if (!rtl_base) return -1;

    // Check if buffer is empty
    uint8_t cmd = rtl8139_read8(RTL8139_REG_CMD);
    if (cmd & RTL8139_CMD_BUF_EMPTY) {
        *len = 0;
        return -1;
    }

    // Read packet header (4 bytes: status + length) from the RX ring safely
    uint16_t offset = rtl_rx_offset % RTL8139_RX_BUF_SIZE;
    uint16_t status    = rtl8139_rx_read16(offset);
    uint16_t raw_pkt_len = rtl8139_rx_read16((offset + 2) % RTL8139_RX_BUF_SIZE);

    // Reject obviously invalid lengths before using them
    if (raw_pkt_len == 0 || raw_pkt_len > (RTL8139_RX_BUF_SIZE - 4)) {
        *len = 0;
        return -1;
    }

    // Check status (bit 0 = ROK, receive OK)
    if (!(status & 0x0001)) {
        // Bad packet, skip it
        uint32_t next_offset = (uint32_t)((offset + raw_pkt_len + 4 + 3) & ~3);
        rtl_rx_offset = (uint16_t)(next_offset % RTL8139_RX_BUF_SIZE);
        rtl8139_write16(RTL8139_REG_CAPR, rtl_rx_offset - 16);
        *len = 0;
        return -1;
    }

    uint16_t pkt_len = raw_pkt_len;

    // Exclude CRC (last 4 bytes)
    if (pkt_len > 4) pkt_len -= 4;
    else pkt_len = 0;

    if (pkt_len > 1518) pkt_len = 1518;  // Sanity check

    // Copy packet data (skip 4-byte header), handling RX ring wraparound
    rtl8139_rx_copy(buf, (offset + 4) % RTL8139_RX_BUF_SIZE, pkt_len);
    *len = pkt_len;

    // Update read offset (4-byte aligned, include header)
    uint32_t next_offset = (uint32_t)((offset + raw_pkt_len + 4 + 3) & ~3);
    rtl_rx_offset = (uint16_t)(next_offset % RTL8139_RX_BUF_SIZE);

    // Update CAPR (Current Address of Packet Read) - must subtract 16
    rtl8139_write16(RTL8139_REG_CAPR, rtl_rx_offset - 16);

    return 0;
}

int rtl8139_ioctl(uint32_t cmd, void *arg) {
    if (cmd == 0 && arg) { *(uint32_t*)arg = rtl_base; return 0; }
    if (cmd == 1 && arg) { *(uint8_t*)arg  = rtl8139_read8(RTL8139_REG_CMD); return 0; }
    return -1;
}

static net_interface_t rtl8139_iface = {
    .name        = "",  // Will be assigned dynamically (eth0, eth1, etc.)
    .mac         = {0},
    .send        = rtl8139_send,
    .recv        = rtl8139_recv,
    .rx_packets  = 0,
    .tx_packets  = 0,
    .rx_errors   = 0,
    .tx_errors   = 0,
    .up          = 0,
};

void rtl8139_init(void) {
    // Try common I/O base addresses
    // Note: In a real driver, we'd scan PCI to find the actual base
    static const uint16_t probe_ports[] = {
        0xC000, 0xC100, 0xD000, 0xD100, 0
    };

    for (int i = 0; probe_ports[i]; i++) {
        if (rtl8139_probe(probe_ports[i])) {
            kprintf("[NET]  RTL8139 found at I/O 0x%x\n", rtl_base);

            // Read MAC address
            rtl8139_read_mac();
            kprintf("[NET]  MAC: %x:%x:%x:%x:%x:%x\n",
                    rtl_mac[0], rtl_mac[1], rtl_mac[2],
                    rtl_mac[3], rtl_mac[4], rtl_mac[5]);

            // Set up receive buffer
            rtl_rx_offset = 0;
            rtl8139_write32(RTL8139_REG_RXBUF, (uint32_t)rtl_rx_buf);

            // Set IMR (Interrupt Mask Register) - disable all interrupts for now
            rtl8139_write16(RTL8139_REG_IMR, 0x0000);

            // Set ISR (clear any pending interrupts)
            rtl8139_write16(RTL8139_REG_ISR, 0xFFFF);

            // Configure receive: accept broadcast + physical match, wrap, max DMA
            uint32_t rcr = RTL8139_RCR_AB | RTL8139_RCR_APM | RTL8139_RCR_WRAP |
                          (7 << 8);  // Max DMA burst
            rtl8139_write32(RTL8139_REG_RCR, rcr);

            // Configure transmit: standard IFG
            rtl8139_write32(RTL8139_REG_TCR, RTL8139_TCR_IFG_STD);

            // Enable transmitter and receiver
            rtl8139_write8(RTL8139_REG_CMD, RTL8139_CMD_RX_ENABLE | RTL8139_CMD_TX_ENABLE);

            // Register interface
            for (int j = 0; j < 6; j++) rtl8139_iface.mac[j] = rtl_mac[j];
            rtl8139_iface.up = 1;
            net_register_interface(&rtl8139_iface);
            kprintf("[NET]  Registered as %s\n", rtl8139_iface.name);
            kprintf("%s\n", MSG_RTL8139_INIT);
            return;
        }
    }

    kprintf("[NET]  RTL8139: No card found - Bingo's racer is in the garage!\n");
}

int rtl8139_send_packet(const uint8_t *data, uint16_t len) {
    return rtl8139_send(data, len);
}

int rtl8139_recv_packet(uint8_t *buf, uint16_t *len) {
    return rtl8139_recv(buf, len);
}

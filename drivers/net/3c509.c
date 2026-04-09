// BlueyOS 3Com EtherLink III (3c509) Network Driver - "Chilli's Classic Car"
// Episode ref: "Grandad" - Chilli's old car that's reliable and classic
// The 3c509 is a classic ISA NIC commonly found in older hardware.
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"
#include "../../include/ports.h"
#include "../../include/bluey.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "3c509.h"
#include "network.h"

static uint16_t el3_base = 0;   // I/O base, 0 if no card found
static uint8_t  el3_mac[6];

static void el3_write16(uint16_t reg, uint16_t val) { outw(el3_base + reg, val); }
static uint16_t el3_read16(uint16_t reg) { return inw(el3_base + reg); }

// Send ID sequence to activate 3c509
static void el3_send_id_sequence(void) {
    // Reset ID port
    outb(EL3_ID_PORT, 0);
    outb(EL3_ID_PORT, 0);

    // Send ID sequence (255 bits)
    uint8_t pattern = 0xFF;
    for (int i = 255; i >= 0; i--) {
        outb(EL3_ID_PORT, pattern);
        pattern <<= 1;
        if (pattern == 0) pattern = 1;
    }
}

// Select a window (0-7)
static void el3_select_window(int window) {
    el3_write16(EL3_COMMAND, EL3_CMD_SELECT_WIN | (window & 0x07));
}

// Wait for command to complete
static void el3_wait_cmd(void) {
    for (int i = 0; i < 10000; i++) {
        if (!(el3_read16(EL3_STATUS) & EL3_STATUS_CMD_IN_PROGRESS)) return;
    }
}

static int el3_probe(uint16_t base) {
    el3_base = base;

    // Activate card via ID port
    el3_send_id_sequence();

    // Try to activate this card at the given I/O address
    outb(EL3_ID_PORT, 0xFF);  // Tag register: select all cards
    outb(EL3_ID_PORT, (base >> 4) | 0xC0);  // Activate at this address

    // Small delay
    for (volatile int i = 0; i < 10000; i++);

    // Try to read from the card
    el3_select_window(0);
    uint16_t test = el3_read16(EL3_W0_RESOURCE);

    // If we read 0xFFFF, the card isn't there
    if (test == 0xFFFF) return 0;

    // Reset the card
    el3_write16(EL3_COMMAND, EL3_CMD_RESET);
    el3_wait_cmd();

    return 1;
}

static void el3_read_mac(void) {
    // Switch to window 2 (station address)
    el3_select_window(2);

    // Read MAC address (3 16-bit words)
    uint16_t w0 = el3_read16(EL3_W2_ADDR0);
    uint16_t w1 = el3_read16(EL3_W2_ADDR1);
    uint16_t w2 = el3_read16(EL3_W2_ADDR2);

    el3_mac[0] = w0 & 0xFF;
    el3_mac[1] = (w0 >> 8) & 0xFF;
    el3_mac[2] = w1 & 0xFF;
    el3_mac[3] = (w1 >> 8) & 0xFF;
    el3_mac[4] = w2 & 0xFF;
    el3_mac[5] = (w2 >> 8) & 0xFF;
}

static int el3_send(const uint8_t *data, uint16_t len) {
    if (!el3_base) return -1;
    if (len > EL3_MAX_PACKET_LEN) return -1;
    if (len < 60) len = 60;  // Minimum Ethernet frame size

    // Wait for TX to be available
    for (int i = 0; i < 1000; i++) {
        uint16_t free = el3_read16(EL3_TX_FREE);
        if (free >= len + 4) break;
        // Small delay
        for (volatile int j = 0; j < 100; j++);
    }

    // Write packet length
    el3_write16(EL3_DATA_PORT, len);
    el3_write16(EL3_DATA_PORT, 0);  // No special flags

    // Write packet data (16-bit at a time)
    for (uint16_t i = 0; i < len; i += 2) {
        uint16_t word;
        if (i + 1 < len) {
            word = data[i] | (data[i+1] << 8);
        } else {
            word = data[i];  // Odd byte at end
        }
        el3_write16(EL3_DATA_PORT, word);
    }

    return 0;
}

static int el3_recv(uint8_t *buf, uint16_t *len) {
    if (!el3_base) return -1;

    // Check if packet is available
    uint16_t status = el3_read16(EL3_STATUS);
    if (!(status & EL3_STATUS_RX_COMPLETE)) {
        *len = 0;
        return -1;
    }

    // Read RX status
    uint16_t rx_status = el3_read16(EL3_DATA_PORT);

    // Extract packet length (bits 0-10)
    uint16_t pkt_len = rx_status & 0x7FF;

    if (pkt_len > EL3_MAX_PACKET_LEN) pkt_len = EL3_MAX_PACKET_LEN;

    // Read packet data
    for (uint16_t i = 0; i < pkt_len; i += 2) {
        uint16_t word = el3_read16(EL3_DATA_PORT);
        buf[i] = word & 0xFF;
        if (i + 1 < pkt_len) buf[i+1] = (word >> 8) & 0xFF;
    }

    *len = pkt_len;

    // Discard packet
    el3_write16(EL3_COMMAND, 0x8000);  // RX_DISCARD

    return 0;
}

int el3_ioctl(uint32_t cmd, void *arg) {
    if (cmd == 0 && arg) { *(uint32_t*)arg = el3_base; return 0; }
    if (cmd == 1 && arg) { *(uint16_t*)arg = el3_read16(EL3_STATUS); return 0; }
    return -1;
}

static net_interface_t el3_iface = {
    .name        = "",  // Will be assigned dynamically (eth0, eth1, etc.)
    .mac         = {0},
    .send        = el3_send,
    .recv        = el3_recv,
    .rx_packets  = 0,
    .tx_packets  = 0,
    .rx_errors   = 0,
    .tx_errors   = 0,
    .up          = 0,
};

void el3_init(void) {
    // Try common I/O base addresses
    static const uint16_t probe_ports[] = {
        EL3_IO_BASE1, EL3_IO_BASE2, EL3_IO_BASE3, EL3_IO_BASE4, 0
    };

    for (int i = 0; probe_ports[i]; i++) {
        if (el3_probe(probe_ports[i])) {
            kprintf("[NET]  3Com EtherLink III found at I/O 0x%x\n", el3_base);

            // Read MAC address
            el3_read_mac();
            kprintf("[NET]  MAC: %x:%x:%x:%x:%x:%x\n",
                    el3_mac[0], el3_mac[1], el3_mac[2],
                    el3_mac[3], el3_mac[4], el3_mac[5]);

            // Configure card
            el3_select_window(4);
            el3_write16(EL3_W4_MEDIA, 0x00C0);  // Enable link beat, auto-select

            el3_select_window(0);

            // Enable stats, RX, and TX
            el3_write16(EL3_COMMAND, EL3_CMD_STATS_ENABLE);
            el3_write16(EL3_COMMAND, EL3_CMD_RX_ENABLE);
            el3_write16(EL3_COMMAND, EL3_CMD_TX_ENABLE);
            el3_write16(EL3_COMMAND, EL3_CMD_SET_TX_START | 2000);

            // Register interface
            for (int j = 0; j < 6; j++) el3_iface.mac[j] = el3_mac[j];
            el3_iface.up = 1;
            net_register_interface(&el3_iface);
            kprintf("[NET]  Registered as %s\n", el3_iface.name);
            kprintf("%s\n", MSG_EL3_INIT);
            return;
        }
    }

    kprintf("[NET]  3Com EtherLink III: No card found - Chilli's car is at the mechanic!\n");
}

int el3_send_packet(const uint8_t *data, uint16_t len) {
    return el3_send(data, len);
}

int el3_recv_packet(uint8_t *buf, uint16_t *len) {
    return el3_recv(buf, len);
}

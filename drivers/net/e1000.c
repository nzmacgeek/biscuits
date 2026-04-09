// BlueyOS Intel e1000 Network Driver - "Chilli's Express Lane"
// Episode ref: "Takeaway" - Fast and reliable network delivery
// The e1000 is Intel's Gigabit Ethernet controller widely supported in QEMU
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"
#include "../../include/ports.h"
#include "../../include/bluey.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "e1000.h"
#include "network.h"

static uint32_t e1000_mmio_base = 0;  // Memory-mapped I/O base
static uint8_t  e1000_mac[6];
static int      e1000_tx_tail = 0;

// Descriptor rings (statically allocated for simplicity)
static e1000_rx_desc_t rx_descs[E1000_NUM_RX_DESC] __attribute__((aligned(16)));
static e1000_tx_desc_t tx_descs[E1000_NUM_TX_DESC] __attribute__((aligned(16)));

// Packet buffers
#define E1000_RX_BUF_SIZE 2048
#define E1000_TX_BUF_SIZE 2048

static uint8_t rx_buffers[E1000_NUM_RX_DESC][E1000_RX_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffers[E1000_NUM_TX_DESC][E1000_TX_BUF_SIZE] __attribute__((aligned(16)));

// MMIO register access
static void e1000_write32(uint16_t reg, uint32_t val) {
    if (!e1000_mmio_base) return;
    *((volatile uint32_t*)(e1000_mmio_base + reg)) = val;
}

static uint32_t e1000_read32(uint16_t reg) {
    if (!e1000_mmio_base) return 0;
    return *((volatile uint32_t*)(e1000_mmio_base + reg));
}

// Read EEPROM (simplified - assumes done bit works)
static uint16_t e1000_read_eeprom(uint8_t addr) {
    uint32_t val = 0;

    // Start EEPROM read
    e1000_write32(E1000_REG_EERD, ((uint32_t)addr << 8) | 1);

    // Wait for read to complete
    for (int i = 0; i < 1000; i++) {
        val = e1000_read32(E1000_REG_EERD);
        if (val & (1 << 4)) {  // Done bit
            return (uint16_t)((val >> 16) & 0xFFFF);
        }
    }

    return 0;
}

// Read MAC address from EEPROM
static void e1000_read_mac(void) {
    uint16_t mac_word;

    mac_word = e1000_read_eeprom(0);
    e1000_mac[0] = mac_word & 0xFF;
    e1000_mac[1] = (mac_word >> 8) & 0xFF;

    mac_word = e1000_read_eeprom(1);
    e1000_mac[2] = mac_word & 0xFF;
    e1000_mac[3] = (mac_word >> 8) & 0xFF;

    mac_word = e1000_read_eeprom(2);
    e1000_mac[4] = mac_word & 0xFF;
    e1000_mac[5] = (mac_word >> 8) & 0xFF;
}

// Simplified PCI scan for e1000
// In a full implementation, this would be a proper PCI bus scan
static int e1000_probe(void) {
    // For QEMU with e1000, the MMIO base is typically at a known location
    // This is a simplified probe - in reality we'd scan PCI config space
    // Common QEMU e1000 MMIO bases: 0xFEBC0000, 0xF0000000

    static const uint32_t probe_bases[] = {
        0xFEBC0000, 0xF0000000, 0xE0000000, 0
    };

    for (int i = 0; probe_bases[i]; i++) {
        e1000_mmio_base = probe_bases[i];

        // Try to read device status register
        uint32_t status = e1000_read32(E1000_REG_STATUS);

        // Check for reasonable status value (not 0xFFFFFFFF or 0)
        if (status != 0xFFFFFFFF && status != 0) {
            // Found a device, verify it's an e1000
            // In a real driver we'd check PCI vendor/device ID
            return 1;
        }
    }

    e1000_mmio_base = 0;
    return 0;
}

// Initialize receive descriptors
static void e1000_init_rx(void) {
    // Set up RX descriptors
    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_descs[i].buffer = (uint64_t)(uint32_t)rx_buffers[i];
        rx_descs[i].status = 0;
    }

    // Program RX descriptor base and length
    e1000_write32(E1000_REG_RDBAL, (uint32_t)rx_descs);
    e1000_write32(E1000_REG_RDBAH, 0);
    e1000_write32(E1000_REG_RDLEN, E1000_NUM_RX_DESC * sizeof(e1000_rx_desc_t));

    // Set head and tail
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, E1000_NUM_RX_DESC - 1);

    // Enable receiver
    e1000_write32(E1000_REG_RCTL,
        E1000_RCTL_EN |       // Enable
        E1000_RCTL_BAM |      // Broadcast accept
        E1000_RCTL_BSIZE_2K | // 2KB buffers
        E1000_RCTL_SECRC);    // Strip CRC
}

// Initialize transmit descriptors
static void e1000_init_tx(void) {
    // Set up TX descriptors
    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_descs[i].buffer = (uint64_t)(uint32_t)tx_buffers[i];
        tx_descs[i].status = E1000_TXD_STAT_DD;  // Mark as done
        tx_descs[i].cmd = 0;
    }

    // Program TX descriptor base and length
    e1000_write32(E1000_REG_TDBAL, (uint32_t)tx_descs);
    e1000_write32(E1000_REG_TDBAH, 0);
    e1000_write32(E1000_REG_TDLEN, E1000_NUM_TX_DESC * sizeof(e1000_tx_desc_t));

    // Set head and tail
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);

    // Set TIPG (Inter-Packet Gap)
    e1000_write32(E1000_REG_TIPG, 0x00602006);

    // Enable transmitter
    e1000_write32(E1000_REG_TCTL,
        E1000_TCTL_EN |                    // Enable
        E1000_TCTL_PSP |                   // Pad short packets
        (15 << E1000_TCTL_CT_SHIFT) |      // Collision threshold
        (64 << E1000_TCTL_COLD_SHIFT));    // Collision distance
}

// Send a packet
static int e1000_send(const uint8_t *data, uint16_t len) {
    if (!e1000_mmio_base) return -1;
    if (len > E1000_TX_BUF_SIZE) return -1;

    // Get current tail
    int tail = e1000_tx_tail;
    int next_tail = (tail + 1) % E1000_NUM_TX_DESC;

    // Check if descriptor is available
    if (!(tx_descs[tail].status & E1000_TXD_STAT_DD)) {
        return -1;  // TX ring full
    }

    // Copy packet to TX buffer
    memcpy(tx_buffers[tail], data, len);

    // Set up descriptor
    tx_descs[tail].length = len;
    tx_descs[tail].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    tx_descs[tail].status = 0;

    // Update tail pointer
    e1000_tx_tail = next_tail;
    e1000_write32(E1000_REG_TDT, next_tail);

    return 0;
}

// Receive a packet
static int e1000_recv(uint8_t *buf, uint16_t *len) {
    if (!e1000_mmio_base) return -1;

    // Get current tail (next descriptor to check)
    uint32_t tail = e1000_read32(E1000_REG_RDT);
    uint32_t next = (tail + 1) % E1000_NUM_RX_DESC;

    // Check if descriptor has data
    if (!(rx_descs[next].status & E1000_RXD_STAT_DD)) {
        *len = 0;
        return -1;  // No packet
    }

    // Check for end of packet
    if (!(rx_descs[next].status & E1000_RXD_STAT_EOP)) {
        *len = 0;
        // Reset descriptor and continue
        rx_descs[next].status = 0;
        e1000_write32(E1000_REG_RDT, next);
        return -1;
    }

    // Get packet length
    uint16_t pkt_len = rx_descs[next].length;

    if (pkt_len > 1518) pkt_len = 1518;  // Sanity check

    // Copy packet data
    memcpy(buf, rx_buffers[next], pkt_len);
    *len = pkt_len;

    // Reset descriptor for reuse
    rx_descs[next].status = 0;

    // Update tail pointer
    e1000_write32(E1000_REG_RDT, next);

    return 0;
}

// ioctl handler for debugging
int e1000_ioctl(uint32_t cmd, void *arg) {
    if (cmd == 0 && arg) {
        *(uint32_t*)arg = e1000_mmio_base;
        return 0;
    }
    if (cmd == 1 && arg) {
        *(uint32_t*)arg = e1000_read32(E1000_REG_STATUS);
        return 0;
    }
    return -1;
}

static net_interface_t e1000_iface = {
    .name        = "",  // Will be assigned dynamically (eth0, eth1, etc.)
    .mac         = {0},
    .send        = e1000_send,
    .recv        = e1000_recv,
    .rx_packets  = 0,
    .tx_packets  = 0,
    .rx_errors   = 0,
    .tx_errors   = 0,
    .up          = 0,
};

void e1000_init(void) {
    if (!e1000_probe()) {
        kprintf("[e1000] Intel e1000 not detected\n");
        return;
    }

    kprintf("[e1000] Intel e1000 detected at MMIO 0x%08x\n", e1000_mmio_base);

    // Disable interrupts
    e1000_write32(E1000_REG_IMC, 0xFFFFFFFF);

    // Global reset
    uint32_t ctrl = e1000_read32(E1000_REG_CTRL);
    e1000_write32(E1000_REG_CTRL, ctrl | E1000_CTRL_RST);

    // Wait for reset to complete
    for (volatile int i = 0; i < 100000; i++);

    // Disable interrupts again after reset
    e1000_write32(E1000_REG_IMC, 0xFFFFFFFF);

    // Clear interrupt status
    e1000_read32(E1000_REG_ICR);

    // Read MAC address from EEPROM
    e1000_read_mac();
    memcpy(e1000_iface.mac, e1000_mac, 6);

    kprintf("[e1000] MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
            e1000_mac[0], e1000_mac[1], e1000_mac[2],
            e1000_mac[3], e1000_mac[4], e1000_mac[5]);

    // Program MAC address into receive address registers
    uint32_t ral = ((uint32_t)e1000_mac[0]) |
                   ((uint32_t)e1000_mac[1] << 8) |
                   ((uint32_t)e1000_mac[2] << 16) |
                   ((uint32_t)e1000_mac[3] << 24);
    uint32_t rah = ((uint32_t)e1000_mac[4]) |
                   ((uint32_t)e1000_mac[5] << 8) |
                   (1 << 31);  // Address Valid bit

    e1000_write32(E1000_REG_RAL, ral);
    e1000_write32(E1000_REG_RAH, rah);

    // Clear multicast table array
    for (int i = 0; i < 128; i++) {
        e1000_write32(E1000_REG_MTA + (i * 4), 0);
    }

    // Initialize RX and TX
    e1000_init_rx();
    e1000_init_tx();

    // Link up
    ctrl = e1000_read32(E1000_REG_CTRL);
    e1000_write32(E1000_REG_CTRL, ctrl | E1000_CTRL_SLU | E1000_CTRL_ASDE);

    // Register network interface
    e1000_iface.up = 1;
    net_register_interface(&e1000_iface);

    kprintf("[e1000] Initialized and registered as %s\n", e1000_iface.name);
}

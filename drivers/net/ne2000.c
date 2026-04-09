// BlueyOS NE2000 Network Driver - "Jack's Network Snorkel"
// Episode ref: "Swimming Lessons" - Jack just needs the right snorkel!
// The NE2000 is a classic ISA NIC widely supported in QEMU (rtl8029as).
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"
#include "../../include/ports.h"
#include "../../include/bluey.h"
#include "../../lib/stdio.h"
#include "ne2000.h"
#include "network.h"

static uint16_t ne_base = 0;   // I/O base, 0 if no card found
static uint8_t  ne_mac[6];

// NE2000 page 1 registers (accessed when CR PS0=1, PS1=0)
#define NE_P1_PAR0  0x01   // Physical Address Register 0 (MAC byte 0)

// ne_write and ne_read: low-level register access used for card configuration
// and future ioctl support via the driver framework.
static void ne_write(uint16_t reg, uint8_t val) { outb(ne_base + reg, val); }
static uint8_t ne_read(uint16_t reg)            { return inb(ne_base + reg); }

// ioctl handler exposed via driver framework (cmd=0: get base port, cmd=1: read CR)
int ne2000_ioctl(uint32_t cmd, void *arg) {
    if (cmd == 0 && arg) { *(uint32_t*)arg = ne_base; return 0; }
    if (cmd == 1 && arg) { *(uint8_t*)arg  = ne_read(NE_P0_CR); return 0; }
    ne_write(0, (uint8_t)cmd);  // generic register write for debugging
    return -1;
}

// Try to detect an NE2000 at the given I/O base
static int ne2000_probe(uint16_t base) {
    // Reset the card
    uint8_t r = inb(base + NE_RESET);
    outb(base + NE_RESET, r);
    // Wait a bit
    for (volatile int i = 0; i < 10000; i++);

    // Check ISR for RESET bit (bit 7) being set after reset
    outb(base + NE_P0_CR, CR_STOP | CR_RD2);
    outb(base + NE_P0_ISR, 0xFF);   // clear interrupts

    // Set DCR: FIFO threshold = 8 bytes, 16-bit, no loopback
    outb(base + NE_P0_DCR, 0x49);

    // Set a recognisable value and check it sticks
    outb(base + NE_P0_RBCR0, 0x00);
    outb(base + NE_P0_RBCR1, 0x00);
    outb(base + NE_P0_RCR, 0x20);   // monitor mode
    outb(base + NE_P0_TCR, 0x02);   // internal loopback

    outb(base + NE_P0_PSTART, 0x40);
    outb(base + NE_P0_PSTOP,  0x80);
    outb(base + NE_P0_BNRY,   0x40);

    // Switch to page 1 to check MAR (multicast) registers
    outb(base + NE_P0_CR, CR_STOP | CR_RD2 | CR_PS0);
    outb(base + NE_P0_CURR, 0x41);
    outb(base + NE_P0_CR, CR_STOP | CR_RD2);  // back to page 0

    // If status is 0xFF the card isn't there
    uint8_t stat = inb(base + NE_P0_CR);
    if (stat == 0xFF) return 0;
    return 1;
}

// Read MAC address from PROM (first 6 bytes at memory address 0)
static void ne2000_read_mac(void) {
    // Remote DMA read: 12 bytes from address 0 (PROM is word-wide on NE2000)
    outb(ne_base + NE_P0_CR,    CR_STOP | CR_RD2);
    outb(ne_base + NE_P0_RBCR0, 12);
    outb(ne_base + NE_P0_RBCR1, 0);
    outb(ne_base + NE_P0_RSAR0, 0);
    outb(ne_base + NE_P0_RSARH, 0);
    outb(ne_base + NE_P0_CR,    CR_START | CR_RD0);  // start remote read

    uint8_t prom[12];
    for (int i = 0; i < 12; i++) prom[i] = inb(ne_base + NE_DATAPORT);

    // MAC is every other byte on NE2000 (word access repeats each byte)
    for (int i = 0; i < 6; i++) ne_mac[i] = prom[i * 2];
}

static int ne2000_send(const uint8_t *data, uint16_t len) {
    if (!ne_base) return -1;
    if (len > 1518) return -1;  // Ethernet frame too large

    // Remote DMA write: copy packet to NE2000 transmit buffer at page 0x40
    outb(ne_base + NE_P0_CR,    CR_STOP | CR_RD2);
    outb(ne_base + NE_P0_RBCR0, (uint8_t)(len & 0xFF));
    outb(ne_base + NE_P0_RBCR1, (uint8_t)(len >> 8));
    outb(ne_base + NE_P0_RSAR0, 0);
    outb(ne_base + NE_P0_RSARH, 0x40);  // page 0x40 in NE2000 RAM
    outb(ne_base + NE_P0_CR,    CR_START | CR_RD1);  // remote write

    for (uint16_t i = 0; i < len; i++) outb(ne_base + NE_DATAPORT, data[i]);

    // Transmit
    outb(ne_base + NE_P0_CR,    CR_START | CR_RD2);
    outb(ne_base + NE_P0_TPSR,  0x40);
    outb(ne_base + NE_P0_TBCR0, (uint8_t)(len & 0xFF));
    outb(ne_base + NE_P0_TBCR1, (uint8_t)(len >> 8));
    outb(ne_base + NE_P0_CR,    CR_START | CR_TXP | CR_RD2);
    return 0;
}

static int ne2000_recv(uint8_t *buf, uint16_t *len) {
    // Simplified: just report no data for now (proper ring-buffer recv is complex)
    (void)buf; *len = 0;
    return -1;
}

static net_interface_t ne2000_iface = {
    .name        = "",  // Will be assigned dynamically (eth0, eth1, etc.)
    .mac         = {0},
    .send        = ne2000_send,
    .recv        = ne2000_recv,
    .rx_packets  = 0,
    .tx_packets  = 0,
    .rx_errors   = 0,
    .tx_errors   = 0,
    .up          = 0,
};

void ne2000_init(void) {
    static const uint16_t probe_ports[] = {
        NE2000_IO_BASE1, NE2000_IO_BASE2, NE2000_IO_BASE3, NE2000_IO_BASE4, 0
    };

    for (int i = 0; probe_ports[i]; i++) {
        if (ne2000_probe(probe_ports[i])) {
            ne_base = probe_ports[i];
            kprintf("[NET]  NE2000 found at I/O 0x%x\n", ne_base);
            ne2000_read_mac();
            kprintf("[NET]  MAC: %x:%x:%x:%x:%x:%x\n",
                    ne_mac[0], ne_mac[1], ne_mac[2],
                    ne_mac[3], ne_mac[4], ne_mac[5]);

            // Configure card for normal operation
            outb(ne_base + NE_P0_CR,  CR_STOP | CR_RD2);
            outb(ne_base + NE_P0_DCR, 0x49);
            outb(ne_base + NE_P0_RCR, 0x04);  // accept broadcast
            outb(ne_base + NE_P0_TCR, 0x00);  // normal transmit
            outb(ne_base + NE_P0_IMR, 0x00);  // no IRQs for now
            outb(ne_base + NE_P0_CR,  CR_START | CR_RD2);

            for (int j = 0; j < 6; j++) ne2000_iface.mac[j] = ne_mac[j];
            ne2000_iface.up = 1;
            net_register_interface(&ne2000_iface);
            kprintf("[NET]  Registered as %s\n", ne2000_iface.name);
            kprintf("%s\n", MSG_NET_INIT);
            return;
        }
    }
    kprintf("[NET]  NE2000: No card found - Jack is still learning to swim!\n");
    kprintf("[NET]  (Network will be unavailable this session)\n");
}

int ne2000_send_packet(const uint8_t *data, uint16_t len) {
    return ne2000_send(data, len);
}

int ne2000_recv_packet(uint8_t *buf, uint16_t *len) {
    return ne2000_recv(buf, len);
}

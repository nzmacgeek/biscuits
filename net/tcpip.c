// BlueyOS TCP/IP Stack - top-level glue and packet dispatcher
// Episode ref: "The Creek" - everything flows downstream eventually
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/net/network.h"
#include "../drivers/net/ne2000.h"
#include "tcpip.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"

static tcpip_config_t cfg;
static int            tcpip_ready = 0;

void tcpip_init(void) {
    // No default IP configuration - must be configured from userspace
    cfg.ip      = 0;
    cfg.gateway = 0;
    cfg.netmask = 0;
    cfg.dns     = 0;
    strncpy(cfg.ifname, "eth0", sizeof(cfg.ifname) - 1);
    cfg.loopback_ip = htonl(0x7F000001u);
    cfg.loopback_mask = htonl(0xFF000000u);
    cfg.loopback_enabled = 1;

    // Grab MAC from the registered NE2000 interface
    net_interface_t *iface = net_get_interface("eth0");
    if (iface) {
        memcpy(cfg.mac, iface->mac, 6);
    } else {
        // Fallback MAC (QEMU default)
        cfg.mac[0] = 0x52; cfg.mac[1] = 0x54;
        cfg.mac[2] = 0x00; cfg.mac[3] = 0x12;
        cfg.mac[4] = 0x34; cfg.mac[5] = 0x56;
    }

    arp_init();
    ip_init();
    icmp_init();
    udp_init();
    tcp_init();

    kprintf("[TCP/IP] IPv4 stack ready (no IP configured - use userspace tools)\n");
    tcpip_ready = 1;
}

void tcpip_set_config(uint32_t ip, uint32_t gw, uint32_t mask, uint32_t dns) {
    cfg.ip      = ip;
    cfg.gateway = gw;
    cfg.netmask = mask;
    cfg.dns     = dns;
}

void tcpip_set_loopback(uint32_t ip, uint32_t mask) {
    cfg.loopback_ip = ip;
    cfg.loopback_mask = mask;
    cfg.loopback_enabled = 1;
}

int tcpip_is_loopback_addr(uint32_t ip) {
    if (!cfg.loopback_enabled) return 0;
    return (ip & cfg.loopback_mask) == (cfg.loopback_ip & cfg.loopback_mask);
}

const tcpip_config_t *tcpip_get_config(void) {
    return &cfg;
}

// ---------------------------------------------------------------------------
// Packet dispatcher - called from the main poll loop or shell commands
// ---------------------------------------------------------------------------
void tcpip_poll(void) {
    if (!tcpip_ready) return;

    static uint8_t frame[1536];
    uint16_t len;

    const char *ifaces[] = { cfg.ifname, "lo", NULL };
    int seen_loopback = 0;
    for (int iface_idx = 0; ifaces[iface_idx]; iface_idx++) {
        const char *ifname = ifaces[iface_idx];
        if (strcmp(ifname, "lo") == 0) {
            if (!cfg.loopback_enabled || seen_loopback) continue;
            seen_loopback = 1;
        }

        // Drain up to 16 packets per call so we don't monopolise the CPU
        for (int i = 0; i < 16; i++) {
            len = 0;
            if (net_recv(ifname, frame, &len) != 0) break;
            if (len < sizeof(eth_hdr_t)) continue;

            eth_hdr_t *eth = (eth_hdr_t *)frame;
            uint16_t et = ntohs(eth->ethertype);

            switch (et) {
                case ETHERTYPE_ARP:
                    arp_handle(frame, len);
                    break;
                case ETHERTYPE_IPV4:
                    ip_handle(frame, len);
                    break;
                default:
                    break;
            }
        }
    }
}

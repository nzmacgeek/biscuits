// BlueyOS ARP - Address Resolution Protocol
// Episode ref: "The Creek" - you need to know where everyone lives
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/net/network.h"
#include "tcpip.h"
#include "arp.h"

// ---------------------------------------------------------------------------
// ARP table
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    int      valid;
} arp_entry_t;

static arp_entry_t arp_table[ARP_TABLE_SIZE];
static int         arp_count = 0;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void arp_build_eth_header(uint8_t *frame, const uint8_t *dst_mac,
                                  const uint8_t *src_mac, uint16_t ethertype) {
    memcpy(frame,     dst_mac, 6);
    memcpy(frame + 6, src_mac, 6);
    frame[12] = (uint8_t)(ethertype >> 8);
    frame[13] = (uint8_t)(ethertype & 0xFF);
}

static const uint8_t broadcast_mac[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
static const uint8_t zero_mac[6]      = {0x00,0x00,0x00,0x00,0x00,0x00};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void arp_init(void) {
    arp_count = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) arp_table[i].valid = 0;
    kprintf("[ARP]  ARP table initialised (%d slots)\n", ARP_TABLE_SIZE);
}

void arp_add_entry(uint32_t ip_be, const uint8_t *mac) {
    // Check if already present; update if so
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip_be) {
            memcpy(arp_table[i].mac, mac, 6);
            return;
        }
    }
    // Find a free slot (circular eviction if full)
    int slot = arp_count % ARP_TABLE_SIZE;
    arp_table[slot].ip    = ip_be;
    arp_table[slot].valid = 1;
    memcpy(arp_table[slot].mac, mac, 6);
    arp_count++;
}

int arp_lookup(uint32_t ip_be, uint8_t *mac_out) {
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (arp_table[i].valid && arp_table[i].ip == ip_be) {
            if (mac_out) memcpy(mac_out, arp_table[i].mac, 6);
            return 0;
        }
    }
    return -1;   /* not in cache */
}

void arp_send_request(uint32_t target_ip_be) {
    const tcpip_config_t *cfg = tcpip_get_config();
    net_interface_t *iface = net_get_interface(cfg->ifname);
    if (!iface) return;

    uint8_t frame[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];
    memset(frame, 0, sizeof(frame));

    // Ethernet header
    arp_build_eth_header(frame, broadcast_mac, cfg->mac,
                         ETHERTYPE_ARP);

    // ARP payload
    arp_pkt_t *pkt = (arp_pkt_t *)(frame + sizeof(eth_hdr_t));
    pkt->htype = htons(1);
    pkt->ptype = htons(ETHERTYPE_IPV4);
    pkt->hlen  = 6;
    pkt->plen  = 4;
    pkt->oper  = htons(ARP_REQUEST);
    memcpy(pkt->sha, cfg->mac, 6);
    pkt->spa   = cfg->ip;
    memcpy(pkt->tha, zero_mac, 6);
    pkt->tpa   = target_ip_be;

    net_send(cfg->ifname, frame, (uint16_t)sizeof(frame));
}

void arp_handle(const uint8_t *frame, uint16_t len) {
    if (len < (uint16_t)(sizeof(eth_hdr_t) + sizeof(arp_pkt_t))) return;

    const arp_pkt_t *pkt = (const arp_pkt_t *)(frame + sizeof(eth_hdr_t));
    const tcpip_config_t *cfg = tcpip_get_config();

    if (ntohs(pkt->htype) != 1 || ntohs(pkt->ptype) != ETHERTYPE_IPV4) return;

    // Learn sender's MAC
    arp_add_entry(pkt->spa, pkt->sha);

    if (ntohs(pkt->oper) == ARP_REQUEST && pkt->tpa == cfg->ip) {
        // Someone is asking for our MAC - reply
        net_interface_t *iface = net_get_interface(cfg->ifname);
        if (!iface) return;

        uint8_t reply[sizeof(eth_hdr_t) + sizeof(arp_pkt_t)];
        arp_build_eth_header(reply, pkt->sha, cfg->mac, ETHERTYPE_ARP);

        arp_pkt_t *rep = (arp_pkt_t *)(reply + sizeof(eth_hdr_t));
        rep->htype = htons(1);
        rep->ptype = htons(ETHERTYPE_IPV4);
        rep->hlen  = 6;
        rep->plen  = 4;
        rep->oper  = htons(ARP_REPLY);
        memcpy(rep->sha, cfg->mac, 6);
        rep->spa   = cfg->ip;
        memcpy(rep->tha, pkt->sha, 6);
        rep->tpa   = pkt->spa;

        net_send(cfg->ifname, reply, (uint16_t)sizeof(reply));
    }
}

void arp_print_table(void) {
    kprintf("[ARP]  Table:\n");
    int any = 0;
    for (int i = 0; i < ARP_TABLE_SIZE; i++) {
        if (!arp_table[i].valid) continue;
        char ipstr[20];
        ip_to_str(arp_table[i].ip, ipstr);
        kprintf("  %s  ->  %x:%x:%x:%x:%x:%x\n",
                ipstr,
                arp_table[i].mac[0], arp_table[i].mac[1],
                arp_table[i].mac[2], arp_table[i].mac[3],
                arp_table[i].mac[4], arp_table[i].mac[5]);
        any = 1;
    }
    if (!any) kprintf("  (empty)\n");
}

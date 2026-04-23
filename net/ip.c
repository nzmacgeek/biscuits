// BlueyOS IPv4 Layer
// Episode ref: "Camping" - navigate by the stars (and a routing table)
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/net/network.h"
#include "tcpip.h"
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"

// Running IP packet identifier
static uint16_t ip_id = 0;

void ip_init(void) {
    ip_id = 1;
    kprintf("[IP]   IPv4 layer ready\n");
}

uint16_t ip_checksum(const void *hdr, size_t len) {
    return net_checksum(hdr, len);
}

int ip_send(uint8_t proto, uint32_t dst_ip,
            const uint8_t *payload, uint16_t payload_len) {
    const tcpip_config_t *cfg = tcpip_get_config();
    int use_loopback = tcpip_is_loopback_addr(dst_ip);

    // Determine next hop: if dst is on same subnet, use dst directly;
    // otherwise use the gateway.
    uint32_t nexthop;
    if (!use_loopback) {
        if ((dst_ip & cfg->netmask) == (cfg->ip & cfg->netmask))
            nexthop = dst_ip;
        else
            nexthop = cfg->gateway;
    } else {
        nexthop = dst_ip;
    }

    // Resolve next-hop MAC via ARP
    uint8_t dst_mac[6];
    if (use_loopback) {
        memset(dst_mac, 0, sizeof(dst_mac));
    } else if (arp_lookup(nexthop, dst_mac) != 0) {
        // Not in cache - send ARP request and fail this send
        arp_send_request(nexthop);
        return -1;
    }

    // Build the frame: eth header + IP header + payload
    uint16_t total_len = (uint16_t)(sizeof(ip_hdr_t) + payload_len);
    uint16_t frame_len = (uint16_t)(sizeof(eth_hdr_t) + total_len);

    // Use a static buffer (1536 bytes covers max Ethernet frame)
    static uint8_t frame[1536];
    if (frame_len > sizeof(frame)) return -1;

    // Ethernet header
    eth_hdr_t *eth = (eth_hdr_t *)frame;
    memcpy(eth->dst, dst_mac, 6);
    if (use_loopback)
        memset(eth->src, 0, 6);
    else
        memcpy(eth->src, cfg->mac, 6);
    eth->ethertype = htons(ETHERTYPE_IPV4);

    // IP header
    ip_hdr_t *ip = (ip_hdr_t *)(frame + sizeof(eth_hdr_t));
    ip->ver_ihl    = (4 << 4) | 5;    /* version 4, IHL = 5 words */
    ip->tos        = 0;
    ip->total_len  = htons(total_len);
    ip->id         = htons(ip_id++);
    ip->flags_frag = htons(IP_FLAG_DF);
    ip->ttl        = 64;
    ip->protocol   = proto;
    ip->checksum   = 0;
    ip->src_ip     = use_loopback ? cfg->loopback_ip : cfg->ip;
    ip->dst_ip     = dst_ip;
    ip->checksum   = ip_checksum(ip, sizeof(ip_hdr_t));

    // Payload
    if (payload && payload_len)
        memcpy(frame + sizeof(eth_hdr_t) + sizeof(ip_hdr_t),
               payload, payload_len);

    return net_send(use_loopback ? "lo" : cfg->ifname, frame, frame_len);
}

void ip_handle(const uint8_t *frame, uint16_t len) {
    if (len < (uint16_t)(sizeof(eth_hdr_t) + sizeof(ip_hdr_t))) return;

    const ip_hdr_t *ip = (const ip_hdr_t *)(frame + sizeof(eth_hdr_t));
    uint8_t  ihl        = (ip->ver_ihl & 0x0F) * 4;
    uint8_t  version    = (ip->ver_ihl >> 4);

    if (version != 4 || ihl < 20) return;

    // Verify checksum
    if (net_checksum(ip, ihl) != 0) return;

    const tcpip_config_t *cfg = tcpip_get_config();

    // Only process packets destined for us, loopback, or broadcast
    if (ip->dst_ip != cfg->ip && ip->dst_ip != 0xFFFFFFFF &&
        !tcpip_is_loopback_addr(ip->dst_ip)) return;

    // Learn sender's MAC from ethernet header
    const eth_hdr_t *eth = (const eth_hdr_t *)frame;
    arp_add_entry(ip->src_ip, eth->src);

    uint16_t data_offset = (uint16_t)(sizeof(eth_hdr_t) + ihl);
    uint16_t data_len    = (uint16_t)(ntohs(ip->total_len) - ihl);

    if (len < data_offset + data_len) return;

    const uint8_t *data = frame + data_offset;

    switch (ip->protocol) {
        case IPPROTO_ICMP:
            icmp_raw_deliver((const uint8_t *)ip, ntohs(ip->total_len), ip->src_ip);
            icmp_handle(ip->src_ip, data, data_len);
            break;
        case IPPROTO_UDP:
            udp_handle(ip->src_ip, data, data_len);
            break;
        case IPPROTO_TCP:
            tcp_handle(ip->src_ip, data, data_len);
            break;
        default:
            break;
    }
}

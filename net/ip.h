#pragma once
// BlueyOS IPv4 Layer
// "Finding the way across the network!" - Bandit (The Creek)
// Episode ref: "Camping" - everyone needs a map
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "tcpip.h"

void ip_init(void);

// Send an IPv4 packet. dst_ip is in network (big-endian) byte order.
// proto: IPPROTO_ICMP / IPPROTO_UDP / IPPROTO_TCP
// payload: data after the IP header (already filled in by caller)
int ip_send(uint8_t proto, uint32_t dst_ip,
            const uint8_t *payload, uint16_t payload_len);

// Called by tcpip_poll() when an Ethernet frame with EtherType=IPv4 arrives.
void ip_handle(const uint8_t *frame, uint16_t len);

// IP checksum helper (calls net_checksum from tcpip.h)
uint16_t ip_checksum(const void *hdr, size_t len);

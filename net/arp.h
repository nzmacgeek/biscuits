#pragma once
// BlueyOS ARP - Address Resolution Protocol
// "Who lives at that address?" - Bluey (The Creek episode)
// Episode ref: "The Creek" - everyone needs to know where to find each other
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "tcpip.h"

#define ARP_TABLE_SIZE   16

typedef struct __attribute__((packed)) {
    uint16_t htype;      /* hardware type: 1 = Ethernet */
    uint16_t ptype;      /* protocol type: 0x0800 = IPv4 */
    uint8_t  hlen;       /* hardware address length: 6 */
    uint8_t  plen;       /* protocol address length: 4 */
    uint16_t oper;       /* operation: 1=request, 2=reply */
    uint8_t  sha[6];     /* sender hardware address */
    uint32_t spa;        /* sender protocol address */
    uint8_t  tha[6];     /* target hardware address */
    uint32_t tpa;        /* target protocol address */
} arp_pkt_t;

#define ARP_REQUEST  1
#define ARP_REPLY    2

void arp_init(void);
int  arp_lookup(uint32_t ip_be, uint8_t *mac_out);
void arp_handle(const uint8_t *frame, uint16_t len);
void arp_send_request(uint32_t target_ip_be);
void arp_add_entry(uint32_t ip_be, const uint8_t *mac);
void arp_print_table(void);

#pragma once
// BlueyOS Network Framework - "Jack's Network Snorkel"
// Episode ref: "Swimming Lessons" - Jack just needs the right snorkel to dive in
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../../include/types.h"

#define NET_MAX_PACKET_LEN  1518   // maximum Ethernet frame
#define NET_MAX_INTERFACES  4

// Raw Ethernet packet (layer 2)
typedef struct {
    uint8_t  dst_mac[6];
    uint8_t  src_mac[6];
    uint16_t ethertype;
    uint8_t  payload[1500];
    uint16_t payload_len;
} net_packet_t;

// Network interface descriptor
typedef struct {
    char     name[16];         // e.g. "eth0"
    uint8_t  mac[6];
    int    (*send)(const uint8_t *data, uint16_t len);
    int    (*recv)(uint8_t *buf, uint16_t *len);
    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint8_t  up;               // 1 if interface is up
} net_interface_t;

void net_init(void);
void net_register_interface(net_interface_t *iface);
int  net_send(const char *ifname, const uint8_t *data, uint16_t len);
int  net_recv(const char *ifname, uint8_t *buf, uint16_t *len);
net_interface_t *net_get_interface(const char *name);
void net_print_interfaces(void);

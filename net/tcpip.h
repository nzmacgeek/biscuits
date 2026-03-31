#pragma once
// BlueyOS TCP/IP IPv4 Network Stack - "Jack's Full Snorkel Kit"
// Episode ref: "Swimming Lessons" - Jack finally gets the right snorkel
// Episode ref: "The Creek" - the Heelers navigate the network together
//
// ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
//
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"

// ---------------------------------------------------------------------------
// Byte-order helpers (x86 is little-endian; network is big-endian)
// ---------------------------------------------------------------------------
static inline uint16_t bswap16(uint16_t x) {
    return (uint16_t)((x >> 8) | (x << 8));
}

static inline uint32_t bswap32(uint32_t x) {
    return ((x >> 24) & 0x000000FFU)
         | ((x >>  8) & 0x0000FF00U)
         | ((x <<  8) & 0x00FF0000U)
         | ((x << 24) & 0xFF000000U);
}

#define htons(x)  bswap16((uint16_t)(x))
#define ntohs(x)  bswap16((uint16_t)(x))
#define htonl(x)  bswap32((uint32_t)(x))
#define ntohl(x)  bswap32((uint32_t)(x))

// ---------------------------------------------------------------------------
// EtherType constants
// ---------------------------------------------------------------------------
#define ETHERTYPE_IPV4   0x0800
#define ETHERTYPE_ARP    0x0806

// ---------------------------------------------------------------------------
// IP protocol numbers
// ---------------------------------------------------------------------------
#define IPPROTO_ICMP   1
#define IPPROTO_TCP    6
#define IPPROTO_UDP    17

// ---------------------------------------------------------------------------
// Well-known ports
// ---------------------------------------------------------------------------
#define PORT_DNS    53
#define PORT_HTTP   80
#define PORT_HTTPS  443
#define PORT_TELNET 23
#define PORT_SSH    22

// ---------------------------------------------------------------------------
// Stack configuration (defaults suitable for QEMU user-mode networking)
// ---------------------------------------------------------------------------
#define TCPIP_CFG_IP_DEFAULT      0x0A00020F  /* 10.0.2.15  */
#define TCPIP_CFG_GW_DEFAULT      0x0A000202  /* 10.0.2.2   */
#define TCPIP_CFG_MASK_DEFAULT    0xFFFFFF00  /* 255.255.255.0 */
#define TCPIP_CFG_DNS_DEFAULT     0x0A000203  /* 10.0.2.3   */

// ---------------------------------------------------------------------------
// Ethernet header (14 bytes)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t ethertype;          /* big-endian */
} eth_hdr_t;

// ---------------------------------------------------------------------------
// IPv4 header (20 bytes minimum)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  ver_ihl;            /* version (4) | IHL in 32-bit words */
    uint8_t  tos;
    uint16_t total_len;          /* big-endian */
    uint16_t id;                 /* big-endian */
    uint16_t flags_frag;         /* big-endian */
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;           /* big-endian */
    uint32_t src_ip;             /* big-endian */
    uint32_t dst_ip;             /* big-endian */
} ip_hdr_t;

// IP flag bits (in the high 3 bits of flags_frag, big-endian)
#define IP_FLAG_DF  0x4000       /* don't fragment */
#define IP_FLAG_MF  0x2000       /* more fragments */

// ---------------------------------------------------------------------------
// ICMP header (8 bytes)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t id;
    uint16_t seq;
} icmp_hdr_t;

#define ICMP_ECHO_REQUEST  8
#define ICMP_ECHO_REPLY    0

// ---------------------------------------------------------------------------
// UDP header (8 bytes)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} udp_hdr_t;

// ---------------------------------------------------------------------------
// TCP header (20 bytes minimum)
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;        /* top 4 bits: header len in 32-bit words */
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} tcp_hdr_t;

// TCP flag bits
#define TCP_FLAG_FIN  0x01
#define TCP_FLAG_SYN  0x02
#define TCP_FLAG_RST  0x04
#define TCP_FLAG_PSH  0x08
#define TCP_FLAG_ACK  0x10
#define TCP_FLAG_URG  0x20

// ---------------------------------------------------------------------------
// Pseudo-header for TCP/UDP checksum
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  zero;
    uint8_t  protocol;
    uint16_t length;
} ip_pseudo_hdr_t;

// ---------------------------------------------------------------------------
// Shared checksum calculator (used by IP, ICMP, TCP, UDP)
// ---------------------------------------------------------------------------
static inline uint16_t net_checksum(const void *data, size_t len) {
    const uint16_t *ptr = (const uint16_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    if (len == 1) sum += *(const uint8_t *)ptr;

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)(~sum);
}

// ---------------------------------------------------------------------------
// Global stack configuration
// ---------------------------------------------------------------------------
typedef struct {
    uint32_t ip;
    uint32_t gateway;
    uint32_t netmask;
    uint32_t dns;
    uint8_t  mac[6];
    char     ifname[16];
    uint32_t loopback_ip;
    uint32_t loopback_mask;
    uint8_t  loopback_enabled;
} tcpip_config_t;

void tcpip_init(void);
void tcpip_set_config(uint32_t ip, uint32_t gw, uint32_t mask, uint32_t dns);
void tcpip_set_loopback(uint32_t ip, uint32_t mask);
int  tcpip_is_loopback_addr(uint32_t ip);
const tcpip_config_t *tcpip_get_config(void);
void tcpip_poll(void);           /* call periodically to drain the RX queue */

// Format a 32-bit IPv4 address (big-endian) as "A.B.C.D" into buf (min 16 bytes)
static inline void ip_to_str(uint32_t ip_be, char *buf) {
    uint8_t *b = (uint8_t *)&ip_be;
    // Manual itoa for each octet
    int pos = 0;
    for (int i = 0; i < 4; i++) {
        uint8_t v = b[i];
        if (v >= 100) { buf[pos++] = (char)('0' + v/100); v %= 100; buf[pos++] = (char)('0' + v/10); v %= 10; }
        else if (v >= 10) { buf[pos++] = (char)('0' + v/10); v %= 10; }
        buf[pos++] = (char)('0' + v);
        if (i < 3) buf[pos++] = '.';
    }
    buf[pos] = '\0';
}

// BlueyOS ICMP - Internet Control Message Protocol
// Episode ref: "The Creek" - "Hello? Is anyone out there?"
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "tcpip.h"
#include "ip.h"
#include "icmp.h"

#define ICMP_MAX_SOCKETS   8
#define ICMP_RECV_BUF_SIZE 1472

typedef struct {
    int      active;
    uint8_t  rx_buf[ICMP_RECV_BUF_SIZE];
    uint16_t rx_len;
    int      rx_ready;
    uint32_t remote_ip;
} icmp_socket_t;

static icmp_socket_t icmp_sockets[ICMP_MAX_SOCKETS];

void icmp_init(void) {
    for (int i = 0; i < ICMP_MAX_SOCKETS; i++) {
        icmp_sockets[i].active = 0;
        icmp_sockets[i].rx_ready = 0;
    }
    kprintf("[ICMP] ICMP handler ready\n");
}

int icmp_open(int protocol) {
    if (protocol != 0 && protocol != IPPROTO_ICMP) return -1;

    for (int i = 0; i < ICMP_MAX_SOCKETS; i++) {
        if (!icmp_sockets[i].active) {
            icmp_sockets[i].active = 1;
            icmp_sockets[i].rx_ready = 0;
            icmp_sockets[i].rx_len = 0;
            icmp_sockets[i].remote_ip = 0;
            return i;
        }
    }

    return -1;
}

void icmp_close(int sock) {
    if (sock < 0 || sock >= ICMP_MAX_SOCKETS) return;
    icmp_sockets[sock].active = 0;
}

int icmp_send(int sock, uint32_t dst_ip, const uint8_t *data, uint16_t len) {
    if (sock < 0 || sock >= ICMP_MAX_SOCKETS || !icmp_sockets[sock].active) return -1;
    if (!data || len < sizeof(icmp_hdr_t)) return -1;

    static uint8_t pkt[1500];
    if (len > sizeof(pkt)) return -1;

    memcpy(pkt, data, len);
    icmp_hdr_t *hdr = (icmp_hdr_t *)pkt;
    hdr->checksum = 0;
    hdr->checksum = net_checksum(pkt, len);

    return ip_send(IPPROTO_ICMP, dst_ip, pkt, len);
}

int icmp_recv(int sock, uint8_t *buf, uint16_t max_len, uint32_t *src_ip) {
    if (sock < 0 || sock >= ICMP_MAX_SOCKETS || !icmp_sockets[sock].active) return -1;
    if (!buf || max_len == 0) return -1;
    if (!icmp_sockets[sock].rx_ready) return 0;

    uint16_t n = icmp_sockets[sock].rx_len;
    if (n > max_len) n = max_len;
    memcpy(buf, icmp_sockets[sock].rx_buf, n);

    if (src_ip) *src_ip = icmp_sockets[sock].remote_ip;

    icmp_sockets[sock].rx_ready = 0;
    return (int)n;
}

void icmp_handle(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    if (len < sizeof(icmp_hdr_t)) return;

    const icmp_hdr_t *hdr = (const icmp_hdr_t *)data;

    // Verify checksum
    if (net_checksum(data, len) != 0) return;

    // Deliver incoming ICMP payload to all active raw ICMP sockets.
    for (int i = 0; i < ICMP_MAX_SOCKETS; i++) {
        if (!icmp_sockets[i].active) continue;
        if (icmp_sockets[i].rx_ready) continue; // preserve unread datagram
        uint16_t copy_len = len;
        if (copy_len > ICMP_RECV_BUF_SIZE) copy_len = ICMP_RECV_BUF_SIZE;
        memcpy(icmp_sockets[i].rx_buf, data, copy_len);
        icmp_sockets[i].rx_len = copy_len;
        icmp_sockets[i].remote_ip = src_ip;
        icmp_sockets[i].rx_ready = 1;
    }

    if (hdr->type == ICMP_ECHO_REQUEST) {
        // Build and send an echo reply
        static uint8_t reply[1500];
        uint16_t reply_len = len;
        if (reply_len > sizeof(reply)) reply_len = (uint16_t)sizeof(reply);

        memcpy(reply, data, reply_len);
        icmp_hdr_t *rep = (icmp_hdr_t *)reply;
        rep->type     = ICMP_ECHO_REPLY;
        rep->checksum = 0;
        rep->checksum = net_checksum(reply, reply_len);

        ip_send(IPPROTO_ICMP, src_ip, reply, reply_len);

        char ipstr[20];
        ip_to_str(src_ip, ipstr);
        kprintf("[ICMP] echo request from %s - replied\n", ipstr);

    } else if (hdr->type == ICMP_ECHO_REPLY) {
        char ipstr[20];
        ip_to_str(src_ip, ipstr);
        kprintf("[ICMP] echo reply from %s: id=%d seq=%d\n",
                ipstr, ntohs(hdr->id), ntohs(hdr->seq));
    }
}

int icmp_ping(uint32_t dst_ip, uint16_t id, uint16_t seq) {
    // Build a minimal echo request (header + 32 bytes payload)
    uint8_t pkt[sizeof(icmp_hdr_t) + 32];
    memset(pkt, 0, sizeof(pkt));

    icmp_hdr_t *hdr = (icmp_hdr_t *)pkt;
    hdr->type     = ICMP_ECHO_REQUEST;
    hdr->code     = 0;
    hdr->id       = htons(id);
    hdr->seq      = htons(seq);
    hdr->checksum = 0;

    // Fill payload with "BlueyOS!" pattern
    const char *pat = "BlueyOS!";
    for (int i = 0; i < 32; i++)
        pkt[sizeof(icmp_hdr_t) + i] = (uint8_t)pat[i % 8];

    hdr->checksum = net_checksum(pkt, sizeof(pkt));

    return ip_send(IPPROTO_ICMP, dst_ip, pkt, (uint16_t)sizeof(pkt));
}

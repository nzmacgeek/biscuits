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

void icmp_init(void) {
    kprintf("[ICMP] ICMP handler ready\n");
}

void icmp_handle(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    if (len < sizeof(icmp_hdr_t)) return;

    const icmp_hdr_t *hdr = (const icmp_hdr_t *)data;

    // Verify checksum
    if (net_checksum(data, len) != 0) return;

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

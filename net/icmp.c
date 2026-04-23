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
#define ICMP_RECV_BUF_SIZE 1480

typedef struct {
    int      active;
    uint8_t  rx_buf[ICMP_RECV_BUF_SIZE];
    uint16_t rx_len;
    int      rx_ready;
    int      rx_busy;
    uint32_t remote_ip;
} icmp_socket_t;

static icmp_socket_t icmp_sockets[ICMP_MAX_SOCKETS];

static uint32_t icmp_irq_save(void) {
    uint32_t flags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void icmp_irq_restore(uint32_t flags) {
    __asm__ volatile("push %0; popf" : : "r"(flags) : "memory", "cc");
}

void icmp_init(void) {
    for (int i = 0; i < ICMP_MAX_SOCKETS; i++) {
        icmp_sockets[i].active = 0;
        icmp_sockets[i].rx_ready = 0;
    }
    kprintf("[ICMP] ICMP handler ready\n");
}

int icmp_open(int protocol) {
    uint32_t flags;
    if (protocol != 0 && protocol != IPPROTO_ICMP) return -1;

    flags = icmp_irq_save();
    for (int i = 0; i < ICMP_MAX_SOCKETS; i++) {
        if (!icmp_sockets[i].active) {
            icmp_sockets[i].active = 1;
            icmp_sockets[i].rx_ready = 0;
            icmp_sockets[i].rx_busy = 0;
            icmp_sockets[i].rx_len = 0;
            icmp_sockets[i].remote_ip = 0;
            icmp_irq_restore(flags);
            return i;
        }
    }
    icmp_irq_restore(flags);

    return -1;
}

void icmp_close(int sock) {
    uint32_t flags;
    if (sock < 0 || sock >= ICMP_MAX_SOCKETS) return;
    flags = icmp_irq_save();
    icmp_sockets[sock].active = 0;
    icmp_sockets[sock].rx_ready = 0;
    icmp_sockets[sock].rx_busy = 0;
    icmp_irq_restore(flags);
}

int icmp_send(int sock, uint32_t dst_ip, const uint8_t *data, uint16_t len) {
    if (sock < 0 || sock >= ICMP_MAX_SOCKETS || !icmp_sockets[sock].active) return -1;
    if (!data || len < sizeof(icmp_hdr_t)) return -1;
    return ip_send(IPPROTO_ICMP, dst_ip, data, len);
}

int icmp_recv(int sock, uint8_t *buf, uint16_t max_len, uint32_t *src_ip) {
    uint32_t flags;
    uint16_t n;
    uint32_t remote_ip;
    if (sock < 0 || sock >= ICMP_MAX_SOCKETS || !icmp_sockets[sock].active) return -1;
    if (!buf || max_len == 0) return -1;
    flags = icmp_irq_save();
    if (!icmp_sockets[sock].rx_ready) {
        icmp_irq_restore(flags);
        return 0;
    }
    if (icmp_sockets[sock].rx_busy) {
        icmp_irq_restore(flags);
        return 0;
    }
    icmp_sockets[sock].rx_busy = 1;

    n = icmp_sockets[sock].rx_len;
    if (n > max_len) n = max_len;
    remote_ip = icmp_sockets[sock].remote_ip;
    icmp_irq_restore(flags);

    memcpy(buf, icmp_sockets[sock].rx_buf, n);

    flags = icmp_irq_save();
    icmp_sockets[sock].rx_ready = 0;
    icmp_sockets[sock].rx_busy = 0;
    icmp_irq_restore(flags);

    if (src_ip) *src_ip = remote_ip;
    return (int)n;
}

int icmp_has_data(int sock) {
    uint32_t flags;
    int ready = 0;
    if (sock < 0 || sock >= ICMP_MAX_SOCKETS) return 0;
    flags = icmp_irq_save();
    // Ready means: socket is active, has queued packet, and no in-flight copy.
    if (icmp_sockets[sock].active && icmp_sockets[sock].rx_ready && !icmp_sockets[sock].rx_busy) ready = 1;
    icmp_irq_restore(flags);
    return ready;
}

void icmp_raw_deliver(const uint8_t *ip_packet, uint16_t ip_len, uint32_t src_ip) {
    // Raw sockets receive full IPv4 packet bytes for ICMP protocol traffic.
    if (!ip_packet || ip_len < sizeof(ip_hdr_t) + 1u) return;
    uint16_t copy_len = ip_len;
    if (copy_len > ICMP_RECV_BUF_SIZE) copy_len = ICMP_RECV_BUF_SIZE;

    for (int i = 0; i < ICMP_MAX_SOCKETS; i++) {
        uint32_t flags = icmp_irq_save();
        if (!icmp_sockets[i].active || icmp_sockets[i].rx_ready || icmp_sockets[i].rx_busy) {
            icmp_irq_restore(flags);
            continue;
        }
        icmp_sockets[i].rx_busy = 1;
        icmp_irq_restore(flags);

        memcpy(icmp_sockets[i].rx_buf, ip_packet, copy_len);

        flags = icmp_irq_save();
        // Re-check active state: socket may have been closed while copy happened.
        if (icmp_sockets[i].active) {
            icmp_sockets[i].rx_len = copy_len;
            icmp_sockets[i].remote_ip = src_ip;
            icmp_sockets[i].rx_ready = 1;
        }
        icmp_sockets[i].rx_busy = 0;
        icmp_irq_restore(flags);
    }
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

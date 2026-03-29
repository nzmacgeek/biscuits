// BlueyOS UDP - User Datagram Protocol
// Episode ref: "Stories" - quick notes passed between friends
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "tcpip.h"
#include "ip.h"
#include "udp.h"

static udp_socket_t udp_sockets[UDP_MAX_SOCKETS];
static uint16_t     udp_ephemeral = 49152;   /* ephemeral port start */

void udp_init(void) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        udp_sockets[i].active   = 0;
        udp_sockets[i].rx_ready = 0;
    }
    kprintf("[UDP]  UDP layer ready\n");
}

int udp_open(uint16_t local_port) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!udp_sockets[i].active) {
            udp_sockets[i].active      = 1;
            udp_sockets[i].rx_ready    = 0;
            udp_sockets[i].rx_len      = 0;
            udp_sockets[i].remote_ip   = 0;
            udp_sockets[i].remote_port = 0;
            if (local_port == 0) {
                udp_sockets[i].local_port = udp_ephemeral++;
                if (udp_ephemeral == 0) udp_ephemeral = 49152;
            } else {
                udp_sockets[i].local_port = local_port;
            }
            return i;
        }
    }
    return -1;   /* no free socket */
}

void udp_close(int sock) {
    if (sock >= 0 && sock < UDP_MAX_SOCKETS)
        udp_sockets[sock].active = 0;
}

int udp_send(int sock, uint32_t dst_ip, uint16_t dst_port,
             const uint8_t *data, uint16_t len) {
    if (sock < 0 || sock >= UDP_MAX_SOCKETS || !udp_sockets[sock].active)
        return -1;

    const tcpip_config_t *cfg = tcpip_get_config();
    uint16_t udp_len = (uint16_t)(sizeof(udp_hdr_t) + len);

    // Build a buffer: UDP header + payload
    static uint8_t buf[1500];
    if (udp_len > sizeof(buf)) return -1;

    udp_hdr_t *hdr = (udp_hdr_t *)buf;
    hdr->src_port = htons(udp_sockets[sock].local_port);
    hdr->dst_port = htons(dst_port);
    hdr->length   = htons(udp_len);
    hdr->checksum = 0;

    if (data && len) memcpy(buf + sizeof(udp_hdr_t), data, len);

    // Compute UDP checksum using pseudo-header
    ip_pseudo_hdr_t pseudo;
    pseudo.src_ip   = cfg->ip;
    pseudo.dst_ip   = dst_ip;
    pseudo.zero     = 0;
    pseudo.protocol = IPPROTO_UDP;
    pseudo.length   = htons(udp_len);

    // Combine pseudo-header + UDP data for checksum
    static uint8_t ckbuf[1520];
    memcpy(ckbuf, &pseudo, sizeof(pseudo));
    memcpy(ckbuf + sizeof(pseudo), buf, udp_len);
    hdr->checksum = net_checksum(ckbuf, sizeof(pseudo) + udp_len);

    return ip_send(IPPROTO_UDP, dst_ip, buf, udp_len);
}

int udp_recv(int sock, uint8_t *buf, uint16_t max_len,
             uint32_t *src_ip, uint16_t *src_port) {
    if (sock < 0 || sock >= UDP_MAX_SOCKETS || !udp_sockets[sock].active)
        return -1;
    if (!udp_sockets[sock].rx_ready) return 0;

    uint16_t n = udp_sockets[sock].rx_len;
    if (n > max_len) n = max_len;
    memcpy(buf, udp_sockets[sock].rx_buf, n);

    if (src_ip)   *src_ip   = udp_sockets[sock].remote_ip;
    if (src_port) *src_port = udp_sockets[sock].remote_port;

    udp_sockets[sock].rx_ready = 0;
    return (int)n;
}

void udp_handle(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    if (len < sizeof(udp_hdr_t)) return;

    const udp_hdr_t *hdr  = (const udp_hdr_t *)data;
    uint16_t dst_port      = ntohs(hdr->dst_port);
    uint16_t src_port_net  = ntohs(hdr->src_port);
    uint16_t payload_len   = (uint16_t)(len - sizeof(udp_hdr_t));
    const uint8_t *payload = data + sizeof(udp_hdr_t);

    // Deliver to the matching socket
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!udp_sockets[i].active) continue;
        if (udp_sockets[i].local_port != dst_port) continue;

        uint16_t copy_len = payload_len;
        if (copy_len > UDP_RECV_BUF_SIZE) copy_len = UDP_RECV_BUF_SIZE;
        memcpy(udp_sockets[i].rx_buf, payload, copy_len);
        udp_sockets[i].rx_len      = copy_len;
        udp_sockets[i].rx_ready    = 1;
        udp_sockets[i].remote_ip   = src_ip;
        udp_sockets[i].remote_port = src_port_net;
        return;
    }
    /* No socket found for this destination port - silently drop */
}

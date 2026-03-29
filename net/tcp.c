// BlueyOS TCP - Transmission Control Protocol
// "You have to do things properly." - Bandit Heeler
// Episode ref: "Hammerbarn" - reliable, ordered, connection-oriented
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "../include/bluey.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../kernel/timer.h"
#include "tcpip.h"
#include "ip.h"
#include "tcp.h"

static tcp_socket_t tcp_sockets[TCP_MAX_SOCKETS];
static uint16_t     tcp_next_port = 49152;
static uint32_t     tcp_isn       = 0xB10EB10E;   /* initial sequence: "BLUE" */

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Generate an ISN by mixing a running LCG state with the kernel timer tick.
// This is not cryptographically secure (see SECURITY.md) but reduces the
// predictability compared to a simple counter.
static uint32_t tcp_gen_isn(void) {
    uint32_t ticks = timer_get_ticks();
    tcp_isn = tcp_isn ^ ticks;
    tcp_isn = tcp_isn * 1664525U + 1013904223U;   /* Numerical Recipes LCG */
    tcp_isn ^= (ticks << 16) | (ticks >> 16);
    return tcp_isn;
}

static tcp_socket_t *tcp_find_socket(uint32_t src_ip, uint16_t src_port,
                                     uint32_t dst_ip, uint16_t dst_port) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t *s = &tcp_sockets[i];
        if (!s->active) continue;
        if (s->remote_ip   == src_ip   &&
            s->remote_port == src_port &&
            s->local_ip    == dst_ip   &&
            s->local_port  == dst_port)
            return s;
    }
    return NULL;
}

// Build and send a TCP segment
static int tcp_send_segment(tcp_socket_t *s, uint8_t flags,
                            const uint8_t *data, uint16_t data_len) {
    const tcpip_config_t *cfg = tcpip_get_config();
    uint16_t hdr_len = (uint16_t)sizeof(tcp_hdr_t);
    uint16_t seg_len = (uint16_t)(hdr_len + data_len);

    static uint8_t buf[1500];
    if (seg_len > sizeof(buf)) return -1;

    tcp_hdr_t *hdr = (tcp_hdr_t *)buf;
    hdr->src_port    = htons(s->local_port);
    hdr->dst_port    = htons(s->remote_port);
    hdr->seq         = htonl(s->snd_nxt);
    hdr->ack         = (flags & TCP_FLAG_ACK) ? htonl(s->rcv_nxt) : 0;
    hdr->data_offset = (uint8_t)((hdr_len / 4) << 4);
    hdr->flags       = flags;
    hdr->window      = htons(TCP_WINDOW);
    hdr->checksum    = 0;
    hdr->urgent      = 0;

    if (data && data_len)
        memcpy(buf + hdr_len, data, data_len);

    // TCP checksum uses pseudo-header
    ip_pseudo_hdr_t pseudo;
    pseudo.src_ip   = cfg->ip;
    pseudo.dst_ip   = s->remote_ip;
    pseudo.zero     = 0;
    pseudo.protocol = IPPROTO_TCP;
    pseudo.length   = htons(seg_len);

    static uint8_t ckbuf[1520];
    memcpy(ckbuf, &pseudo, sizeof(pseudo));
    memcpy(ckbuf + sizeof(pseudo), buf, seg_len);
    hdr->checksum = net_checksum(ckbuf, sizeof(pseudo) + seg_len);

    return ip_send(IPPROTO_TCP, s->remote_ip, buf, seg_len);
}

// ---------------------------------------------------------------------------
// Receive buffer helpers
// ---------------------------------------------------------------------------

static void rbuf_push(tcp_socket_t *s, const uint8_t *data, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        if (s->recv_count >= TCP_RECV_BUF_SIZE) break;
        s->recv_buf[s->recv_head] = data[i];
        s->recv_head = (uint16_t)((s->recv_head + 1) % TCP_RECV_BUF_SIZE);
        s->recv_count++;
    }
}

static uint16_t rbuf_pop(tcp_socket_t *s, uint8_t *dst, uint16_t max) {
    uint16_t n = 0;
    while (n < max && s->recv_count > 0) {
        dst[n++] = s->recv_buf[s->recv_tail];
        s->recv_tail  = (uint16_t)((s->recv_tail + 1) % TCP_RECV_BUF_SIZE);
        s->recv_count--;
    }
    return n;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void tcp_init(void) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_sockets[i].active = 0;
        tcp_sockets[i].state  = TCP_CLOSED;
    }
    kprintf("[TCP]  TCP layer ready\n");
}

int tcp_connect(uint32_t dst_ip, uint16_t dst_port) {
    const tcpip_config_t *cfg = tcpip_get_config();

    // Find a free socket
    int idx = -1;
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        if (!tcp_sockets[i].active) { idx = i; break; }
    }
    if (idx < 0) return -1;

    tcp_socket_t *s = &tcp_sockets[idx];
    memset(s, 0, sizeof(*s));
    s->active      = 1;
    s->state       = TCP_SYN_SENT;
    s->local_ip    = cfg->ip;
    s->local_port  = tcp_next_port++;
    s->remote_ip   = dst_ip;
    s->remote_port = dst_port;
    s->snd_una     = tcp_gen_isn();
    s->snd_nxt     = s->snd_una;
    s->snd_wnd     = TCP_WINDOW;

    // Send SYN
    if (tcp_send_segment(s, TCP_FLAG_SYN, NULL, 0) != 0) {
        s->active = 0;
        return -1;
    }
    s->snd_nxt++;   /* SYN consumes one sequence number */

    // Poll until ESTABLISHED or timeout
    for (int t = 0; t < 3000 && s->state == TCP_SYN_SENT; t++) {
        tcpip_poll();
        for (volatile int w = 0; w < 10000; w++);
    }

    if (s->state == TCP_ESTABLISHED) return idx;

    s->active = 0;
    s->state  = TCP_CLOSED;
    return -1;
}

int tcp_send(int sock, const uint8_t *data, uint16_t len) {
    if (sock < 0 || sock >= TCP_MAX_SOCKETS) return -1;
    tcp_socket_t *s = &tcp_sockets[sock];
    if (!s->active || s->state != TCP_ESTABLISHED) return -1;

    uint16_t sent = 0;
    while (sent < len) {
        uint16_t chunk = (uint16_t)(len - sent);
        if (chunk > TCP_MSS) chunk = TCP_MSS;
        tcp_send_segment(s, TCP_FLAG_ACK | TCP_FLAG_PSH,
                         data + sent, chunk);
        s->snd_nxt += chunk;
        sent = (uint16_t)(sent + chunk);
    }
    return sent;
}

int tcp_recv(int sock, uint8_t *buf, uint16_t max_len) {
    if (sock < 0 || sock >= TCP_MAX_SOCKETS) return -1;
    tcp_socket_t *s = &tcp_sockets[sock];
    if (!s->active) return -1;

    // Wait until data arrives or connection closes
    while (s->recv_count == 0 &&
           (s->state == TCP_ESTABLISHED || s->state == TCP_CLOSE_WAIT)) {
        tcpip_poll();
        for (volatile int w = 0; w < 10000; w++);
    }

    return (int)rbuf_pop(s, buf, max_len);
}

void tcp_close(int sock) {
    if (sock < 0 || sock >= TCP_MAX_SOCKETS) return;
    tcp_socket_t *s = &tcp_sockets[sock];
    if (!s->active) return;

    if (s->state == TCP_ESTABLISHED || s->state == TCP_CLOSE_WAIT) {
        s->state = (s->state == TCP_ESTABLISHED) ? TCP_FIN_WAIT_1
                                                  : TCP_LAST_ACK;
        tcp_send_segment(s, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        s->snd_nxt++;
    }
    s->active = 0;
    s->state  = TCP_CLOSED;
}

void tcp_tick(void) {
    for (int i = 0; i < TCP_MAX_SOCKETS; i++) {
        tcp_socket_t *s = &tcp_sockets[i];
        if (!s->active) continue;
        if (s->retx_ticks > 0) s->retx_ticks--;
        if (s->retx_ticks == 0 && s->state == TCP_SYN_SENT) {
            // Retransmit SYN
            s->snd_nxt = s->snd_una;
            tcp_send_segment(s, TCP_FLAG_SYN, NULL, 0);
            s->snd_nxt++;
            s->retx_ticks = TCP_RETX_TICKS;
        }
    }
}

void tcp_handle(uint32_t src_ip, const uint8_t *data, uint16_t len) {
    if (len < sizeof(tcp_hdr_t)) return;

    const tcp_hdr_t *hdr  = (const tcp_hdr_t *)data;
    uint16_t src_port      = ntohs(hdr->src_port);
    uint16_t dst_port      = ntohs(hdr->dst_port);
    uint8_t  flags         = hdr->flags;
    uint32_t seq           = ntohl(hdr->seq);
    uint32_t ack_num       = ntohl(hdr->ack);
    uint8_t  hdr_words     = (hdr->data_offset >> 4);
    uint16_t hdr_bytes     = (uint16_t)(hdr_words * 4);

    const tcpip_config_t *cfg = tcpip_get_config();

    const uint8_t *payload    = data + hdr_bytes;
    uint16_t       payload_len = (uint16_t)(len - hdr_bytes);

    tcp_socket_t *s = tcp_find_socket(src_ip, src_port, cfg->ip, dst_port);
    if (!s) return;

    switch (s->state) {
        case TCP_SYN_SENT:
            if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) ==
                         (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
                // SYN-ACK received
                s->rcv_nxt = seq + 1;
                s->snd_una = ack_num;
                s->state   = TCP_ESTABLISHED;
                // Send ACK
                tcp_send_segment(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;

        case TCP_ESTABLISHED:
        case TCP_CLOSE_WAIT:
            if (flags & TCP_FLAG_ACK) s->snd_una = ack_num;

            // Deliver payload to receive buffer
            if (payload_len > 0) {
                rbuf_push(s, payload, payload_len);
                s->rcv_nxt += payload_len;
                // Send ACK for received data
                tcp_send_segment(s, TCP_FLAG_ACK, NULL, 0);
            }

            if (flags & TCP_FLAG_FIN) {
                s->rcv_nxt++;
                if (s->state == TCP_ESTABLISHED)
                    s->state = TCP_CLOSE_WAIT;
                tcp_send_segment(s, TCP_FLAG_ACK, NULL, 0);
            }
            break;

        case TCP_FIN_WAIT_1:
            if (flags & TCP_FLAG_ACK) {
                s->state = TCP_FIN_WAIT_2;
            }
            if (flags & TCP_FLAG_FIN) {
                s->rcv_nxt++;
                tcp_send_segment(s, TCP_FLAG_ACK, NULL, 0);
                s->state = TCP_TIME_WAIT;
            }
            break;

        case TCP_FIN_WAIT_2:
            if (flags & TCP_FLAG_FIN) {
                s->rcv_nxt++;
                tcp_send_segment(s, TCP_FLAG_ACK, NULL, 0);
                s->state = TCP_TIME_WAIT;
            }
            break;

        case TCP_LAST_ACK:
            if (flags & TCP_FLAG_ACK) {
                s->active = 0;
                s->state  = TCP_CLOSED;
            }
            break;

        case TCP_TIME_WAIT:
            s->active = 0;
            s->state  = TCP_CLOSED;
            break;

        default:
            break;
    }
}

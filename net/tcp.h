#pragma once
// BlueyOS TCP - Transmission Control Protocol
// Episode ref: "Hammerbarn" - reliable connections, everything in order
// "You have to do things properly." - Bandit
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "tcpip.h"

#define TCP_MAX_SOCKETS    8
#define TCP_SEND_BUF_SIZE  4096
#define TCP_RECV_BUF_SIZE  4096
#define TCP_MSS            1460   /* Maximum Segment Size */
#define TCP_WINDOW         4096
#define TCP_RETX_TICKS     500    /* retransmit after ~500 timer ticks */

// TCP connection states (RFC 793)
typedef enum {
    TCP_CLOSED = 0,
    TCP_LISTEN,
    TCP_SYN_SENT,
    TCP_SYN_RECEIVED,
    TCP_ESTABLISHED,
    TCP_FIN_WAIT_1,
    TCP_FIN_WAIT_2,
    TCP_CLOSE_WAIT,
    TCP_CLOSING,
    TCP_LAST_ACK,
    TCP_TIME_WAIT
} tcp_state_t;

typedef struct {
    tcp_state_t state;
    int         active;

    uint32_t    local_ip;
    uint16_t    local_port;
    uint32_t    remote_ip;
    uint16_t    remote_port;

    uint32_t    snd_una;         /* oldest unacknowledged seq */
    uint32_t    snd_nxt;         /* next seq to send */
    uint32_t    snd_wnd;         /* remote receive window */
    uint32_t    rcv_nxt;         /* next seq expected from remote */

    uint8_t     send_buf[TCP_SEND_BUF_SIZE];
    uint16_t    send_len;

    uint8_t     recv_buf[TCP_RECV_BUF_SIZE];
    uint16_t    recv_head;
    uint16_t    recv_tail;
    uint16_t    recv_count;

    uint32_t    retx_ticks;
} tcp_socket_t;

void tcp_init(void);

// Connect to remote host (blocking-style: sends SYN and polls for ESTABLISHED)
// Returns socket index on success, -1 on failure.
int tcp_connect(uint32_t dst_ip, uint16_t dst_port);

// Send data on an established connection
int tcp_send(int sock, const uint8_t *data, uint16_t len);

// Receive data (blocks until data arrives or connection closes)
int tcp_recv(int sock, uint8_t *buf, uint16_t max_len);

// Close a connection (sends FIN)
void tcp_close(int sock);

// Timer tick: call from scheduler or idle loop for retransmission
void tcp_tick(void);

// Called by ip_handle() for incoming TCP segments
void tcp_handle(uint32_t src_ip, const uint8_t *data, uint16_t len);

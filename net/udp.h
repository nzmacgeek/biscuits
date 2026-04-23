#pragma once
// BlueyOS UDP - User Datagram Protocol
// Episode ref: "Stories" - quick messages, no fuss
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
#include "../include/types.h"
#include "tcpip.h"

#define UDP_MAX_SOCKETS   8
#define UDP_RECV_BUF_SIZE 1472   /* 1500 - 20 (IP) - 8 (UDP) */

typedef struct {
    uint16_t local_port;
    uint32_t remote_ip;          /* 0 = unconnected */
    uint16_t remote_port;
    int      active;
    uint8_t  rx_buf[UDP_RECV_BUF_SIZE];
    uint16_t rx_len;
    int      rx_ready;
} udp_socket_t;

void udp_init(void);

// Open a UDP socket bound to local_port (0 = ephemeral)
int udp_open(uint16_t local_port);

// Close a UDP socket
void udp_close(int sock);

// Send a UDP datagram
int udp_send(int sock, uint32_t dst_ip, uint16_t dst_port,
             const uint8_t *data, uint16_t len);

// Non-blocking receive; returns number of bytes or 0 if nothing waiting.
// src_ip and src_port are filled in if non-NULL.
int udp_recv(int sock, uint8_t *buf, uint16_t max_len,
             uint32_t *src_ip, uint16_t *src_port);

// True when a datagram is pending for this UDP socket.
int udp_has_data(int sock);

// Called by ip_handle() for incoming UDP packets
void udp_handle(uint32_t src_ip, const uint8_t *data, uint16_t len);

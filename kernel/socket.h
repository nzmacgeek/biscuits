#pragma once

#include "../include/types.h"

#define BLUEY_AF_UNIX      1
#define BLUEY_AF_NETCTL    2  // Network control plane (Netlink-inspired)
#define BLUEY_AF_INET      3
#define BLUEY_SOCK_STREAM  1
#define BLUEY_SOCK_DGRAM   2
#define BLUEY_SOCK_NETCTL  3  // Message-oriented netctl socket

void socket_init(void);
int  socket_create(int domain, int type, int protocol);
int  socket_bind(int socket_id, const char *path);
int  socket_listen(int socket_id, int backlog);
int  socket_connect(int socket_id, const char *path);
int  socket_accept(int socket_id);
int  socket_add_ref(int socket_id);
int  socket_close(int socket_id);
int  socket_read(int socket_id, uint8_t *buf, size_t len);
int  socket_write(int socket_id, const uint8_t *buf, size_t len);
int  socket_is_readable(int socket_id);
int  socket_is_writable(int socket_id);

// NETCTL socket helpers
int  socket_is_netctl(int socket_id);
int  socket_netctl_send(int socket_id, const void *msg, size_t len);
int  socket_netctl_recv(int socket_id, void *buf, size_t len);

// AF_INET datagram helpers (backed by UDP layer)
int  socket_is_inet(int socket_id);
int  socket_inet_bind(int socket_id, uint32_t ip, uint16_t port);
int  socket_inet_sendto(int socket_id, uint32_t dst_ip, uint16_t dst_port,
						const void *msg, size_t len);
int  socket_inet_recvfrom(int socket_id, void *buf, size_t len,
						 uint32_t *src_ip, uint16_t *src_port);

// AF_UNIX datagram helpers
int  socket_is_unix_dgram(int socket_id);
int  socket_unix_sendto(int socket_id, const char *dest_path,
					   const void *msg, size_t len);
int  socket_unix_recvfrom(int socket_id, void *buf, size_t len,
						char *src_path, size_t src_path_size);
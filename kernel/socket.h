#pragma once

#include "../include/types.h"

#define BLUEY_AF_UNIX      1
#define BLUEY_SOCK_STREAM  1

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
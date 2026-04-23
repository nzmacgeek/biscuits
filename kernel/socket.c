#include "socket.h"
#include "netctl.h"

#include "../net/udp.h"
#include "../net/icmp.h"

#include "../lib/string.h"

#define SOCKET_MAX_COUNT       16
#define SOCKET_PATH_LEN        108
#define SOCKET_BACKLOG_MAX     8
#define SOCKET_BUFFER_SIZE     4096

#define SOCKET_EINVAL        22
#define SOCKET_EAGAIN        11
#define SOCKET_EADDRINUSE    98
#define SOCKET_ECONNREFUSED  111

#define SOCKET_STATE_INIT            0
#define SOCKET_STATE_BOUND           1
#define SOCKET_STATE_LISTENING       2
#define SOCKET_STATE_CONNECT_PENDING 3
#define SOCKET_STATE_CONNECTED       4

typedef struct {
    int      used;
    int      refcount;
    int      domain;
    int      type;
    int      protocol;
    int      state;
    int      inet_udp_id; // for BLUEY_AF_INET + DGRAM -> udp socket index
    int      inet_icmp_id; // for BLUEY_AF_INET + RAW(ICMP) -> icmp socket index
    int      peer_id;
    int      listener_id;
    int      peer_closed;
    int      backlog;
    int      pending_ids[SOCKET_BACKLOG_MAX];
    uint32_t pending_head;
    uint32_t pending_tail;
    uint32_t pending_count;
    int      netctl_id;    // For BLUEY_AF_NETCTL: underlying netctl socket id; -1 otherwise
    char     path[SOCKET_PATH_LEN];
    char     rx_src_path[SOCKET_PATH_LEN];
    uint8_t  rx_buf[SOCKET_BUFFER_SIZE];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;
} bluey_socket_t;

static bluey_socket_t socket_table[SOCKET_MAX_COUNT];

static void socket_reset(bluey_socket_t *sock) {
    if (!sock) return;
    memset(sock, 0, sizeof(*sock));
    sock->peer_id = -1;
    sock->listener_id = -1;
    sock->netctl_id = -1;
    sock->inet_udp_id = -1;
    sock->inet_icmp_id = -1;
    sock->rx_src_path[0] = '\0';
}

static int socket_valid_id(int socket_id) {
    return socket_id >= 0 && socket_id < SOCKET_MAX_COUNT && socket_table[socket_id].used;
}

static int socket_alloc(void) {
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        if (!socket_table[i].used) {
            socket_reset(&socket_table[i]);
            socket_table[i].used = 1;
            return i;
        }
    }
    return -1;
}

static void socket_maybe_free(int socket_id) {
    bluey_socket_t *sock;

    if (!socket_valid_id(socket_id)) return;
    sock = &socket_table[socket_id];
    if (sock->refcount > 0) return;
    if (sock->state == SOCKET_STATE_CONNECT_PENDING) return;
    if (sock->state == SOCKET_STATE_LISTENING && sock->pending_count > 0) return;
    socket_reset(sock);
}

static int socket_find_listener(const char *path) {
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        if (!socket_table[i].used) continue;
        if (socket_table[i].state != SOCKET_STATE_LISTENING) continue;
        if (strcmp(socket_table[i].path, path) == 0) return i;
    }
    return -1;
}

static int socket_path_in_use(const char *path) {
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        if (!socket_table[i].used) continue;
        if (socket_table[i].state != SOCKET_STATE_BOUND &&
            socket_table[i].state != SOCKET_STATE_LISTENING) continue;
        if (strcmp(socket_table[i].path, path) == 0) return 1;
    }
    return 0;
}

void socket_init(void) {
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) socket_reset(&socket_table[i]);
}

int socket_create(int domain, int type, int protocol) {
    int socket_id;
    bluey_socket_t *sock;

    // Handle NETCTL family: allocate a socket_table entry AND a netctl socket
    // so that all generic socket APIs (add_ref, close, etc.) work correctly
    // and NETCTL ids cannot collide with AF_UNIX ids.
    if (domain == BLUEY_AF_NETCTL) {
        if (type != BLUEY_SOCK_NETCTL) return -1;
        socket_id = socket_alloc();
        if (socket_id < 0) return -1;
        sock = &socket_table[socket_id];
        sock->domain = domain;
        sock->type = type;
        sock->protocol = protocol;
        sock->state = SOCKET_STATE_INIT;
        sock->netctl_id = netctl_socket_create(protocol);
        if (sock->netctl_id < 0) {
            // Roll back: clear the socket_table slot (socket_reset sets used=0)
            socket_reset(sock);
            return -1;
        }
        return socket_id;
    }

    // Handle UNIX domain sockets (stream and datagram)
    if (domain == BLUEY_AF_UNIX) {
        if (type != BLUEY_SOCK_STREAM && type != BLUEY_SOCK_DGRAM) return -1;
        socket_id = socket_alloc();
        if (socket_id < 0) return -1;
        sock = &socket_table[socket_id];
        sock->domain = domain;
        sock->type = type;
        sock->protocol = protocol;
        sock->state = SOCKET_STATE_INIT;
        return socket_id;
    }

    // Handle INET datagram sockets (UDP-backed)
    if (domain == BLUEY_AF_INET && type == BLUEY_SOCK_DGRAM) {
        socket_id = socket_alloc();
        if (socket_id < 0) return -1;
        sock = &socket_table[socket_id];
        sock->domain = domain;
        sock->type = type;
        sock->protocol = protocol;
        sock->state = SOCKET_STATE_INIT;
        sock->inet_udp_id = udp_open(0); // allocate UDP socket (ephemeral port)
        if (sock->inet_udp_id < 0) {
            socket_reset(sock);
            return -1;
        }
        return socket_id;
    }

    // Handle INET raw sockets (ICMP-backed)
    if (domain == BLUEY_AF_INET && type == BLUEY_SOCK_RAW) {
        socket_id = socket_alloc();
        if (socket_id < 0) return -1;
        sock = &socket_table[socket_id];
        sock->domain = domain;
        sock->type = type;
        sock->protocol = protocol;
        sock->state = SOCKET_STATE_INIT;
        sock->inet_icmp_id = icmp_open(protocol);
        if (sock->inet_icmp_id < 0) {
            socket_reset(sock);
            return -1;
        }
        return socket_id;
    }

    return -1;
}

int socket_add_ref(int socket_id) {
    if (!socket_valid_id(socket_id)) return -1;
    socket_table[socket_id].refcount++;
    return 0;
}

int socket_bind(int socket_id, const char *path) {
    bluey_socket_t *sock;
    size_t path_len;

    if (!socket_valid_id(socket_id)) return -SOCKET_EINVAL;

    sock = &socket_table[socket_id];

    // For NETCTL sockets, bind subscribes to multicast groups.
    // The "path" parameter carries a pointer to a uint32_t group mask.
    if (sock->domain == BLUEY_AF_NETCTL) {
        if (!path) return -SOCKET_EINVAL;
        uint32_t groups = *(const uint32_t *)path;
        return netctl_socket_bind(sock->netctl_id, groups);
    }

    // For UNIX domain sockets, use path binding
    if (!path || !path[0]) return -SOCKET_EINVAL;
    if (socket_path_in_use(path)) return -SOCKET_EADDRINUSE;

    path_len = strlen(path);
    if (path_len >= SOCKET_PATH_LEN) return -SOCKET_EINVAL;

    if (sock->state != SOCKET_STATE_INIT && sock->state != SOCKET_STATE_BOUND) return -SOCKET_EINVAL;

    strncpy(sock->path, path, sizeof(sock->path) - 1);
    sock->path[sizeof(sock->path) - 1] = '\0';
    sock->state = SOCKET_STATE_BOUND;
    return 0;
}

int socket_listen(int socket_id, int backlog) {
    bluey_socket_t *sock;

    if (!socket_valid_id(socket_id)) return -1;
    sock = &socket_table[socket_id];
    if (sock->state != SOCKET_STATE_BOUND) return -1;

    if (backlog <= 0) backlog = 1;
    if (backlog > SOCKET_BACKLOG_MAX) backlog = SOCKET_BACKLOG_MAX;
    sock->backlog = backlog;
    sock->state = SOCKET_STATE_LISTENING;
    return 0;
}

int socket_connect(int socket_id, const char *path) {
    bluey_socket_t *sock;
    bluey_socket_t *listener;
    int listener_id;

    if (!socket_valid_id(socket_id) || !path) return -SOCKET_EINVAL;
    sock = &socket_table[socket_id];
    if (sock->state != SOCKET_STATE_INIT && sock->state != SOCKET_STATE_BOUND) return -SOCKET_EINVAL;

    listener_id = socket_find_listener(path);
    if (listener_id < 0) return -SOCKET_ECONNREFUSED;

    listener = &socket_table[listener_id];
    if (listener->pending_count >= (uint32_t)listener->backlog) return -SOCKET_EAGAIN;

    listener->pending_ids[listener->pending_tail] = socket_id;
    listener->pending_tail = (listener->pending_tail + 1u) % SOCKET_BACKLOG_MAX;
    listener->pending_count++;

    sock->listener_id = listener_id;
    sock->state = SOCKET_STATE_CONNECT_PENDING;
    return 0;
}

int socket_accept(int socket_id) {
    bluey_socket_t *listener;
    bluey_socket_t *client;
    bluey_socket_t *server;
    int client_id;
    int server_id;

    if (!socket_valid_id(socket_id)) return -1;
    listener = &socket_table[socket_id];
    if (listener->state != SOCKET_STATE_LISTENING) return -1;
    if (listener->pending_count == 0) return -1;

    client_id = listener->pending_ids[listener->pending_head];
    listener->pending_head = (listener->pending_head + 1u) % SOCKET_BACKLOG_MAX;
    listener->pending_count--;

    if (!socket_valid_id(client_id)) return -1;
    client = &socket_table[client_id];

    server_id = socket_alloc();
    if (server_id < 0) return -1;

    server = &socket_table[server_id];
    server->domain = listener->domain;
    server->type = listener->type;
    server->protocol = listener->protocol;
    server->state = SOCKET_STATE_CONNECTED;
    server->peer_id = client_id;

    client->state = SOCKET_STATE_CONNECTED;
    client->peer_id = server_id;
    client->listener_id = -1;
    return server_id;
}

int socket_close(int socket_id) {
    bluey_socket_t *sock;
    int peer_id;

    if (!socket_valid_id(socket_id)) return -1;
    sock = &socket_table[socket_id];
    if (sock->refcount > 0) {
        sock->refcount--;
        if (sock->refcount > 0) return 0;
    }

    // For NETCTL sockets, release the underlying netctl socket
    if (sock->domain == BLUEY_AF_NETCTL && sock->netctl_id >= 0) {
        netctl_socket_close(sock->netctl_id);
        sock->netctl_id = -1;
        socket_reset(sock);
        return 0;
    }

    if (sock->domain == BLUEY_AF_INET && sock->type == BLUEY_SOCK_DGRAM && sock->inet_udp_id >= 0) {
        udp_close(sock->inet_udp_id);
        sock->inet_udp_id = -1;
    }
    if (sock->domain == BLUEY_AF_INET && sock->type == BLUEY_SOCK_RAW && sock->inet_icmp_id >= 0) {
        icmp_close(sock->inet_icmp_id);
        sock->inet_icmp_id = -1;
    }

    peer_id = sock->peer_id;
    if (peer_id >= 0 && socket_valid_id(peer_id)) {
        socket_table[peer_id].peer_id = -1;
        socket_table[peer_id].peer_closed = 1;
    }

    sock->peer_id = -1;
    sock->listener_id = -1;
    sock->path[0] = '\0';
    sock->state = SOCKET_STATE_INIT;
    socket_maybe_free(socket_id);
    return 0;
}

int socket_read(int socket_id, uint8_t *buf, size_t len) {
    bluey_socket_t *sock;
    size_t copied = 0;

    if (!socket_valid_id(socket_id) || !buf) return -1;
    sock = &socket_table[socket_id];
    if (sock->state != SOCKET_STATE_CONNECTED) return -1;

    if (sock->rx_count == 0) return sock->peer_closed ? 0 : -1;

    while (copied < len && sock->rx_count > 0) {
        buf[copied++] = sock->rx_buf[sock->rx_tail];
        sock->rx_tail = (sock->rx_tail + 1u) % SOCKET_BUFFER_SIZE;
        sock->rx_count--;
    }

    return (int)copied;
}

int socket_write(int socket_id, const uint8_t *buf, size_t len) {
    bluey_socket_t *sock;
    bluey_socket_t *peer;
    size_t written = 0;

    if (!socket_valid_id(socket_id) || !buf) return -1;
    sock = &socket_table[socket_id];
    if (sock->state != SOCKET_STATE_CONNECTED) return -1;
    if (!socket_valid_id(sock->peer_id)) return -1;

    peer = &socket_table[sock->peer_id];
    while (written < len && peer->rx_count < SOCKET_BUFFER_SIZE) {
        peer->rx_buf[peer->rx_head] = buf[written++];
        peer->rx_head = (peer->rx_head + 1u) % SOCKET_BUFFER_SIZE;
        peer->rx_count++;
    }

    return written > 0 ? (int)written : -1;
}

int socket_is_readable(int socket_id) {
    bluey_socket_t *sock;

    if (!socket_valid_id(socket_id)) return 0;
    sock = &socket_table[socket_id];
    if (sock->domain == BLUEY_AF_INET && sock->type == BLUEY_SOCK_DGRAM && sock->inet_udp_id >= 0) {
        return udp_has_data(sock->inet_udp_id);
    }
    if (sock->domain == BLUEY_AF_INET && sock->type == BLUEY_SOCK_RAW && sock->inet_icmp_id >= 0) {
        return icmp_has_data(sock->inet_icmp_id);
    }
    if (sock->state == SOCKET_STATE_LISTENING) return sock->pending_count > 0;
    if (sock->state == SOCKET_STATE_CONNECTED) return sock->rx_count > 0 || sock->peer_closed;
    return 0;
}

int socket_is_writable(int socket_id) {
    bluey_socket_t *sock;

    if (!socket_valid_id(socket_id)) return 0;
    sock = &socket_table[socket_id];
    if (sock->state != SOCKET_STATE_CONNECTED) return 0;
    if (!socket_valid_id(sock->peer_id)) return 0;
    return socket_table[sock->peer_id].rx_count < SOCKET_BUFFER_SIZE;
}

int socket_is_netctl(int socket_id) {
    if (!socket_valid_id(socket_id)) return 0;
    return socket_table[socket_id].domain == BLUEY_AF_NETCTL;
}

int socket_netctl_send(int socket_id, const void *msg, size_t len) {
    if (!socket_valid_id(socket_id)) return -1;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_NETCTL || sock->netctl_id < 0) return -1;
    return netctl_socket_send(sock->netctl_id, msg, len);
}

int socket_netctl_recv(int socket_id, void *buf, size_t len) {
    if (!socket_valid_id(socket_id)) return -1;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_NETCTL || sock->netctl_id < 0) return -1;
    return netctl_socket_recv(sock->netctl_id, buf, len);
}

int socket_is_inet(int socket_id) {
    if (!socket_valid_id(socket_id)) return 0;
    bluey_socket_t *sock = &socket_table[socket_id];
    return sock->domain == BLUEY_AF_INET && sock->type == BLUEY_SOCK_DGRAM;
}

int socket_inet_bind(int socket_id, uint32_t ip, uint16_t port) {
    if (!socket_valid_id(socket_id)) return -SOCKET_EINVAL;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_INET || sock->type != BLUEY_SOCK_DGRAM) return -SOCKET_EINVAL;
    if (sock->inet_udp_id >= 0) udp_close(sock->inet_udp_id);
    sock->inet_udp_id = udp_open(port);
    return sock->inet_udp_id >= 0 ? 0 : -1;
}

int socket_inet_sendto(int socket_id, uint32_t dst_ip, uint16_t dst_port,
                        const void *msg, size_t len) {
    if (!socket_valid_id(socket_id) || !msg) return -1;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_INET || sock->type != BLUEY_SOCK_DGRAM) return -1;
    if (sock->inet_udp_id < 0) return -1;
    return udp_send(sock->inet_udp_id, dst_ip, dst_port, (const uint8_t*)msg, (uint16_t)len);
}

int socket_inet_recvfrom(int socket_id, void *buf, size_t len,
                         uint32_t *src_ip, uint16_t *src_port) {
    if (!socket_valid_id(socket_id) || !buf) return -1;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_INET || sock->type != BLUEY_SOCK_DGRAM) return -1;
    if (sock->inet_udp_id < 0) return -1;
    return udp_recv(sock->inet_udp_id, (uint8_t*)buf, (uint16_t)len, src_ip, src_port);
}

int socket_is_inet_raw(int socket_id) {
    if (!socket_valid_id(socket_id)) return 0;
    bluey_socket_t *sock = &socket_table[socket_id];
    return sock->domain == BLUEY_AF_INET && sock->type == BLUEY_SOCK_RAW;
}

int socket_inet_raw_bind(int socket_id, uint32_t ip) {
    if (!socket_valid_id(socket_id)) return -SOCKET_EINVAL;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_INET || sock->type != BLUEY_SOCK_RAW) return -SOCKET_EINVAL;
    if (ip != 0) return -SOCKET_EINVAL; // raw ICMP bind supports INADDR_ANY only
    return 0;
}

int socket_inet_raw_sendto(int socket_id, uint32_t dst_ip,
                           const void *msg, size_t len) {
    if (!socket_valid_id(socket_id) || !msg) return -1;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_INET || sock->type != BLUEY_SOCK_RAW) return -1;
    if (sock->inet_icmp_id < 0) return -1;
    if (len > 0xFFFFu) return -1;
    return icmp_send(sock->inet_icmp_id, dst_ip, (const uint8_t*)msg, (uint16_t)len);
}

int socket_inet_raw_recvfrom(int socket_id, void *buf, size_t len,
                             uint32_t *src_ip) {
    if (!socket_valid_id(socket_id) || !buf) return -1;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_INET || sock->type != BLUEY_SOCK_RAW) return -1;
    if (sock->inet_icmp_id < 0) return -1;
    if (len > 0xFFFFu) len = 0xFFFFu;
    return icmp_recv(sock->inet_icmp_id, (uint8_t*)buf, (uint16_t)len, src_ip);
}

int socket_is_unix_dgram(int socket_id) {
    if (!socket_valid_id(socket_id)) return 0;
    bluey_socket_t *sock = &socket_table[socket_id];
    return sock->domain == BLUEY_AF_UNIX && sock->type == BLUEY_SOCK_DGRAM;
}

int socket_unix_sendto(int socket_id, const char *dest_path,
                       const void *msg, size_t len) {
    if (!socket_valid_id(socket_id) || !dest_path || !msg) return -1;
    bluey_socket_t *src = &socket_table[socket_id];
    // Find destination socket bound to dest_path
    int dest_id = -1;
    for (int i = 0; i < SOCKET_MAX_COUNT; i++) {
        if (!socket_table[i].used) continue;
        if (socket_table[i].domain != BLUEY_AF_UNIX) continue;
        if (socket_table[i].type != BLUEY_SOCK_DGRAM) continue;
        if (socket_table[i].state != SOCKET_STATE_BOUND) continue;
        if (strcmp(socket_table[i].path, dest_path) == 0) { dest_id = i; break; }
    }
    if (dest_id < 0) return -1;
    bluey_socket_t *dst = &socket_table[dest_id];
    // Copy up to buffer capacity
    size_t copy_len = len;
    if (copy_len > SOCKET_BUFFER_SIZE) copy_len = SOCKET_BUFFER_SIZE;
    memcpy(dst->rx_buf, msg, copy_len);
    dst->rx_count = (uint32_t)copy_len;
    // store source path for recvfrom
    if (src->path[0]) strncpy(dst->rx_src_path, src->path, SOCKET_PATH_LEN - 1);
    else dst->rx_src_path[0] = '\0';
    return (int)copy_len;
}

int socket_unix_recvfrom(int socket_id, void *buf, size_t len,
                        char *src_path, size_t src_path_size) {
    if (!socket_valid_id(socket_id) || !buf) return -1;
    bluey_socket_t *sock = &socket_table[socket_id];
    if (sock->domain != BLUEY_AF_UNIX || sock->type != BLUEY_SOCK_DGRAM) return -1;
    if (sock->rx_count == 0) return -1;
    size_t copy_len = sock->rx_count;
    if (copy_len > len) copy_len = len;
    memcpy(buf, sock->rx_buf, copy_len);
    if (src_path && src_path_size > 0) {
        size_t p = strlen(sock->rx_src_path);
        if (p >= src_path_size) p = src_path_size - 1;
        if (p > 0) {
            memcpy(src_path, sock->rx_src_path, p);
        }
        src_path[p] = '\0';
    }
    sock->rx_count = 0;
    sock->rx_src_path[0] = '\0';
    return (int)copy_len;
}

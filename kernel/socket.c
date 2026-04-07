#include "socket.h"
#include "netctl.h"

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
    int      peer_id;
    int      listener_id;
    int      peer_closed;
    int      backlog;
    int      pending_ids[SOCKET_BACKLOG_MAX];
    uint32_t pending_head;
    uint32_t pending_tail;
    uint32_t pending_count;
    char     path[SOCKET_PATH_LEN];
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

    // Handle NETCTL family separately
    if (domain == BLUEY_AF_NETCTL) {
        if (type != BLUEY_SOCK_NETCTL) return -1;
        // Delegate to netctl subsystem
        return netctl_socket_create(protocol);
    }

    // Handle UNIX domain sockets
    if (domain != BLUEY_AF_UNIX || type != BLUEY_SOCK_STREAM) return -1;

    socket_id = socket_alloc();
    if (socket_id < 0) return -1;

    sock = &socket_table[socket_id];
    sock->domain = domain;
    sock->type = type;
    sock->protocol = protocol;
    sock->state = SOCKET_STATE_INIT;
    return socket_id;
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

    // For NETCTL sockets, delegate to netctl_socket_bind
    // The "path" parameter is reinterpreted as a pointer to uint32_t groups
    if (sock->domain == BLUEY_AF_NETCTL) {
        // For netctl, bind is used to subscribe to multicast groups
        // addr is actually a pointer to uint32_t containing group mask
        if (!path) return -SOCKET_EINVAL;
        uint32_t groups = *(const uint32_t *)path;
        return netctl_socket_bind(socket_id, groups);
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
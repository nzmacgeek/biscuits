// BlueyOS Network Control Plane - "Walkies with Netlink"
// Episode ref: "Walkies" - Sometimes you need the right way to communicate
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#include "netctl.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "kheap.h"

// ============================================================================
// Socket Management
// ============================================================================

#define NETCTL_MAX_SOCKETS      16
#define NETCTL_SOCKET_BUFFER    8192

typedef struct {
    int      used;
    int      refcount;
    uint32_t groups;           // Subscribed multicast groups (bitmask)
    uint8_t  rx_buf[NETCTL_SOCKET_BUFFER];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint32_t rx_count;
} netctl_socket_t;

static netctl_socket_t socket_table[NETCTL_MAX_SOCKETS];

// ============================================================================
// Initialization
// ============================================================================

void netctl_init(void) {
    memset(socket_table, 0, sizeof(socket_table));
    kprintf("[NETCTL] Network control plane initialized (protocol v%d)\n",
            NETCTL_PROTOCOL_VERSION);
}

// ============================================================================
// Socket Operations
// ============================================================================

static int netctl_socket_valid(int socket_id) {
    return socket_id >= 0 && socket_id < NETCTL_MAX_SOCKETS &&
           socket_table[socket_id].used;
}

int netctl_socket_create(int protocol) {
    // Protocol parameter reserved for future use (e.g., different netctl protocols)
    (void)protocol;

    for (int i = 0; i < NETCTL_MAX_SOCKETS; i++) {
        if (!socket_table[i].used) {
            memset(&socket_table[i], 0, sizeof(netctl_socket_t));
            socket_table[i].used = 1;
            socket_table[i].refcount = 1;
            socket_table[i].groups = NETCTL_GROUP_NONE;
            return i;
        }
    }
    return -1;  // No free sockets
}

int netctl_socket_bind(int socket_id, uint32_t groups) {
    // Bind subscribes to multicast groups
    if (!netctl_socket_valid(socket_id)) return -1;
    socket_table[socket_id].groups = groups & NETCTL_GROUP_ALL;
    return 0;
}

int netctl_socket_close(int socket_id) {
    if (!netctl_socket_valid(socket_id)) return -1;

    netctl_socket_t *sock = &socket_table[socket_id];
    sock->refcount--;

    if (sock->refcount <= 0) {
        memset(sock, 0, sizeof(netctl_socket_t));
    }
    return 0;
}

int netctl_socket_send(int socket_id, const void *msg, size_t len) {
    // User space sends requests to kernel via this path
    if (!netctl_socket_valid(socket_id) || !msg || len == 0) return -1;

    // For now, process synchronously and put response in RX buffer
    // In a real implementation, this might queue for async processing
    uint8_t response_buf[NETCTL_SOCKET_BUFFER];
    int response_len = netctl_process_message(msg, len, response_buf,
                                               sizeof(response_buf));

    if (response_len <= 0) return response_len;

    netctl_socket_t *sock = &socket_table[socket_id];

    // Copy response to RX buffer
    size_t to_copy = (size_t)response_len;
    if (to_copy > NETCTL_SOCKET_BUFFER - sock->rx_count) {
        to_copy = NETCTL_SOCKET_BUFFER - sock->rx_count;
    }

    for (size_t i = 0; i < to_copy; i++) {
        sock->rx_buf[sock->rx_head] = response_buf[i];
        sock->rx_head = (sock->rx_head + 1) % NETCTL_SOCKET_BUFFER;
        sock->rx_count++;
    }

    return (int)to_copy;
}

int netctl_socket_recv(int socket_id, void *buf, size_t len) {
    if (!netctl_socket_valid(socket_id) || !buf || len == 0) return -1;

    netctl_socket_t *sock = &socket_table[socket_id];
    if (sock->rx_count == 0) return 0;  // No data available

    size_t copied = 0;
    while (copied < len && sock->rx_count > 0) {
        ((uint8_t *)buf)[copied++] = sock->rx_buf[sock->rx_tail];
        sock->rx_tail = (sock->rx_tail + 1) % NETCTL_SOCKET_BUFFER;
        sock->rx_count--;
    }

    return (int)copied;
}

// ============================================================================
// Message Construction Helpers
// ============================================================================

void netctl_msg_init(netctl_msg_header_t *hdr, uint16_t type, uint16_t flags,
                     uint32_t seq, uint32_t pid) {
    if (!hdr) return;
    memset(hdr, 0, sizeof(*hdr));
    hdr->msg_len = sizeof(netctl_msg_header_t);
    hdr->msg_type = type;
    hdr->msg_flags = flags;
    hdr->msg_seq = seq;
    hdr->msg_pid = pid;
    hdr->msg_version = NETCTL_PROTOCOL_VERSION;
}

int netctl_msg_add_attr(void *msg, size_t msg_maxlen, uint16_t type,
                        const void *data, uint16_t len) {
    if (!msg) return -1;

    netctl_msg_header_t *hdr = (netctl_msg_header_t *)msg;
    uint32_t current_len = hdr->msg_len;
    uint16_t attr_total_len = NETCTL_ATTR_LENGTH(len);

    // Check if there's room for the attribute
    if (current_len + attr_total_len > msg_maxlen) return -1;

    // Write attribute header
    netctl_attr_header_t *attr = (netctl_attr_header_t *)
                                  ((uint8_t *)msg + current_len);
    attr->attr_len = NETCTL_ATTR_HDRLEN + len;
    attr->attr_type = type;

    // Write attribute data
    if (data && len > 0) {
        memcpy((uint8_t *)attr + NETCTL_ATTR_HDRLEN, data, len);
    }

    // Zero-pad to alignment
    uint16_t padding = attr_total_len - attr->attr_len;
    if (padding > 0) {
        memset((uint8_t *)attr + attr->attr_len, 0, padding);
    }

    // Update message length
    hdr->msg_len += attr_total_len;
    return 0;
}

// ============================================================================
// Message Processing (Kernel-side Request Handling)
// ============================================================================

// Helper: Build error response
static int netctl_build_error(void *buf, size_t maxlen, uint32_t seq,
                              int32_t error_code, const char *error_msg) {
    if (maxlen < sizeof(netctl_msg_header_t) + sizeof(netctl_error_payload_t)) {
        return -1;
    }

    netctl_msg_header_t *hdr = (netctl_msg_header_t *)buf;
    netctl_msg_init(hdr, NETCTL_MSG_ERROR, NETCTL_FLAG_ERROR, seq, 0);

    netctl_error_payload_t *payload = (netctl_error_payload_t *)
                                       ((uint8_t *)buf + sizeof(*hdr));
    payload->error_code = error_code;
    payload->bad_seq = seq;
    strncpy(payload->error_msg, error_msg, sizeof(payload->error_msg) - 1);
    payload->error_msg[sizeof(payload->error_msg) - 1] = '\0';

    hdr->msg_len = sizeof(*hdr) + sizeof(*payload);
    return (int)hdr->msg_len;
}

// Helper: Process GET_VERSION request
static int netctl_handle_get_version(const netctl_msg_header_t *req,
                                      void *response, size_t response_maxlen) {
    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_GET_VERSION, 0, req->msg_seq, 0);

    uint16_t version = NETCTL_PROTOCOL_VERSION;
    if (netctl_msg_add_attr(response, response_maxlen,
                            NETCTL_ATTR_IFINDEX,  // Reusing as generic value
                            &version, sizeof(version)) < 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Buffer too small");
    }

    return (int)resp->msg_len;
}

// Main message dispatcher
int netctl_process_message(const void *msg, size_t len, void *response,
                           size_t response_maxlen) {
    if (!msg || len < sizeof(netctl_msg_header_t) || !response) {
        return -1;
    }

    const netctl_msg_header_t *hdr = (const netctl_msg_header_t *)msg;

    // Validate protocol version
    if (hdr->msg_version != NETCTL_PROTOCOL_VERSION) {
        return netctl_build_error(response, response_maxlen, hdr->msg_seq,
                                  -1, "Protocol version mismatch");
    }

    // Validate message length
    if (hdr->msg_len > len) {
        return netctl_build_error(response, response_maxlen, hdr->msg_seq,
                                  -1, "Invalid message length");
    }

    // Dispatch based on message type
    switch (hdr->msg_type) {
        case NETCTL_MSG_NOOP:
            // NOOP: just send back an ACK
            netctl_msg_init((netctl_msg_header_t *)response,
                           NETCTL_MSG_DONE, 0, hdr->msg_seq, 0);
            return sizeof(netctl_msg_header_t);

        case NETCTL_MSG_GET_VERSION:
            return netctl_handle_get_version(hdr, response, response_maxlen);

        case NETCTL_MSG_NETDEV_LIST:
        case NETCTL_MSG_NETDEV_GET:
        case NETCTL_MSG_NETDEV_SET:
        case NETCTL_MSG_ADDR_NEW:
        case NETCTL_MSG_ADDR_DEL:
        case NETCTL_MSG_ADDR_LIST:
        case NETCTL_MSG_ROUTE_NEW:
        case NETCTL_MSG_ROUTE_DEL:
        case NETCTL_MSG_ROUTE_LIST:
            // Not yet implemented
            return netctl_build_error(response, response_maxlen, hdr->msg_seq,
                                      -1, "Operation not yet implemented");

        default:
            return netctl_build_error(response, response_maxlen, hdr->msg_seq,
                                      -1, "Unknown message type");
    }
}

// ============================================================================
// Multicast Notification
// ============================================================================

void netctl_notify(uint32_t groups, const void *msg, size_t len) {
    if (!msg || len == 0) return;

    // Send to all sockets subscribed to any of the specified groups
    for (int i = 0; i < NETCTL_MAX_SOCKETS; i++) {
        if (!socket_table[i].used) continue;
        if ((socket_table[i].groups & groups) == 0) continue;

        netctl_socket_t *sock = &socket_table[i];

        // Copy message to socket RX buffer
        size_t to_copy = len;
        if (to_copy > NETCTL_SOCKET_BUFFER - sock->rx_count) {
            to_copy = NETCTL_SOCKET_BUFFER - sock->rx_count;
        }

        const uint8_t *msg_bytes = (const uint8_t *)msg;
        for (size_t j = 0; j < to_copy; j++) {
            sock->rx_buf[sock->rx_head] = msg_bytes[j];
            sock->rx_head = (sock->rx_head + 1) % NETCTL_SOCKET_BUFFER;
            sock->rx_count++;
        }
    }
}

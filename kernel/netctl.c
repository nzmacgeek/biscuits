// BlueyOS Network Control Plane - "Walkies with Netlink"
// Episode ref: "Walkies" - Sometimes you need the right way to communicate
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#include "netctl.h"
#include "netdev.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "kheap.h"

// ============================================================================
// Socket Management
// ============================================================================

#define NETCTL_MAX_SOCKETS      16
#define NETCTL_SOCKET_BUFFER    8192
#define NETCTL_ERR_EAGAIN       11  // EAGAIN: RX ring full, caller should drain and retry

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

    // Messages must be delivered atomically to preserve framing in the RX ring.
    // If the response cannot fit entirely, return -EAGAIN rather than truncating.
    if ((size_t)response_len > NETCTL_SOCKET_BUFFER - sock->rx_count) {
        return -NETCTL_ERR_EAGAIN;  // caller should drain the ring and retry
    }

    for (int i = 0; i < response_len; i++) {
        sock->rx_buf[sock->rx_head] = response_buf[i];
        sock->rx_head = (sock->rx_head + 1) % NETCTL_SOCKET_BUFFER;
        sock->rx_count++;
    }

    return response_len;
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
                            NETCTL_ATTR_VERSION,
                            &version, sizeof(version)) < 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Buffer too small");
    }

    return (int)resp->msg_len;
}

// Helper: Process NETDEV_LIST request
static int netctl_handle_netdev_list(const netctl_msg_header_t *req,
                                      void *response, size_t response_maxlen) {
    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_NETDEV_LIST, 0, req->msg_seq, 0);

    netdev_device_t *devs[NETDEV_MAX_DEVICES];
    int count = 0;
    netdev_list_all(devs, &count, NETDEV_MAX_DEVICES);

    // Wrap each device's attributes in a NETCTL_ATTR_NESTED container so
    // consumers can parse the list robustly regardless of attribute order.
    for (int i = 0; i < count; i++) {
        // Build this device's attributes in a temporary buffer.
        // Use a fake msg_header at the front so we can reuse netctl_msg_add_attr.
        uint8_t dev_msg[192];
        netctl_msg_header_t *dev_hdr = (netctl_msg_header_t *)dev_msg;
        netctl_msg_init(dev_hdr, 0, 0, 0, 0);

        netctl_msg_add_attr(dev_msg, sizeof(dev_msg), NETCTL_ATTR_IFINDEX,
                            &devs[i]->ifindex, sizeof(devs[i]->ifindex));
        // Name length is bounded by NETDEV_NAME_LEN (16), so the +1 fits in uint16_t
        netctl_msg_add_attr(dev_msg, sizeof(dev_msg), NETCTL_ATTR_IFNAME,
                            devs[i]->name, (uint16_t)(strlen(devs[i]->name) + 1));
        netctl_msg_add_attr(dev_msg, sizeof(dev_msg), NETCTL_ATTR_FLAGS,
                            &devs[i]->flags, sizeof(devs[i]->flags));
        netctl_msg_add_attr(dev_msg, sizeof(dev_msg), NETCTL_ATTR_MTU,
                            &devs[i]->mtu, sizeof(devs[i]->mtu));
        netctl_msg_add_attr(dev_msg, sizeof(dev_msg), NETCTL_ATTR_MAC,
                            devs[i]->mac, 6);
        netctl_msg_add_attr(dev_msg, sizeof(dev_msg), NETCTL_ATTR_CARRIER,
                            &devs[i]->carrier, sizeof(devs[i]->carrier));

        // Payload is everything after the fake header
        const uint8_t *payload = dev_msg + sizeof(netctl_msg_header_t);
        uint16_t payload_len = (uint16_t)(dev_hdr->msg_len - sizeof(netctl_msg_header_t));

        netctl_msg_add_attr(response, response_maxlen, NETCTL_ATTR_NESTED,
                            payload, payload_len);
    }

    return (int)resp->msg_len;
}

// Helper: Process NETDEV_GET request
static int netctl_handle_netdev_get(const netctl_msg_header_t *req,
                                     const void *msg, size_t len,
                                     void *response, size_t response_maxlen) {
    (void)len;  // Unused for now

    // Extract ifindex from request attributes
    const uint8_t *attr_data = (const uint8_t *)msg + sizeof(netctl_msg_header_t);
    uint32_t remaining = req->msg_len - sizeof(netctl_msg_header_t);
    uint32_t ifindex = 0;

    while (remaining >= sizeof(netctl_attr_header_t)) {
        const netctl_attr_header_t *attr = (const netctl_attr_header_t *)attr_data;
        if (attr->attr_type == NETCTL_ATTR_IFINDEX && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&ifindex, attr_data + sizeof(netctl_attr_header_t), sizeof(uint32_t));
            break;
        }
        uint16_t aligned_len = NETCTL_ATTR_ALIGN(attr->attr_len);
        if (aligned_len > remaining) break;
        attr_data += aligned_len;
        remaining -= aligned_len;
    }

    netdev_device_t *dev = netdev_get_by_index(ifindex);
    if (!dev) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Device not found");
    }

    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_NETDEV_GET, 0, req->msg_seq, 0);

    netctl_msg_add_attr(response, response_maxlen, NETCTL_ATTR_IFINDEX,
                        &dev->ifindex, sizeof(dev->ifindex));
    netctl_msg_add_attr(response, response_maxlen, NETCTL_ATTR_IFNAME,
                        dev->name, strlen(dev->name) + 1);
    netctl_msg_add_attr(response, response_maxlen, NETCTL_ATTR_FLAGS,
                        &dev->flags, sizeof(dev->flags));
    netctl_msg_add_attr(response, response_maxlen, NETCTL_ATTR_MTU,
                        &dev->mtu, sizeof(dev->mtu));
    netctl_msg_add_attr(response, response_maxlen, NETCTL_ATTR_MAC,
                        dev->mac, 6);
    netctl_msg_add_attr(response, response_maxlen, NETCTL_ATTR_CARRIER,
                        &dev->carrier, sizeof(dev->carrier));

    return (int)resp->msg_len;
}

// Helper: Process NETDEV_SET request
static int netctl_handle_netdev_set(const netctl_msg_header_t *req,
                                     const void *msg, size_t len,
                                     void *response, size_t response_maxlen) {
    (void)len;

    // Parse attributes
    const uint8_t *attr_data = (const uint8_t *)msg + sizeof(netctl_msg_header_t);
    uint32_t remaining = req->msg_len - sizeof(netctl_msg_header_t);
    uint32_t ifindex = 0;
    uint32_t flags = 0;
    uint32_t mtu = 0;
    int have_flags = 0;
    int have_mtu = 0;

    while (remaining >= sizeof(netctl_attr_header_t)) {
        const netctl_attr_header_t *attr = (const netctl_attr_header_t *)attr_data;
        const uint8_t *payload = attr_data + sizeof(netctl_attr_header_t);

        if (attr->attr_type == NETCTL_ATTR_IFINDEX && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&ifindex, payload, sizeof(uint32_t));
        } else if (attr->attr_type == NETCTL_ATTR_FLAGS && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&flags, payload, sizeof(uint32_t));
            have_flags = 1;
        } else if (attr->attr_type == NETCTL_ATTR_MTU && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&mtu, payload, sizeof(uint32_t));
            have_mtu = 1;
        }

        uint16_t aligned_len = NETCTL_ATTR_ALIGN(attr->attr_len);
        if (aligned_len > remaining) break;
        attr_data += aligned_len;
        remaining -= aligned_len;
    }

    if (ifindex == 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "No ifindex specified");
    }

    // Apply changes
    if (have_flags) {
        if (netdev_set_flags(ifindex, flags) < 0) {
            return netctl_build_error(response, response_maxlen, req->msg_seq,
                                      -1, "Failed to set flags");
        }
    }

    if (have_mtu) {
        if (netdev_set_mtu(ifindex, mtu) < 0) {
            return netctl_build_error(response, response_maxlen, req->msg_seq,
                                      -1, "Failed to set MTU");
        }
    }

    // Send ACK
    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_DONE, 0, req->msg_seq, 0);
    return (int)resp->msg_len;
}

// Helper: Process ADDR_NEW request
static int netctl_handle_addr_new(const netctl_msg_header_t *req,
                                   const void *msg, size_t len,
                                   void *response, size_t response_maxlen) {
    (void)len;

    const uint8_t *attr_data = (const uint8_t *)msg + sizeof(netctl_msg_header_t);
    uint32_t remaining = req->msg_len - sizeof(netctl_msg_header_t);
    uint32_t ifindex = 0;
    uint16_t family = NETDEV_AF_INET;
    uint32_t addr_value = 0;
    uint8_t prefix_len = 0;
    int has_addr = 0;

    while (remaining >= sizeof(netctl_attr_header_t)) {
        const netctl_attr_header_t *attr = (const netctl_attr_header_t *)attr_data;
        const uint8_t *payload = attr_data + sizeof(netctl_attr_header_t);

        if (attr->attr_type == NETCTL_ATTR_IFINDEX && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&ifindex, payload, sizeof(uint32_t));
        } else if (attr->attr_type == NETCTL_ATTR_ADDR_FAMILY && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint16_t)) {
            memcpy(&family, payload, sizeof(uint16_t));
        } else if (attr->attr_type == NETCTL_ATTR_ADDR_VALUE && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&addr_value, payload, sizeof(uint32_t));
            has_addr = 1;
        } else if (attr->attr_type == NETCTL_ATTR_ADDR_PREFIX && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint8_t)) {
            prefix_len = *payload;
        }

        if (attr->attr_len < NETCTL_ATTR_HDRLEN) break;
        uint16_t aligned_len = NETCTL_ATTR_ALIGN(attr->attr_len);
        if (aligned_len == 0 || aligned_len > remaining) break;
        attr_data += aligned_len;
        remaining -= aligned_len;
    }

    if (ifindex == 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "No ifindex specified");
    }

    if (family != NETDEV_AF_INET) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Unsupported address family");
    }

    if (!has_addr) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "No address specified");
    }

    if (prefix_len > 32) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Invalid IPv4 prefix length");
    }

    if (netdev_addr_add(ifindex, (uint16_t)family, &addr_value, prefix_len) < 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Failed to add address");
    }

    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_DONE, 0, req->msg_seq, 0);
    return (int)resp->msg_len;
}

// Helper: Process ADDR_DEL request
static int netctl_handle_addr_del(const netctl_msg_header_t *req,
                                   const void *msg, size_t len,
                                   void *response, size_t response_maxlen) {
    (void)len;

    const uint8_t *attr_data = (const uint8_t *)msg + sizeof(netctl_msg_header_t);
    uint32_t remaining = req->msg_len - sizeof(netctl_msg_header_t);
    uint32_t ifindex = 0;
    uint16_t family = NETDEV_AF_INET;
    uint32_t addr_value = 0;

    while (remaining >= sizeof(netctl_attr_header_t)) {
        const netctl_attr_header_t *attr = (const netctl_attr_header_t *)attr_data;
        const uint8_t *payload = attr_data + sizeof(netctl_attr_header_t);

        if (attr->attr_type == NETCTL_ATTR_IFINDEX && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&ifindex, payload, sizeof(uint32_t));
        } else if (attr->attr_type == NETCTL_ATTR_ADDR_FAMILY && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint16_t)) {
            memcpy(&family, payload, sizeof(uint16_t));
        } else if (attr->attr_type == NETCTL_ATTR_ADDR_VALUE && attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&addr_value, payload, sizeof(uint32_t));
        }

        if (attr->attr_len < NETCTL_ATTR_HDRLEN) break;
        uint16_t aligned_len = NETCTL_ATTR_ALIGN(attr->attr_len);
        if (aligned_len == 0 || aligned_len > remaining) break;
        attr_data += aligned_len;
        remaining -= aligned_len;
    }

    if (ifindex == 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "No ifindex specified");
    }

    if (family != NETDEV_AF_INET) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Unsupported address family");
    }

    if (netdev_addr_del(ifindex, (uint16_t)family, &addr_value) < 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Failed to delete address");
    }

    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_DONE, 0, req->msg_seq, 0);
    return (int)resp->msg_len;
}

// Helper: Process ADDR_LIST request
static int netctl_handle_addr_list(const netctl_msg_header_t *req,
                                    const void *msg, size_t len,
                                    void *response, size_t response_maxlen) {
    (void)len;

    const uint8_t *attr_data = (const uint8_t *)msg + sizeof(netctl_msg_header_t);
    uint32_t remaining = req->msg_len - sizeof(netctl_msg_header_t);
    uint32_t ifindex = 0;

    while (remaining >= sizeof(netctl_attr_header_t)) {
        const netctl_attr_header_t *attr = (const netctl_attr_header_t *)attr_data;
        const uint8_t *payload = attr_data + sizeof(netctl_attr_header_t);

        if (attr->attr_type == NETCTL_ATTR_IFINDEX &&
            attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&ifindex, payload, sizeof(uint32_t));
        }

        if (attr->attr_len < NETCTL_ATTR_HDRLEN) break;
        uint16_t aligned_len = NETCTL_ATTR_ALIGN(attr->attr_len);
        if (aligned_len == 0 || aligned_len > remaining) break;
        attr_data += aligned_len;
        remaining -= aligned_len;
    }

    if (ifindex == 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "No ifindex specified");
    }

    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_ADDR_LIST, 0, req->msg_seq, 0);

    netdev_addr_t addrs[NETDEV_MAX_ADDRS];
    int count = netdev_addr_list(ifindex, addrs, NETDEV_MAX_ADDRS);

    for (int i = 0; i < count; i++) {
        uint8_t entry_buf[128];
        netctl_msg_header_t *ehdr = (netctl_msg_header_t *)entry_buf;
        netctl_msg_init(ehdr, 0, 0, 0, 0);

        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_IFINDEX,
                            &addrs[i].ifindex, sizeof(addrs[i].ifindex));
        uint16_t fam = addrs[i].family;
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ADDR_FAMILY, &fam, sizeof(fam));
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ADDR_PREFIX,
                            &addrs[i].prefix_len, sizeof(addrs[i].prefix_len));
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ADDR_VALUE,
                            &addrs[i].addr.ipv4, sizeof(addrs[i].addr.ipv4));

        const uint8_t *payload = entry_buf + sizeof(netctl_msg_header_t);
        uint16_t plen = (uint16_t)(ehdr->msg_len - sizeof(netctl_msg_header_t));
        netctl_msg_add_attr(response, response_maxlen,
                            NETCTL_ATTR_NESTED, payload, plen);
    }

    return (int)resp->msg_len;
}

// Helper: Process ROUTE_NEW request
static int netctl_handle_route_new(const netctl_msg_header_t *req,
                                    const void *msg, size_t len,
                                    void *response, size_t response_maxlen) {
    (void)len;

    const uint8_t *attr_data = (const uint8_t *)msg + sizeof(netctl_msg_header_t);
    uint32_t remaining = req->msg_len - sizeof(netctl_msg_header_t);
    uint16_t family = NETDEV_AF_INET;
    uint32_t dest = 0;
    uint8_t prefix_len = 0;
    uint32_t gateway = 0;
    uint32_t oif = 0;
    uint32_t metric = 0;
    int has_dest = 0;

    while (remaining >= sizeof(netctl_attr_header_t)) {
        const netctl_attr_header_t *attr = (const netctl_attr_header_t *)attr_data;
        const uint8_t *payload = attr_data + sizeof(netctl_attr_header_t);

        if (attr->attr_type == NETCTL_ATTR_ROUTE_FAMILY &&
            attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint16_t)) {
            memcpy(&family, payload, sizeof(uint16_t));
        } else if (attr->attr_type == NETCTL_ATTR_ROUTE_DST &&
                   attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&dest, payload, sizeof(uint32_t));
            has_dest = 1;
        } else if (attr->attr_type == NETCTL_ATTR_ROUTE_PREFIX &&
                   attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint8_t)) {
            prefix_len = *payload;
        } else if (attr->attr_type == NETCTL_ATTR_ROUTE_GW &&
                   attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&gateway, payload, sizeof(uint32_t));
        } else if (attr->attr_type == NETCTL_ATTR_ROUTE_OIF &&
                   attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&oif, payload, sizeof(uint32_t));
        } else if (attr->attr_type == NETCTL_ATTR_ROUTE_METRIC &&
                   attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&metric, payload, sizeof(uint32_t));
        }

        if (attr->attr_len < NETCTL_ATTR_HDRLEN) break;
        uint16_t aligned_len = NETCTL_ATTR_ALIGN(attr->attr_len);
        if (aligned_len == 0 || aligned_len > remaining) break;
        attr_data += aligned_len;
        remaining -= aligned_len;
    }

    if (!has_dest) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "No destination specified");
    }

    if (family != NETDEV_AF_INET) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Unsupported route family");
    }

    if (prefix_len > 32) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Invalid IPv4 prefix length");
    }

    const void *gw_ptr = gateway ? &gateway : NULL;
    if (netdev_route_add(family, &dest, prefix_len, gw_ptr, oif, metric) < 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Failed to add route");
    }

    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_DONE, 0, req->msg_seq, 0);
    return (int)resp->msg_len;
}

// Helper: Process ROUTE_DEL request
static int netctl_handle_route_del(const netctl_msg_header_t *req,
                                    const void *msg, size_t len,
                                    void *response, size_t response_maxlen) {
    (void)len;

    const uint8_t *attr_data = (const uint8_t *)msg + sizeof(netctl_msg_header_t);
    uint32_t remaining = req->msg_len - sizeof(netctl_msg_header_t);
    uint16_t family = NETDEV_AF_INET;
    uint32_t dest = 0;
    uint8_t prefix_len = 0;
    int has_dest = 0;

    while (remaining >= sizeof(netctl_attr_header_t)) {
        const netctl_attr_header_t *attr = (const netctl_attr_header_t *)attr_data;
        const uint8_t *payload = attr_data + sizeof(netctl_attr_header_t);

        if (attr->attr_type == NETCTL_ATTR_ROUTE_FAMILY &&
            attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint16_t)) {
            memcpy(&family, payload, sizeof(uint16_t));
        } else if (attr->attr_type == NETCTL_ATTR_ROUTE_DST &&
                   attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint32_t)) {
            memcpy(&dest, payload, sizeof(uint32_t));
            has_dest = 1;
        } else if (attr->attr_type == NETCTL_ATTR_ROUTE_PREFIX &&
                   attr->attr_len >= sizeof(netctl_attr_header_t) + sizeof(uint8_t)) {
            prefix_len = *payload;
        }

        if (attr->attr_len < NETCTL_ATTR_HDRLEN) break;
        uint16_t aligned_len = NETCTL_ATTR_ALIGN(attr->attr_len);
        if (aligned_len == 0 || aligned_len > remaining) break;
        attr_data += aligned_len;
        remaining -= aligned_len;
    }

    if (!has_dest) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "No destination specified");
    }

    if (family != NETDEV_AF_INET) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Unsupported route family");
    }

    if (prefix_len > 32) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Invalid IPv4 prefix length");
    }

    if (netdev_route_del(family, &dest, prefix_len) < 0) {
        return netctl_build_error(response, response_maxlen, req->msg_seq,
                                  -1, "Route not found");
    }

    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_DONE, 0, req->msg_seq, 0);
    return (int)resp->msg_len;
}

// Helper: Process ROUTE_LIST request
static int netctl_handle_route_list(const netctl_msg_header_t *req,
                                     void *response, size_t response_maxlen) {
    netctl_msg_header_t *resp = (netctl_msg_header_t *)response;
    netctl_msg_init(resp, NETCTL_MSG_ROUTE_LIST, 0, req->msg_seq, 0);

    netdev_route_t routes[NETDEV_MAX_ROUTES];
    int count = netdev_route_list(routes, NETDEV_MAX_ROUTES);

    for (int i = 0; i < count; i++) {
        uint8_t entry_buf[192];
        netctl_msg_header_t *ehdr = (netctl_msg_header_t *)entry_buf;
        netctl_msg_init(ehdr, 0, 0, 0, 0);

        uint16_t fam = routes[i].family;
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ROUTE_FAMILY, &fam, sizeof(fam));
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ROUTE_DST,
                            &routes[i].dest.ipv4, sizeof(routes[i].dest.ipv4));
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ROUTE_PREFIX,
                            &routes[i].prefix_len, sizeof(routes[i].prefix_len));
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ROUTE_GW,
                            &routes[i].gateway.ipv4, sizeof(routes[i].gateway.ipv4));
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ROUTE_OIF,
                            &routes[i].oif, sizeof(routes[i].oif));
        netctl_msg_add_attr(entry_buf, sizeof(entry_buf),
                            NETCTL_ATTR_ROUTE_METRIC,
                            &routes[i].metric, sizeof(routes[i].metric));

        const uint8_t *payload = entry_buf + sizeof(netctl_msg_header_t);
        uint16_t plen = (uint16_t)(ehdr->msg_len - sizeof(netctl_msg_header_t));
        netctl_msg_add_attr(response, response_maxlen,
                            NETCTL_ATTR_NESTED, payload, plen);
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

    // Validate message length (must be at least a full header, and no larger than input)
    if (hdr->msg_len < sizeof(netctl_msg_header_t) || hdr->msg_len > len) {
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
            return netctl_handle_netdev_list(hdr, response, response_maxlen);

        case NETCTL_MSG_NETDEV_GET:
            return netctl_handle_netdev_get(hdr, msg, len, response, response_maxlen);

        case NETCTL_MSG_NETDEV_SET:
            return netctl_handle_netdev_set(hdr, msg, len, response, response_maxlen);

        case NETCTL_MSG_ADDR_NEW:
            return netctl_handle_addr_new(hdr, msg, len, response, response_maxlen);

        case NETCTL_MSG_ADDR_DEL:
            return netctl_handle_addr_del(hdr, msg, len, response, response_maxlen);

        case NETCTL_MSG_ADDR_LIST:
            return netctl_handle_addr_list(hdr, msg, len, response, response_maxlen);

        case NETCTL_MSG_ROUTE_NEW:
            return netctl_handle_route_new(hdr, msg, len, response, response_maxlen);

        case NETCTL_MSG_ROUTE_DEL:
            return netctl_handle_route_del(hdr, msg, len, response, response_maxlen);

        case NETCTL_MSG_ROUTE_LIST:
            return netctl_handle_route_list(hdr, response, response_maxlen);

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

    // Messages must be delivered atomically to preserve framing in the RX ring.
    // If a message cannot fit in a socket buffer, skip delivery to that socket.
    if (len > NETCTL_SOCKET_BUFFER) return;

    // Send to all sockets subscribed to any of the specified groups
    for (int i = 0; i < NETCTL_MAX_SOCKETS; i++) {
        if (!socket_table[i].used) continue;
        if ((socket_table[i].groups & groups) == 0) continue;

        netctl_socket_t *sock = &socket_table[i];
        size_t free_space = NETCTL_SOCKET_BUFFER - sock->rx_count;

        // Preserve message atomicity: do not enqueue partial notifications.
        if (len > free_space) continue;

        const uint8_t *msg_bytes = (const uint8_t *)msg;
        for (size_t j = 0; j < len; j++) {
            sock->rx_buf[sock->rx_head] = msg_bytes[j];
            sock->rx_head = (sock->rx_head + 1) % NETCTL_SOCKET_BUFFER;
            sock->rx_count++;
        }
    }
}

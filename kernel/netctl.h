#pragma once
// BlueyOS Network Control Plane - "Walkies with Netlink"
// Episode ref: "Walkies" - Sometimes you need the right way to communicate
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
//
// This is a Netlink-inspired control plane for kernel ↔ user space network
// configuration. It is NOT Linux-compatible but follows similar design
// principles: socket-based IPC, message-oriented, TLV attributes, extensible.

#include "../include/types.h"

// ============================================================================
// Socket Family Definition
// ============================================================================

#define AF_BLUEY_NETCTL    2   // Network control socket family
#define SOCK_NETCTL        3   // Message-oriented netctl socket type

// ============================================================================
// Protocol Version
// ============================================================================

#define NETCTL_PROTOCOL_VERSION  1

// ============================================================================
// Message Header
// ============================================================================

// Message flags
#define NETCTL_FLAG_REQUEST     0x0001  // Request from user space
#define NETCTL_FLAG_MULTICAST   0x0002  // Multicast notification
#define NETCTL_FLAG_ACK         0x0004  // Acknowledgement required
#define NETCTL_FLAG_ERROR       0x0008  // Error response

// Message types
#define NETCTL_MSG_NOOP         0   // No operation (for testing)
#define NETCTL_MSG_ERROR        1   // Error response
#define NETCTL_MSG_DONE         2   // End of multi-part message
#define NETCTL_MSG_GET_VERSION  3   // Get protocol version

// Netdev (network device) messages
#define NETCTL_MSG_NETDEV_NEW      10  // New device notification
#define NETCTL_MSG_NETDEV_DEL      11  // Device removed notification
#define NETCTL_MSG_NETDEV_GET      12  // Get device info
#define NETCTL_MSG_NETDEV_SET      13  // Set device attributes
#define NETCTL_MSG_NETDEV_LIST     14  // List all devices

// Address messages
#define NETCTL_MSG_ADDR_NEW        20  // Add address
#define NETCTL_MSG_ADDR_DEL        21  // Remove address
#define NETCTL_MSG_ADDR_GET        22  // Get address info
#define NETCTL_MSG_ADDR_LIST       23  // List addresses

// Route messages
#define NETCTL_MSG_ROUTE_NEW       30  // Add route
#define NETCTL_MSG_ROUTE_DEL       31  // Remove route
#define NETCTL_MSG_ROUTE_GET       32  // Get route info
#define NETCTL_MSG_ROUTE_LIST      33  // List routes

// Message header structure
// All integers are in host byte order (little-endian on i386)
typedef struct {
    uint32_t msg_len;      // Total message length including header
    uint16_t msg_type;     // Message type (NETCTL_MSG_*)
    uint16_t msg_flags;    // Message flags (NETCTL_FLAG_*)
    uint32_t msg_seq;      // Sequence number (for request/response matching)
    uint32_t msg_pid;      // Sender process ID (0 for kernel)
    uint16_t msg_version;  // Protocol version
    uint16_t msg_reserved; // Reserved for future use
} netctl_msg_header_t;

// Error message payload
typedef struct {
    int32_t  error_code;   // Error code (negative errno value)
    uint32_t bad_seq;      // Sequence number of the request that failed
    char     error_msg[64]; // Human-readable error message
} netctl_error_payload_t;

// ============================================================================
// TLV Attribute System
// ============================================================================

// Attribute types - generic
#define NETCTL_ATTR_UNSPEC      0   // Unspecified (padding)
#define NETCTL_ATTR_NESTED      1   // Nested attributes
#define NETCTL_ATTR_VERSION     5   // Protocol version (uint16_t)

// Attribute types - netdev
#define NETCTL_ATTR_IFINDEX     10  // Interface index (uint32_t)
#define NETCTL_ATTR_IFNAME      11  // Interface name (string)
#define NETCTL_ATTR_MTU         12  // MTU (uint32_t)
#define NETCTL_ATTR_MAC         13  // MAC address (6 bytes)
#define NETCTL_ATTR_FLAGS       14  // Device flags (uint32_t)
#define NETCTL_ATTR_CARRIER     15  // Carrier state (uint8_t)

// Attribute types - address
#define NETCTL_ATTR_ADDR_FAMILY 20  // Address family (uint16_t)
#define NETCTL_ATTR_ADDR_VALUE  21  // Address value (4 or 16 bytes)
#define NETCTL_ATTR_ADDR_PREFIX 22  // Prefix length (uint8_t)

// Attribute types - route
#define NETCTL_ATTR_ROUTE_DST   30  // Destination prefix
#define NETCTL_ATTR_ROUTE_GW    31  // Gateway address
#define NETCTL_ATTR_ROUTE_OIF   32  // Output interface index
#define NETCTL_ATTR_ROUTE_METRIC 33 // Route metric (uint32_t)
#define NETCTL_ATTR_ROUTE_PREFIX 34 // Route prefix length (uint8_t)
#define NETCTL_ATTR_ROUTE_FAMILY 35 // Route address family (uint16_t)

// TLV attribute header
// attr_len stores the actual attribute length (header + payload), NOT including
// any trailing padding bytes. Padding is added when advancing to the next
// attribute so that each attribute starts on a 4-byte boundary.
typedef struct {
    uint16_t attr_len;     // Length including header; not required to be 4-byte aligned
    uint16_t attr_type;    // Attribute type
} netctl_attr_header_t;

// Attribute alignment
// NETCTL_ATTR_ALIGN() rounds a length up to the padded size used for stepping
// to the next attribute. The length stored in attr_len remains unaligned.
#define NETCTL_ATTR_ALIGN(len)  (((len) + 3) & ~3)
#define NETCTL_ATTR_HDRLEN      sizeof(netctl_attr_header_t)
#define NETCTL_ATTR_LENGTH(payload_len) \
    NETCTL_ATTR_ALIGN(NETCTL_ATTR_HDRLEN + (payload_len))

// ============================================================================
// Multicast Groups
// ============================================================================

#define NETCTL_GROUP_NONE       0   // No group
#define NETCTL_GROUP_LINK       1   // Link state events (up/down/carrier)
#define NETCTL_GROUP_ADDR       2   // Address events (add/del)
#define NETCTL_GROUP_ROUTE      4   // Route events (add/del)
#define NETCTL_GROUP_ALL        7   // All events (bitmask)

// ============================================================================
// Device Flags (compatible with common network flags)
// ============================================================================

#define NETCTL_FLAG_UP          0x0001  // Interface is up
#define NETCTL_FLAG_RUNNING     0x0002  // Resources allocated
#define NETCTL_FLAG_CARRIER     0x0004  // Carrier detected
#define NETCTL_FLAG_LOOPBACK    0x0008  // Loopback interface
#define NETCTL_FLAG_BROADCAST   0x0010  // Supports broadcast

// ============================================================================
// Address Families
// ============================================================================

#define NETCTL_AF_UNSPEC        0
#define NETCTL_AF_INET          2   // IPv4
#define NETCTL_AF_INET6         10  // IPv6 (future)

// ============================================================================
// API Functions
// ============================================================================

void netctl_init(void);

// Socket operations
int  netctl_socket_create(int protocol);
int  netctl_socket_bind(int socket_id, uint32_t groups);
int  netctl_socket_close(int socket_id);
int  netctl_socket_send(int socket_id, const void *msg, size_t len);
int  netctl_socket_recv(int socket_id, void *buf, size_t len);

// Message construction helpers
void netctl_msg_init(netctl_msg_header_t *hdr, uint16_t type, uint16_t flags,
                     uint32_t seq, uint32_t pid);
int  netctl_msg_add_attr(void *msg, size_t msg_maxlen, uint16_t type,
                         const void *data, uint16_t len);

// Message processing (kernel-side)
int  netctl_process_message(const void *msg, size_t len, void *response,
                            size_t response_maxlen);

// Multicast notification
void netctl_notify(uint32_t groups, const void *msg, size_t len);

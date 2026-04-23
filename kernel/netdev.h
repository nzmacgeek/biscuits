#pragma once
// BlueyOS Network Device Management - "Jack's Network Adventure"
// Episode ref: "Hammerbarn" - Sometimes you need good tools to build something
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.
//
// This module defines the core kernel networking objects: devices, addresses,
// and routes. It provides the data structures and operations for the netctl
// control plane to manage.

#include "../include/types.h"

// ============================================================================
// Network Device (netdev) Management
// ============================================================================

#define NETDEV_MAX_DEVICES      8
#define NETDEV_NAME_LEN         16
#define NETDEV_MAX_ADDRS        4   // Max addresses per interface

// Device flags
// Flag values intentionally match glibc IFF_* (net/if.h) so kernel flags can
// be used directly in userspace without translation.
#define NETDEV_FLAG_UP          0x0001  // Interface is administratively up (IFF_UP)
#define NETDEV_FLAG_BROADCAST   0x0002  // Supports broadcast (IFF_BROADCAST)
#define NETDEV_FLAG_CARRIER     0x0004  // Physical link detected (no IFF_ equiv)
#define NETDEV_FLAG_LOOPBACK    0x0008  // Loopback interface (IFF_LOOPBACK)
#define NETDEV_FLAG_RUNNING     0x0040  // Resources allocated, carrier present (IFF_RUNNING)

// Forward declarations for driver operations
struct netdev_device;
typedef int (*netdev_tx_func_t)(struct netdev_device *dev, const uint8_t *data, uint16_t len);
typedef int (*netdev_rx_func_t)(struct netdev_device *dev, uint8_t *buf, uint16_t *len);

// Network device structure
typedef struct netdev_device {
    // Identity
    uint32_t ifindex;           // Interface index (unique, immutable)
    char     name[NETDEV_NAME_LEN]; // Interface name (e.g., "eth0", "lo")

    // State
    uint32_t flags;             // Device flags (UP, RUNNING, CARRIER, etc.)
    uint32_t mtu;               // Maximum Transmission Unit
    uint8_t  mac[6];            // MAC address
    uint8_t  carrier;           // Carrier state (0=down, 1=up)

    // Statistics
    uint32_t tx_packets;
    uint32_t rx_packets;
    uint32_t tx_errors;
    uint32_t rx_errors;
    uint32_t tx_bytes;
    uint32_t rx_bytes;

    // Driver operations
    netdev_tx_func_t send;
    netdev_rx_func_t recv;
    void    *driver_priv;       // Driver private data

    // Reference counting / lifetime
    int      refcount;
    int      registered;        // 1 if device is registered
} netdev_device_t;

// ============================================================================
// Address Management
// ============================================================================

#define NETDEV_AF_INET          2   // IPv4
#define NETDEV_AF_INET6         10  // IPv6 (future)

// Address structure
typedef struct {
    int      used;              // 1 if this slot is in use
    uint32_t ifindex;           // Associated interface
    uint16_t family;            // Address family (AF_INET, AF_INET6)
    uint8_t  prefix_len;        // Prefix length (e.g., 24 for /24)
    uint8_t  reserved;
    union {
        uint32_t ipv4;          // IPv4 address (host byte order)
        uint8_t  ipv6[16];      // IPv6 address (future)
    } addr;
} netdev_addr_t;

// ============================================================================
// Routing Table
// ============================================================================

#define NETDEV_MAX_ROUTES       32

// Route structure
typedef struct {
    int      used;              // 1 if this route is active
    uint16_t family;            // Address family
    uint8_t  prefix_len;        // Destination prefix length
    uint8_t  reserved;
    union {
        uint32_t ipv4;          // IPv4 destination prefix
        uint8_t  ipv6[16];      // IPv6 destination prefix (future)
    } dest;
    union {
        uint32_t ipv4;          // IPv4 gateway (0 = direct)
        uint8_t  ipv6[16];      // IPv6 gateway (future)
    } gateway;
    uint32_t oif;               // Output interface index
    uint32_t metric;            // Route metric (lower = preferred)
} netdev_route_t;

// ============================================================================
// API Functions
// ============================================================================

void netdev_init(void);

// Device management
int  netdev_register(netdev_device_t *dev);
int  netdev_unregister(uint32_t ifindex);
netdev_device_t *netdev_get_by_index(uint32_t ifindex);
netdev_device_t *netdev_get_by_name(const char *name);
int  netdev_set_flags(uint32_t ifindex, uint32_t flags);
int  netdev_set_mtu(uint32_t ifindex, uint32_t mtu);
int  netdev_set_carrier(uint32_t ifindex, uint8_t carrier);
void netdev_list_all(netdev_device_t **devs, int *count, int maxcount);

// Address management
int  netdev_addr_add(uint32_t ifindex, uint16_t family, const void *addr, uint8_t prefix_len);
int  netdev_addr_del(uint32_t ifindex, uint16_t family, const void *addr);
int  netdev_addr_list(uint32_t ifindex, netdev_addr_t *addrs, int maxcount);
netdev_addr_t *netdev_addr_find(uint32_t ifindex, uint16_t family, const void *addr);

// Routing table
int  netdev_route_add(uint16_t family, const void *dest, uint8_t prefix_len,
                      const void *gateway, uint32_t oif, uint32_t metric);
int  netdev_route_del(uint16_t family, const void *dest, uint8_t prefix_len);
int  netdev_route_list(netdev_route_t *routes, int maxcount);
netdev_route_t *netdev_route_lookup(uint16_t family, const void *dest);

// Event notification helpers (call netctl_notify internally)
void netdev_notify_link_change(uint32_t ifindex, uint32_t flags, uint8_t carrier);
void netdev_notify_addr_change(uint32_t ifindex, uint16_t family, const void *addr,
                               uint8_t prefix_len, int added);
void netdev_notify_route_change(const netdev_route_t *route, int added);

// BlueyOS Network Device Management - "Jack's Network Adventure"
// Episode ref: "Hammerbarn" - Sometimes you need good tools to build something
// Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
// licensed by BBC Studios. BlueyOS is an unofficial fan/research project.

#include "netdev.h"
#include "netctl.h"
#include "../lib/string.h"
#include "../lib/stdio.h"

// ============================================================================
// Global State
// ============================================================================

static netdev_device_t devices[NETDEV_MAX_DEVICES];
static netdev_addr_t   addresses[NETDEV_MAX_DEVICES * NETDEV_MAX_ADDRS];
static netdev_route_t  routes[NETDEV_MAX_ROUTES];
static uint32_t        next_ifindex = 1;

// ============================================================================
// Initialization
// ============================================================================

void netdev_init(void) {
    memset(devices, 0, sizeof(devices));
    memset(addresses, 0, sizeof(addresses));
    memset(routes, 0, sizeof(routes));
    kprintf("[NETDEV] Network device management initialized\n");
}

// ============================================================================
// Device Management
// ============================================================================

int netdev_register(netdev_device_t *dev) {
    if (!dev || !dev->name[0]) return -1;

    // Find free slot
    for (int i = 0; i < NETDEV_MAX_DEVICES; i++) {
        if (!devices[i].registered) {
            // Copy device structure
            memcpy(&devices[i], dev, sizeof(netdev_device_t));
            devices[i].ifindex = next_ifindex++;
            devices[i].registered = 1;
            devices[i].refcount = 1;

            kprintf("[NETDEV] Registered %s (ifindex=%u)\n",
                    devices[i].name, devices[i].ifindex);

            // Notify listeners
            netdev_notify_link_change(devices[i].ifindex, devices[i].flags,
                                     devices[i].carrier);
            return (int)devices[i].ifindex;
        }
    }

    return -1;  // No free slots
}

int netdev_unregister(uint32_t ifindex) {
    for (int i = 0; i < NETDEV_MAX_DEVICES; i++) {
        if (devices[i].registered && devices[i].ifindex == ifindex) {
            devices[i].refcount--;
            if (devices[i].refcount <= 0) {
                kprintf("[NETDEV] Unregistered %s (ifindex=%u)\n",
                        devices[i].name, devices[i].ifindex);

                // Remove all addresses associated with this interface
                for (int j = 0; j < NETDEV_MAX_DEVICES * NETDEV_MAX_ADDRS; j++) {
                    if (addresses[j].used && addresses[j].ifindex == ifindex) {
                        addresses[j].used = 0;
                    }
                }

                // Remove all routes using this interface
                for (int j = 0; j < NETDEV_MAX_ROUTES; j++) {
                    if (routes[j].used && routes[j].oif == ifindex) {
                        routes[j].used = 0;
                    }
                }

                memset(&devices[i], 0, sizeof(netdev_device_t));
            }
            return 0;
        }
    }
    return -1;
}

netdev_device_t *netdev_get_by_index(uint32_t ifindex) {
    for (int i = 0; i < NETDEV_MAX_DEVICES; i++) {
        if (devices[i].registered && devices[i].ifindex == ifindex) {
            return &devices[i];
        }
    }
    return NULL;
}

netdev_device_t *netdev_get_by_name(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < NETDEV_MAX_DEVICES; i++) {
        if (devices[i].registered && strcmp(devices[i].name, name) == 0) {
            return &devices[i];
        }
    }
    return NULL;
}

int netdev_set_flags(uint32_t ifindex, uint32_t flags) {
    netdev_device_t *dev = netdev_get_by_index(ifindex);
    if (!dev) return -1;

    uint32_t old_flags = dev->flags;
    dev->flags = flags;

    // Notify if flags changed
    if (old_flags != flags) {
        netdev_notify_link_change(ifindex, flags, dev->carrier);
    }

    return 0;
}

int netdev_set_mtu(uint32_t ifindex, uint32_t mtu) {
    netdev_device_t *dev = netdev_get_by_index(ifindex);
    if (!dev) return -1;
    dev->mtu = mtu;
    return 0;
}

int netdev_set_carrier(uint32_t ifindex, uint8_t carrier) {
    netdev_device_t *dev = netdev_get_by_index(ifindex);
    if (!dev) return -1;

    uint8_t old_carrier = dev->carrier;
    dev->carrier = carrier;

    // Update CARRIER flag
    if (carrier) {
        dev->flags |= NETDEV_FLAG_CARRIER;
    } else {
        dev->flags &= ~NETDEV_FLAG_CARRIER;
    }

    // Notify if carrier changed
    if (old_carrier != carrier) {
        netdev_notify_link_change(ifindex, dev->flags, carrier);
    }

    return 0;
}

void netdev_list_all(netdev_device_t **devs, int *count, int maxcount) {
    if (!devs || !count) return;
    *count = 0;

    for (int i = 0; i < NETDEV_MAX_DEVICES && *count < maxcount; i++) {
        if (devices[i].registered) {
            devs[(*count)++] = &devices[i];
        }
    }
}

// ============================================================================
// Address Management
// ============================================================================

int netdev_addr_add(uint32_t ifindex, uint16_t family, const void *addr,
                    uint8_t prefix_len) {
    if (!addr) return -1;

    // Verify interface exists
    if (!netdev_get_by_index(ifindex)) return -1;

    // Check if address already exists
    if (netdev_addr_find(ifindex, family, addr)) return -1;

    // Find free slot
    for (int i = 0; i < NETDEV_MAX_DEVICES * NETDEV_MAX_ADDRS; i++) {
        if (!addresses[i].used) {
            addresses[i].used = 1;
            addresses[i].ifindex = ifindex;
            addresses[i].family = family;
            addresses[i].prefix_len = prefix_len;

            if (family == NETDEV_AF_INET) {
                addresses[i].addr.ipv4 = *(const uint32_t *)addr;
            }

            netdev_notify_addr_change(ifindex, family, addr, prefix_len, 1);
            return 0;
        }
    }

    return -1;  // No free slots
}

int netdev_addr_del(uint32_t ifindex, uint16_t family, const void *addr) {
    if (!addr) return -1;

    for (int i = 0; i < NETDEV_MAX_DEVICES * NETDEV_MAX_ADDRS; i++) {
        if (!addresses[i].used) continue;
        if (addresses[i].ifindex != ifindex) continue;
        if (addresses[i].family != family) continue;

        int match = 0;
        if (family == NETDEV_AF_INET) {
            match = (addresses[i].addr.ipv4 == *(const uint32_t *)addr);
        }

        if (match) {
            uint8_t prefix_len = addresses[i].prefix_len;
            addresses[i].used = 0;
            netdev_notify_addr_change(ifindex, family, addr, prefix_len, 0);
            return 0;
        }
    }

    return -1;  // Address not found
}

int netdev_addr_list(uint32_t ifindex, netdev_addr_t *addrs, int maxcount) {
    if (!addrs) return -1;

    int count = 0;
    for (int i = 0; i < NETDEV_MAX_DEVICES * NETDEV_MAX_ADDRS && count < maxcount; i++) {
        if (addresses[i].used && addresses[i].ifindex == ifindex) {
            memcpy(&addrs[count++], &addresses[i], sizeof(netdev_addr_t));
        }
    }

    return count;
}

netdev_addr_t *netdev_addr_find(uint32_t ifindex, uint16_t family, const void *addr) {
    if (!addr) return NULL;

    for (int i = 0; i < NETDEV_MAX_DEVICES * NETDEV_MAX_ADDRS; i++) {
        if (!addresses[i].used) continue;
        if (addresses[i].ifindex != ifindex) continue;
        if (addresses[i].family != family) continue;

        if (family == NETDEV_AF_INET) {
            if (addresses[i].addr.ipv4 == *(const uint32_t *)addr) {
                return &addresses[i];
            }
        }
    }

    return NULL;
}

// ============================================================================
// Routing Table
// ============================================================================

int netdev_route_add(uint16_t family, const void *dest, uint8_t prefix_len,
                     const void *gateway, uint32_t oif, uint32_t metric) {
    if (!dest) return -1;

    // Verify output interface exists if specified
    if (oif != 0 && !netdev_get_by_index(oif)) return -1;

    // Find free slot
    for (int i = 0; i < NETDEV_MAX_ROUTES; i++) {
        if (!routes[i].used) {
            routes[i].used = 1;
            routes[i].family = family;
            routes[i].prefix_len = prefix_len;
            routes[i].oif = oif;
            routes[i].metric = metric;

            if (family == NETDEV_AF_INET) {
                routes[i].dest.ipv4 = *(const uint32_t *)dest;
                if (gateway) {
                    routes[i].gateway.ipv4 = *(const uint32_t *)gateway;
                } else {
                    routes[i].gateway.ipv4 = 0;
                }
            }

            netdev_notify_route_change(&routes[i], 1);
            return 0;
        }
    }

    return -1;  // No free slots
}

int netdev_route_del(uint16_t family, const void *dest, uint8_t prefix_len) {
    if (!dest) return -1;

    for (int i = 0; i < NETDEV_MAX_ROUTES; i++) {
        if (!routes[i].used) continue;
        if (routes[i].family != family) continue;
        if (routes[i].prefix_len != prefix_len) continue;

        int match = 0;
        if (family == NETDEV_AF_INET) {
            match = (routes[i].dest.ipv4 == *(const uint32_t *)dest);
        }

        if (match) {
            netdev_route_t route_copy = routes[i];
            routes[i].used = 0;
            netdev_notify_route_change(&route_copy, 0);
            return 0;
        }
    }

    return -1;  // Route not found
}

int netdev_route_list(netdev_route_t *out_routes, int maxcount) {
    if (!out_routes) return -1;

    int count = 0;
    for (int i = 0; i < NETDEV_MAX_ROUTES && count < maxcount; i++) {
        if (routes[i].used) {
            memcpy(&out_routes[count++], &routes[i], sizeof(netdev_route_t));
        }
    }

    return count;
}

// Longest prefix match for IPv4
netdev_route_t *netdev_route_lookup(uint16_t family, const void *dest) {
    if (!dest || family != NETDEV_AF_INET) return NULL;

    uint32_t dest_ip = *(const uint32_t *)dest;
    netdev_route_t *best = NULL;
    int best_prefix = -1;

    for (int i = 0; i < NETDEV_MAX_ROUTES; i++) {
        if (!routes[i].used) continue;
        if (routes[i].family != family) continue;

        // Create mask for this route's prefix length
        uint32_t mask = 0;
        if (routes[i].prefix_len > 0) {
            mask = ~((1u << (32 - routes[i].prefix_len)) - 1);
        }

        // Check if destination matches this route's prefix
        if ((dest_ip & mask) == (routes[i].dest.ipv4 & mask)) {
            // Prefer longer prefix (more specific route)
            if ((int)routes[i].prefix_len > best_prefix) {
                best = &routes[i];
                best_prefix = routes[i].prefix_len;
            } else if ((int)routes[i].prefix_len == best_prefix && best) {
                // Same prefix length: prefer lower metric
                if (routes[i].metric < best->metric) {
                    best = &routes[i];
                }
            }
        }
    }

    return best;
}

// ============================================================================
// Event Notifications
// ============================================================================

void netdev_notify_link_change(uint32_t ifindex, uint32_t flags, uint8_t carrier) {
    // Build a NETDEV_NEW or link change notification
    uint8_t msg_buf[256];
    netctl_msg_header_t *hdr = (netctl_msg_header_t *)msg_buf;

    netctl_msg_init(hdr, NETCTL_MSG_NETDEV_NEW, NETCTL_FLAG_MULTICAST, 0, 0);
    netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_IFINDEX,
                        &ifindex, sizeof(ifindex));
    netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_FLAGS,
                        &flags, sizeof(flags));
    netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_CARRIER,
                        &carrier, sizeof(carrier));

    netctl_notify(NETCTL_GROUP_LINK, msg_buf, hdr->msg_len);
}

void netdev_notify_addr_change(uint32_t ifindex, uint16_t family, const void *addr,
                               uint8_t prefix_len, int added) {
    uint8_t msg_buf[256];
    netctl_msg_header_t *hdr = (netctl_msg_header_t *)msg_buf;

    uint16_t msg_type = added ? NETCTL_MSG_ADDR_NEW : NETCTL_MSG_ADDR_DEL;
    netctl_msg_init(hdr, msg_type, NETCTL_FLAG_MULTICAST, 0, 0);

    netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_IFINDEX,
                        &ifindex, sizeof(ifindex));
    netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_ADDR_FAMILY,
                        &family, sizeof(family));
    netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_ADDR_PREFIX,
                        &prefix_len, sizeof(prefix_len));

    if (family == NETDEV_AF_INET && addr) {
        netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_ADDR_VALUE,
                            addr, sizeof(uint32_t));
    }

    netctl_notify(NETCTL_GROUP_ADDR, msg_buf, hdr->msg_len);
}

void netdev_notify_route_change(const netdev_route_t *route, int added) {
    if (!route) return;

    uint8_t msg_buf[256];
    netctl_msg_header_t *hdr = (netctl_msg_header_t *)msg_buf;

    uint16_t msg_type = added ? NETCTL_MSG_ROUTE_NEW : NETCTL_MSG_ROUTE_DEL;
    netctl_msg_init(hdr, msg_type, NETCTL_FLAG_MULTICAST, 0, 0);

    if (route->family == NETDEV_AF_INET) {
        netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_ROUTE_DST,
                            &route->dest.ipv4, sizeof(route->dest.ipv4));
        if (route->gateway.ipv4 != 0) {
            netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_ROUTE_GW,
                                &route->gateway.ipv4, sizeof(route->gateway.ipv4));
        }
    }

    netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_ROUTE_OIF,
                        &route->oif, sizeof(route->oif));
    netctl_msg_add_attr(msg_buf, sizeof(msg_buf), NETCTL_ATTR_ROUTE_METRIC,
                        &route->metric, sizeof(route->metric));

    netctl_notify(NETCTL_GROUP_ROUTE, msg_buf, hdr->msg_len);
}

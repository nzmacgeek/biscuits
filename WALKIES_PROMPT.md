# Walkies - BlueyOS Network Configuration Tool

## Overview

You are tasked with implementing **walkies**, a userspace network configuration daemon/tool for BlueyOS. This tool will use the newly implemented Netlink-inspired control plane to configure network interfaces, IP addresses, and routes.

## Background

BlueyOS now has a complete Netlink-inspired control plane for network configuration:

- **AF_BLUEY_NETCTL** socket family for kernel ↔ user IPC
- Message-oriented protocol with TLV attributes
- Multicast event notifications for link, address, and route changes
- Full syscall support: `socket()`, `bind()`, `sendmsg()`, `recvmsg()`

The kernel no longer automatically configures networking. All network configuration must be performed from userspace using the netctl control plane.

## Primary Objectives

1. **Create a C program** (`user/walkies.c`) that configures network interfaces
2. **Use the netctl control plane** exclusively (no ioctl, no direct kernel calls)
3. **Read configuration** from `/etc/interfaces` (Debian-style format)
4. **Apply configuration** using netctl messages to:
   - Bring interfaces UP/DOWN
   - Configure IP addresses
   - Set MTU
   - Add/remove routes
5. **Monitor events** by subscribing to multicast groups
6. **Integrate with init** system so network is configured at boot

## Technical Specifications

### Control Plane API

#### Socket Family
```c
#define AF_BLUEY_NETCTL    2   // Network control socket family
#define SOCK_NETCTL        3   // Message-oriented netctl socket type
```

#### Creating a netctl socket
```c
int fd = socket(AF_BLUEY_NETCTL, SOCK_NETCTL, 0);
```

#### Binding to multicast groups
```c
uint32_t groups = NETCTL_GROUP_LINK | NETCTL_GROUP_ADDR | NETCTL_GROUP_ROUTE;
struct sockaddr_netctl {
    uint16_t family;      // AF_BLUEY_NETCTL
    uint16_t pad;
    uint32_t groups;      // Multicast group mask
};
struct sockaddr_netctl addr = { .family = AF_BLUEY_NETCTL, .groups = groups };
bind(fd, (struct sockaddr *)&addr, sizeof(addr));
```

#### Sending/Receiving messages
Use `sendmsg()` and `recvmsg()` with `struct msghdr` and `struct iovec`.

### Message Structure

All messages have a header followed by TLV attributes:

```c
typedef struct {
    uint32_t msg_len;      // Total message length including header
    uint16_t msg_type;     // Message type (NETCTL_MSG_*)
    uint16_t msg_flags;    // Message flags (NETCTL_FLAG_*)
    uint32_t msg_seq;      // Sequence number
    uint32_t msg_pid;      // Sender process ID
    uint16_t msg_version;  // Protocol version (currently 1)
    uint16_t msg_reserved;
} netctl_msg_header_t;

typedef struct {
    uint16_t attr_len;     // Length including header (4-byte aligned)
    uint16_t attr_type;    // Attribute type
    // followed by payload data
} netctl_attr_header_t;
```

### Message Types

#### Device Operations
- `NETCTL_MSG_NETDEV_LIST` (14) - List all network devices
- `NETCTL_MSG_NETDEV_GET` (12) - Get device info by ifindex
- `NETCTL_MSG_NETDEV_SET` (13) - Set device attributes (flags, MTU)

#### Address Operations
- `NETCTL_MSG_ADDR_NEW` (20) - Add IP address
- `NETCTL_MSG_ADDR_DEL` (21) - Remove IP address
- `NETCTL_MSG_ADDR_LIST` (23) - List addresses

#### Route Operations
- `NETCTL_MSG_ROUTE_NEW` (30) - Add route
- `NETCTL_MSG_ROUTE_DEL` (31) - Remove route
- `NETCTL_MSG_ROUTE_LIST` (33) - List routes

#### Events (multicast)
- `NETCTL_MSG_NETDEV_NEW` (10) - Link state changed
- `NETCTL_MSG_ADDR_NEW` (20) - Address added
- `NETCTL_MSG_ADDR_DEL` (21) - Address removed
- `NETCTL_MSG_ROUTE_NEW` (30) - Route added
- `NETCTL_MSG_ROUTE_DEL` (31) - Route removed

### Attribute Types

#### Device Attributes
- `NETCTL_ATTR_IFINDEX` (10) - Interface index (uint32_t)
- `NETCTL_ATTR_IFNAME` (11) - Interface name (string)
- `NETCTL_ATTR_MTU` (12) - MTU (uint32_t)
- `NETCTL_ATTR_MAC` (13) - MAC address (6 bytes)
- `NETCTL_ATTR_FLAGS` (14) - Device flags (uint32_t)
- `NETCTL_ATTR_CARRIER` (15) - Carrier state (uint8_t)

#### Address Attributes
- `NETCTL_ATTR_ADDR_FAMILY` (20) - Address family (uint16_t, 2=IPv4)
- `NETCTL_ATTR_ADDR_VALUE` (21) - Address value (4 bytes for IPv4)
- `NETCTL_ATTR_ADDR_PREFIX` (22) - Prefix length (uint8_t)

#### Route Attributes
- `NETCTL_ATTR_ROUTE_DST` (30) - Destination prefix (4 bytes IPv4)
- `NETCTL_ATTR_ROUTE_GW` (31) - Gateway address (4 bytes IPv4)
- `NETCTL_ATTR_ROUTE_OIF` (32) - Output interface index (uint32_t)
- `NETCTL_ATTR_ROUTE_METRIC` (33) - Route metric (uint32_t)

### Device Flags
- `NETCTL_FLAG_UP` (0x0001) - Interface is administratively up
- `NETCTL_FLAG_RUNNING` (0x0002) - Resources allocated
- `NETCTL_FLAG_CARRIER` (0x0004) - Physical link detected
- `NETCTL_FLAG_LOOPBACK` (0x0008) - Loopback interface
- `NETCTL_FLAG_BROADCAST` (0x0010) - Supports broadcast

### Multicast Groups
- `NETCTL_GROUP_NONE` (0) - No group
- `NETCTL_GROUP_LINK` (1) - Link events
- `NETCTL_GROUP_ADDR` (2) - Address events
- `NETCTL_GROUP_ROUTE` (4) - Route events
- `NETCTL_GROUP_ALL` (7) - All events

## Implementation Requirements

### Core Functionality

1. **Message Construction Helpers**
   - Function to initialize message header
   - Function to add TLV attributes to messages
   - Function to align attribute lengths to 4-byte boundaries

2. **Message Sending/Receiving**
   - Send request messages using `sendmsg()`
   - Receive response messages using `recvmsg()`
   - Parse TLV attributes from received messages
   - Handle errors and validate responses

3. **Configuration Parser**
   - Read `/etc/interfaces` file (can reuse kernel/netcfg.c parsing logic)
   - Parse interface stanzas: `iface <name> inet <method>`
   - Support `static` method with `address`, `netmask`, `gateway`
   - Support `loopback` method

4. **Configuration Application**
   - List existing network devices
   - For each configured interface:
     - Set interface UP (update flags)
     - Add IP address with prefix length
     - Add default route if gateway specified
     - Set MTU if specified

5. **Event Monitoring (Optional Phase 2)**
   - Subscribe to multicast groups
   - Receive and log link state changes
   - Receive and log address/route changes
   - Daemon mode to continuously monitor

### Example Workflow

```c
// 1. Open netctl socket
int sock = socket(AF_BLUEY_NETCTL, SOCK_NETCTL, 0);

// 2. List network devices
send_netctl_message(sock, NETCTL_MSG_NETDEV_LIST, ...);
recv_netctl_response(sock, &response);
// Parse response to find interface by name

// 3. Bring interface UP
netctl_msg_header_t msg;
netctl_msg_init(&msg, NETCTL_MSG_NETDEV_SET, 0, seq++, getpid());
netctl_msg_add_attr(&msg, NETCTL_ATTR_IFINDEX, &ifindex, sizeof(ifindex));
uint32_t flags = NETCTL_FLAG_UP | NETCTL_FLAG_RUNNING;
netctl_msg_add_attr(&msg, NETCTL_ATTR_FLAGS, &flags, sizeof(flags));
send_netctl_message(sock, &msg);

// 4. Add IP address
netctl_msg_init(&msg, NETCTL_MSG_ADDR_NEW, 0, seq++, getpid());
netctl_msg_add_attr(&msg, NETCTL_ATTR_IFINDEX, &ifindex, sizeof(ifindex));
uint16_t family = 2; // AF_INET
netctl_msg_add_attr(&msg, NETCTL_ATTR_ADDR_FAMILY, &family, sizeof(family));
uint32_t addr = htonl(0xC0A80164); // 192.168.1.100
netctl_msg_add_attr(&msg, NETCTL_ATTR_ADDR_VALUE, &addr, sizeof(addr));
uint8_t prefix = 24;
netctl_msg_add_attr(&msg, NETCTL_ATTR_ADDR_PREFIX, &prefix, sizeof(prefix));
send_netctl_message(sock, &msg);

// 5. Add default route
netctl_msg_init(&msg, NETCTL_MSG_ROUTE_NEW, 0, seq++, getpid());
uint32_t dest = 0; // 0.0.0.0 (default route)
netctl_msg_add_attr(&msg, NETCTL_ATTR_ROUTE_DST, &dest, sizeof(dest));
uint32_t gw = htonl(0xC0A80101); // 192.168.1.1
netctl_msg_add_attr(&msg, NETCTL_ATTR_ROUTE_GW, &gw, sizeof(gw));
netctl_msg_add_attr(&msg, NETCTL_ATTR_ROUTE_OIF, &ifindex, sizeof(ifindex));
send_netctl_message(sock, &msg);
```

## Integration with Init System

The walkies tool should be invoked early in the boot process:

1. **Option A: Run from init**
   - Modify `/user/init.c` to spawn `/bin/walkies` before starting shell
   - Run once to configure network, then exit

2. **Option B: Systemd-style service** (future)
   - Create `/etc/init.d/walkies` or similar
   - Run as daemon to monitor events

## File Locations

- Main program: `user/walkies.c`
- Header with constants: `user/walkies.h` or include from `kernel/netctl.h`
- Configuration: `/etc/interfaces` (read-only by walkies)
- Makefile integration: Add to `USER_TARGETS` in Makefile

## Testing Strategy

1. **Unit testing**: Test message construction/parsing
2. **Integration testing**:
   - Boot BlueyOS in QEMU
   - Create `/etc/interfaces` with test config
   - Run walkies and verify network configured correctly
   - Use `ping` or other tools to verify connectivity
3. **Event monitoring**: Subscribe to events and verify notifications received

## References

- Kernel headers: `kernel/netctl.h`, `kernel/netdev.h`
- Message format: See `kernel/netctl.c` for message processing
- Existing network config parser: `kernel/netcfg.c` (can reference but use netctl instead)
- Design checklist: `net/design.md`

## Success Criteria

- [ ] Walkies can list network interfaces using NETCTL_MSG_NETDEV_LIST
- [ ] Walkies can bring an interface UP using NETCTL_MSG_NETDEV_SET
- [ ] Walkies can add an IP address using NETCTL_MSG_ADDR_NEW
- [ ] Walkies can add a route using NETCTL_MSG_ROUTE_NEW
- [ ] Walkies reads `/etc/interfaces` and applies full configuration
- [ ] Network is functional after walkies runs (can ping, TCP works)
- [ ] No hard-coded network configuration remains in kernel
- [ ] Walkies integrates with init system for automatic boot-time configuration

## Notes

- Focus on correctness over performance
- Keep code simple and readable
- Use explicit error handling
- Log all operations for debugging
- The netctl protocol is extensible - add features incrementally
- Start with static configuration, DHCP can come later

Good luck! Let's get BlueyOS connected! 🐕🏃‍♂️

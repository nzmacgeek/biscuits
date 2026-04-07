# BlueyOS Networking Stack – Implementation Checklist

This checklist defines the required kernel infrastructure for the BlueyOS
network stack, with a strong focus on a Netlink-inspired control plane.

The goal is correctness, clarity, and long-term extensibility — not Linux
compatibility.

---

## Phase 0 – Ground Rules
- [ ] No ioctl-based networking control paths
- [ ] Prefer socket-based IPC for kernel ↔ user control
- [ ] Avoid bespoke networking syscalls where message passing suffices
- [ ] Design for async notifications from day one

---

## Phase 1 – Netlink‑Like Control Plane (FOUNDATIONAL)

### Socket Family
- [ ] Define a new socket family (e.g. `AF_BLUEY_NETCTL`)
- [ ] Socket type is message-oriented
- [ ] Kernel ↔ user space IPC via sockets only

### Message Format
- [ ] Message header:
  - [ ] message type
  - [ ] flags
  - [ ] sequence number
  - [ ] sender port / pid
  - [ ] protocol version
- [ ] Attribute system:
  - [ ] TLV-style attributes
  - [ ] Nestable attributes (optional but preferred)
  - [ ] Unknown attributes safely ignored

### Versioning & Capability Discovery
- [ ] Kernel reports protocol version
- [ ] Kernel reports supported message types
- [ ] Optional capability flags via `getsockopt` or control message

---

## Phase 2 – Multicast / Group Subscription

- [ ] Define multicast group identifiers:
  - [ ] Link events
  - [ ] Address events
  - [ ] Route events
- [ ] Allow sockets to subscribe to groups via `bind`
- [ ] Kernel can broadcast events to subscribed sockets
- [ ] User space subscribes once and reacts (no polling)

---

## Phase 3 – Core Kernel Networking Objects

### Network Device (`netdev`)
- [ ] Device name
- [ ] Interface index
- [ ] Flags:
  - [ ] UP
  - [ ] RUNNING
  - [ ] CARRIER
- [ ] MTU
- [ ] MAC address
- [ ] Driver pointer / ops table
- [ ] Reference counting or lifetime rules

### Address Objects
- [ ] Associated interface
- [ ] Address family (e.g. IPv4 initially)
- [ ] Prefix length
- [ ] Multiple addresses per interface supported

### Routing Table
- [ ] Destination prefix
- [ ] Gateway
- [ ] Output interface
- [ ] Metric
- [ ] Longest-prefix match lookup

---

## Phase 4 – Control Plane Operations

### Netdev Operations
- [ ] Enumerate devices
- [ ] Query device attributes
- [ ] Bring interface UP/DOWN
- [ ] Change MTU
- [ ] Report link state

### Address Operations
- [ ] Add address
- [ ] Remove address
- [ ] Enumerate addresses per interface

### Routing Operations
- [ ] Add route
- [ ] Remove route
- [ ] Enumerate routes

All operations must be accessible via the Netlink-like socket.

---

## Phase 5 – Kernel → User Notifications (MANDATORY)

- [ ] Emit event on link up/down
- [ ] Emit event on carrier change
- [ ] Emit event on address add/remove
- [ ] Emit event on route add/remove
- [ ] Events include enough attributes for user space to act

---

## Phase 6 – Syscall Surface (MINIMAL)

- [ ] `socket()` supports new control family
- [ ] `sendmsg()` / `recvmsg()` supported
- [ ] `bind()` supports multicast group subscription
- [ ] Optional: `getsockopt()` for discovery

No networking-specific syscalls beyond this phase.

---

## Phase 7 – Packet Path & Drivers (Short-Term)

- [ ] Packet buffer abstraction (simpler than `sk_buff`)
- [ ] Clear ownership rules for buffers
- [ ] Clean TX path (L3 → L2 → driver)
- [ ] Clean RX path (driver → L2 → L3)
- [ ] Interrupt-driven RX or NAPI-like polling

---

## Phase 8 – Medium-Term Design Goals

- [ ] Multiple TX/RX queues
- [ ] Basic offloads (e.g. checksum)
- [ ] Clear separation of:
  - [ ] L2 (Ethernet)
  - [ ] L3 (IP)
- [ ] Control plane remains protocol-agnostic

---

## Non-Goals (For Now)
- No Linux ABI compatibility
- No full Netlink reimplementation
- No containers or namespaces yet (but do not block them)

---
``

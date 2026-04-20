# BlueyOS Networking Stack – Implementation Checklist

This checklist defines the required kernel infrastructure for the BlueyOS
network stack, with a strong focus on a Netlink-inspired control plane.

The goal is correctness, clarity, and long-term extensibility — not Linux
compatibility.

---

## Phase 0 – Ground Rules
- [x] No ioctl-based networking control paths
- [x] Prefer socket-based IPC for kernel ↔ user control
- [x] Avoid bespoke networking syscalls where message passing suffices
- [x] Design for async notifications from day one

---

## Phase 1 – Netlink‑Like Control Plane (FOUNDATIONAL)

### Socket Family
- [x] Define a new socket family (e.g. `AF_BLUEY_NETCTL`)
- [x] Socket type is message-oriented
- [x] Kernel ↔ user space IPC via sockets only

### Message Format
- [x] Message header:
  - [x] message type
  - [x] flags
  - [x] sequence number
  - [x] sender port / pid
  - [x] protocol version
- [x] Attribute system:
  - [x] TLV-style attributes
  - [x] Nestable attributes (optional but preferred)
  - [x] Unknown attributes safely ignored

### Versioning & Capability Discovery
- [x] Kernel reports protocol version
- [x] Kernel reports supported message types
- [x] Optional capability flags via `getsockopt` or control message

---

## Phase 2 – Multicast / Group Subscription

- [x] Define multicast group identifiers:
  - [x] Link events
  - [x] Address events
  - [x] Route events
- [x] Allow sockets to subscribe to groups via `bind`
- [x] Kernel can broadcast events to subscribed sockets
- [x] User space subscribes once and reacts (no polling)

---

## Phase 3 – Core Kernel Networking Objects

### Network Device (`netdev`)
- [x] Device name
- [x] Interface index
- [x] Flags:
  - [x] UP
  - [x] RUNNING
  - [x] CARRIER
- [x] MTU
- [x] MAC address
- [x] Driver pointer / ops table
- [x] Reference counting or lifetime rules

### Address Objects
- [x] Associated interface
- [x] Address family (e.g. IPv4 initially)
- [x] Prefix length
- [x] Multiple addresses per interface supported

### Routing Table
- [x] Destination prefix
- [x] Gateway
- [x] Output interface
- [x] Metric
- [x] Longest-prefix match lookup

---

## Phase 4 – Control Plane Operations

### Netdev Operations
- [x] Enumerate devices
- [x] Query device attributes
- [x] Bring interface UP/DOWN
- [x] Change MTU
- [x] Report link state

### Address Operations
- [x] Add address
- [x] Remove address
- [x] Enumerate addresses per interface

### Routing Operations
- [x] Add route
- [x] Remove route
- [x] Enumerate routes

All operations must be accessible via the Netlink-like socket.

---

## Phase 5 – Kernel → User Notifications (MANDATORY)

- [x] Emit event on link up/down
- [x] Emit event on carrier change
- [x] Emit event on address add/remove
- [x] Emit event on route add/remove
- [x] Events include enough attributes for user space to act

---

## Phase 6 – Syscall Surface (MINIMAL)

- [x] `socket()` supports new control family
- [x] `sendmsg()` / `recvmsg()` supported
- [x] `bind()` supports multicast group subscription
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

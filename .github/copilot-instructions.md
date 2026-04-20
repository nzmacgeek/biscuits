# GitHub Copilot Instructions — BlueyOS / biscuits

> This is a **kernel research project** for BlueyOS, a Linux-like OS for i386.
> Read [AGENT_INSTRUCTIONS.md](../AGENT_INSTRUCTIONS.md) for the full picture.

## Quick reference

- Kernel: C11, freestanding, 4-space indent, no libc
- Network control: `AF_BLUEY_NETCTL` sockets (not ioctl, not Linux Netlink)
- Build: `make` (kernel ELF), `make disk-musl` (bootable image), `make run` (QEMU)
- Verbosity: `syslog_get_verbose()` / `VERBOSE_QUIET=0`, `VERBOSE_INFO=1`, `VERBOSE_DEBUG=2`

## Ecosystem repos

The authoritative repo-purpose mapping lives in `AGENT_INSTRUCTIONS.md`. Use the
list below as a quick repo index only, and refer to the root instructions file
for each repo's current purpose/role.

| Repo |
|------|
| `nzmacgeek/biscuits` |
| `nzmacgeek/claw` |
| `nzmacgeek/matey` |
| `nzmacgeek/walkies` |
| `nzmacgeek/yap` |
| `nzmacgeek/dimsim` |
| `nzmacgeek/musl-blueyos` |
| `nzmacgeek/blueyos-bash` |
| `nzmacgeek/scout` |

## Key conventions

- All new syscalls: add to `kernel/syscall.h` AND `kernel/syscall.c` dispatch table
- Network: use `netctl` control plane only — no hard-coded IP config in kernel
- DHCP: handled by **scout** userspace daemon via netctl
- When fixing a defect, add an entry to `docs/DEFECT_ANALYSIS.md`
- Store useful facts with `store_memory` so future agents benefit

## Network stack

The kernel provides `AF_BLUEY_NETCTL` sockets for network configuration.
Userspace tools (walkies, scout) use `socket(AF_BLUEY_NETCTL, SOCK_NETCTL, 0)`.

Control plane message flow:
1. Userspace sends `NETCTL_MSG_*` request via `sendmsg()`
2. Kernel processes in `netctl_process_message()`
3. Kernel responds or sends multicast notification
4. Userspace reads response via `recvmsg()`

See `kernel/netctl.h` and `net/design.md` for protocol details.

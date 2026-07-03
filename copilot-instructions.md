# GitHub Copilot Instructions — BlueyOS / biscuits

> This is a **kernel research project** for BlueyOS, a Linux-like OS for i386.
> Read [AGENT_INSTRUCTIONS.md](./AGENT_INSTRUCTIONS.md) for full project context.

## Agent operating mode (efficiency first)

- Keep plans minimal and execution focused; avoid broad exploratory searches when a direct file read will do.
- Prefer targeted edits over large refactors.
- Reuse existing patterns and interfaces already present in this repo.
- Before proposing new abstractions, verify there is no existing equivalent in `kernel/`, `net/`, or current docs.

## Quick reference

- Kernel: C11, freestanding, 4-space indent, no libc
- Network control: `AF_BLUEY_NETCTL` sockets (not ioctl, not Linux Netlink)
- Build: `make` (kernel ELF), `make disk-musl` (bootable image), `make run` (QEMU)
- Verbosity: `syslog_get_verbose()` / `VERBOSE_QUIET=0`, `VERBOSE_INFO=1`, `VERBOSE_DEBUG=2`

## Key conventions

- All new syscalls: add to `kernel/syscall.h` AND `kernel/syscall.c` dispatch table
- Network: use `netctl` control plane only — no hard-coded IP config in kernel
- DHCP: handled by **scout** userspace daemon via netctl
- When fixing a defect, add an entry to `docs/DEFECT_ANALYSIS.md`

## WSL (Windows) install/dev with QEMU

Use this flow for local development on WSL2 Ubuntu.

1. Install dependencies:
   - `sudo apt update`
   - `sudo apt install -y build-essential make gcc nasm qemu-system-x86 mtools`
2. Clone and enter repo:
   - `git clone https://github.com/nzmacgeek/biscuits.git`
   - `cd biscuits`
3. Build kernel/artifacts:
   - `make`
4. Build bootable disk image:
   - `make disk-musl`
5. Run in QEMU:
   - `make run`

Notes:
- If hardware acceleration is unavailable in WSL, run without assuming KVM support.
- Keep display requirements minimal; if GUI forwarding is unavailable, use serial/monitor options already defined by project make targets.

## Headless Ubuntu server install/dev

Use this flow on a remote Ubuntu server (no desktop UI).

1. Install dependencies:
   - `sudo apt update`
   - `sudo apt install -y build-essential make gcc nasm qemu-system-x86 mtools`
2. Clone and enter repo:
   - `git clone https://github.com/nzmacgeek/biscuits.git`
   - `cd biscuits`
3. Build:
   - `make`
   - `make disk-musl`
4. Run headless with serial console:
   - Prefer existing project target/settings first (`make run` if already headless-friendly).
   - If a direct QEMU invocation is needed, use nographic/serial mode and point it at the produced disk image.

Headless expectations:
- Use serial output for logs and debugging.
- Avoid adding GUI-only run assumptions in docs or scripts.

## Network stack

The kernel provides `AF_BLUEY_NETCTL` sockets for network configuration.
Userspace tools (walkies, scout) use `socket(AF_BLUEY_NETCTL, SOCK_NETCTL, 0)`.

Control plane message flow:
1. Userspace sends `NETCTL_MSG_*` request via `sendmsg()`
2. Kernel processes in `netctl_process_message()`
3. Kernel responds or sends multicast notification
4. Userspace reads response via `recvmsg()`

See `kernel/netctl.h` and `net/design.md` for protocol details.

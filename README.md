# BlueyOS

```
  ____  _                    ___  ____
 | __ )| |_   _  ___ _   _ / _ \/ ___|
 |  _ \| | | | |/ _ \ | | | | | \___ \
 | |_) | | |_| |  __/ |_| | |_| |___) |
 |____/|_|\__,_|\___|\___, |\___/|____/
                        |___/
  Where Every Boot is a New Adventure!
```

---

> ## ⚠️ IMPORTANT DISCLAIMER
>
> **This is a VIBE CODED, AI-GENERATED RESEARCH PROJECT.**
>
> BlueyOS was created by **GitHub Copilot (an AI agent)** as a learning experiment to explore OS development concepts and the capabilities of AI agents on complex, multi-file software projects.
>
> **DO NOT USE THIS IN PRODUCTION. DO NOT USE THIS ON REAL HARDWARE. DO NOT TRUST IT WITH REAL DATA.**
>
> - It has known security limitations (see [SECURITY.md](SECURITY.md))
> - It is incomplete and experimental  
> - It has not been audited or peer-reviewed
> - It exists purely for educational purposes

---

> **Bluey and all related characters are trademarks of Ludo Studio Pty Ltd, licensed by BBC Studios.**
> BlueyOS is an unofficial fan/research project with no affiliation to, or endorsement by, Ludo Studio or the BBC.
> "Bluey" is a registered trademark. All character names, likenesses and episode references belong to their respective owners.

---

## What is BlueyOS?

BlueyOS is a minimal x86 operating system kernel written in C and NASM assembly, themed around the beloved Australian kids TV show *Bluey*. It was built by an AI coding agent to demonstrate:

- How AI agents approach complex, multi-component software projects
- Core OS development concepts (bootloaders, memory management, interrupts, filesystems, etc.)
- How to structure a freestanding C + assembly project that boots via GRUB/QEMU

Every component is named after a Bluey character, and Bluey references are woven throughout the code comments and messages.

---

## Character to Component Mapping

| Character | Component | Quote |
|-----------|-----------|-------|
| **Bandit** (Dad) | Root user, kernel init, GDT, scheduler | "I reckon, yeah." |
| **Chilli** (Mum) | Interrupt configuration, system calls | "She's got it sorted!" |
| **Bluey** | Kernel process, syscall interface | "It's a big day!" |
| **Bingo** | Timer, keyboard, filesystem | "Tap tap tap!" |
| **Nana** | IRQ remapping | "Nana would be proud!" |
| **Jack** | Network driver (NE2000) | "Jack's Network Snorkel" |
| **Judo** | ELF loader | "Flipping programs into memory!" |
| **Calypso** | ISR/exception handler | "Sometimes things go wrong, and that's okay." |

---

## Architecture Overview

```
+------------------+
|   GRUB Multiboot |  boot/boot.asm - GRUB loads us at 1MB physical
+------------------+
|   kernel_main()  |  kernel/kernel.c - init sequence
+------------------+
|  CPU Tables      |  GDT (ring0/ring3 + TSS) | IDT (256 gates)
|  Interrupts      |  ISR (exceptions 0-31)   | IRQ (PIC->INT 32-47)
|  Memory          |  Physical frame allocator + 4KB paging
|  Heap            |  Linked-list allocator (magic 0xB10EB10E)
|  Processes       |  PCB + round-robin scheduler
|  Syscalls        |  int 0x80 (POSIX-like, Linux-compatible numbers)
|  ELF Loader      |  ELF32 executable loading
+------------------+
|  Drivers         |  VGA | PS/2 Keyboard | ATA PIO | NE2000 network
+------------------+
|  Filesystem      |  VFS layer -> FAT16 implementation
+------------------+
|  Users           |  passwd/shadow, SHA-256 salted passwords
|  System Info     |  Hostname, domain, Brisbane timezone, Bandit epoch
+------------------+
```

---

## Prerequisites

```bash
# Debian/Ubuntu
sudo apt-get install -y \
    nasm \
    gcc-multilib \
    binutils \
    qemu-system-x86 \
    grub-pc-bin \
    grub-common \
    xorriso
```

---

## Building

```bash
make                   # Build kernel ELF (build #1)
make BUILD_NUMBER=42   # Build with a specific build number
make iso               # Create bootable ISO
make run               # Launch in QEMU
make version           # Print version info
make clean             # Clean build artifacts
```

See [TESTING.md](TESTING.md) for detailed testing instructions and expected output.

---

## Build Version System

Every build captures:

| Variable | Source | Example |
|----------|--------|---------|
| `BUILD_NUMBER` | `make BUILD_NUMBER=N` | `1` |
| `BUILD_DATE` | `date -u` at compile time | `2026-03-29` |
| `BUILD_TIME` | `date -u` at compile time | `04:27:00` |
| `BUILD_HOST` | `hostname` on build machine | `mycomputer` |
| `BUILD_USER` | `whoami` on build machine | `alice` |

All injected as `-D` flags, available in `include/version.h`.

---

## Bandit's Birthday Epoch

BlueyOS uses its own time epoch: **Bandit Heeler's fictional birthday -- October 15, 1980, 00:00:00 AEST**.

In UTC this is October 14, 1980 14:00:00 UTC = Unix timestamp `340405200`.

```c
uint32_t bluey_to_unix(uint32_t bluey_secs);   // BlueyOS epoch -> Unix
uint32_t unix_to_bluey(uint32_t unix_secs);    // Unix -> BlueyOS epoch
void     unix_to_datetime(...);                // Unix -> human date (Brisbane AEST)
```

---

## Brisbane Timezone

Default timezone is **AEST (UTC+10), no DST** -- because Queensland famously does not observe Daylight Saving Time.

```c
#define BLUEYOS_TZ_NAME        "AEST"
#define BLUEYOS_TZ_OFFSET_SEC  36000   // +10:00
#define BLUEYOS_TZ_DST         0       // No DST - Queensland style!
```

---

## User Accounts

Default users (for testing ONLY):

| Username | UID | Password | Notes |
|----------|-----|----------|-------|
| `bandit` | 0 | `bandit2dad` | Root |
| `bluey`  | 1 | `woohoo123` | Standard user |
| `bingo`  | 2 | (locked) | Account locked |
| `chilli` | 3 | (none) | No password |
| `jack`   | 4 | (none) | No password |
| `judo`   | 5 | (none) | No password |

Passwords stored SHA-256 + salt hashed in /etc/shadow format. See [SECURITY.md](SECURITY.md).

---

## Syscall Interface (int 0x80)

| Number | Name | Description |
|--------|------|-------------|
| 0 | SYS_READ | Read from fd (fd=0 = keyboard) |
| 1 | SYS_WRITE | Write to fd (fd=1 = VGA screen) |
| 20 | SYS_GETPID | Get process ID |
| 24 | SYS_GETUID | Get user ID |
| 60 | SYS_EXIT | Exit process |
| 63 | SYS_UNAME | Get system information |
| 125 | SYS_GETHOSTNAME | Get hostname |

---

## Related Resources

- [Bluey official website](https://www.bluey.tv/)
- [Ludo Studio](https://www.ludostudio.com.au/) -- creators of Bluey
- [OSDev Wiki](https://wiki.osdev.org/) -- OS development reference
- [POSIX.1 standard](https://pubs.opengroup.org/onlinepubs/9799919799/)

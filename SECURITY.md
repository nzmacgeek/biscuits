# BlueyOS Security Policy

> ## ⚠️ RESEARCH PROJECT — NOT FOR PRODUCTION USE
>
> **BlueyOS is a vibe-coded, AI-generated research operating system.**
> It has NEVER been security audited. Do not run it in any environment where security matters.
> Do not use it on real hardware. Do not store real data on it.

> **Bluey and all related characters are trademarks of Ludo Studio Pty Ltd, licensed by BBC Studios.**
> BlueyOS is an unofficial fan/research project.

---

## Purpose

This document describes the secure-by-design practices that *were* implemented in BlueyOS, and the known security limitations. It exists to demonstrate that even a research/learning OS should think about security architecture.

---

## Secure-by-Design Practices Implemented

These are real security controls present in the codebase:

### 1. Ring 0 / Ring 3 Privilege Separation

The GDT (`kernel/gdt.c`) defines separate kernel (ring 0) and user (ring 3) code/data segments. Kernel code runs at CPL=0 ("Bandit's domain — grown-ups only"). User code runs at CPL=3 ("Bluey's domain — where the kids play"). The TSS enables stack switching on privilege transitions.

### 2. PBKDF2-SHA256 Password Hashing

Passwords are never stored in plaintext. `/etc/shadow`-style entries use the format:

```
$pbkdf2-sha256$<iters>$<32 hex salt>$<64 hex hash>
```

The hash uses PBKDF2-HMAC-SHA256 with a configurable iteration count. The implementation is in `kernel/password.c` and `kernel/sha256.c`.

> **Production note:** For a real OS, use **bcrypt**, **scrypt**, or **argon2id** which include memory-hard work factors. PBKDF2 is an improvement over plain SHA-256, but still not ideal for production.

### 3. UID/GID Separation

Every process has a UID and GID (`kernel/process.h`). Root is UID=0 (Bandit). User-level processes run as UID >= 1. `multiuser_is_root()` checks are used before privileged operations.

### 4. Bounds-Checked Keyboard Buffer

The PS/2 keyboard driver (`drivers/keyboard.c`) uses a circular ring buffer of exactly 256 bytes with hard bounds enforcement:

```c
uint32_t next_head = (kb_head + 1) & (KB_BUF_SIZE - 1);
if (next_head != kb_tail) {   // only write if not full
    kb_buf[kb_head] = c;
    kb_head = next_head;
}
```

Keys are silently dropped if the buffer is full — no overflow possible.

### 5. Heap Block Magic for Corruption Detection

The kernel heap allocator (`kernel/kheap.c`) tags every block header with magic `0xB10EB10E`. On `kheap_free()`, the magic is verified:

```c
if (blk->magic != BLUEY_HEAP_MAGIC) {
    kprintf("[HEP] WARN: heap magic mismatch! Heap corruption?\n");
    return;
}
```

### 6. Multiboot Magic Validation

`kernel_main()` validates the multiboot magic number (`0x2BADB002`) before proceeding. If booted by a non-compliant bootloader, the kernel panics immediately.

### 7. ELF Loader NULL Guard

The ELF loader (`kernel/elf.c`) refuses to load any segment into the first page (address < 0x1000):

```c
if (ph->p_vaddr < 0x1000) {
    kprintf("[ELF] Segment tries to load at NULL page - denied!\n");
    return -1;
}
```

### 8. ELF File Bounds Checking

Before loading any ELF segment, the loader verifies that the segment's file offset + size does not exceed the file length, preventing reads beyond the buffer.

### 9. Syscall Write Limit

`SYS_WRITE` caps the write length at 4096 bytes per call to prevent a single syscall from flooding the screen buffer indefinitely.

### 10. Account Locking

Shadow entries can be set to `!` (locked) or `*` (no password). Bingo's account is locked by default because "Bingo's not old enough!"

---

## Known Security Limitations

This is a research OS. There are **many** known limitations:

1. **No ASLR** — kernel and user processes load at fixed addresses
2. **No stack canaries** (`-fno-stack-protector` is required for freestanding builds)
3. **No NX bit** — no W^X enforcement on memory pages
4. **PBKDF2 iteration counts are conservative** — not suitable for production password storage
5. **Hardcoded default passwords** — never acceptable in production
6. **No process isolation** — user processes share the same page directory as the kernel
7. **No SMEP/SMAP** — no protection against kernel executing/accessing user memory
8. **No audit logging** — no record of login attempts or privilege escalation
9. **No network security** — the NE2000 driver accepts all packets with no filtering
10. **No cryptographic random number generator** — salt generation uses timer ticks
11. **No secure boot** — no verification of kernel image integrity
12. **No memory encryption** — all memory is plaintext

---

## Reporting Issues

Since this is a research project, security issues should be reported as **GitHub Issues** in the repository. Label them `security`.

There is no bug bounty. There is no SLA. This is a learning project. But we do appreciate responsible disclosure so the documentation can be improved.

---

## Threat Model

BlueyOS's threat model is: **there is no threat model**. It is a single-user educational kernel designed to run in QEMU. Do not expose it to untrusted input, networks, or users.

*"Dad, what's a threat model?" - Bluey*
*"Something grown-ups worry about, mate." - Bandit*

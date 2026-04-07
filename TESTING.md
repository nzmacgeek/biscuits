# BlueyOS Testing Guide

> **Bluey and all related characters are trademarks of Ludo Studio Pty Ltd, licensed by BBC Studios.**
> BlueyOS is an unofficial AI-generated research project. See README.md for full disclaimer.

---

## Prerequisites

Install the following tools before testing:

```bash
sudo apt-get install -y \
    nasm \
    gcc-multilib \
    binutils \
    qemu-system-x86 \
    grub-pc-bin \
    grub-common \
    xorriso
```

Verify your tools:

```bash
nasm --version          # should be 2.x or later
gcc --version           # should include 32-bit support (multilib)
qemu-system-i386 --version
grub-mkrescue --version
```

---

## Step 1: Build the Kernel

```bash
# From the repository root
make BUILD_NUMBER=1
```

**Expected output:**

```
  [AS]  boot/boot.asm
  [CC]  kernel/gdt.c
  [CC]  kernel/idt.c
  ... (all source files compiled) ...
  [LD]  Linked blueyos.elf

  G'day! BlueyOS Build #1 complete!
  Built by <youruser>@<yourhostname> on <date> <time>
  Kernel: blueyos.elf
```

**Verify the ELF:**

```bash
file blueyos.elf
# Expected: ELF 32-bit LSB executable, Intel 80386
ls -lh blueyos.elf
# Expected: ~50KB
```

---

## Step 2: Build the Bootable ISO

```bash
make iso
# or: bash tools/mkdisk.sh
```

**Expected output:**

```
BlueyOS: Building ISO image...
  (This is the best day EVER! - Bluey)
Done! build/blueyos.iso created (~4.0M)
Run with: bash tools/qemu-run.sh
```

**Verify the ISO:**

```bash
file build/blueyos.iso
# Expected: ISO 9660 CD-ROM filesystem data
```

## Step 2b: Build the BlueyOS Disk Image

```bash
make disk
```

Expected outcomes:

- `build/blueyos-disk.img` is created under `build/`
- the root BlueyFS partition contains `/bin/init`
- `/etc/fstab` contains at least:

```text
/dev/hda2 / blueyfs defaults 0 1
/dev/hda3 none swap defaults 0 0
```

Sanity-check the root partition with the host fsck tool:

```bash
./build/tools/fsck_blueyfs -o 67584 build/blueyos-disk.img   # 67584 = default BlueyFS root partition LBA
```

---

## Step 3: Run in QEMU

```bash
make run
# or: bash tools/qemu-run.sh
```

**Or run manually with extra options:**

```bash
qemu-system-i386 \
    -drive file=build/blueyos-disk.img,format=raw,if=ide,index=0 \
    -boot c \
    -m 256M \
    -serial stdio \
    -no-reboot \
    -no-shutdown
```

**To quit QEMU:** Press `Ctrl+A` then `X`.

---

## Step 4: Expected Boot Sequence

When BlueyOS boots successfully, you should see the following output in order:

### 4.1 GRUB Menu (5 second timeout)

```
                        GNU GRUB  version 2.x

  BlueyOS - It's a big day!
  BlueyOS - Safe Mode (No Rules, No Fun)
  Reboot
```

Select the first entry (or wait for timeout).

### 4.2 BlueyOS Banner

```
  ____  _                    ___  ____
 | __ )| |_   _  ___ _   _ / _ \/ ___|
 |  _ \| | | | |/ _ \ | | | | | \___ \
 | |_) | | |_| |  __/ |_| | |_| |___) |
 |____/|_|\__,_|\___|\___, |\___/|____/
                        |___/
  Where Every Boot is a New Adventure!
  Codename: Bandit | v0.1.0
```

### 4.3 Version and Build Information

```
  BlueyOS v0.1.0 (Build #1)
  Codename : Bandit
  Built by : <user>@<hostname>
  Date     : <YYYY-MM-DD> <HH:MM:SS>
  Cheeky Nuggies Mode: ENABLED

  Bluey (c) Ludo Studio Pty Ltd. Licensed by BBC Studios.
  BlueyOS is an unofficial AI research project. NOT FOR PRODUCTION.
```

### 4.4 Init Sequence Messages

Each subsystem prints a message as it initialises:

```
[GDT]  Bandit set up the Descriptor Table - like building a cubby house!
[IDT]  Chilli configured the Interrupts - she's got it sorted!
[ISR]  Exception handlers online - no crashing allowed at this playdate!
[IRQ]  Hardware interrupts remapped - Nana would be proud!
[TMR]  Bingo's Tick Tock Timer is ticking!
[KBD]  Bingo's Keyboard ready - Tap tap tap!
[HEP]  Kernel heap ready - plenty of room to play!
[PGE]  Paging enabled - Bandit mapped all the rooms!
[SYS]  Hostname: blueyos.local  Timezone: AEST (UTC+10, no DST - Queensland style!)
[SYS]  Bandit's Birthday Epoch: Bandit's Birthday (15 Oct 1980 AEST)
[USR]  Multiuser system ready - who's playing today?
[USR]  Users: bandit(uid=0), bluey(uid=1), bingo(uid=2), chilli(uid=3), jack(uid=4), judo(uid=5)
[DRV]  Driver framework ready - Bandit's toolbox is open!
[VFS]  Bingo's Backpack Filesystem mounted!
[VFS]  Registered filesystem: fat16
[VFS]  Registered filesystem: biscuitfs
[ATA]  ATA disk driver online - let's find some data!       (or: No ATA device found)
[BISCUITFS] Mounted 'BlueyRoot' (...)
[ROOT] Mounted /dev/hda1 on biscuitfs
[SWAP] Swap space ready: ... label='ChatterSwap'
[FSTAB] Applied 1 mount directives
[SYS]  Bluey's Daddy Daughter Syscalls are ready!
[PRC]  Process table initialised - everyone gets a turn!
[SCH]  Bandit's Homework Scheduler is running!
[NET]  NE2000: No card found - Jack is still learning to swim!
      (or: NE2000 found at I/O 0x300 if a NE2000 card is present)
[ELF]  Judo's ELF Loader: ready to flip some programs!
```

### 4.5 Final Ready Message

```
[OK]   G'day mate! Welcome to BlueyOS! She's all yours!
Hostname : blueyos.local
Timezone : AEST (UTC+10, Brisbane - no DST because Queensland!)
Epoch    : Bandit's Birthday (15 Oct 1980 AEST)

BlueyOS is ready. Type your commands below.
"This is the best day EVER!" - Bluey Heeler
```

You should then see `/bin/init` launch from BlueyFS and pass its smoke tests:

```text
[KERN] Bootstrap launching /bin/init as pid=2
[init] userspace bootstrap ok
[init-parent] waitpid ok
[init] all tests passed
```

After this the kernel starts the interactive shell.

### 4.6 TTY / Shell Smoke Test

At the `bluey@biscuit:/$` prompt, verify that keyboard input is echoed back through
the terminal and that the built-in diagnostic commands work:

```text
help
uname
version
meminfo
free
swapinfo
```

---

## Build Version Testing

Verify that build information is captured correctly:

```bash
# Build with a different build number
make BUILD_NUMBER=7

# Expected in boot output:
#   BlueyOS v0.1.0 (Build #7)
#   Built by: <your user>@<your hostname>
```

```bash
# Print version without building
make version
# Expected:
#   BlueyOS v0.1.0 (Codename: Bandit)
#   Build #1 | 2026-03-29 04:27:00
#   Built by: runner@hostname
```

---

## Testing Without a Disk Image (ATA)

QEMU without a disk image will show:

```
[ATA]  No ATA device found (Jack forgot the disk!)
[VFS]  No FAT16 volume at LBA 63 - running diskless
```

This is expected and normal — the OS continues to boot without disk access.

---

## Adding a FAT16 Disk Image (Optional)

To test the FAT16 filesystem, create a FAT16 disk image:

Or use the repository helper to create the default extra FAT log disk image:

```bash
make fat-log-disk
```

```bash
# Create a 32MB FAT16 image
dd if=/dev/zero of=disk.img bs=1M count=32
mkfs.fat -F 16 disk.img

# Mount and add some files (Linux)
sudo mount -o loop disk.img /mnt
echo "G'day from BlueyOS!" | sudo tee /mnt/hello.txt
sudo umount /mnt

# Run QEMU with the disk
qemu-system-i386 \
    -cdrom blueyos.iso \
    -hda disk.img \
    -m 256M \
    -serial stdio \
    -no-reboot
```

---

## Kernel Panic Testing

To verify the kernel panic handler works, you can trigger an exception. The panic message should look like:

```
*** Oh no! [Bandit voice]: KERNEL PANIC! ***
  <reason>
  The playdate is over. Please reset the computer.
  (BlueyOS research OS - see SECURITY.md for known limitations)
```

---

## Debug Mode (QEMU Flags)

Useful QEMU flags for debugging:

```bash
# Log CPU instructions (very verbose!)
qemu-system-i386 -cdrom blueyos.iso -d in_asm 2>&1 | head -100

# Attach GDB
qemu-system-i386 -cdrom blueyos.iso -s -S &
gdb -ex "target remote :1234" -ex "set architecture i386" blueyos.elf

# Show VGA only (no serial)
qemu-system-i386 -cdrom blueyos.iso -m 256M -no-reboot
```

---

## Known Limitations and Expected Failures

| Feature | Status | Notes |
|---------|--------|-------|
| Screen output | Working | VGA text mode 80x25 |
| Keyboard input | Working | PS/2 keyboard, US QWERTY |
| Timer | Working | PIT at 1000Hz |
| GDT/IDT/ISR/IRQ | Working | Ring 0/3, exceptions, hardware IRQs |
| Paging | Working | Identity-mapped 4MB, frame allocator |
| Heap | Working | Linked list, magic number protection |
| Multiuser | Working | In-memory passwd/shadow, SHA-256 |
| Hostname/timezone | Working | Brisbane AEST, Bandit epoch |
| ATA disk | Probe only | Works in QEMU with `-hda`; no disk = graceful degradation |
| FAT16 read | Implemented | Requires ATA disk with FAT16 partition |
| FAT16 write | Not implemented | Read-only for now |
| Process scheduling | Basic | Round-robin, no context switch registers yet |
| ELF loading | Implemented | ELF32 x86 executables |
| NE2000 network | Probe only | Registers driver if card found |
| Syscalls | Basic | Read/write/getpid/getuid/exit/uname/gethostname |

---

## Build Number Convention

BlueyOS uses a monotonically increasing build number:

```
Build #1  — initial development build
Build #2  — second iteration
Build #N  — ...
```

Increment the build number with each significant change:

```bash
make BUILD_NUMBER=2   # next build
make BUILD_NUMBER=3   # and so on
```

The build number, host, user, date and time are all embedded in the kernel binary and printed at boot.

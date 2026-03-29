# BlueyOS Build System
# "Let's play!" - Bluey, Season 1
#
# Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
# licensed by BBC Studios. BlueyOS is an unofficial fan/research project
# with no affiliation to Ludo Studio or the BBC.
#
# ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
#
# Usage:
#   make                     - build kernel ELF
#   make iso                 - build bootable ISO image
#   make run                 - build ISO and launch in QEMU
#   make version             - print version information
#   make clean               - remove build artifacts
#   make BUILD_NUMBER=N      - build with a specific build number

CC  = gcc
AS  = nasm
LD  = ld

# ---------------------------------------------------------------------------
# Build version injection
# Increment BUILD_NUMBER with each release: make BUILD_NUMBER=2
# BUILD_HOST and BUILD_USER are captured automatically from the build machine.
# ---------------------------------------------------------------------------
BUILD_NUMBER ?= 1
BUILD_DATE   := $(shell date -u '+%Y-%m-%d')
BUILD_TIME   := $(shell date -u '+%H:%M:%S')
BUILD_HOST   := $(shell hostname 2>/dev/null || echo unknown-host)
BUILD_USER   := $(shell whoami  2>/dev/null || echo unknown-user)

CFLAGS = \
    -m32 \
    -ffreestanding \
    -O2 \
    -Wall \
    -Wextra \
    -Wno-unused-parameter \
    -fno-stack-protector \
    -nostdlib \
    -nostdinc \
    -fno-builtin \
    -fno-pic \
    -DBLUEYOS_BUILD_NUMBER=$(BUILD_NUMBER) \
    -DBLUEYOS_BUILD_HOST=\"$(BUILD_HOST)\" \
    -DBLUEYOS_BUILD_USER=\"$(BUILD_USER)\" \
    -I include \
    -I .

ASFLAGS = -f elf32

LDFLAGS = -m elf_i386 -T linker.ld --no-warn-rwx-segments

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
C_SOURCES = \
    kernel/kernel.c \
    kernel/gdt.c \
    kernel/idt.c \
    kernel/isr.c \
    kernel/irq.c \
    kernel/timer.c \
    kernel/kheap.c \
    kernel/paging.c \
    kernel/process.c \
    kernel/scheduler.c \
    kernel/syscall.c \
    kernel/elf.c \
    kernel/sha256.c \
    kernel/multiuser.c \
    kernel/sysinfo.c \
    kernel/swap.c \
    drivers/vga.c \
    drivers/keyboard.c \
    drivers/ata.c \
    drivers/driver.c \
    drivers/net/ne2000.c \
    drivers/net/network.c \
    fs/vfs.c \
    fs/fat.c \
    fs/blueyfs.c \
    net/tcpip.c \
    net/arp.c \
    net/ip.c \
    net/icmp.c \
    net/udp.c \
    net/tcp.c \
    shell/shell.c \
    lib/string.c \
    lib/stdio.c \
    lib/stdlib.c

ASM_SOURCES = \
    boot/boot.asm \
    kernel/gdt_flush.asm \
    kernel/idt_flush.asm \
    kernel/isr_stubs.asm \
    kernel/irq_stubs.asm \
    kernel/syscall_stub.asm \
    kernel/paging_enable.asm

C_OBJECTS   = $(C_SOURCES:.c=.o)
ASM_OBJECTS = $(ASM_SOURCES:.asm=.o)
OBJECTS     = $(ASM_OBJECTS) $(C_OBJECTS)

TARGET = blueyos.elf
ISO    = blueyos.iso

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
.PHONY: all iso run version clean help tools-host

all: $(TARGET)
	@echo ""
	@echo "  G'day! BlueyOS Build \#$(BUILD_NUMBER) complete!"
	@echo "  Built by $(BUILD_USER)@$(BUILD_HOST) on $(BUILD_DATE) $(BUILD_TIME)"
	@echo "  Kernel: $(TARGET)"
	@echo ""

$(TARGET): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "  [LD]  Linked $@"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "  [CC]  $<"

%.o: %.asm
	$(AS) $(ASFLAGS) $< -o $@
	@echo "  [AS]  $<"

iso: $(TARGET)
	@bash tools/mkdisk.sh

run: iso
	@bash tools/qemu-run.sh

version:
	@echo "BlueyOS v0.1.0 (Codename: $(shell grep CODENAME include/version.h | head -1 | sed 's/.*\"\(.*\)\".*/\1/'))"
	@echo "Build \#$(BUILD_NUMBER) | $(BUILD_DATE) $(BUILD_TIME)"
	@echo "Built by: $(BUILD_USER)@$(BUILD_HOST)"

clean:
	@find . \( -name '*.o' -o -name '*.d' \) -not -path './.git/*' -delete
	@rm -f $(TARGET) $(ISO) tools/mkfs_blueyfs
	@rm -rf isodir/
	@echo "  Clean! Bluey would be proud."

tools-host: tools/mkfs_blueyfs
	@echo "  Host tools built!"

tools/mkfs_blueyfs: tools/mkfs_blueyfs.c
	gcc -O2 -Wall -Wextra -o $@ $<
	@echo "  [CC]  $< (host)"

help:
	@echo "BlueyOS Build System - 'Let's Play!' - Bluey"
	@echo ""
	@echo "  make                  - build the kernel ELF"
	@echo "  make iso              - create bootable ISO (needs grub-mkrescue)"
	@echo "  make run              - build ISO and launch in QEMU"
	@echo "  make tools-host       - build host-side tools (mkfs.biscuitfs)"
	@echo "  make version          - print version information"
	@echo "  make clean            - remove all build artifacts"
	@echo "  make BUILD_NUMBER=N   - set build number (default: 1)"
	@echo ""
	@echo "Prerequisites: nasm, gcc (multilib), ld (binutils), qemu-system-i386,"
	@echo "               grub-pc-bin, grub-common, xorriso"

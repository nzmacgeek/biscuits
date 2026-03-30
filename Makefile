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
#   make                     - build kernel ELF (i386)
#   make ARCH=m68k           - build M68K kernel for Macintosh LC III
#   make ARCH=ppc            - build PowerPC kernel for iMac G4 "Sunflower"
#   make iso                 - build bootable ISO image (i386 only)
#   make run                 - build ISO and launch in QEMU (i386 only)
#   make version             - print version information
#   make clean               - remove build artifacts
#   make BUILD_NUMBER=N      - build with a specific build number
#
# M68K prerequisites (Ubuntu/Debian):
#   sudo tools/install-m68k-toolchain.sh
#
# PowerPC prerequisites (Ubuntu/Debian):
#   sudo tools/install-ppc-toolchain.sh

# ---------------------------------------------------------------------------
# Architecture selection
# Default: i386 (x86 32-bit, existing hardware).
# Set ARCH=m68k to target the Macintosh LC III (Motorola MC68030).
# Set ARCH=ppc  to target the iMac G4 "Sunflower" (PowerPC G4 / MPC7450).
# ---------------------------------------------------------------------------
ARCH ?= i386

.DEFAULT_GOAL := all

# ---------------------------------------------------------------------------
# Per-architecture toolchain selection
# ---------------------------------------------------------------------------
ifeq ($(ARCH),m68k)
  CC  = m68k-linux-gnu-gcc
  AS  = m68k-linux-gnu-as
  LD  = m68k-linux-gnu-ld
else ifeq ($(ARCH),ppc)
  CC  = powerpc-linux-gnu-gcc
  AS  = powerpc-linux-gnu-as
  LD  = powerpc-linux-gnu-ld
else
  CC  = gcc
  AS  = nasm
  LD  = ld
endif

PYTHON ?= python3

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

# ---------------------------------------------------------------------------
# Architecture-specific compiler / assembler / linker flags
# ---------------------------------------------------------------------------
ifeq ($(ARCH),m68k)
  CFLAGS = \
      -m68030 \
      -std=gnu11 \
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
      -DBLUEYOS_ARCH_M68K \
      -DBLUEYOS_PLATFORM_MAC_LC3 \
      -DBLUEYOS_BUILD_NUMBER=$(BUILD_NUMBER) \
      -DBLUEYOS_BUILD_HOST=\"$(BUILD_HOST)\" \
      -DBLUEYOS_BUILD_USER=\"$(BUILD_USER)\" \
      -I include \
      -I .
  ASFLAGS = -m68030
  LDFLAGS = -T arch/m68k/linker.ld --no-warn-rwx-segments
else ifeq ($(ARCH),ppc)
  CFLAGS = \
      -mcpu=7450 \
      -maltivec \
      -std=gnu11 \
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
      -DBLUEYOS_ARCH_PPC \
      -DBLUEYOS_PLATFORM_IMAC_G4 \
      -DBLUEYOS_BUILD_NUMBER=$(BUILD_NUMBER) \
      -DBLUEYOS_BUILD_HOST=\"$(BUILD_HOST)\" \
      -DBLUEYOS_BUILD_USER=\"$(BUILD_USER)\" \
      -I include \
      -I .
  ASFLAGS = -mregnames
  LDFLAGS = -T arch/ppc/linker.ld --no-warn-rwx-segments
else
  CFLAGS = \
      -m32 \
      -std=gnu11 \
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
      -DBLUEYOS_ARCH_I386 \
      -DBLUEYOS_BUILD_NUMBER=$(BUILD_NUMBER) \
      -DBLUEYOS_BUILD_HOST=\"$(BUILD_HOST)\" \
      -DBLUEYOS_BUILD_USER=\"$(BUILD_USER)\" \
      -I include \
      -I .
  ASFLAGS = -f elf32
  LDFLAGS = -m elf_i386 -T linker.ld --no-warn-rwx-segments
endif

# ---------------------------------------------------------------------------
# Source files — split by architecture
# ---------------------------------------------------------------------------

# i386 kernel sources (full featured)
I386_C_SOURCES = \
  kernel/bootui.c \
    kernel/bootargs.c \
    kernel/kernel.c \
    kernel/gdt.c \
    kernel/idt.c \
    kernel/isr.c \
    kernel/irq.c \
    kernel/timer.c \
    kernel/kheap.c \
    kernel/paging.c \
    kernel/process.c \
    kernel/rootfs.c \
    kernel/scheduler.c \
    kernel/signal.c \
    kernel/syscall.c \
    kernel/elf.c \
    kernel/sha256.c \
    kernel/rtc.c \
    kernel/multiuser.c \
    kernel/sysinfo.c \
    kernel/smp.c \
    kernel/tty.c \
    kernel/swap.c \
    kernel/syslog.c \
    kernel/netcfg.c \
    kernel/poll.c \
    kernel/devev.c \
    drivers/vga.c \
    drivers/vt100.c \
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

I386_ASM_SOURCES = \
    boot/boot.asm \
    kernel/gdt_flush.asm \
    kernel/idt_flush.asm \
    kernel/isr_stubs.asm \
    kernel/irq_stubs.asm \
    kernel/syscall_stub.asm \
    kernel/paging_enable.asm

# M68K kernel sources (stub — Macintosh LC III port in progress)
M68K_C_SOURCES = \
  kernel/bootui.c \
    kernel/rtc.c \
    arch/m68k/bootinfo.c \
    arch/m68k/kernel_m68k.c \
  arch/m68k/platform.c \
  arch/m68k/dafb.c \
    lib/string.c \
    lib/stdio.c \
    lib/stdlib.c

M68K_ASM_SOURCES = \
    arch/m68k/startup.S

# PowerPC kernel sources (stub — iMac G4 "Sunflower" port in progress)
# Note: lib/stdlib.c is excluded because it calls kheap_alloc/kheap_free from
# kernel/kheap.h, which has no PPC implementation yet.
PPC_C_SOURCES = \
  kernel/rtc.c \
    arch/ppc/kernel_ppc.c \
    lib/string.c \
    lib/stdio.c

PPC_ASM_SOURCES = \
    arch/ppc/startup.S

# Select the right set based on ARCH
ifeq ($(ARCH),m68k)
  C_SOURCES   = $(M68K_C_SOURCES)
  ASM_SOURCES = $(M68K_ASM_SOURCES)
  TARGET      = blueyos-m68k.elf
else ifeq ($(ARCH),ppc)
  C_SOURCES   = $(PPC_C_SOURCES)
  ASM_SOURCES = $(PPC_ASM_SOURCES)
  TARGET      = blueyos-ppc.elf
else
  C_SOURCES   = $(I386_C_SOURCES)
  ASM_SOURCES = $(I386_ASM_SOURCES)
  TARGET      = blueyos.elf
  USER_TARGETS = user/init.elf
endif

ISO    = blueyos.iso

C_OBJECTS   = $(C_SOURCES:.c=.o)
ASM_OBJECTS_C = $(M68K_ASM_SOURCES:.S=.o) $(PPC_ASM_SOURCES:.S=.o)
ifeq ($(ARCH),m68k)
  ASM_OBJECTS = $(M68K_ASM_SOURCES:.S=.o)
else ifeq ($(ARCH),ppc)
  ASM_OBJECTS = $(PPC_ASM_SOURCES:.S=.o)
else
  ASM_OBJECTS = $(I386_ASM_SOURCES:.asm=.o)
endif
OBJECTS     = $(ASM_OBJECTS) $(C_OBJECTS)
M68K_GENERATED_HEADERS = arch/m68k/boot_font.h

# ---------------------------------------------------------------------------

# Ensure object files are rebuilt when ARCH changes: maintain a small stamp
# file `.arch_record` containing the last-built ARCH. When ARCH differs the
# stamp is updated which forces object files to be rebuilt.
ARCH_STAMP := .arch_record

$(ARCH_STAMP): FORCE
	@printf "%s\n" "$(ARCH)" > $(ARCH_STAMP).tmp
	@if [ -f $(ARCH_STAMP) ] && cmp -s $(ARCH_STAMP) $(ARCH_STAMP).tmp; then \
		rm -f $(ARCH_STAMP).tmp; \
	else \
		mv $(ARCH_STAMP).tmp $(ARCH_STAMP); \
	fi

$(OBJECTS): $(ARCH_STAMP)

FORCE:

arch/m68k/boot_font.h: arch/m68k/boot_font.sbf tools/fonty_rg_to_c.py
	$(PYTHON) tools/fonty_rg_to_c.py $< $@
	@echo "  [GEN] $@"

ifeq ($(ARCH),m68k)
arch/m68k/dafb.o: arch/m68k/boot_font.h
endif

# Targets
# ---------------------------------------------------------------------------
.PHONY: all iso run run-m68k version clean help tools-host toolinfo FORCE

all: $(TARGET) $(USER_TARGETS)
	@echo ""
	@echo "  G'day! BlueyOS Build \#$(BUILD_NUMBER) complete!"
	@echo "  Built by $(BUILD_USER)@$(BUILD_HOST) on $(BUILD_DATE) $(BUILD_TIME)"
	@echo "  Kernel: $(TARGET)"
	@if [ -n "$(USER_TARGETS)" ]; then echo "  Userland: $(USER_TARGETS)"; fi
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

# Rule for GNU assembler (.S files — used by M68K and PPC ports)
%.o: %.S
	$(AS) $(ASFLAGS) -o $@ $<
	@echo "  [AS]  $<"

iso: $(TARGET)
	@if [ "$(ARCH)" != "i386" ]; then \
	    echo "  [ISO]  ISO build is only supported for ARCH=i386"; exit 1; fi
	@bash tools/mkdisk.sh

run: iso
	@if [ "$(ARCH)" != "i386" ]; then \
	    echo "  [RUN]  QEMU run is only supported for ARCH=i386"; exit 1; fi
	@bash tools/qemu-run.sh

run-m68k: blueyos-m68k.elf
	@bash tools/qemu-run-m68k.sh

version:
	@echo "BlueyOS v0.1.0 (Codename: $(shell grep CODENAME include/version.h | head -1 | sed 's/.*\"\(.*\)\".*/\1/'))"
	@echo "Build \#$(BUILD_NUMBER) | $(BUILD_DATE) $(BUILD_TIME)"
	@echo "Built by: $(BUILD_USER)@$(BUILD_HOST)"
	@echo "Architecture: $(ARCH)"

clean:
	@find . \( -name '*.o' -o -name '*.d' \) -not -path './.git/*' -delete
	@rm -f blueyos.elf blueyos-m68k.elf blueyos-ppc.elf $(ISO) tools/mkfs_blueyfs
	@rm -f $(M68K_GENERATED_HEADERS)
	@rm -rf isodir/
	@echo "  Clean! Bluey would be proud."

tools-host: tools/mkfs_blueyfs
	@echo "  Host tools built!"

tools/mkfs_blueyfs: tools/mkfs_blueyfs.c
	gcc -O2 -Wall -Wextra -o $@ $<
	@echo "  [CC]  $< (host)"

user/init.elf: user/init.c
	gcc -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -nostdlib -fno-builtin -fno-pic -no-pie -Wl,-m,elf_i386 -Wl,-Ttext,0x00400000 -o $@ $<
	@echo "  [LD]  $@"

toolinfo:
	@echo "BlueyOS Build Tool Versions (ARCH=$(ARCH))"
	@echo "-------------------------------------------"
	@printf "  CC  (%-30s) : " "$(CC)";  $(CC)  --version 2>&1 | head -1
	@printf "  AS  (%-30s) : " "$(AS)";  $(AS)  --version 2>&1 | head -1
	@printf "  LD  (%-30s) : " "$(LD)";  $(LD)  --version 2>&1 | head -1
	@printf "  QEMU (i386) : "; if command -v qemu-system-i386 >/dev/null 2>&1; then qemu-system-i386 --version 2>&1 | head -1; else echo "not installed"; fi
	@printf "  QEMU (m68k) : "; if command -v qemu-system-m68k >/dev/null 2>&1; then qemu-system-m68k --version 2>&1 | head -1; else echo "not installed"; fi
	@printf "  QEMU (ppc)  : "; if command -v qemu-system-ppc >/dev/null 2>&1; then qemu-system-ppc --version 2>&1 | head -1; else echo "not installed"; fi
	@printf "  grub-mkrescue: "; if command -v grub-mkrescue >/dev/null 2>&1; then grub-mkrescue --version 2>&1 | head -1; else echo "not installed"; fi
	@printf "  xorriso     : "; if command -v xorriso >/dev/null 2>&1; then xorriso --version 2>&1 | head -1; else echo "not installed"; fi
	@echo ""

help:
	@echo "BlueyOS Build System - 'Let's Play!' - Bluey"
	@echo ""
	@echo "  make                  - build the i386 kernel ELF"
	@echo "  make ARCH=m68k        - build M68K kernel (Macintosh LC III)"
	@echo "  make ARCH=ppc         - build PowerPC kernel (iMac G4 Sunflower)"
	@echo "  make iso              - create bootable ISO (i386 only)"
	@echo "  make run              - build ISO and launch in QEMU (i386 only)"
	@echo "  make run-m68k         - launch M68K QEMU with detached serial capture"
	@echo "  make tools-host       - build host-side tools (mkfs.biscuitfs)"
	@echo "  make toolinfo         - print versions of all build tools"
	@echo "  make version          - print version information"
	@echo "  make clean            - remove all build artifacts"
	@echo "  make BUILD_NUMBER=N   - set build number (default: 1)"
	@echo ""
	@echo "Toolchain installers (Ubuntu/Debian):"
	@echo "  sudo tools/install-m68k-toolchain.sh  (for ARCH=m68k)"
	@echo "  sudo tools/install-ppc-toolchain.sh   (for ARCH=ppc)"
	@echo ""
	@echo "i386 prerequisites: nasm, gcc (multilib), ld (binutils),"
	@echo "                    qemu-system-i386, grub-pc-bin, grub-common, xorriso"

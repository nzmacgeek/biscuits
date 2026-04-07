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
# Derive BUILD_NUMBER from git commit count when available. This can be
# overridden by passing `BUILD_NUMBER=` on the make command line.
# Also capture a short commit id for traceability.
BUILD_NUMBER ?= $(shell git rev-list --count HEAD 2>/dev/null || echo 1)
BUILD_COMMIT := $(shell git rev-parse --short HEAD 2>/dev/null || echo unknown)
BUILD_DATE   := $(shell date -u '+%Y-%m-%d')
BUILD_TIME   := $(shell date -u '+%H:%M:%S')
BUILD_HOST   := $(shell hostname 2>/dev/null || echo unknown-host)
BUILD_USER   := $(shell whoami  2>/dev/null || echo unknown-user)

# Enable debug build: `make DEBUG=1` will add `-g -O0` and `-DDEBUG`.
# Default is 0 (release-ish build with -O2). Use this when actively
# developing and debugging the kernel.
DEBUG ?= 0
BUILD_DIR ?= build
BUILD_KERNEL_DIR := $(BUILD_DIR)/kernel
BUILD_TOOLS_DIR := $(BUILD_DIR)/tools
BUILD_USERS_DIR := $(BUILD_DIR)/userspace
BUILD_USER_DIR := $(BUILD_USERS_DIR)
BUILD_SYSROOT := $(BUILD_DIR)/sysroot
MUSL_PREFIX ?= $(BUILD_USERS_DIR)/musl
MUSL_INCLUDE_DIR := $(MUSL_PREFIX)/include
MUSL_LIB_DIR := $(MUSL_PREFIX)/lib
MUSL_INIT_TARGET := $(BUILD_USERS_DIR)/init/init-musl.elf
BLUEYOS_SYSROOT ?= /opt/blueyos-sysroot
BLUEYOS_CROSS ?= /opt/blueyos-cross
BLUEYOS_CROSS_MUSL ?= $(BLUEYOS_CROSS)/musl
# Optional external sysroot to assemble disk images from (preferred when present)
# If this directory exists, disk builds will use it instead of the locally
# assembled $(BUILD_SYSROOT).
SYSROOT_SRC ?= /opt/blueyos-sysroot
ifeq ($(shell [ -d $(SYSROOT_SRC) ] && echo yes),yes)
  ROOT_EXTRA_DIR := $(SYSROOT_SRC)
else
  ROOT_EXTRA_DIR := $(BUILD_SYSROOT)
endif

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

ifeq ($(DEBUG),1)
  # For reliable debug builds that interact correctly with inline asm
  # use -O0 (no optimization) and preserve the frame pointer to keep
  # stack frames and asm operand locations stable. Also disable
  # sibling-call optimization which can remove frames and confuse
  # __builtin_return_address and inline-asm assumptions.
  CFLAGS += -g -O0 -DDEBUG -fno-omit-frame-pointer -fno-optimize-sibling-calls 
  LDFLAGS += -g
else
  # Release build: optimize for speed.
  CFLAGS += -O2
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
  kernel/password.c \
  kernel/module.c \
  kernel/rtc.c \
  kernel/multiuser.c \
  kernel/sysinfo.c \
  kernel/tty.c \
  kernel/socket.c \
    kernel/swap.c \
    kernel/syslog.c \
    kernel/netcfg.c \
  drivers/vga.c \
  drivers/vt100.c \
  drivers/keyboard.c \
  drivers/ata.c \
  drivers/driver.c \
  drivers/modules.c \
  drivers/net/ne2000.c \
  drivers/net/loopback.c \
  drivers/net/network.c \
    kernel/syscall.c \
    kernel/elf.c \
    kernel/sha256.c \
    kernel/rtc.c \
    kernel/multiuser.c \
    kernel/sysinfo.c \
    kernel/smp.c \
    kernel/tty.c \
    kernel/socket.c \
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
  TARGET      = $(BUILD_DIR)/blueyos-m68k.elf
else ifeq ($(ARCH),ppc)
  C_SOURCES   = $(PPC_C_SOURCES)
  ASM_SOURCES = $(PPC_ASM_SOURCES)
  TARGET      = $(BUILD_DIR)/blueyos-ppc.elf
else
  C_SOURCES   = $(I386_C_SOURCES)
  ASM_SOURCES = $(I386_ASM_SOURCES)
  TARGET      = $(BUILD_KERNEL_DIR)/bkernel
  # Prefer musl-backed init when present (build userspace musl with `make musl-init`).
  # This makes the musl-test init the default payload until the new `claw` init is ready.
  USER_TARGETS = $(MUSL_INIT_TARGET) $(BUILD_USER_DIR)/init.elf
endif

ISO    = $(BUILD_DIR)/blueyos.iso
DISK_IMAGE = $(BUILD_DIR)/blueyos-disk.img
LOG_DISK_IMAGE ?= $(BUILD_DIR)/blueyos-log-fat.img
MKFS_BLUEYFS = $(BUILD_TOOLS_DIR)/mkfs_blueyfs
MKSWAP_BLUEYFS = $(BUILD_TOOLS_DIR)/mkswap_blueyfs
FSCK_BLUEYFS = $(BUILD_TOOLS_DIR)/fsck_blueyfs
LIST_BLUEYFS = $(BUILD_TOOLS_DIR)/list_blueyfs
MOUNT_BLUEYFS = $(BUILD_TOOLS_DIR)/mount_blueyfs
ISO_STAGE_DIR = $(BUILD_DIR)/isodir

# Convenience dirs
$(BUILD_KERNEL_DIR): ; @mkdir -p $(BUILD_KERNEL_DIR)
$(BUILD_TOOLS_DIR): ; @mkdir -p $(BUILD_TOOLS_DIR)
$(BUILD_USERS_DIR): ; @mkdir -p $(BUILD_USERS_DIR)
$(BUILD_SYSROOT): ; @mkdir -p $(BUILD_SYSROOT)

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
ARCH_STAMP := $(BUILD_DIR)/.arch_record

$(ARCH_STAMP): FORCE
	@mkdir -p $(dir $(ARCH_STAMP))
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
.PHONY: all iso disk disk-musl fat-log-disk musl-init run run-m68k version clean full-clean help tools-host toolinfo FORCE

all: $(TARGET)
	@echo ""
	@echo "  G'day! BlueyOS Build \#$(BUILD_NUMBER) complete!"
	@echo "  Built by $(BUILD_USER)@$(BUILD_HOST) on $(BUILD_DATE) $(BUILD_TIME)"
	@echo "  Kernel: $(TARGET)"
	@if [ -n "$(USER_TARGETS)" ]; then echo "  Userland: $(USER_TARGETS)"; fi
	@echo ""

$(TARGET): $(OBJECTS)
	@mkdir -p $(dir $@)
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

iso: $(TARGET) ; @if [ "$(ARCH)" != "i386" ]; then echo "  [ISO]  ISO build is only supported for ARCH=i386"; exit 1; fi; BUILD_DIR=$(BUILD_DIR) bash tools/mkdisk.sh

sysroot: $(TARGET) $(MUSL_INIT_TARGET) | $(BUILD_SYSROOT)
	@echo "  [SYSROOT] Assembling $(BUILD_SYSROOT)"
	@mkdir -p $(BUILD_SYSROOT)/boot $(BUILD_SYSROOT)/bin $(BUILD_SYSROOT)/lib $(BUILD_SYSROOT)/etc
	@cp -f $(TARGET) $(BUILD_SYSROOT)/boot/bkernel
	@if [ -f $(MUSL_INIT_TARGET) ]; then cp -f $(MUSL_INIT_TARGET) $(BUILD_SYSROOT)/bin/init; fi
	@if [ -d $(BUILD_USERS_DIR)/bash/bin ]; then cp -a $(BUILD_USERS_DIR)/bash/bin/* $(BUILD_SYSROOT)/bin/ 2>/dev/null || true; fi
	@if [ -d $(MUSL_LIB_DIR) ]; then cp -a $(MUSL_LIB_DIR)/* $(BUILD_SYSROOT)/lib/ 2>/dev/null || true; fi
	@echo "  [SYSROOT] ready"


disk: $(TARGET) tools-host ; @if [ "$(ARCH)" != "i386" ]; then echo "  [DISK]  Disk image build is only supported for ARCH=i386"; exit 1; fi; $(PYTHON) tools/mkbluey_disk.py --image $(DISK_IMAGE) --kernel $(TARGET) --root-extra-dir $(ROOT_EXTRA_DIR) --boot-extra-dir $(ROOT_EXTRA_DIR)/boot --mkfs-tool $(MKFS_BLUEYFS) --mkswap-tool $(MKSWAP_BLUEYFS)

disk-musl: $(TARGET) $(MUSL_INIT_TARGET) tools-host ; @if [ "$(ARCH)" != "i386" ]; then echo "  [DISK]  Disk image build is only supported for ARCH=i386"; exit 1; fi; $(PYTHON) tools/mkbluey_disk.py --image $(DISK_IMAGE) --kernel $(TARGET) --root-extra-dir $(ROOT_EXTRA_DIR) --boot-extra-dir $(ROOT_EXTRA_DIR)/boot --mkfs-tool $(MKFS_BLUEYFS) --mkswap-tool $(MKSWAP_BLUEYFS)

fat-log-disk:
	@bash tools/mkfat_logs_disk.sh "$(LOG_DISK_IMAGE)"

run: disk ; @if [ "$(ARCH)" != "i386" ]; then echo "  [RUN]  QEMU run is only supported for ARCH=i386"; exit 1; fi; BUILD_DIR=$(BUILD_DIR) bash tools/qemu-run.sh

run-m68k: $(BUILD_DIR)/blueyos-m68k.elf
	@bash tools/qemu-run-m68k.sh

version:
	@echo "BlueyOS v0.1.0 (Codename: $(shell grep CODENAME include/version.h | head -1 | sed 's/.*\"\(.*\)\".*/\1/'))"
	@echo "Build \#$(BUILD_NUMBER) | $(BUILD_DATE) $(BUILD_TIME)"
	@echo "Built by: $(BUILD_USER)@$(BUILD_HOST)"
	@echo "Architecture: $(ARCH)"

clean: ; @find . \( -name '*.o' -o -name '*.d' \) -not -path './.git/*' -delete; \
	if [ -z "$(BUILD_DIR)" ] || [ "$(BUILD_DIR)" = "/" ] || [ "$(BUILD_DIR)" = "." ] || [ "$(BUILD_DIR)" = ".." ]; then \
		echo "  [CLEAN] Refusing to remove unsafe BUILD_DIR='$(BUILD_DIR)'"; exit 1; \
	fi; \
	rm -rf -- "$(BUILD_DIR)"; rm -f $(M68K_GENERATED_HEADERS); echo "  Clean! Build outputs removed from $(BUILD_DIR)."

full-clean: clean ; @echo "  Full clean! All build artifacts removed."

tools-host: $(MKFS_BLUEYFS) $(MKSWAP_BLUEYFS) $(FSCK_BLUEYFS) $(LIST_BLUEYFS) ; @echo "  Host tools built!"


.PHONY: build-musl-blueyos
build-musl-blueyos:
	@echo "Building musl for BlueyOS into local, sysroot, and cross prefixes (this may take a while)"
	@TARGET=i386-linux-gnu ./tools/build-musl.sh \
		--prefix=$(MUSL_PREFIX) \
		--sysroot=$(BLUEYOS_SYSROOT) \
		--cross-prefix=$(BLUEYOS_CROSS_MUSL)

$(MKFS_BLUEYFS): tools/mkfs_blueyfs.c ; @mkdir -p $(dir $@); gcc -O2 -Wall -Wextra -o $@ $<; echo "  [CC]  $< (host)"

$(MKSWAP_BLUEYFS): tools/mkswap_blueyfs.c ; @mkdir -p $(dir $@); gcc -O2 -Wall -Wextra -o $@ $<; echo "  [CC]  $< (host)"

$(FSCK_BLUEYFS): tools/fsck_blueyfs.c ; @mkdir -p $(dir $@); gcc -O2 -Wall -Wextra -o $@ $<; echo "  [CC]  $< (host)"

$(LIST_BLUEYFS): tools/list_blueyfs.c ; @mkdir -p $(dir $@); gcc -O2 -Wall -Wextra -o $@ $<; echo "  [CC]  $< (host)"

$(MOUNT_BLUEYFS): tools/mount_blueyfs.c ; @mkdir -p $(dir $@); \
  if ! command -v pkg-config >/dev/null 2>&1 || ! pkg-config --exists fuse3; then \
    echo "  [CC]  missing fuse3 development files; install libfuse3-dev to build $@"; \
    exit 1; \
  fi; \
  gcc -O2 -Wall -Wextra -o $@ $< $$(pkg-config --cflags --libs fuse3); \
  echo "  [CC]  $< (host, fuse3)"

$(BUILD_USER_DIR)/init.elf: user/init.c ; @mkdir -p $(dir $@); gcc -m32 -std=gnu11 -ffreestanding -O2 -Wall -Wextra -fno-stack-protector -nostdlib -fno-builtin -fno-pic -no-pie -Wl,-m,elf_i386 -Wl,-Ttext,0x00400000 -o $@ $<; echo "  [LD]  $@"

musl-init: $(MUSL_INIT_TARGET)

$(MUSL_INIT_TARGET): tests/musl/init/init.c tests/musl/init/syscalls.c ; @mkdir -p $(dir $@); \
  if [ ! -d "$(MUSL_INCLUDE_DIR)" ] || [ ! -f "$(MUSL_LIB_DIR)/libc.a" ]; then \
    echo "  [MUSL] Missing musl install under $(MUSL_PREFIX)"; \
    echo "         expected $(MUSL_INCLUDE_DIR) and $(MUSL_LIB_DIR)/libc.a"; \
    exit 1; \
  fi; \
  set -e; \
  gcc -m32 -std=gnu11 -O2 -Wall -Wextra -fno-stack-protector -fno-builtin -fno-pic -static -no-pie -isystem $(MUSL_INCLUDE_DIR) -Wl,-m,elf_i386 -Wl,-Ttext,0x00400000 -L$(MUSL_LIB_DIR) -o $@ $^ -lc; \
  echo "  [LD]  $@ (musl static)"

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
	@echo "  make disk             - create a partitioned BlueyOS disk image (root auto-sized from sysroot +30%)"
	@echo "  make fat-log-disk     - create an optional FAT16 log disk image at $(LOG_DISK_IMAGE)"
	@echo "  make $(MOUNT_BLUEYFS) - build the read-only Linux FUSE BiscuitFS mounter"
	@echo "  make musl-init        - build static musl test init at $(MUSL_INIT_TARGET)"
	@echo "  make disk-musl        - create a disk image using the musl test init (root auto-sized from sysroot +30%)"
	@echo "  make run              - build ISO and launch in QEMU (i386 only)"
	@echo "  make run-m68k         - launch M68K QEMU with detached serial capture"
	@echo "  make tools-host       - build host-side mkfs/mkswap/fsck tools"
	@echo "  make version          - print version information"
	@echo "  make clean            - remove all build artifacts"
	@echo "  make MUSL_PREFIX=... musl-init  - override the musl install prefix"
	@echo "  make BUILD_NUMBER=N   - set build number (default: 1)"
	@echo ""
	@echo "Toolchain installers (Ubuntu/Debian):"
	@echo "  sudo tools/install-m68k-toolchain.sh  (for ARCH=m68k)"
	@echo "  sudo tools/install-ppc-toolchain.sh   (for ARCH=ppc)"
	@echo ""
	@echo "i386 prerequisites: nasm, gcc (multilib), ld (binutils),"
	@echo "                    qemu-system-i386, grub-pc-bin, grub-common, xorriso"

.PHONY: test-boot
test-boot:
	@bash tools/boot-test.sh

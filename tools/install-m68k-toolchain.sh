#!/usr/bin/env bash
# BlueyOS M68K Cross-Compiler Toolchain Installer
# "It was the 80s!" — Bandit Heeler
#
# Installs an m68k-linux-gnu cross-compiler + binutils on Ubuntu or Debian.
# This toolchain is used to build BlueyOS for the Macintosh LC III (MC68030).
#
# Usage:
#   chmod +x tools/install-m68k-toolchain.sh
#   sudo ./tools/install-m68k-toolchain.sh
#
# After installation, build the M68K kernel with:
#   make ARCH=m68k
#
# ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
#
# Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
# licensed by BBC Studios. BlueyOS is an unofficial fan/research project
# with no affiliation to Ludo Studio or the BBC.

set -euo pipefail

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
info()  { echo "[INFO]  $*"; }
ok()    { echo "[OK]    $*"; }
warn()  { echo "[WARN]  $*" >&2; }
die()   { echo "[ERROR] $*" >&2; exit 1; }

# Require root (apt-get needs it)
if [ "$(id -u)" -ne 0 ]; then
    die "This script must be run as root (sudo $0)"
fi

# ---------------------------------------------------------------------------
# Detect distro
# ---------------------------------------------------------------------------
if [ ! -f /etc/os-release ]; then
    die "Cannot detect OS — /etc/os-release not found. Is this Debian or Ubuntu?"
fi
# shellcheck source=/dev/null
. /etc/os-release

case "${ID:-}" in
    ubuntu|debian|linuxmint|pop|elementary)
        : ;;  # supported
    *)
        warn "Untested distro '${ID:-unknown}'. Trying anyway (Debian-compatible assumed)."
        ;;
esac

info "Detected: ${PRETTY_NAME:-${ID}}"
info ""
info "Installing M68K cross-compiler toolchain for BlueyOS / Macintosh LC III..."
info ""

# ---------------------------------------------------------------------------
# Update package lists
# ---------------------------------------------------------------------------
info "Updating package lists..."
apt-get update -qq

# ---------------------------------------------------------------------------
# Core cross-compilation packages
# ---------------------------------------------------------------------------
info "Installing gcc-m68k-linux-gnu, binutils-m68k-linux-gnu..."
apt-get install -y \
    gcc-m68k-linux-gnu \
    binutils-m68k-linux-gnu \
    cpp-m68k-linux-gnu

# ---------------------------------------------------------------------------
# Optional: GDB for remote debugging (via QEMU's GDB stub or SCC serial)
# ---------------------------------------------------------------------------
info "Installing gdb-multiarch (for cross-debugging over QEMU GDB stub)..."
apt-get install -y gdb-multiarch || warn "gdb-multiarch not available — skipping"

# ---------------------------------------------------------------------------
# QEMU M68K system emulator (for testing without real hardware)
# ---------------------------------------------------------------------------
info "Installing qemu-system-m68k..."
apt-get install -y qemu-system-misc || \
    apt-get install -y qemu-system-m68k 2>/dev/null || \
    warn "qemu-system-m68k not available in this release — install manually if needed"

# ---------------------------------------------------------------------------
# Build tools used by the BlueyOS Makefile
# ---------------------------------------------------------------------------
info "Installing build essentials (make, nasm, xorriso)..."
apt-get install -y make nasm xorriso

# ---------------------------------------------------------------------------
# Verify installation
# ---------------------------------------------------------------------------
echo ""
info "Verifying toolchain..."
echo ""

TOOLS_OK=1

check_tool() {
    local cmd="$1"
    local label="$2"
    if command -v "$cmd" >/dev/null 2>&1; then
        printf "  %-40s %s\n" "${label}:" "$("$cmd" --version 2>&1 | head -1)"
    else
        printf "  %-40s NOT FOUND\n" "${label}:"
        TOOLS_OK=0
    fi
}

check_tool m68k-linux-gnu-gcc    "C compiler (m68k-linux-gnu-gcc)"
check_tool m68k-linux-gnu-as     "Assembler  (m68k-linux-gnu-as)"
check_tool m68k-linux-gnu-ld     "Linker     (m68k-linux-gnu-ld)"
check_tool m68k-linux-gnu-objcopy "objcopy   (m68k-linux-gnu-objcopy)"
check_tool m68k-linux-gnu-nm     "nm         (m68k-linux-gnu-nm)"
check_tool m68k-linux-gnu-objdump "objdump   (m68k-linux-gnu-objdump)"
check_tool gdb-multiarch         "Debugger   (gdb-multiarch)"
check_tool qemu-system-m68k     "Emulator   (qemu-system-m68k)"

echo ""
if [ "${TOOLS_OK}" -eq 1 ]; then
    ok "All required tools found!"
else
    warn "Some tools were not found. Check the output above."
    warn "On older Debian/Ubuntu you may need to enable the 'universe' repository:"
    warn "  sudo add-apt-repository universe && sudo apt-get update"
fi

# ---------------------------------------------------------------------------
# Print usage summary
# ---------------------------------------------------------------------------
cat <<'EOF'

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 BlueyOS M68K Toolchain — Quick-Start Guide
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

 Build the M68K kernel:
   make ARCH=m68k

 Inspect the kernel binary:
   m68k-linux-gnu-objdump -d blueyos-m68k.elf | less

 Run under QEMU (q800 — the closest 68030 Mac machine in QEMU):
   qemu-system-m68k -M q800 -kernel blueyos-m68k.elf -nographic -serial stdio

 Remote GDB debugging:
   # Terminal 1:
   qemu-system-m68k -M q800 -kernel blueyos-m68k.elf -s -S -nographic
   # Terminal 2:
   gdb-multiarch blueyos-m68k.elf
   (gdb) target remote :1234
   (gdb) continue

 Serial output (Macintosh SCC channel A, 9600,8N1):
   The M68K kernel sends boot messages over SCC channel A (modem port).
   On real hardware, connect a null-modem cable to the Mac modem port and
   use minicom/screen at 9600,8N1 on the host.

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 "It was the 80s!" — Bandit Heeler
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

EOF

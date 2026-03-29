#!/usr/bin/env bash
# BlueyOS PowerPC Cross-Compiler Toolchain Installer
# "That's a ripper machine!" — Bandit Heeler
#
# Installs a powerpc-linux-gnu cross-compiler + binutils on Ubuntu or Debian.
# This toolchain is used to build BlueyOS for the iMac G4 "Sunflower" (PPC G4).
#
# Usage:
#   chmod +x tools/install-ppc-toolchain.sh
#   sudo ./tools/install-ppc-toolchain.sh
#
# After installation, build the PowerPC kernel with:
#   make ARCH=ppc
#
# ⚠️  VIBE CODED RESEARCH PROJECT — NOT FOR PRODUCTION USE ⚠️
#
# Bluey and all related characters are trademarks of Ludo Studio Pty Ltd,
# licensed by BBC Studios. BlueyOS is an unofficial fan/research project
# with no affiliation to Ludo Studio or the BBC.

set -euo pipefail

info()  { echo "[INFO]  $*"; }
ok()    { echo "[OK]    $*"; }
warn()  { echo "[WARN]  $*" >&2; }
die()   { echo "[ERROR] $*" >&2; exit 1; }

if [ "$(id -u)" -ne 0 ]; then
    die "This script must be run as root (sudo $0)"
fi

if [ ! -f /etc/os-release ]; then
    die "Cannot detect OS — /etc/os-release not found."
fi
# shellcheck source=/dev/null
. /etc/os-release

case "${ID:-}" in
    ubuntu|debian|linuxmint|pop|elementary) : ;;
    *) warn "Untested distro '${ID:-unknown}'. Trying anyway." ;;
esac

info "Detected: ${PRETTY_NAME:-${ID}}"
info ""
info "Installing PowerPC cross-compiler toolchain for BlueyOS / iMac G4 Sunflower..."
info ""

# ---------------------------------------------------------------------------
# Update and install
# ---------------------------------------------------------------------------
info "Updating package lists..."
apt-get update -qq

info "Installing gcc-powerpc-linux-gnu, binutils-powerpc-linux-gnu..."
apt-get install -y \
    gcc-powerpc-linux-gnu \
    binutils-powerpc-linux-gnu \
    cpp-powerpc-linux-gnu

info "Installing gdb-multiarch (for cross-debugging over QEMU GDB stub)..."
apt-get install -y gdb-multiarch || warn "gdb-multiarch not available — skipping"

info "Installing qemu-system-ppc (for testing without real hardware)..."
apt-get install -y qemu-system-ppc || \
    apt-get install -y qemu-system-misc 2>/dev/null || \
    warn "qemu-system-ppc not available in this release — install manually if needed"

info "Installing build essentials..."
apt-get install -y make nasm xorriso

# ---------------------------------------------------------------------------
# Verify
# ---------------------------------------------------------------------------
echo ""
info "Verifying toolchain..."
echo ""

TOOLS_OK=1

check_tool() {
    local cmd="$1"
    local label="$2"
    if command -v "$cmd" >/dev/null 2>&1; then
        printf "  %-44s %s\n" "${label}:" "$("$cmd" --version 2>&1 | head -1)"
    else
        printf "  %-44s NOT FOUND\n" "${label}:"
        TOOLS_OK=0
    fi
}

check_tool powerpc-linux-gnu-gcc    "C compiler   (powerpc-linux-gnu-gcc)"
check_tool powerpc-linux-gnu-as     "Assembler    (powerpc-linux-gnu-as)"
check_tool powerpc-linux-gnu-ld     "Linker       (powerpc-linux-gnu-ld)"
check_tool powerpc-linux-gnu-objcopy "objcopy     (powerpc-linux-gnu-objcopy)"
check_tool powerpc-linux-gnu-nm     "nm           (powerpc-linux-gnu-nm)"
check_tool powerpc-linux-gnu-objdump "objdump     (powerpc-linux-gnu-objdump)"
check_tool gdb-multiarch            "Debugger     (gdb-multiarch)"
check_tool qemu-system-ppc          "Emulator     (qemu-system-ppc)"

echo ""
if [ "${TOOLS_OK}" -eq 1 ]; then
    ok "All required tools found!"
else
    warn "Some tools were not found. On older releases you may need:"
    warn "  sudo add-apt-repository universe && sudo apt-get update"
fi

# ---------------------------------------------------------------------------
# Usage summary
# ---------------------------------------------------------------------------
cat <<'EOF'

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 BlueyOS PowerPC Toolchain — Quick-Start Guide
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

 Build the PowerPC kernel:
   make ARCH=ppc

 Inspect the kernel binary:
   powerpc-linux-gnu-objdump -d blueyos-ppc.elf | less

 Run under QEMU (Mac99 — best G4 machine model available in QEMU):
   qemu-system-ppc -M mac99 -cpu g4 -m 128 \
       -kernel blueyos-ppc.elf -nographic -serial stdio

 Remote GDB debugging:
   # Terminal 1:
   qemu-system-ppc -M mac99 -cpu g4 -m 128 \
       -kernel blueyos-ppc.elf -s -S -nographic
   # Terminal 2:
   gdb-multiarch blueyos-ppc.elf
   (gdb) set architecture powerpc:common
   (gdb) target remote :1234
   (gdb) continue

 Serial output (iMac G4 SCC channel A / internal modem debug port):
   Enable in Open Firmware before booting:
     0 > setenv serial-debug 1
     0 > reset-all
   Then connect a USB-serial adapter to the modem port (DE-9 null-modem)
   and open a terminal at 57600,8N1.

 Open Firmware access on real hardware:
   Hold Cmd-Option-O-F at power-on to enter OF prompt.

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
 "That's a ripper machine!" — Bandit Heeler
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

EOF

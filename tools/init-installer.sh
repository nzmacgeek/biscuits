#!/usr/bin/env sh
# Minimal first-boot installer to run as /bin/init
set -eux

# If already installed, hand off to the real init
if [ -f /installed ]; then
  if [ -x /bin/init.real ]; then
    exec /bin/init.real "$@"
  else
    exec /bin/sh
  fi
fi

mkdir -p /mnt/boot
# Try common device names for the boot partition (BSD-style preferred, Linux aliases as fallback)
mount /dev/disk0s1 /mnt/boot 2>/dev/null || mount /dev/disk1s1 /mnt/boot 2>/dev/null || mount /dev/hda1 /mnt/boot 2>/dev/null || mount /dev/sda1 /mnt/boot 2>/dev/null || true

if [ -d /mnt/boot/stage ]; then
  echo "Installing stage from /mnt/boot/stage..."
  cp -a /mnt/boot/stage/. /
  sync
elif [ -f /mnt/boot/stage.tar ]; then
  echo "Found /mnt/boot/stage.tar; attempting to extract..."
  if command -v tar >/dev/null 2>&1; then
    tar -xpf /mnt/boot/stage.tar -C /
    sync
  else
    echo "tar not available; cannot extract stage.tar" >&2
  fi
else
  echo "No stage found under /mnt/boot" >&2
fi

touch /installed
sync

if [ -x /bin/init.real ]; then
  exec /bin/init.real "$@"
else
  exec /bin/sh
fi

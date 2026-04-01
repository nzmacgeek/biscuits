#!/usr/bin/env bash
set -euo pipefail

# Wrapper to build a disk image with a host stage directory copied into the
# boot partition. Usage:
#   scripts/build-disk-with-stage.sh /path/to/stage-dir [image-path]

STAGE_DIR=${1:-/tmp/blueyos-stage}
IMAGE=${2:-build/blueyos-disk.img}

echo "Stage dir: $STAGE_DIR"
echo "Image: $IMAGE"

if [ ! -d "$STAGE_DIR" ]; then
  echo "Stage dir '$STAGE_DIR' does not exist. Create and populate it with files to install under /"
  exit 1
fi

python3 tools/mkbluey_disk.py --image "$IMAGE" --init tools/init-installer.sh --boot-extra-dir "$STAGE_DIR"

#!/usr/bin/env bash

set -euo pipefail

IMAGE_PATH=${1:-build/blueyos-log-fat.img}
IMAGE_SIZE_MB=${2:-64}
VOLUME_LABEL=${3:-BLUEYLOGS}

if ! command -v mkfs.fat >/dev/null 2>&1; then
    echo "ERROR: mkfs.fat not found. Install dosfstools to build the FAT log disk." >&2
    exit 1
fi

mkdir -p "$(dirname "$IMAGE_PATH")"
rm -f "$IMAGE_PATH"
truncate -s "${IMAGE_SIZE_MB}M" "$IMAGE_PATH"
mkfs.fat -F 16 -n "$VOLUME_LABEL" "$IMAGE_PATH" >/dev/null

echo "  [FAT] Created $IMAGE_PATH (${IMAGE_SIZE_MB} MiB, FAT16 label=$VOLUME_LABEL)"
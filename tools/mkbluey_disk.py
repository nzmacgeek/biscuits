#!/usr/bin/env python3
import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

SECTOR_SIZE = 512
ROOT_START_LBA = 2048


def sectors_from_mb(mb: int) -> int:
    return mb * 1024 * 1024 // SECTOR_SIZE


def write_mbr(image: Path, root_start: int, root_sectors: int, swap_start: int, swap_sectors: int) -> None:
    mbr = bytearray(512)

    def put_entry(index: int, part_type: int, start_lba: int, sector_count: int) -> None:
        offset = 446 + index * 16
        entry = struct.pack(
            "<B3sB3sII",
            0x00,
            b"\x00\x02\x00",
            part_type,
            b"\xfe\xff\xff",
            start_lba,
            sector_count,
        )
        mbr[offset:offset + 16] = entry

    put_entry(0, 0x83, root_start, root_sectors)
    put_entry(1, 0x82, swap_start, swap_sectors)
    mbr[510:512] = b"\x55\xaa"

    with image.open("r+b") as fp:
        fp.seek(0)
        fp.write(mbr)


def run(cmd, cwd: Path) -> None:
    subprocess.run(cmd, cwd=str(cwd), check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a BlueyOS disk image with BlueyFS root and swap")
    parser.add_argument("--image", default="blueyos-disk.img")
    parser.add_argument("--root-mb", type=int, default=64)
    parser.add_argument("--swap-mb", type=int, default=16)
    parser.add_argument("--slack-mb", type=int, default=16)
    parser.add_argument("--init", default="user/init.elf")
    parser.add_argument("--root-label", default="BlueyRoot")
    parser.add_argument("--swap-label", default="ChatterSwap")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    image = repo / args.image
    init_path = repo / args.init
    root_sectors = sectors_from_mb(args.root_mb)
    swap_sectors = sectors_from_mb(args.swap_mb)
    slack_sectors = sectors_from_mb(args.slack_mb)
    swap_start = ROOT_START_LBA + root_sectors
    total_sectors = swap_start + swap_sectors + slack_sectors
    total_bytes = total_sectors * SECTOR_SIZE

    if not init_path.exists():
        raise SystemExit(f"missing init payload: {init_path}")

    with image.open("wb") as fp:
        fp.truncate(total_bytes)

    write_mbr(image, ROOT_START_LBA, root_sectors, swap_start, swap_sectors)

    with tempfile.NamedTemporaryFile("w", delete=False, encoding="ascii", newline="\n") as fstab_fp:
        fstab_fp.write("# BlueyOS mount table - Chilli keeps it organised\n")
        fstab_fp.write("/dev/hda1 / blueyfs defaults 0 1\n")
        fstab_fp.write("/dev/hda2 none swap defaults 0 0\n")
        fstab_name = fstab_fp.name

    try:
        run([
            str(repo / "tools" / "mkfs_blueyfs"),
            "-F",
            "-L", args.root_label,
            "-o", str(ROOT_START_LBA),
            "-n", str(root_sectors),
            "-I", str(init_path),
            "-T", fstab_name,
            str(image),
        ], repo)

        run([
            str(repo / "tools" / "mkswap_blueyfs"),
            "-L", args.swap_label,
            "-o", str(swap_start),
            "-n", str(swap_sectors),
            str(image),
        ], repo)
    finally:
        try:
            os.unlink(fstab_name)
        except OSError:
            pass

    print(f"[DISK] Built {image.name} with BlueyFS root at LBA {ROOT_START_LBA} and swap at LBA {swap_start}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
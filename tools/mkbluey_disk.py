#!/usr/bin/env python3
import argparse
import os
import struct
import subprocess
import tempfile
import shutil
from pathlib import Path

SECTOR_SIZE = 512
BOOT_START_LBA = 2048
BOOT_MB = 32


def sectors_from_mb(mb: int) -> int:
    return mb * 1024 * 1024 // SECTOR_SIZE


def write_mbr(image: Path,
              boot_start: int, boot_sectors: int,
              root_start: int, root_sectors: int,
              swap_start: int, swap_sectors: int) -> None:
    mbr = bytearray(512)

    def put_entry(index: int, part_type: int, start_lba: int, sector_count: int, bootable: bool = False) -> None:
        offset = 446 + index * 16
        entry = struct.pack(
            "<B3sB3sII",
            0x80 if bootable else 0x00,
            b"\x00\x02\x00",
            part_type,
            b"\xfe\xff\xff",
            start_lba,
            sector_count,
        )
        mbr[offset:offset + 16] = entry

    put_entry(0, 0x83, boot_start, boot_sectors, bootable=True)
    put_entry(1, 0x83, root_start, root_sectors)
    put_entry(2, 0x82, swap_start, swap_sectors)
    mbr[510:512] = b"\x55\xaa"

    with image.open("r+b") as fp:
        fp.seek(0)
        fp.write(mbr)


def write_partition_region(image: Path, offset_lba: int, partition_image: Path) -> None:
    with image.open("r+b") as disk_fp, partition_image.open("rb") as part_fp:
        disk_fp.seek(offset_lba * SECTOR_SIZE)
        shutil.copyfileobj(part_fp, disk_fp)


def build_boot_partition(repo: Path, image: Path, boot_mb: int, root_device: str, root_fstype: str) -> None:
    boot_sectors = sectors_from_mb(boot_mb)
    boot_size_bytes = boot_sectors * SECTOR_SIZE
    boot_img = image.with_suffix(".boot.tmp")
    boot_stage = repo / ".boot-stage"
    core_img = image.with_suffix(".core.tmp")
    early_cfg = image.with_suffix(".early.cfg.tmp")
    boot_img_src = Path("/usr/lib/grub/i386-pc/boot.img")
    modules = [
        "biosdisk",
        "part_msdos",
        "ext2",
        "multiboot",
        "serial",
        "terminal",
        "echo",
    ]

    if not boot_img_src.exists():
        raise SystemExit(f"missing GRUB BIOS boot image: {boot_img_src}")

    if boot_stage.exists():
        shutil.rmtree(boot_stage)
    (boot_stage / "boot" / "grub").mkdir(parents=True)
    shutil.copy2(repo / "blueyos.elf", boot_stage / "boot" / "blueyos.elf")

    disk_grub_cfg = (
        "serial --unit=0 --speed=115200\n"
        "terminal_output --append serial\n"
        "set timeout=1\n"
        "set default=0\n"
        "menuentry \"BlueyOS - Hard Disk Boot\" {\n"
        f"    multiboot /boot/blueyos.elf root={root_device} rootfstype={root_fstype}\n"
        "    boot\n"
        "}\n"
    )
    (boot_stage / "boot" / "grub" / "grub.cfg").write_text(disk_grub_cfg, encoding="ascii")

    early_grub_cfg = (
        "serial --unit=0 --speed=115200\n"
        "terminal_output --append serial\n"
        "set root=(hd0,msdos1)\n"
        f"multiboot /boot/blueyos.elf root={root_device} rootfstype={root_fstype}\n"
        "boot\n"
    )
    early_cfg.write_text(early_grub_cfg, encoding="ascii")

    with boot_img.open("wb") as fp:
        fp.truncate(boot_size_bytes)

    run([
        "mke2fs",
        "-q",
        "-F",
        "-t",
        "ext2",
        "-L",
        "BlueyBoot",
        "-d",
        str(boot_stage),
        str(boot_img),
    ], repo)

    run([
        "grub-mkimage",
        "-O",
        "i386-pc",
        "-c",
        str(early_cfg),
        "-p",
        "/boot/grub",
        "-o",
        str(core_img),
        *modules,
    ], repo)

    core_data = core_img.read_bytes()
    max_embed = (BOOT_START_LBA - 1) * SECTOR_SIZE
    if len(core_data) > max_embed:
        raise SystemExit(f"GRUB core image too large for MBR gap: {len(core_data)} > {max_embed}")

    with image.open("r+b") as fp:
        mbr = bytearray(fp.read(512))
        boot_code = boot_img_src.read_bytes()
        mbr[:446] = boot_code[:446]
        mbr[510:512] = b"\x55\xaa"
        fp.seek(0)
        fp.write(mbr)
        fp.seek(SECTOR_SIZE)
        fp.write(core_data)

    write_partition_region(image, BOOT_START_LBA, boot_img)

    core_img.unlink(missing_ok=True)
    boot_img.unlink(missing_ok=True)
    early_cfg.unlink(missing_ok=True)
    shutil.rmtree(boot_stage, ignore_errors=True)


def run(cmd, cwd: Path) -> None:
    subprocess.run(cmd, cwd=str(cwd), check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a GRUB-bootable BlueyOS disk image with boot, root, and swap partitions")
    parser.add_argument("--image", default="blueyos-disk.img")
    parser.add_argument("--boot-mb", type=int, default=BOOT_MB)
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
    boot_sectors = sectors_from_mb(args.boot_mb)
    root_sectors = sectors_from_mb(args.root_mb)
    swap_sectors = sectors_from_mb(args.swap_mb)
    slack_sectors = sectors_from_mb(args.slack_mb)
    root_start = BOOT_START_LBA + boot_sectors
    swap_start = root_start + root_sectors
    total_sectors = swap_start + swap_sectors + slack_sectors
    total_bytes = total_sectors * SECTOR_SIZE

    if not init_path.exists():
        raise SystemExit(f"missing init payload: {init_path}")

    with image.open("wb") as fp:
        fp.truncate(total_bytes)

    write_mbr(image, BOOT_START_LBA, boot_sectors, root_start, root_sectors, swap_start, swap_sectors)

    with tempfile.NamedTemporaryFile("w", delete=False, encoding="ascii", newline="\n") as fstab_fp:
        fstab_fp.write("# BlueyOS mount table - Chilli keeps it organised\n")
        fstab_fp.write("/dev/hda2 / blueyfs defaults 0 1\n")
        fstab_fp.write("/dev/hda3 none swap defaults 0 0\n")
        fstab_name = fstab_fp.name

    try:
        build_boot_partition(repo, image, args.boot_mb, "/dev/hda2", "blueyfs")

        run([
            str(repo / "tools" / "mkfs_blueyfs"),
            "-F",
            "-L", args.root_label,
            "-o", str(root_start),
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

    print(f"[DISK] Built {image.name} with ext2 boot at LBA {BOOT_START_LBA}, BlueyFS root at LBA {root_start}, and swap at LBA {swap_start}")
    print("[DISK] Installed GRUB boot.img in the MBR and embedded core.img in the post-MBR gap.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
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


def build_boot_partition(repo: Path, image: Path, kernel_path: Path, boot_mb: int, root_device: str, root_fstype: str, boot_extra_dir: str = None, init_kernel_path: str = "/sbin/claw") -> None:
    boot_sectors = sectors_from_mb(boot_mb)
    boot_size_bytes = boot_sectors * SECTOR_SIZE
    boot_img = image.with_suffix(".boot.tmp")
    boot_stage = image.parent / ".boot-stage"
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
    shutil.copy2(kernel_path, boot_stage / "boot" / "blueyos.elf")

    # If an extra boot directory was provided, copy its contents into the
    # boot staging directory so they appear under /boot in the created ext2.
    if boot_extra_dir:
        extra_path = Path(boot_extra_dir)
        if not extra_path.exists() or not extra_path.is_dir():
            raise SystemExit(f"--boot-extra-dir not found or not a dir: {boot_extra_dir}")
        for item in extra_path.iterdir():
            dest = boot_stage / "boot" / item.name
            if item.is_dir():
                try:
                    shutil.copytree(item, dest, symlinks=True, dirs_exist_ok=True)
                except TypeError:
                    # Older Python versions may not support dirs_exist_ok
                    try:
                        shutil.copytree(item, dest, symlinks=True)
                    except Exception:
                        # Fallback: create dest and copy children
                        dest.mkdir(parents=True, exist_ok=True)
                        for root, dirs, files in os.walk(item):
                            rel = Path(root).relative_to(item)
                            for d in dirs:
                                (dest / rel / d).mkdir(parents=True, exist_ok=True)
                            for f in files:
                                srcf = Path(root) / f
                                dstf = dest / rel / f
                                try:
                                    shutil.copy2(srcf, dstf)
                                except Exception:
                                    # ignore problematic entries
                                    pass
            else:
                shutil.copy2(item, dest)

    # Include the requested init path in the kernel commandline so the kernel
    # runs the intended userspace `claw` program by default.
    disk_grub_cfg = (
        "serial --unit=0 --speed=115200\n"
        "terminal_output --append serial\n"
        "set timeout=1\n"
        "set default=0\n"
        "menuentry \"BlueyOS - Hard Disk Boot\" {\n"
        f"    multiboot /boot/blueyos.elf root={root_device} rootfstype={root_fstype} init={init_kernel_path}\n"
        "    boot\n"
        "}\n"
    )
    (boot_stage / "boot" / "grub" / "grub.cfg").write_text(disk_grub_cfg, encoding="ascii")

    early_grub_cfg = (
        "serial --unit=0 --speed=115200\n"
        "terminal_output --append serial\n"
        "set root=(hd0,msdos1)\n"
        f"multiboot /boot/blueyos.elf root={root_device} rootfstype={root_fstype} init={init_kernel_path}\n"
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


def prepare_root_extra_dir(root_extra_dir: str | None, timezone_file: str | None) -> str | None:
    if not root_extra_dir:
        return None

    root_extra = Path(root_extra_dir)
    if not root_extra.exists() or not root_extra.is_dir():
        raise SystemExit(f"--root-extra-dir not found or not a dir: {root_extra_dir}")

    # Seed the runtime tree claw expects even when the external sysroot was
    # installed without empty directories.
    for relpath in (
        "run",
        "run/claw",
        "run/lock",
        "run/log",
        "var",
        "var/lib",
        "var/lib/claw",
        "var/log",
        "var/log/claw",
    ):
        (root_extra / relpath).mkdir(parents=True, exist_ok=True)

    localtime_path = root_extra / "etc" / "localtime"
    if localtime_path.exists() or not timezone_file:
        return str(root_extra)

    tz_source = Path(timezone_file)
    if not tz_source.exists() or not tz_source.is_file():
        print(f"[DISK] Timezone source missing, skipping /etc/localtime provisioning: {tz_source}")
        return str(root_extra)

    localtime_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(tz_source, localtime_path)
    print(f"[DISK] Provisioned /etc/localtime from {tz_source}")
    return str(root_extra)


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a GRUB-bootable BlueyOS disk image with boot, root, and swap partitions")
    parser.add_argument("--image", default="build/blueyos-disk.img")
    parser.add_argument("--boot-mb", type=int, default=BOOT_MB)
    parser.add_argument("--root-mb", type=int, default=64)
    parser.add_argument("--swap-mb", type=int, default=16)
    parser.add_argument("--slack-mb", type=int, default=16)
    parser.add_argument("--kernel", default="build/blueyos.elf")
    parser.add_argument("--init", default="build/userspace/init/init-musl.elf")
    parser.add_argument("--boot-extra-dir", default=None,
                        help="Copy contents of this host dir into the boot partition's /boot before building")
    parser.add_argument("--mkfs-tool", default="build/tools/mkfs_blueyfs")
    parser.add_argument("--mkswap-tool", default="build/tools/mkswap_blueyfs")
    parser.add_argument("--root-label", default="BlueyRoot")
    parser.add_argument("--swap-label", default="ChatterSwap")
    parser.add_argument("--root-extra-dir", default=None,
                        help="Recursively copy contents of this host dir into the root filesystem during mkfs (install into /)")
    parser.add_argument("--init-kernel-path", default="/sbin/claw",
                        help="Kernel commandline init= path to embed in grub.cfg (default: /sbin/claw)")
    parser.add_argument("--timezone-file", default="/usr/share/zoneinfo/Australia/Brisbane",
                        help="Host tzfile to install as /etc/localtime when root-extra-dir lacks one")
    args = parser.parse_args()

    repo = Path(__file__).resolve().parent.parent
    image = repo / args.image
    kernel_path = repo / args.kernel
    init_path = repo / args.init
    # If a root-extra-dir is provided, prefer /sbin/claw from that sysroot
    # so the kernel sees init=/sbin/claw; fall back to bin/init if claw
    # isn't present. This allows using an external sysroot like
    # /opt/blueyos-sysroot as the source of the init payload.
    if getattr(args, 'root_extra_dir', None):
        root_extra = Path(args.root_extra_dir)
        sbin_claw = root_extra / "sbin" / "claw"
        root_bin_init = root_extra / "bin" / "init"
        if sbin_claw.exists():
            init_path = sbin_claw
            print(f"[DISK] Using /sbin/claw from root-extra-dir: {init_path}")
        elif root_bin_init.exists():
            init_path = root_bin_init
            print(f"[DISK] Using /bin/init from root-extra-dir: {init_path}")
    mkfs_tool = repo / args.mkfs_tool
    mkswap_tool = repo / args.mkswap_tool
    effective_root_extra_dir = prepare_root_extra_dir(
        getattr(args, 'root_extra_dir', None),
        getattr(args, 'timezone_file', None),
    )
    boot_sectors = sectors_from_mb(args.boot_mb)
    root_sectors = sectors_from_mb(args.root_mb)
    swap_sectors = sectors_from_mb(args.swap_mb)
    slack_sectors = sectors_from_mb(args.slack_mb)
    root_start = BOOT_START_LBA + boot_sectors
    swap_start = root_start + root_sectors
    total_sectors = swap_start + swap_sectors + slack_sectors
    total_bytes = total_sectors * SECTOR_SIZE

    image.parent.mkdir(parents=True, exist_ok=True)

    if not init_path.exists():
        raise SystemExit(f"missing init payload: {init_path}")
    if not kernel_path.exists():
        raise SystemExit(f"missing kernel image: {kernel_path}")
    if not mkfs_tool.exists():
        raise SystemExit(f"missing mkfs tool: {mkfs_tool}")
    if not mkswap_tool.exists():
        raise SystemExit(f"missing mkswap tool: {mkswap_tool}")

    with image.open("wb") as fp:
        fp.truncate(total_bytes)

    write_mbr(image, BOOT_START_LBA, boot_sectors, root_start, root_sectors, swap_start, swap_sectors)

    with tempfile.NamedTemporaryFile("w", delete=False, encoding="ascii", newline="\n") as fstab_fp:
        fstab_fp.write("# BlueyOS mount table - Chilli keeps it organised\n")
        fstab_fp.write("/dev/hda2 / blueyfs defaults 0 1\n")
        fstab_fp.write("/dev/hda3 none swap defaults 0 0\n")
        fstab_name = fstab_fp.name

    try:
        build_boot_partition(repo, image, kernel_path, args.boot_mb, "/dev/hda2", "blueyfs", getattr(args, 'boot_extra_dir', None), getattr(args, 'init_kernel_path', '/sbin/claw'))

        mkfs_cmd = [str(mkfs_tool), "-F", "-L", args.root_label, "-o", str(root_start), "-n", str(root_sectors), "-I", str(init_path), "-T", fstab_name]
        if effective_root_extra_dir:
            mkfs_cmd += ["-A", effective_root_extra_dir]
        mkfs_cmd.append(str(image))
        run(mkfs_cmd, repo)

        run([
            str(mkswap_tool),
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
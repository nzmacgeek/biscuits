#!/usr/bin/env python3
import argparse
import os
import struct
import subprocess
import tempfile
import shutil
import math
from pathlib import Path

SECTOR_SIZE = 512
FS_BLOCK_SIZE = 4096
BOOT_START_LBA = 2048
BOOT_MB = 32
DEFAULT_ROOT_BUFFER_PCT = 30

DEFAULT_PASSWD = """root:x:0:0:root:/root:/bin/sh
bandit:x:0:0:Bandit Heeler (Root):/root:/bin/sh
bluey:x:1:1:Bluey Heeler:/home/bluey:/bin/sh
bingo:x:2:1:Bingo Heeler:/home/bingo:/bin/sh
chilli:x:3:1:Chilli Heeler:/home/chilli:/bin/sh
jack:x:4:2:Jack (Bluey's mate):/home/jack:/bin/sh
judo:x:5:2:Judo:/home/judo:/bin/sh
"""

DEFAULT_GROUP = """root:x:0:root,bandit
heelers:x:1:bluey,bingo,chilli,bandit
mates:x:2:jack,judo
"""

DEFAULT_SHADOW = """root:$6$blueyos$i71qzV0KYDjxto0fG97RZJk/yuMr.qZzc9zx79xmx9pm56pa5v6liPjsPiSa61tRrhF0j/cLIqXhrHd.GWm7K0:0:0:99999:7:::
bandit:$6$blueyos$i71qzV0KYDjxto0fG97RZJk/yuMr.qZzc9zx79xmx9pm56pa5v6liPjsPiSa61tRrhF0j/cLIqXhrHd.GWm7K0:0:0:99999:7:::
bluey:$pbkdf2-sha256$10000$7d9f4a1e0c0b5f6e1122334455667788$f5f07c6926875023aedfd2087b52e1dca0c596b16e7d3a360ac744eec25720f5:0:0:99999:7:::
bingo:!:0:0:99999:7:::
chilli:*:0:0:99999:7:::
jack:*:0:0:99999:7:::
judo:*:0:0:99999:7:::
"""
MBR_PARTITION_TABLE_OFFSET = 446
MBR_ENTRY_SIZE = 16
BLUEYFS_INODE_SIZE = 256
BLUEYFS_INODES_PER_GROUP = 8192
BLUEYFS_BLOCKS_PER_GROUP = 8192
BLUEYFS_JOURNAL_BLOCKS = 2048
BLUEYFS_FIRST_INO = 11


def sectors_from_mb(mb: int) -> int:
    return mb * 1024 * 1024 // SECTOR_SIZE


def blocks_from_bytes(size_bytes: int) -> int:
    if size_bytes <= 0:
        return 0
    return (size_bytes + FS_BLOCK_SIZE - 1) // FS_BLOCK_SIZE


def pointer_metadata_blocks(data_blocks: int) -> int:
    if data_blocks <= 12:
        return 0

    ptrs_per_block = FS_BLOCK_SIZE // 4
    remaining_blocks = data_blocks - 12
    metadata_blocks = 1

    if remaining_blocks <= ptrs_per_block:
        return metadata_blocks

    remaining_blocks -= ptrs_per_block
    metadata_blocks += 1
    metadata_blocks += math.ceil(remaining_blocks / ptrs_per_block)
    return metadata_blocks


def filesystem_overhead_blocks(total_blocks: int) -> tuple[int, int, int]:
    num_groups = max(1, math.ceil(total_blocks / BLUEYFS_BLOCKS_PER_GROUP))
    inodes_per_block = FS_BLOCK_SIZE // BLUEYFS_INODE_SIZE
    inode_table_blocks = math.ceil(BLUEYFS_INODES_PER_GROUP / inodes_per_block)
    overhead_per_group = 1 + 1 + inode_table_blocks + 1
    total_overhead = num_groups * overhead_per_group + 1 + BLUEYFS_JOURNAL_BLOCKS
    free_blocks = max(0, total_blocks - total_overhead)
    reserved_blocks = free_blocks // 20
    usable_blocks = free_blocks - reserved_blocks
    return num_groups, total_overhead, usable_blocks


def estimate_directory_payload(root_dir: Path) -> tuple[int, int, int, int, int]:
    total_file_bytes = 0
    total_file_blocks = 0
    total_dirs = 0
    total_files = 0
    total_indirect_blocks = 0
    active_real_dirs: set[Path] = set()
    root_dir_resolved = root_dir.resolve()

    def resolve_packaged_path(entry: Path) -> Path | None:
        if not entry.is_symlink():
            return entry

        link_target = os.readlink(entry)
        if os.path.isabs(link_target):
            candidate = root_dir_resolved / link_target.lstrip("/")
        else:
            candidate = entry.parent / link_target

        resolved = candidate.resolve()
        try:
            resolved.relative_to(root_dir_resolved)
        except ValueError:
            return None
        return resolved

    def scan_dir(path: Path) -> None:
        nonlocal total_file_bytes, total_file_blocks, total_dirs, total_files, total_indirect_blocks
        real_path = path.resolve()
        if real_path in active_real_dirs:
            return
        active_real_dirs.add(real_path)

        try:
            entries = list(path.iterdir())
        except OSError:
            active_real_dirs.remove(real_path)
            return

        for entry in entries:
            try:
                packaged_path = resolve_packaged_path(entry)
                if packaged_path is None:
                    continue

                if packaged_path.is_dir():
                    total_dirs += 1
                    scan_dir(packaged_path)
                elif packaged_path.is_file():
                    size_bytes = packaged_path.stat().st_size
                    file_blocks = blocks_from_bytes(size_bytes)
                    total_files += 1
                    total_file_bytes += size_bytes
                    total_file_blocks += file_blocks
                    total_indirect_blocks += pointer_metadata_blocks(file_blocks)
            except OSError:
                continue

        active_real_dirs.remove(real_path)

    scan_dir(root_dir)
    return total_file_bytes, total_file_blocks, total_dirs, total_files, total_indirect_blocks


def estimate_root_partition_sectors(root_extra_dir: str | None,
                                    init_path: Path,
                                    fstab_path: Path,
                                    fallback_root_mb: int,
                                    buffer_pct: int) -> tuple[int, dict[str, int | bool]]:
    details: dict[str, int | bool] = {
        "dynamic": False,
        "payload_bytes": 0,
        "payload_blocks": 0,
        "buffered_blocks": 0,
        "required_inodes": 0,
        "total_blocks": 0,
        "total_mb": fallback_root_mb,
    }

    if not root_extra_dir:
        return sectors_from_mb(fallback_root_mb), details

    root_dir = Path(root_extra_dir)
    if not root_dir.exists() or not root_dir.is_dir():
        return sectors_from_mb(fallback_root_mb), details

    root_dir_resolved = root_dir.resolve()
    payload_bytes, payload_blocks, directory_count, file_count, indirect_blocks = estimate_directory_payload(root_dir)
    for required_dir in ("bin", "etc"):
        if not (root_dir / required_dir).exists():
            directory_count += 1

    explicit_files: set[Path] = set()
    if init_path.exists():
        explicit_files.add(init_path.resolve())
    if fstab_path.exists():
        explicit_files.add(fstab_path.resolve())

    for explicit_file in explicit_files:
        try:
            if not explicit_file.is_relative_to(root_dir_resolved):
                file_size = explicit_file.stat().st_size
                file_blocks = blocks_from_bytes(file_size)
                payload_bytes += file_size
                payload_blocks += file_blocks
                file_count += 1
                indirect_blocks += pointer_metadata_blocks(file_blocks)
        except ValueError:
            file_size = explicit_file.stat().st_size
            file_blocks = blocks_from_bytes(file_size)
            payload_bytes += file_size
            payload_blocks += file_blocks
            file_count += 1
            indirect_blocks += pointer_metadata_blocks(file_blocks)

    directory_blocks = directory_count

    buffered_blocks = math.ceil((payload_blocks + directory_blocks + indirect_blocks) * (1 + (buffer_pct / 100.0)))
    required_inodes = BLUEYFS_FIRST_INO + directory_count + file_count

    total_blocks = max(1, buffered_blocks)
    while True:
        _, _, usable_blocks = filesystem_overhead_blocks(total_blocks)
        num_groups = max(1, math.ceil(total_blocks / BLUEYFS_BLOCKS_PER_GROUP))
        total_inodes = num_groups * BLUEYFS_INODES_PER_GROUP
        if usable_blocks >= buffered_blocks and total_inodes - BLUEYFS_FIRST_INO >= required_inodes:
            break
        total_blocks += 1

    total_mb = math.ceil((total_blocks * FS_BLOCK_SIZE) / (1024 * 1024))
    details.update({
        "dynamic": True,
        "payload_bytes": payload_bytes,
        "payload_blocks": payload_blocks + directory_blocks + indirect_blocks,
        "buffered_blocks": buffered_blocks,
        "required_inodes": required_inodes,
        "total_blocks": total_blocks,
        "total_mb": total_mb,
    })
    return total_blocks * (FS_BLOCK_SIZE // SECTOR_SIZE), details


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


def build_boot_partition(repo: Path, image: Path, kernel_path: Path, boot_sectors: int, root_device: str, root_fstype: str, boot_extra_dir: str | None = None, init_kernel_path: str = "/sbin/claw") -> None:
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
        "configfile",
        "normal",
        "multiboot",
        "serial",
        "terminal",
        "video",
        "vbe",
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
        "    set gfxpayload=text\n"
        f"    multiboot /boot/blueyos.elf root={root_device} rootfstype={root_fstype} init={init_kernel_path}\n"
        "    boot\n"
        "}\n"
        "menuentry \"BlueyOS - Safe Mode\" {\n"
        "    set gfxpayload=text\n"
        f"    multiboot /boot/blueyos.elf safe root={root_device} rootfstype={root_fstype} init={init_kernel_path}\n"
        "    boot\n"
        "}\n"
    )
    (boot_stage / "boot" / "grub" / "grub.cfg").write_text(disk_grub_cfg, encoding="ascii")

    early_grub_cfg = (
        "serial --unit=0 --speed=115200\n"
        "terminal_output --append serial\n"
        "set root=(hd0,msdos1)\n"
        "set prefix=(hd0,msdos1)/boot/grub\n"
        "configfile /boot/grub/grub.cfg\n"
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


def read_existing_layout(image: Path) -> tuple[int, int, int, int, int, int]:
    with image.open("rb") as fp:
        mbr = fp.read(SECTOR_SIZE)

    if len(mbr) != SECTOR_SIZE or mbr[510:512] != b"\x55\xaa":
        raise SystemExit(f"invalid or missing MBR in existing image: {image}")

    def read_entry(index: int) -> tuple[int, int]:
        offset = MBR_PARTITION_TABLE_OFFSET + index * MBR_ENTRY_SIZE
        _status, _chs_first, _ptype, _chs_last, start_lba, sectors = struct.unpack_from("<B3sB3sII", mbr, offset)
        if start_lba == 0 or sectors == 0:
            raise SystemExit(f"missing partition entry {index + 1} in existing image: {image}")
        return start_lba, sectors

    boot_start, boot_sectors = read_entry(0)
    root_start, root_sectors = read_entry(1)
    swap_start, swap_sectors = read_entry(2)
    if boot_start != BOOT_START_LBA:
        raise SystemExit(
            f"unexpected boot partition start in existing image ({boot_start}); expected {BOOT_START_LBA}. Use --erase to rebuild."
        )

    return boot_start, boot_sectors, root_start, root_sectors, swap_start, swap_sectors


def stage_root_extra_dir(root_extra: Path) -> Path:
    stage_dir = Path(tempfile.mkdtemp(prefix="blueyos-root-extra."))
    shutil.copytree(root_extra, stage_dir, symlinks=True, dirs_exist_ok=True)
    print(f"[DISK] Staged root-extra-dir in {stage_dir}")
    return stage_dir


def ensure_default_account_files(root_extra: Path) -> None:
    etc_dir = root_extra / "etc"
    etc_dir.mkdir(parents=True, exist_ok=True)

    account_files = (
        (etc_dir / "passwd", DEFAULT_PASSWD, 0o644),
        (etc_dir / "group", DEFAULT_GROUP, 0o644),
        (etc_dir / "shadow", DEFAULT_SHADOW, 0o600),
    )

    for path, content, mode in account_files:
        if path.exists():
            continue
        path.write_text(content, encoding="ascii")
        path.chmod(mode)
        print(f"[DISK] Provisioned missing {path.relative_to(root_extra)}")


def maybe_override_login_binary(repo: Path, root_extra: Path) -> None:
    local_login = repo.parent.parent / "login-tools" / "pkg" / "payload" / "usr" / "bin" / "login"
    target_login = root_extra / "usr" / "bin" / "login"

    if not local_login.exists():
        return

    target_login.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(local_login, target_login)
    print(f"[DISK] Using locally built login binary from {local_login}")


def limit_claw_login_services(root_extra: Path) -> None:
    manifest_path = root_extra / "etc" / "claw" / "units.manifest"
    target_path = root_extra / "etc" / "claw" / "targets.d" / "claw-multiuser.target.yml"
    basic_target_path = root_extra / "etc" / "claw" / "targets.d" / "claw-basic.target.yml"

    if manifest_path.exists():
        manifest_lines = [line.strip() for line in manifest_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        filtered_lines = []
        for line in manifest_lines:
            if line.endswith("matey@tty2.yml") or line.endswith("matey@tty3.yml"):
                continue
            filtered_lines.append(line)
        if filtered_lines != manifest_lines:
            manifest_path.write_text("\n".join(filtered_lines) + "\n", encoding="utf-8")
            print("[DISK] Limited claw manifest to matey@tty1 for login validation")

    if target_path.exists():
        target_text = target_path.read_text(encoding="utf-8")
        updated_text = target_text.replace("wants: matey@tty1 matey@tty2 matey@tty3", "wants: matey@tty1")
        if not basic_target_path.exists():
            updated_text = updated_text.replace("requires: claw-basic.target\n", "")
            updated_text = updated_text.replace("after: claw-basic.target\n", "")
        if updated_text != target_text:
            target_path.write_text(updated_text, encoding="utf-8")
            print("[DISK] Limited claw-multiuser.target to matey@tty1")

    if not basic_target_path.exists():
        for service_name in ("matey@tty1.yml", "matey@tty2.yml", "matey@tty3.yml"):
            service_path = root_extra / "etc" / "claw" / "services.d" / service_name
            if not service_path.exists():
                continue
            service_text = service_path.read_text(encoding="utf-8")
            updated_text = service_text.replace("after: claw-basic.target\n", "")
            if updated_text != service_text:
                service_path.write_text(updated_text, encoding="utf-8")
                print(f"[DISK] Removed missing claw-basic.target dependency from {service_name}")


def prepare_root_extra_dir(repo: Path, root_extra_dir: str | None, timezone_file: str | None) -> tuple[str | None, str | None]:
    if not root_extra_dir:
        return None, None

    root_extra = Path(root_extra_dir)
    if not root_extra.exists() or not root_extra.is_dir():
        raise SystemExit(f"--root-extra-dir not found or not a dir: {root_extra_dir}")

    effective_root = root_extra
    cleanup_dir: Path | None = None

    login_compat_path = root_extra / "sbin" / "login"
    usr_bin_login = root_extra / "usr" / "bin" / "login"
    if not login_compat_path.exists() and usr_bin_login.exists():
        effective_root = stage_root_extra_dir(root_extra)
        cleanup_dir = effective_root

    ensure_default_account_files(effective_root)
    maybe_override_login_binary(repo, effective_root)
    limit_claw_login_services(effective_root)

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
        (effective_root / relpath).mkdir(parents=True, exist_ok=True)

    login_compat_path = effective_root / "sbin" / "login"
    usr_bin_login = effective_root / "usr" / "bin" / "login"
    if not login_compat_path.exists() and usr_bin_login.exists():
        login_compat_path.parent.mkdir(parents=True, exist_ok=True)
        try:
            login_compat_path.symlink_to(Path("..") / "usr" / "bin" / "login")
            print("[DISK] Provisioned /sbin/login -> ../usr/bin/login compatibility link")
        except FileExistsError:
            pass

    localtime_path = effective_root / "etc" / "localtime"
    if localtime_path.exists() or not timezone_file:
        return str(effective_root), str(cleanup_dir) if cleanup_dir else None

    tz_source = Path(timezone_file)
    if not tz_source.exists() or not tz_source.is_file():
        print(f"[DISK] Timezone source missing, skipping /etc/localtime provisioning: {tz_source}")
        return str(effective_root), str(cleanup_dir) if cleanup_dir else None

    localtime_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(tz_source, localtime_path)
    print(f"[DISK] Provisioned /etc/localtime from {tz_source}")
    return str(effective_root), str(cleanup_dir) if cleanup_dir else None


def main() -> int:
    parser = argparse.ArgumentParser(description="Build a GRUB-bootable BlueyOS disk image with boot, root, and swap partitions")
    parser.add_argument("--image", default="build/blueyos-disk.img")
    parser.add_argument("--boot-mb", type=int, default=BOOT_MB)
    parser.add_argument("--root-mb", type=int, default=64)
    parser.add_argument("--swap-mb", type=int, default=16)
    parser.add_argument("--slack-mb", type=int, default=16)
    parser.add_argument("--root-buffer-pct", type=int, default=DEFAULT_ROOT_BUFFER_PCT,
                        help="Extra headroom added to the estimated root filesystem payload size")
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
    parser.add_argument("--erase", action="store_true",
                        help="Wipe and recreate the full disk layout before populating partitions")
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
    effective_root_extra_dir, cleanup_root_extra_dir = prepare_root_extra_dir(
        repo,
        getattr(args, 'root_extra_dir', None),
        getattr(args, 'timezone_file', None),
    )
    with tempfile.NamedTemporaryFile("w", delete=False, encoding="ascii", newline="\n") as fstab_fp:
        fstab_fp.write("# BlueyOS mount table - Chilli keeps it organised\n")
        fstab_fp.write("/dev/disk0s2 / blueyfs defaults 0 1\n")
        fstab_fp.write("/dev/disk0s3 none swap defaults 0 0\n")
        fstab_name = fstab_fp.name
    fstab_path = Path(fstab_name)

    image.parent.mkdir(parents=True, exist_ok=True)

    if not init_path.exists():
        raise SystemExit(f"missing init payload: {init_path}")
    if not kernel_path.exists():
        raise SystemExit(f"missing kernel image: {kernel_path}")
    if not mkfs_tool.exists():
        raise SystemExit(f"missing mkfs tool: {mkfs_tool}")
    if not mkswap_tool.exists():
        raise SystemExit(f"missing mkswap tool: {mkswap_tool}")

    requested_root_sectors, requested_root_size_details = estimate_root_partition_sectors(
        effective_root_extra_dir,
        init_path,
        fstab_path,
        args.root_mb,
        args.root_buffer_pct,
    )

    recreate_image = args.erase or not image.exists()
    root_size_details = requested_root_size_details
    boot_sectors = 0
    root_start = 0
    root_sectors = 0
    swap_start = 0
    swap_sectors = 0

    if not recreate_image:
        _boot_start, boot_sectors, root_start, root_sectors, swap_start, swap_sectors = read_existing_layout(image)
        required_bytes = (swap_start + swap_sectors) * SECTOR_SIZE
        image_bytes = image.stat().st_size
        if image_bytes < required_bytes:
            raise SystemExit(
                f"existing image is smaller than its partition table expects ({image_bytes} < {required_bytes}); use --erase"
            )
        if root_sectors < requested_root_sectors:
            print(
                "[DISK] Existing root partition too small for current payload: "
                f"{root_sectors * SECTOR_SIZE // (1024 * 1024)} MiB < "
                f"{requested_root_sectors * SECTOR_SIZE // (1024 * 1024)} MiB; recreating image"
            )
            recreate_image = True
        else:
            print(f"[DISK] Reusing existing image layout in {image}")

    if recreate_image:
        boot_sectors = sectors_from_mb(args.boot_mb)
        swap_sectors = sectors_from_mb(args.swap_mb)
        slack_sectors = sectors_from_mb(args.slack_mb)
        root_sectors = requested_root_sectors
        root_start = BOOT_START_LBA + boot_sectors
        swap_start = root_start + root_sectors
        total_sectors = swap_start + swap_sectors + slack_sectors
        total_bytes = total_sectors * SECTOR_SIZE

        with image.open("wb") as fp:
            fp.truncate(total_bytes)
        write_mbr(image, BOOT_START_LBA, boot_sectors, root_start, root_sectors, swap_start, swap_sectors)

    try:
        if root_size_details["dynamic"]:
            print(
                "[DISK] Root partition sized from sysroot payload: "
                f"{root_size_details['payload_bytes']} bytes -> "
                f"{root_size_details['total_mb']} MiB "
                f"({args.root_buffer_pct}% buffer included)"
            )
        else:
            print(f"[DISK] Root partition using fallback size: {args.root_mb} MiB")

        build_boot_partition(repo, image, kernel_path, boot_sectors, "/dev/disk0s2", "blueyfs", getattr(args, 'boot_extra_dir', None), getattr(args, 'init_kernel_path', '/sbin/claw'))

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
        if cleanup_root_extra_dir:
            shutil.rmtree(cleanup_root_extra_dir, ignore_errors=True)

    print(f"[DISK] Built {image.name} with ext2 boot at LBA {BOOT_START_LBA}, BlueyFS root at LBA {root_start}, and swap at LBA {swap_start}")
    print("[DISK] Installed GRUB boot.img in the MBR and embedded core.img in the post-MBR gap.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
#!/usr/bin/env python3

import argparse
import os
import shutil
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

SECTOR_SIZE = 512
BLOCK_SIZE = 4096
MAGIC = 0xB15C0001
MBR_ENTRY_OFFSET = 446
MBR_ENTRY_SIZE = 16
ROOT_PARTITION_INDEX = 1
INODE_SIZE = 256
INODES_PER_GROUP = 8192
BLOCKS_PER_GROUP = 8192
ROOT_INO = 2
N_DIRECT_BLOCKS = 12

IFMT = 0xF000
IFREG = 0x8000
IFDIR = 0x4000
IFLNK = 0xA000


@dataclass
class Inode:
    mode: int
    uid: int
    gid: int
    size_lo: int
    size_hi: int
    atime: int
    ctime: int
    mtime: int
    links_count: int
    blocks_lo: int
    block: tuple[int, ...]

    @property
    def size(self) -> int:
        return (self.size_hi << 32) | self.size_lo


class BlueyFSImage:
    def __init__(self, image_path: Path, start_sector: int | None = None):
        self.image_path = image_path
        self.fp = image_path.open("rb")
        self.start_sector = start_sector if start_sector is not None else self._read_root_partition_start()
        self.fs_offset = self.start_sector * SECTOR_SIZE
        self.block_count, self.inode_count = self._load_superblock()
        self.num_groups = (self.block_count + BLOCKS_PER_GROUP - 1) // BLOCKS_PER_GROUP
        self.bgd_table = self._read_exact(self.fs_offset + BLOCK_SIZE, self.num_groups * 32)

    def close(self) -> None:
        self.fp.close()

    def _read_exact(self, offset: int, size: int) -> bytes:
        self.fp.seek(offset)
        data = self.fp.read(size)
        if len(data) != size:
            raise ValueError(f"short read at offset {offset} (wanted {size} bytes, got {len(data)})")
        return data

    def _read_root_partition_start(self) -> int:
        mbr = self._read_exact(0, SECTOR_SIZE)
        if mbr[510:512] != b"\x55\xaa":
            raise ValueError("disk image does not contain a valid MBR; pass --start-sector for raw BlueyFS images")

        entry_offset = MBR_ENTRY_OFFSET + (ROOT_PARTITION_INDEX * MBR_ENTRY_SIZE)
        entry = mbr[entry_offset:entry_offset + MBR_ENTRY_SIZE]
        start_lba = struct.unpack_from("<I", entry, 8)[0]
        sector_count = struct.unpack_from("<I", entry, 12)[0]
        if start_lba == 0 or sector_count == 0:
            raise ValueError("root partition entry is empty; pass --start-sector explicitly")
        return start_lba

    def _load_superblock(self) -> tuple[int, int]:
        super_block = self._read_exact(self.fs_offset, BLOCK_SIZE)
        magic = struct.unpack_from("<I", super_block, 1024)[0]
        if magic != MAGIC:
            raise ValueError(
                f"BlueyFS magic mismatch at sector {self.start_sector}: expected 0x{MAGIC:08x}, got 0x{magic:08x}"
            )
        block_count = struct.unpack_from("<I", super_block, 1024 + 8)[0]
        inode_count = struct.unpack_from("<I", super_block, 1024 + 20)[0]
        return block_count, inode_count

    def _read_block(self, block_no: int) -> bytes:
        return self._read_exact(self.fs_offset + (block_no * BLOCK_SIZE), BLOCK_SIZE)

    def _bgd_inode_table_block(self, group: int) -> int:
        if group >= self.num_groups:
            raise FileNotFoundError(f"inode group {group} is out of range")
        return struct.unpack_from("<I", self.bgd_table, group * 32 + 8)[0]

    def read_inode(self, inode_no: int) -> Inode:
        if inode_no == 0 or inode_no > self.inode_count:
            raise FileNotFoundError(f"inode {inode_no} is out of range")

        inode_index = inode_no - 1
        group = inode_index // INODES_PER_GROUP
        local_index = inode_index % INODES_PER_GROUP
        inodes_per_block = BLOCK_SIZE // INODE_SIZE
        block_no = self._bgd_inode_table_block(group) + (local_index // inodes_per_block)
        block_offset = (local_index % inodes_per_block) * INODE_SIZE
        raw = self._read_block(block_no)[block_offset:block_offset + INODE_SIZE]

        mode = struct.unpack_from("<H", raw, 0)[0]
        uid = struct.unpack_from("<H", raw, 2)[0]
        size_lo = struct.unpack_from("<I", raw, 4)[0]
        atime = struct.unpack_from("<I", raw, 8)[0]
        ctime = struct.unpack_from("<I", raw, 12)[0]
        mtime = struct.unpack_from("<I", raw, 16)[0]
        gid = struct.unpack_from("<H", raw, 24)[0]
        links_count = struct.unpack_from("<H", raw, 26)[0]
        blocks_lo = struct.unpack_from("<I", raw, 28)[0]
        block = struct.unpack_from("<15I", raw, 40)
        size_hi = struct.unpack_from("<I", raw, 108)[0]

        return Inode(
            mode=mode,
            uid=uid,
            gid=gid,
            size_lo=size_lo,
            size_hi=size_hi,
            atime=atime,
            ctime=ctime,
            mtime=mtime,
            links_count=links_count,
            blocks_lo=blocks_lo,
            block=block,
        )

    def inode_block(self, inode: Inode, block_index: int) -> int:
        if block_index < N_DIRECT_BLOCKS:
            return inode.block[block_index]

        pointers_per_block = BLOCK_SIZE // 4
        block_index -= N_DIRECT_BLOCKS

        if block_index < pointers_per_block:
            indirect_block = inode.block[12]
            if indirect_block == 0:
                return 0
            indirect = self._read_block(indirect_block)
            return struct.unpack_from("<I", indirect, block_index * 4)[0]

        block_index -= pointers_per_block
        if block_index < pointers_per_block * pointers_per_block:
            level1_block = inode.block[13]
            if level1_block == 0:
                return 0
            level1 = self._read_block(level1_block)
            level1_index = block_index // pointers_per_block
            level2_index = block_index % pointers_per_block
            level2_block = struct.unpack_from("<I", level1, level1_index * 4)[0]
            if level2_block == 0:
                return 0
            level2 = self._read_block(level2_block)
            return struct.unpack_from("<I", level2, level2_index * 4)[0]

        return 0

    def read_file(self, inode: Inode) -> bytes:
        remaining = inode.size
        if remaining == 0:
            return b""

        chunks = []
        block_index = 0
        while remaining > 0:
            block_no = self.inode_block(inode, block_index)
            if block_no == 0:
                break
            block = self._read_block(block_no)
            chunk = min(remaining, BLOCK_SIZE)
            chunks.append(block[:chunk])
            remaining -= chunk
            block_index += 1

        data = b"".join(chunks)
        if len(data) != inode.size:
            raise ValueError(f"inode read was truncated: expected {inode.size} bytes, got {len(data)}")
        return data

    def iter_dir(self, inode: Inode):
        if (inode.mode & IFMT) != IFDIR:
            raise NotADirectoryError("inode is not a directory")

        cursor = 0
        while cursor < inode.size:
            logical_block = cursor // BLOCK_SIZE
            block_offset = cursor % BLOCK_SIZE
            block_no = self.inode_block(inode, logical_block)
            if block_no == 0:
                break
            block = self._read_block(block_no)
            advanced = False

            while block_offset + 8 <= BLOCK_SIZE and cursor < inode.size:
                inode_no = struct.unpack_from("<I", block, block_offset)[0]
                rec_len = struct.unpack_from("<H", block, block_offset + 4)[0]
                name_len = block[block_offset + 6]
                if rec_len < 8:
                    break

                if inode_no != 0 and name_len > 0:
                    name_bytes = block[block_offset + 8:block_offset + 8 + name_len]
                    name = name_bytes.decode("utf-8", errors="surrogateescape")
                    if name not in (".", ".."):
                        yield name, inode_no

                block_offset += rec_len
                cursor += rec_len
                advanced = True

            if not advanced:
                cursor += BLOCK_SIZE

    def lookup_path(self, source_path: str) -> tuple[int, Inode]:
        if not source_path.startswith("/"):
            raise ValueError(f"path must be absolute: {source_path}")

        current_ino = ROOT_INO
        current_inode = self.read_inode(current_ino)
        if source_path == "/":
            return current_ino, current_inode

        for component in [part for part in source_path.split("/") if part]:
            if (current_inode.mode & IFMT) != IFDIR:
                raise NotADirectoryError(f"{component} is not inside a directory")

            next_ino = None
            for name, inode_no in self.iter_dir(current_inode):
                if name == component:
                    next_ino = inode_no
                    break
            if next_ino is None:
                raise FileNotFoundError(f"{source_path} not found in {self.image_path}")
            current_ino = next_ino
            current_inode = self.read_inode(current_ino)

        return current_ino, current_inode


def apply_metadata(dest: Path, inode: Inode, *, follow_symlinks: bool) -> None:
    mode = inode.mode & 0o7777
    os.chmod(dest, mode, follow_symlinks=follow_symlinks)
    os.utime(dest, (inode.atime, inode.mtime), follow_symlinks=follow_symlinks)


def safe_child_path(dest: Path, name: str) -> Path:
    if not name or name in (".", "..") or "/" in name or "\\" in name:
        raise ValueError(f"unsafe directory entry name: {name!r}")
    return dest / name


def extract_inode(image: BlueyFSImage, inode_no: int, dest: Path) -> None:
    inode = image.read_inode(inode_no)
    inode_type = inode.mode & IFMT

    if inode_type == IFDIR:
        dest.mkdir(parents=True, exist_ok=True)
        for name, child_ino in image.iter_dir(inode):
            extract_inode(image, child_ino, safe_child_path(dest, name))
        apply_metadata(dest, inode, follow_symlinks=False)
        return

    if inode_type == IFREG:
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(image.read_file(inode))
        apply_metadata(dest, inode, follow_symlinks=True)
        return

    if inode_type == IFLNK:
        dest.parent.mkdir(parents=True, exist_ok=True)
        target = image.read_file(inode).decode("utf-8", errors="surrogateescape").rstrip("\x00")
        if dest.exists() or dest.is_symlink():
            dest.unlink()
        os.symlink(target, dest)
        return

    raise ValueError(f"unsupported inode type 0x{inode_type:04x} for {dest}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Extract /var/log from a BlueyOS disk image")
    parser.add_argument("--image", default="build/blueyos-disk.img", help="disk image to inspect")
    parser.add_argument("--source-path", default="/var/log", help="absolute BlueyFS path to extract")
    parser.add_argument("--output-dir", default="debug/var-log", help="host directory to populate")
    parser.add_argument("--start-sector", type=int, help="BlueyFS start sector (defaults to MBR root partition)")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    image_path = Path(args.image)
    output_dir = Path(args.output_dir)

    if not image_path.is_file():
        print(f"error: image not found: {image_path}", file=sys.stderr)
        return 1

    if output_dir.exists():
        if not output_dir.is_dir():
            print(f"error: output path exists and is not a directory: {output_dir}", file=sys.stderr)
            return 1
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    image = BlueyFSImage(image_path, args.start_sector)
    try:
        _, source_inode = image.lookup_path(args.source_path)
        if (source_inode.mode & IFMT) != IFDIR:
            print(f"error: source path is not a directory: {args.source_path}", file=sys.stderr)
            return 1

        for name, inode_no in image.iter_dir(source_inode):
            extract_inode(image, inode_no, safe_child_path(output_dir, name))
    except (FileNotFoundError, NotADirectoryError, ValueError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    finally:
        image.close()

    print(f"Extracted {args.source_path} from {image_path} (start sector {image.start_sector}) to {output_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

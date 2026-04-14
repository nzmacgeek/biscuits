#!/usr/bin/env python3
"""Extract a file from the BlueyOS BiscuitFS root partition.

Defaults to dumping /var/log/kernel.log from the standard disk image.
"""

import argparse
import math
import sys
from pathlib import Path


HOST_SECTOR_SIZE = 512
BISCUITFS_MAGIC = 0xB15C0001
BISCUITFS_BLOCK_SIZE = 4096
BISCUITFS_INODE_SIZE = 256
BISCUITFS_INODES_PER_GRP = 8192
BISCUITFS_BLOCKS_PER_GRP = 8192
BISCUITFS_ROOT_INO = 2
BISCUITFS_N_DIRECT = 12
BISCUITFS_IFMT = 0xF000
BISCUITFS_IFREG = 0x8000
BISCUITFS_IFDIR = 0x4000


def read_u16(buf: bytes, offset: int) -> int:
    return int.from_bytes(buf[offset:offset + 2], "little")


def read_u32(buf: bytes, offset: int) -> int:
    return int.from_bytes(buf[offset:offset + 4], "little")


class BlueyFsImage:
    def __init__(self, image_path: Path, start_sector: int) -> None:
        self.image_path = image_path
        self.start_sector = start_sector
        self.fp = image_path.open("rb")
        self.fs_offset = start_sector * HOST_SECTOR_SIZE
        self.super = self._load_superblock()
        self.block_count = read_u32(self.super, 8)
        self.inode_count = read_u32(self.super, 20)
        self.block_size_log = read_u32(self.super, 32)
        if self.block_size_log != 2:
            raise ValueError(f"unsupported BlueyFS block_size_log={self.block_size_log}")
        self.num_groups = math.ceil(self.block_count / BISCUITFS_BLOCKS_PER_GRP)
        self.bgd_table = self._read_exact(self.fs_offset + BISCUITFS_BLOCK_SIZE, self.num_groups * 32)

    def close(self) -> None:
        self.fp.close()

    def _read_exact(self, offset: int, size: int) -> bytes:
        self.fp.seek(offset)
        data = self.fp.read(size)
        if len(data) != size:
            raise ValueError(f"short read at offset {offset} (wanted {size}, got {len(data)})")
        return data

    def _load_superblock(self) -> bytes:
        block0 = self._read_exact(self.fs_offset, BISCUITFS_BLOCK_SIZE)
        superblock = block0[1024:1024 + 512]
        if read_u32(superblock, 0) != BISCUITFS_MAGIC:
            raise ValueError(
                f"BlueyFS magic not found at start sector {self.start_sector} in {self.image_path}"
            )
        return superblock

    def _read_block(self, block_no: int) -> bytes:
        return self._read_exact(self.fs_offset + (block_no * BISCUITFS_BLOCK_SIZE), BISCUITFS_BLOCK_SIZE)

    def _locate_inode(self, inode_no: int) -> tuple[int, int]:
        if inode_no <= 0 or inode_no > self.inode_count:
            raise FileNotFoundError(f"inode {inode_no} is out of range")
        inode_index = inode_no - 1
        group = inode_index // BISCUITFS_INODES_PER_GRP
        local = inode_index % BISCUITFS_INODES_PER_GRP
        bgd = self.bgd_table[group * 32:(group + 1) * 32]
        inode_table_block = read_u32(bgd, 8)
        inodes_per_block = BISCUITFS_BLOCK_SIZE // BISCUITFS_INODE_SIZE
        block_no = inode_table_block + (local // inodes_per_block)
        offset = (local % inodes_per_block) * BISCUITFS_INODE_SIZE
        return block_no, offset

    def read_inode(self, inode_no: int) -> bytes:
        block_no, offset = self._locate_inode(inode_no)
        block = self._read_block(block_no)
        return block[offset:offset + BISCUITFS_INODE_SIZE]

    def inode_mode(self, inode: bytes) -> int:
        return read_u16(inode, 0)

    def inode_size(self, inode: bytes) -> int:
        return read_u32(inode, 4) | (read_u32(inode, 108) << 32)

    def inode_block_ptr(self, inode: bytes, index: int) -> int:
        return read_u32(inode, 40 + (index * 4))

    def inode_get_block(self, inode: bytes, block_index: int) -> int:
        ptrs_per_block = BISCUITFS_BLOCK_SIZE // 4
        if block_index < BISCUITFS_N_DIRECT:
            return self.inode_block_ptr(inode, block_index)

        block_index -= BISCUITFS_N_DIRECT
        if block_index < ptrs_per_block:
            indirect_block = self.inode_block_ptr(inode, 12)
            if indirect_block == 0:
                return 0
            indirect = self._read_block(indirect_block)
            return read_u32(indirect, block_index * 4)

        block_index -= ptrs_per_block
        if block_index < ptrs_per_block * ptrs_per_block:
            level1_block = self.inode_block_ptr(inode, 13)
            if level1_block == 0:
                return 0
            level1 = self._read_block(level1_block)
            level1_index = block_index // ptrs_per_block
            level2_index = block_index % ptrs_per_block
            level2_block = read_u32(level1, level1_index * 4)
            if level2_block == 0:
                return 0
            level2 = self._read_block(level2_block)
            return read_u32(level2, level2_index * 4)

        return 0

    def read_file(self, inode: bytes) -> bytes:
        file_size = self.inode_size(inode)
        data = bytearray()
        offset = 0
        while offset < file_size:
            logical_block = offset // BISCUITFS_BLOCK_SIZE
            block_offset = offset % BISCUITFS_BLOCK_SIZE
            phys_block = self.inode_get_block(inode, logical_block)
            if phys_block == 0:
                break
            block = self._read_block(phys_block)
            chunk = min(BISCUITFS_BLOCK_SIZE - block_offset, file_size - offset)
            data.extend(block[block_offset:block_offset + chunk])
            offset += chunk
        return bytes(data)

    def dir_lookup(self, dir_inode_no: int, name: str) -> int:
        dir_inode = self.read_inode(dir_inode_no)
        if (self.inode_mode(dir_inode) & BISCUITFS_IFMT) != BISCUITFS_IFDIR:
            raise NotADirectoryError(name)

        dir_size = self.inode_size(dir_inode)
        cursor = 0
        target = name.encode("ascii")
        while cursor < dir_size:
            logical_block = cursor // BISCUITFS_BLOCK_SIZE
            block_offset = cursor % BISCUITFS_BLOCK_SIZE
            phys_block = self.inode_get_block(dir_inode, logical_block)
            if phys_block == 0:
                break
            block = self._read_block(phys_block)
            advanced = False
            while block_offset + 8 <= BISCUITFS_BLOCK_SIZE and cursor < dir_size:
                inode_no = read_u32(block, block_offset)
                rec_len = read_u16(block, block_offset + 4)
                name_len = block[block_offset + 6]
                if rec_len < 8:
                    break
                if inode_no != 0 and name_len == len(target):
                    entry_name = block[block_offset + 8:block_offset + 8 + name_len]
                    if entry_name == target:
                        return inode_no
                block_offset += rec_len
                cursor += rec_len
                advanced = True
            if not advanced:
                cursor += BISCUITFS_BLOCK_SIZE
        raise FileNotFoundError(name)

    def lookup_path(self, path: str) -> tuple[int, bytes]:
        if not path.startswith("/"):
            raise ValueError(f"path must be absolute: {path}")
        current_ino = BISCUITFS_ROOT_INO
        current_inode = self.read_inode(current_ino)
        if path == "/":
            return current_ino, current_inode
        for component in [part for part in path.split("/") if part]:
            current_ino = self.dir_lookup(current_ino, component)
            current_inode = self.read_inode(current_ino)
        return current_ino, current_inode


def detect_start_sector(image_path: Path, partition_index: int) -> int:
    with image_path.open("rb") as fp:
        mbr = fp.read(HOST_SECTOR_SIZE)
    if len(mbr) != HOST_SECTOR_SIZE:
        return 0
    if mbr[510:512] != b"\x55\xaa":
        return 0
    if partition_index < 1 or partition_index > 4:
        raise ValueError("partition index must be between 1 and 4")
    entry_offset = 446 + ((partition_index - 1) * 16)
    entry = mbr[entry_offset:entry_offset + 16]
    start_sector = read_u32(entry, 8)
    return start_sector


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Extract BlueyOS kernel logs from a disk image")
    parser.add_argument("image", nargs="?", default="build/blueyos-disk.img",
                        help="disk image to inspect (default: build/blueyos-disk.img)")
    parser.add_argument("--path", default="/var/log/kernel.log",
                        help="absolute path inside BlueyFS to extract")
    parser.add_argument("--partition", type=int, default=2,
                        help="MBR partition number to inspect when --start-sector is not set (default: 2)")
    parser.add_argument("--start-sector", type=int, default=None,
                        help="override the filesystem start sector instead of using the MBR")
    parser.add_argument("--output", default="-",
                        help="output path, or '-' for stdout (default: -)")
    return parser.parse_args()


def write_output(data: bytes, output_path: str) -> None:
    if output_path == "-":
        sys.stdout.buffer.write(data)
        return
    out_path = Path(output_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(data)


def main() -> int:
    args = parse_args()
    image_path = Path(args.image)
    image = None
    if not image_path.exists():
        print(f"ERROR: image not found: {image_path}", file=sys.stderr)
        return 1

    start_sector = args.start_sector
    if start_sector is None:
        start_sector = detect_start_sector(image_path, args.partition)

    try:
        image = BlueyFsImage(image_path, start_sector)
        _, inode = image.lookup_path(args.path)
        if (image.inode_mode(inode) & BISCUITFS_IFMT) != BISCUITFS_IFREG:
            print(f"ERROR: {args.path} is not a regular file", file=sys.stderr)
            return 1
        write_output(image.read_file(inode), args.output)
        if args.output != "-":
            print(f"Extracted {args.path} from {image_path} (start sector {start_sector}) to {args.output}")
        return 0
    except FileNotFoundError:
        print(f"ERROR: {args.path} was not found in {image_path}", file=sys.stderr)
        return 1
    except (NotADirectoryError, ValueError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        try:
            if image is not None:
                image.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
#!/usr/bin/env python3
"""Build a small armv7 root disk image for the kernel's initial ramdisk.

The image is an MBR disk with one partition, the UFS root, which carries
the /etc/zedbsd-root marker.  The kernel takes MBR partition 1 as its boot
partition and, finding no other marked UFS root beside it, mounts that
partition itself as the root.  There is no FAT boot partition: the board
loads the kernel and the ramdisk over JTAG, so nothing reads one, and the
image stays small enough to load that way.

  make-armv7-root-image.py [--ufs-size-mib N] [--dir DEST]...
      [--file DEST=SOURCE]... OUTPUT

--dir makes an empty directory; --file copies a host file to DEST with the
host file's permission bits.  The UFS root is 4 MiB unless --ufs-size-mib
says otherwise (4 MiB is the smallest the format tool makes).
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import importlib.util
import os
import struct
import sys
from pathlib import Path

SECTOR = 512

# The root partition starts on the first MiB, as partitioning tools put it.
# 0xA5 is the type the Raspberry Pi 4 and riscv64 images give their UFS root.
ROOT_LBA = 2048
ROOT_TYPE = 0xA5

# The fields of one MBR partition entry before the start and the length:
# not bootable, and the placeholder CHS addresses LBA-only tools write.
CHS_PLACEHOLDER = b"\xfe\xff\xff"

# What the kernel's root search reads in /etc/zedbsd-root.
ROOT_MARKER = b"zedBSD ufs root v1\n"

TOP = Path(__file__).resolve().parents[3]


def partition_entry(kind: int, lba: int, blocks: int) -> bytes:
    return struct.pack("<B3sB3sII", 0, CHS_PLACEHOLDER, kind,
                       CHS_PLACEHOLDER, lba, blocks)


def directory_of(image, directories: dict[str, int], path: str) -> int:
    """Returns the inode of a directory, making it and its parents."""
    if path in directories:
        return directories[path]
    parent_path, _, name = path.rpartition("/")
    parent = directory_of(image, directories, parent_path)
    inode = image.add_dir(parent, name)
    directories[path] = inode
    return inode


def build_root(size: int, dirs: list[str], files: list[tuple[str, Path]]) -> bytes:
    sys.path.insert(0, str(TOP / "tools/build"))
    from ufs_format import Image  # noqa: E402

    image = Image(size)
    directories = {"": 2}
    for path in dirs:
        directory_of(image, directories, path.strip("/"))
    for destination, source in files:
        parent_path, _, name = destination.strip("/").rpartition("/")
        parent = directory_of(image, directories, parent_path)
        image.add_file(parent, name, source.read_bytes(),
                       source.stat().st_mode & 0o7777)
    etc = directory_of(image, directories, "etc")
    image.add_file(etc, "zedbsd-root", ROOT_MARKER)
    return image.finish()


def check_root(root: bytes) -> None:
    checker = TOP / "tools/build/check-ufs-image.py"
    spec = importlib.util.spec_from_file_location("check_ufs_image", checker)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    filesystem = module.UFS(root)
    filesystem.validate()
    marker = filesystem.read_file(filesystem.lookup("/etc/zedbsd-root"))
    if marker != ROOT_MARKER:
        raise SystemExit("the UFS root has no zedBSD root marker")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--ufs-size-mib", type=int, default=4)
    parser.add_argument("--dir", action="append", default=[])
    parser.add_argument("--file", action="append", default=[])
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    if args.ufs_size_mib < 4:
        raise SystemExit("the UFS root must be at least 4 MiB")
    files = []
    for specification in args.file:
        destination, separator, source = specification.partition("=")
        if not separator or not destination.strip("/") or not Path(source).is_file():
            raise SystemExit(f"invalid --file: {specification}")
        files.append((destination, Path(source)))

    root = build_root(args.ufs_size_mib * 1024 * 1024, args.dir, files)
    check_root(root)

    root_blocks = len(root) // SECTOR
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Written beside the output and moved over it only when complete, so a
    # failed build never leaves an image that looks finished.
    partial = args.output.with_name(args.output.name + ".partial")
    with partial.open("wb") as image:
        image.truncate((ROOT_LBA + root_blocks) * SECTOR)
        mbr = bytearray(SECTOR)
        mbr[0x1B8:0x1BC] = struct.pack("<I", 0x7a656442)
        mbr[0x1BE:0x1CE] = partition_entry(ROOT_TYPE, ROOT_LBA, root_blocks)
        mbr[510:512] = b"\x55\xaa"
        image.write(mbr)
        image.seek(ROOT_LBA * SECTOR)
        image.write(root)
    os.replace(partial, args.output)


if __name__ == "__main__":
    main()

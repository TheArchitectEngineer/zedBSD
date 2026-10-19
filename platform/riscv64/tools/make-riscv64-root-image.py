#!/usr/bin/env python3
"""Build the riscv64 root disk image the kernel reads from its ramdisk.

The image is partitioned like the Raspberry Pi 4's SD card, so the kernel
finds its root the same way: an MBR whose first partition is a small FAT
boot partition and whose second is the UFS root that carries the
/etc/zedbsd-root marker.

  make-riscv64-root-image.py --ufs-image ROOT.ufs OUTPUT
  make-riscv64-root-image.py --ufs-tree ROOTFS [--ufs-size-mib N] OUTPUT
  make-riscv64-root-image.py --bare-root OUTPUT

--ufs-tree makes the UFS root from a staged root tree (64 MiB unless
--ufs-size-mib says otherwise).  --bare-root makes a root that holds
nothing but the marker (a kernel check
without any user program).  --boot-file DEST=SOURCE puts a file on the boot
partition.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import importlib.util
import os
import struct
import subprocess
import sys
from pathlib import Path

SECTOR = 512

# The boot partition: 8 MiB of FAT16 starting at the first MiB.  0x0E is
# FAT16 addressed by LBA.
BOOT_LBA = 2048
BOOT_BLOCKS = 16384
BOOT_TYPE = 0x0E

# The root partition follows on the next MiB.  0xA5 is the type the
# Raspberry Pi 4 image gives its UFS root.
ROOT_LBA = BOOT_LBA + BOOT_BLOCKS
ROOT_TYPE = 0xA5

# The size of a bare root, the smallest UFS the format tool makes.
BARE_ROOT_BYTES = 16 * 1024 * 1024

# The fields of one MBR partition entry before the start and the length:
# not bootable, and the placeholder CHS addresses LBA-only tools write.
CHS_PLACEHOLDER = b"\xfe\xff\xff"


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def partition_entry(kind: int, lba: int, blocks: int) -> bytes:
    return struct.pack("<B3sB3sII", 0, CHS_PLACEHOLDER, kind,
                       CHS_PLACEHOLDER, lba, blocks)


def ufs_create(size: int, tree: Path | None) -> bytes:
    sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools/build"))
    from ufs_format import create  # noqa: E402
    return create(size, tree)


def check_root(root: bytes) -> None:
    checker = Path(__file__).resolve().parents[3] / "tools/build/check-ufs-image.py"
    spec = importlib.util.spec_from_file_location("check_ufs_image", checker)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    filesystem = module.UFS(root)
    filesystem.validate()
    marker = filesystem.read_file(filesystem.lookup("/etc/zedbsd-root"))
    if marker != b"zedBSD ufs root v1\n":
        raise SystemExit("the UFS root has no zedBSD root marker")


def build(args: argparse.Namespace) -> None:
    if args.output.exists() and not args.force:
        raise SystemExit(f"output exists (use --force): {args.output}")
    if args.bare_root:
        root = ufs_create(BARE_ROOT_BYTES, None)
    elif args.ufs_tree is not None:
        if not (args.ufs_tree / "etc" / "zedbsd-root").is_file():
            raise SystemExit(f"root tree has no etc/zedbsd-root: {args.ufs_tree}")
        if args.ufs_size_mib < 16:
            raise SystemExit("the UFS root must be at least 16 MiB")
        root = ufs_create(args.ufs_size_mib * 1024 * 1024, args.ufs_tree)
    else:
        if not args.ufs_image.is_file():
            raise SystemExit(f"missing UFS root image: {args.ufs_image}")
        root = args.ufs_image.read_bytes()
    if len(root) == 0 or len(root) % SECTOR:
        raise SystemExit("the UFS root is not a whole number of sectors")
    files = []
    for specification in args.boot_file:
        destination, separator, source = specification.partition("=")
        if not separator or not destination or not Path(source).is_file():
            raise SystemExit(f"invalid --boot-file: {specification}")
        files.append((destination.lstrip("/"), Path(source)))

    root_blocks = len(root) // SECTOR
    total_blocks = ROOT_LBA + root_blocks
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Written beside the output and moved over it only when complete, so a
    # failed build never leaves an image that looks finished.
    partial = args.output.with_name(args.output.name + ".partial")
    with partial.open("wb") as image:
        image.truncate(total_blocks * SECTOR)
        mbr = bytearray(SECTOR)
        mbr[0x1B8:0x1BC] = struct.pack("<I", 0x7a656442)
        mbr[0x1BE:0x1CE] = partition_entry(BOOT_TYPE, BOOT_LBA, BOOT_BLOCKS)
        mbr[0x1CE:0x1DE] = partition_entry(ROOT_TYPE, ROOT_LBA, root_blocks)
        mbr[510:512] = b"\x55\xaa"
        image.write(mbr)
        image.seek(ROOT_LBA * SECTOR)
        image.write(root)

    spec = f"{partial}@@{BOOT_LBA * SECTOR}"
    run("mformat", "-i", spec, "-T", str(BOOT_BLOCKS), "-H", str(BOOT_LBA),
        "-h", "64", "-s", "32", "-c", "1", "-N", "7a656442", "-v", "ZEDBOOT", "::")
    for destination, source in files:
        run("mcopy", "-i", spec, str(source), f"::/{destination}")

    check_root(root)
    os.replace(partial, args.output)


def main() -> None:
    parser = argparse.ArgumentParser()
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--ufs-image", type=Path)
    source.add_argument("--ufs-tree", type=Path)
    source.add_argument("--bare-root", action="store_true")
    parser.add_argument("--ufs-size-mib", type=int, default=64)
    parser.add_argument("--boot-file", action="append", default=[])
    parser.add_argument("--force", action="store_true")
    parser.add_argument("output", type=Path)
    build(parser.parse_args())


if __name__ == "__main__":
    main()

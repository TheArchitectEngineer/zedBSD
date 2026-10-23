#!/usr/bin/env python3
"""Build the ZYBO's SD card image: a FAT16 boot partition and the UFS root.

The image is an MBR disk with two partitions.  Partition 1 is FAT16, marked
bootable, and carries the files the board's boot ROM and first stage loader
read (given with --boot-file; none is needed when the kernel is loaded some
other way, as in the emulator).  Partition 2 is the UFS root (--root-ufs, a
raw UFS such as make-armv7-root-image.py --raw writes), which the kernel
mounts when the boot parameters name it (rootpart=/dev/mmcblk0p2); the
kernel mounts partition 1 beside it as /boot/boot0.

  make-zybo-sd-image.py --root-ufs ROOT.UFS [--boot-file DEST=SOURCE]...
      [--fat-mib N] [--image-mib N] OUTPUT

The image is --image-mib MiB (512 by default), a power of two, as the
emulator's SD card model requires; partition 1 is --fat-mib MiB (64 by
default).  The order of the partitions is fixed: the boot ROM and the
loader read partition 1.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import os
import struct
import subprocess
from pathlib import Path

SECTOR = 512
MIB_SECTORS = 1024 * 1024 // SECTOR

# The boot partition starts on the first MiB, as partitioning tools put it.
# 0x06 is FAT16 of more than 32 MiB, 0x80 marks it bootable.  The root
# follows it, of the type the other zedBSD images give their UFS root.
BOOT_LBA = 2048
BOOT_TYPE = 0x06
BOOT_ACTIVE = 0x80
ROOT_TYPE = 0xA5

# The placeholder CHS addresses LBA-only tools write.
CHS_PLACEHOLDER = b"\xfe\xff\xff"

# The MBR's disk signature: "zedB", as the other zedBSD images write it.
DISK_SIGNATURE = 0x7a656442

# The FAT's volume label and serial number, fixed so that two builds of
# the same files give the same image.
FAT_LABEL = "ZEDBSD"
FAT_SERIAL = "5a594230"


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def partition_entry(active: int, kind: int, lba: int, blocks: int) -> bytes:
    return struct.pack("<B3sB3sII", active, CHS_PLACEHOLDER, kind,
                       CHS_PLACEHOLDER, lba, blocks)


def copy_into(image, source: Path, lba: int) -> None:
    """Copies a file into the image at a sector, in pieces."""
    image.seek(lba * SECTOR)
    with source.open("rb") as data:
        while True:
            chunk = data.read(1024 * 1024)
            if not chunk:
                break
            image.write(chunk)


def parse_boot_files(specifications: list[str]) -> list[tuple[str, Path]]:
    files = []
    for specification in specifications:
        destination, separator, source = specification.partition("=")
        destination = destination.strip("/")
        if not separator or not destination or not Path(source).is_file():
            raise SystemExit(f"invalid --boot-file: {specification}")
        files.append((destination, Path(source)))
    return files


def build(args: argparse.Namespace) -> None:
    image_mib = args.image_mib
    if image_mib <= 0 or image_mib & (image_mib - 1):
        raise SystemExit("the image size must be a power of two MiB")
    if args.fat_mib < 16:
        raise SystemExit("the boot partition must be at least 16 MiB")
    root_size = args.root_ufs.stat().st_size
    if root_size == 0 or root_size % SECTOR:
        raise SystemExit(f"invalid UFS root size: {root_size}")
    files = parse_boot_files(args.boot_file)

    total = image_mib * MIB_SECTORS
    boot_sectors = args.fat_mib * MIB_SECTORS
    root_lba = BOOT_LBA + boot_sectors
    root_sectors = root_size // SECTOR
    if root_lba + root_sectors > total:
        raise SystemExit("the UFS root does not fit in the image")

    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Written beside the output and moved over it only when complete, so a
    # failed build never leaves an image that looks finished.
    partial = args.output.with_name(args.output.name + ".partial")
    with partial.open("wb") as image:
        image.truncate(total * SECTOR)
        mbr = bytearray(SECTOR)
        mbr[0x1B8:0x1BC] = struct.pack("<I", DISK_SIGNATURE)
        mbr[0x1BE:0x1CE] = partition_entry(BOOT_ACTIVE, BOOT_TYPE, BOOT_LBA,
                                           boot_sectors)
        mbr[0x1CE:0x1DE] = partition_entry(0, ROOT_TYPE, root_lba,
                                           root_sectors)
        mbr[510:512] = b"\x55\xaa"
        image.write(mbr)
        copy_into(image, args.root_ufs, root_lba)

    # The FAT is made in place with mtools; at this size mformat lays out
    # FAT16, which the checker confirms.
    spec = f"{partial}@@{BOOT_LBA * SECTOR}"
    run("mformat", "-i", spec, "-T", str(boot_sectors), "-H", str(BOOT_LBA),
        "-h", "64", "-s", "32", "-N", FAT_SERIAL, "-v", FAT_LABEL, "::")
    directories = []
    for destination, _ in files:
        parts = destination.split("/")[:-1]
        for depth in range(1, len(parts) + 1):
            directory = "/".join(parts[:depth])
            if directory not in directories:
                directories.append(directory)
    if directories:
        run("mmd", "-i", spec, *[f"::/{directory}" for directory in directories])
    for destination, source in files:
        run("mcopy", "-i", spec, str(source), f"::/{destination}")
    os.replace(partial, args.output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root-ufs", type=Path, required=True)
    parser.add_argument("--boot-file", action="append", default=[])
    parser.add_argument("--fat-mib", type=int, default=64)
    parser.add_argument("--image-mib", type=int, default=512)
    parser.add_argument("output", type=Path)
    build(parser.parse_args())


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Build a riscv64 UEFI disk image: a GPT disk with an EFI system partition.

  make-riscv64-disk-image.py --efi BOOTRISCV64.EFI --kernel vmunix \\
      --config zedbsd.cfg [--root-image root.img] OUTPUT
  make-riscv64-disk-image.py --efi BOOTRISCV64.EFI --kernel vmunix \\
      --config zedbsd-native.cfg --root-ufs root.ufs [--scratch-mib N] \\
      [--spl u-boot-spl.bin.normal.out --uboot-itb u-boot.itb] OUTPUT

The EFI system partition (FAT32) holds the loader as the firmware's default
removable-media path, EFI/BOOT/BOOTRISCV64.EFI, with zedbsd.cfg and the
kernel beside it.  With --root-image the root disk image is there too, and
the loader reads it into memory as the kernel's initial ramdisk.

With --root-ufs the disk carries its root itself: a UFS partition labelled
zedBSD-root after the EFI system partition, which the configuration names
with rootpart=PARTLABEL=zedBSD-root; no ramdisk is read.  --scratch-mib
adds an unused partition labelled zedBSD-scratch at the end, for raw read
and write tests of the medium that must not touch a file system.

--spl and --uboot-itb put the VisionFive 2's firmware first on the disk,
for booting from the SD card: the SPL in partition 1, of the type its boot
ROM looks for, and the U-Boot image in partition 2, which the SPL reads.
The EFI system partition is then partition 3 and the root partition 4.

--reserve-firmware keeps those two partitions' sectors, empty and of
zedBSD's reserved type, when the firmware is elsewhere (the VisionFive 2's
SPI flash), so the EFI system partition is still partition 3, where the
vendor U-Boot looks for its boot files.  --esp-file DEST=SRC adds a file
to the EFI system partition (repeatable).
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import uuid
import zlib
from pathlib import Path

# The GPT's layout and the EFI system partition's type; the root partition
# takes the FreeBSD UFS type, as the amd64 native image's root does.
#
# Values referenced: UEFI Specification 2.10 section 5 (the protective MBR,
# the GPT header and partition entry layout, the EFI system partition type
# C12A7328-F81F-11D2-BA4B-00A0C93EC93B).
SECTOR = 512
ENTRY_COUNT = 128
ENTRY_SIZE = 128
ENTRY_SECTORS = ENTRY_COUNT * ENTRY_SIZE // SECTOR
ESP_LBA = 2048
ESP_TYPE = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
ROOT_TYPE = uuid.UUID("516e7cb6-6ecf-11d6-8ff8-00022d09712b")
ROOT_LABEL = "zedBSD-root"
SCRATCH_LABEL = "zedBSD-scratch"

# The VisionFive 2's firmware partitions: the SPL where the boot ROM looks
# for it, and U-Boot in partition 2, where the SPL reads it.
#
# Values referenced: U-Boot v2025.01 doc/board/starfive/visionfive2.rst and
# configs/starfive_visionfive2_defconfig
# (CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_PARTITION): the SPL and U-Boot
# partition types, their sectors 4096 to 8191 and 8192 to 16383, and the
# SPL reading U-Boot from partition 2.
SPL_TYPE = uuid.UUID("2e54b353-1271-4842-806f-e436d6af6985")
SPL_LBA = 4096
SPL_LAST = 8191
UBOOT_TYPE = uuid.UUID("bc13c2ff-59e6-4262-a352-b275fd6f7172")
UBOOT_LBA = 8192
UBOOT_LAST = 16383

# The partitions after the firmware start on the next MiB.
FIRMWARE_END_LBA = 16384

# Fixed identities, so the same inputs give the same disk.
DISK_GUID = uuid.UUID("7a656442-5244-4b76-a172-697363763634")
ESP_GUID = uuid.UUID("7a656442-5244-4b76-a172-697363763645")
ESP_SERIAL = "5a454253"
ROOT_GUID = uuid.UUID("7a656442-5244-4b76-a172-697363763652")
SCRATCH_GUID = uuid.UUID("7a656442-5244-4b76-a172-697363763653")
SPL_GUID = uuid.UUID("7a656442-5244-4b76-a172-697363763631")
UBOOT_GUID = uuid.UUID("7a656442-5244-4b76-a172-697363763632")

# The scratch partition's type: zedBSD's own, which no system mounts.
SCRATCH_TYPE = uuid.UUID("7a656442-5244-4b76-a172-736372617463")

# The reserved firmware partitions' type: zedBSD's own, which no boot ROM
# or system reads.
RESERVED_TYPE = uuid.UUID("7a656442-5244-4b76-a172-726573657276")


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def mib_up(value: int) -> int:
    mib = 1024 * 1024
    return (value + mib - 1) // mib * mib


def entry_bytes(kind: uuid.UUID, unique: uuid.UUID, first: int, last: int,
                name: str) -> bytes:
    label = name.encode("utf-16-le").ljust(72, b"\0")
    return struct.pack("<16s16sQQQ72s", kind.bytes_le, unique.bytes_le,
                       first, last, 0, label)


def header_bytes(current: int, backup: int, first_usable: int, last_usable: int,
                 entries_lba: int, entries_crc: int) -> bytes:
    def pack(crc: int) -> bytes:
        return struct.pack("<8sIIIIQQQQ16sQIII", b"EFI PART", 0x00010000, 92,
                           crc, 0, current, backup, first_usable, last_usable,
                           DISK_GUID.bytes_le, entries_lba, ENTRY_COUNT,
                           ENTRY_SIZE, entries_crc)
    crc = zlib.crc32(pack(0)) & 0xFFFFFFFF
    return pack(crc).ljust(SECTOR, b"\0")


def copy_into(image, source: Path, lba: int) -> None:
    """Copies a file into the image at a sector, in pieces."""
    image.seek(lba * SECTOR)
    with source.open("rb") as data:
        while True:
            chunk = data.read(1024 * 1024)
            if not chunk:
                break
            image.write(chunk)


def build(args: argparse.Namespace) -> None:
    # The loader goes where the firmware looks on removable media.
    #
    # Values referenced: UEFI Specification 2.10 section 3.5.1.1 (the
    # removable media boot path); U-Boot v2025.01
    # lib/efi_loader/efi_helper.c (the RISC-V file name BOOTRISCV64.EFI).
    files = [("EFI/BOOT/BOOTRISCV64.EFI", args.efi), ("zedbsd.cfg", args.config),
             ("vmunix", args.kernel)]
    if args.root_image is not None:
        files.append(("root.img", args.root_image))
    for extra in args.esp_file:
        destination, separator, source = extra.partition("=")
        if not separator or not destination or destination.startswith("/") \
                or ".." in destination.split("/"):
            raise SystemExit(f"bad --esp-file: {extra}")
        files.append((destination, Path(source)))
    for _, path in files:
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")
    if args.root_ufs is not None:
        if args.root_image is not None:
            raise SystemExit("--root-ufs and --root-image name two roots")
        if not args.root_ufs.is_file():
            raise SystemExit(f"missing input: {args.root_ufs}")
        if args.root_ufs.stat().st_size == 0 or args.root_ufs.stat().st_size % SECTOR:
            raise SystemExit("the UFS root is not a whole number of sectors")
    if args.scratch_mib < 0:
        raise SystemExit("--scratch-mib must not be negative")
    if (args.spl is None) != (args.uboot_itb is None):
        raise SystemExit("--spl and --uboot-itb go together")
    if args.reserve_firmware and args.spl is not None:
        raise SystemExit("--reserve-firmware and --spl exclude each other")
    firmware = []
    if args.reserve_firmware:
        firmware = [(RESERVED_TYPE, SPL_GUID, SPL_LBA, SPL_LAST, "reserved", None),
                    (RESERVED_TYPE, UBOOT_GUID, UBOOT_LBA, UBOOT_LAST, "reserved", None)]
    elif args.spl is not None:
        firmware = [(SPL_TYPE, SPL_GUID, SPL_LBA, SPL_LAST, "spl", args.spl),
                    (UBOOT_TYPE, UBOOT_GUID, UBOOT_LBA, UBOOT_LAST, "uboot",
                     args.uboot_itb)]
        for _, _, first, last, _, path in firmware:
            if not path.is_file():
                raise SystemExit(f"missing input: {path}")
            if path.stat().st_size > (last - first + 1) * SECTOR:
                raise SystemExit(f"too large for its partition: {path}")
    if args.output.exists() and not args.force:
        raise SystemExit(f"output exists (use --force): {args.output}")

    # The partition: the files, the FAT's own overhead and room to spare,
    # at least the 64 MiB a FAT32 volume of small clusters needs.
    payload = sum(path.stat().st_size for _, path in files)
    esp_bytes = max(64 * 1024 * 1024, mib_up(payload + payload // 8 + 16 * 1024 * 1024))
    esp_sectors = esp_bytes // SECTOR
    esp_lba = FIRMWARE_END_LBA if firmware else ESP_LBA
    esp_last = esp_lba + esp_sectors - 1

    # The root and the scratch partitions follow, each on a whole MiB.
    partitions = [(kind, unique, first, last, name)
                  for kind, unique, first, last, name, _ in firmware]
    partitions.append((ESP_TYPE, ESP_GUID, esp_lba, esp_last, "EFI system"))
    next_lba = esp_last + 1
    root_lba = None
    if args.root_ufs is not None:
        root_lba = next_lba
        root_sectors = mib_up(args.root_ufs.stat().st_size) // SECTOR
        partitions.append((ROOT_TYPE, ROOT_GUID, root_lba,
                           root_lba + root_sectors - 1, ROOT_LABEL))
        next_lba = root_lba + root_sectors
    if args.scratch_mib > 0:
        scratch_sectors = args.scratch_mib * 1024 * 1024 // SECTOR
        partitions.append((SCRATCH_TYPE, SCRATCH_GUID, next_lba,
                           next_lba + scratch_sectors - 1, SCRATCH_LABEL))
        next_lba += scratch_sectors

    total = next_lba + 2048
    backup_header = total - 1
    backup_entries = backup_header - ENTRY_SECTORS
    first_usable = 2 + ENTRY_SECTORS
    last_usable = backup_entries - 1

    entries = bytearray(ENTRY_COUNT * ENTRY_SIZE)
    for index, (kind, unique, first, last, name) in enumerate(partitions):
        entries[index * ENTRY_SIZE:(index + 1) * ENTRY_SIZE] = \
            entry_bytes(kind, unique, first, last, name)
    entries_crc = zlib.crc32(bytes(entries)) & 0xFFFFFFFF

    args.output.parent.mkdir(parents=True, exist_ok=True)
    partial = args.output.with_name(args.output.name + ".partial")
    with partial.open("wb") as image:
        image.truncate(total * SECTOR)
        mbr = bytearray(SECTOR)
        mbr[0x1BE:0x1CE] = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0xEE,
                                       b"\xff\xff\xff", 1, min(total - 1, 0xFFFFFFFF))
        mbr[510:512] = b"\x55\xaa"
        image.write(mbr)
        image.write(header_bytes(1, backup_header, first_usable, last_usable, 2, entries_crc))
        image.write(entries)
        image.seek(backup_entries * SECTOR)
        image.write(entries)
        image.write(header_bytes(backup_header, 1, first_usable, last_usable,
                                 backup_entries, entries_crc))
        for _, _, first, _, _, path in firmware:
            if path is not None:
                copy_into(image, path, first)
        if root_lba is not None:
            copy_into(image, args.root_ufs, root_lba)

    spec = f"{partial}@@{esp_lba * SECTOR}"
    run("mformat", "-i", spec, "-F", "-T", str(esp_sectors), "-H", str(esp_lba),
        "-h", "64", "-s", "32", "-N", ESP_SERIAL, "-v", "ZEDBSD", "::")
    directories = []
    for destination, _ in files:
        parts = destination.split("/")[:-1]
        for depth in range(1, len(parts) + 1):
            directory = "/".join(parts[:depth])
            if directory not in directories:
                directories.append(directory)
    run("mmd", "-i", spec, *[f"::/{directory}" for directory in directories])
    for destination, source in files:
        run("mcopy", "-i", spec, str(source), f"::/{destination}")
    os.replace(partial, args.output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--efi", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--root-image", type=Path)
    parser.add_argument("--root-ufs", type=Path)
    parser.add_argument("--scratch-mib", type=int, default=0)
    parser.add_argument("--spl", type=Path)
    parser.add_argument("--uboot-itb", type=Path)
    parser.add_argument("--reserve-firmware", action="store_true")
    parser.add_argument("--esp-file", action="append", default=[])
    parser.add_argument("--force", action="store_true")
    parser.add_argument("output", type=Path)
    build(parser.parse_args())


if __name__ == "__main__":
    main()

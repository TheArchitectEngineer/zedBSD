#!/usr/bin/env python3
"""Build the riscv64 UEFI disk image: a GPT disk with one EFI system partition.

  make-riscv64-disk-image.py --efi BOOTRISCV64.EFI --kernel vmunix \\
      --config zedbsd.cfg [--root-image root.img] OUTPUT

The EFI system partition (FAT32) holds the loader as the firmware's default
removable-media path, EFI/BOOT/BOOTRISCV64.EFI, with zedbsd.cfg, the kernel
and the root disk image beside it; the loader reads the root image into
memory as the kernel's initial ramdisk.  The disk has no root partition of
its own yet: the kernel reads no disk on these boards so far.

Values referenced: UEFI Specification 2.10 section 5 (the protective MBR,
the GPT header and partition entry layout, the EFI system partition type
C12A7328-F81F-11D2-BA4B-00A0C93EC93B) and section 3.5.1.1 (the removable
media boot path); the RISC-V file name BOOTRISCV64.EFI is the one U-Boot
v2025.01 lib/efi_loader/efi_helper.c looks for.
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

SECTOR = 512
ENTRY_COUNT = 128
ENTRY_SIZE = 128
ENTRY_SECTORS = ENTRY_COUNT * ENTRY_SIZE // SECTOR
ESP_LBA = 2048
ESP_TYPE = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")

# Fixed identities, so the same inputs give the same disk.
DISK_GUID = uuid.UUID("7a656442-5244-4b76-a172-697363763634")
ESP_GUID = uuid.UUID("7a656442-5244-4b76-a172-697363763645")
ESP_SERIAL = "5a454253"


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


def build(args: argparse.Namespace) -> None:
    files = [("EFI/BOOT/BOOTRISCV64.EFI", args.efi), ("zedbsd.cfg", args.config),
             ("vmunix", args.kernel)]
    if args.root_image is not None:
        files.append(("root.img", args.root_image))
    for _, path in files:
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")
    if args.output.exists() and not args.force:
        raise SystemExit(f"output exists (use --force): {args.output}")

    # The partition: the files, the FAT's own overhead and room to spare,
    # at least the 64 MiB a FAT32 volume of small clusters needs.
    payload = sum(path.stat().st_size for _, path in files)
    esp_bytes = max(64 * 1024 * 1024, mib_up(payload + payload // 8 + 16 * 1024 * 1024))
    esp_sectors = esp_bytes // SECTOR
    esp_last = ESP_LBA + esp_sectors - 1
    total = esp_last + 1 + 2048
    backup_header = total - 1
    backup_entries = backup_header - ENTRY_SECTORS
    first_usable = 2 + ENTRY_SECTORS
    last_usable = backup_entries - 1

    entries = bytearray(ENTRY_COUNT * ENTRY_SIZE)
    entries[0:ENTRY_SIZE] = entry_bytes(ESP_TYPE, ESP_GUID, ESP_LBA, esp_last, "EFI system")
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

    spec = f"{partial}@@{ESP_LBA * SECTOR}"
    run("mformat", "-i", spec, "-F", "-T", str(esp_sectors), "-H", str(ESP_LBA),
        "-h", "64", "-s", "32", "-N", ESP_SERIAL, "-v", "ZEDBSD", "::")
    run("mmd", "-i", spec, "::/EFI", "::/EFI/BOOT")
    for destination, source in files:
        run("mcopy", "-i", spec, str(source), f"::/{destination}")
    os.replace(partial, args.output)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--efi", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--root-image", type=Path)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("output", type=Path)
    build(parser.parse_args())


if __name__ == "__main__":
    main()

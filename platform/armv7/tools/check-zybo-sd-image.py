#!/usr/bin/env python3
"""Check the ZYBO's SD card image that make-zybo-sd-image.py writes.

  check-zybo-sd-image.py --root-ufs ROOT.UFS [--boot-file DEST=SOURCE]...
      [--fat-mib N] IMAGE

It checks that the image is a power of two in size, that its MBR holds the
bootable FAT16 boot partition at the first MiB and the UFS root right after
it, that the boot partition is FAT16 and carries every --boot-file with the
same contents, and that partition 2 is the UFS root given, a valid UFS with
the zedBSD root marker.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import struct
import subprocess
import tempfile
from pathlib import Path

SECTOR = 512
MIB_SECTORS = 1024 * 1024 // SECTOR
BOOT_LBA = 2048
BOOT_TYPE = 0x06
BOOT_ACTIVE = 0x80
ROOT_TYPE = 0xA5
ROOT_MARKER = b"zedBSD ufs root v1\n"

TOP = Path(__file__).resolve().parents[3]


def fail(message: str) -> None:
    raise SystemExit("ZYBO SD image check: " + message)


def digest_file(path: Path) -> bytes:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        while True:
            chunk = stream.read(1024 * 1024)
            if not chunk:
                break
            hasher.update(chunk)
    return hasher.digest()


def extract(image: Path, name: str) -> bytes:
    with tempfile.TemporaryDirectory(prefix="zedbsd-zybo-sd-check-") as work:
        output = Path(work) / "payload"
        subprocess.run(["mcopy", "-n", "-i", f"{image}@@{BOOT_LBA * SECTOR}",
                        f"::{name}", str(output)], check=True,
                       stdout=subprocess.DEVNULL)
        return output.read_bytes()


def check_root(root: bytes) -> None:
    checker = TOP / "tools/build/check-ufs-image.py"
    spec = importlib.util.spec_from_file_location("check_ufs_image", checker)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    filesystem = module.UFS(root)
    filesystem.validate()
    marker = filesystem.read_file(filesystem.lookup("/etc/zedbsd-root"))
    if marker != ROOT_MARKER:
        fail("the UFS root has no zedBSD root marker")


def check(args: argparse.Namespace) -> None:
    size = args.image.stat().st_size
    if size == 0 or size % (MIB_SECTORS * SECTOR) or size & (size - 1):
        fail(f"the image size {size} is not a power of two MiB")
    root_size = args.root_ufs.stat().st_size
    boot_sectors = args.fat_mib * MIB_SECTORS
    root_lba = BOOT_LBA + boot_sectors

    with args.image.open("rb") as stream:
        mbr = stream.read(SECTOR)
        if mbr[510:512] != b"\x55\xaa":
            fail("missing MBR signature")
        boot = struct.unpack_from("<B3sB3sII", mbr, 0x1BE)
        root = struct.unpack_from("<B3sB3sII", mbr, 0x1CE)
        if (boot[0], boot[2], boot[4], boot[5]) != \
                (BOOT_ACTIVE, BOOT_TYPE, BOOT_LBA, boot_sectors):
            fail(f"partition 1 is not the bootable FAT16 at {BOOT_LBA}: {boot}")
        if (root[0], root[2], root[4], root[5]) != \
                (0, ROOT_TYPE, root_lba, root_size // SECTOR):
            fail(f"partition 2 is not the UFS root at {root_lba}: {root}")
        for offset in (0x1DE, 0x1EE):
            if any(mbr[offset:offset + 16]):
                fail("an MBR entry past partition 2 is in use")
        if root_lba * SECTOR + root_size > size:
            fail("partition 2 runs past the end of the image")

        stream.seek(BOOT_LBA * SECTOR)
        fat = stream.read(SECTOR)
        if fat[510:512] != b"\x55\xaa" or fat[0x36:0x3E] != b"FAT16   ":
            fail("partition 1 is not FAT16")
        fat_sectors = struct.unpack_from("<H", fat, 0x13)[0]
        if fat_sectors == 0:
            fat_sectors = struct.unpack_from("<I", fat, 0x20)[0]
        if fat_sectors != boot_sectors:
            fail(f"the FAT spans {fat_sectors} sectors, not {boot_sectors}")

        stream.seek(root_lba * SECTOR)
        root_bytes = stream.read(root_size)
    if hashlib.sha256(root_bytes).digest() != digest_file(args.root_ufs):
        fail("partition 2 differs from the UFS root")
    check_root(root_bytes)

    for specification in args.boot_file:
        destination, separator, source = specification.partition("=")
        if not separator:
            fail(f"invalid --boot-file: {specification}")
        payload = extract(args.image, "/" + destination.strip("/"))
        if hashlib.sha256(payload).digest() != digest_file(Path(source)):
            fail(f"/{destination.strip('/')} differs from {source}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root-ufs", type=Path, required=True)
    parser.add_argument("--boot-file", action="append", default=[])
    parser.add_argument("--fat-mib", type=int, default=64)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    check(args)
    print(f"ZYBO SD image check: PASS {args.image}")


if __name__ == "__main__":
    main()

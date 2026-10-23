#!/usr/bin/env python3
"""Check the ZYBO's SD card image that make-zybo-sd-image.py writes.

  check-zybo-sd-image.py --root-ufs ROOT.UFS [--boot-file DEST=SOURCE]...
      [--fat-mib N] [--kernel VMUNIX.BIN [--bootarg PARAMETER]...] IMAGE

It checks that the image is a power of two in size, that its MBR holds the
bootable FAT32 boot partition at the first MiB and the UFS root right after
it, that the boot partition is FAT32 and carries every --boot-file with the
same contents, and that partition 2 is the UFS root given, a valid UFS with
the zedBSD root marker.

With --kernel it also checks the files of the SD boot on the boot
partition, as U-Boot SPL reads them (make-zybo-boot-files.py): vmunix.itb
is a FIT whose one kernel is VMUNIX.BIN, loaded and entered at 0x00100000,
and system.dtb gives the board's memory and a command line that names the
root (rootpart=/dev/mmcblk0p2) and every --bootarg.  A BOOT.BIN among the
--boot-file files must carry the boot ROM's image header.
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
BOOT_TYPE = 0x0C
# The fewest clusters a FAT32 has: a reader takes a volume of fewer for FAT16.
FAT32_MINIMUM_CLUSTERS = 65525
BOOT_ACTIVE = 0x80
ROOT_TYPE = 0xA5
ROOT_MARKER = b"zedBSD ufs root v1\n"

# The boot ROM's image header (U-Boot's tools/zynqimage.c describes it):
# the width detection word and the identifier, then the words up to the
# checksum, which is the complement of their sum.
BOOT_ROM_WIDTH_OFFSET = 0x20
BOOT_ROM_WIDTH_WORD = 0xAA995566
BOOT_ROM_IDENTIFIER = 0x584C4E58
BOOT_ROM_CHECKSUM_OFFSET = 0x48
BOOT_ROM_SOURCE_OFFSET = 0x30
BOOT_ROM_OCM_BYTES = 0x30000

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


def load_boot_files_tool():
    """Loads make-zybo-boot-files.py, whose checks the card's copies take."""
    tool = Path(__file__).resolve().parent / "make-zybo-boot-files.py"
    spec = importlib.util.spec_from_file_location("make_zybo_boot_files", tool)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def check_boot_rom_image(data: bytes) -> None:
    """Checks that BOOT.BIN starts with the header the boot ROM reads."""
    if len(data) < BOOT_ROM_CHECKSUM_OFFSET + 4:
        fail("BOOT.BIN is too short for a boot ROM image")
    width, identifier = struct.unpack_from("<II", data, BOOT_ROM_WIDTH_OFFSET)
    if width != BOOT_ROM_WIDTH_WORD or identifier != BOOT_ROM_IDENTIFIER:
        fail("BOOT.BIN has no boot ROM image header")
    words = struct.unpack_from("<10I", data, BOOT_ROM_WIDTH_OFFSET)
    (checksum,) = struct.unpack_from("<I", data, BOOT_ROM_CHECKSUM_OFFSET)
    if checksum != (~sum(words)) & 0xFFFFFFFF:
        fail("BOOT.BIN's header checksum is wrong")
    # The image follows the header (its offset and length are the two words
    # at 0x30; U-Boot's mkimage gives as the length that of the whole file).
    # What the boot ROM copies to the on-chip memory and runs must fit its
    # 192 KiB.
    source, length = struct.unpack_from("<II", data, BOOT_ROM_SOURCE_OFFSET)
    if source < BOOT_ROM_CHECKSUM_OFFSET + 4 or source >= len(data):
        fail("BOOT.BIN's image does not start within the file")
    if length == 0 or length > len(data):
        fail("BOOT.BIN's image length is not within the file")
    if len(data) - source > BOOT_ROM_OCM_BYTES:
        fail("BOOT.BIN's image is larger than the on-chip memory")


def check_sd_boot(args: argparse.Namespace) -> None:
    """Checks vmunix.itb, system.dtb and BOOT.BIN on the boot partition."""
    tool = load_boot_files_tool()
    kernel = args.kernel.read_bytes()
    try:
        tool.check_fit(extract(args.image, "/vmunix.itb"), kernel)
        tool.check_dtb(extract(args.image, "/system.dtb"),
                       ["rootpart=/dev/mmcblk0p2"] + args.bootarg)
    except tool.BootFileError as error:
        fail(str(error))
    for specification in args.boot_file:
        destination = specification.partition("=")[0].strip("/")
        if destination.upper() == "BOOT.BIN":
            check_boot_rom_image(extract(args.image, "/BOOT.BIN"))


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
            fail(f"partition 1 is not the bootable FAT32 at {BOOT_LBA}: {boot}")
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
        if fat[510:512] != b"\x55\xaa" or fat[0x52:0x5A] != b"FAT32   ":
            fail("partition 1 is not FAT32")
        fat_sectors = struct.unpack_from("<H", fat, 0x13)[0]
        if fat_sectors == 0:
            fat_sectors = struct.unpack_from("<I", fat, 0x20)[0]
        if fat_sectors != boot_sectors:
            fail(f"the FAT spans {fat_sectors} sectors, not {boot_sectors}")

        # A FAT32 has no fixed root and no 16-bit table size, and enough
        # clusters that the kernel's driver, U-Boot and the boot ROM, which
        # tell the width by the count, all take it for FAT32.
        sector_bytes = struct.unpack_from("<H", fat, 0x0B)[0]
        cluster_sectors = fat[0x0D]
        reserved = struct.unpack_from("<H", fat, 0x0E)[0]
        tables = fat[0x10]
        root_entries = struct.unpack_from("<H", fat, 0x11)[0]
        table_sectors16 = struct.unpack_from("<H", fat, 0x16)[0]
        table_sectors = struct.unpack_from("<I", fat, 0x24)[0]
        if sector_bytes != SECTOR or cluster_sectors == 0 or tables == 0:
            fail("partition 1 has a malformed FAT32 boot sector")
        if root_entries != 0 or table_sectors16 != 0 or table_sectors == 0:
            fail("partition 1 does not have the FAT32 layout")
        clusters = (fat_sectors - reserved - tables * table_sectors) // cluster_sectors
        if clusters < FAT32_MINIMUM_CLUSTERS:
            fail(f"partition 1 has {clusters} clusters, too few for FAT32")

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

    if args.kernel is not None:
        check_sd_boot(args)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root-ufs", type=Path, required=True)
    parser.add_argument("--boot-file", action="append", default=[])
    parser.add_argument("--fat-mib", type=int, default=64)
    parser.add_argument("--kernel", type=Path)
    parser.add_argument("--bootarg", action="append", default=[])
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    if args.bootarg and args.kernel is None:
        parser.error("--bootarg needs --kernel")
    check(args)
    print(f"ZYBO SD image check: PASS {args.image}")


if __name__ == "__main__":
    main()

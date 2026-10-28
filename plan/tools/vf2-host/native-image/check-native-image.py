#!/usr/bin/env python3
"""Check the riscv64 native disk image the build makes (ws256-p015).

  check-native-image.py REPOSITORY OUTDIR

Builds a UFS root alone (make-riscv64-root-image.py --raw) from a small
tree, then the native disk image (make-riscv64-disk-image.py --root-ufs)
twice: without firmware, and with stand-in SPL and U-Boot files.  Checks
the GPT entries (types, labels, places), the EFI system partition's files
and serial, the root partition's UFS and marker, the scratch partition's
size, and the firmware's bytes in partitions 1 and 2.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import importlib.util
import struct
import subprocess
import sys
import uuid
import zlib
from pathlib import Path

SECTOR = 512
ESP = uuid.UUID("c12a7328-f81f-11d2-ba4b-00a0c93ec93b")
UFS = uuid.UUID("516e7cb6-6ecf-11d6-8ff8-00022d09712b")
SPL = uuid.UUID("2e54b353-1271-4842-806f-e436d6af6985")
UBOOT = uuid.UUID("bc13c2ff-59e6-4262-a352-b275fd6f7172")
SCRATCH = uuid.UUID("7a656442-5244-4b76-a172-736372617463")

failures = 0
passes = 0


def check(condition: bool, what: str) -> None:
    global failures, passes
    if condition:
        passes += 1
        print(f"PASS {what}")
    else:
        failures += 1
        print(f"FAIL {what}")


def entries(image: Path) -> list[tuple[uuid.UUID, int, int, str]]:
    data = image.read_bytes()[:SECTOR * 34]
    header = data[SECTOR:2 * SECTOR]
    check(header[:8] == b"EFI PART", f"{image.name}: GPT header")
    size = struct.unpack_from("<I", header, 12)[0]
    stored = struct.unpack_from("<I", header, 16)[0]
    blank = header[:16] + b"\0\0\0\0" + header[20:size]
    check(zlib.crc32(blank) & 0xFFFFFFFF == stored, f"{image.name}: header CRC")
    count, length, crc = struct.unpack_from("<III", header, 80)
    array = data[2 * SECTOR:2 * SECTOR + count * length]
    check(zlib.crc32(array) & 0xFFFFFFFF == crc, f"{image.name}: entry CRC")
    found = []
    for index in range(count):
        entry = array[index * length:(index + 1) * length]
        kind = uuid.UUID(bytes_le=entry[:16])
        if kind.int == 0:
            continue
        first, last = struct.unpack_from("<QQ", entry, 32)
        name = entry[56:128].decode("utf-16-le").rstrip("\0")
        found.append((kind, first, last, name))
    return found


def esp_listing(image: Path, lba: int) -> str:
    spec = f"{image}@@{lba * SECTOR}"
    listing = subprocess.run(["mdir", "-i", spec, "-/", "-b", "::/"], check=True,
                             capture_output=True, text=True).stdout
    info = subprocess.run(["minfo", "-i", spec, "::"], check=True,
                          capture_output=True, text=True).stdout
    return listing + info


def main() -> None:
    repository = Path(sys.argv[1])
    out = Path(sys.argv[2])
    tools = repository / "platform/riscv64/tools"

    # The stand-in inputs.
    tree = out / "tree"
    (tree / "etc").mkdir(parents=True)
    (tree / "etc/zedbsd-root").write_bytes(b"zedBSD ufs root v1\n")
    (tree / "etc/hello").write_bytes(b"hello\n")
    efi = out / "BOOTRISCV64.EFI"
    efi.write_bytes(b"MZ" + bytes(4094))
    kernel = out / "vmunix"
    kernel.write_bytes(b"\x7fELF" + bytes(65532))
    config = repository / "platform/riscv64/zedbsd-native.cfg"
    spl = out / "spl.bin"
    spl.write_bytes(bytes(range(256)) * 600)
    itb = out / "u-boot.itb"
    itb.write_bytes(bytes(range(255, -1, -1)) * 3000)

    # The UFS root alone.
    ufs = out / "root.ufs"
    subprocess.run([sys.executable, "-I", str(tools / "make-riscv64-root-image.py"),
                    "--raw", "--ufs-tree", str(tree), "--ufs-size-mib", "16",
                    str(ufs)], check=True)
    check(ufs.stat().st_size == 16 * 1024 * 1024, "root.ufs is 16 MiB")

    checker = repository / "tools/build/check-ufs-image.py"
    spec = importlib.util.spec_from_file_location("check_ufs_image", checker)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    for name, firmware in (("plain", []),
                           ("vf2", ["--spl", str(spl), "--uboot-itb", str(itb)])):
        image = out / f"{name}.img"
        subprocess.run([sys.executable, "-I", str(tools / "make-riscv64-disk-image.py"),
                        "--efi", str(efi), "--kernel", str(kernel),
                        "--config", str(config), "--root-ufs", str(ufs),
                        "--scratch-mib", "4", *firmware, str(image)], check=True)
        table = entries(image)
        expected = ([SPL, UBOOT] if firmware else []) + [ESP, UFS, SCRATCH]
        check([kind for kind, _, _, _ in table] == expected, f"{name}: partition types")
        esp_index = 2 if firmware else 0
        _, esp_first, esp_last, _ = table[esp_index]
        _, root_first, root_last, root_name = table[esp_index + 1]
        _, scratch_first, scratch_last, scratch_name = table[esp_index + 2]
        check(esp_first == (16384 if firmware else 2048), f"{name}: ESP start {esp_first}")
        check(root_name == "zedBSD-root" and root_first == esp_last + 1,
              f"{name}: root partition label and start")
        check((root_last - root_first + 1) * SECTOR == ufs.stat().st_size,
              f"{name}: root partition size")
        check(scratch_name == "zedBSD-scratch" and scratch_first == root_last + 1 and
              (scratch_last - scratch_first + 1) * SECTOR == 4 * 1024 * 1024,
              f"{name}: scratch partition")
        data = image.read_bytes()
        root = data[root_first * SECTOR:(root_last + 1) * SECTOR]
        check(root == ufs.read_bytes(), f"{name}: root partition holds root.ufs")
        filesystem = module.UFS(root)
        filesystem.validate()
        check(filesystem.read_file(filesystem.lookup("/etc/zedbsd-root")) ==
              b"zedBSD ufs root v1\n", f"{name}: root marker")
        check(filesystem.read_file(filesystem.lookup("/etc/hello")) == b"hello\n",
              f"{name}: root file")
        listing = esp_listing(image, esp_first)
        for wanted in ("EFI/BOOT/BOOTRISCV64.EFI", "zedbsd.cfg", "vmunix"):
            check(wanted in listing, f"{name}: ESP has {wanted}")
        check("root.img" not in listing, f"{name}: ESP has no root.img")
        check("5A454253" in listing.upper().replace("-", ""), f"{name}: ESP serial 5A45-4253")
        if firmware:
            _, spl_first, spl_last, spl_name = table[0]
            _, itb_first, itb_last, itb_name = table[1]
            check((spl_first, spl_last, spl_name) == (4096, 8191, "spl"), f"{name}: SPL partition")
            check((itb_first, itb_last, itb_name) == (8192, 16383, "uboot"), f"{name}: U-Boot partition")
            check(data[spl_first * SECTOR:spl_first * SECTOR + spl.stat().st_size] ==
                  spl.read_bytes(), f"{name}: SPL bytes")
            check(data[itb_first * SECTOR:itb_first * SECTOR + itb.stat().st_size] ==
                  itb.read_bytes(), f"{name}: U-Boot bytes")

    # Refusals: two roots, firmware without its pair, a firmware file too large.
    def refused(extra: list[str], what: str) -> None:
        result = subprocess.run([sys.executable, "-I", str(tools / "make-riscv64-disk-image.py"),
                                 "--efi", str(efi), "--kernel", str(kernel),
                                 "--config", str(config), "--root-ufs", str(ufs),
                                 *extra, str(out / "refused.img")],
                                capture_output=True, text=True)
        check(result.returncode != 0 and not (out / "refused.img").exists(), f"refuses {what}")
    refused(["--root-image", str(ufs)], "--root-ufs with --root-image")
    refused(["--spl", str(spl)], "--spl without --uboot-itb")
    big = out / "big.itb"
    big.write_bytes(bytes(4 * 1024 * 1024 + 1))
    refused(["--spl", str(spl), "--uboot-itb", str(big)], "an oversized U-Boot image")

    print(f"PASS {passes} FAIL {failures}")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()

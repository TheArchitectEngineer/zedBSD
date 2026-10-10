#!/usr/bin/env python3
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Compile the actual VFS publication function against the focused mount model."""

from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[3]
    output = root / "build/ws193-boot0-host"
    output.mkdir(parents=True, exist_ok=True)
    source = (root / "src/kern/vfs.c").read_text()
    start = source.index("static void\nvfs_publish_boot_filesystems(")
    end = source.index("/* Marks the slot of a bootN:", start)
    (output / "boot0-publication-under-test.inc").write_text(source[start:end])
    cases = [
        ("rpi4", ["-DHAL_ARCH_ARM64", "-DHAL_BOARD_RPI4"]),
        ("amd64", []),
    ]
    for name, flags in cases:
        binary = output / name
        subprocess.run(
            [
                "cc", "-std=c90", "-Wall", "-Wextra", "-Werror", *flags,
                "-I" + str(output),
                str(root / "plan/ws193/tests/boot0-publication-host.c"),
                "-o", str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()

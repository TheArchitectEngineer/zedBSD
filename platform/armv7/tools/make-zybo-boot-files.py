#!/usr/bin/env python3
"""Make the files U-Boot SPL boots the ZYBO's kernel from on the SD card.

On the SD boot the board's boot ROM runs U-Boot SPL from the card's
BOOT.BIN, and SPL, in Falcon mode, reads two more files from the card's
first partition (FAT) and enters the kernel without U-Boot proper:

  system.dtb   the device tree, read whole to 0x01F00000 and given to the
               kernel in r2, as the JTAG boot gives its own;
  vmunix.itb   a FIT holding the kernel's raw image (vmunix.bin) and
               nothing else, which SPL copies to its load address,
               0x00100000, and enters there.

  make-zybo-boot-files.py fit --kernel VMUNIX.BIN --elf VMUNIX OUTPUT
  make-zybo-boot-files.py dtb --dts DTS [--bootargs TEXT] OUTPUT

fit checks against the kernel's ELF that the raw image is the ELF's
loaded bytes from 0x00100000, entered at its first byte, and that the
kernel, its BSS included, and the device tree SPL appends after the raw
image stay below the DTB at 0x01F00000.  dtb compiles the device tree
source and adds TEXT after the command line it names.  Both write the
file with dtc and check it; check-zybo-sd-image.py runs the same checks
on the copies on the card.

The FIT is the public format of U-Boot's doc/usage/fit/ (the names of
its nodes and properties).  Its kernel says os = "linux": SPL enters such
a kernel with r0 = 0, r1 = 0xffffffff and r2 = the DTB, which is the
entry the zedBSD kernel takes; for any other kind it would start U-Boot.
A legacy uImage is not used because SPL reads its 64-byte header to just
below the load address, 0x000FFFC0, the alias of the high on-chip memory
where the second processor waits.
"""
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

from __future__ import annotations

import argparse
import os
import struct
import subprocess
import tempfile
from pathlib import Path

# Where SPL puts the kernel and the DTB, the same addresses as the JTAG
# boot (plan/tools/zybo/jtag-boot.sh) and the emulator's script.
KERNEL_LOAD = 0x00100000
DTB_ADDRESS = 0x01F00000

# SPL copies its own device tree (some 20 KiB, grown by 8 KiB) to just past
# the raw image, into the kernel's BSS, which the kernel clears; this much
# room past the raw image is kept below the DTB for it.
APPENDED_FDT_ROOM = 64 * 1024

# SPL reads the whole FIT into its 12 MiB malloc pool (spl-build.sh) before
# it copies the kernel out; this leaves the pool's last 4 MiB to the MMC
# and FAT drivers.
FIT_LIMIT = 8 * 1024 * 1024

# The DTB is read whole to 0x01F00000; a tree of this size is far beyond
# any the board needs.
DTB_LIMIT = 64 * 1024

FIT_DESCRIPTION = "zedBSD kernel for the ZYBO"
FIT_CONFIGURATION = "zybo"

FDT_MAGIC = 0xD00DFEED
FDT_BEGIN_NODE = 1
FDT_END_NODE = 2
FDT_PROP = 3
FDT_NOP = 4
FDT_END = 9

ELF_PT_LOAD = 1


class BootFileError(Exception):
    """A boot file that SPL could not boot, or would boot wrongly."""


class Node:
    """A node of a flattened device tree: its properties and children."""

    def __init__(self, name: str) -> None:
        self.name = name
        self.properties: dict[str, bytes] = {}
        self.children: dict[str, Node] = {}

    def find(self, path: str) -> Node | None:
        """Finds the node at a path below this one, such as images/kernel."""
        node = self
        for part in path.strip("/").split("/"):
            if not part:
                continue
            node = node.children.get(part)
            if node is None:
                return None
        return node

    def string(self, name: str) -> str | None:
        """Reads a property that holds one string."""
        value = self.properties.get(name)
        if value is None or not value.endswith(b"\0"):
            return None
        return value[:-1].decode("utf-8", "replace")

    def cells(self, name: str) -> tuple[int, ...] | None:
        """Reads a property that holds 32-bit cells."""
        value = self.properties.get(name)
        if value is None or len(value) % 4:
            return None
        return struct.unpack(f">{len(value) // 4}I", value)


def read_fdt(data: bytes) -> Node:
    """Parses a flattened device tree into its root node."""
    if len(data) < 40:
        raise BootFileError("too short for a device tree")
    magic, total, structure, strings = struct.unpack_from(">4I", data, 0)
    if magic != FDT_MAGIC:
        raise BootFileError("not a device tree (bad magic)")
    if total > len(data):
        raise BootFileError("the device tree runs past the end of the file")

    # Walks the structure block's tokens; each node starts a level and its
    # end closes it, and a property belongs to the innermost open node.
    def name_at(offset: int) -> str:
        end = data.index(b"\0", offset)
        return data[offset:end].decode("utf-8", "replace")

    stack: list[Node] = []
    root = None
    offset = structure
    while True:
        (token,) = struct.unpack_from(">I", data, offset)
        offset += 4
        if token == FDT_BEGIN_NODE:
            name = name_at(offset)
            offset += (len(name.encode()) + 4) & ~3
            node = Node(name)
            if stack:
                stack[-1].children[name] = node
            else:
                root = node
            stack.append(node)
        elif token == FDT_END_NODE:
            if not stack:
                raise BootFileError("the device tree closes a node it never opened")
            stack.pop()
        elif token == FDT_PROP:
            length, name_offset = struct.unpack_from(">2I", data, offset)
            offset += 8
            if not stack:
                raise BootFileError("the device tree has a property outside a node")
            name = name_at(strings + name_offset)
            stack[-1].properties[name] = data[offset:offset + length]
            offset += (length + 3) & ~3
        elif token == FDT_NOP:
            continue
        elif token == FDT_END:
            break
        else:
            raise BootFileError(f"the device tree has an unknown token {token}")
    if root is None or stack:
        raise BootFileError("the device tree's nodes are not closed")
    return root


def read_elf_layout(elf: bytes) -> tuple[int, int, int, int]:
    """Reads a 32-bit ARM ELF's physical layout.

    Returns the lowest physical address of its loaded segments, the end of
    their file contents, the end of their memory (the BSS included) and the
    physical address of the entry point.
    """
    if elf[:4] != b"\x7fELF" or elf[4] != 1 or elf[5] != 1:
        raise BootFileError("the kernel's ELF is not a 32-bit little-endian ELF")
    entry, program_offset = struct.unpack_from("<II", elf, 24)
    program_size, program_count = struct.unpack_from("<HH", elf, 42)
    low = None
    file_end = 0
    memory_end = 0
    entry_physical = None

    # Each loaded segment widens the span.  The kernel's link gives its entry
    # point as the physical address it starts at; an entry point inside a
    # segment's virtual span instead is moved to its physical address.
    for index in range(program_count):
        header = struct.unpack_from("<8I", elf, program_offset + index * program_size)
        kind, _, virtual, physical, file_size, memory_size = header[:6]
        if kind != ELF_PT_LOAD or memory_size == 0:
            continue
        if low is None or physical < low:
            low = physical
        file_end = max(file_end, physical + file_size)
        memory_end = max(memory_end, physical + memory_size)
        if physical <= entry < physical + memory_size:
            entry_physical = entry
        elif entry_physical is None and virtual <= entry < virtual + memory_size:
            entry_physical = entry - virtual + physical
    if low is None:
        raise BootFileError("the kernel's ELF has no loaded segment")
    if entry_physical is None:
        raise BootFileError(f"the kernel's entry point {entry:#x} is in no loaded segment")
    return low, file_end, memory_end, entry_physical


def check_kernel_layout(kernel: bytes, elf: bytes) -> None:
    """Checks that SPL can put this kernel at 0x00100000 and enter it."""
    low, file_end, memory_end, entry = read_elf_layout(elf)

    # The raw image starts at the load address and is entered at its first
    # byte, as SPL enters it.
    if low != KERNEL_LOAD:
        raise BootFileError(f"the kernel is linked at {low:#010x}, not {KERNEL_LOAD:#010x}")
    if entry != KERNEL_LOAD:
        raise BootFileError(f"the kernel's entry is {entry:#010x}, not its first byte {KERNEL_LOAD:#010x}")
    if len(kernel) != file_end - low:
        raise BootFileError(f"the raw image is {len(kernel)} bytes, the ELF's loaded bytes {file_end - low}")

    # SPL reads the DTB first and the kernel after it, so a kernel that
    # reaches the DTB overwrites it; the kernel's BSS and the tree SPL
    # appends past the raw image must stay below it.
    if memory_end > DTB_ADDRESS:
        raise BootFileError(f"the kernel ends at {memory_end:#010x}, past the DTB at {DTB_ADDRESS:#010x}")
    appended_end = ((low + len(kernel) + 7) & ~7) + APPENDED_FDT_ROOM
    if appended_end > DTB_ADDRESS:
        raise BootFileError(f"the device tree SPL appends would reach {appended_end:#010x}, past the DTB")


def check_fit(fit: bytes, kernel: bytes) -> None:
    """Checks a FIT as SPL reads it: its one configuration and kernel."""
    if len(fit) > FIT_LIMIT:
        raise BootFileError(f"the FIT is {len(fit)} bytes, more than SPL's pool holds ({FIT_LIMIT})")
    root = read_fdt(fit)

    # SPL takes the default configuration (each configuration needs a
    # description), and its kernel image when there is no firmware.
    configurations = root.find("configurations")
    if configurations is None:
        raise BootFileError("the FIT has no /configurations")
    default = configurations.string("default")
    configuration = configurations.children.get(default or "")
    if configuration is None:
        raise BootFileError(f"the FIT's default configuration {default!r} is missing")
    for node in configurations.children.values():
        if node.string("description") is None:
            raise BootFileError(f"the FIT's configuration {node.name} has no description")
    if configuration.string("firmware") is not None:
        raise BootFileError("the FIT's configuration names firmware, which SPL would boot instead")
    image_name = configuration.string("kernel")
    image = root.find(f"images/{image_name}") if image_name else None
    if image is None:
        raise BootFileError("the FIT's configuration names no kernel image")

    # The image is the raw kernel, uncompressed, to be entered as an ARM
    # Linux kernel at the load address.
    expected = {"type": "kernel", "arch": "arm", "os": "linux", "compression": "none"}
    for name, value in expected.items():
        if image.string(name) != value:
            raise BootFileError(f"the FIT's kernel has {name} = {image.string(name)!r}, not {value!r}")
    if image.cells("load") != (KERNEL_LOAD,):
        raise BootFileError(f"the FIT's kernel loads at {image.cells('load')}, not {KERNEL_LOAD:#x}")
    if image.cells("entry") != (KERNEL_LOAD,):
        raise BootFileError(f"the FIT's kernel enters at {image.cells('entry')}, not {KERNEL_LOAD:#x}")
    if image.properties.get("data") != kernel:
        raise BootFileError("the FIT's kernel data is not the raw kernel")


def check_dtb(dtb: bytes, required: list[str]) -> None:
    """Checks the DTB the kernel gets: its memory and command line."""
    if len(dtb) > DTB_LIMIT:
        raise BootFileError(f"the DTB is {len(dtb)} bytes, more than {DTB_LIMIT}")
    root = read_fdt(dtb)
    memory = root.find("memory@0")
    if memory is None or memory.cells("reg") != (0, 0x20000000):
        raise BootFileError("the DTB does not give the board's 512 MiB at 0")
    chosen = root.find("chosen")
    bootargs = chosen.string("bootargs") if chosen is not None else None
    if bootargs is None:
        raise BootFileError("the DTB has no /chosen/bootargs")
    words = bootargs.split()
    for parameter in required:
        if parameter not in words:
            raise BootFileError(f"the DTB's command line {bootargs!r} lacks {parameter}")


def compile_dts(source: Path, output: Path) -> bytes:
    """Compiles a device tree source with dtc, which must not warn."""
    result = subprocess.run(["dtc", "-I", "dts", "-O", "dtb", "-o", str(output), str(source)],
                            capture_output=True, text=True, check=False)
    if result.returncode != 0 or result.stderr.strip():
        raise BootFileError(f"dtc {source}: {result.stderr.strip()}")
    return output.read_bytes()


def dts_string(text: str) -> str:
    """Quotes a string for a device tree source."""
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write_atomically(output: Path, data: bytes) -> None:
    """Writes a file beside its final name and moves it there when complete."""
    output.parent.mkdir(parents=True, exist_ok=True)
    partial = output.with_name(output.name + ".partial")
    partial.write_bytes(data)
    os.replace(partial, output)


def make_fit(args: argparse.Namespace) -> None:
    kernel = args.kernel.read_bytes()
    check_kernel_layout(kernel, args.elf.read_bytes())

    # The image source names the raw kernel by its full path, which dtc
    # takes into the FIT whole.
    kernel_path = str(args.kernel.resolve())
    if '"' in kernel_path or "\\" in kernel_path:
        raise BootFileError(f"the kernel's path cannot be named in a source: {kernel_path}")
    its = f"""/dts-v1/;
/ {{
	description = {dts_string(FIT_DESCRIPTION)};
	#address-cells = <1>;
	images {{
		kernel {{
			description = "zedBSD kernel";
			data = /incbin/({dts_string(kernel_path)});
			type = "kernel";
			arch = "arm";
			os = "linux";
			compression = "none";
			load = <{KERNEL_LOAD:#010x}>;
			entry = <{KERNEL_LOAD:#010x}>;
		}};
	}};
	configurations {{
		default = {dts_string(FIT_CONFIGURATION)};
		{FIT_CONFIGURATION} {{
			description = "zedBSD on the ZYBO";
			kernel = "kernel";
		}};
	}};
}};
"""
    with tempfile.TemporaryDirectory(prefix="zedbsd-zybo-fit-") as work:
        source = Path(work) / "vmunix.its"
        source.write_text(its)
        fit = compile_dts(source, Path(work) / "vmunix.itb")
    check_fit(fit, kernel)
    write_atomically(args.output, fit)


def make_dtb(args: argparse.Namespace) -> None:
    with tempfile.TemporaryDirectory(prefix="zedbsd-zybo-dtb-") as work:
        dtb = compile_dts(args.dts, Path(work) / "base.dtb")

        # Parameters the build adds go after the source's own: the source
        # is included whole and its command line set again.
        if args.bootargs.strip():
            chosen = read_fdt(dtb).find("chosen")
            base = chosen.string("bootargs") if chosen is not None else None
            if base is None:
                raise BootFileError(f"{args.dts} has no /chosen/bootargs")
            dts_path = str(args.dts.resolve())
            if '"' in dts_path or "\\" in dts_path:
                raise BootFileError(f"the source's path cannot be included: {dts_path}")
            combined = " ".join((base + " " + args.bootargs).split())
            source = Path(work) / "system.dts"
            source.write_text(f"/include/ {dts_string(dts_path)}\n"
                              f"/ {{ chosen {{ bootargs = {dts_string(combined)}; }}; }};\n")
            dtb = compile_dts(source, Path(work) / "system.dtb")
    check_dtb(dtb, ["rootpart=/dev/mmcblk0p2"])
    write_atomically(args.output, dtb)


def main() -> None:
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    fit = commands.add_parser("fit")
    fit.add_argument("--kernel", type=Path, required=True)
    fit.add_argument("--elf", type=Path, required=True)
    fit.add_argument("output", type=Path)
    dtb = commands.add_parser("dtb")
    dtb.add_argument("--dts", type=Path, required=True)
    dtb.add_argument("--bootargs", default="")
    dtb.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "fit":
            make_fit(args)
        else:
            make_dtb(args)
    except BootFileError as error:
        raise SystemExit(f"make-zybo-boot-files: {error}")


if __name__ == "__main__":
    main()

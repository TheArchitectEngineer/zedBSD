#!/usr/bin/env python3
# zedBSD: wraps a raw kernel in a legacy uImage header for the RG350's
# boot loader.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Write a legacy uImage: a 64-byte big-endian header and the raw kernel.

The header carries the CRC-32 of the data and, computed with its own CRC
field zero, of itself.  The boot loader checks only the magic, the
operating system, the architecture, the type and the compression, but
both CRCs are given, so that any other reader accepts the image too.
The time stamp is SOURCE_DATE_EPOCH, or zero, so that the same kernel
gives the same image.
"""

import argparse
import os
import struct
import sys
import zlib

# Values referenced: U-Boot include/image.h (IH_MAGIC 0x27051956, IH_OS_LINUX
# 5, IH_ARCH_MIPS 5, IH_TYPE_KERNEL 2, IH_COMP_NONE 0, IH_NMLEN 32, and the
# order and widths of the legacy header's fields).
HEADER_MAGIC = 0x27051956
HEADER_OS_LINUX = 5
HEADER_ARCH_MIPS = 5
HEADER_TYPE_KERNEL = 2
HEADER_COMP_NONE = 0
HEADER_NAME_SIZE = 32
HEADER_FORMAT = ">IIIIIIIBBBB32s"
HEADER_SIZE = 64


def make_header(data, load, entry, stamp, name, header_crc=0):
    """Pack the header for data with the given header CRC field."""
    encoded = name.encode("ascii")[: HEADER_NAME_SIZE - 1]
    return struct.pack(
        HEADER_FORMAT,
        HEADER_MAGIC,
        header_crc,
        stamp,
        len(data),
        load,
        entry,
        zlib.crc32(data) & 0xFFFFFFFF,
        HEADER_OS_LINUX,
        HEADER_ARCH_MIPS,
        HEADER_TYPE_KERNEL,
        HEADER_COMP_NONE,
        encoded,
    )


def build_image(data, load, entry, stamp, name):
    """Return the header with both CRCs, followed by data."""
    blank = make_header(data, load, entry, stamp, name)
    crc = zlib.crc32(blank) & 0xFFFFFFFF
    return make_header(data, load, entry, stamp, name, crc) + data


def check_image(image):
    """Return a description of a uImage's header, or raise ValueError."""
    if len(image) < HEADER_SIZE:
        raise ValueError("shorter than a header")
    fields = struct.unpack(HEADER_FORMAT, image[:HEADER_SIZE])
    magic, header_crc, stamp, size, load, entry, data_crc = fields[:7]
    os_id, arch, kind, comp, name = fields[7:]
    if magic != HEADER_MAGIC:
        raise ValueError("bad magic %08x" % magic)
    blank = image[:4] + b"\0\0\0\0" + image[8:HEADER_SIZE]
    if zlib.crc32(blank) & 0xFFFFFFFF != header_crc:
        raise ValueError("bad header CRC")
    data = image[HEADER_SIZE:]
    if len(data) != size:
        raise ValueError("size %u, data %u" % (size, len(data)))
    if zlib.crc32(data) & 0xFFFFFFFF != data_crc:
        raise ValueError("bad data CRC")
    if (os_id, arch, kind, comp) != (
        HEADER_OS_LINUX,
        HEADER_ARCH_MIPS,
        HEADER_TYPE_KERNEL,
        HEADER_COMP_NONE,
    ):
        raise ValueError("not an uncompressed MIPS Linux kernel")
    return "name=%s size=%u load=%08x entry=%08x time=%u" % (
        name.rstrip(b"\0").decode("ascii", "replace"),
        size,
        load,
        entry,
        stamp,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--load", type=lambda v: int(v, 0))
    parser.add_argument("--entry", type=lambda v: int(v, 0))
    parser.add_argument("--name", default="zedBSD")
    parser.add_argument("--check", action="store_true", help="check an existing image")
    parser.add_argument("input")
    parser.add_argument("output", nargs="?")
    args = parser.parse_args()

    with open(args.input, "rb") as stream:
        data = stream.read()

    if args.check:
        try:
            print(check_image(data))
        except ValueError as error:
            print("%s: %s" % (args.input, error), file=sys.stderr)
            return 1
        return 0

    if args.load is None or args.entry is None or args.output is None:
        parser.error("--load, --entry and an output are required")
    stamp = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    image = build_image(data, args.load, args.entry, stamp, args.name)
    with open(args.output, "wb") as stream:
        stream.write(image)
    return 0


if __name__ == "__main__":
    sys.exit(main())

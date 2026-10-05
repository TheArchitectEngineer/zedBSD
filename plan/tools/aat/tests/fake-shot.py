#!/usr/bin/env python3
"""The self-test's stand-in for keiland-shot: writes a 64x48 grey PNG to the path it is given.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import struct
import sys
import zlib


def chunk(kind: bytes, data: bytes) -> bytes:
	"""Returns one PNG chunk."""
	return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


width, height = 64, 48
rows = b"".join(b"\x00" + b"\x80\x80\x80" * width for _ in range(height))
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
png += chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
with open(sys.argv[1], "wb") as output:
	output.write(png)

#!/usr/bin/env python3
"""Compares two screenshots of the desktop (keiland-shot) pixel by pixel.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    shot-diff.py BEFORE.png AFTER.png [DIFF.png]

Prints the largest difference of a channel (0 to 255), how many pixels
differ at all, by more than 2 and by more than 8, and the box around those
over 2 (a changed clock or cursor shows as a small box).  DIFF.png, when
given, is the difference made 16 times brighter.
"""
import sys

import numpy
from PIL import Image


def main() -> int:
	if len(sys.argv) not in (3, 4):
		print(__doc__.strip().splitlines()[3], file=sys.stderr)
		return 2
	before = numpy.asarray(Image.open(sys.argv[1]).convert("RGB"), dtype=numpy.int16)
	after = numpy.asarray(Image.open(sys.argv[2]).convert("RGB"), dtype=numpy.int16)
	if before.shape != after.shape:
		print(f"sizes differ: {before.shape} and {after.shape}")
		return 1
	difference = numpy.abs(before - after).max(axis=2)
	print(f"max {int(difference.max())} differ {int((difference > 0).sum())} "
		f"over2 {int((difference > 2).sum())} over8 {int((difference > 8).sum())} of {difference.size}")
	rows, columns = numpy.nonzero(difference > 2)
	if rows.size:
		print(f"box over2 x={columns.min()}..{columns.max()} y={rows.min()}..{rows.max()}")
	if len(sys.argv) == 4:
		Image.fromarray(numpy.clip(difference * 16, 0, 255).astype(numpy.uint8)).save(sys.argv[3])
	return 0


if __name__ == "__main__":
	sys.exit(main())

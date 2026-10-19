#!/usr/bin/env python3
# Writes tone-480hz.raw: 0.1 s of a 480 Hz sine, signed 16-bit little
# endian stereo at 48 kHz (the jack's one format), at a quarter of full
# scale; 480 Hz is 100 frames a cycle, so copies of the file join without
# a click.  Played on the board with: cat /tone.raw /tone.raw ... > /dev/dsp0
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import math, struct, sys
out = sys.argv[1] if len(sys.argv) > 1 else "tone-480hz.raw"
frames = bytearray()
for n in range(4800):
    v = int(round(8192 * math.sin(2 * math.pi * n / 100)))
    frames += struct.pack("<hh", v, v)
open(out, "wb").write(frames)

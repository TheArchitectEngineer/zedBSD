#!/usr/bin/env python3
"""Independent host-side image oracle for standard wltest GPU drawing.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import hashlib
import re


def read_ppm(path):
    raw = path.read_bytes()
    match = re.match(rb'P6\s+(\d+)\s+(\d+)\s+255\s', raw)
    if match is None:
        raise ValueError('expected binary RGB8 PPM')
    width, height = map(int, match.groups())
    if not 64 <= width <= 4096 or not 12 <= height <= 4096:
        raise ValueError(f'unsupported capture geometry {width}x{height}')
    pixels = raw[match.end():]
    if len(pixels) != width * height * 3:
        raise ValueError('capture has an incomplete or extended RGB payload')
    return width, height, pixels


def verify(path, frame, width=320, height=240):
    actual_width, actual_height, pixels = read_ppm(path)
    if (actual_width, actual_height) != (width, height):
        raise ValueError(f'capture geometry {actual_width}x{actual_height} != {width}x{height}')
    if not 1 <= frame <= 3600:
        raise ValueError('frame lies outside the bounded application contract')
    bar_left = (frame - 1) * 29 % (width - 40)
    mismatch = 0
    first = None
    for y in range(height):
        for x in range(width):
            if bar_left <= x < bar_left + 40 and height // 3 <= y < 2 * (height // 3):
                expected = bytes((255, 255, 255))
            elif y >= height // 2:
                expected = bytes((0, 0, 255))
            elif x < width // 2:
                expected = bytes((255, 0, 0))
            else:
                expected = bytes((0, 255, 0))
            offset = (y * width + x) * 3
            actual = pixels[offset:offset + 3]
            if actual != expected:
                mismatch += 1
                if first is None:
                    first = {'x': x, 'y': y, 'actual': list(actual), 'expected': list(expected)}
    return {'passed': mismatch == 0, 'frame': frame, 'width': width, 'height': height,
            'evaluated_pixels': width * height, 'mismatch_pixels': mismatch,
            'first_mismatch': first, 'rgb_sha256': hashlib.sha256(pixels).hexdigest()}

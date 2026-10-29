#!/usr/bin/env python3
# ws068-p039: an independent reference decoder of OpenGL ES 3.0's ETC2 and EAC blocks (the Khronos Data Format
# Specification's tables, bit fields read from the block as one 64-bit big-endian integer), for etc2-test.c.
#
#   python3 -I reference.py OUTDIR
#
# Writes OUTDIR/<format>.blocks (random blocks, a seed of each format; some blocks forced into the T, H and planar
# modes and into punch-through transparency) and OUTDIR/<format>.expected (each texel of a 4 x N image, in the
# kept form libGLESv2 keeps: RGBA8, or the 11-bit channels as integers 0..2047 / -1023..1023).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import random
import struct
import sys

MOD = [[2, 8, -2, -8], [5, 17, -5, -17], [9, 29, -9, -29], [13, 42, -13, -42],
       [18, 60, -18, -60], [24, 80, -24, -80], [33, 106, -33, -106], [47, 183, -47, -183]]
DIST = [3, 6, 11, 16, 23, 32, 41, 64]
EAC = [[-3, -6, -9, -15, 2, 5, 8, 14], [-3, -7, -10, -13, 2, 6, 9, 12], [-2, -5, -8, -13, 1, 4, 7, 12],
       [-2, -4, -6, -13, 1, 3, 5, 12], [-3, -6, -8, -12, 2, 5, 7, 11], [-3, -7, -9, -11, 2, 6, 8, 10],
       [-4, -7, -8, -11, 3, 6, 7, 10], [-3, -5, -8, -11, 2, 4, 7, 10], [-2, -6, -8, -10, 1, 5, 7, 9],
       [-2, -5, -8, -10, 1, 4, 7, 9], [-2, -4, -8, -10, 1, 3, 7, 9], [-2, -5, -7, -10, 1, 4, 6, 9],
       [-3, -4, -7, -10, 2, 3, 6, 9], [-1, -2, -3, -10, 0, 1, 2, 9], [-4, -6, -8, -9, 3, 5, 7, 8],
       [-3, -5, -7, -9, 2, 4, 6, 8]]


def bits(word, high, low):
    """The field of bits high..low (inclusive) of a 64-bit word."""
    return (word >> low) & ((1 << (high - low + 1)) - 1)


def clamp(v, lo=0, hi=255):
    return max(lo, min(hi, v))


def ext4(v):
    return v * 17


def ext5(v):
    return (v << 3) | (v >> 2)


def sext3(v):
    return v - 8 if v >= 4 else v


def index_of(word, x, y):
    i = x * 4 + y
    return (bits(word, 16 + i, 16 + i) << 1) | bits(word, i, i)


def rgb_block(word, punch):
    """Returns 16 texels (index x*4+y) of (r, g, b, a)."""
    opaque_bit = bits(word, 33, 33)
    if not punch and opaque_bit == 0:
        c1 = [ext4(bits(word, 63, 60)), ext4(bits(word, 55, 52)), ext4(bits(word, 47, 44))]
        c2 = [ext4(bits(word, 59, 56)), ext4(bits(word, 51, 48)), ext4(bits(word, 43, 40))]
        return sub_blocks(word, c1, c2, True)
    r, dr = bits(word, 63, 59), sext3(bits(word, 58, 56))
    g, dg = bits(word, 55, 51), sext3(bits(word, 50, 48))
    b, db = bits(word, 47, 43), sext3(bits(word, 42, 40))
    opaque = (not punch) or opaque_bit == 1
    if not 0 <= r + dr <= 31:
        a = [ext4((bits(word, 60, 59) << 2) | bits(word, 57, 56)), ext4(bits(word, 55, 52)), ext4(bits(word, 51, 48))]
        bcol = [ext4(bits(word, 47, 44)), ext4(bits(word, 43, 40)), ext4(bits(word, 39, 36))]
        d = DIST[(bits(word, 35, 34) << 1) | bits(word, 32, 32)]
        paint = [a, [c + d for c in bcol], bcol, [c - d for c in bcol]]
        return painted(word, paint, opaque)
    if not 0 <= g + dg <= 31:
        r1 = bits(word, 62, 59)
        g1 = (bits(word, 58, 56) << 1) | bits(word, 52, 52)
        b1 = (bits(word, 51, 51) << 3) | (bits(word, 49, 48) << 1) | bits(word, 47, 47)
        r2 = bits(word, 46, 43)
        g2 = (bits(word, 42, 40) << 1) | bits(word, 39, 39)
        b2 = bits(word, 38, 35)
        low = 1 if (r1 << 8 | g1 << 4 | b1) >= (r2 << 8 | g2 << 4 | b2) else 0
        d = DIST[(bits(word, 34, 34) << 2) | (bits(word, 32, 32) << 1) | low]
        a = [ext4(r1), ext4(g1), ext4(b1)]
        bcol = [ext4(r2), ext4(g2), ext4(b2)]
        paint = [[c + d for c in a], [c - d for c in a], [c + d for c in bcol], [c - d for c in bcol]]
        return painted(word, paint, opaque)
    if not 0 <= b + db <= 31:
        ro = bits(word, 62, 57)
        go = (bits(word, 56, 56) << 6) | bits(word, 54, 49)
        bo = (bits(word, 48, 48) << 5) | (bits(word, 44, 43) << 3) | (bits(word, 41, 40) << 1) | bits(word, 39, 39)
        rh = (bits(word, 38, 34) << 1) | bits(word, 32, 32)
        gh = bits(word, 31, 25)
        bh = bits(word, 24, 19)
        rv = bits(word, 18, 13)
        gv = bits(word, 12, 6)
        bv = bits(word, 5, 0)
        e6 = lambda v: (v << 2) | (v >> 4)
        e7 = lambda v: (v << 1) | (v >> 6)
        o = [e6(ro), e7(go), e6(bo)]
        h = [e6(rh), e7(gh), e6(bh)]
        v = [e6(rv), e7(gv), e6(bv)]
        out = [None] * 16
        for x in range(4):
            for y in range(4):
                out[x * 4 + y] = tuple(clamp((x * (h[c] - o[c]) + y * (v[c] - o[c]) + 4 * o[c] + 2) >> 2) for c in range(3)) + (255,)
        return out
    c1 = [ext5(r), ext5(g), ext5(b)]
    c2 = [ext5(r + dr), ext5(g + dg), ext5(b + db)]
    return sub_blocks(word, c1, c2, opaque)


def sub_blocks(word, c1, c2, opaque):
    t1, t2 = bits(word, 39, 37), bits(word, 36, 34)
    flip = bits(word, 32, 32)
    out = [None] * 16
    for x in range(4):
        for y in range(4):
            second = (y >= 2) if flip else (x >= 2)
            base, table = (c2, t2) if second else (c1, t1)
            idx = index_of(word, x, y)
            if not opaque and idx == 2:
                out[x * 4 + y] = (0, 0, 0, 0)
                continue
            m = MOD[table][idx]
            if not opaque and idx == 0:
                m = 0
            out[x * 4 + y] = tuple(clamp(c + m) for c in base) + (255,)
    return out


def painted(word, paint, opaque):
    out = [None] * 16
    for x in range(4):
        for y in range(4):
            idx = index_of(word, x, y)
            if not opaque and idx == 2:
                out[x * 4 + y] = (0, 0, 0, 0)
            else:
                out[x * 4 + y] = tuple(clamp(c) for c in paint[idx]) + (255,)
    return out


def eac(word, kind):
    """kind 'alpha' (0..255), 'unsigned' (0..2047) or 'signed' (-1023..1023)."""
    base = bits(word, 63, 56)
    mult = bits(word, 55, 52)
    table = bits(word, 51, 48)
    if kind == 'signed':
        base = base - 256 if base >= 128 else base
        base = -127 if base == -128 else base
    out = [None] * 16
    for x in range(4):
        for y in range(4):
            i = x * 4 + y
            m = EAC[table][bits(word, 47 - 3 * i, 45 - 3 * i)]
            if kind == 'alpha':
                out[i] = clamp(base + m * mult)
            elif kind == 'unsigned':
                out[i] = clamp(base * 8 + 4 + (m * mult * 8 if mult else m), 0, 2047)
            else:
                out[i] = clamp(base * 8 + (m * mult * 8 if mult else m), -1023, 1023)
    return out


def force(word, mode):
    """Rewrites a differential block's fields so that it takes a mode: t, h, planar, or diff."""
    word |= 1 << 33
    def put(w, high, low, value):
        mask = ((1 << (high - low + 1)) - 1) << low
        return (w & ~mask) | ((value << low) & mask)
    if mode == 't':
        return put(put(word, 63, 59, 31), 58, 56, 1)
    word = put(put(word, 63, 59, 10), 58, 56, 0)
    if mode == 'h':
        return put(put(word, 55, 51, 0), 50, 48, 7)
    word = put(put(word, 55, 51, 10), 50, 48, 0)
    if mode == 'planar':
        return put(put(word, 47, 43, 31), 42, 40, 2)
    return put(put(word, 47, 43, 10), 42, 40, 0)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rng = random.Random(68039)
    count = 64
    for name, size, kind in (('rgb8', 8, 'rgb'), ('punch', 8, 'punch'), ('rgba8', 16, 'rgba'),
                             ('r11', 8, 'r11'), ('sr11', 8, 'sr11'), ('rg11', 16, 'rg11'), ('srg11', 16, 'srg11')):
        blocks = []
        texels = []
        for n in range(count):
            words = [rng.getrandbits(64) for _ in range(size // 8)]
            if kind in ('rgb', 'punch', 'rgba'):
                colour = len(words) - 1
                mode = ['t', 'h', 'planar', 'diff', None][n % 5]
                if mode is not None:
                    words[colour] = force(words[colour], mode)
                if kind == 'punch' and n % 2 == 0:
                    words[colour] &= ~(1 << 33)
                pixels = rgb_block(words[colour], kind == 'punch')
                if kind == 'rgba':
                    alpha = eac(words[0], 'alpha')
                    pixels = [p[:3] + (alpha[i],) for i, p in enumerate(pixels)]
                texels.append(['%d %d %d %d' % p for p in pixels])
            else:
                channel = 'signed' if kind.startswith('s') else 'unsigned'
                values = [eac(w, channel) for w in words]
                texels.append([' '.join(str(v[i]) for v in values) for i in range(16)])
            blocks.append(b''.join(struct.pack('>Q', w) for w in words))
        with open(os.path.join(out, name + '.blocks'), 'wb') as f:
            f.write(b''.join(blocks))
        # The image is 4 texels wide and 4 * count high: block n is rows 4n..4n+3; the file lists the texels by row.
        with open(os.path.join(out, name + '.expected'), 'w') as f:
            for n in range(count):
                for y in range(4):
                    for x in range(4):
                        f.write(texels[n][x * 4 + y] + '\n')


main()

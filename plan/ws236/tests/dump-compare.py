#!/usr/bin/env python3
# ws236: compares the frames the Pi 4's decoder gave (a dump from the kernel probe's /dev/mmal-probe, or from
# vkvideo-probe --dump=FILE) with the same stream decoded on the host by ffmpeg, and writes PNGs a person can read.
#
# usage: python3 -I plan/ws236/tests/dump-compare.py DUMP OUTDIR [--streams DIR]...
#
# A dump is a run of records: a 512-byte text header line of key=value words (stream, file, run, out, pts,
# pts_display, display, fmt, width, height, crop_width, crop_height, offset0, offset1, pitch0, pitch1, length, ...)
# and then `length` bytes of the frame as the decoder put them in memory.  For each record it writes:
#   OUTDIR/<file>-<run>-<out>.png        reference | actual | |difference| for Y, then Cb and Cr, then RGB
#   OUTDIR/<file>-<run>-<out>-alt.png    the Y difference under other readings of the bytes (pitch, height, order)
# and OUTDIR/summary.txt, one block a record: the reference picture chosen, the error numbers, the best reading.
# The reference picture is the header's display (a checksum match), else its pts_display, else (and always reported)
# the reference picture closest to the actual one.  A vulkan record (vkvideo-probe) is in decode order.
# Needs ffmpeg and ffprobe, numpy and Pillow.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

HEADER = 512
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
DEFAULT_STREAMS = [HERE, os.path.join(REPO, 'plan', 'ws083', 'tests', 'streams')]


def records(path):
    data = open(path, 'rb').read()
    at = 0
    while at + HEADER <= len(data):
        line = data[at:at + HEADER].decode('ascii', 'replace').strip()
        if not line.startswith('zdump'):
            sys.exit('%s: no record header at byte %d' % (path, at))
        fields = dict(word.split('=', 1) for word in line.split()[1:] if '=' in word)
        length = int(fields['length'])
        body = data[at + HEADER:at + HEADER + length]
        if len(body) != length:
            sys.exit('%s: record at byte %d is cut short' % (path, at))
        yield fields, np.frombuffer(body, dtype=np.uint8)
        at += HEADER + length


def find_stream(name, directories):
    for directory in directories:
        candidate = os.path.join(directory, name)
        if os.path.exists(candidate):
            return candidate
    sys.exit('stream %s not found in %s (give --streams DIR)' % (name, ' '.join(directories)))


REFERENCES = {}


def reference(path):
    """The stream's frames decoded by ffmpeg, display order, as (Y, U, V) arrays; and decode index -> display index."""
    if path in REFERENCES:
        return REFERENCES[path]
    dims = subprocess.check_output(['ffprobe', '-v', 'error', '-select_streams', 'v:0', '-show_entries',
                                    'stream=width,height', '-of', 'csv=p=0', path]).decode().strip().split(',')
    w, h = int(dims[0]), int(dims[1])
    raw = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', path, '-f', 'rawvideo', '-pix_fmt', 'yuv420p', '-'])
    size = w * h * 3 // 2
    frames = []
    for i in range(0, len(raw), size):
        f = np.frombuffer(raw[i:i + size], dtype=np.uint8)
        y = f[:w * h].reshape(h, w)
        u = f[w * h:w * h + w * h // 4].reshape(h // 2, w // 2)
        v = f[w * h + w * h // 4:].reshape(h // 2, w // 2)
        frames.append((y, u, v))
    pos = subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'frame=pkt_pos', '-of', 'csv=p=0',
                                   path]).decode().split()
    pos = [int(p.strip(',')) for p in pos]
    decode = sorted(range(len(pos)), key=lambda i: pos[i])
    REFERENCES[path] = (frames, decode)
    return REFERENCES[path]


def read_planes(raw, width, height, offset0, offset1, pitch0, pitch1, order):
    """Reads Y, U, V of width x height from raw under one reading; None if the bytes do not reach."""
    if offset0 + pitch0 * (height - 1) + width > len(raw):
        return None
    y = np.lib.stride_tricks.as_strided(raw[offset0:], shape=(height, width), strides=(pitch0, 1)).copy()
    ch, cw = height // 2, width // 2
    if order in ('NV12', 'NV21'):
        if offset1 + pitch1 * (ch - 1) + width > len(raw):
            return None
        uv = np.lib.stride_tricks.as_strided(raw[offset1:], shape=(ch, width), strides=(pitch1, 1)).copy()
        u, v = uv[:, 0::2], uv[:, 1::2]
        if order == 'NV21':
            u, v = v, u
    else:
        half = pitch1 // 2 if pitch1 else pitch0 // 2
        second = offset1 + half * (height // 2)
        if second + half * (ch - 1) + cw > len(raw):
            return None
        u = np.lib.stride_tricks.as_strided(raw[offset1:], shape=(ch, cw), strides=(half, 1)).copy()
        v = np.lib.stride_tricks.as_strided(raw[second:], shape=(ch, cw), strides=(half, 1)).copy()
        if order == 'YV12':
            u, v = v, u
    return y, u, v


def to_rgb(y, u, v):
    """BT.601 limited range, for looking at only."""
    uu = np.repeat(np.repeat(u, 2, axis=0), 2, axis=1)[:y.shape[0], :y.shape[1]].astype(np.float32) - 128.0
    vv = np.repeat(np.repeat(v, 2, axis=0), 2, axis=1)[:y.shape[0], :y.shape[1]].astype(np.float32) - 128.0
    yy = (y.astype(np.float32) - 16.0) * 1.164
    rgb = np.stack([yy + 1.596 * vv, yy - 0.392 * uu - 0.813 * vv, yy + 2.017 * uu], axis=-1)
    return np.clip(rgb, 0, 255).astype(np.uint8)


def heat(difference, gain):
    """|difference| times gain as a picture: black where equal, red to yellow where not."""
    d = np.clip(np.abs(difference.astype(np.int16)) * gain, 0, 255).astype(np.uint8)
    out = np.zeros(d.shape + (3,), dtype=np.uint8)
    out[..., 0] = np.where(d > 0, np.maximum(d, 96), 0)
    out[..., 1] = np.where(d > 128, d, 0)
    return out


def gray(plane):
    return np.stack([plane] * 3, axis=-1)


def labelled(picture, text):
    image = Image.fromarray(picture)
    canvas = Image.new('RGB', (image.width, image.height + 14), (40, 40, 40))
    canvas.paste(image, (0, 14))
    ImageDraw.Draw(canvas).text((2, 1), text, fill=(255, 255, 255))
    return canvas


def row(pictures):
    width = sum(p.width for p in pictures) + 4 * (len(pictures) - 1)
    height = max(p.height for p in pictures)
    canvas = Image.new('RGB', (width, height), (0, 0, 0))
    x = 0
    for p in pictures:
        canvas.paste(p, (x, 0))
        x += p.width + 4
    return canvas


def column(rows):
    width = max(r.width for r in rows)
    height = sum(r.height for r in rows) + 4 * (len(rows) - 1)
    canvas = Image.new('RGB', (width, height), (0, 0, 0))
    y = 0
    for r in rows:
        canvas.paste(r, (0, y))
        y += r.height + 4
    return canvas


def numbers(actual, expected):
    y = actual[0].astype(np.int16) - expected[0].astype(np.int16)
    uv = np.concatenate([(actual[1].astype(np.int16) - expected[1].astype(np.int16)).ravel(),
                         (actual[2].astype(np.int16) - expected[2].astype(np.int16)).ravel()])
    rows = np.nonzero(np.any(y != 0, axis=1))[0]
    first = None
    if len(rows):
        r = int(rows[0])
        first = (r, int(np.nonzero(y[r])[0][0]))
    return {
        'y_mad': float(np.mean(np.abs(y))), 'y_max': int(np.max(np.abs(y))),
        'y_differ': float(np.mean(y != 0)) * 100.0, 'uv_mad': float(np.mean(np.abs(uv))),
        'uv_max': int(np.max(np.abs(uv))), 'uv_differ': float(np.mean(uv != 0)) * 100.0,
        'rows_differ': len(rows), 'first': first,
        'psnr_y': float('inf') if not np.any(y) else 10 * np.log10(255.0 ** 2 / np.mean(y.astype(np.float64) ** 2)),
    }


def candidates(fields, width, height):
    """Other readings of the bytes: pitches, the chroma plane after the coded or the visible height, plane orders."""
    p0 = int(fields.get('pitch0') or 0) or width
    full_h = int(fields.get('height') or height)
    pitches = sorted({p0, width, (width + 31) // 32 * 32, (width + 63) // 64 * 64, (width + 127) // 128 * 128,
                      2048 if width > 1024 else 384})
    readings = []
    for pitch in pitches:
        for chroma_h in sorted({height, full_h, (height + 15) // 16 * 16}):
            for order in ('NV12', 'NV21', 'I420', 'YV12'):
                pitch1 = pitch
                readings.append(('pitch %d, chroma after %d rows, %s' % (pitch, chroma_h, order),
                                 0, pitch * chroma_h, pitch, pitch1, order))
    header_reading = ('header: offsets %s/%s pitches %s/%s, NV12' % (fields.get('offset0'), fields.get('offset1'),
                                                                     fields.get('pitch0'), fields.get('pitch1')),
                      int(fields.get('offset0') or 0), int(fields.get('offset1') or 0), p0,
                      int(fields.get('pitch1') or 0) or p0, 'NV12')
    return [header_reading] + readings


def main():
    args = sys.argv[1:]
    streams = list(DEFAULT_STREAMS)
    while '--streams' in args:
        at = args.index('--streams')
        streams.insert(0, args[at + 1])
        del args[at:at + 2]
    if len(args) != 2:
        sys.exit('usage: dump-compare.py DUMP OUTDIR [--streams DIR]...')
    dump, outdir = args
    os.makedirs(outdir, exist_ok=True)
    summary = open(os.path.join(outdir, 'summary.txt'), 'w')
    for fields, raw in records(dump):
        name = fields.get('file', '?')
        path = find_stream(name, streams)
        frames, decode = reference(path)
        width = int(fields.get('crop_width') or fields['width'])
        height = int(fields.get('crop_height') or fields['height'])
        cx, cy = int(fields.get('crop_x', 0)), int(fields.get('crop_y', 0))
        out = int(fields.get('out', 0))
        tag = '%s-%s-%03d' % (os.path.splitext(name)[0], fields.get('run', '?'), out)

        # The actual picture under the header's reading (cropped by the display window when one is given).
        readings = candidates(fields, width + cx, height + cy)
        actual = read_planes(raw, width + cx, height + cy, *readings[0][1:])
        if actual is None:
            print('%s: the header does not describe the bytes' % tag, file=summary)
            continue
        actual = (actual[0][cy:, cx:], actual[1][cy // 2:, cx // 2:], actual[2][cy // 2:, cx // 2:])

        # The reference picture: the header's, and the closest of all.
        chosen = -1
        why = 'none'
        for key in ('display', 'pts_display'):
            value = int(fields.get(key, -1))
            if 0 <= value < len(frames):
                chosen, why = value, key
                break
        if fields.get('stream') == 'vulkan' and 0 <= out < len(decode):
            chosen, why = decode[out], 'decode order'
        distances = [float(np.mean(np.abs(actual[0].astype(np.int16) - f[0].astype(np.int16)))) for f in frames]
        closest = int(np.argmin(distances))
        if chosen < 0:
            chosen, why = closest, 'closest'
        expected = frames[chosen]
        n = numbers(actual, expected)

        # reference | actual | difference: Y, then Cb and Cr, then colour.
        dy = actual[0].astype(np.int16) - expected[0].astype(np.int16)
        du = actual[1].astype(np.int16) - expected[1].astype(np.int16)
        dv = actual[2].astype(np.int16) - expected[2].astype(np.int16)
        rgb_e, rgb_a = to_rgb(*expected), to_rgb(*actual)
        sheet = column([
            row([labelled(gray(expected[0]), 'Y reference (display %d)' % chosen),
                 labelled(gray(actual[0]), 'Y actual (%s run=%s out=%d)' % (fields.get('stream'), fields.get('run'), out)),
                 labelled(heat(dy, 8), '|Y diff| x8, mad %.2f max %d' % (n['y_mad'], n['y_max']))]),
            row([labelled(gray(expected[1]), 'Cb ref'), labelled(gray(actual[1]), 'Cb actual'),
                 labelled(heat(du, 8), '|Cb diff| x8'), labelled(gray(expected[2]), 'Cr ref'),
                 labelled(gray(actual[2]), 'Cr actual'), labelled(heat(dv, 8), '|Cr diff| x8')]),
            row([labelled(rgb_e, 'RGB reference'), labelled(rgb_a, 'RGB actual'),
                 labelled(np.clip(np.abs(rgb_a.astype(np.int16) - rgb_e.astype(np.int16)) * 4, 0, 255).astype(np.uint8),
                          '|RGB diff| x4')]),
        ])
        sheet.save(os.path.join(outdir, tag + '.png'))

        # The other readings: each one's Y difference and numbers, best first.
        scored = []
        for reading in readings:
            planes = read_planes(raw, width + cx, height + cy, *reading[1:])
            if planes is None:
                continue
            planes = (planes[0][cy:, cx:], planes[1][cy // 2:, cx // 2:], planes[2][cy // 2:, cx // 2:])
            m = numbers(planes, expected)
            scored.append((m['y_mad'] + m['uv_mad'], reading[0], planes, m))
        scored.sort(key=lambda s: s[0])
        tiles = []
        for score, text, planes, m in scored[:12]:
            d = planes[0].astype(np.int16) - expected[0].astype(np.int16)
            tile = Image.fromarray(heat(d, 8))
            if tile.width > 480:
                tile = tile.resize((480, tile.height * 480 // tile.width))
            tiles.append(labelled(np.asarray(tile), '%s: Y %.2f UV %.2f' % (text, m['y_mad'], m['uv_mad'])))
        column([row(tiles[i:i + 3]) for i in range(0, len(tiles), 3)]).save(os.path.join(outdir, tag + '-alt.png'))

        print('%s: %s' % (tag, ' '.join('%s=%s' % kv for kv in sorted(fields.items()))), file=summary)
        print('  reference: display %d (%s); closest display %d (Y mad %.2f)' % (chosen, why, closest, distances[closest]),
              file=summary)
        print('  header reading: Y mad %.3f max %d differ %.2f%% psnr %.2f dB, UV mad %.3f max %d differ %.2f%%, '
              'rows that differ %d of %d, first difference (row, col) %s' % (
                  n['y_mad'], n['y_max'], n['y_differ'], n['psnr_y'], n['uv_mad'], n['uv_max'], n['uv_differ'],
                  n['rows_differ'], height, n['first']), file=summary)
        for score, text, planes, m in scored[:3]:
            print('  best readings: %s: Y mad %.3f UV mad %.3f' % (text, m['y_mad'], m['uv_mad']), file=summary)
        print('%s: reference display %d (%s), Y mad %.3f, UV mad %.3f, best reading %s' % (
            tag, chosen, why, n['y_mad'], n['uv_mad'], scored[0][1] if scored else '-'))
    summary.close()


if __name__ == '__main__':
    main()

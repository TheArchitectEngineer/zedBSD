#!/usr/bin/env python3
# ws157-p002: writes the folder of photos the host test of Photos' library reads (run-host-photos-library.sh): JPEG
# headers with EXIF dates (DateTimeOriginal in the EXIF directory, or only IFD0's DateTime; little- and big-endian),
# a PNG, a GIF and files that are not pictures, in albums, deep folders and hidden folders, with set file times.
#   python3 plan/ws157/tests/make-photos.py FOLDER
# With --view (ws157-p003, run-host-photos.sh): real pictures (PIL) in two albums over three months, a tall JPEG with an
# EXIF orientation, a PNG with alpha, a GIF, and one damaged JPEG.
#   python3 plan/ws157/tests/make-photos.py --view FOLDER
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import calendar
import os
import struct
import sys


def tiff(order, original, changed, orientation=None):
    """A TIFF block: IFD0 (the orientation, DateTime, and a pointer to the EXIF directory with DateTimeOriginal when
    given)."""
    e = '<' if order == 'II' else '>'
    header = order.encode() + struct.pack(e + 'HI', 42, 8)
    entries0 = []
    data = b''
    # IFD0 at 8: its entries, then the EXIF directory, then the strings.
    count0 = (1 if changed else 0) + (1 if original else 0) + (1 if orientation else 0)
    ifd0_size = 2 + count0 * 12 + 4
    exif_at = 8 + ifd0_size
    exif_size = (2 + 12 + 4) if original else 0
    strings_at = exif_at + exif_size
    if orientation:
        entries0.append(struct.pack(e + 'HHIHH', 0x0112, 3, 1, orientation, 0))
    if changed:
        entries0.append(struct.pack(e + 'HHII', 0x0132, 2, 20, strings_at + len(data)))
        data += changed.encode() + b'\0'
    if original:
        entries0.append(struct.pack(e + 'HHII', 0x8769, 4, 1, exif_at))
    ifd0 = struct.pack(e + 'H', count0) + b''.join(entries0) + struct.pack(e + 'I', 0)
    exif = b''
    if original:
        exif = struct.pack(e + 'H', 1) + struct.pack(e + 'HHII', 0x9003, 2, 20, strings_at + len(data)) + struct.pack(e + 'I', 0)
        data += original.encode() + b'\0'
    return header + ifd0 + exif + data


def jpeg(path, order='II', original=None, changed=None, app0=True):
    body = b'\xff\xd8'
    if app0:
        jfif = b'JFIF\0\x01\x01\0\0\x01\0\x01\0\0'
        body += b'\xff\xe0' + struct.pack('>H', len(jfif) + 2) + jfif
    if original or changed:
        block = b'Exif\0\0' + tiff(order, original, changed)
        body += b'\xff\xe1' + struct.pack('>H', len(block) + 2) + block
    body += b'\xff\xda\0\x08\x01\x01\0\0\x3f\0' + b'\0' * 16 + b'\xff\xd9'
    write(path, body)


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def stamp(path, year, month, day):
    when = calendar.timegm((year, month, day, 12, 0, 0, 0, 0, 0))
    os.utime(path, (when, when))


def picture(path, size, colours, original=None, orientation=None, kind='JPEG'):
    """A real picture: bands of colours with a white disc, as a JPEG (with an EXIF block when given), PNG or GIF."""
    import io
    from PIL import Image, ImageDraw
    mode = 'RGBA' if kind == 'PNG' else 'RGB'
    image = Image.new(mode, size)
    draw = ImageDraw.Draw(image)
    w, h = size
    for index, colour in enumerate(colours):
        draw.rectangle((0, index * h // len(colours), w, (index + 1) * h // len(colours)), fill=colour)
    draw.ellipse((w // 3, h // 3, w * 2 // 3, h * 2 // 3), fill=(255, 255, 255, 255) if kind == 'PNG' else (255, 255, 255))
    if kind == 'PNG':
        draw.rectangle((0, 0, w // 4, h // 4), fill=(0, 0, 0, 0))
    out = io.BytesIO()
    if kind == 'GIF':
        image.convert('P').save(out, 'GIF')
    else:
        image.save(out, kind, quality=85)
    data = out.getvalue()
    if kind == 'JPEG' and (original or orientation):
        block = b'Exif\0\0' + tiff('II', original, None, orientation)
        data = data[:2] + b'\xff\xe1' + struct.pack('>H', len(block) + 2) + block + data[2:]
    write(path, data)


def view(top):
    picture(top + '/Trips/lake.jpg', (640, 480), [(40, 110, 200), (60, 160, 90)], '2026:09:20 10:00:00')
    picture(top + '/Trips/hill.jpg', (480, 640), [(200, 120, 40), (90, 60, 30)], '2026:09:12 16:30:00', orientation=6)
    picture(top + '/Trips/sea.jpg', (800, 450), [(20, 60, 140), (230, 210, 160)], '2026:08:30 09:00:00')
    picture(top + '/Family/cake.jpg', (600, 600), [(240, 200, 210), (200, 40, 80)], '2026:08:03 19:00:00')
    picture(top + '/Family/park.jpg', (640, 360), [(120, 190, 240), (40, 140, 60)], '2026:08:01 11:00:00')
    picture(top + '/sticker.png', (300, 300), [(250, 200, 40), (240, 120, 20)], kind='PNG')
    stamp(top + '/sticker.png', 2026, 7, 14)
    picture(top + '/wave.gif', (320, 240), [(120, 40, 160), (40, 160, 200)], kind='GIF')
    stamp(top + '/wave.gif', 2026, 7, 10)
    picture(top + '/garden.jpg', (512, 384), [(70, 150, 70), (150, 210, 110)], '2026:07:05 08:00:00')
    write(top + '/broken.jpg', b'\xff\xd8\xff\xe0\0\x10JFIF\0' + b'\x55' * 64)
    stamp(top + '/broken.jpg', 2026, 7, 1)


def main():
    if sys.argv[1] == '--view':
        view(sys.argv[2])
        return
    top = sys.argv[1]
    jpeg(top + '/beach.jpg', 'II', original='2024:08:15 10:30:00', changed='2024:09:01 08:00:00')
    jpeg(top + '/Trips/Kyoto/temple.jpg', 'MM', original='2025:04:02 09:15:00')
    jpeg(top + '/Trips/street.jpeg', 'II', changed='2023:12:31 23:59:59')
    jpeg(top + '/Family/birthday.jpg', 'MM', original='2026:01:20 18:00:00', app0=False)
    jpeg(top + '/Family/noclock.jpg', 'II', original='0000:00:00 00:00:00')
    stamp(top + '/Family/noclock.jpg', 2022, 6, 1)
    write(top + '/Family/drawing.png', b'\x89PNG\r\n\x1a\n' + b'\0' * 16)
    stamp(top + '/Family/drawing.png', 2021, 3, 4)
    write(top + '/anim.gif', b'GIF89a' + b'\0' * 16)
    stamp(top + '/anim.gif', 2020, 1, 2)
    write(top + '/notes.txt', b'not a picture\n')
    write(top + '/fake.jpg', b'JFIF but no')
    jpeg(top + '/.hidden/secret.jpg', 'II', original='2026:02:02 02:02:02')
    jpeg(top + '/Trips/.cache/thumb.jpg', 'II', original='2026:02:02 02:02:02')
    jpeg(top + '/Deep/a/b/four.jpg', 'II', original='2019:05:05 05:05:05')
    jpeg(top + '/Deep/a/b/c/five.jpg', 'II', original='2019:05:05 05:05:05')
    os.makedirs(top + '/Empty', exist_ok=True)
    jpeg(os.path.dirname(top.rstrip('/')) + '/outside-' + os.path.basename(top.rstrip('/')) + '.jpg', 'II',
         original='2018:07:07 07:07:07')


main()

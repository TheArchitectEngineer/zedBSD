#!/usr/bin/env python3
# ws168-p003: the host test of keiland-preview (run-host-preview.sh builds it on Linux, with its seccomp confinement):
# the pictures and documents made here (PIL, a hand-made PDF), each made a preview of as Files (256 contain) and
# Settings (240x150 cover) ask, the output's size, stamp and colours checked; a damaged, an unknown and an empty file,
# wrong arguments, an input that is not a regular file; and the test build's escapes after the confinement: open
# refused (EACCES), socket and fork ending the process by SIGSYS with nothing written.
#   python3 plan/ws168/tests/host-preview.py PROGRAM ESCAPE-PROGRAM FOLDER
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import io
import os
import signal
import struct
import subprocess
import sys

from PIL import Image

program, escape_program, folder = sys.argv[1:4]
failures = 0


def check(name, passed, detail=''):
    global failures
    print(('ok ' if passed else 'NOT OK ') + name + (': ' + detail if detail else ''))
    if not passed:
        failures += 1


def write(name, data):
    path = os.path.join(folder, name)
    with open(path, 'wb') as f:
        f.write(data)
    return path


def run(command, source, *arguments):
    """Runs the program with the source as fd 0 and a new file as fd 1; returns the status and the output's bytes."""
    output = os.path.join(folder, os.path.basename(source) + '.out')
    with open(source, 'rb') as given, open(output, 'wb') as made:
        result = subprocess.run([command, *arguments], stdin=given, stdout=made, stderr=subprocess.DEVNULL,
                                timeout=30, env={})
    with open(output, 'rb') as made:
        return result.returncode, made.read()


def ppm(data):
    """Reads a P6 the program wrote: its comment line (or None) and its picture."""
    if not data.startswith(b'P6\n'):
        return None, None
    rest = data[3:]
    comment = None
    if rest.startswith(b'#'):
        line, rest = rest.split(b'\n', 1)
        comment = line[2:].decode()
    head, rest = rest.split(b'\n', 1)
    width, height = map(int, head.split())
    maximum, pixels = rest.split(b'\n', 1)
    if maximum != b'255' or len(pixels) != width * height * 3:
        return comment, None
    return comment, Image.frombytes('RGB', (width, height), pixels)


def near(colour, wanted, slack=24):
    return all(abs(a - b) <= slack for a, b in zip(colour, wanted))


def picture(size, top, bottom, kind, **save):
    """A picture of two bands of colour (top half, bottom half) as bytes of a kind."""
    image = Image.new('RGB', size, top)
    image.paste(bottom, (0, size[1] // 2, size[0], size[1]))
    out = io.BytesIO()
    if kind == 'GIF':
        image = image.convert('P', palette=Image.ADAPTIVE, colors=8)
    image.save(out, kind, **save)
    return out.getvalue()


def exif_orientation(value):
    """An APP1 EXIF block holding only an orientation."""
    tiff = b'II' + struct.pack('<HI', 42, 8) + struct.pack('<H', 1) + struct.pack('<HHIHH', 0x0112, 3, 1, value, 0) + struct.pack('<I', 0)
    block = b'Exif\0\0' + tiff
    return b'\xff\xe1' + struct.pack('>H', len(block) + 2) + block


def pdf(width, height, colour):
    """A one-page PDF filled with a colour, a black square in its middle, and text in a font it does not embed."""
    body = ('%.3f %.3f %.3f rg 0 0 %d %d re f 0 g %d %d %d %d re f BT /F1 24 Tf 10 10 Td (Preview) Tj ET' %
            (colour[0] / 255, colour[1] / 255, colour[2] / 255, width, height, width // 3, height // 3, width // 3, height // 3)).encode()
    objects = [b'<< /Type /Catalog /Pages 2 0 R >>', b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
               b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources << /Font << /F1 5 0 R >> >> /Contents 4 0 R >>' % (width, height),
               b'<< /Length %d >>\nstream\n' % len(body) + body + b'\nendstream',
               b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>']
    out = b'%PDF-1.7\n'
    offsets = []
    for number, text in enumerate(objects, 1):
        offsets.append(len(out))
        out += b'%d 0 obj\n' % number + text + b'\nendobj\n'
    xref = len(out)
    out += b'xref\n0 %d\n0000000000 65535 f \n' % (len(objects) + 1) + b''.join(b'%010d 00000 n \n' % o for o in offsets)
    out += b'trailer\n<< /Size %d /Root 1 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(objects) + 1, xref)
    return out


red, blue, green, white, black = (220, 40, 40), (40, 60, 220), (40, 180, 70), (255, 255, 255), (0, 0, 0)

# PNG with transparency at the top (premultiplied: black under it), 400x300: contain 256 -> 256x192.
png_image = Image.new('RGBA', (400, 300), (0, 0, 0, 0))
png_image.paste((40, 60, 220, 255), (0, 150, 400, 300))
out = io.BytesIO()
png_image.save(out, 'PNG')
status, data = run(program, write('a.png', out.getvalue()), '--width=256', '--height=256', '--stamp=keiland-thumbnail mtime=1 size=2')
comment, image = ppm(data)
check('png', status == 0 and image is not None and image.size == (256, 192), f'status {status} size {image.size if image else None}')
check('png-stamp', comment == 'keiland-thumbnail mtime=1 size=2', repr(comment))
if image:
    check('png-colours', near(image.getpixel((128, 40)), black) and near(image.getpixel((128, 150)), blue),
          f'{image.getpixel((128, 40))} {image.getpixel((128, 150))}')

# JPEG 640x480 turned by its EXIF (6: a quarter clockwise) -> upright 480x640, contain 256 -> 192x256, red on the right.
jpeg = picture((640, 480), red, blue, 'JPEG', quality=92)
jpeg = jpeg[:2] + exif_orientation(6) + jpeg[2:]
status, data = run(program, write('b.jpg', jpeg), '--width=256', '--height=256')
comment, image = ppm(data)
check('jpeg-turned', status == 0 and image is not None and image.size == (192, 256), f'status {status} size {image.size if image else None}')
if image:
    check('jpeg-colours', near(image.getpixel((160, 128)), red) and near(image.getpixel((30, 128)), blue),
          f'{image.getpixel((160, 128))} {image.getpixel((30, 128))}')
check('jpeg-no-comment', comment is None)

# GIF 320x240, Settings' tile: cover 240x150 (cut from the middle).
status, data = run(program, write('c.gif', picture((320, 240), green, red, 'GIF')), '--width=240', '--height=150', '--fit=cover')
comment, image = ppm(data)
check('gif-cover', status == 0 and image is not None and image.size == (240, 150), f'status {status} size {image.size if image else None}')
if image:
    check('gif-colours', near(image.getpixel((120, 20)), green) and near(image.getpixel((120, 130)), red),
          f'{image.getpixel((120, 20))} {image.getpixel((120, 130))}')

# A small PPM is not made larger (contain); a PGM is grey.
small = Image.new('RGB', (100, 50), green)
out = io.BytesIO()
small.save(out, 'PPM')
status, data = run(program, write('d.ppm', out.getvalue()), '--width=256', '--height=256')
comment, image = ppm(data)
check('ppm-small', status == 0 and image is not None and image.size == (100, 50) and near(image.getpixel((50, 25)), green, 2),
      f'status {status} size {image.size if image else None}')
grey = Image.new('L', (64, 64), 128)
out = io.BytesIO()
grey.save(out, 'PPM')
status, data = run(program, write('e.pgm', out.getvalue()), '--width=32', '--height=32')
comment, image = ppm(data)
check('pgm', status == 0 and image is not None and image.size == (32, 32) and near(image.getpixel((16, 16)), (128, 128, 128), 2),
      f'status {status}')

# A PDF's first page (A4-like 595x842): contain 256 -> 182x256, its colour around a black square; the Helvetica
# text it does not embed is left out (no substitute file is opened in the confinement).
status, data = run(program, write('f.pdf', pdf(595, 842, (250, 200, 40))), '--width=256', '--height=256')
comment, image = ppm(data)
check('pdf', status == 0 and image is not None and image.size in ((181, 256), (182, 256)),
      f'status {status} size {image.size if image else None}')
if image:
    check('pdf-colours', near(image.getpixel((20, 20)), (250, 200, 40)) and near(image.getpixel((image.size[0] // 2, 128)), black),
          f'{image.getpixel((20, 20))} {image.getpixel((image.size[0] // 2, 128))}')

# The failures' statuses: unknown 1, damaged 2, usage 64; nothing written.
status, data = run(program, write('g.txt', b'just words\n'), '--width=64', '--height=64')
check('unknown', status == 1 and data == b'', f'status {status}')
png_bytes = open(os.path.join(folder, 'a.png'), 'rb').read()
status, data = run(program, write('h.png', png_bytes[:200]), '--width=64', '--height=64')
check('damaged', status == 2 and data == b'', f'status {status}')
status, data = run(program, write('i.empty', b''), '--width=64', '--height=64')
check('empty', status == 1 and data == b'', f'status {status}')
for arguments in (['--width=64'], ['--width=0', '--height=64'], ['--width=5000', '--height=64'], ['--width=64', '--height=64', '--bogus'],
                  ['--width=64', '--height=64', '--test-escape=open']):
    status, data = run(program, os.path.join(folder, 'a.png'), *arguments)
    check('usage ' + ' '.join(arguments), status == 64 and data == b'', f'status {status}')

# An input that is not a regular file (a pipe) is refused.
reader, writer = os.pipe()
with open(os.path.join(folder, 'pipe.out'), 'wb') as made:
    result = subprocess.run([program, '--width=64', '--height=64'], stdin=reader, stdout=made, stderr=subprocess.DEVNULL, timeout=30, env={})
os.close(reader)
os.close(writer)
check('pipe', result.returncode == 64, f'status {result.returncode}')

# The test build's escapes after the confinement: killed by SIGSYS, nothing written.
# (ws168-p004: an open is refused with EACCES rather than fatal, so that the shared libpdf goes without a substitute
# font file; the test build then says what came of it and ends with 70.)
status, data = run(escape_program, os.path.join(folder, 'a.png'), '--width=64', '--height=64', '--test-escape=open')
check('escape open', status == 70 and data == b'ESCAPED result=-1 errno=13\n', f'status {status} output {data[:40]!r}')
for name in ('socket', 'fork'):
    status, data = run(escape_program, os.path.join(folder, 'a.png'), '--width=64', '--height=64', '--test-escape=' + name)
    check('escape ' + name, status == -signal.SIGSYS and data == b'', f'status {status} output {data[:40]!r}')

# The test build without an escape works as the real one.
status, data = run(escape_program, os.path.join(folder, 'a.png'), '--width=64', '--height=64')
check('escape-build plain', status == 0 and data.startswith(b'P6\n'), f'status {status}')

print('host-preview: ' + ('PASS' if failures == 0 else f'FAIL {failures}'))
sys.exit(1 if failures else 0)

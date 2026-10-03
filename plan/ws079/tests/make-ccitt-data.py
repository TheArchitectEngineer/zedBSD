#!/usr/bin/env python3
# ws079-p015: writes the CCITT fax test data of run-pdf-ccitt.sh.  The bitmaps are made here; the coding is ghostscript's
# CCITTFaxEncode filter (an implementation independent of libpdf's decoder), so a table or mode the decoder gets wrong
# does not cancel out.  Nothing is committed.
#
#   OUT/cases/NAME.bin    the coded data
#   OUT/cases/NAME.raw    the bitmap it must decode to (packed rows, 0 black unless BlackIs1)
#   OUT/cases.txt         one line a case: NAME K COLUMNS ROWS ENDOFLINE BYTEALIGN ENDOFBLOCK BLACKIS1 HEIGHT
#                         (ROWS is the /Rows given to the decoder, 0 for none; HEIGHT the rows the bitmap has)
#   OUT/ccitt.pdf         pages of the same data as image XObjects, stencil masks and inline images (/F /CCF), for
#                         pdftoppm and host-pdf-render
#
# The bitmaps: runs (every run length's terminating and make-up code, and the long make-up codes, in both colours,
# 2600 wide), noise (random pixels and blocks, 203 wide: every vertical offset and the pass mode), text (a scanned-
# looking page of text, 1275 wide), and edge (rows all white, all black, one pixel at each end, 1001 wide).  The codings:
# K -1 and 0, each with and without end-of-line codes, byte alignment and the end-of-block code, K 4 with end-of-line
# codes (see below), and BlackIs1 on some.
#
#   python3 plan/ws079/tests/make-ccitt-data.py OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import io
import os
import random
import subprocess
import sys
import zlib

from PIL import Image, ImageDraw, ImageFont

out = sys.argv[1]
cases_dir = os.path.join(out, 'cases')
os.makedirs(cases_dir, exist_ok=True)
random.seed(15)


def pack(rows, width, black_is_1):
    """Packs rows of 0 (white) / 1 (black) into bytes, padding bits white."""
    data = bytearray()
    for row in rows:
        bits = list(row) + [0] * ((-width) % 8)
        for start in range(0, len(bits), 8):
            value = 0
            for bit in bits[start:start + 8]:
                black = bit == 1
                value = (value << 1) | (1 if black == black_is_1 else 0)
            data.append(value)
    return bytes(data)


def runs_rows(width):
    """Rows of alternating runs whose lengths walk through every code."""
    lengths = list(range(0, 64)) + [m + t for m in range(64, 1729, 64) for t in (0, 1, 37, 63)]
    lengths += [m + t for m in range(1792, 2561, 64) for t in (0, 5, 63)]
    rows = []
    color = 0
    row = []
    for length in lengths:
        for _ in range(length):
            row.append(color)
            if len(row) == width:
                rows.append(row)
                row = []
        color = 1 - color
    if row:
        rows.append(row + [color] * (width - len(row)))
    # A row of each colour whole (2600 = 2560 + 40), and runs just past 2560 split by one pixel.
    rows.append([0] * width)
    rows.append([1] * width)
    rows.append([0] * 2580 + [1] + [0] * (width - 2581))
    rows.append([1] * 2561 + [0] * (width - 2561))
    return rows


def noise_rows(width, height):
    rows = []
    for y in range(height):
        if y < height // 3:
            rows.append([random.randint(0, 1) for _ in range(width)])
        elif y < 2 * height // 3:
            # Blocks whose edges move by a few pixels a row: vertical offsets and pass modes.
            row = [0] * width
            for block in range(0, width, 29):
                shift = (y * (block % 7 + 1)) % 9 - 4
                start = max(0, min(width, block + 5 + shift))
                end = max(0, min(width, block + 12 + ((y + block) % 5) - 2))
                for x in range(start, end):
                    row[x] = 1
            rows.append(row)
        else:
            rows.append([1 if random.random() < 0.08 else 0 for _ in range(width)])
    return rows


def text_rows(width, height):
    image = Image.new('1', (width, height), 1)
    draw = ImageDraw.Draw(image)
    font = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf', 26)
    small = ImageFont.truetype('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf', 17)
    draw.text((90, 60), 'CCITT Group 4 test page', font=font, fill=0)
    words = ('scanned page fax image decoder reference row changing element pass horizontal vertical mode '
             'white black run length terminating make-up code end of line block alignment').split()
    y = 130
    while y < height - 60:
        line = ' '.join(random.choice(words) for _ in range(12))
        draw.text((90, y), line, font=small, fill=0)
        y += 26
    draw.rectangle((60, 40, width - 60, height - 30), outline=0, width=3)
    draw.ellipse((width - 260, 50, width - 90, 220), outline=0, width=5)
    pixels = image.load()
    return [[1 - (1 if pixels[x, y] else 0) for x in range(width)] for y in range(height)]


def edge_rows(width):
    rows = [[0] * width, [1] * width, [1] + [0] * (width - 1), [0] * (width - 1) + [1],
            [1] + [0] * (width - 2) + [1], [0, 1] * (width // 2) + [0] * (width % 2),
            [1, 1, 0] * (width // 3) + [1] * (width % 3), [0] * width, [1] * width]
    return rows * 3


def encode(name, raw, width, rows_count, k, eol, align, eob, black_is_1, give_rows):
    """Codes packed rows with ghostscript's CCITTFaxEncode."""
    raw_path = os.path.join(cases_dir, name + '.input')
    bin_path = os.path.join(cases_dir, name + '.bin')
    with open(raw_path, 'wb') as handle:
        handle.write(raw)
    params = '/K %d /Columns %d /EndOfLine %s /EncodedByteAlign %s /EndOfBlock %s /BlackIs1 %s' % (
        k, width, 'true' if eol else 'false', 'true' if align else 'false', 'true' if eob else 'false',
        'true' if black_is_1 else 'false')
    if give_rows:
        params += ' /Rows %d' % rows_count
    program = ('/infile (%s) (r) file def\n'
               '/outfile (%s) (w) file << %s >> /CCITTFaxEncode filter def\n'
               '/buf 65536 string def\n'
               '{ infile buf readstring exch outfile exch writestring not { exit } if } loop\n'
               'outfile closefile\n') % (raw_path, bin_path, params)
    subprocess.run(['gs', '-q', '-dNOSAFER', '-dNODISPLAY', '-dBATCH', '-'], input=program.encode(), check=True)
    os.remove(raw_path)
    with open(bin_path, 'rb') as handle:
        return handle.read()


images = [
    ('runs', 2600, runs_rows(2600)),
    ('noise', 203, noise_rows(203, 150)),
    ('text', 1275, text_rows(1275, 1650)),
    ('edge', 1001, edge_rows(1001)),
]
codings = []
for k in (-1, 0, 4):
    for eol in (False, True):
        for align in (False, True):
            for eob in (True, False):
                # Mixed data (K > 0) without end-of-line codes: ghostscript writes no tag bits then (a 1D row every K
                # rows), while poppler, pdf.js and libpdf read a tag after each row as after an end-of-line.  Real
                # mixed data comes with end-of-line codes (T.4), so only those are tested.
                if k > 0 and not eol:
                    continue
                codings.append((k, eol, align, eob))

manifest = []
coded = {}
for image_name, width, rows in images:
    packed = {False: pack(rows, width, False), True: pack(rows, width, True)}
    for index, (k, eol, align, eob) in enumerate(codings):
        black_is_1 = index % 3 == 2
        give_rows = index % 2 == 0
        # Without /Rows and without the end-of-block code the decoder cannot tell where the rows end past the data;
        # the height still bounds the output, as a PDF image's /Height does.
        name = '%s-k%d-%s%s%s%s' % (image_name, k, 'eol-' if eol else '', 'align-' if align else '',
                                     'eob' if eob else 'noeob', '-black1' if black_is_1 else '')
        raw = packed[black_is_1]
        data = encode(name, raw, width, len(rows), k, eol, align, eob, black_is_1, give_rows)
        with open(os.path.join(cases_dir, name + '.raw'), 'wb') as handle:
            handle.write(raw)
        manifest.append('%s %d %d %d %d %d %d %d %d' % (name, k, width, len(rows) if give_rows else 0, eol, align, eob,
                                                       black_is_1, len(rows)))
        coded[(image_name, k, eol, align, eob)] = (data, width, len(rows), give_rows, black_is_1)
with open(os.path.join(out, 'cases.txt'), 'w') as handle:
    handle.write('\n'.join(manifest) + '\n')


# The document: image XObjects, a stencil mask, BlackIs1 with /Decode, and inline images.
class Document:
    def __init__(self):
        self.objects = []

    def add(self, body):
        self.objects.append(body)
        return len(self.objects)

    def reserve(self):
        self.objects.append(None)
        return len(self.objects)

    def set(self, number, body):
        self.objects[number - 1] = body

    def write(self, path, root):
        stream = io.BytesIO()
        stream.write(b'%PDF-1.7\n%\xe2\xe3\xcf\xd3\n')
        offsets = []
        for number, body in enumerate(self.objects, 1):
            offsets.append(stream.tell())
            stream.write(b'%d 0 obj\n' % number + body + b'\nendobj\n')
        xref = stream.tell()
        stream.write(b'xref\n0 %d\n0000000000 65535 f \n' % (len(self.objects) + 1))
        for offset in offsets:
            stream.write(b'%010d 00000 n \n' % offset)
        stream.write(b'trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(self.objects) + 1, root, xref))
        with open(path, 'wb') as handle:
            handle.write(stream.getvalue())


def parms(k, width, rows, eol, align, eob, black_is_1, give_rows):
    text = b'/K %d /Columns %d' % (k, width)
    if give_rows:
        text += b' /Rows %d' % rows
    if eol:
        text += b' /EndOfLine true'
    if align:
        text += b' /EncodedByteAlign true'
    if not eob:
        text += b' /EndOfBlock false'
    if black_is_1:
        text += b' /BlackIs1 true'
    return text


doc = Document()
catalog = doc.reserve()
pages_number = doc.reserve()
page_numbers = []


def image(key, extra=b''):
    data, width, rows, give_rows, black_is_1 = coded[key]
    _, k, eol, align, eob = key
    decode = b''
    if black_is_1:
        decode = b' /Decode [1 0]'
    return doc.add(b'<< /Type /XObject /Subtype /Image /Width %d /Height %d /BitsPerComponent 1 /ColorSpace /DeviceGray'
                   b'%s /Filter /CCITTFaxDecode /DecodeParms << %s >> /Length %d%s >>\nstream\n' % (
                       width, rows, decode, parms(k, width, rows, eol, align, eob, black_is_1, give_rows), len(data),
                       extra) + data + b'\nendstream')


def add_page(content, xobjects, size):
    body = zlib.compress(content, 9)
    contents = doc.add(b'<< /Length %d /Filter /FlateDecode >>\nstream\n' % len(body) + body + b'\nendstream')
    names = b' '.join(b'/%s %d 0 R' % (name, number) for name, number in xobjects)
    page_numbers.append(doc.add(b'<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Resources << /XObject << %s >> >>'
                                b' /Contents %d 0 R >>' % (pages_number, size[0], size[1], names, contents)))


# Page 1: the text page in Group 4, full page.
text_image = image(('text', -1, False, False, True))
add_page(b'q 612 0 0 792 0 0 cm /Im1 Do Q', [(b'Im1', text_image)], (612, 792))

# Page 2: the noise in each coding, and the text as a red stencil mask in Group 3 mixed with end-of-line codes.
xobjects = []
content = b''
x = 20
y = 600
for index, (k, eol, align, eob) in enumerate(codings):
    key = ('noise', k, eol, align, eob)
    number = image(key)
    name = b'N%d' % index
    xobjects.append((name, number))
    content += b'q 101.5 0 0 75 %d %d cm /%s Do Q\n' % (x, y, name)
    x += 115
    if x > 520:
        x = 20
        y -= 90
mask_data, mask_width, mask_rows, mask_give, mask_black = coded[('text', 4, True, False, True)]
mask_decode = b' /Decode [1 0]' if mask_black else b''
mask = doc.add(b'<< /Type /XObject /Subtype /Image /Width %d /Height %d /ImageMask true%s /Filter /CCITTFaxDecode'
               b' /DecodeParms << %s >> /Length %d >>\nstream\n' % (
                   mask_width, mask_rows, mask_decode, parms(4, mask_width, mask_rows, True, False, True, mask_black,
                                                             mask_give), len(mask_data)) + mask_data + b'\nendstream')
xobjects.append((b'M1', mask))
content += b'q 0.8 0.1 0.1 rg 170 0 0 220 400 20 cm /M1 Do Q\n'
add_page(content, xobjects, (612, 792))

# Page 3: inline images, abbreviated keys: Group 4, Group 3 one-dimensional with end-of-line codes and alignment,
# Group 3 mixed with BlackIs1 and /D, and a stencil mask.
inline = b''
for index, (k, eol, align, eob, is_mask) in enumerate(
        ((-1, False, False, True, False), (0, True, True, True, False), (4, True, False, False, False),
         (-1, False, True, True, True))):
    data, width, rows, give_rows, black_is_1 = coded[('noise', k, eol, align, eob)]
    header = b'BI /W %d /H %d /F /CCF /DP << %s >>' % (width, rows, parms(k, width, rows, eol, align, eob, black_is_1,
                                                                          give_rows))
    if is_mask:
        header += b' /IM true'
        if black_is_1:
            header += b' /D [1 0]'
    else:
        header += b' /BPC 1 /CS /G'
        if black_is_1:
            header += b' /D [1 0]'
    fill = b'0.1 0.3 0.8 rg ' if is_mask else b''
    inline += b'q %s203 0 0 150 %d %d cm %s ID ' % (fill, 30 + (index % 2) * 280, 420 - (index // 2) * 200, header)
    inline += data + b'\nEI Q\n'
page_numbers.append(0)
body = zlib.compress(inline, 9)
contents = doc.add(b'<< /Length %d /Filter /FlateDecode >>\nstream\n' % len(body) + body + b'\nendstream')
page_numbers[-1] = doc.add(b'<< /Type /Page /Parent %d 0 R /MediaBox [0 0 560 620] /Contents %d 0 R >>' % (
    pages_number, contents))

doc.set(pages_number, b'<< /Type /Pages /Kids [%s] /Count %d >>' % (
    b' '.join(b'%d 0 R' % number for number in page_numbers), len(page_numbers)))
doc.set(catalog, b'<< /Type /Catalog /Pages %d 0 R >>' % pages_number)
doc.write(os.path.join(out, 'ccitt.pdf'), catalog)
print('make-ccitt-data: %d cases, ccitt.pdf with %d pages' % (len(manifest), len(page_numbers)))

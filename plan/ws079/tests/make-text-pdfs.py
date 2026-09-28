#!/usr/bin/env python3
# ws079-p007: writes the text test documents of run-pdf-text.sh from the host's TrueType fonts (Liberation, DejaVu,
# Droid Sans Fallback under /usr/share/fonts; nothing is committed).  Each document has a classic xref table and
# Flate-compressed streams.
#
#   text-simple.pdf  simple TrueType fonts embedded (/FontFile2): WinAnsiEncoding, /Differences over it,
#                    MacRomanEncoding, a symbolic font with only a (3,0) map, a font with only a (1,0) map; the text
#                    state operators (Tc Tw Tz TL T* ' " Ts TJ), rendering modes 0-3 and 7 (a clip), rotated text,
#                    text state kept by q/Q, text inside a form XObject
#   text-cid.pdf     Type0 fonts with a CIDFontType2 program without a cmap: Identity-H with CIDToGIDMap /Identity,
#                    Identity-H with a CIDToGIDMap stream (CID = Unicode) and /W in both forms, Identity-V (CJK)
#   text-std14.pdf   the standard 14 fonts not embedded (Helvetica, Times, Courier and their bold and italic), with
#                    /Widths from the Liberation fonts' metrics, and one Helvetica without /Widths
#
#   python3 plan/ws079/tests/make-text-pdfs.py OUTDIR
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import io
import sys
import zlib

from fontTools.ttLib import TTFont
from fontTools.ttLib.tables._c_m_a_p import cmap_format_4, cmap_format_6

LIBERATION = '/usr/share/fonts/truetype/liberation/'
DEJAVU = '/usr/share/fonts/truetype/dejavu/'
DROID = '/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf'


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

    def stream(self, data, extra=b'', compress=True):
        if compress:
            data = zlib.compress(data, 9)
            extra += b' /Filter /FlateDecode'
        return self.add(b'<< /Length %d%s >>\nstream\n' % (len(data), extra) + data + b'\nendstream')

    def write(self, path, root):
        out = io.BytesIO()
        out.write(b'%PDF-1.7\n%\xe2\xe3\xcf\xd3\n')
        offsets = []
        for number, body in enumerate(self.objects, 1):
            offsets.append(out.tell())
            out.write(b'%d 0 obj\n' % number + body + b'\nendobj\n')
        xref = out.tell()
        out.write(b'xref\n0 %d\n0000000000 65535 f \n' % (len(self.objects) + 1))
        for offset in offsets:
            out.write(b'%010d 00000 n \n' % offset)
        out.write(b'trailer\n<< /Size %d /Root %d 0 R >>\nstartxref\n%d\n%%%%EOF\n' % (len(self.objects) + 1, root, xref))
        with open(path, 'wb') as f:
            f.write(out.getvalue())


def font_bytes(font):
    buffer = io.BytesIO()
    font.save(buffer)
    return buffer.getvalue()


def pdf_string(data):
    out = b'('
    for byte in data:
        if byte in b'()\\':
            out += b'\\' + bytes([byte])
        elif byte < 32 or byte > 126:
            out += b'\\%03o' % byte
        else:
            out += bytes([byte])
    return out + b')'


def hex_string(data):
    return b'<' + data.hex().encode() + b'>'


def widths_for(font, codes_to_glyphs):
    scale = 1000.0 / font['head'].unitsPerEm
    hmtx = font['hmtx']
    return [round(hmtx[glyph][0] * scale) if glyph else 0 for glyph in codes_to_glyphs]


def encoding_glyphs(font, decode):
    """The glyph of each code 0..255 of a font whose codes decode (bytes -> str) by an encoding."""
    cmap = font.getBestCmap()
    glyphs = []
    for code in range(256):
        try:
            character = decode(bytes([code]))
        except UnicodeDecodeError:
            glyphs.append(None)
            continue
        glyphs.append(cmap.get(ord(character)) if code >= 32 else None)
    return glyphs


def descriptor(doc, font, name, flags, program=None):
    head = font['head']
    scale = 1000.0 / head.unitsPerEm
    box = b'[%d %d %d %d]' % (head.xMin * scale, head.yMin * scale, head.xMax * scale, head.yMax * scale)
    body = (b'<< /Type /FontDescriptor /FontName /' + name + b' /Flags %d /FontBBox ' % flags + box +
            b' /ItalicAngle 0 /Ascent %d /Descent %d /CapHeight 700 /StemV 80' %
            (font['hhea'].ascent * scale, font['hhea'].descent * scale))
    if program is not None:
        body += b' /FontFile2 %d 0 R' % doc.stream(program, b' /Length1 %d' % len(program))
    return doc.add(body + b' >>')


def simple_font(doc, font, name, flags, encoding, glyphs, embed=True):
    program = font_bytes(font) if embed else None
    widths = widths_for(font, glyphs[32:256])
    fd = descriptor(doc, font, name, flags, program)
    body = (b'<< /Type /Font /Subtype /TrueType /BaseFont /' + name + b' /FirstChar 32 /LastChar 255 /Widths [' +
            b' '.join(b'%d' % w for w in widths) + b'] /FontDescriptor %d 0 R' % fd)
    if encoding is not None:
        body += b' /Encoding ' + encoding
    return doc.add(body + b' >>')


def page(doc, parent, content, fonts, xobjects=b'', size=(420, 595)):
    resources = b'<< /Font << ' + b' '.join(b'/%s %d 0 R' % (key, value) for key, value in fonts) + b' >>' + xobjects + b' >>'
    contents = doc.stream(content)
    return doc.add(b'<< /Type /Page /Parent %d 0 R /MediaBox [0 0 %d %d] /Resources ' % (parent, size[0], size[1]) +
                   resources + b' /Contents %d 0 R >>' % contents)


def finish(doc, parent, pages, path):
    doc.set(parent, b'<< /Type /Pages /Kids [' + b' '.join(b'%d 0 R' % p for p in pages) + b'] /Count %d >>' % len(pages))
    catalog = doc.add(b'<< /Type /Catalog /Pages %d 0 R >>' % parent)
    doc.write(path, catalog)


def make_simple(path):
    doc = Document()
    parent = doc.reserve()

    # F1: DejaVu Sans, WinAnsiEncoding.
    dejavu = TTFont(DEJAVU + 'DejaVuSans.ttf')
    f1 = simple_font(doc, dejavu, b'DejaVuSans', 32, b'/WinAnsiEncoding', encoding_glyphs(dejavu, lambda b: b.decode('cp1252')))

    # F2: Liberation Serif, WinAnsi with /Differences (Greek letters at A B C, ligatures at a b).
    serif = TTFont(LIBERATION + 'LiberationSerif-Regular.ttf')
    serif_cmap = serif.getBestCmap()
    glyphs2 = encoding_glyphs(serif, lambda b: b.decode('cp1252'))
    for code, character in ((65, 0x3B1), (66, 0x3B2), (67, 0x3B3), (97, 0xFB01), (98, 0xFB02)):
        glyphs2[code] = serif_cmap.get(character)
    f2 = simple_font(doc, serif, b'LiberationSerif', 34,
                     b'<< /Type /Encoding /BaseEncoding /WinAnsiEncoding /Differences [65 /alpha /beta /gamma 97 /fi /fl] >>',
                     glyphs2)

    # F3: Liberation Sans, MacRomanEncoding.
    sans = TTFont(LIBERATION + 'LiberationSans-Regular.ttf')
    f3 = simple_font(doc, sans, b'LiberationSans', 32, b'/MacRomanEncoding', encoding_glyphs(sans, lambda b: b.decode('mac_roman')))

    # F4: a symbolic font with only a (3,0) map at 0xF020..0xF0FF (from Liberation Sans Bold), no /Encoding.
    bold = TTFont(LIBERATION + 'LiberationSans-Bold.ttf')
    bold_cmap = bold.getBestCmap()
    symbol_map = {}
    glyphs4 = [None] * 256
    for code in range(32, 127):
        glyph = bold_cmap.get(code)
        if glyph:
            symbol_map[0xF000 + code] = glyph
            glyphs4[code] = glyph
    table = cmap_format_4(4)
    table.platformID, table.platEncID, table.language, table.cmap = 3, 0, 0, symbol_map
    bold['cmap'].tables = [table]
    f4 = simple_font(doc, bold, b'SymbolicBold', 4, None, glyphs4)

    # F5: a font with only a (1,0) map (format 6, from Liberation Mono), MacRomanEncoding.
    mono = TTFont(LIBERATION + 'LiberationMono-Regular.ttf')
    mono_cmap = mono.getBestCmap()
    mac_map = {}
    glyphs5 = [None] * 256
    for code in range(32, 256):
        try:
            character = bytes([code]).decode('mac_roman')
        except UnicodeDecodeError:
            continue
        glyph = mono_cmap.get(ord(character))
        if glyph:
            mac_map[code] = glyph
            glyphs5[code] = glyph
    table = cmap_format_6(6)
    table.platformID, table.platEncID, table.language, table.cmap = 1, 0, 0, mac_map
    mono['cmap'].tables = [table]
    f5 = simple_font(doc, mono, b'MacOnlyMono', 32 | 1, b'/MacRomanEncoding', glyphs5)

    fonts = [(b'F1', f1), (b'F2', f2), (b'F3', f3), (b'F4', f4), (b'F5', f5)]
    win = 'Hello, World! àéîõü – “quotes” € 1/2'.encode('cp1252')
    mac = 'Mac Roman: àéî † ß π'.encode('mac_roman')
    content = b''.join([
        b'0.95 g 0 0 420 595 re f\n',
        b'BT /F1 18 Tf 0 0 0 rg 24 560 Td ' + pdf_string(win) + b' Tj ET\n',
        b'BT /F2 20 Tf 0 0 0.6 rg 24 530 Td ' + pdf_string(b'ABC ab Serif \xe9t\xe9') + b' Tj ET\n',
        b'BT /F3 16 Tf 0.5 0 0 rg 24 502 Td ' + pdf_string(mac) + b' Tj ET\n',
        b'BT /F4 16 Tf 0 0.4 0 rg 24 476 Td ' + pdf_string(b'Symbolic (3,0) map: Bold 123') + b' Tj ET\n',
        b'BT /F5 14 Tf 0 0 0 rg 24 452 Td ' + pdf_string('Mac (1,0) only: éàü'.encode('mac_roman')) + b' Tj ET\n',
        # Character and word spacing, horizontal scale, leading, T*, ', ".
        b'BT /F1 12 Tf 24 420 Td 14 TL 2 Tc (Character spacing 2) Tj T* 0 Tc 8 Tw (Word spacing eight wide) Tj ',
        b'T* 0 Tw 150 Tz (Horizontal scale 150) Tj 100 Tz (Next line quote) \' 3 1 (Double quote op) " ET\n',
        # Rise, TJ kerning.
        b'BT /F1 14 Tf 24 350 Td (Rise ) Tj 5 Ts (up) Tj -5 Ts (down) Tj 0 Ts ',
        b'[(W) 120 (A) 120 (V) -300 (E) 500 (S)] TJ ET\n',
        # Rendering modes: fill, stroke, fill and stroke, invisible.
        b'BT /F1 28 Tf 1 0 0 RG 0.8 w 0 0.5 1 rg 24 300 Td 0 Tr (Fill) Tj 1 Tr (Stroke) Tj 2 Tr (Both) Tj 3 Tr (Invisible) Tj 0 Tr ET\n',
        # Rotated and skewed text.
        b'q 1 0 0 1 300 150 cm BT /F2 16 Tf 0.866 0.5 -0.5 0.866 0 0 Tm (Rotated 30) Tj ET Q\n',
        b'BT /F1 14 Tf 1 0 0.3 1 24 250 Tm 0.3 0 0.3 rg (Skewed by Tm) Tj ET\n',
        # Text state kept by q/Q.
        b'BT /F1 12 Tf 24 220 Td q 3 Tc /F2 16 Tf (Inside q) Tj Q 0 -18 Td (Back to F1 12) Tj ET\n',
        # Clip mode 7, then stripes through the clip.
        b'q BT /F1 44 Tf 7 Tr 24 150 Td (CLIPPED) Tj ET\n',
        b'1 0 0 rg 0 150 420 8 re f 0 0.6 0 rg 0 162 420 8 re f 0 0 1 rg 0 174 420 8 re f 0.5 0 0.5 rg 0 186 420 8 re f Q\n',
        # A form XObject with text.
        b'q 1 0 0 1 24 60 cm /Fm1 Do Q\n',
    ])
    form = doc.stream(b'BT /F3 18 Tf 0 0 0 rg 0 10 Td (Text in a form XObject) Tj ET 0 0 1 RG 0 0 250 40 re S',
                      b' /Type /XObject /Subtype /Form /BBox [0 0 260 50] /Resources << /Font << /F3 %d 0 R >> >>' % f3)
    page1 = page(doc, parent, content, fonts, b' /XObject << /Fm1 %d 0 R >>' % form)
    finish(doc, parent, [page1], path)


def make_cid(path):
    doc = Document()
    parent = doc.reserve()

    # C1: DejaVu Sans without a cmap, Identity-H, CIDToGIDMap /Identity: the codes are glyph numbers.
    dejavu = TTFont(DEJAVU + 'DejaVuSans.ttf')
    cmap = dejavu.getBestCmap()
    order = dejavu.getGlyphOrder()
    index = {name: number for number, name in enumerate(order)}
    text1 = 'Identity-H, no cmap: Œuvre été Ωλφα Жизнь → ∞'
    gids1 = [index[cmap[ord(c)]] for c in text1]
    del dejavu['cmap']
    program = font_bytes(dejavu)
    scale = 1000.0 / dejavu['head'].unitsPerEm
    w1 = b' '.join(b'%d [%d]' % (g, round(dejavu['hmtx'][order[g]][0] * scale)) for g in sorted(set(gids1)))
    fd1 = descriptor(doc, dejavu, b'ABCDEF+DejaVuSans', 32, program)
    cid1 = doc.add(b'<< /Type /Font /Subtype /CIDFontType2 /BaseFont /ABCDEF+DejaVuSans /CIDSystemInfo << /Registry (Adobe) '
                   b'/Ordering (Identity) /Supplement 0 >> /FontDescriptor %d 0 R /DW 1000 /W [' % fd1 + w1 +
                   b'] /CIDToGIDMap /Identity >>')
    c1 = doc.add(b'<< /Type /Font /Subtype /Type0 /BaseFont /ABCDEF+DejaVuSans /Encoding /Identity-H /DescendantFonts [%d 0 R] >>' % cid1)

    # C2: Liberation Serif, CID = Unicode through a CIDToGIDMap stream, /W in both forms.
    serif = TTFont(LIBERATION + 'LiberationSerif-Italic.ttf')
    serif_cmap = serif.getBestCmap()
    serif_order = serif.getGlyphOrder()
    serif_index = {name: number for number, name in enumerate(serif_order)}
    text2 = 'CIDToGIDMap stream: Italic æøå 0123456789'
    highest = max(ord(c) for c in text2)
    mapping = bytearray(2 * (highest + 1))
    for c in text2:
        g = serif_index[serif_cmap[ord(c)]]
        mapping[2 * ord(c)] = g >> 8
        mapping[2 * ord(c) + 1] = g & 0xFF
    sscale = 1000.0 / serif['head'].unitsPerEm
    digits = [round(serif['hmtx'][serif_cmap[ord(c)]][0] * sscale) for c in '0123456789']
    others = sorted(set(ord(c) for c in text2 if not c.isdigit()))
    w2 = b'48 57 %d ' % digits[0] + b' '.join(b'%d [%d]' % (u, round(serif['hmtx'][serif_cmap[u]][0] * sscale)) for u in others)
    del serif['cmap']
    fd2 = descriptor(doc, serif, b'GHIJKL+LiberationSerif-Italic', 32 | 64, font_bytes(serif))
    map_stream = doc.stream(bytes(mapping))
    cid2 = doc.add(b'<< /Type /Font /Subtype /CIDFontType2 /BaseFont /GHIJKL+LiberationSerif-Italic /CIDSystemInfo << '
                   b'/Registry (Adobe) /Ordering (Identity) /Supplement 0 >> /FontDescriptor %d 0 R /W [' % fd2 + w2 +
                   b'] /CIDToGIDMap %d 0 R >>' % map_stream)
    c2 = doc.add(b'<< /Type /Font /Subtype /Type0 /BaseFont /GHIJKL+LiberationSerif-Italic /Encoding /Identity-H /DescendantFonts [%d 0 R] >>' % cid2)
    text2_codes = b''.join(bytes([ord(c) >> 8, ord(c) & 0xFF]) for c in text2)

    # C3: Droid Sans Fallback, Identity-V: vertical Japanese.
    droid = TTFont(DROID)
    droid_cmap = droid.getBestCmap()
    droid_order = droid.getGlyphOrder()
    droid_index = {name: number for number, name in enumerate(droid_order)}
    text3 = '縦書きの日本語'
    gids3 = [droid_index[droid_cmap[ord(c)]] for c in text3]
    fd3 = descriptor(doc, droid, b'MNOPQR+DroidSansFallback', 4, font_bytes(droid))
    cid3 = doc.add(b'<< /Type /Font /Subtype /CIDFontType2 /BaseFont /MNOPQR+DroidSansFallback /CIDSystemInfo << /Registry (Adobe) '
                   b'/Ordering (Identity) /Supplement 0 >> /FontDescriptor %d 0 R /DW 1000 /CIDToGIDMap /Identity >>' % fd3)
    c3 = doc.add(b'<< /Type /Font /Subtype /Type0 /BaseFont /MNOPQR+DroidSansFallback /Encoding /Identity-V /DescendantFonts [%d 0 R] >>' % cid3)
    text3_codes = b''.join(bytes([g >> 8, g & 0xFF]) for g in gids3)
    text4 = '横書きも'
    c4_codes = b''.join(bytes([droid_index[droid_cmap[ord(c)]] >> 8, droid_index[droid_cmap[ord(c)]] & 0xFF]) for c in text4)
    cid4 = doc.add(b'<< /Type /Font /Subtype /CIDFontType2 /BaseFont /MNOPQR+DroidSansFallback /CIDSystemInfo << /Registry (Adobe) '
                   b'/Ordering (Identity) /Supplement 0 >> /FontDescriptor %d 0 R /DW 1000 /CIDToGIDMap /Identity >>' % fd3)
    c4 = doc.add(b'<< /Type /Font /Subtype /Type0 /BaseFont /MNOPQR+DroidSansFallback /Encoding /Identity-H /DescendantFonts [%d 0 R] >>' % cid4)

    codes1 = b''.join(bytes([g >> 8, g & 0xFF]) for g in gids1)
    content = b''.join([
        b'1 g 0 0 420 595 re f\n',
        b'BT /C1 14 Tf 0 0 0 rg 20 550 Td ' + hex_string(codes1) + b' Tj ET\n',
        b'BT /C1 22 Tf 0.2 0.2 0.7 rg 20 510 Td [' + hex_string(codes1[:20]) + b' -500 ' + hex_string(codes1[20:40]) + b'] TJ ET\n',
        b'BT /C2 18 Tf 0.5 0 0 rg 20 470 Td 1 Tc ' + hex_string(text2_codes) + b' Tj ET\n',
        b'BT /C4 24 Tf 0 0 0 rg 20 420 Td ' + hex_string(c4_codes) + b' Tj ET\n',
        b'BT /C3 28 Tf 0 0.3 0 rg 360 400 Td ' + hex_string(text3_codes) + b' Tj ET\n',
        b'BT /C1 40 Tf 2 Tr 0 0 1 RG 1 1 0 rg 1.2 w 20 120 Td ' + hex_string(codes1[:16]) + b' Tj ET\n',
    ])
    page1 = page(doc, parent, content, [(b'C1', c1), (b'C2', c2), (b'C3', c3), (b'C4', c4)])
    finish(doc, parent, [page1], path)


def make_std14(path):
    doc = Document()
    parent = doc.reserve()
    specs = [
        (b'Helvetica', 'LiberationSans-Regular.ttf', 32),
        (b'Helvetica-Bold', 'LiberationSans-Bold.ttf', 32),
        (b'Helvetica-Oblique', 'LiberationSans-Italic.ttf', 32 | 64),
        (b'Times-Roman', 'LiberationSerif-Regular.ttf', 34),
        (b'Times-Bold', 'LiberationSerif-Bold.ttf', 34),
        (b'Times-Italic', 'LiberationSerif-Italic.ttf', 34 | 64),
        (b'Times-BoldItalic', 'LiberationSerif-BoldItalic.ttf', 34 | 64),
        (b'Courier', 'LiberationMono-Regular.ttf', 33),
        (b'Courier-Bold', 'LiberationMono-Bold.ttf', 33),
    ]
    fonts = []
    for number, (name, file, flags) in enumerate(specs, 1):
        font = TTFont(LIBERATION + file)
        glyphs = encoding_glyphs(font, lambda b: b.decode('cp1252'))
        widths = widths_for(font, glyphs[32:256])
        body = (b'<< /Type /Font /Subtype /Type1 /BaseFont /' + name + b' /Encoding /WinAnsiEncoding /FirstChar 32 /LastChar 255 /Widths [' +
                b' '.join(b'%d' % w for w in widths) + b'] >>')
        fonts.append((b'S%d' % number, doc.add(body)))
    plain = doc.add(b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>')
    fonts.append((b'P1', plain))
    lines = [b'1 g 0 0 420 595 re f 0 g\n']
    y = 555
    for number, (name, _, _) in enumerate(specs, 1):
        lines.append(b'BT /S%d 15 Tf 20 %d Td (' % (number, y) + name + b': The quick brown fox \xe9\xe8 \x93q\x94 1234) Tj ET\n')
        y -= 30
    lines.append(b'BT /P1 15 Tf 20 %d Td (Helvetica without /Widths: jumps over) Tj ET\n' % y)
    y -= 40
    lines.append(b'BT /S1 11 Tf 20 %d Td 13 TL (A paragraph set in Helvetica with its metrics from /Widths, so that the) Tj T* '
                 b'(line breaks and justification of the document are kept whatever face) Tj T* '
                 b'(draws the glyphs. 0 Tw) Tj T* 2.5 Tw (Justified by word spacing of two and a half points.) Tj ET\n' % y)
    page1 = page(doc, parent, b''.join(lines), fonts)
    finish(doc, parent, [page1], path)


outdir = sys.argv[1]
make_simple(outdir + '/text-simple.pdf')
make_cid(outdir + '/text-cid.pdf')
make_std14(outdir + '/text-std14.pdf')
print('make-text-pdfs: ok')

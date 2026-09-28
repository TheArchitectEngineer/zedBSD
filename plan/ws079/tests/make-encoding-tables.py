#!/usr/bin/env python3
# ws079-p007: writes userland/base/libpdf/encoding.c, the tables of libpdf's simple-font encodings:
# the three base encodings of PDF (StandardEncoding, WinAnsiEncoding and MacRomanEncoding, PDF 1.7 Annex D)
# as the Unicode value of each code, and the glyph names a /Differences array uses, sorted, with their
# Unicode values.  The names are those of the three encodings and those of the Adobe Glyph List
# (BSD-3-Clause, through fontTools.agl) in the Latin, Greek, punctuation, currency, letterlike, arrow,
# mathematical and Latin-ligature ranges; afii names and private-use values are left out.
#   python3 plan/ws079/tests/make-encoding-tables.py > userland/base/libpdf/encoding.c
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import codecs
from fontTools import agl
from fontTools.encodings.StandardEncoding import StandardEncoding
from fontTools.encodings.MacRoman import MacRoman

names = agl.LEGACY_AGL2UV


def unicode_of(name):
    values = names.get(name)
    if values is None or len(values) != 1:
        return 0
    return values[0]


standard = [unicode_of(name) if name != '.notdef' else 0 for name in StandardEncoding]

# WinAnsiEncoding: code page 1252; every unused code above 40 (octal) is the bullet (Annex D note 5),
# 0xA0 is the space and 0xAD the hyphen.
win_ansi = [0] * 256
for code in range(32, 256):
    try:
        win_ansi[code] = ord(bytes([code]).decode('cp1252'))
    except UnicodeDecodeError:
        win_ansi[code] = 0x2022
win_ansi[0x7F] = 0x2022
win_ansi[0xA0] = 0x20
win_ansi[0xAD] = 0x2D

# MacRomanEncoding: the Macintosh Roman names; 0xCA is the space, 0xDB the currency sign, and the
# Apple logo (0xF0) is not in PDF's table.
mac_roman = [0] * 256
for code in range(32, 256):
    name = MacRoman[code]
    if code == 0x7F or name in ('.notdef', 'apple'):
        continue
    if name == 'nbspace':
        mac_roman[code] = 0x20
        continue
    mac_roman[code] = unicode_of(name)

ranges = [(0x20, 0x250), (0x370, 0x400), (0x2000, 0x2300), (0x2500, 0x2600), (0xFB00, 0xFB07)]
table = {}
for name, values in names.items():
    if name.startswith('afii') or len(values) != 1:
        continue
    value = values[0]
    if any(low <= value < high for low, high in ranges):
        table[name] = value
for encoding_names in (StandardEncoding, MacRoman):
    for name in encoding_names:
        value = unicode_of(name)
        if value:
            table[name] = value
table['nbspace'] = 0xA0
table['sfthyphen'] = 0xAD
table['apple'] = 0
del table['apple']


def emit_codes(label, comment, values):
    print('/*')
    for line in comment:
        print(' * ' + line if line else ' *')
    print(' */')
    print('const unsigned short %s[256] = {' % label)
    for row in range(0, 256, 8):
        print('\t' + ', '.join('0x%04x' % value for value in values[row:row + 8]) + ',')
    print('};')
    print()


print('''/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The encodings of libpdf's simple fonts (written by
 * plan/ws079/tests/make-encoding-tables.py; do not edit by hand).
 *
 * A simple font's one-byte codes name glyphs through an encoding: one of
 * the three base encodings of PDF, changed by a /Differences array of glyph
 * names.  The reader turns each code into a Unicode value, which the
 * font's Unicode character map, or a substitute font's, turns into a glyph.
 * The glyph names' values follow the Adobe Glyph List (BSD-3-Clause) for
 * the characters of the three encodings and of the Latin, Greek,
 * punctuation, currency, arrow and mathematical ranges.
 */

#include <stddef.h>

#include <pdf.h>

#include "internal.h"
''')
emit_codes('pdf_encoding_standard', ['StandardEncoding: the Unicode value of each code, 0 for none.',
                                     '', 'It is the encoding of a Latin text font that names no other.'], standard)
emit_codes('pdf_encoding_win_ansi', ['WinAnsiEncoding: the Unicode value of each code, 0 for none.',
                                     '', 'Every unused code above 0x20 is the bullet, as PDF defines.'], win_ansi)
emit_codes('pdf_encoding_mac_roman', ['MacRomanEncoding: the Unicode value of each code, 0 for none.',
                                      '', 'It also turns a character back into the code a Macintosh',
                                      'character map (platform 1, encoding 0) is indexed by.'], mac_roman)
print('''/*
 * The glyph names a /Differences array may use, sorted by name (bytewise),
 * each with its Unicode value.
 */
const struct pdf_glyph_name pdf_glyph_names[] = {''')
for name in sorted(table, key=lambda n: n.encode()):
    print('\t{ "%s", 0x%04x },' % (name, table[name]))
print('''};

/* How many names pdf_glyph_names holds. */
const size_t pdf_glyph_names_count = sizeof(pdf_glyph_names) / sizeof(pdf_glyph_names[0]);''')

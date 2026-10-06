#!/usr/bin/env python3
"""convert.py: the handwriting's templates from the Hershey fonts (ws165-p002, plan/ws165/phase001/phase.md section 4).

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    convert.py MAPPING OUTPUT SHAR...

SHAR is a part of the Usenet distribution of the Hershey fonts (comp.sources.unix volume 4, "hershey", parts 2 to 5),
already uncompressed.  Each part is a shell archive; it is read as text and never run: the files between
"cat << \\SHAR_EOF > 'hersh.xxN'" and "SHAR_EOF" are taken out.  Of the font data (hersh.oc1-4, hersh.or1-4), the
glyphs MAPPING names are written to OUTPUT, one a line, in MAPPING's order:

    U+3042 6000 x,y x,y ... / x,y ...

the code point, the Hershey number, then the strokes, each a run of points (Hershey's coordinates: x to the right and
y down, relative to the glyph's centre), separated by " / ".  A Hershey record is its number in columns 1-5, its
count of pairs in columns 6-8, then the pairs: the first is the left and right side, each other is a point or " R"
(the pen lifted), every coordinate a character less 'R'.  A record goes on over the next lines until it has its pairs.
"""

import re
import sys

BEGIN = re.compile(r"^cat << \\SHAR_EOF > '(hersh\.o[cr][1-4])'$")


def shar_files(text):
    """Gives the font files of a shell archive's text: name -> content."""
    files = {}
    name = None
    lines = []
    for line in text.split('\n'):
        if name is None:
            match = BEGIN.match(line)
            if match:
                name = match.group(1)
                lines = []
            continue
        if line == 'SHAR_EOF':
            files[name] = '\n'.join(lines) + '\n'
            name = None
            continue
        lines.append(line)
    return files


def glyphs(text):
    """Reads Hershey records: number -> list of strokes (lists of (x, y))."""
    found = {}
    rows = text.split('\n')
    index = 0
    while index < len(rows):
        row = rows[index]
        index += 1
        if len(row.strip()) == 0:
            continue
        number = int(row[0:5])
        count = int(row[5:8])
        data = row[8:]
        while len(data) < 2 * count and index < len(rows):
            data += rows[index]
            index += 1
        if len(data) < 2 * count:
            raise ValueError('glyph %d is cut short' % number)
        strokes = []
        stroke = []
        for pair in range(1, count):
            a = data[2 * pair]
            b = data[2 * pair + 1]
            if a == ' ' and b == 'R':
                if stroke:
                    strokes.append(stroke)
                stroke = []
                continue
            stroke.append((ord(a) - ord('R'), ord(b) - ord('R')))
        if stroke:
            strokes.append(stroke)
        found[number] = strokes
    return found


def main(arguments):
    """Writes the templates; the exit status says whether every glyph named was found."""
    if len(arguments) < 4:
        sys.stderr.write('usage: convert.py MAPPING OUTPUT SHAR...\n')
        return 2
    mapping = []
    with open(arguments[1], encoding='ascii') as source:
        for line in source:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            number, code = line.split()
            mapping.append((int(number), int(code, 16)))
    texts = {}
    for path in arguments[3:]:
        with open(path, encoding='latin-1') as source:
            texts.update(shar_files(source.read()))
    found = {}
    for name in sorted(texts):
        found.update(glyphs(texts[name]))
    out = ['# The handwriting templates of Kei (ws165-p002), converted from the Hershey fonts by convert.py.',
           '# The Hershey Fonts were originally created by Dr. A. V. Hershey while working at the U. S. National',
           '# Bureau of Standards.  The format of the font data this was converted from was originally created by',
           '# James Hurt, Cognition, Inc., 900 Technology Park Drive, Billerica, MA 01821 (mit-eddie!ci-dandelion!hurt).',
           '# A line: the code point, the Hershey number, the strokes (x,y points; y down) separated by " / ".']
    missing = 0
    for number, code in mapping:
        strokes = found.get(number)
        if not strokes:
            sys.stderr.write('convert.py: no glyph %d\n' % number)
            missing += 1
            continue
        parts = [' '.join('%d,%d' % point for point in stroke) for stroke in strokes]
        out.append('U+%04X %d %s' % (code, number, ' / '.join(parts)))
    with open(arguments[2], 'w', encoding='ascii') as target:
        target.write('\n'.join(out) + '\n')
    sys.stderr.write('convert.py: %d templates, %d missing\n' % (len(mapping) - missing, missing))
    if missing:
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

#!/usr/bin/env python3
"""Plans the pointer's taps that type a text on the on-screen keyboard's QWERTY panel (ws102-p006).

    qwerty-plan.py LOG TEXT MILLISECONDS

LOG is zdesktop's log with the panel's key places (ZWL OSK qrect face=letters|symbols ...), the latest of each face
taken; the panel is taken to show the letters face with Shift off.  The output is the arguments of qmp-pointer.py
(move x y sleep ms down sleep ms up sleep ms ...) for every tap: the other face's key when the character is on the
other face, Shift before a character that needs it, then the character's key, each at the middle of its key, spread
evenly over MILLISECONDS.  A character no key types stops the plan with an error.
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import re
import sys

LINE = re.compile(r'ZWL OSK qrect face=(\w+) row=(\d+) index=(\d+) x=(-?\d+) y=(-?\d+) width=(\d+) height=(\d+) label=(.*)$')

# What each key types without and with Shift, by face and label (keyboard-layout.c): the letters' rows.
DIGITS = dict(zip('1234567890', '!@#$%^&*()'))


def keys_of(path):
    """Reads the latest place of every key of each face: {face: {label: (x, y)}}."""
    faces = {}
    with open(path, encoding='utf-8', errors='replace') as log:
        for line in log:
            found = LINE.search(line.rstrip('\n'))
            if not found:
                continue
            face, _, _, x, y, width, height, label = found.groups()
            faces.setdefault(face, {})[label] = (int(x) + int(width) // 2, int(y) + int(height) // 2)
    return faces


def where(character, faces):
    """Finds a character's key: (face, label, shift) or None."""
    letters = faces.get('letters', {})
    symbols = faces.get('symbols', {})
    if character == ' ':
        return ('letters', 'space', False)
    if character == '\n':
        return ('letters', 'Enter', False)
    if character.islower() and character in letters:
        return ('letters', character, False)
    if character.isupper() and character.lower() in letters:
        return ('letters', character.lower(), True)
    if character in letters:
        return ('letters', character, False)
    for digit, shifted in DIGITS.items():
        if character == shifted:
            return ('letters', digit, True)
    if character in symbols:
        return ('symbols', character, False)
    return None


def main():
    faces = keys_of(sys.argv[1])
    text = sys.argv[2]
    total = int(sys.argv[3])
    taps = []
    face = 'letters'
    for character in text:
        found = where(character, faces)
        if found is None:
            sys.exit(f'qwerty-plan: no key types {character!r}')
        want, label, shift = found
        if want != face:
            taps.append(faces[face]['?123' if face == 'letters' else 'ABC'])
            face = want
        if shift:
            taps.append(faces['letters']['Shift'])
        taps.append(faces[face][label])
    step = max(60, total // len(taps))
    words = []
    for x, y in taps:
        words += ['move', str(x), str(y), 'sleep', '10', 'down', 'sleep', '30', 'up', 'sleep', str(step - 40)]
    print(' '.join(words))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""ws075-p016: the dark intervals of the HDMI output from a watch log of h4-ctl.py.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    h4-blank.py OUTDIR       (reads OUTDIR/watch.log and OUTDIR/kernel.log)

The output is dark while pipe B's transcoder or its primary plane is off, and black while the plane scans out a
resident buffer that was cleared when the display was lit and nothing has been drawn into yet: after a modeset
the plane shows buffer A (cleared) until the first flip to B.  Every interval is printed with its start and length
(milliseconds since the watch began, and the watch's sampling interval bounds the precision).  A real monitor adds
its own time to find the signal again after the transcoder was off; that is not in these numbers.
"""
import re
import sys

BUFFERS = re.compile(r'resident display: buffers \d+x\d+ pitch \d+: A surf 0x([0-9a-f]+) cpu 0x[0-9a-f]+, '
                     r'B surf 0x([0-9a-f]+)')


def main():
    out = sys.argv[1]
    surfaces_a = set()
    with open(f'{out}/kernel.log', errors='replace') as log:
        for found in BUFFERS.finditer(log.read()):
            surfaces_a.add(int(found.group(1), 16) & 0xfffff000)

    # The samples: time, transcoder on, plane on, live surface.
    samples = []
    interval = None
    with open(f'{out}/watch.log') as log:
        for line in log:
            if line.startswith('# watch'):
                found = re.search(r'every (\d+) ms', line)
                interval = int(found.group(1)) if found else None
                continue
            if line.startswith('#'):
                continue
            words = line.split()
            transconf, plane_ctl, surflive = (int(w, 16) for w in words[1:4])
            samples.append((int(words[0]), transconf >> 31, plane_ctl >> 31, surflive & 0xfffff000))

    # Walks the changes: dark (off), black (the cleared A before the first flip of a lighting), or a picture.
    intervals = []
    state = None
    since = 0
    fresh = False
    for time_ms, transcoder_on, plane_on, live in samples:
        if not transcoder_on or not plane_on:
            now = 'dark'
            fresh = True
        elif fresh and live in surfaces_a:
            now = 'black'
        else:
            now = 'picture'
            fresh = False
        if now != state:
            if state is not None:
                intervals.append((state, since, time_ms))
            state, since = now, time_ms
    if state is not None:
        intervals.append((state, since, None))

    print(f'sampling every {interval} ms; buffer A surfaces {[hex(s) for s in sorted(surfaces_a)]}')
    for state, start, end in intervals:
        length = 'open' if end is None else f'{end - start} ms'
        print(f'{start:8d} ms  {state:8s} {length}')

    # The blank spans between two pictures: every dark and black interval in between, summed.
    spans = []
    current = None
    for state, start, end in intervals:
        if state == 'picture':
            if current is not None:
                spans.append((current, start))
                current = None
        elif current is None:
            current = start
    for start, end in spans:
        print(f'blank between pictures: {start} ms .. {end} ms = {end - start} ms')


if __name__ == '__main__':
    main()

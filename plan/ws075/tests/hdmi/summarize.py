#!/usr/bin/env python3
"""ws075-p023: one line per measure-apps.sh run (OUTDIR/measure.txt): the desktop's and the ten applications' flip rate
and latency median, the compositor's engine share and time per run (the busiest session of engine-gdb.sh), the engine
total.  Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    summarize.py OUTDIR...
"""
import re
import sys


def summary(path):
    text = open(path + '/measure.txt').read()
    parts = text.split('== ten applications')
    desktop, apps = parts[0], parts[1] if len(parts) > 1 else ''
    apps, engine = (apps.split('== engine') + [''])[:2]

    def rate(t):
        m = re.search(r'rate: \d+ flips in [\d.]+ s \(([\d.]+)/s\)', t)
        return m.group(1) if m else '-'

    def median(t):
        m = re.search(r'median ([\d.]+) ms, min ([\d.]+), max ([\d.]+)', t)
        return f'{m.group(1)} ({m.group(2)}-{m.group(3)})' if m else '-'
    sessions = [(float(p), float(r), float(ms)) for r, p, ms in
                re.findall(r'([\d.]+) runs/s, engine ([\d.]+)%, ([\d.]+) ms/run', engine)]
    busiest = max(sessions) if sessions else (0.0, 0.0, 0.0)
    total = re.search(r'engine busy ([\d.]+)%', engine)
    return (f'{path}: desktop {rate(desktop)}/s {median(desktop)} ms | apps {rate(apps)}/s {median(apps)} ms | '
            f'compositor {busiest[0]}% {busiest[2]} ms/run {busiest[1]} run/s | engine {total.group(1) if total else "-"}%')


for directory in sys.argv[1:]:
    print(summary(directory))

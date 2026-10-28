#!/usr/bin/env python3
"""ws075-p015: one line per bug085-hw.sh run directory, for the BUG-085 and BUG-094 counts.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    summary.py RUNDIR...

For each run: the timer calibration (the chosen period, and the windows when the kernel logs them), the capture
harness's verdict, the last captured frame, the watcher's marks (stall, fault, poweroff), whether the compositor
ended normally (KWL EXIT error=0), whether egltest finished its scenes, the EGLTEST failures, and how many different
pictures the harness took (the scenes change the picture; a frozen screen repeats one).
"""
import json
import re
import sys
from pathlib import Path


def text(path):
    """A file's text, or nothing when it is missing."""
    try:
        return path.read_text(errors='replace')
    except OSError:
        return ''


def main():
    for name in sys.argv[1:]:
        run = Path(name)
        kernel = text(run / 'kernel.log')
        guest = text(run / 'guest-logs.txt')
        ticks = re.search(r'A64 TIMER CAL READY ticks=(\d+)', kernel)
        windows = re.findall(r'A64 TIMER CAL WINDOW \d+ elapsed=(\d+)', kernel)
        frames = re.findall(r'i915: capture: frame=(\d+)', kernel)
        timecounter = 'unavailable' if 'TIMECOUNTER UNAVAILABLE' in kernel else 'ok'
        started = 'i915 stopped' if 'start stopped at' in kernel else 'i915 ok'
        try:
            result = json.loads(text(run / 'capture' / 'result.json'))
            verdict = result.get('status')
            checks = result.get('checks', {})
            hashes = {v['rgb_sha256'] for k, v in result.get('images', {}).items() if k.startswith('scene')}
        except (ValueError, KeyError):
            verdict, checks, hashes = 'none', {}, set()
        marks = []
        for mark in ('stall', 'fault', 'poweroff'):
            value = text(run / mark).strip()
            if value:
                marks.append(f'{mark}: {value[:90]}')
        exit_line = re.search(r'KWL EXIT frames=(\d+) error=(\d+)', guest)
        failures = re.findall(r'EGLTEST CHECK run=\S+ failures=(\d+)', guest)
        print(f'{run.name}: cal={ticks.group(1) if ticks else "-"} windows={",".join(windows) or "-"} '
              f'timecounter={timecounter} {started} harness={verdict} {checks} '
              f'last_frame={frames[-1] if frames else "-"} pictures={len(hashes)} '
              f'zwl_exit={exit_line.groups() if exit_line else "-"} '
              f'scenes_done={"EGLTEST SCENES DONE" in guest} checks={len(failures)} '
              f'failed={sum(int(f) for f in failures)} | {" | ".join(marks)}')


if __name__ == '__main__':
    main()

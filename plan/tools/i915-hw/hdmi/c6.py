#!/usr/bin/env python3
"""ws075-p024: WS099's criterion C6 (ten windows, the pointer's move to its display, median 50 ms at most, on the
machine) over several measure-apps.sh runs of one image: each run's samples (the "c6 samples:" line of
OUTDIR/measure.txt, h4-ctl.py c6), its median and 90th percentile, then all the samples pooled, the spread of the
runs' medians, and the verdict.

Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib

    c6.py OUTDIR...

A run's median moves with the frame's phase (the flips come every 16.7 ms, and at ten windows the compositor's
frames take several of them), so one run of ten trials says little; the verdict is on the pooled samples of five
or more runs, and the runs' medians show whether the image behaves the same from boot to boot.
"""
import statistics
import sys

LIMIT_MS = 50.0


def percentile(values, fraction):
    """The value at FRACTION of the sorted values (nearest rank)."""
    ordered = sorted(values)
    rank = max(1, int(fraction * len(ordered) + 0.999999))
    return ordered[min(rank, len(ordered)) - 1]


def samples(path):
    """The run's C6 samples in ms, and how many of its trials did not show the cursor."""
    values = []
    missed = 0
    with open(path + '/measure.txt') as text:
        for line in text:
            if line.startswith('c6 samples:'):
                values += [float(v) for v in line.split(':', 1)[1].split()]
            elif line.startswith('c6: '):
                missed += int(line.split(', ')[-1].split()[0]) if 'not shown' in line else 0
    return values, missed


def main():
    pooled = []
    medians = []
    for path in sys.argv[1:]:
        values, missed = samples(path)
        if not values:
            print(f'{path}: no c6 samples')
            continue
        pooled += values
        medians.append(statistics.median(values))
        print(f'{path}: {len(values)} samples, median {statistics.median(values):.1f} ms, '
              f'p90 {percentile(values, 0.9):.1f} ms, min {min(values):.1f}, max {max(values):.1f}, {missed} not shown')
    if not pooled:
        return 1
    median = statistics.median(pooled)
    spread = f'{min(medians):.1f}-{max(medians):.1f} ms'
    deviation = statistics.stdev(medians) if len(medians) > 1 else 0.0
    print(f'pooled: {len(medians)} runs, {len(pooled)} samples, median {median:.1f} ms, p90 {percentile(pooled, 0.9):.1f} ms; '
          f'run medians {spread} (sd {deviation:.1f} ms)')
    verdict = 'met' if median <= LIMIT_MS else 'not met'
    enough = '' if len(medians) >= 5 else ' (fewer than 5 runs: not a verdict)'
    print(f'C6 (pooled median <= {LIMIT_MS:.0f} ms): {verdict}{enough}')
    return 0


if __name__ == '__main__':
    sys.exit(main())

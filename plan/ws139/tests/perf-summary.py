#!/usr/bin/env python3
"""Turns one perf-run.sh output directory into the ledger's rows (ws139-p001).

    plan/ws139/tests/perf-summary.py OUT

Reads OUT/env.txt, OUT/monitor.out, OUT/c5.out, OUT/latency/latency.txt and OUT/steps.txt; writes OUT/summary.tsv
(id, metric, value, unit, samples, env, image_config, commit) and prints the same rows as a markdown table.  A metric
that could not be taken has the value NA, and the reason is printed under the table.  Exit status 0 when every metric
has a value, 1 otherwise.
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import os
import re
import statistics
import sys

# The C5 kinds, each measured once a round.
C5_KINDS = ('wiseview-open', 'wiseview-close', 'home-open', 'home-close')


def read_lines(path):
    """The lines of a file, or none when it is not there."""
    if not os.path.exists(path):
        return []
    with open(path, encoding='utf-8', errors='replace') as stream:
        return stream.read().splitlines()


def read_env(out):
    """The key: value lines of env.txt."""
    env = {}
    for line in read_lines(os.path.join(out, 'env.txt')):
        if ':' in line:
            key, value = line.split(':', 1)
            env[key.strip()] = value.strip()
    return env


def median(values):
    """The median, or None without values."""
    if not values:
        return None
    return statistics.median(values)


def monitor_metrics(out, reasons):
    """P-03: the compositor's frame cost and rate, and the System Monitor's rate, from monitor.out."""
    lines = read_lines(os.path.join(out, 'monitor.out'))
    frame_ms = []
    draw_ms = []
    frames = 0
    window_ms = 0
    fps = []
    callback_ms = []
    for line in lines:
        compose = re.search(r'KWL PERF compose frames=(\d+) draw_ms=([\d.]+) .* frame_ms=([\d.]+)', line)
        if compose:
            frames += int(compose.group(1))
            draw_ms.append(float(compose.group(2)))
            frame_ms.append(float(compose.group(3)))
            continue
        window = re.search(r'KWL PERF (\d+)ms:', line)
        if window:
            window_ms += int(window.group(1))
            continue
        monitor = re.search(r'ZMON FRAME fps=([\d.]+) .*callback_ms=([\d.]+)', line)
        if monitor:
            fps.append(float(monitor.group(1)))
            callback_ms.append(float(monitor.group(2)))
    if not lines:
        reasons.append('P-03: no monitor.out (the monitor step did not run)')
    rate = frames / (window_ms / 1000.0) if window_ms else None
    return [
        ('P-03', 'compose_frame_ms', median(frame_ms), 'ms', len(frame_ms)),
        ('P-03', 'compose_draw_ms', median(draw_ms), 'ms', len(draw_ms)),
        ('P-03', 'compose_per_s', rate, '1/s', len(frame_ms)),
        ('P-03', 'monitor_fps', median(fps), 'fps', len(fps)),
        ('P-03', 'monitor_callback_ms', median(callback_ms), 'ms', len(callback_ms)),
    ]


def c5_metrics(out, reasons):
    """P-01: the first round's largest first frame, the later rounds' median, and the largest gap, from c5.out."""
    lines = read_lines(os.path.join(out, 'c5.out'))
    seen = {kind: 0 for kind in C5_KINDS}
    round1 = []
    rest = []
    gap_max = None
    for line in lines:
        match = re.match(r'^C5 (\S+)( windows=\d+)?: first_frame_ms=(\S+)', line)
        if match and match.group(1) in seen:
            kind = match.group(1)
            seen[kind] += 1
            value = match.group(3)
            if value == 'None':
                continue
            if seen[kind] == 1:
                round1.append(float(value))
            else:
                rest.append(float(value))
            continue
        result = re.search(r'C5 RESULT .*gap_max=(\d+)', line)
        if result:
            gap_max = float(result.group(1))
    if not lines:
        reasons.append('P-01: no c5.out (the c5 step did not run)')
    return [
        ('P-01', 'c5_first_frame_ms_round1', max(round1) if round1 else None, 'ms', len(round1)),
        ('P-01', 'c5_first_frame_ms_rest', median(rest), 'ms', len(rest)),
        ('P-01', 'c5_gap_ms_max', gap_max, 'ms', 1 if gap_max is not None else 0),
    ]


def latency_metrics(out, reasons):
    """P-02: Text Editor's and Terminal's direct typing latency (median and maximum), from latency/latency.txt."""
    lines = read_lines(os.path.join(out, 'latency', 'latency.txt'))
    found = {}
    for line in lines:
        match = re.search(r'LATENCY name=(\S+) trials=(\d+) median_ms=(\d+) max_ms=(\d+)', line)
        if match:
            found[match.group(1)] = (float(match.group(3)), float(match.group(4)), int(match.group(2)))
    if not lines:
        reasons.append('P-02: no latency/latency.txt (the latency step did not run)')
    rows = []
    for name, metric in (('textedit-direct', 'textedit_direct'), ('terminal-direct', 'terminal_direct')):
        median_ms, max_ms, trials = found.get(name, (None, None, 0))
        rows.append(('P-02', metric + '_ms', median_ms, 'ms', trials))
        rows.append(('P-02', metric + '_max_ms', max_ms, 'ms', trials))
    return rows


def show(value):
    """A value for the table: rounded to two places, or NA."""
    if value is None:
        return 'NA'
    return '%.2f' % value


def main():
    """Reads the run, writes summary.tsv and prints the table."""
    if len(sys.argv) != 2:
        sys.stderr.write('usage: perf-summary.py OUT\n')
        return 2
    out = sys.argv[1]
    env = read_env(out)
    reasons = []
    for line in read_lines(os.path.join(out, 'steps.txt')):
        if 'TIMEOUT' in line:
            reasons.append('step %s' % line)
    rows = monitor_metrics(out, reasons) + c5_metrics(out, reasons) + latency_metrics(out, reasons)
    mark = env.get('env', 'NA')
    config = env.get('image_config', 'NA')
    commit = env.get('commit', 'NA')
    with open(os.path.join(out, 'summary.tsv'), 'w', encoding='utf-8') as stream:
        stream.write('id\tmetric\tvalue\tunit\tsamples\tenv\timage_config\tcommit\n')
        for ident, metric, value, unit, samples in rows:
            stream.write('%s\t%s\t%s\t%s\t%d\t%s\t%s\t%s\n' % (ident, metric, show(value), unit, samples, mark, config, commit))
    print('| id | metric | value | unit | samples |')
    print('| --- | --- | --- | --- | --- |')
    missing = 0
    for ident, metric, value, unit, samples in rows:
        print('| %s | %s | %s | %s | %d |' % (ident, metric, show(value), unit, samples))
        if value is None:
            missing += 1
    print('')
    print('env %s, image %s, commit %s' % (mark, config, commit))
    for reason in reasons:
        print('NA reason: %s' % reason)
    if missing:
        print('perf-summary: %d metric(s) NA' % missing)
        return 1
    print('perf-summary: every metric has a value')
    return 0


if __name__ == '__main__':
    sys.exit(main())

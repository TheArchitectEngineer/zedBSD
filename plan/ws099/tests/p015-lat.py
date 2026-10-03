#!/usr/bin/env python3
# ws099-p015: reads zdesktop's per-frame time lines (ZWL LAT pen/adopt/submit/shown, --log-frames) and prints the
# pen's latency (a pen place sent to the surface -> the end of the frame that shows that surface's next image)
# and the interval between the frames that show a new image of it during a stroke.
#   plan/ws099/tests/p015-lat.py LAT.LOG
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import bisect, re, statistics, sys

pens, adopts, submits, shown_direct, shown = [], {}, [], [], {}
for line in open(sys.argv[1], errors="replace"):
    m = re.match(r"ZWL LAT (\w+) (.*)", line)
    if not m:
        continue
    kind, rest = m.groups()
    f = dict(p.split("=", 1) for p in rest.split() if "=" in p)
    t = int(f["at_us"])
    if kind == "pen":
        pens.append((t, int(f["surface"])))
    elif kind == "adopt":
        adopts.setdefault(int(f["surface"]), []).append(t)
    elif kind == "submit":
        submits.append((t, int(f["frame"])))
    elif kind == "shown":
        if f.get("direct") == "1":
            shown_direct.append(t)
        else:
            shown[int(f["frame"])] = t
if not pens:
    print("no pen lines"); sys.exit(1)
surface = max(set(s for _, s in pens), key=[s for _, s in pens].count)
pen_times = [t for t, s in pens if s == surface]
adopt_times = sorted(adopts.get(surface, []))
submit_times = [t for t, _ in submits]

def shown_after(t1):
    """The end of the first frame that shows an image taken at t1."""
    if shown_direct:
        i = bisect.bisect_left(shown_direct, t1)
        return shown_direct[i] if i < len(shown_direct) else None
    i = bisect.bisect_left(submit_times, t1)
    while i < len(submits):
        frame = submits[i][1]
        if frame in shown:
            return shown[frame]
        i += 1
    return None

lat = []
for t0 in pen_times:
    i = bisect.bisect_right(adopt_times, t0)
    if i >= len(adopt_times):
        continue
    t2 = shown_after(adopt_times[i])
    if t2 is not None and t2 - t0 < 1000000:
        lat.append((t2 - t0) / 1000.0)

# The strokes: pen lines closer than 300 ms to the one before; the frames showing a new image inside each.
strokes, cur = [], [pen_times[0]]
for t in pen_times[1:]:
    if t - cur[-1] > 300000:
        strokes.append(cur); cur = []
    cur.append(t)
strokes.append(cur)
frame_ends = sorted(set(x for x in (shown_after(a) for a in adopt_times) if x is not None))
gaps = []
for s in strokes:
    ends = [e for e in frame_ends if s[0] <= e <= s[-1] + 50000]
    gaps += [(b - a) / 1000.0 for a, b in zip(ends, ends[1:])]

def q(v, p):
    v = sorted(v); return v[min(len(v) - 1, int(p * len(v)))]
mode = "direct" if shown_direct else "composed"
print("mode=%s surface=%d pen_lines=%d strokes=%d" % (mode, surface, len(pen_times), len(strokes)))
if lat:
    print("latency_ms median=%.1f p90=%.1f max=%.1f n=%d" % (statistics.median(lat), q(lat, 0.9), max(lat), len(lat)))
if gaps:
    print("interval_ms median=%.1f p90=%.1f max=%.1f frames=%d" % (statistics.median(gaps), q(gaps, 0.9), max(gaps), len(gaps) + len(strokes)))

#!/usr/bin/env python3
# Decodes the PowerVR firmware trace a HWR dump printed ("powervr: dump: trace" lines).
#   python3 -I fw-trace-decode.py pvr_rogue_fwif_sf.h dmesg.txt
# pvr_rogue_fwif_sf.h: Linux drivers/gpu/drm/imagination (e.g. /home/awe/visionfive-linux).
# The first word is taken from the dump's "trace pointer P, N words from F" line.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re, sys
sf = open(sys.argv[1]).read()
groups = re.findall(r'ROGUE_FW_GROUP_(\w+),', sf.split('};')[0])
gi = {('ROGUE_FW_GROUP_'+g): i for i, g in enumerate(groups)}
tab = {}
for m in re.finditer(r'ROGUE_FW_LOG_CREATESFID\((\d+),\s*(ROGUE_FW_GROUP_\w+),\s*(\d+)\),\s*((?:"[^"]*"\s*)+)', sf):
    a, g, e, s = int(m.group(1)), gi.get(m.group(2), 0), int(m.group(3)), m.group(4)
    s = ''.join(re.findall(r'"([^"]*)"', s))
    tab[(a | (g << 12) | (e << 16))] = (e, s)
words = {}
for line in open(sys.argv[2]):
    m = re.search(r'dump: trace\s+(\d+): ((?:[0-9a-f]{8} ?){8})', line)
    if m:
        b = int(m.group(1))
        for i, w in enumerate(m.group(2).split()):
            words[b + i] = int(w, 16)
start = int(re.search(r"words from (\d+)", open(sys.argv[2]).read()).group(1)); n = 12000
seq = [words.get((start + i) % n, 0) for i in range(1024)]
i = 0
while i < len(seq):
    w = seq[i]
    k = w & 0x0fffffff
    if (w & 0xf0000000) == 0x70000000 and k in tab:
        e, s = tab[k]
        ts = (seq[i+1] << 32 | seq[i+2]) if i + 2 < len(seq) else 0
        args = seq[i+3:i+3+e]
        try:
            txt = re.sub(r'%[-0-9.]*l*[duxX]', '{}', s).format(*[hex(a) for a in args])
        except Exception:
            txt = s + ' ' + ' '.join(hex(a) for a in args)
        print('%10x %s' % (ts, txt))
        i += 3 + e
    else:
        i += 1

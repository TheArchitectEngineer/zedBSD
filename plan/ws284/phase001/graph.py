#!/usr/bin/env python3
"""The YOLOX-Nano graph of plan/ws284/design.md, written out from the official
model source (Megvii-BaseDetection/YOLOX 6ddff482, exps/default/yolox_nano.py:
width 0.25, depth 0.33, depthwise=True) to print the layer table (appendix A),
the parameter and multiply-accumulate counts, and the activation memory.

Usage: python3 -I graph.py [INPUT-SIZE] [table|sum|gpu]   (default 416 table)
gpu prints the version 2 GPU plan (design section 3.4 to 3.6 and 5): the
dispatches after fusion and splitting, the arena, and the DRAM traffic.
Design evidence only; the weight converter of ws284-p002 replaces it.
Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""
import sys
SIZE = int(sys.argv[1]) if len(sys.argv) > 1 else 416
nodes = []   # (name, op, inputs, output, cout, h, w, params, macs, extra)
shape = {}   # tensor -> (c,h,w)
cnt = [0]
def t(name): return name
def conv(name, x, cout, k, s, groups=1, act=True, bias_conv=False):
    c, h, w = shape[x]
    ho, wo = (h + 2*((k-1)//2) - k)//s + 1, (w + 2*((k-1)//2) - k)//s + 1
    params = cout * (c//groups) * k * k + cout
    macs = cout * ho * wo * (c//groups) * k * k
    op = ("dw%dx%d" % (k, k) if groups == c and groups > 1 else "conv%dx%d" % (k, k)) + ("/s%d" % s if s > 1 else "") + ("+silu" if act else "")
    out = name
    shape[out] = (cout, ho, wo)
    nodes.append((name, op, [x], out, params, macs))
    return out
def base(name, x, cout, k, s): return conv(name, x, cout, k, s)
def dwconv(name, x, cout, k, s):
    c = shape[x][0]
    y = conv(name + ".dconv", x, c, k, s, groups=c)
    return conv(name + ".pconv", y, cout, 1, 1)
def add(name, a, b):
    shape[name] = shape[a]; nodes.append((name, "add", [a, b], name, 0, 0)); return name
def cat(name, xs):
    c = sum(shape[x][0] for x in xs); _, h, w = shape[xs[0]]
    shape[name] = (c, h, w); nodes.append((name, "concat", xs, name, 0, 0)); return name
def up(name, x):
    c, h, w = shape[x]; shape[name] = (c, 2*h, 2*w); nodes.append((name, "upsample2x", [x], name, 0, 0)); return name
def pool(name, x, k):
    shape[name] = shape[x]; nodes.append((name, "maxpool%d" % k, [x], name, 0, 0)); return name
def bottleneck(name, x, cout, shortcut):
    y = base(name + ".conv1", x, cout, 1, 1)
    y = dwconv(name + ".conv2", y, cout, 3, 1)
    if shortcut and shape[x][0] == cout: y = add(name + ".add", y, x)
    return y
def csp(name, x, cout, n, shortcut=True):
    hid = cout // 2
    x1 = base(name + ".conv1", x, hid, 1, 1)
    x2 = base(name + ".conv2", x, hid, 1, 1)
    for i in range(n): x1 = bottleneck("%s.m.%d" % (name, i), x1, hid, shortcut)
    y = cat(name + ".cat", [x1, x2])
    return base(name + ".conv3", y, cout, 1, 1)
def spp(name, x, cout):
    hid = shape[x][0] // 2
    y = base(name + ".conv1", x, hid, 1, 1)
    ps = [pool("%s.m.%d" % (name, i), y, k) for i, k in enumerate((5, 9, 13))]
    y = cat(name + ".cat", [y] + ps)
    return base(name + ".conv2", y, cout, 1, 1)
shape["image"] = (3, SIZE, SIZE)
shape["focus"] = (12, SIZE//2, SIZE//2); nodes.append(("focus", "space2depth", ["image"], "focus", 0, 0))
x = base("stem", "focus", 16, 3, 1)
x = dwconv("dark2.0", x, 32, 3, 2); x = csp("dark2.1", x, 32, 1)
x = dwconv("dark3.0", x, 64, 3, 2); d3 = csp("dark3.1", x, 64, 3)
x = dwconv("dark4.0", d3, 128, 3, 2); d4 = csp("dark4.1", x, 128, 3)
x = dwconv("dark5.0", d4, 256, 3, 2); x = spp("dark5.1", x, 256); d5 = csp("dark5.2", x, 256, 1, False)
f0 = base("lateral_conv0", d5, 128, 1, 1)
x = up("up0", f0); x = cat("cat_p4", [x, d4]); x = csp("C3_p4", x, 128, 1, False)
f1 = base("reduce_conv1", x, 64, 1, 1)
x = up("up1", f1); x = cat("cat_p3", [x, d3]); p2 = csp("C3_p3", x, 64, 1, False)
x = dwconv("bu_conv2", p2, 64, 3, 2); x = cat("cat_n3", [x, f1]); p1 = csp("C3_n3", x, 128, 1, False)
x = dwconv("bu_conv1", p1, 128, 3, 2); x = cat("cat_n4", [x, f0]); p0 = csp("C3_n4", x, 256, 1, False)
for k, p in enumerate((p2, p1, p0)):
    s = base("head.stems.%d" % k, p, 64, 1, 1)
    c = dwconv("head.cls_convs.%d.0" % k, s, 64, 3, 1); c = dwconv("head.cls_convs.%d.1" % k, c, 64, 3, 1)
    conv("head.cls_preds.%d" % k, c, 80, 1, 1, act=False)
    r = dwconv("head.reg_convs.%d.0" % k, s, 64, 3, 1); r = dwconv("head.reg_convs.%d.1" % k, r, 64, 3, 1)
    conv("head.reg_preds.%d" % k, r, 4, 1, 1, act=False)
    conv("head.obj_preds.%d" % k, r, 1, 1, 1, act=False)
mode = sys.argv[2] if len(sys.argv) > 2 else "table"
tp = sum(n[4] for n in nodes); tm = sum(n[5] for n in nodes)
if mode == "table":
    for n in nodes:
        ins = ",".join("%dx%dx%d" % shape[i] for i in n[2])
        print("| %s | %s | %s | %dx%dx%d | %d | %.2f |" % (n[0], n[1], ins, *shape[n[3]], n[4], n[5] / 1e6))
print("nodes", len(nodes), "convs", sum(1 for n in nodes if n[1].startswith(("conv","dw"))), "params(folded)", tp, "MMAC", tm/1e6)
import collections
by = collections.Counter(); bym = collections.Counter()
for n in nodes:
    kind = n[1].split("+")[0].split("/")[0]
    by[kind] += 1; bym[kind] += n[5]
for k in by: print(" ", k, by[k], "%.1f MMAC" % (bym[k]/1e6))
# liveness: max sum of live tensor sizes (fp32 bytes)
last = {}
for i, n in enumerate(nodes):
    for x in n[2]: last[x] = i
live = set(); peak = 0
outs = set(n[3] for n in nodes if n[0].startswith("head.") and "preds" in n[0])
for i, n in enumerate(nodes):
    live.add(n[3])
    # The input image lives until op 0 (focus) has read it, the same rule as
    # the aliased count below (review F13: version 1 kept it to the end here).
    cur = sum(shape[x][0]*shape[x][1]*shape[x][2] for x in live | ({"image"} if i == 0 else set()))
    peak = max(peak, cur)
    for x in n[2]:
        if last[x] == i and x not in outs: live.discard(x)
print("peak live activations (elements)", peak, "fp32 MiB %.2f" % (peak*4/2**20))
tot = sum(shape[n[3]][0]*shape[n[3]][1]*shape[n[3]][2] for n in nodes)
print("sum of all tensors (elements)", tot, "fp32 MiB %.2f" % (tot*4/2**20))
big = max(shape[n[3]][0]*shape[n[3]][1]*shape[n[3]][2] for n in nodes); print("largest tensor", big, "%.2f MiB" % (big*4/2**20))
# The same peak when every concat input is a slice of the concat's buffer
# (design section 3.5): the buffer lives from its first producer to its
# last reader.
owner = {}
for n in nodes:
    if n[1] == "concat":
        for x in n[2]: owner[x] = n[3]
def root(x):
    while x in owner: x = owner[x]
    return x
first, final = {"image": 0}, {}
for i, n in enumerate(nodes):
    r = root(n[3]); first.setdefault(r, i); final[r] = max(final.get(r, i), i)
    for x in n[2]:
        r = root(x); final[r] = max(final.get(r, i), i)
for n in nodes:
    if "preds" in n[0]: final[root(n[3])] = len(nodes)
size = lambda x: shape[x][0] * shape[x][1] * shape[x][2]
peak = max(sum(size(r) for r in first if first[r] <= i <= final[r]) for i in range(len(nodes) + 1))
print("peak with concat aliasing (elements)", peak, "fp32 MiB %.2f" % (peak * 4 / 2**20), "buffers", len(first))

# ---------------------------------------------------------------------------
# Version 2 GPU plan (design sections 3.3 to 3.6 and 5).  Tensors are "C4
# flat": ceil(C/4) blocks of HWa pixels of one vec4 (16 bytes), HWa = H*W
# rounded up to 32 so a 1x1 conv may give each lane 2 pixels without a guard.
# Fusion: focus+stem is one 6x6 stride-2 conv on the RGB0 input, the residual
# add is the epilogue of the pointwise conv before it, concat is aliasing,
# reg_preds and obj_preds are one 1x1 conv with 8 outputs, maxpool 9 and 13
# are maxpool 5 of the previous pool, decode is one dispatch per level.
if mode == "gpu":
    import math
    LIMIT = float(sys.argv[3]) * 1e6 if len(sys.argv) > 3 else 8e6   # MAC per dispatch
    hwa = lambda h, w: -(-h * w // 32) * 32
    def c4(c, h, w): return -(-c // 4) * hwa(h, w) * 16
    gshape = dict(shape); gshape["image"] = (4, SIZE, SIZE)
    disp = []   # (name, kind, inputs, output, mac_useful, mac_exec, weight_bytes, pixels)
    byname = {n[0]: n for n in nodes}
    residual = {}
    for i, n in enumerate(nodes):
        if n[1] == "add": residual[n[2][0]] = (n[2][1], n[3])
    pool_src = {}
    disp.append(("preprocess", "pre", [], "image", 0, 0, 0, SIZE * SIZE))
    for n in nodes:
        name, op, ins, out, params, macs = n
        if op in ("space2depth", "add", "concat"): continue
        if name.startswith("head.obj_preds"): continue
        c, h, w = shape[out]
        if name == "stem":
            disp.append((name, "stem", ["image"], out, macs, macs, -(-c // 4) * 36 * 3 * 16 + c * 4, h * w)); continue
        if op.startswith("dw"):
            disp.append((name, "dw-s2" if "/s2" in op else "dw", ins, out, macs, macs, -(-c // 4) * 10 * 16, h * w)); continue
        if op.startswith("conv1x1"):
            cin = shape[ins[0]][0]; cout = c
            if name.startswith("head.reg_preds"):
                cout = 8; gshape[out] = (8, h, w)
            e = -(-cout // 4) * 4 * -(-cin // 4) * 4 * hwa(h, w)
            wb = -(-cout // 4) * -(-cin // 4) * 64 + -(-cout // 4) * 16
            k = "pw+res" if out in residual else "pw"
            # The fused add writes the add's tensor (which a concat may alias).
            res, dst = residual[out] if out in residual else (None, out)
            disp.append((name, k, ins + ([res] if res else []), dst, macs if cout != 8 else macs + 64 * h * w, e, wb, h * w)); continue
        if op.startswith("maxpool"):
            src = {"5": ins[0]}
            prev = {"maxpool5": ins[0], "maxpool9": name[:-1] + "0", "maxpool13": name[:-1] + "1"}[op]
            disp.append((name, "pool5", [prev], out, 0, 0, 0, h * w)); continue
        if op == "upsample2x":
            disp.append((name, "up", ins, out, 0, 0, 0, h * w)); continue
        raise SystemExit("unplanned op " + op)
    for k in range(3):
        lv = {0: "head.cls_preds.0", 1: "head.cls_preds.1", 2: "head.cls_preds.2"}[k]
        _, h, w = shape[lv]
        disp.append(("decode.%d" % k, "decode", [lv, "head.reg_preds.%d" % k], "detections", 0, 0, 0, h * w))
    gsize = lambda x: c4(*gshape[x]) if x in gshape else 0
    total_split = 0; rows = []
    for d in disp:
        n_split = max(1, math.ceil(d[5] / LIMIT)) if d[5] else 1
        total_split += n_split
        rd = sum(gsize(x) for x in d[2]) + d[6]
        wr = gsize(d[3]) if d[3] != "detections" else d[7] * 24
        cin4 = -(-gshape[d[2][0]][0] // 4) if d[1] in ("pw", "pw+res") else 1
        cout4 = -(-gshape[d[3]][0] // 4) if d[1] in ("pw", "pw+res") else 1
        rd_hi = cout4 * gsize(d[2][0]) + (gsize(d[2][1]) if d[1] == "pw+res" else 0) if d[1].startswith("pw") else rd
        w_hi = d[6] * math.ceil(hwa(*gshape[d[3]][1:]) / 16) if d[1].startswith("pw") else d[6]
        rows.append((d, n_split, rd, wr, rd_hi, w_hi))
    if len(sys.argv) > 4 and sys.argv[4] == "list":
        for d, ns, rd, wr, rh, wh in rows:
            print("| %s | %s | %.2f | %.2f | %d | %.2f | %.2f |" % (d[0], d[1], d[4] / 1e6, d[5] / 1e6, ns, (rd + wr) / 1e6, (rh + wr + wh) / 1e6))
    import collections
    kinds = collections.Counter(d[1] for d in disp)
    print("gpu dispatches (fused)", len(disp), dict(kinds))
    print("dispatches after splitting at %.1f MMAC" % (LIMIT / 1e6), total_split)
    print("MMAC useful %.1f executed %.1f" % (sum(d[4] for d in disp) / 1e6, sum(d[5] for d in disp) / 1e6))
    big = sorted(((d[5], d[0]) for d in disp), reverse=True)[:5]
    print("largest dispatches (MMAC executed)", ", ".join("%s %.1f" % (n, m / 1e6) for m, n in big))
    wr = sum(r[3] for r in rows); rd = sum(r[2] for r in rows); rh = sum(r[4] for r in rows); wh = sum(r[5] for r in rows)
    wt = sum(d[6] for d in disp)
    print("DRAM MB: writes %.1f, reads once %.1f (weights once %.2f), total %.1f" % (wr / 1e6, rd / 1e6, wt / 1e6, (wr + rd) / 1e6))
    print("DRAM MB upper: inputs re-read per output block %.1f, weights per 16-pixel batch %.1f, total %.1f" % (rh / 1e6, wh / 1e6, (wr + rh + wh) / 1e6))
    # Arena: first fit of the buffers (concat aliased) over their live ranges, 256-byte aligned.
    owner = {}
    for n in nodes:
        if n[1] == "concat":
            for x in n[2]: owner[x] = n[3]
    def groot(x):
        while x in owner: x = owner[x]
        return x
    def bsize(r):
        if r in gshape and not any(n[1] == "concat" and n[3] == r for n in nodes): return gsize(r)
        parts = [x for x in owner if owner[x] == r]
        return sum(bsize(x) if x not in gshape or any(n[1] == "concat" and n[3] == x for n in nodes) else gsize(x) for x in parts)
    first, final = {}, {}
    for i, d in enumerate(disp):
        if d[3] != "detections":
            r = groot(d[3]); first.setdefault(r, i); final[r] = max(final.get(r, i), i)
        for x in d[2]:
            r = groot(x); first.setdefault(r, i); final[r] = max(final.get(r, i), i)
    placed = []; top = 0
    for r in sorted(first, key=lambda r: first[r]):
        sz = -(-bsize(r) // 256) * 256
        busy = sorted((o, o + s) for (o, s, a, b) in placed if not (b < first[r] or a > final[r]))
        off = 0
        for a, b in busy:
            if off + sz <= a: break
            off = max(off, b)
        placed.append((off, sz, first[r], final[r])); top = max(top, off + sz)
    peak = max(sum(s for (o, s, a, b) in placed if a <= i <= b) for i in range(len(disp)))
    print("arena: live peak %.2f MiB, first fit %.2f MiB, buffers %d" % (peak / 2**20, top / 2**20, len(placed)))
    print("weights (C4 packed) %.2f MiB, decode records %d B" % (wt / 2**20, sum(d[7] for d in disp if d[1] == "decode") * 24))

#!/usr/bin/env python3
"""The YOLOX-Nano graph of plan/ws284/design.md, written out from the official
model source (Megvii-BaseDetection/YOLOX 6ddff482, exps/default/yolox_nano.py:
width 0.25, depth 0.33, depthwise=True) to print the layer table (appendix A),
the parameter and multiply-accumulate counts, and the activation memory.

Usage: python3 -I graph.py [INPUT-SIZE] [table|sum]   (default 416 table)
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
    cur = sum(shape[x][0]*shape[x][1]*shape[x][2] for x in live | {"image"})
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

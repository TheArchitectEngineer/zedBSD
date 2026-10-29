#!/usr/bin/env python3
# Turns the "pcprof:" lines of a zedBSD kernel with the temporary PC sampler (a read of hw.cputimes writes the samples
# since the read before: each CPU's user, kernel and idle ticks, the ticks of each thread (a kernel thread by its entry,
# a user one by its process id plus one), and the busiest 32-byte buckets of the kernel's text on CPU 0 (h0) and on the other
# CPUs (h1)) into functions, with the kernel's symbols (nm).  The last block in the log is used (the read after the run).
#   pcprof-summary.py VMUNIX DMESG
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import bisect
import re
import subprocess
import sys


def symbols(vmunix):
    out = subprocess.run(["nm", "-n", vmunix], capture_output=True, text=True, check=True).stdout
    addrs = []
    names = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "tTwW":
            addrs.append(int(parts[0], 16))
            names.append(parts[2])
    return addrs, names


def lookup(addrs, names, address):
    i = bisect.bisect_right(addrs, address) - 1
    return names[i] if i >= 0 else "?%x" % address


def blocks(path):
    current = None
    found = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"pcprof: (.*)", line)
            if not m:
                continue
            body = m.group(1).strip()
            if body.startswith("begin"):
                current = {"cpu": [], "thread": [], "h0": [], "h1": []}
            elif body == "end":
                if current is not None:
                    found.append(current)
                current = None
            elif current is not None:
                w = body.split()
                if w[0] == "cpu":
                    current["cpu"].append((int(w[1]), int(w[3]), int(w[5]), int(w[7]), int(w[9])))
                elif w[0] == "thread":
                    current["thread"].append((int(w[1], 16), int(w[3]), int(w[5]), int(w[7])))
                elif w[0] in ("h0", "h1"):
                    current[w[0]].append((int(w[1], 16), int(w[2])))
    return found


def main():
    vmunix, dmesg = sys.argv[1], sys.argv[2]
    addrs, names = symbols(vmunix)
    found = blocks(dmesg)
    if not found:
        print("no pcprof block")
        return
    block = found[-1]
    busy = sorted(block["cpu"], key=lambda c: -(c[1] + c[2]))
    print("cpus (user/kern/idle/irqoff ticks): " + ", ".join(
        "cpu%d %d/%d/%d/%d" % c for c in busy if c[1] + c[2] > 0))
    threads = []
    for key, c0, other, user in block["thread"]:
        if key < (1 << 32):
            label = "pid %d" % (key - 1)
        else:
            label = lookup(addrs, names, key)
        threads.append((c0 + other + user, "%s cpu0 %d other %d user %d" % (label, c0, other, user)))
    threads.sort(reverse=True)
    print("threads (kernel ticks on cpu0, on others, user ticks): " + "; ".join(t[1] for t in threads[:10]))
    for h, label in (("h0", "cpu0"), ("h1", "other cpus")):
        funcs = {}
        total = 0
        for address, count in block[h]:
            name = lookup(addrs, names, address)
            funcs[name] = funcs.get(name, 0) + count
            total += count
        ranked = sorted(funcs.items(), key=lambda kv: -kv[1])
        print("%s kernel, top buckets %d ticks: %s" % (label, total, ", ".join(
            "%s %d" % (n, c) for n, c in ranked[:30])))


main()

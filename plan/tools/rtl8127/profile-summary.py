#!/usr/bin/env python3
# ws235: turns the hw.cputimes and the RTL8125-family driver's profile line taken before and after one tcp-perf run
# (plan/tools/rtl8127/rtl-link.sh PROFILE=1) into the driver's time a second, its cost a frame, and each CPU's load.
#   profile-summary.py DIRECTION SECONDS BEFORE AFTER
# The driver's line: "rtl8125: IF profile ms T irq N US poll N FULL FRAMES US tx N COPIES REFUSALS US [missed M]" (totals).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import re
import sys


def read(path):
    cpus = {}
    hz = None
    prof = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = re.search(r"hw\.cputimes: hz=(\d+)", line)
            if m:
                hz = int(m.group(1))
            m = re.search(r"cpu=(\d+) user=(\d+) system=(\d+) idle=(\d+) other=(\d+)", line)
            if m:
                cpus[int(m.group(1))] = [int(x) for x in m.groups()[1:]]
            m = re.search(r"profile ms (\d+) irq (\d+) (\d+) poll (\d+) (\d+) (\d+) (\d+) tx (\d+) (\d+) (\d+) (\d+)(?: missed (\d+))?", line)
            if m:
                prof = [int(x) if x is not None else -1 for x in m.groups()]
    return hz, cpus, prof


def main():
    direction, seconds, before, after = sys.argv[1], float(sys.argv[2]), sys.argv[3], sys.argv[4]
    hz, c0, p0 = read(before)
    _, c1, p1 = read(after)
    out = [direction]
    if p0 and p1:
        d = [b - a for a, b in zip(p0[:11], p1[:11])]
        ms, irq, irq_us, polls, full, frames, poll_us, tx, copies, refused, tx_us = d
        driver_us = irq_us + poll_us + tx_us
        out.append("driver %.1f%% of a CPU (irq %.1f%%, poll %.1f%%, tx %.1f%%) over %.0f s" % (
            100.0 * driver_us / (seconds * 1e6), 100.0 * irq_us / (seconds * 1e6),
            100.0 * poll_us / (seconds * 1e6), 100.0 * tx_us / (seconds * 1e6), seconds))
        out.append("irq %d/s, poll %d/s (%.1f frames a poll, %d full), rx %d/s, tx %d/s (%d copied, %d refused)" % (
            irq / seconds, polls / seconds, frames / polls if polls else 0.0, full, frames / seconds,
            tx / seconds, copies, refused))
        out.append("per frame: poll %.2f us, tx %.2f us" % (
            poll_us / frames if frames else 0.0, tx_us / tx if tx else 0.0))
        if p0[11] >= 0 and p1[11] >= 0:
            out.append("missed by the controller: %d (16-bit count)" % ((p1[11] - p0[11]) % 65536))
    else:
        out.append("no profile line before and after")
    if hz and c0 and c1:
        loads = []
        total = 0.0
        for cpu in sorted(c1):
            if cpu not in c0:
                continue
            user, system, idle, other = [b - a for a, b in zip(c0[cpu], c1[cpu])]
            busy = (user + system + other) / hz
            total += busy
            loads.append("cpu%d %.0f%% (sys %.0f%%)" % (cpu, 100.0 * busy / seconds, 100.0 * system / hz / seconds))
        out.append("CPU busy a second of the run: " + ", ".join(loads) + "; all %.2f CPUs" % (total / seconds))
    else:
        out.append("no hw.cputimes before and after")
    print("\n".join(out))


main()

#!/usr/bin/env python3
# The TcpExt counters of this host's /proc/net/netstat that moved between two copies (before and after a run of
# plan/ws221/tests/rdna-tcp.sh): the peer's view of the run (its loss probes, timeouts, fast retransmits, ...).
#   netstat-delta.py BEFORE AFTER
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import sys


def read(path):
    values = {}
    with open(path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    for names, numbers in zip(lines[0::2], lines[1::2]):
        prefix = names.split(":")[0]
        if prefix != "TcpExt":
            continue
        for name, number in zip(names.split()[1:], numbers.split()[1:]):
            values[name] = int(number)
    return values


before = read(sys.argv[1])
after = read(sys.argv[2])
moved = [(name, after[name] - before.get(name, 0)) for name in after if after[name] != before.get(name, 0)]
print(" ".join("%s=%d" % m for m in moved))

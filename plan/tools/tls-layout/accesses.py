# zedBSD; Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
"""Lists each m68k TLS access as vaddr:align:memsz:symbol:displacement."""

import re
import sys


def main():
    phdrs, syms, dis = (open(path).read() for path in sys.argv[1:4])
    tls = re.search(r"^\s*TLS\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+0x[0-9a-f]+"
                    r"\s+0x[0-9a-f]+\s+0x([0-9a-f]+)\s+\S+\s+0x([0-9a-f]+)",
                    phdrs, re.M)
    if tls is None:
        sys.exit("no PT_TLS")
    vaddr, memsz, align = (int(v, 16) for v in tls.groups())

    # TLS symbols are offsets from the segment's start.
    offsets = {}
    for line in syms.splitlines():
        value, _, name = line.split()
        if name.startswith("probe_") and not name.startswith("probe_read"):
            offsets[name] = int(value, 16)

    # Each reader's loads, in order of the variable's words.
    readers = {"probe_read_word": ("probe_word", 0),
               "probe_read_aligned": ("probe_aligned", 0),
               "probe_read_bytes": ("probe_bytes", 2),
               "probe_read_zero": ("probe_zero", 0)}
    out = []
    for function, (name, extra) in readers.items():
        body = re.search(r"<%s>:\n(.*?)\n\n" % function, dis, re.S)
        if body is None:
            sys.exit("no %s" % function)
        loads = re.findall(r"%a0@\((?:0x)?([0-9a-f]+)\)", body.group(1))
        if not loads:
            sys.exit("no access in %s" % function)
        for index, raw in enumerate(loads):
            displacement = int(raw, 16)
            if displacement >= 1 << 63:
                displacement -= 1 << 64
            symbol = vaddr + offsets[name] + extra + 4 * index
            out.append("%x:%x:%x:%x:%d" % (vaddr, align, memsz, symbol,
                                            displacement))
    print(" ".join(out))


main()

#!/usr/bin/env python3
"""Compares the function bodies of VM sources before and after a move.

usage: body-compare.py <old-git-ref> <old-path>... -- <new-path>...

The old files are read from the git ref and the new files from the work
tree.  Every function definition (an opening brace in column 0) is keyed by
its name.  For each name the set of definitions must be equal; the only
difference allowed is a leading "static" in the header.  Exits 1 on any
difference and prints the names involved.
"""

import re
import subprocess
import sys
from collections import Counter

NAME = re.compile(r"([A-Za-z_][A-Za-z0-9_]*)\s*\(")


def functions(text):
    lines = text.split("\n")
    result = []
    i = 0
    while i < len(lines):
        if lines[i] != "{":
            i += 1
            continue
        # Collects the header lines above the brace.
        h = i - 1
        while h >= 0:
            s = lines[h]
            if (s.strip() == "" or s.rstrip().endswith("*/")
                    or s.rstrip().endswith(";") or s == "}"
                    or s.startswith("#")):
                break
            h -= 1
        header = lines[h + 1:i]
        j = i + 1
        while j < len(lines) and lines[j] != "}":
            j += 1
        body = lines[i:j + 1]
        m = NAME.search(" ".join(header))
        if m is None:
            i = j + 1
            continue
        head = "\n".join(header)
        head = re.sub(r"^static(\s+|\n)", "", head)
        result.append((m.group(1), head + "\n" + "\n".join(body)))
        i = j + 1
    return result


def collect(texts):
    table = {}
    for text in texts:
        for name, definition in functions(text):
            table.setdefault(name, Counter())[definition] += 1
    return table


def main(argv):
    if "--" not in argv or len(argv) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    split = argv.index("--")
    ref = argv[1]
    old_paths = argv[2:split]
    new_paths = argv[split + 1:]
    old_texts = [subprocess.run(["git", "show", ref + ":" + p],
                                check=True, capture_output=True,
                                text=True).stdout for p in old_paths]
    new_texts = [open(p, encoding="utf-8").read() for p in new_paths]
    old = collect(old_texts)
    new = collect(new_texts)
    bad = 0
    for name in sorted(set(old) | set(new)):
        if old.get(name) != new.get(name):
            print("DIFF", name)
            bad += 1
    count = sum(sum(c.values()) for c in old.values())
    print("functions", count, "names", len(old), "differences", bad)
    print("PASS" if bad == 0 else "FAIL")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))

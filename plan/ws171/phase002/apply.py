#!/usr/bin/env python3
"""Replace or insert the comment above declarations of hal.h.

Usage: apply.py HEADER SPEC

SPEC holds entries:
  @@ name          the declaration whose name line starts with "name("
  @@ ^regex        the declaration whose first line matches regex
  @@ block:regex   the comment block that has a line matching regex
followed by the new comment block (/* ... */).
"""
import re
import sys


def find_decl(lines, key):
    if key.startswith("^"):
        pattern = re.compile(key)
        for i, line in enumerate(lines):
            if pattern.match(line):
                return i, i
        raise SystemExit("not found: " + key)
    for i, line in enumerate(lines):
        if line.startswith(key + "("):
            start = i
            while start > 0:
                above = lines[start - 1]
                if above.strip() == "" or above.rstrip().endswith("*/"):
                    break
                if above.startswith("/*") or above.startswith(" *"):
                    break
                start -= 1
            return start, i
    raise SystemExit("not found: " + key)


def main():
    header, spec = sys.argv[1], sys.argv[2]
    with open(header) as f:
        lines = f.read().split("\n")
    with open(spec) as f:
        text = f.read()
    entries = re.split(r"^@@ ", text, flags=re.M)[1:]
    for entry in entries:
        key, body = entry.split("\n", 1)
        key = key.strip()
        comment = body.strip("\n").split("\n")
        if key.startswith("block:"):
            pattern = re.compile(key[6:])
            inside = [i for i, line in enumerate(lines) if pattern.match(line)]
            if len(inside) != 1:
                raise SystemExit("block not found once: " + key)
            top = inside[0]
            while not lines[top].startswith("/*"):
                top -= 1
            end = inside[0]
            while not lines[end].rstrip().endswith("*/"):
                end += 1
            lines[top:end + 1] = comment
            continue
        start, _ = find_decl(lines, key)
        if start > 0 and lines[start - 1].rstrip().endswith("*/"):
            end = start - 1
            top = end
            while not lines[top].startswith("/*"):
                top -= 1
            lines[top:end + 1] = comment
        else:
            lines[start:start] = comment
    with open(header, "w") as f:
        f.write("\n".join(lines))


main()

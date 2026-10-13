#!/usr/bin/env python3
"""Check that two versions of hal.h differ only in comments.

Usage: check.py ORIGINAL PROPOSED

Removes every comment, joins the remaining text into tokens separated by
white space, and compares the two token streams.  Exits 0 when they are
equal, 1 when they are not.  Also reports a multi-line comment whose
opening or closing delimiter shares a line with prose (plan/coding-style.md
section 10) in the proposed file.
"""
import re
import sys


def tokens(path):
    with open(path) as f:
        text = f.read()
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return text.split()


def delimiter_problems(path):
    problems = []
    inside = False
    with open(path) as f:
        lines = f.read().split("\n")
    for number, line in enumerate(lines, 1):
        stripped = line.strip()
        if not inside:
            if stripped.startswith("/*") and "*/" not in stripped:
                inside = True
                if stripped != "/*":
                    problems.append(number)
            continue
        if "*/" in stripped:
            inside = False
            if stripped != "*/":
                problems.append(number)
    return problems


def main():
    original, proposed = sys.argv[1], sys.argv[2]
    same = tokens(original) == tokens(proposed)
    problems = delimiter_problems(proposed)
    print("declarations unchanged:", "yes" if same else "NO")
    print("comment delimiter problems:", problems if problems else "none")
    if not same:
        sys.exit(1)


main()

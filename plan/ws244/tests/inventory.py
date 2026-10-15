#!/usr/bin/env python3
#
# zedBSD
# Copyright (C) 2026 Awe Morris
#
# SPDX-License-Identifier: Zlib
#
# WS244: lists, file by file and function by function, the parts of the i915
# tree that name a Linux or Mesa origin, so that each rewrite phase can see
# what is left.  It reads the tree only; it never writes into it.
#
# Usage: inventory.py TREE [--functions] [--summary-tsv]
#   TREE is src/drivers/gpu/i915 (or a copy).
#   --functions prints one row per function that the heuristic marks C or S.
#   --summary-tsv prints the per-file summary as tab-separated values.
#
# The classes are a heuristic, not a verdict; inventory.md says how to read
# them:
#   C  the comment above or inside the function names a Linux or Mesa
#      function or file as its origin ("the Linux foo()", "Follows Linux",
#      "Mesa brw_...", or the file declares itself derived and the function
#      uses Linux type names).
#   S  no origin is named, but the function uses Linux type names
#      (struct intel_*, struct drm_*, drm_i915_private) or Linux kernel API
#      spellings; its structure is suspect.
#   O  neither.
#

import os
import re
import sys

ORIGIN_FUNC = re.compile(
    r"(?:Linux|Mesa)(?:'s)?\s+(?:6\.8\.12\s+|v6\.8\.12\s+)?"
    r"([A-Za-z_][A-Za-z0-9_]*)\(\)")
ORIGIN_WORD = re.compile(
    r"\b(Linux|Mesa)\b|\bbrw_[a-z0-9_]+\b|\belk_[a-z0-9_]+\b|\bisl_[a-z0-9_]+\b|\banv_[a-z0-9_]+\b")
ORIGIN_FILE = re.compile(
    r"\b((?:intel|drm|i915|skl|icl|tgl|gen[0-9]+|brw|elk|anv|isl)_[a-z0-9_]+\.[ch])\b")
FILE_DERIVED = re.compile(
    r"Derived from the Linux|derived from the Linux|Linux original|"
    r"Linux v6\.8\.12|Linux 6\.8\.12|the Linux text|Mesa", re.I)
LINUX_TYPES = re.compile(
    r"\bstruct\s+(?:intel_[a-z0-9_]+|drm_[a-z0-9_]+|i915_power_well[a-z_]*)\b|"
    r"\bdrm_i915_private\b|\bto_i915\(|\bdrm_WARN|\bdrm_dbg|\bintel_de_(?:read|write|rmw)\b|"
    r"\bwakeref\b|\bREG_FIELD_PREP\b")
FUNC_NAME = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\(")
DATA_TABLE = re.compile(r"^(?:static\s+)?const\s+[^=;(]*\[[^\]]*\]\s*=")
DEFINE = re.compile(r"^#define\s+([A-Za-z_][A-Za-z0-9_]*)")


def comment_above(lines, index):
    """Returns the comment block that ends right above lines[index]."""
    block = []
    i = index - 1
    while i >= 0 and lines[i].strip() == "":
        i -= 1
    if i >= 0 and lines[i].rstrip().endswith("*/"):
        while i >= 0:
            block.append(lines[i])
            if lines[i].lstrip().startswith("/*"):
                break
            i -= 1
    return "".join(reversed(block))


def scan(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.readlines()
    head = "".join(lines[:200])
    derived_file = bool(FILE_DERIVED.search(head))
    files = sorted(set(ORIGIN_FILE.findall("".join(lines))))
    funcs = []
    tables = []
    defines = 0
    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        m = FUNC_NAME.match(line)
        if m and i > 0 and lines[i - 1].strip() and \
                not lines[i - 1].rstrip().endswith((";", ",", "\\")) and \
                not lines[i - 1].startswith(("#", "}", " ", "\t", "/", "*")):
            # A function definition: find its opening and closing braces.
            j = i
            while j < n and not lines[j].startswith("{") and \
                    not lines[j].rstrip().endswith(";"):
                j += 1
            if j < n and lines[j].startswith("{"):
                k = j + 1
                while k < n and not lines[k].startswith("}"):
                    k += 1
                type_index = i - 1
                above = comment_above(lines, type_index)
                body = "".join(lines[j:k + 1])
                names = ORIGIN_FUNC.findall(above) + ORIGIN_FUNC.findall(body)
                origin = bool(names) or bool(ORIGIN_WORD.search(above))
                types = len(LINUX_TYPES.findall(body))
                if origin or (derived_file and types):
                    cls = "C"
                elif types:
                    cls = "S"
                else:
                    cls = "O"
                funcs.append((m.group(1), type_index + 1, k + 1, cls,
                              sorted(set(names)), types))
                i = k + 1
                continue
        if DATA_TABLE.match(line):
            k = i
            while k < n and not lines[k].rstrip().endswith("};"):
                k += 1
            above = comment_above(lines, i)
            tables.append((line.strip()[:70], i + 1, k + 1,
                           bool(ORIGIN_WORD.search(above) or
                                ORIGIN_FILE.search(above))))
            i = k + 1
            continue
        if DEFINE.match(line):
            defines += 1
        i += 1
    return {
        "lines": n,
        "derived": derived_file,
        "files": files,
        "funcs": funcs,
        "tables": tables,
        "defines": defines,
        "origin_lines": sum(1 for l in lines if ORIGIN_WORD.search(l)),
    }


def main():
    if len(sys.argv) < 2:
        print(__doc__ or "usage: inventory.py TREE", file=sys.stderr)
        return 2
    tree = sys.argv[1]
    want_funcs = "--functions" in sys.argv
    paths = []
    for root, dirs, names in os.walk(tree):
        dirs.sort()
        for name in sorted(names):
            if name.endswith((".c", ".h", ".inc")):
                paths.append(os.path.join(root, name))
    print("file\tlines\tderived\tfuncs\tC\tS\tO\tC_lines\tS_lines\t"
          "tables\ttables_cited\tdefines\torigin_lines\tlinux_files")
    for path in paths:
        r = scan(path)
        rel = os.path.relpath(path, tree)
        c = [f for f in r["funcs"] if f[3] == "C"]
        s = [f for f in r["funcs"] if f[3] == "S"]
        o = [f for f in r["funcs"] if f[3] == "O"]
        cl = sum(f[2] - f[1] + 1 for f in c)
        sl = sum(f[2] - f[1] + 1 for f in s)
        if not (r["derived"] or c or s or r["origin_lines"] or r["files"]):
            continue
        print("%s\t%d\t%s\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%s" % (
            rel, r["lines"], "y" if r["derived"] else "-", len(r["funcs"]),
            len(c), len(s), len(o), cl, sl, len(r["tables"]),
            sum(1 for t in r["tables"] if t[3]), r["defines"],
            r["origin_lines"], " ".join(r["files"])))
        if want_funcs:
            for name, a, b, cls, names, types in r["funcs"]:
                if cls == "O":
                    continue
                print("  %s\t%s:%d-%d\t%s\t%s\ttypes=%d" % (
                    cls, rel, a, b, name, ",".join(names), types))
    return 0


if __name__ == "__main__":
    sys.exit(main())

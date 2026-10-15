# Compares the amdrdna encoder's words with llvm-mc's (ws039-p007).
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
#
# usage: gfx10-compare-words.py ENCODE.txt LLVM.txt
# ENCODE.txt: "hex<TAB>text" per case (gfx10-encode-test).  LLVM.txt: llvm-mc -show-encoding of the texts, in order.
import re
import sys

ours = [line.rstrip("\n").split("\t", 1) for line in open(sys.argv[1]) if line.strip()]
theirs = []
for line in open(sys.argv[2]):
    match = re.search(r"encoding: \[([^\]]*)\]", line)
    if match:
        theirs.append("".join(byte[2:] for byte in match.group(1).split(",")))
failed = 0
refused = [text for words, text in ours if words in ("REFUSED", "DESCRIBE")]
for text in refused:
    print("FAIL encoder refused or described wrongly:", text)
    failed += 1
cases = [(words, text) for words, text in ours if words not in ("REFUSED", "DESCRIBE")]
if len(cases) != len(theirs):
    print("FAIL %d cases but llvm-mc encoded %d" % (len(cases), len(theirs)))
    sys.exit(1)
for (words, text), expected in zip(cases, theirs):
    if words != expected:
        print("FAIL %s: ours %s, llvm-mc %s" % (text, words, expected))
        failed += 1
print("%d known words, %d differ" % (len(cases), failed))
sys.exit(1 if failed else 0)

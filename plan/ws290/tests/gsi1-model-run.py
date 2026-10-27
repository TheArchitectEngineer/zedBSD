#!/usr/bin/env python3
# Runs amaterrace-gpu's GSI1 model (gsi1-tools/gsi1model.py, copied unchanged from cf8be08) with
# the two parts of docs/ghci.md d9252e4 section 12.10.3 that it predates, patched into the text it
# runs (the copy on disk stays as it was; a patch whose text is not found exactly once stops the run):
#   - lda reads PT words 0-3 too, each vertex's clip-space position (the model reads them as 0);
#   - operands 120 (vtx_idx) and 121 (inst_idx) of a vertex warp read 7 + lane and 3, the values
#     gsi1-host-test.c gives the reference (the model reads every operand above 119 as 0).
#
#   gsi1-model-run.py TOOLS_DIR [gsi1model.py's arguments ...]
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import sys

tools = sys.argv[1]
path = os.path.join(tools, "gsi1model.py")
with open(path) as f:
    source = f.read()

PATCHES = [
    ("4 <= wd < 4 + self.vary", "wd < 4 + self.vary"),
    ("119: exec_init}", "119: exec_init, 120: _vtx_idx(g, lane), 121: _inst_idx(g)}"),
]
for old, new in PATCHES:
    if source.count(old) != 1:
        raise SystemExit("gsi1-model-run: %r is not in %s exactly once" % (old, path))
    source = source.replace(old, new)

PRELUDE = '''
def _vtx_idx(g, lane):
    return 7 + lane if g is not None and g["kind"] == 0 else 0


def _inst_idx(g):
    return 3 if g is not None and g["kind"] == 0 else 0
'''
marker = "import argparse\n"
if source.count(marker) != 1:
    raise SystemExit("gsi1-model-run: no place for the operands in %s" % path)
source = source.replace(marker, marker + PRELUDE, 1)

sys.argv = [path] + sys.argv[2:]
exec(compile(source, path, "exec"), {"__name__": "__main__", "__file__": path})

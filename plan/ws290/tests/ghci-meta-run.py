#!/usr/bin/env python3
# Runs one dispatch of the GHCI driver's own transfer programs (src/drivers/gpu/ghci/vk/vk-meta.c) on
# amaterrace-gpu's GSI1 model (gsi1-tools/gsi1model.py through gsi1-model-run.py), for
# ghci-meta-host-test.c: PREFIX.bin is the program's code as the driver placed it (32-bit words,
# little-endian, two to an instruction), PREFIX.data.hex the model's data image (the constant bank,
# the resource table and the two buffers, which the test laid out); the code is disassembled with
# gsi1-tools/gsi1dis.py into PREFIX.s (it must assemble back to the same words), the model runs
# GROUPS workgroups of 64 invocations, and its memory after the dispatch is PREFIX.mem.
#
#   ghci-meta-run.py TOOLS_DIR PREFIX GROUPS
# Exit status 0, or 1 with the reason on stdout.
# Copyright (C) 2026 Awe Morris; SPDX-License-Identifier: Zlib
import os
import subprocess
import sys

tools, prefix, groups = sys.argv[1], sys.argv[2], sys.argv[3]
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, tools)
import gsi1asm as A  # noqa: E402
import gsi1dis as D  # noqa: E402

with open(prefix + ".bin", "rb") as f:
    w32 = D.read_bin(f.read())
head, words = D.from_words32(w32)
lines = D.disassemble(words)
bad = [ln for ln in lines if ".inst" in ln]
if bad:
    print("ghci-meta-run: the disassembly has words gsi1asm cannot write: %s" % bad[:3])
    sys.exit(1)
if A.encode("\n".join(lines)) != words:
    print("ghci-meta-run: the reassembled code differs from the placed code")
    sys.exit(1)
with open(prefix + ".s", "w") as f:
    f.write("\n".join(head + lines) + "\n")

model = [sys.executable, "-I", os.path.join(here, "gsi1-model-run.py"), tools, prefix + ".s",
         "--lanes", "64", "--groups", groups, "--nregs", "64", "--data", prefix + ".data.hex",
         "-o", prefix + ".regs", "--mem-out", prefix + ".mem"]
done = subprocess.run(model, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=600)
if done.returncode != 0:
    print("ghci-meta-run: the model stops: " + done.stdout.decode(errors="replace").strip()[-400:])
    sys.exit(1)
sys.exit(0)

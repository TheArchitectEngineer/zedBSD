"""Generate the Gen12 forcewake range table from the Linux 6.8.12 reference.

The hand-made table missed the media domains (found in P6-c1) and would keep
missing ranges one register at a time.  This extracts __gen12_fw_ranges
verbatim: every entry whose domain is not 0 becomes a range; the always-on
entries (domain 0) are listed as comments only, because no match in
i915_gen12_ranges (src/drivers/gpu/i915/mmio.c) already means always on.

Usage (from anywhere; paths are relative to the repository root):
    python3 plan/ws031/handover/tools/gen_fw_ranges.py [OUT]
OUT defaults to src/drivers/gpu/i915/data/forcewake-ranges.inc.  To check the
checked-in file, write to a temporary path and compare it with cmp.

The header text, including its mention of scratchpad/gen_fw_ranges.py, is kept
exactly as in the checked-in file so that a re-run reproduces it byte for byte.
"""
import os
import re
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
REF = os.path.join(ROOT, "plan/ws031/linux-parity/linux-reference/i915-src/intel_uncore.c")
OUT = os.path.join(ROOT, "src/drivers/gpu/i915/data/forcewake-ranges.inc")
if len(sys.argv) > 1:
    OUT = sys.argv[1]

src = open(REF).read()
m = re.search(r"static const struct intel_forcewake_range __gen12_fw_ranges\[\] = \{(.*?)^\};",
              src, re.S | re.M)
assert m, "table"
rows = re.findall(r"GEN_FW_RANGE\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*([A-Z0-9_]+)\)", m.group(1))
assert len(rows) == 43, len(rows)

DOM = {
    "FORCEWAKE_RENDER": "I915_FORCEWAKE_RENDER",
    "FORCEWAKE_GT": "I915_FORCEWAKE_GT",
    "FORCEWAKE_MEDIA_VDBOX0": "I915_FORCEWAKE_MEDIA_VDBOX0",
    "FORCEWAKE_MEDIA_VDBOX2": "I915_FORCEWAKE_MEDIA_VDBOX2",
    "FORCEWAKE_MEDIA_VEBOX0": "I915_FORCEWAKE_MEDIA_VEBOX0",
}

out = ["/*",
       " * The Gen12 register to forcewake domain map.",
       " *",
       " * Generated from the Linux 6.8.12 reference (intel_uncore.c, __gen12_fw_ranges,",
       " * which uncore_forcewake_init() assigns to Alder Lake-P) by",
       " * scratchpad/gen_fw_ranges.py; the domain names were renamed for this driver.",
       " * The always-on entries are omitted because no match already means always on.",
       " */"]
n = 0
for s, e, d in rows:
    if d == "0":
        out.append("\t/* 0x%s .. 0x%s always-on */" % (s[2:], e[2:]))
        continue
    assert d in DOM, d
    out.append("\t{ %su, %su, %s }," % (s, e, DOM[d]))
    n += 1
open(OUT, "w").write("\n".join(out) + "\n")
print("wrote %s: %d domain ranges, %d always-on as comments" % (OUT, n, len(rows) - n))

"""Generate the parity Gen12 forcewake range table from the 6.8.12 reference.

The hand-made table missed the media domains (found in P6-c1) and would keep
missing ranges one register at a time.  This extracts __gen12_fw_ranges
verbatim: every entry whose domain is not 0 becomes a parity range; the
always-on entries (domain 0) are simply not listed, which is what
osdep_mmio_domain_of() already means by "no match".
"""
import os, re

REF = os.path.expanduser(
    "~/zedBSD/plan/ws031/linux-parity/linux-reference/i915-src/intel_uncore.c")
OUT = os.path.expanduser(
    "~/zedBSD/src/drivers/gpu/i915/parity/gt_fw_ranges.inc")
BACKEND = os.path.expanduser(
    "~/zedBSD/src/drivers/gpu/i915/parity/backend_mmio.c")

src = open(REF).read()
m = re.search(r"static const struct intel_forcewake_range __gen12_fw_ranges\[\] = \{(.*?)^\};",
              src, re.S | re.M)
assert m, "table"
rows = re.findall(r"GEN_FW_RANGE\((0x[0-9a-fA-F]+),\s*(0x[0-9a-fA-F]+),\s*([A-Z0-9_]+)\)", m.group(1))
assert len(rows) == 43, len(rows)

DOM = {
    "FORCEWAKE_RENDER": "OSDEP_FW_RENDER",
    "FORCEWAKE_GT": "OSDEP_FW_GT",
    "FORCEWAKE_MEDIA_VDBOX0": "OSDEP_FW_MEDIA_VDBOX0",
    "FORCEWAKE_MEDIA_VDBOX2": "OSDEP_FW_MEDIA_VDBOX2",
    "FORCEWAKE_MEDIA_VEBOX0": "OSDEP_FW_MEDIA_VEBOX0",
}

out = ["/*",
       " * WS031 Linux-parity — Gen12 register -> forcewake domain map.",
       " *",
       " * GENERATED from the 6.8.12 reference (intel_uncore.c, __gen12_fw_ranges,",
       " * which uncore_forcewake_init() assigns to ADL-P) by",
       " * scratchpad/gen_fw_ranges.py.  %d ranges; the always-on entries (domain 0)" % len(rows),
       " * are omitted because \"no match\" already means always-on.",
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
print("wrote %s: %d domain ranges, %d always-on omitted" % (OUT, n, len(rows) - n))

b = open(BACKEND).read()
start = b.index("const struct osdep_mmio_range parity_mmio_ranges[] = {")
end = b.index("};", start) + 2
b = b[:start] + "const struct osdep_mmio_range parity_mmio_ranges[] = {\n#include \"gt_fw_ranges.inc\"\n};" + b[end:]
open(BACKEND, "w").write(b)
print("backend_mmio.c now includes the generated table")

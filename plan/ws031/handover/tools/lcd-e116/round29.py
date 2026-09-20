#!/usr/bin/env python3
"""WS031 E-116 round 29: the DBUF_CTL_S register table of the power-domain init was wrong (found by LCD-B's register
readback: DBUF_CTL_S1 0x45008 never powered, "slice 3 power enable timeout").  Reference, skl_watermark_regs.h:
_DBUF_CTL_S0 0x45008, _DBUF_CTL_S1 0x44FE8, _DBUF_CTL_S2 0x44300, _DBUF_CTL_S3 0x44304 (enum dbuf_slice DBUF_S1 = 0).
usage: round29.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

d = load(P + "display_core.c")
d = rep(d, "static const uint32_t dbuf_ctl_s[4] = { 0x44FE8u, 0x44300u, 0x44304u, 0x44308u };",
        "/* skl_watermark_regs.h: _DBUF_CTL_S0 0x45008, _DBUF_CTL_S1 0x44FE8, _DBUF_CTL_S2 0x44300, _DBUF_CTL_S3 0x44304; slice S1 is" + NL +
        " * index 0.  (Until E-116 this table began at 0x44FE8 and ended at 0x44308: \"slice 1\" powered the second slice and" + NL +
        " * the fourth request went to a register that is not a DBUF control -- found by LCD-B's register readback.) */" + NL +
        "static const uint32_t dbuf_ctl_s[4] = { 0x45008u, 0x44FE8u, 0x44300u, 0x44304u };")
save(P + "display_core.c", d)
k = load(P + "ktest.c")
k = rep(k, "	if (off == 0x44FE8u || off == 0x44300u || off == 0x44304u || off == 0x44308u) {",
        "	if (off == 0x45008u || off == 0x44FE8u || off == 0x44300u || off == 0x44304u) {")
k = rep(k, "		osdep_mmio_raw_write32(&m, 0x44FE8u, (1u << 31));   /* S1 */" + NL + "		osdep_mmio_raw_write32(&m, 0x44300u, (1u << 31));   /* S2 */",
        "		osdep_mmio_raw_write32(&m, 0x45008u, (1u << 31));   /* S1 */" + NL + "		osdep_mmio_raw_write32(&m, 0x44FE8u, (1u << 31));   /* S2 */")
save(P + "ktest.c", k)
print("done")

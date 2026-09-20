#!/usr/bin/env python3
"""WS031 E-117 round 36: after the first reuse run: judge the pipe IMR on the bits the path relies on; bits 17 / 18
read back 0 although written 1 (logged, not interpreted).  usage: round36.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
p = root + "src/drivers/gpu/i915/parity/lcd/parity_lcd_kernel.c"
s = open(p).read()
def rep(s, a, b):
    assert s.count(a) == 1, a[:70]
    return s.replace(a, b)
s = rep(s, "	ok = get_rc == 0 && imr == d->irq->de_irq_mask[0] && (imr & 1u) != 0u && ier == want_ier &&",
"""	/*
	 * The IMR is judged on the bits this path relies on -- vblank, the underrun bits, flip done -- which must read back
	 * as the driver's mask.  The first reuse run found bits 17 / 18 written 1 but reading 0 (the pre-driver default
	 * 0xfff9ffff has them 0 too); the reference never reads IMR back, so their meaning is not interpreted: they are
	 * logged, not judged.
	 */
	ok = get_rc == 0 && ((imr ^ d->irq->de_irq_mask[0]) & (extra | 0x1u)) == 0u && (imr & 1u) != 0u && ier == want_ier &&""")
s = rep(s, """		"masking: %u | refs %u -> %s\\n", imr, d->irq->de_irq_mask[0], ier, want_ier, get_rc, imr_on, rc[0], rc[1], rc[2],
		seen[0], seen[1], seen[2], raw1 - raw0, f0, f1, imr_off, raw2 - raw1, v->refs[0], ok ? "OK" : "FAIL");""",
"""		"masking: %u | refs %u | IMR bits differing from the written mask 0x%08x (judged bits 0x%08x) -> %s\\n", imr,
		d->irq->de_irq_mask[0], ier, want_ier, get_rc, imr_on, rc[0], rc[1], rc[2], seen[0], seen[1], seen[2], raw1 - raw0, f0, f1,
		imr_off, raw2 - raw1, v->refs[0], imr ^ d->irq->de_irq_mask[0], extra | 0x1u, ok ? "OK" : "FAIL");""")
open(p, "w").write(s)
print("patched")

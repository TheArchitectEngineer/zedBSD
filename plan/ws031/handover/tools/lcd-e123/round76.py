#!/usr/bin/env python3
"""WS031 E-123 round 76: gen11_gu_misc_irq_handler(): GSE -> intel_opregion_asle_intr, here the OpRegion service's GSE
entry (gated: nothing is queued unless the service accepts; now possible because the kworkqueue is IRQ-safe).  The
handler runs after the master interrupt is re-enabled, as in the reference (gen11_irq_handler: gu_misc ack, master
enable, then gen11_gu_misc_irq_handler).  usage: round76.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
P = root + "src/drivers/gpu/i915/parity/"
NL = chr(10)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)
h = open(P + "irq.h").read()
h = rep(h, "	volatile uint32_t last_gu_misc_iir;", "	volatile uint32_t last_gu_misc_iir;" + NL +
        "	volatile unsigned gse_count;            /* GU_MISC GSE interrupts handed to the OpRegion GSE entry (E-123) */")
open(P + "irq.h", "w").write(h)
c = open(P + "irq.c").read()
c = rep(c, '#include "wait.h"' + NL, '#include "wait.h"' + NL + '#include "lcd/parity_opregion.h"' + NL)
c = rep(c, """	d->last_gu_misc_iir = gu_misc_iir;

	gen11_master_intr_enable(d);
""", """	d->last_gu_misc_iir = gu_misc_iir;

	gen11_master_intr_enable(d);

	/* gen11_gu_misc_irq_handler(): GSE -> intel_opregion_asle_intr (the service's gated GSE entry) */
	if (gu_misc_iir & GEN11_GU_MISC_GSE) {
		d->gse_count++;
		parity_opregion_gse_entry();
	}
""")
open(P + "irq.c", "w").write(c)
print("done")

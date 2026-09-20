#!/usr/bin/env python3
"""WS031 E-122 round 70: OP-LIFECYCLE (unit 3) -- one termination contract for the OpRegion service:
producer gate (the GSE entry queues only while the service accepts) -> the reference unregister (ARDY NOT_READY,
cancel_work_sync, DRDY 0, notifier off; the chain waits for a running callback) -> cleanup only when the ASLE work is
shown idle (else the mapping is kept and the refusal recorded).  usage: round70.py <repo root>"""
import sys
root = sys.argv[1].rstrip("/") + "/"
L = root + "src/drivers/gpu/i915/parity/lcd/"
NL = chr(10)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

g = open(L + "parity_opregion_glue.inc").read()
g = rep(g, "static unsigned parity_opregion_nmaps, parity_opregion_unported, parity_opregion_boundaries, parity_opregion_unmaps;",
        "static unsigned parity_opregion_nmaps, parity_opregion_unported, parity_opregion_boundaries, parity_opregion_unmaps;" + NL +
        "static int parity_opregion_accepting;            /* the producer gate: GSE requests are taken only while 1 */" + NL +
        "static unsigned parity_opregion_dropped, parity_opregion_cleanup_refused;")
g = rep(g, """void parity_opregion_register(void) { intel_opregion_register(&parity_opregion_dev); }
void parity_opregion_unregister(void) { intel_opregion_unregister(&parity_opregion_dev); }
void parity_opregion_cleanup(void)
{
	intel_opregion_cleanup(&parity_opregion_dev);
	parity_opregion_backend = "NONE";
	parity_opregion_nmaps = 0u;
}""", """void parity_opregion_register(void)
{
	intel_opregion_register(&parity_opregion_dev);
	/* requests are accepted once the reference published ARDY READY (there is an ASLE mailbox) */
	parity_opregion_accepting = parity_opregion_dev.display.opregion.asle != 0;
}

/* the termination contract: stop the producer first, then the reference unregister (which syncs the worker) */
void parity_opregion_unregister(void)
{
	parity_opregion_accepting = 0;
	intel_opregion_unregister(&parity_opregion_dev);
}

/* cleanup only once the ASLE work is shown idle; otherwise the mapping and the instance stay (recorded) */
int parity_opregion_cleanup(void)
{
	struct work_struct *w = &parity_opregion_dev.display.opregion.asle_work;

	if (parity_opregion_accepting)
		return -EBUSY;                  /* still registered: unregister first */
	if (parity_opregion_dev.unordered_wq != 0 && parity_opregion_dev.display.opregion.header != 0 &&
	    (parity_kwork_is_pending(parity_opregion_dev.unordered_wq, &w->kwork) || w->kwork.state == PARITY_KWORK_RUNNING)) {
		parity_opregion_cleanup_refused++;
		kern_logf("i915: parity opregion cleanup REFUSED: the ASLE work is not shown idle -- mapping kept\\n");
		return -EBUSY;
	}
	intel_opregion_cleanup(&parity_opregion_dev);
	parity_opregion_backend = "NONE";
	parity_opregion_nmaps = 0u;
	return 0;
}""")
g = rep(g, """void parity_opregion_gse_entry(void)
{
	intel_opregion_asle_intr(&parity_opregion_dev);
}""", """void parity_opregion_gse_entry(void)
{
	if (!parity_opregion_accepting) {
		parity_opregion_dropped++;      /* stopped / not started: a late request queues nothing */
		return;
	}
	intel_opregion_asle_intr(&parity_opregion_dev);
}

void parity_opregion_gate_counters(unsigned *dropped, unsigned *cleanup_refused)
{
	*dropped = parity_opregion_dropped;
	*cleanup_refused = parity_opregion_cleanup_refused;
}""")
open(L + "parity_opregion_glue.inc", "w").write(g)

h = open(L + "parity_opregion.h").read()
h = rep(h, "void parity_opregion_cleanup(void);", "int parity_opregion_cleanup(void);        /* -EBUSY: still registered, or the ASLE work not shown idle */")
h = rep(h, "#endif /* PARITY_OPREGION_H */", "void parity_opregion_gate_counters(unsigned *dropped, unsigned *cleanup_refused);" + NL + NL + "#endif /* PARITY_OPREGION_H */")
open(L + "parity_opregion.h", "w").write(h)
print("done")

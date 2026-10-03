#!/usr/bin/env python3
"""WS031 E-119 round 43: parity_lrc_keep; probe teardown keeps the engines' objects while the GPU is retained and logs
the IRQ uninstall only when it ran; vblank reads through one locked snapshot; the T1..T3 harnesses put their PTEs back
to scratch before freeing the pages (fixtures unchanged).  usage: round43.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

h = load(P + "gt_lrc.h")
h = rep(h, "void parity_lrc_release(", "/* the context's ring and image are kept for ever (a request on it was not shown to be finished) */" + NL +
        "void parity_lrc_keep(struct parity_gt_context *ce);" + NL + "void parity_lrc_release(")
save(P + "gt_lrc.h", h)
c = load(P + "gt_lrc.c")
c = rep(c, "void" + NL + "parity_lrc_release(struct parity_gt_context *ce, struct parity_gt_mem *gm)",
        "void" + NL + "parity_lrc_keep(struct parity_gt_context *ce)" + NL + "{" + NL + "	if (ce == 0)" + NL + "		return;" + NL +
        "	if (ce->ring.obj != 0)" + NL + "		ce->ring.obj->keep = 1;" + NL + "	if (ce->state != 0)" + NL + "		ce->state->keep = 1;" + NL + "}" + NL + NL +
        "void" + NL + "parity_lrc_release(struct parity_gt_context *ce, struct parity_gt_mem *gm)")
save(P + "gt_lrc.c", c)

pc = load(P + "probe.c")
pc = rep(pc, "	if (gteng_inited) {" + NL + "		parity_intel_engines_release(&gteng, &gtmem);",
         "	if (gteng_inited && parity_lcd_kernel_gpu_retained()) {" + NL +
         '		kern_logf("i915: parity teardown: engines NOT released (the GPU was not shown to be done with a kept buffer)\\n");' + NL +
         "	} else if (gteng_inited) {" + NL + "		parity_intel_engines_release(&gteng, &gtmem);")
pc = rep(pc, """		if (pwc.irq_sync_failed)
			kern_logf("i915: parity teardown: a pipe interrupt drain failed earlier -- the IRQ handler stays attached, "
				"display power and device resources are kept\\n");
		else
			parity_intel_irq_uninstall(&irqdev);
		kern_logf("i915: parity teardown: intel_irq_uninstall (sources reset, "
			"handler detached; irq_count=%u handled=%u none=%u)\\n",
			irqdev.irq_count, irqdev.irq_handled_count, irqdev.irq_none_count);""",
         """		if (pwc.irq_sync_failed) {
			kern_logf("i915: parity teardown: intel_irq_uninstall NOT run: a pipe interrupt drain failed earlier "
				"(irq_attached=1 resources_retained=1; irq_count=%u handled=%u none=%u)\\n",
				irqdev.irq_count, irqdev.irq_handled_count, irqdev.irq_none_count);
		} else {
			parity_intel_irq_uninstall(&irqdev);
			kern_logf("i915: parity teardown: intel_irq_uninstall (sources reset, "
				"handler detached; irq_count=%u handled=%u none=%u)\\n",
				irqdev.irq_count, irqdev.irq_handled_count, irqdev.irq_none_count);
		}""")
save(P + "probe.c", pc)

ic = load(P + "irq.c")
ic = rep(ic, "int" + NL + "parity_wait_vblank(", """/* one consistent view of a pipe's vblank state, read under the IRQ lock */
static void
vbl_snapshot(struct parity_irq_dev *d, unsigned pipe, uint32_t *count, int *enabled, unsigned *refs)
{
	unsigned long flags = spin_lock_irqsave(&d->vbl->lock);

	if (count != 0)
		*count = d->vbl->count[pipe];
	if (enabled != 0)
		*enabled = d->vbl->enabled[pipe];
	if (refs != 0)
		*refs = d->vbl->refs[pipe];
	spin_unlock_irqrestore(&d->vbl->lock, flags);
}

int
parity_wait_vblank(""")
ic = rep(ic, """		if ((uint32_t)(d->vbl->count[pipe] - start) >= n &&""", """		uint32_t now;

		vbl_snapshot(d, pipe, &now, 0, 0);
		if ((uint32_t)(now - start) >= n &&""")
ic = rep(ic, """	if (count_seen != 0)
		*count_seen = d->vbl->count[pipe] - start;""", """	if (count_seen != 0) {
		uint32_t now;

		vbl_snapshot(d, pipe, &now, 0, 0);
		*count_seen = now - start;
	}""")
save(P + "irq.c", ic)

e = load(P + "eu_test.c")
e = rep(e, "void" + NL + "parity_eu_test_release(struct parity_eu_test *t, struct parity_gt_mem *gm)" + NL + "{" + NL +
        "	if (t == 0 || gm == 0)" + NL + "		return;",
        """/*
 * The fixture VAs (0x100400000 .. 0x100405fff) back to scratch in the context's vm before any page they named is freed.
 * The teardown runs after the engines were reset (their TLBs dropped with it); the fixtures themselves are unchanged.
 */
static void
eu_scrub_fixture_ptes(struct parity_eu_test *t)
{
	unsigned p;

	if (t == 0 || t->ce.vm == 0)
		return;
	for (p = 0u; p < 6u; p++)
		if (parity_gt_ppgtt_insert_scratch(t->ce.vm, PARITY_EU_SHARED_VA + (uint64_t)p * 4096u) == 0)
			t->ptes_scrubbed++;
}

void
parity_eu_test_release(struct parity_eu_test *t, struct parity_gt_mem *gm)
{
	if (t == 0 || gm == 0)
		return;
	eu_scrub_fixture_ptes(t);""")
e = rep(e, "void" + NL + "parity_tex_test_release(struct parity_tex_test *x, struct parity_gt_mem *gm)" + NL + "{" + NL +
        "	if (x == 0 || gm == 0)" + NL + "		return;",
        "void" + NL + "parity_tex_test_release(struct parity_tex_test *x, struct parity_gt_mem *gm)" + NL + "{" + NL +
        "	if (x == 0 || gm == 0)" + NL + "		return;" + NL + "	eu_scrub_fixture_ptes(&x->t);")
save(P + "eu_test.c", e)
eh = load(P + "eu_test.h")
eh = rep(eh, "struct parity_tex_test {", "struct parity_tex_test;" + NL + "struct parity_tex_test {")
save(P + "eu_test.h", eh)
print("done")

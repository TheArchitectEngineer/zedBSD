/*
 * WS031 Linux-parity — P6-c4a: intel_engines_init() and intel_gt_resume().
 * See gt_resume.h for the reference order.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include "gt_mem.h"
#include "gt_init.h"
#include "gt_request.h"
#include "gt_resume.h"
#include "reset.h"
#include "osdep/mmio.h"

int
parity_intel_engines_init(struct parity_gt_engines *es, struct parity_gt_mmio *g,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp)
{
	unsigned i;
	int rc;

	if (es == 0 || g == 0 || gm == 0 || pp == 0)
		return -EINVAL;
	for (i = 0u; i < sizeof(*es); i++)
		((char *)es)[i] = 0;

	for (i = 0u; i < g->num_engines && i < (unsigned)PARITY_MAX_ENGINES; i++) {
		struct parity_gt_engine *ge = &es->ge[i];
		struct parity_gt_context *ce = &es->kernel_ce[i];

		/* engine_setup_common(): the status page and the execlists state. */
		rc = parity_engine_setup_common(ge, &g->engines[i], gm, &g->sseu);
		if (rc != 0)
			goto fail;

		/* intel_execlists_submission_setup(). */
		parity_execlists_submission_setup(ge);
		parity_execlists_init(&es->el[i]);

		/*
		 * engine_init_common() -> create_kernel_context():
		 * intel_engine_create_pinned_context(engine, gt->vm, SZ_4K,
		 * I915_GEM_HWS_SEQNO_ADDR, ...) and perma-pin it.  Pinning a
		 * never-initialised context runs lrc_init_state() and then
		 * lrc_update_regs() on the (empty) ring.
		 */
		rc = parity_lrc_alloc(ce, ge, pp, gm, 4096u, 0u);
		if (rc != 0)
			goto fail;
		parity_lrc_init_state(ce);
		(void)parity_lrc_update_regs(ce, ce->ring.tail);
		es->kernel_tl_seqno[i] = 0u;
		es->n = i + 1u;
	}
	es->inited = 1;
	return 0;

fail:
	kern_logf("i915: parity intel_engines_init: engine %u failed rc=%d\n", i, rc);
	es->n = i + 1u;
	parity_intel_engines_release(es, gm);
	return rc;
}

void
parity_intel_engines_release(struct parity_gt_engines *es, struct parity_gt_mem *gm)
{
	unsigned i;

	if (es == 0 || gm == 0)
		return;
	for (i = es->n; i-- > 0u; ) {
		parity_lrc_release(&es->kernel_ce[i], gm);
		parity_engine_release(&es->ge[i], gm);
	}
	es->n = 0u;
	es->inited = 0;
}

/* execlists_sanitize(). */
static void
execlists_sanitize(struct parity_gt_engines *es, unsigned i, struct osdep_mmio *m)
{
	struct parity_gt_engine *ge = &es->ge[i];

	/* CONFIG_DRM_I915_DEBUG_GEM poisons the page first; it is off. */
	parity_execlists_reset_csb_pointers(ge, m);

	/*
	 * sanitize_hwsp(): the kernel context's timeline lives in the status
	 * page, which may have been lost, so its seqno is written back.
	 */
	ge->hwsp[PARITY_I915_GEM_HWS_SEQNO_ADDR / 4u] = es->kernel_tl_seqno[i];

	/* intel_engine_reset_pinned_contexts() -> lrc_reset() on the kernel ctx. */
	parity_lrc_reset(&es->kernel_ce[i]);
}

int
parity_intel_gt_resume(struct parity_gt_engines *es, struct parity_gt_init *gi,
	const struct parity_gt_mmio *g, struct osdep_mmio *m,
	struct spinlock *uncore_lock)
{
	unsigned i;

	if (es == 0 || !es->inited || gi == 0 || g == 0 || m == 0)
		return -EINVAL;

	/* ---- gt_sanitize(gt, true) ---- */
	for (i = 0u; i < es->n; i++) {
		parity_execlists_reset_prepare(&es->ge[i], m);
		if (es->ge[i].stop_cs_rc != 0)
			es->stop_cs_timeouts++;
		execlists_sanitize(es, i, m);
	}

	/* reset_engines(gt): __intel_gt_reset(gt, ALL_ENGINES). */
	es->reset_rc = parity_gt_reset_all(uncore_lock, m, 2000u);
	if (es->reset_rc != 0)
		kern_logf("i915: parity gt_sanitize: reset_engines rc=%d (continuing, as the reference does)\n",
			es->reset_rc);

	/*
	 * __intel_engine_reset(engine, false) -> reset.rewind: nothing was ever
	 * submitted, so there is nothing to unwind.  reset.finish re-enables a
	 * tasklet this port does not have.
	 */
	parity_intel_rps_sanitize(&gi->rps, m);

	/* ---- intel_rc6_sanitize ---- */
	parity_intel_rc6_sanitize(&gi->rc6, m);

	/* ---- intel_gt_init_hw ---- */
	parity_gt_init_hw_core(gi, g, m);

	/* ---- intel_rps_enable; intel_llc_enable is recorded, not faked ---- */
	parity_intel_rps_enable(&gi->rps, m);

	/* ---- per engine: serial++, intel_engine_resume() ---- */
	for (i = 0u; i < es->n; i++) {
		struct parity_gt_engine *ge = &es->ge[i];

		es->el[i].serial++;   /* kernel context lost */
		parity_engine_apply_resume_wa(gi, g, i, m);

		/* execlists_resume(): intel_mocs_init_engine() first. */
		if (ge->info->class == PARITY_RENDER_CLASS) {
			/*
			 * Global MOCS means no per-engine MOCS table, but
			 * HAS_RENDER_L3CC re-programs the L3CC table for render.
			 */
			parity_init_l3cc_table(&gi->mocs, m, &es->l3cc_writes_rcs);
		}
		/* intel_breadcrumbs_reset(): software only. */
		parity_execlists_enable(ge, m);
		es->resumed++;
	}

	/* ---- intel_rc6_enable ---- */
	parity_gen11_rc6_enable(&gi->rc6, m, g);
	return 0;
}

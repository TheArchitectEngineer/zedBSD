/*
 * WS031 Linux-parity — P6-c4b: __engines_record_defaults().
 * See gt_defaults.h for the reference sequence and the recorded adaptations.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include <string.h>
#include "gt_mem.h"
#include "gt_init.h"
#include "gt_defaults.h"
#include "reset.h"
#include "wait.h"
#include "osdep/mmio.h"

static int
fail(struct parity_gt_defaults *d, int rc, const char *where)
{
	if (d->err == 0) {
		d->err = rc;
		d->err_where = where;
	}
	return rc;
}

int
parity_engines_record_defaults_submit(struct parity_gt_defaults *d,
	struct parity_gt_engines *es, struct parity_gt_init *gi,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp, struct osdep_mmio *m)
{
	unsigned i;
	int rc;

	if (d == 0 || es == 0 || gi == 0 || gm == 0 || pp == 0 || m == 0)
		return -EINVAL;
	for (i = 0u; i < sizeof(*d); i++)
		((char *)d)[i] = 0;

	for (i = 0u; i < es->n; i++) {
		struct parity_gt_engine *ge = &es->ge[i];
		struct parity_gt_context *ce = &d->ce[i];
		struct parity_gt_request *rq = &d->rq[i];

		/* intel_context_create(engine): a 4 KiB ring. */
		rc = parity_lrc_alloc(ce, ge, pp, gm, 4096u, 0u);
		if (rc != 0)
			return fail(d, rc, "intel_context_create");
		d->n = i + 1u;

		/* intel_timeline_create(): hwsp_alloc() -> its own page, pinned. */
		d->tl_page[i] = parity_gt_object_create(gm, 4096u);
		if (d->tl_page[i] == 0)
			return fail(d, -ENOMEM, "intel_timeline_create");
		rc = parity_gt_ggtt_bind(gm, d->tl_page[i]);
		if (rc != 0)
			return fail(d, rc, "intel_timeline_pin");
		d->tl_seqno[i] = 0u;

		/*
		 * intel_renderstate_init() pins the context: never initialised,
		 * so lrc_init_state() (restore INHIBITED, no default state yet),
		 * then lrc_update_regs() on the empty ring.
		 */
		parity_lrc_init_state(ce);
		(void)parity_lrc_update_regs(ce, ce->ring.tail);

		/* i915_request_create(): has_initial_breadcrumb, so seqno += 2. */
		d->tl_seqno[i] += 2u;
		rc = parity_request_create(rq, ce, d->tl_seqno[i],
			(uint32_t)d->tl_page[i]->ggtt_offset,
			(volatile uint32_t *)d->tl_page[i]->cpu);
		if (rc != 0)
			return fail(d, rc, "i915_request_create");

		rc = parity_emit_ctx_wa(rq, &gi->ctx_wa[i], m);
		if (rc != 0)
			return fail(d, rc, "intel_engine_emit_ctx_wa");

		/* intel_renderstate_emit(): no rodata on gen12. */

		rc = parity_request_add(rq);
		if (rc != 0)
			return fail(d, rc, "i915_request_add");

		rc = parity_execlists_submit(ge, &es->el[i], m, rq);
		if (rc != 0)
			return fail(d, rc, "execlists_submit");
		d->state[i] = PARITY_DEF_RECORD;
	}
	return 0;
}

/* The request has landed AND the engine has reported the context complete. */
static int
retired(struct parity_gt_request *rq, struct parity_execlists *el)
{
	return parity_request_completed(rq) && !el->have_active &&
		el->pending[0] == 0;
}

unsigned
parity_engines_record_defaults_poll(struct parity_gt_defaults *d,
	struct parity_gt_engines *es, struct osdep_mmio *m)
{
	unsigned busy = 0u;
	unsigned i;

	if (d == 0 || es == 0)
		return 0u;
	d->polls++;

	for (i = 0u; i < d->n; i++) {
		struct parity_gt_engine *ge = &es->ge[i];
		struct parity_execlists *el = &es->el[i];

		(void)parity_execlists_process_csb(ge, el, m);
		if (el->csb_errors != 0u)
			(void)fail(d, -EIO, "execlists CSB error");

		switch (d->state[i]) {
		case PARITY_DEF_RECORD:
			if (!retired(&d->rq[i], el))
				break;
			/*
			 * The engine parks.  switch_to_kernel_context(): already in
			 * the kernel context only if no request ran since the last
			 * park -- not the case here, submission advanced serial.
			 */
			if (el->wakeref_serial == el->serial) {
				d->state[i] = PARITY_DEF_PARKED;
				break;
			}
			{
				struct parity_gt_request *krq = &d->krq[i];
				uint32_t kseq = ++es->kernel_tl_seqno[i];
				int rc;

				rc = parity_request_create(krq, &es->kernel_ce[i], kseq,
					(uint32_t)ge->hwsp_ggtt + PARITY_I915_GEM_HWS_SEQNO_ADDR,
					&ge->hwsp[PARITY_I915_GEM_HWS_SEQNO_ADDR / 4u]);
				/* i915_request_add_active_barriers(): none are pending. */
				if (rc == 0)
					rc = parity_request_add(krq);
				/* "Check again on the next retirement." */
				el->wakeref_serial = el->serial + 1u;
				if (rc == 0)
					rc = parity_execlists_submit(ge, el, m, krq);
				if (rc != 0) {
					(void)fail(d, rc, "switch_to_kernel_context");
					break;
				}
				d->state[i] = PARITY_DEF_SWITCH;
			}
			break;
		case PARITY_DEF_SWITCH:
			if (retired(&d->krq[i], el) && el->wakeref_serial == el->serial)
				d->state[i] = PARITY_DEF_PARKED;
			break;
		default:
			break;
		}
		if (d->state[i] == PARITY_DEF_RECORD || d->state[i] == PARITY_DEF_SWITCH)
			busy++;
	}
	return busy;
}

int
parity_engines_record_defaults_finish(struct parity_gt_defaults *d,
	struct parity_gt_engines *es, struct parity_gt_mem *gm)
{
	unsigned i;

	if (d == 0 || es == 0 || gm == 0)
		return -EINVAL;

	/* rq->fence.error: an engine that reported a CSB error failed its request. */
	for (i = 0u; i < d->n; i++)
		if (es->el[i].csb_errors != 0u)
			return fail(d, -EIO, "rq->fence.error");

	/* shmem_create_from_object(rq->context->state->obj): the whole image. */
	for (i = 0u; i < d->n; i++) {
		struct parity_gt_object *o;

		o = parity_gt_object_create(gm, d->ce[i].state_bytes);
		if (o == 0)
			return fail(d, -ENOMEM, "shmem_create_from_object");
		memcpy(o->cpu, d->ce[i].state->cpu, d->ce[i].state_bytes);
		d->default_state[i] = o;
	}
	return 0;
}

static void
release_contexts(struct parity_gt_defaults *d, struct parity_gt_mem *gm)
{
	unsigned i;

	for (i = d->n; i-- > 0u; ) {
		if (d->tl_page[i] != 0) {
			parity_gt_object_destroy(gm, d->tl_page[i]);
			d->tl_page[i] = 0;
		}
		parity_lrc_release(&d->ce[i], gm);
	}
}

void
parity_engines_defaults_release(struct parity_gt_defaults *d, struct parity_gt_mem *gm)
{
	unsigned i;

	if (d == 0 || gm == 0)
		return;
	release_contexts(d, gm);
	for (i = 0u; i < PARITY_MAX_ENGINES; i++) {
		if (d->default_state[i] != 0) {
			parity_gt_object_destroy(gm, d->default_state[i]);
			d->default_state[i] = 0;
		}
	}
	d->n = 0u;
}

void
parity_engine_dump(struct parity_gt_engine *ge, struct parity_execlists *el,
	struct osdep_mmio *m, const char *why)
{
	uint32_t b;
	unsigned k;

	if (ge == 0 || m == 0)
		return;
	b = ge->info->mmio_base;
	kern_logf("i915: parity %s dump(%s): HEAD=%08x TAIL=%08x START=%08x CTL=%08x "
		"ACTHD=%08x:%08x IPEHR=%08x INSTDONE=%08x MI_MODE=%08x ESR=%08x EIR=%08x\n",
		ge->info->name, why,
		osdep_mmio_read32(m, b + 0x34u), osdep_mmio_read32(m, b + 0x30u),
		osdep_mmio_read32(m, b + 0x38u), osdep_mmio_read32(m, b + 0x3cu),
		osdep_mmio_read32(m, b + 0x5cu), osdep_mmio_read32(m, b + 0x74u),
		osdep_mmio_read32(m, b + 0x68u), osdep_mmio_read32(m, b + 0x6cu),
		osdep_mmio_read32(m, b + 0x9cu), osdep_mmio_read32(m, b + 0xb8u),
		osdep_mmio_read32(m, b + 0xb0u));
	kern_logf("i915: parity %s dump(%s): EXECLIST_STATUS=%08x:%08x CSB_PTR=%08x "
		"hwsp: csb_write=%u preempt=%u seqno=%u | sw: csb_head=%u submits=%u "
		"promotes=%u completes=%u errors=%u late=%u mmio=%u last=%08x:%08x\n",
		ge->info->name, why,
		osdep_mmio_read32(m, b + 0x238u), osdep_mmio_read32(m, b + 0x234u),
		osdep_mmio_read32(m, b + 0x3a0u),
		*ge->csb_write, ge->hwsp[PARITY_I915_GEM_HWS_PREEMPT],
		ge->hwsp[PARITY_I915_GEM_HWS_SEQNO_ADDR / 4u],
		ge->csb_head,
		el != 0 ? el->submits : 0u, el != 0 ? el->promotes : 0u,
		el != 0 ? el->completes : 0u, el != 0 ? el->csb_errors : 0u,
		el != 0 ? el->csb_late : 0u, el != 0 ? el->csb_mmio_fallback : 0u,
		el != 0 ? el->last_csb_hi : 0u, el != 0 ? el->last_csb_lo : 0u);
	for (k = 0u; k < ge->csb_size; k++)
		kern_logf("i915: parity %s dump(%s): csb[%u]=%08x:%08x\n",
			ge->info->name, why, k,
			(uint32_t)(ge->csb_status[k] >> 32), (uint32_t)ge->csb_status[k]);
}

int
parity_engines_record_defaults(struct parity_gt_defaults *d,
	struct parity_gt_engines *es, struct parity_gt_init *gi,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp,
	struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms)
{
	unsigned i;

	(void)parity_engines_record_defaults_submit(d, es, gi, gm, pp, m);

	if (d->err == 0) {
		/* intel_gt_wait_for_idle(gt, I915_GEM_IDLE_TIMEOUT): >= timeout_ms. */
		unsigned budget = timeout_ms * 20u;   /* 50 us per step */
		unsigned busy = 1u;
		unsigned k;

		for (k = 0u; k < budget; k++) {
			busy = parity_engines_record_defaults_poll(d, es, m);
			if (busy == 0u || d->err != 0)
				break;
			if (parity_udelay(50u) != 0) {
				(void)fail(d, -EIO, "time base");
				break;
			}
		}
		if (busy != 0u && d->err == 0) {
			d->timed_out = 1;
			(void)fail(d, -EIO, "intel_gt_wait_for_idle -ETIME");
		}
	}

	if (d->err == 0)
		(void)parity_engines_record_defaults_finish(d, es, gm);

	if (d->err != 0) {
		for (i = 0u; i < es->n; i++)
			parity_engine_dump(&es->ge[i], &es->el[i], m, d->err_where);
		/*
		 * intel_gt_set_wedged(): "the quickest way we can accomplish [idle
		 * engines ready for teardown] is by declaring ourselves wedged",
		 * which stops the engines and resets them.
		 */
		for (i = 0u; i < es->n; i++)
			parity_execlists_reset_prepare(&es->ge[i], m);
		(void)parity_gt_reset_all(uncore_lock, m, 2000u);
		d->wedged = 1;
	}

	/* out: put the requests and their contexts. */
	release_contexts(d, gm);
	return d->err;
}

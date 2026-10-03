/*
 * WS031 Linux-parity — P6-c5: __engines_verify_workarounds().
 * See gt_verify_wa.h for the reference sequence and the recorded adaptations.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include <string.h>
#include "gt_mem.h"
#include "gt_init.h"
#include "gt_verify_wa.h"
#include "wait.h"
#include "osdep/mmio.h"

/* gt/intel_workarounds.c: mcr_ranges_gen12[] */
static const struct { uint32_t start, end; } mcr_ranges_gen12[] = {
	{ 0x8150u,  0x815fu },
	{ 0x9520u,  0x955fu },
	{ 0xb100u,  0xb3ffu },
	{ 0xde80u,  0xe8ffu },
	{ 0x24a00u, 0x24a7fu },
};

int
parity_gen12_mcr_range(uint32_t offset)
{
	unsigned i;

	for (i = 0u; i < sizeof(mcr_ranges_gen12) / sizeof(mcr_ranges_gen12[0]); i++)
		if (offset >= mcr_ranges_gen12[i].start && offset <= mcr_ranges_gen12[i].end)
			return 1;
	return 0;
}

static int
fail(struct parity_gt_verify_wa *v, int rc, const char *where)
{
	if (v->err == 0) {
		v->err = rc;
		v->err_where = where;
	}
	return rc;
}

int
parity_wa_list_srm(struct parity_gt_request *rq, const struct parity_wa_list *wal,
	uint32_t scratch_ggtt, unsigned *emitted, unsigned *skipped)
{
	uint32_t srm = PARITY_MI_STORE_REGISTER_MEM_GEN8 | PARITY_MI_SRM_LRM_GLOBAL_GTT;
	unsigned i, count = 0u;
	uint32_t *cs;

	if (rq == 0 || wal == 0)
		return -EINVAL;
	for (i = 0u; i < wal->count; i++)
		if (!parity_gen12_mcr_range(wal->list[i].reg))
			count++;
	if (emitted != 0)
		*emitted = count;
	if (skipped != 0)
		*skipped = wal->count - count;

	cs = parity_ring_begin(rq, 4u * count);
	if (cs == 0)
		return rq->error != 0 ? rq->error : -ENOSPC;
	for (i = 0u; i < wal->count; i++) {
		uint32_t offset = wal->list[i].reg;

		if (parity_gen12_mcr_range(offset))
			continue;
		*cs++ = srm;
		*cs++ = offset;
		*cs++ = scratch_ggtt + 4u * i;   /* the LIST index, gaps included */
		*cs++ = 0u;
	}
	parity_ring_advance(rq, cs);
	return 0;
}

int
parity_wa_list_check(struct parity_gt_verify_wa *v, unsigned i,
	const struct parity_wa_list *wal, const char *from)
{
	const volatile uint32_t *results;
	unsigned k;
	int rc = 0;

	if (v == 0 || wal == 0 || i >= PARITY_MAX_ENGINES || v->scratch[i] == 0)
		return -EINVAL;
	results = (const volatile uint32_t *)v->scratch[i]->cpu;
	v->verified[i] = 0u;
	v->mismatched[i] = 0u;
	v->not_verifiable[i] = 0u;
	for (k = 0u; k < wal->count; k++) {
		const struct parity_wa *w = &wal->list[k];
		uint32_t cur;

		if (parity_gen12_mcr_range(w->reg))
			continue;
		cur = results[k];
		if (w->read_mask == 0u) {
			/* wa->read == 0: (cur ^ set) & 0 never fires. */
			v->not_verifiable[i]++;
			continue;
		}
		/* wa_verify() */
		if (((cur ^ w->set) & w->read_mask) != 0u) {
			kern_logf("i915: parity %s workaround lost on %s! (reg[%x]=0x%x, "
				"relevant bits were 0x%x vs expected 0x%x) %s\n",
				wal->name != 0 ? wal->name : "?", from, w->reg, cur,
				cur & w->read_mask, w->set & w->read_mask,
				w->name != 0 ? w->name : "");
			v->mismatched[i]++;
			rc = -ENXIO;
		} else {
			v->verified[i]++;
		}
	}
	return rc;
}

int
parity_engine_verify_wa_submit(struct parity_gt_verify_wa *v, unsigned i,
	struct parity_gt_engines *es, const struct parity_wa_list *wal,
	struct parity_gt_mem *gm, struct osdep_mmio *m)
{
	struct parity_gt_engine *ge;
	struct parity_gt_request *rq;
	uint32_t kseq;
	int rc;

	if (v == 0 || es == 0 || wal == 0 || gm == 0 || m == 0 ||
	    i >= es->n || i >= PARITY_MAX_ENGINES)
		return -EINVAL;
	ge = &es->ge[i];
	rq = &v->rq[i];
	v->list_count[i] = wal->count;
	v->engine_err[i] = 0;
	if (i + 1u > v->n)
		v->n = i + 1u;

	/* if (!wal->count) return 0: nothing is submitted, the engine stays parked. */
	if (wal->count == 0u) {
		v->state[i] = PARITY_VWA_IDLE;
		return 0;
	}

	/* __vm_create_scratch_for_read(&ggtt->vm, count * 4): one page, PIN_GLOBAL. */
	v->scratch[i] = parity_gt_object_create(gm, 4096u);
	if (v->scratch[i] == 0)
		return fail(v, -ENOMEM, "__vm_create_scratch_for_read");
	memset(v->scratch[i]->cpu, 0, 4096u);
	rc = parity_gt_ggtt_bind(gm, v->scratch[i]);
	if (rc != 0)
		return fail(v, rc, "i915_vma_pin_ww");

	/* intel_engine_pm_get(); i915_request_create(engine->kernel_context). */
	kseq = ++es->kernel_tl_seqno[i];
	rc = parity_request_create(rq, &es->kernel_ce[i], kseq,
		(uint32_t)ge->hwsp_ggtt + PARITY_I915_GEM_HWS_SEQNO_ADDR,
		&ge->hwsp[PARITY_I915_GEM_HWS_SEQNO_ADDR / 4u]);
	if (rc != 0)
		return fail(v, rc, "i915_request_create");

	/* i915_vma_move_to_active(): fence bookkeeping only.  wa_list_srm(). */
	rc = parity_wa_list_srm(rq, wal, (uint32_t)v->scratch[i]->ggtt_offset,
		&v->emitted[i], &v->mcr_skipped[i]);
	if (rc != 0)
		return fail(v, rc, "wa_list_srm");

	rc = parity_request_add(rq);
	if (rc != 0)
		return fail(v, rc, "i915_request_add");
	rc = parity_execlists_submit(ge, &es->el[i], m, rq);
	if (rc != 0)
		return fail(v, rc, "execlists_submit");
	v->state[i] = PARITY_VWA_SRM;
	return 0;
}

/* The request has landed AND the engine has reported the context complete. */
static int
retired(struct parity_gt_request *rq, struct parity_execlists *el)
{
	return parity_request_completed(rq) && !el->have_active &&
		el->pending[0] == 0;
}

int
parity_engine_verify_wa_poll(struct parity_gt_verify_wa *v, unsigned i,
	struct parity_gt_engines *es, struct osdep_mmio *m)
{
	struct parity_execlists *el;

	if (v == 0 || es == 0 || i >= v->n)
		return 0;
	el = &es->el[i];
	v->polls++;
	(void)parity_execlists_process_csb(&es->ge[i], el, m);
	if (el->csb_errors != 0u)
		(void)fail(v, -EIO, "execlists CSB error");

	switch (v->state[i]) {
	case PARITY_VWA_SRM:
		if (retired(&v->rq[i], el))
			v->state[i] = PARITY_VWA_DONE;
		break;
	case PARITY_VWA_SWITCH:
		if (retired(&v->krq[i], el) && el->wakeref_serial == el->serial)
			v->state[i] = PARITY_VWA_PARKED;
		break;
	default:
		break;
	}
	return v->state[i] == PARITY_VWA_SRM || v->state[i] == PARITY_VWA_SWITCH;
}

int
parity_engine_verify_wa_park(struct parity_gt_verify_wa *v, unsigned i,
	struct parity_gt_engines *es, struct osdep_mmio *m)
{
	struct parity_gt_engine *ge;
	struct parity_execlists *el;
	struct parity_gt_request *krq;
	uint32_t kseq;
	int rc;

	if (v == 0 || es == 0 || i >= v->n || v->state[i] != PARITY_VWA_DONE)
		return -EINVAL;
	ge = &es->ge[i];
	el = &es->el[i];
	krq = &v->krq[i];

	/* switch_to_kernel_context(): already there only if nothing ran since. */
	if (el->wakeref_serial == el->serial) {
		v->state[i] = PARITY_VWA_PARKED;
		return 0;
	}
	kseq = ++es->kernel_tl_seqno[i];
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
	if (rc != 0)
		return fail(v, rc, "switch_to_kernel_context");
	v->state[i] = PARITY_VWA_SWITCH;
	return 0;
}

/* Polls engine i until `want` (or timeout); returns 0, -ETIME or -EIO. */
static int
wait_engine(struct parity_gt_verify_wa *v, unsigned i,
	struct parity_gt_engines *es, struct osdep_mmio *m, int want,
	unsigned timeout_ms)
{
	unsigned budget = timeout_ms * 20u;   /* 50 us per step */
	unsigned k;

	for (k = 0u; k < budget; k++) {
		(void)parity_engine_verify_wa_poll(v, i, es, m);
		if (v->state[i] == want)
			return 0;
		if (v->err != 0 && v->err != -EIO)
			return v->err;
		if (es->el[i].csb_errors != 0u)
			return -EIO;
		if (parity_udelay(50u) != 0)
			return fail(v, -EIO, "time base");
	}
	return -ETIME;
}

int
parity_engines_verify_workarounds(struct parity_gt_verify_wa *v,
	struct parity_gt_engines *es, struct parity_gt_init *gi,
	struct parity_gt_mem *gm, struct osdep_mmio *m, unsigned timeout_ms)
{
	unsigned i;
	int rc;

	if (v == 0 || es == 0 || gi == 0 || gm == 0 || m == 0)
		return -EINVAL;
	for (i = 0u; i < sizeof(*v); i++)
		((char *)v)[i] = 0;

	/* for_each_engine(): intel_engine_verify_workarounds(engine, "load"). */
	for (i = 0u; i < es->n && i < 6u; i++) {
		const struct parity_wa_list *wal = &gi->engine_wa[i];

		rc = parity_engine_verify_wa_submit(v, i, es, wal, gm, m);
		if (rc != 0) {
			v->engine_err[i] = rc;
			(void)fail(v, -EIO, v->err_where);
			continue;
		}
		if (v->state[i] == PARITY_VWA_IDLE)
			continue;

		/* i915_request_wait(rq, 0, HZ / 5) */
		rc = wait_engine(v, i, es, m, PARITY_VWA_DONE, timeout_ms);
		if (rc != 0) {
			v->engine_err[i] = rc;
			if (rc == -ETIME) {
				v->timed_out = 1;
				(void)fail(v, -EIO, "i915_request_wait -ETIME");
			} else {
				(void)fail(v, -EIO, "i915_request_wait");
			}
			/* No park switch behind a hung request (adaptation). */
			continue;
		}

		/* results: wa_verify() each entry. */
		rc = parity_wa_list_check(v, i, wal, "load");
		if (rc != 0) {
			v->engine_err[i] = rc;
			(void)fail(v, -EIO, "wa_verify");
		}

		/* intel_engine_pm_put(): park, switch to the kernel context. */
		rc = parity_engine_verify_wa_park(v, i, es, m);
		if (rc != 0) {
			v->engine_err[i] = rc;
			(void)fail(v, -EIO, "switch_to_kernel_context");
		}
	}

	/* "Flush and restore the kernel context for safety": wait_for_idle. */
	for (i = 0u; i < v->n; i++) {
		if (v->state[i] != PARITY_VWA_SWITCH)
			continue;
		rc = wait_engine(v, i, es, m, PARITY_VWA_PARKED, timeout_ms);
		if (rc != 0) {
			if (v->engine_err[i] == 0)
				v->engine_err[i] = rc;
			if (rc == -ETIME)
				v->timed_out = 1;
			(void)fail(v, -EIO, rc == -ETIME ?
				"intel_gt_wait_for_idle -ETIME" : "intel_gt_wait_for_idle");
		}
	}
	return v->err;
}

void
parity_engines_verify_wa_release(struct parity_gt_verify_wa *v,
	struct parity_gt_mem *gm)
{
	unsigned i;

	if (v == 0 || gm == 0)
		return;
	for (i = 0u; i < PARITY_MAX_ENGINES; i++) {
		if (v->scratch[i] != 0) {
			parity_gt_object_destroy(gm, v->scratch[i]);
			v->scratch[i] = 0;
		}
	}
	v->n = 0u;
}

/*
 * WS031 Linux-parity — P6-c3b: execlists submission and CSB processing.
 * See gt_submit.h for the reference mapping and the recorded adaptation.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/device-io.h>
#include <errno.h>
#include "gt_mmio.h"
#include "gt_mem.h"
#include "gt_engine.h"
#include "gt_lrc.h"
#include "gt_request.h"
#include "gt_submit.h"
#include "osdep/mmio.h"
#include "wait.h"

#define CSB_UNWRITTEN (~(uint64_t)0)

void
parity_execlists_init(struct parity_execlists *el)
{
	unsigned i;

	if (el == 0)
		return;
	for (i = 0u; i < sizeof(*el); i++)
		((char *)el)[i] = 0;
	/* engine->context_tag = GENMASK(BITS_PER_LONG - 2, 0). */
	el->context_tag = (((uint64_t)1) << 63) - 1u;
}

int
parity_gen12_csb_parse(uint64_t csb)
{
	uint32_t lo = (uint32_t)csb;
	uint32_t hi = (uint32_t)(csb >> 32);
	int ctx_to_valid = PARITY_GEN12_CSB_SW_CTX_ID(lo) != PARITY_GEN12_IDLE_CTX_ID;
	int ctx_away_valid = PARITY_GEN12_CSB_SW_CTX_ID(hi) != PARITY_GEN12_IDLE_CTX_ID;
	int new_queue = (lo & PARITY_GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE) != 0u;

	(void)ctx_to_valid;
	/*
	 * __gen12_csb_parse(): nothing switched away, or a new queue was
	 * loaded, means the pending ports were promoted.  Anything else is the
	 * active context completing (switch detail is 0: we never use an
	 * unsuccessful WAIT, we always poll).
	 */
	if (!ctx_away_valid || new_queue)
		return 1;
	(void)PARITY_GEN12_CTX_SWITCH_DETAIL(hi);
	return 0;
}

int
parity_request_completed(const struct parity_gt_request *rq)
{
	if (rq == 0 || rq->hwsp_cpu == 0)
		return 0;
	/* i915_seqno_passed(): a signed difference, so the seqno may wrap. */
	return (int32_t)(*rq->hwsp_cpu - rq->seqno) >= 0;
}

/* --- __execlists_schedule_in / __execlists_schedule_out ------------------- */

static int
schedule_in(struct parity_gt_engine *ge, struct parity_execlists *el,
	struct parity_gt_request *rq)
{
	struct parity_gt_context *ce = rq->ce;
	uint32_t ccid;
	int tag;

	if (el->context_tag == 0u)
		return -EBUSY;
	/* __ffs(): the lowest free id. */
	for (tag = 0; ((el->context_tag >> tag) & 1u) == 0u; tag++)
		;
	el->context_tag &= ~(((uint64_t)1) << tag);

	/* GRAPHICS_VER_FULL < 12.50: (1 + tag) << (GEN11_SW_CTX_ID_SHIFT - 32). */
	ccid = ((uint32_t)(1 + tag)) << (PARITY_GEN11_SW_CTX_ID_SHIFT - 32);
	ccid |= ge->ccid;
	ce->lrc_desc = (((uint64_t)ccid) << 32) | (uint32_t)ce->lrc_desc;
	ce->tag = tag;
	return 0;
}

static void
schedule_out(struct parity_execlists *el, struct parity_gt_request *rq)
{
	struct parity_gt_context *ce = rq->ce;

	if (ce->tag >= 0 && ce->tag < 63) {
		el->context_tag |= ((uint64_t)1) << ce->tag;
		ce->tag = -1;
	}
}

/* --- execlists_update_context --------------------------------------------- */

static uint64_t
update_context(struct parity_gt_request *rq)
{
	struct parity_gt_context *ce = rq->ce;
	uint64_t desc = ce->lrc_desc;
	uint32_t prev = ce->ring.tail;
	uint32_t tail = rq->tail;
	int32_t dir;

	/* intel_ring_set_tail() */
	ce->ring.tail = tail;

	/*
	 * intel_ring_direction(): the sign of (next - prev) within the ring,
	 * i.e. shifted up by ring->wrap = 32 - ilog2(size).  Resubmitting the
	 * same (or an earlier) tail must force a full restore.
	 */
	{
		unsigned wrap = 32u, s = ce->ring.size;

		while (s > 1u) { s >>= 1; wrap--; }
		dir = (int32_t)((tail - prev) << wrap);
	}
	if (dir <= 0)
		desc |= PARITY_CTX_DESC_FORCE_RESTORE;

	ce->lrc_reg_state[PARITY_CTX_RING_TAIL] = tail;
	rq->tail = rq->wa_tail;

	/* The context image must be complete before the (uncached) ELSQ write. */
	kern_io_write_barrier();

	ce->lrc_desc &= ~(uint64_t)PARITY_CTX_DESC_FORCE_RESTORE;
	return desc;
}

/* --- execlists_submit_ports ----------------------------------------------- */

static void
write_desc(struct parity_gt_engine *ge, struct osdep_mmio *m, uint64_t desc,
	unsigned port)
{
	/* ELSQ: submit_reg + port * 2 dwords; lower dword first, then upper. */
	osdep_mmio_write32(m, ge->submit_reg + port * 8u, (uint32_t)desc);
	osdep_mmio_write32(m, ge->submit_reg + port * 8u + 4u, (uint32_t)(desc >> 32));
}

int
parity_execlists_submit(struct parity_gt_engine *ge,
	struct parity_execlists *el, struct osdep_mmio *m,
	struct parity_gt_request *rq)
{
	unsigned n;
	int rc;

	if (ge == 0 || el == 0 || m == 0 || rq == 0 || !rq->added)
		return -EINVAL;
	/* One request in flight at a time (see the header). */
	if (el->have_active || el->pending[0] != 0)
		return -EBUSY;

	rc = schedule_in(ge, el, rq);
	if (rc != 0)
		return rc;

	el->pending[0] = rq;
	el->pending[1] = 0;

	/*
	 * The ELSQ is not cleared after it is submitted, so BOTH ports are
	 * always written -- the empty one with 0 -- highest port first.
	 */
	for (n = 2u; n-- > 0u; ) {
		struct parity_gt_request *r = el->pending[n];

		write_desc(ge, m, r != 0 ? update_context(r) : 0u, n);
	}
	/* The submit queue is loaded by hand. */
	osdep_mmio_write32(m, ge->ctrl_reg, PARITY_EL_CTRL_LOAD);

	el->serial++;
	el->submits++;
	return 0;
}

/* --- csb_read / wa_csb_read ------------------------------------------------ */

static uint64_t
csb_read(struct parity_gt_engine *ge, struct parity_execlists *el,
	struct osdep_mmio *m, unsigned idx)
{
	volatile uint64_t *slot = &ge->csb_status[idx];
	uint64_t entry = *slot;

	if (entry == CSB_UNWRITTEN) {
		unsigned us;

		/*
		 * The GPU does not always make the entry visible before the write
		 * pointer.  Give it 10 us (preempt-off busy wait in the reference).
		 */
		for (us = 0u; us < 10u && entry == CSB_UNWRITTEN; us++) {
			(void)parity_udelay(1u);
			entry = *slot;
		}
		if (entry == CSB_UNWRITTEN) {
			uint32_t status = PARITY_GEN8_EXECLISTS_STATUS_BUF;
			unsigned j = idx;
			uint32_t reg;

			if (j >= 6u) {
				status = PARITY_GEN11_EXECLISTS_STATUS_BUF2;
				j -= 6u;
			}
			reg = ge->info->mmio_base + status + 8u * j;
			/*
			 * intel_uncore_read64(); the osdep layer is 32-bit, so the
			 * two halves are read lower first (recorded).
			 */
			entry = (uint64_t)osdep_mmio_read32(m, reg) |
				((uint64_t)osdep_mmio_read32(m, reg + 4u) << 32);
			el->csb_mmio_fallback++;
		} else {
			el->csb_late++;
		}
	}

	/* Consume the entry so a future reuse of the slot can be spotted. */
	*slot = CSB_UNWRITTEN;
	return entry;
}

/* --- process_csb ----------------------------------------------------------- */

struct parity_gt_request *
parity_execlists_process_csb(struct parity_gt_engine *ge,
	struct parity_execlists *el, struct osdep_mmio *m)
{
	struct parity_gt_request *done = 0;
	unsigned head, tail;

	if (ge == 0 || el == 0)
		return 0;

	head = ge->csb_head;
	tail = *ge->csb_write;
	if (head == tail)
		return 0;
	/* A pointer beyond the ring is not something to walk. */
	if (tail >= ge->csb_size) {
		el->csb_errors++;
		return 0;
	}
	ge->csb_head = tail;
	kern_io_read_barrier();

	do {
		uint64_t csb;

		if (++head == ge->csb_size)
			head = 0u;
		csb = csb_read(ge, el, m, head);
		el->csb_events++;
		el->last_csb_lo = (uint32_t)csb;
		el->last_csb_hi = (uint32_t)(csb >> 32);

		if (parity_gen12_csb_parse(csb)) {
			if (el->pending[0] == 0) {
				el->csb_errors++;   /* ERROR_CSB */
				break;
			}
			/* Anything that was active is switched out (preempted). */
			el->inflight[0] = el->pending[0];
			el->inflight[1] = el->pending[1];
			el->have_active = 1;
			el->pending[0] = 0;
			el->pending[1] = 0;
			el->promotes++;
		} else {
			struct parity_gt_request *rq;

			if (!el->have_active || el->inflight[0] == 0) {
				el->csb_errors++;   /* ERROR_CSB */
				break;
			}
			rq = el->inflight[0];
			el->inflight[0] = el->inflight[1];
			el->inflight[1] = 0;
			if (el->inflight[0] == 0)
				el->have_active = 0;
			schedule_out(el, rq);
			el->completes++;
			done = rq;
		}
	} while (head != tail);

	return done;
}

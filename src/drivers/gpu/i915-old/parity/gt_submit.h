/*
 * WS031 Linux-parity — P6-c3b: execlists submission through the ELSQ, and the
 * context status buffer that reports what the engine did with it.
 *
 * What this file stands for in the reference (intel_execlists_submission.c):
 *
 *   __execlists_schedule_in()   give the context a SW context id ("tag") and
 *                               build lrc.ccid = (1+tag) << 5 | engine ccid
 *   execlists_update_context()  program CTX_RING_TAIL = rq->tail in the image,
 *                               force a restore if the tail did not advance,
 *                               then move rq->tail on to rq->wa_tail
 *   execlists_submit_ports()    write both ELSQ ports, HIGHEST port first, each
 *                               as lower then upper dword, then EL_CTRL_LOAD
 *   process_csb()               walk the CSB from the cached head to the
 *                               HWSP write pointer; gen12_csb_parse() decides
 *                               "promote" (pending -> active) vs "complete"
 *   csb_read() / wa_csb_read()  an entry still reading all-ones may just not be
 *                               globally visible yet (tgl:HSDES#22011248461):
 *                               poll it for 10 us, then fall back to the MMIO
 *                               status buffer; poison it after reading
 *   execlists_schedule_out()    give the tag back
 *
 * A request is complete when its breadcrumb has landed:
 * i915_seqno_passed(*hwsp, seqno), which is a signed 32-bit comparison.
 *
 * ADAPTATION (recorded): the reference is a tasklet run from the CS interrupt,
 * with a priority queue behind it.  The only user here is
 * __engines_record_defaults(), which puts exactly one request on an idle
 * engine at a time, so this keeps one request in flight and runs the CSB
 * processing synchronously -- which is also what the reference does on the
 * wait path, where intel_engine_flush_submission() runs the tasklet body
 * inline.  The interrupt still arrives and is still acknowledged by the P4
 * handler; it just is not what drives the processing.
 */
#ifndef PARITY_GT_SUBMIT_H
#define PARITY_GT_SUBMIT_H

#include <stdint.h>

struct osdep_mmio;
struct parity_gt_engine;
struct parity_gt_request;

/* gen12 CSB fields (intel_execlists_submission.c). */
#define PARITY_GEN12_CTX_STATUS_SWITCHED_TO_NEW_QUEUE 0x1u     /* lower dword */
#define PARITY_GEN12_CTX_SWITCH_DETAIL(dw)            ((dw) & 0xfu) /* upper */
#define PARITY_GEN12_CSB_SW_CTX_ID(dw)                (((dw) >> 15) & 0x7ffu)
#define PARITY_GEN12_IDLE_CTX_ID                      0x7ffu

/* The MMIO copy of the CSB, used when the HWSP entry is not yet visible. */
#define PARITY_GEN8_EXECLISTS_STATUS_BUF   0x370u
#define PARITY_GEN11_EXECLISTS_STATUS_BUF2 0x3c0u

struct parity_execlists {
	struct parity_gt_request *pending[2];
	struct parity_gt_request *inflight[2];
	int have_active;                  /* inflight[0] is running */

	uint64_t context_tag;             /* free SW context ids */
	unsigned serial;                  /* engine->serial */
	unsigned wakeref_serial;          /* engine->wakeref_serial */

	/* Diagnostics: what the CSB said, kept for the report. */
	unsigned csb_events;
	unsigned promotes;
	unsigned completes;
	unsigned csb_late;                /* read all-ones, then became visible */
	unsigned csb_mmio_fallback;       /* never became visible in 10 us */
	unsigned csb_errors;              /* an event with nothing to apply it to */
	uint32_t last_csb_lo;
	uint32_t last_csb_hi;
	unsigned submits;
};

void parity_execlists_init(struct parity_execlists *el);

/* gen12_csb_parse(): 1 = promote (pending -> active), 0 = completion. */
int parity_gen12_csb_parse(uint64_t csb);

/* i915_seqno_passed(*hwsp, seqno). */
int parity_request_completed(const struct parity_gt_request *rq);

/* schedule_in + update_context + submit_ports, for an idle engine. */
int parity_execlists_submit(struct parity_gt_engine *ge,
	struct parity_execlists *el, struct osdep_mmio *m,
	struct parity_gt_request *rq);

/*
 * process_csb(): returns the request that completed, or 0.  The caller learns
 * of errors through el->csb_errors.
 */
struct parity_gt_request *parity_execlists_process_csb(
	struct parity_gt_engine *ge, struct parity_execlists *el,
	struct osdep_mmio *m);

#endif /* PARITY_GT_SUBMIT_H */

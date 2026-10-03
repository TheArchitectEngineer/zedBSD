/*
 * WS031 Linux-parity — P6-c4b: __engines_record_defaults().
 *
 * The reference, per engine:
 *   ce = intel_context_create(engine)        4 KiB ring; a NEW timeline, which
 *                                            gets its own 4 KiB HWSP page
 *                                            (hwsp_alloc) and
 *                                            has_initial_breadcrumb = true, so
 *                                            its seqnos advance by 2
 *   intel_renderstate_init(&so, ce)          pins ce (lrc_init_state with the
 *                                            restore INHIBITED, update_regs);
 *                                            no rodata on gen12
 *   rq = i915_request_create(ce)             request_alloc: EMIT_INVALIDATE
 *   intel_engine_emit_ctx_wa(rq)
 *   intel_renderstate_emit(&so, rq)          nothing on gen12
 *   i915_request_add(rq)                     breadcrumb, submit
 * then intel_gt_wait_for_idle(gt, I915_GEM_IDLE_TIMEOUT = HZ/5):
 *   each request retires once its seqno lands; the engine then parks, and
 *   park runs switch_to_kernel_context(): unless engine->wakeref_serial ==
 *   engine->serial, a request on the pinned kernel context is submitted, so
 *   the record context is switched OUT and its image written back.  Only when
 *   that second request has also retired is the engine idle.
 * then, per engine: fence error -> -EIO; otherwise the record context's whole
 * image becomes engine->default_state (shmem_create_from_object).
 * On any error the GT is wedged, which resets the engines.
 *
 * Because the context was created with the restore inhibited, the engine does
 * not load it: it runs from its post-reset register state and SAVES that into
 * the image on switch-out.  That saved image is the "default state".
 *
 * ADAPTATIONS (recorded):
 *   - The wait is a poll of the CSB and the HWSP (what
 *     intel_engine_flush_submission() does inline on the wait path), bounded
 *     by >= 200 ms of real time, instead of a retire worker.
 *   - The park switch is submitted after the record context has COMPLETED
 *     (CSB), not merely after its seqno landed, because this port keeps one
 *     request in flight per engine.  The words submitted are the same.
 */
#ifndef PARITY_GT_DEFAULTS_H
#define PARITY_GT_DEFAULTS_H

#include <stdint.h>
#include "gt_resume.h"
#include "gt_request.h"

struct parity_gt_init;
struct parity_gt_mem;
struct parity_gt_ppgtt;

/* Per-engine progress through the sequence above. */
#define PARITY_DEF_IDLE     0
#define PARITY_DEF_RECORD   1   /* record request in flight */
#define PARITY_DEF_SWITCH   2   /* park switch to the kernel context in flight */
#define PARITY_DEF_PARKED   3

struct parity_gt_defaults {
	unsigned n;
	struct parity_gt_context ce[PARITY_MAX_ENGINES];
	struct parity_gt_object *tl_page[PARITY_MAX_ENGINES];
	uint32_t tl_seqno[PARITY_MAX_ENGINES];
	struct parity_gt_request rq[PARITY_MAX_ENGINES];
	struct parity_gt_request krq[PARITY_MAX_ENGINES];
	struct parity_gt_object *default_state[PARITY_MAX_ENGINES];
	int state[PARITY_MAX_ENGINES];
	int err;
	const char *err_where;
	unsigned polls;
	int timed_out;
	int wedged;
};

int parity_engines_record_defaults_submit(struct parity_gt_defaults *d,
	struct parity_gt_engines *es, struct parity_gt_init *gi,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp, struct osdep_mmio *m);

/* One pass of the wait: returns how many engines are not parked yet. */
unsigned parity_engines_record_defaults_poll(struct parity_gt_defaults *d,
	struct parity_gt_engines *es, struct osdep_mmio *m);

/* Fence errors, then the default state copies, then release the contexts. */
int parity_engines_record_defaults_finish(struct parity_gt_defaults *d,
	struct parity_gt_engines *es, struct parity_gt_mem *gm);

/* The whole of __engines_record_defaults(), with the wedge on error. */
int parity_engines_record_defaults(struct parity_gt_defaults *d,
	struct parity_gt_engines *es, struct parity_gt_init *gi,
	struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp,
	struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms);

/* Log an engine's command-streamer state (the hang report). */
void parity_engine_dump(struct parity_gt_engine *ge, struct parity_execlists *el,
	struct osdep_mmio *m, const char *why);

void parity_engines_defaults_release(struct parity_gt_defaults *d,
	struct parity_gt_mem *gm);

#endif /* PARITY_GT_DEFAULTS_H */

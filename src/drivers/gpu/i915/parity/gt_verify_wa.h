/*
 * WS031 Linux-parity — P6-c5: __engines_verify_workarounds().
 *
 * The reference (gt/intel_gt.c, gt/intel_workarounds.c, 6.8.12), per engine:
 *   intel_engine_verify_workarounds(engine, "load")
 *     = engine_wa_list_verify(engine->kernel_context, &engine->wa_list)
 *       if (!wal->count) return 0                 no request at all
 *       vma = __vm_create_scratch_for_read(ggtt, count * 4)   one page, GGTT
 *       intel_engine_pm_get(engine)               unpark
 *       rq = i915_request_create(ce)              request_alloc: EMIT_INVALIDATE
 *       wa_list_srm(rq, wal, vma)                 MI_STORE_REGISTER_MEM_GEN8 |
 *                                                 GLOBAL_GTT per entry, to
 *                                                 vma + 4 * i (i = list index),
 *                                                 SKIPPING the MCR ranges: the
 *                                                 MCR selector steers CPU MMIO
 *                                                 only, so the CS would read
 *                                                 some other instance
 *       i915_request_add(rq)                      breadcrumb, submit
 *       i915_request_wait(rq, 0, HZ / 5)          -ETIME
 *       results[i]: wa_verify(): ((cur ^ wa->set) & wa->read) != 0 -> -ENXIO
 *                   (wa->read == 0, the NO_VERIFY entries, always pass)
 *       intel_engine_pm_put(engine)               park -> switch_to_kernel_context
 *   any engine error -> -EIO
 *   intel_gt_wait_for_idle(gt, I915_GEM_IDLE_TIMEOUT) == -ETIME -> -EIO
 * and in intel_gt_init() an error here is fatal (err_gt: __intel_gt_disable).
 *
 * This is the first request of this port that makes the GPU READ registers
 * and write memory (the record request was an LRI + breadcrumb).  It is
 * compiled under CONFIG_DRM_I915_DEBUG_GEM in the reference; here it is a
 * permanent diagnostic (approved).
 *
 * ADAPTATIONS (recorded):
 *   - The wait is the same CSB/HWSP poll as record_defaults (>= 200 ms).
 *   - The park switch is submitted once the SRM request has COMPLETED (CSB),
 *     this port keeping one request in flight per engine.  On a timed-out
 *     engine no park switch is submitted (the engine is hung; the reference's
 *     would queue behind the hang and fail the final wait the same way).
 *   - The results page is a driver object from the fixed pool, not shmem.
 */
#ifndef PARITY_GT_VERIFY_WA_H
#define PARITY_GT_VERIFY_WA_H

#include <stdint.h>
#include <errno.h>
#include "gt_resume.h"
#include "gt_request.h"

/* The reference reports a wait timeout as -ETIME; this errno set has no ETIME. */
#ifndef ETIME
#define ETIME ETIMEDOUT
#endif

struct parity_gt_init;
struct parity_gt_mem;
struct parity_wa_list;

/* intel_gpu_commands.h: MI_INSTR(0x24, 2) | MI_SRM_LRM_GLOBAL_GTT. */
#define PARITY_MI_STORE_REGISTER_MEM_GEN8  ((0x24u << 23) | 2u)
#define PARITY_MI_SRM_LRM_GLOBAL_GTT       (1u << 22)

/* Per-engine progress. */
#define PARITY_VWA_IDLE     0   /* no list, or not started */
#define PARITY_VWA_SRM      1   /* the SRM request is in flight */
#define PARITY_VWA_DONE     2   /* it retired; results are readable */
#define PARITY_VWA_SWITCH   3   /* park switch to the kernel context in flight */
#define PARITY_VWA_PARKED   4

struct parity_gt_verify_wa {
	unsigned n;
	struct parity_gt_object *scratch[PARITY_MAX_ENGINES];
	struct parity_gt_request rq[PARITY_MAX_ENGINES];
	struct parity_gt_request krq[PARITY_MAX_ENGINES];
	int state[PARITY_MAX_ENGINES];

	/* wa_list_srm() / wa_verify() accounting, per engine. */
	unsigned list_count[PARITY_MAX_ENGINES];
	unsigned emitted[PARITY_MAX_ENGINES];        /* SRMs in the ring */
	unsigned mcr_skipped[PARITY_MAX_ENGINES];
	unsigned verified[PARITY_MAX_ENGINES];
	unsigned mismatched[PARITY_MAX_ENGINES];
	unsigned not_verifiable[PARITY_MAX_ENGINES]; /* read mask 0 */
	int engine_err[PARITY_MAX_ENGINES];          /* -ETIME / -ENXIO / other */

	int err;                 /* the reference's: -EIO once anything failed */
	const char *err_where;
	unsigned polls;
	int timed_out;
};

/* mcr_range() for GRAPHICS_VER 12 (< 12.50): mcr_ranges_gen12. */
int parity_gen12_mcr_range(uint32_t offset);

/* wa_list_srm(): the SRMs into a request; *emitted / *skipped report the split. */
int parity_wa_list_srm(struct parity_gt_request *rq,
	const struct parity_wa_list *wal, uint32_t scratch_ggtt,
	unsigned *emitted, unsigned *skipped);

/* wa_verify() over the results page; -ENXIO when any entry is lost. */
int parity_wa_list_check(struct parity_gt_verify_wa *v, unsigned i,
	const struct parity_wa_list *wal, const char *from);

/* engine_wa_list_verify() up to i915_request_add(): scratch + request + submit. */
int parity_engine_verify_wa_submit(struct parity_gt_verify_wa *v, unsigned i,
	struct parity_gt_engines *es, const struct parity_wa_list *wal,
	struct parity_gt_mem *gm, struct osdep_mmio *m);

/* One pass over the CSB/HWSP; returns nonzero while engine i is busy. */
int parity_engine_verify_wa_poll(struct parity_gt_verify_wa *v, unsigned i,
	struct parity_gt_engines *es, struct osdep_mmio *m);

/* intel_engine_pm_put(): the park switch to the kernel context. */
int parity_engine_verify_wa_park(struct parity_gt_verify_wa *v, unsigned i,
	struct parity_gt_engines *es, struct osdep_mmio *m);

/* The whole of __engines_verify_workarounds(); 0 or -EIO. */
int parity_engines_verify_workarounds(struct parity_gt_verify_wa *v,
	struct parity_gt_engines *es, struct parity_gt_init *gi,
	struct parity_gt_mem *gm, struct osdep_mmio *m, unsigned timeout_ms);

void parity_engines_verify_wa_release(struct parity_gt_verify_wa *v,
	struct parity_gt_mem *gm);

#endif /* PARITY_GT_VERIFY_WA_H */

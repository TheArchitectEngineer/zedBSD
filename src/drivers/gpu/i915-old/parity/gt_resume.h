/*
 * WS031 Linux-parity — P6-c4a: intel_engines_init() and intel_gt_resume().
 *
 * intel_gt_init() runs, in this order and under FORCEWAKE_ALL:
 *   intel_gt_init_workarounds, intel_gt_init_scratch, intel_gt_pm_init,
 *   kernel_vm, intel_set_mocs_index,
 *   intel_engines_init()      per engine: engine_setup_common (status page,
 *                             execlists state), intel_execlists_submission_
 *                             setup, engine_init_common (the pinned kernel
 *                             context: 4 KiB ring, timeline in the status
 *                             page at I915_GEM_HWS_SEQNO_ADDR)
 *   intel_uc_init             (enable_guc=0: nothing)
 *   intel_gt_resume()         see below
 *   __engines_record_defaults (P6-c4b)
 *
 * intel_gt_resume(), in the reference order:
 *   gt_sanitize(gt, force=true)
 *       per engine: reset.prepare (pause, stop_cs + Wa_22011802037, drain
 *       MI_FORCE_WAKEs) and sanitize (reset_csb_pointers, sanitize_hwsp,
 *       reset the pinned kernel context);
 *       reset_engines(gt) -- `reset_engines(gt) || force` evaluates the reset
 *       FIRST, so a full __intel_gt_reset(ALL_ENGINES) always happens here;
 *       rewind (nothing in flight), reset.finish, intel_rps_sanitize
 *   intel_rc6_sanitize        PG_ENABLE, RC_CONTROL, RC_STATE to 0
 *   intel_gt_init_hw          GT workarounds + verify, MOCS
 *   intel_rps_enable, intel_llc_enable (recorded, not faked)
 *   per engine: serial++, intel_engine_resume = engine workarounds,
 *       whitelist, execlists_resume (intel_mocs_init_engine -- the L3CC table
 *       AGAIN for render, breadcrumbs reset, enable_execlists)
 *   intel_rc6_enable
 *
 * The P6-b placement put the engine workarounds, rps and rc6 inside init_hw
 * and never sanitized rc6; this is the corrected order.
 */
#ifndef PARITY_GT_RESUME_H
#define PARITY_GT_RESUME_H

#include <stdint.h>
#include "gt_mmio.h"
#include "gt_engine.h"
#include "gt_lrc.h"
#include "gt_submit.h"

struct osdep_mmio;
struct spinlock;
struct parity_gt_mem;
struct parity_gt_ppgtt;
struct parity_gt_init;

struct parity_gt_engines {
	unsigned n;
	struct parity_gt_engine ge[PARITY_MAX_ENGINES];
	struct parity_execlists el[PARITY_MAX_ENGINES];
	struct parity_gt_context kernel_ce[PARITY_MAX_ENGINES];
	uint32_t kernel_tl_seqno[PARITY_MAX_ENGINES];   /* the pinned timeline */
	int inited;

	/* intel_gt_resume() diagnostics */
	int reset_rc;               /* reset_engines(gt) */
	unsigned stop_cs_timeouts;
	unsigned resumed;
	unsigned l3cc_writes_rcs;   /* intel_mocs_init_engine(RCS) */
};

int parity_intel_engines_init(struct parity_gt_engines *es,
	struct parity_gt_mmio *g, struct parity_gt_mem *gm,
	struct parity_gt_ppgtt *pp);
void parity_intel_engines_release(struct parity_gt_engines *es,
	struct parity_gt_mem *gm);

int parity_intel_gt_resume(struct parity_gt_engines *es,
	struct parity_gt_init *gi, const struct parity_gt_mmio *g,
	struct osdep_mmio *m, struct spinlock *uncore_lock);

#endif /* PARITY_GT_RESUME_H */

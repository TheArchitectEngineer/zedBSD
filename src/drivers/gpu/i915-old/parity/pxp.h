/*
 * WS031 Linux-parity — P7-0: intel_pxp_init().
 *
 * The reference (pxp/intel_pxp.c, 6.8.12), for ADL-P (has_pxp, no media GT,
 * CONFIG_DRM_I915_PXP + CONFIG_INTEL_MEI_PXP, a VDBOX present):
 *   find_gt_for_required_protected_content() -> the root GT, full feature
 *   i915->pxp = kzalloc; ctrl_gt = gt; tee_mutex
 *   pxp_init_full():
 *     init_completion(&termination) + complete_all
 *     kcr_base = GEN12_KCR_BASE
 *     intel_pxp_session_management_init()   arb_mutex + session_work
 *     create_vcs_context()                   intel_engine_create_pinned_context
 *                                            (first VCS, engine->gt->vm, SZ_4K,
 *                                            I915_GEM_HWS_PXP_ADDR, "pxp_context")
 *     intel_pxp_tee_component_init()         alloc_streaming_command (one page)
 *                                            + component_add(I915_COMPONENT_PXP)
 *   The KCR/interrupt hardware init (intel_pxp_init_hw) and the arbitration
 *   session start only when the mei_pxp component binds, which has no
 *   counterpart here: nothing is written to KCR at probe.
 *
 * ADAPTATIONS (recorded): the component framework is N/A (the component is
 * recorded as "added", never bound); the streaming command page is a driver
 * object without the CPU map/DMA pin pair.
 */
#ifndef PARITY_PXP_H
#define PARITY_PXP_H

#include <stdint.h>
#include "gt_mem.h"
#include "gt_lrc.h"

struct parity_gt_engines;
struct parity_gt_ppgtt;

#define PARITY_GEN12_KCR_BASE       0x32000u
#define PARITY_I915_GEM_HWS_PXP     (0x60u * 4u)

struct parity_pxp {
	int has_engine;
	unsigned engine_idx;          /* the first VCS */
	uint32_t kcr_base;
	struct parity_gt_context ce;  /* pxp->ce, pinned */
	uint32_t hwsp_ggtt;
	volatile uint32_t *hwsp_cpu;
	struct parity_gt_object *stream_cmd;   /* alloc_streaming_command() */
	int component_added;
	int full_feature;
	int inited;
	int err;
	const char *err_where;
};

/* 0, -ENODEV (no PXP GT), or the pinned-context / allocation error. */
int parity_intel_pxp_init(struct parity_pxp *x, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *gt_vm, struct parity_gt_mem *gm, int has_pxp);
void parity_intel_pxp_fini(struct parity_pxp *x, struct parity_gt_mem *gm);

#endif /* PARITY_PXP_H */

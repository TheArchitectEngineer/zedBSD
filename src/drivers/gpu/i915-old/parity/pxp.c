/*
 * WS031 Linux-parity — P7-0: intel_pxp_init().
 * See pxp.h for the reference sequence and the recorded adaptations.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include <string.h>
#include "gt_mmio.h"
#include "gt_resume.h"
#include "pxp.h"

static int
fail(struct parity_pxp *x, int rc, const char *where)
{
	if (x->err == 0) {
		x->err = rc;
		x->err_where = where;
	}
	return rc;
}

int
parity_intel_pxp_init(struct parity_pxp *x, struct parity_gt_engines *es,
	struct parity_gt_ppgtt *gt_vm, struct parity_gt_mem *gm, int has_pxp)
{
	unsigned i;
	int vdbox = 0;
	int rc;

	if (x == 0 || es == 0 || gt_vm == 0 || gm == 0)
		return -EINVAL;
	memset(x, 0, sizeof(*x));

	/*
	 * find_gt_for_required_protected_content(): has_pxp, no media GT, and a
	 * VDBOX on the root GT (the mei-pxp path of pre-MTL parts).
	 */
	for (i = 0u; i < es->n; i++) {
		if (es->ge[i].info->class == PARITY_VIDEO_DECODE_CLASS) {
			if (!vdbox)
				x->engine_idx = i;
			vdbox = 1;
		}
	}
	if (!has_pxp || !vdbox)
		return fail(x, -ENODEV, "find_gt_for_required_protected_content");
	x->full_feature = 1;
	x->has_engine = 1;

	/* pxp_init_full() */
	x->kcr_base = PARITY_GEN12_KCR_BASE;
	/* intel_pxp_session_management_init(): arb_mutex + session_work (idle). */

	/* create_vcs_context(): pinned, engine->gt->vm, SZ_4K, HWS_PXP. */
	rc = parity_lrc_alloc(&x->ce, &es->ge[x->engine_idx], gt_vm, gm, 4096u, 0u);
	if (rc != 0)
		return fail(x, rc, "create_vcs_context");
	parity_lrc_init_state(&x->ce);
	(void)parity_lrc_update_regs(&x->ce, x->ce.ring.tail);
	x->hwsp_ggtt = (uint32_t)es->ge[x->engine_idx].hwsp_ggtt + PARITY_I915_GEM_HWS_PXP;
	x->hwsp_cpu = &es->ge[x->engine_idx].hwsp[PARITY_I915_GEM_HWS_PXP / 4u];

	/* intel_pxp_tee_component_init(): alloc_streaming_command() then component_add. */
	x->stream_cmd = parity_gt_object_create(gm, 4096u);
	if (x->stream_cmd == 0) {
		(void)fail(x, -ENOMEM, "alloc_streaming_command");
		parity_lrc_release(&x->ce, gm);   /* out_context: destroy_vcs_context */
		return x->err;
	}
	x->component_added = 1;   /* component_add(): N/A framework, recorded */
	x->inited = 1;
	return 0;
}

void
parity_intel_pxp_fini(struct parity_pxp *x, struct parity_gt_mem *gm)
{
	if (x == 0 || gm == 0)
		return;
	/* intel_pxp_fini(): arb_is_valid = false; tee component fini; destroy_vcs_context */
	x->component_added = 0;
	if (x->stream_cmd != 0) {
		parity_gt_object_destroy(gm, x->stream_cmd);
		x->stream_cmd = 0;
	}
	if (x->ce.allocated)
		parity_lrc_release(&x->ce, gm);
	x->inited = 0;
}

/*
 * WS031 Linux-parity — P6-c6: intel_migrate_init().
 * See gt_migrate.h for the reference sequence and the recorded adaptations.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <errno.h>
#include <string.h>
#include "gt_mmio.h"
#include "gt_resume.h"
#include "gt_migrate.h"

static int
fail(struct parity_gt_migrate *m, int rc, const char *where)
{
	if (m->err == 0) {
		m->err = rc;
		m->err_where = where;
	}
	return rc;
}

/* insert_pte(): the PT page itself becomes readable/writable at d.offset. */
static void
insert_pte(struct parity_gt_ppgtt *pp, struct parity_gt_object *pt,
	uint64_t pt_dma, void *data)
{
	struct parity_gt_migrate *m = data;
	int rc;

	(void)pt;
	rc = parity_gt_ppgtt_insert_page(pp, pt_dma, m->pte_window +
		(uint64_t)m->exposed_pts * PARITY_GT_PAGE_BYTES,
		PARITY_PAT_INDEX_CACHE_NONE);
	if (rc != 0)
		(void)fail(m, rc, "insert_pte");
	m->exposed_pts++;
}

/* engine_supports_migration(): every copy engine (MI_ARB_ON_OFF, MI_STORE_DATA, blits). */
static int
first_copy_engine(struct parity_gt_engines *es, unsigned *idx)
{
	unsigned i;

	for (i = 0u; i < es->n; i++) {
		if (es->ge[i].info->class == PARITY_COPY_ENGINE_CLASS) {
			*idx = i;
			return 1;
		}
	}
	return 0;
}

int
parity_intel_migrate_init(struct parity_gt_migrate *m,
	struct parity_gt_engines *es, struct parity_gt_mem *gm)
{
	struct parity_gt_engine *ge;
	uint64_t base = 0u;   /* (u64)i << 32 for copy instance i: only bcs0 here */
	uint64_t sz;
	unsigned i;
	int rc;

	if (m == 0 || es == 0 || gm == 0)
		return -EINVAL;
	for (i = 0u; i < sizeof(*m); i++)
		((char *)m)[i] = 0;

	/* first_copy_engine() */
	if (!first_copy_engine(es, &m->engine_idx))
		return fail(m, -ENODEV, "first_copy_engine");
	m->has_engine = 1;
	ge = &es->ge[m->engine_idx];

	/* migrate_vm(): i915_ppgtt_create(gt, I915_BO_ALLOC_PM_EARLY) */
	rc = parity_gt_ppgtt_create(gm, &m->vm);
	if (rc != 0)
		return fail(m, rc, "i915_ppgtt_create");

	/* "We copy in 8MiB chunks ... 4x2 page directories for source/destination." */
	sz = 2u * (uint64_t)PARITY_MIGRATE_CHUNK_SZ;
	m->window_bytes = sz;
	m->pte_window = base + sz;
	/* "another page directory setup so that we can write the 8x512 PTE" */
	sz += (sz >> 12) * sizeof(uint64_t);

	/* i915_vm_alloc_pt_stash + allocate_va_range(base, sz) */
	rc = parity_gt_ppgtt_alloc_range(gm, &m->vm, base, sz);
	if (rc != 0)
		goto err_vm;

	/* "Now allow the GPU to rewrite the PTE via its own ppGTT" */
	rc = parity_gt_ppgtt_foreach_pt(&m->vm, base, m->pte_window - base,
		insert_pte, m);
	if (rc == 0 && m->err != 0)
		rc = m->err;
	if (rc != 0)
		goto err_vm;

	/*
	 * intel_engine_create_pinned_context(engine, vm, SZ_512K,
	 * I915_GEM_HWS_MIGRATE, "migrate"): intel_context_create() then
	 * intel_context_pin() -> lrc_alloc (state + ring, the timeline on the
	 * engine's status page), lrc_init_state, lrc_update_regs.
	 */
	rc = parity_lrc_alloc(&m->ce, ge, &m->vm, gm, PARITY_MIGRATE_RING_BYTES, 0u);
	if (rc != 0) {
		(void)fail(m, rc, "intel_engine_create_pinned_context");
		goto err_vm;
	}
	parity_lrc_init_state(&m->ce);
	(void)parity_lrc_update_regs(&m->ce, m->ce.ring.tail);

	/* intel_timeline_create_from_engine(engine, I915_GEM_HWS_MIGRATE) */
	m->hwsp_ggtt = (uint32_t)ge->hwsp_ggtt + PARITY_I915_GEM_HWS_MIGRATE;
	m->hwsp_cpu = &ge->hwsp[PARITY_I915_GEM_HWS_MIGRATE / 4u];
	m->tl_seqno = 0u;   /* no initial breadcrumb on an engine-HWSP timeline */

	m->inited = 1;
	return 0;

err_vm:
	(void)fail(m, rc, m->err_where != 0 ? m->err_where : "migrate_vm");
	parity_gt_ppgtt_destroy(gm, &m->vm);
	return rc;
}

void
parity_intel_migrate_fini(struct parity_gt_migrate *m, struct parity_gt_mem *gm)
{
	if (m == 0 || gm == 0)
		return;
	/* intel_engine_destroy_pinned_context(): unpin + put, then the vm. */
	if (m->ce.allocated)
		parity_lrc_release(&m->ce, gm);
	parity_gt_ppgtt_destroy(gm, &m->vm);
	m->inited = 0;
	m->has_engine = 0;
}

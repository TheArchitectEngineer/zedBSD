/*
 * WS031 Linux-parity — P6-c0: GT memory objects, GGTT binding and the kernel ppgtt.
 *
 * See gt_mem.h for what each piece stands for in the reference and for the two
 * recorded adaptations (fixed GGTT window, fixed object pool).
 *
 * One reference behaviour that is easy to get backwards and is asserted by the
 * tests: on gen11+ the GGTT page-table window is mapped UNCACHED, so
 * gen8_ggtt_invalidate() writes NOTHING -- needs_wc_ggtt_mapping() is false for
 * GRAPHICS_VER >= 11, and the flush register (GFX_FLSH_CNTL_GEN6) belongs to the
 * gen6 path only.  Writing it here would be a deviation, not a safety net.
 */
#include "../internal.h"
#include <kern/klog.h>
#include <kern/device-io.h>
#include <drivers/dma.h>
#include <errno.h>
#include <string.h>
#include "gt_mem.h"
#include "pte.h"
#include "osdep/mmio.h"

/* --- encoders ------------------------------------------------------------ */

/*
 * gen12_pte_encode(addr, pat_index, flags).  ADL-P never passes PTE_READ_ONLY
 * (vm->has_read_only is false on gen11/12) and never PTE_LM (system memory), so
 * only the PRESENT/RW bits and the PAT index bits appear.
 */
uint64_t
parity_gen12_ppgtt_pte_encode(uint64_t dma, unsigned pat_index)
{
	uint64_t pte = dma | PARITY_GEN8_PAGE_PRESENT_B | PARITY_GEN8_PAGE_RW_B;

	if ((pat_index & 1u) != 0u)
		pte |= PARITY_GEN12_PTE_PAT0;
	if ((pat_index & 2u) != 0u)
		pte |= PARITY_GEN12_PTE_PAT1;
	if ((pat_index & 4u) != 0u)
		pte |= PARITY_GEN12_PTE_PAT2;
	/* MTL_PPGTT_PTE_PAT3 (bit 62) is Meteor Lake only; ADL-P has no index >= 8. */
	return pte;
}

/* gen8_pde_encode(addr, I915_CACHE_NONE): the directory levels are all uncached. */
uint64_t
parity_gen8_pde_encode(uint64_t dma)
{
	return dma | PARITY_GEN8_PAGE_PRESENT_B | PARITY_GEN8_PAGE_RW_B |
		PARITY_PPAT_UNCACHED;
}

/* --- the pool and the GGTT window ---------------------------------------- */

int
parity_gt_mem_init(struct parity_gt_mem *gm, struct drv_dma_device *dma,
	uint64_t dma_mask, void *table, unsigned entries, uint64_t scratch_pte,
	struct osdep_mmio *m)
{
	unsigned i;

	if (gm == 0 || dma == 0 || table == 0)
		return -EINVAL;
	for (i = 0u; i < sizeof(*gm); i++)
		((char *)gm)[i] = 0;

	/* The window must fit, and must not run into the bottom of the table. */
	if (entries <= PARITY_GT_GGTT_PAGES)
		return -ENOSPC;

	gm->dma = dma;
	gm->dma_mask = dma_mask;
	gm->table = (volatile uint8_t *)table;
	gm->entries = entries;
	gm->scratch_pte = scratch_pte;
	gm->m = m;

	/* PIN_HIGH: take the window at the TOP of the GGTT. */
	gm->window_first = entries - PARITY_GT_GGTT_PAGES;
	gm->window_pages = PARITY_GT_GGTT_PAGES;
	gm->inited = 1;
	return 0;
}

void
parity_gt_mem_fini(struct parity_gt_mem *gm)
{
	unsigned i;

	if (gm == 0 || !gm->inited)
		return;
	/* Release in reverse: unbind first so no live PTE names a freed page. */
	for (i = PARITY_GT_MAX_OBJECTS; i-- > 0u; ) {
		if (gm->objects[i].in_use)
			parity_gt_object_destroy(gm, &gm->objects[i]);
	}
	gm->inited = 0;
}

/* --- objects -------------------------------------------------------------- */

struct parity_gt_object *
parity_gt_object_create(struct parity_gt_mem *gm, uint32_t bytes)
{
	struct parity_gt_object *o = 0;
	unsigned i;
	int rc;

	if (gm == 0 || !gm->inited || bytes == 0u)
		return 0;

	/* Round up to whole pages: a GGTT binding is page granular. */
	bytes = (uint32_t)((bytes + PARITY_GT_PAGE_BYTES - 1u) &
		~(PARITY_GT_PAGE_BYTES - 1u));

	for (i = 0u; i < PARITY_GT_MAX_OBJECTS; i++) {
		if (!gm->objects[i].in_use) {
			o = &gm->objects[i];
			break;
		}
	}
	if (o == 0) {
		gm->obj_alloc_fail++;
		kern_logf("i915: parity gt_mem: object pool exhausted (%u slots)\n",
			PARITY_GT_MAX_OBJECTS);
		return 0;
	}

	rc = drv_dma_vector_create(gm->dma, (size_t)bytes, &o->vec);
	if (rc != 0) {
		gm->obj_alloc_fail++;
		kern_logf("i915: parity gt_mem: backing pages for %u bytes failed rc=%d\n",
			bytes, rc);
		o->vec = 0;
		return 0;
	}
	o->cpu = drv_dma_vector_address(o->vec);
	if (o->cpu == 0) {
		(void)drv_dma_vector_free(o->vec);
		o->vec = 0;
		gm->obj_alloc_fail++;
		return 0;
	}

	/*
	 * An internal object starts zeroed: a context image, a status page and a
	 * page-table page are all read by the GPU before anything writes them.
	 */
	memset(o->cpu, 0, (size_t)bytes);

	o->bytes = bytes;
	o->pages = bytes / PARITY_GT_PAGE_BYTES;
	o->bound = 0;
	o->ggtt_page = 0u;
	o->ggtt_offset = 0u;
	o->in_use = 1;
	gm->objects_live++;
	return o;
}

void
parity_gt_object_destroy(struct parity_gt_mem *gm, struct parity_gt_object *o)
{
	if (gm == 0 || o == 0 || !o->in_use)
		return;
	if (o->bound)
		parity_gt_ggtt_unbind(gm, o);
	if (o->vec != 0)
		(void)drv_dma_vector_free(o->vec);
	o->vec = 0;
	o->cpu = 0;
	o->bytes = 0u;
	o->pages = 0u;
	o->in_use = 0;
	if (gm->objects_live != 0u)
		gm->objects_live--;
}

int
parity_gt_object_page_dma(const struct parity_gt_object *o, unsigned page,
	uint64_t *dma_out)
{
	uint64_t want;
	uint64_t seen = 0;
	unsigned segs, i;

	if (o == 0 || !o->in_use || dma_out == 0 || page >= o->pages)
		return -EINVAL;

	want = (uint64_t)page * PARITY_GT_PAGE_BYTES;
	segs = drv_dma_vector_count(o->vec);
	for (i = 0u; i < segs; i++) {
		struct drv_dma_segment seg;

		if (drv_dma_vector_segment(o->vec, i, &seg) != 0)
			return -EIO;
		if (want < seen + (uint64_t)seg.length) {
			/*
			 * A segment must stay page aligned for the offset within
			 * it to name a page; refuse rather than produce a PTE that
			 * points into the middle of a page.
			 */
			uint64_t off = want - seen;

			if ((seg.address & (PARITY_GT_PAGE_BYTES - 1u)) != 0u)
				return -EIO;
			*dma_out = seg.address + off;
			return 0;
		}
		seen += (uint64_t)seg.length;
	}
	return -EIO;
}

/* --- GGTT window allocation ----------------------------------------------- */

static int
window_bit(const struct parity_gt_mem *gm, unsigned page)
{
	return (int)((gm->bitmap[page / 32u] >> (page % 32u)) & 1u);
}

static void
window_set(struct parity_gt_mem *gm, unsigned page, int used)
{
	if (used)
		gm->bitmap[page / 32u] |= (uint32_t)1u << (page % 32u);
	else
		gm->bitmap[page / 32u] &= ~((uint32_t)1u << (page % 32u));
}

/* First fit over the driver window; returns the window-relative first page. */
static int
window_alloc(struct parity_gt_mem *gm, unsigned pages, unsigned *first_out)
{
	unsigned start = 0u;
	unsigned run = 0u;
	unsigned i;

	if (pages == 0u || pages > gm->window_pages)
		return -EINVAL;
	for (i = 0u; i < gm->window_pages; i++) {
		if (window_bit(gm, i)) {
			run = 0u;
			start = i + 1u;
			continue;
		}
		run++;
		if (run == pages)
			break;
	}
	if (run != pages) {
		gm->ggtt_alloc_fail++;
		return -ENOSPC;
	}
	for (i = start; i < start + pages; i++)
		window_set(gm, i, 1);
	gm->allocated_pages += pages;
	*first_out = start;
	return 0;
}

static void
ggtt_write_pte(struct parity_gt_mem *gm, unsigned index, uint64_t pte)
{
	kern_mmio_write64((volatile void *)(gm->table + (size_t)index * 8u), pte);
	gm->pte_writes++;
}

void
parity_gt_ggtt_flush(struct parity_gt_mem *gm)
{
	if (gm == 0)
		return;
	/*
	 * gen8_ggtt_invalidate(): on gen11+ needs_wc_ggtt_mapping() is false, so
	 * the reference writes NO register here -- the table window is uncached
	 * and the writes have already landed.  Order them and record the call.
	 */
	kern_io_write_barrier();
	gm->flushes++;
}

int
parity_gt_ggtt_bind(struct parity_gt_mem *gm, struct parity_gt_object *o)
{
	unsigned first = 0u;
	unsigned p;
	int rc;

	if (gm == 0 || !gm->inited || o == 0 || !o->in_use)
		return -EINVAL;
	if (o->bound)
		return -EBUSY;

	rc = window_alloc(gm, o->pages, &first);
	if (rc != 0)
		return rc;

	for (p = 0u; p < o->pages; p++) {
		uint64_t dma = 0;
		uint64_t pte = 0;

		rc = parity_gt_object_page_dma(o, p, &dma);
		if (rc != 0)
			goto unwind;
		/* Range-check and encode; refuse rather than mask high bits off. */
		if (!parity_ggtt_pte_encode(osdep_dma_addr(dma),
				(uint64_t)PARITY_GT_PAGE_BYTES, gm->dma_mask, &pte)) {
			rc = -ERANGE;
			goto unwind;
		}
		ggtt_write_pte(gm, gm->window_first + first + p, pte);
	}

	parity_gt_ggtt_flush(gm);
	o->ggtt_page = gm->window_first + first;
	o->ggtt_offset = (uint64_t)o->ggtt_page * PARITY_GT_PAGE_BYTES;
	o->bound = 1;
	return 0;

unwind:
	/* Point every entry we touched back at scratch and give the run back. */
	for (p = 0u; p < o->pages; p++)
		ggtt_write_pte(gm, gm->window_first + first + p, gm->scratch_pte);
	parity_gt_ggtt_flush(gm);
	for (p = 0u; p < o->pages; p++)
		window_set(gm, first + p, 0);
	gm->allocated_pages -= o->pages;
	return rc;
}

void
parity_gt_ggtt_unbind(struct parity_gt_mem *gm, struct parity_gt_object *o)
{
	unsigned first;
	unsigned p;

	if (gm == 0 || o == 0 || !o->bound)
		return;
	first = o->ggtt_page - gm->window_first;
	for (p = 0u; p < o->pages; p++)
		ggtt_write_pte(gm, o->ggtt_page + p, gm->scratch_pte);
	parity_gt_ggtt_flush(gm);
	for (p = 0u; p < o->pages; p++)
		window_set(gm, first + p, 0);
	gm->allocated_pages -= o->pages;
	o->bound = 0;
	o->ggtt_page = 0u;
	o->ggtt_offset = 0u;
}

/* --- intel_gt_init_scratch ------------------------------------------------ */

int
parity_gt_init_scratch(struct parity_gt_mem *gm, struct parity_gt_object **out)
{
	struct parity_gt_object *o;
	int rc;

	if (gm == 0 || out == 0)
		return -EINVAL;
	*out = 0;

	/* GRAPHICS_VER != 2, so the reference asks for SZ_4K. */
	o = parity_gt_object_create(gm, PARITY_GT_PAGE_BYTES);
	if (o == 0)
		return -ENOMEM;

	/* i915_ggtt_pin(vma, NULL, 0, PIN_HIGH). */
	rc = parity_gt_ggtt_bind(gm, o);
	if (rc != 0) {
		parity_gt_object_destroy(gm, o);
		return rc;
	}
	*out = o;
	return 0;
}

/* --- the kernel ppgtt ----------------------------------------------------- */

/* fill_px(): every entry of one page holds the same encoded address. */
static void
fill_px(struct parity_gt_object *o, uint64_t value, unsigned count)
{
	uint64_t *p = (uint64_t *)o->cpu;
	unsigned i;

	for (i = 0u; i < count; i++)
		p[i] = value;
}

int
parity_gt_ppgtt_create(struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp)
{
	uint64_t dma = 0;
	unsigned i;
	int rc;

	if (gm == 0 || pp == 0)
		return -EINVAL;
	for (i = 0u; i < sizeof(*pp); i++)
		((char *)pp)[i] = 0;
	pp->top = PARITY_PPGTT_TOP;
	pp->top_count = PARITY_PPGTT_TOP_COUNT;

	/*
	 * gen8_init_scratch().  The clone branch is not taken: gt->vm does not
	 * exist yet when the kernel vm is created, and has_read_only is false on
	 * gen12 anyway.
	 */
	pp->scratch[0] = parity_gt_object_create(gm, PARITY_GT_PAGE_BYTES);
	if (pp->scratch[0] == 0) {
		rc = -ENOMEM;
		goto fail;
	}
	rc = parity_gt_object_page_dma(pp->scratch[0], 0u, &dma);
	if (rc != 0)
		goto fail;
	if (!parity_dma_in_range(dma, (uint64_t)PARITY_GT_PAGE_BYTES, gm->dma_mask)) {
		rc = -ERANGE;
		goto fail;
	}
	/* pat_index = cachelevel_to_pat[I915_CACHE_NONE] = 3 on the TGL table. */
	pp->scratch_encode[0] = parity_gen12_ppgtt_pte_encode(dma,
		PARITY_PAT_INDEX_CACHE_NONE);

	for (i = 1u; i <= (unsigned)PARITY_PPGTT_TOP; i++) {
		pp->scratch[i] = parity_gt_object_create(gm, PARITY_GT_PAGE_BYTES);
		if (pp->scratch[i] == 0) {
			rc = -ENOMEM;
			goto fail;
		}
		/* Each level is a full page of the level below's encoded address. */
		fill_px(pp->scratch[i], pp->scratch_encode[i - 1u],
			PARITY_GT_PTES_PER_PAGE);
		rc = parity_gt_object_page_dma(pp->scratch[i], 0u, &dma);
		if (rc != 0)
			goto fail;
		if (!parity_dma_in_range(dma, (uint64_t)PARITY_GT_PAGE_BYTES, gm->dma_mask)) {
			rc = -ERANGE;
			goto fail;
		}
		pp->scratch_encode[i] = parity_gen8_pde_encode(dma);
	}

	/* gen8_alloc_top_pd(): one page, its first top_count entries scratch[top]. */
	pp->top_pd = parity_gt_object_create(gm, PARITY_GT_PAGE_BYTES);
	if (pp->top_pd == 0) {
		rc = -ENOMEM;
		goto fail;
	}
	fill_px(pp->top_pd, pp->scratch_encode[PARITY_PPGTT_TOP], pp->top_count);
	rc = parity_gt_object_page_dma(pp->top_pd, 0u, &pp->top_pd_dma);
	if (rc != 0)
		goto fail;
	if (!parity_dma_in_range(pp->top_pd_dma, (uint64_t)PARITY_GT_PAGE_BYTES,
			gm->dma_mask)) {
		rc = -ERANGE;
		goto fail;
	}

	/*
	 * The page-table pages are reached by the GPU through the context's PDP0
	 * pair, by DMA address; they are NOT bound into the GGTT.
	 */
	pp->inited = 1;
	return 0;

fail:
	parity_gt_ppgtt_destroy(gm, pp);
	return rc;
}

void
parity_gt_ppgtt_destroy(struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp)
{
	unsigned i;

	if (gm == 0 || pp == 0)
		return;
	if (pp->top_pd != 0) {
		parity_gt_object_destroy(gm, pp->top_pd);
		pp->top_pd = 0;
	}
	for (i = (unsigned)PARITY_PPGTT_TOP + 1u; i-- > 0u; ) {
		if (pp->scratch[i] != 0) {
			parity_gt_object_destroy(gm, pp->scratch[i]);
			pp->scratch[i] = 0;
		}
	}
	pp->top_pd_dma = 0;
	pp->inited = 0;
}

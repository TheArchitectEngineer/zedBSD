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

uint64_t
parity_gen8_pde_encode_cached(uint64_t dma)
{
	/* PPAT_CACHED_PDE is 0: WB LLC. */
	return dma | PARITY_GEN8_PAGE_PRESENT_B | PARITY_GEN8_PAGE_RW_B;
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
		if (!gm->objects[i].in_use)
			continue;
		if (gm->objects[i].keep) {
			/* the display could not be shown to have stopped reading it: leaking is the safe side */
			gm->kept_objects++;
			kern_logf("i915: parity gt_mem: object at GGTT 0x%llx (%u pages) NOT released: still owned by the display\n",
				(unsigned long long)gm->objects[i].ggtt_offset, gm->objects[i].pages);
			continue;
		}
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

	o->contiguous = 0;
	o->vec = 0;
	if (bytes > DRV_DMA_VECTOR_MAX_SIZE) {
		/* Above the vector cap: one coherent, page-aligned allocation. */
		rc = drv_dma_alloc_coherent(gm->dma, (size_t)bytes,
			PARITY_GT_PAGE_BYTES, &o->big);
		if (rc != 0 || o->big.address == 0 ||
		    (o->big.device_address & (PARITY_GT_PAGE_BYTES - 1u)) != 0u) {
			if (rc == 0)
				drv_dma_free_coherent(gm->dma, &o->big);
			gm->obj_alloc_fail++;
			kern_logf("i915: parity gt_mem: coherent backing for %u bytes failed rc=%d\n",
				bytes, rc);
			return 0;
		}
		o->contiguous = 1;
		o->cpu = o->big.address;
	} else {
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
	if (o->keep) {                  /* kept for the display: reachable only below the scanout wrapper -- refuse there too */
		gm->keep_refusals++;
		return;
	}
	if (o->bound && o->display)
		parity_gt_display_unbind(gm, o);
	else if (o->bound)
		parity_gt_ggtt_unbind(gm, o);
	if (o->vec != 0)
		(void)drv_dma_vector_free(o->vec);
	if (o->contiguous)
		drv_dma_free_coherent(gm->dma, &o->big);
	o->contiguous = 0;
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
	if (o->contiguous) {
		*dma_out = o->big.device_address + want;
		return 0;
	}
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
	if (o->display) {
		parity_gt_display_unbind(gm, o);
		return;
	}
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

/* --- display window -------------------------------------------------------- */

uint64_t
parity_gt_ggtt_read_pte(const struct parity_gt_mem *gm, unsigned index)
{
	if (gm == 0 || index >= gm->entries)
		return 0;
	/* the same access width the writes use: the PTE table is a 64-bit register window */
	return kern_mmio_read64((volatile void *)(gm->table + (size_t)index * 8u));
}

int
parity_gt_display_window_init(struct parity_gt_mem *gm, unsigned pages)
{
	unsigned i;

	if (gm == 0 || !gm->inited || pages == 0u || pages > PARITY_GT_DISPLAY_PAGES ||
	    (pages % 32u) != 0u)
		return -EINVAL;
	if (gm->display_pages != 0u)
		return -EBUSY;
	/* below the GT window, and never reaching the bottom of the table */
	if (gm->window_first <= pages)
		return -ENOSPC;
	gm->display_first = gm->window_first - pages;
	gm->display_pages = pages;
	for (i = 0u; i < PARITY_GT_DISPLAY_WORDS; i++)
		gm->display_bitmap[i] = 0u;
	gm->display_allocated_pages = 0u;
	return 0;
}

static int
display_bit(const struct parity_gt_mem *gm, unsigned page)
{
	return (int)((gm->display_bitmap[page / 32u] >> (page % 32u)) & 1u);
}

static void
display_set(struct parity_gt_mem *gm, unsigned first, unsigned pages, int used)
{
	unsigned i;

	for (i = first; i < first + pages; i++) {
		if (used)
			gm->display_bitmap[i / 32u] |= (uint32_t)1u << (i % 32u);
		else
			gm->display_bitmap[i / 32u] &= ~((uint32_t)1u << (i % 32u));
	}
}

int
parity_gt_display_bind(struct parity_gt_mem *gm, struct parity_gt_object *o,
	unsigned align_pages, unsigned guard_pages)
{
	unsigned start = 0u, p, span;
	int rc;

	if (gm == 0 || !gm->inited || o == 0 || !o->in_use || gm->display_pages == 0u ||
	    align_pages == 0u || (align_pages & (align_pages - 1u)) != 0u)
		return -EINVAL;
	if (o->bound)
		return -EBUSY;
	span = guard_pages + o->pages + guard_pages;
	if (span > gm->display_pages)
		return -ENOSPC;

	/*
	 * First fit for [guard | object | guard] with the OBJECT's absolute GGTT page
	 * aligned -- the alignment is a property of the address the plane is given,
	 * not of the window-relative index.
	 */
	{
		unsigned obj_abs = (gm->display_first + guard_pages + align_pages - 1u) & ~(align_pages - 1u);
		int found = 0;

		for (; obj_abs + o->pages + guard_pages <= gm->display_first + gm->display_pages;
		     obj_abs += align_pages) {
			unsigned i, clash = 0u;

			start = obj_abs - guard_pages - gm->display_first;
			for (i = start; i < start + span; i++) {
				if (display_bit(gm, i)) {
					clash = 1u;
					break;
				}
			}
			if (!clash) {
				found = 1;
				break;
			}
		}
		if (!found) {
			gm->ggtt_alloc_fail++;
			return -ENOSPC;
		}
	}

	/* encode every page BEFORE touching a PTE: a buffer that cannot be mapped changes nothing */
	for (p = 0u; p < o->pages; p++) {
		uint64_t dma = 0, pte = 0;

		rc = parity_gt_object_page_dma(o, p, &dma);
		if (rc != 0)
			return rc;
		if (!parity_ggtt_pte_encode(osdep_dma_addr(dma), (uint64_t)PARITY_GT_PAGE_BYTES,
				gm->dma_mask, &pte))
			return -ERANGE;
	}

	display_set(gm, start, span, 1);
	gm->display_allocated_pages += span;
	for (p = 0u; p < guard_pages; p++) {
		ggtt_write_pte(gm, gm->display_first + start + p, gm->scratch_pte);
		ggtt_write_pte(gm, gm->display_first + start + guard_pages + o->pages + p, gm->scratch_pte);
		gm->display_pte_writes += 2u;
	}
	for (p = 0u; p < o->pages; p++) {
		uint64_t dma = 0, pte = 0;

		(void)parity_gt_object_page_dma(o, p, &dma);
		(void)parity_ggtt_pte_encode(osdep_dma_addr(dma), (uint64_t)PARITY_GT_PAGE_BYTES,
			gm->dma_mask, &pte);
		ggtt_write_pte(gm, gm->display_first + start + guard_pages + p, pte);
		gm->display_pte_writes++;
	}
	parity_gt_ggtt_flush(gm);

	o->ggtt_page = gm->display_first + start + guard_pages;
	o->ggtt_offset = (uint64_t)o->ggtt_page * PARITY_GT_PAGE_BYTES;
	o->display = 1;
	o->display_guard = guard_pages;
	o->bound = 1;
	return 0;
}

void
parity_gt_display_unbind(struct parity_gt_mem *gm, struct parity_gt_object *o)
{
	unsigned first, span, p;

	if (gm == 0 || o == 0 || !o->bound || !o->display)
		return;
	if (o->keep) {
		gm->keep_refusals++;
		return;
	}
	first = o->ggtt_page - o->display_guard - gm->display_first;
	span = o->display_guard + o->pages + o->display_guard;
	for (p = 0u; p < o->pages; p++) {
		ggtt_write_pte(gm, o->ggtt_page + p, gm->scratch_pte);
		gm->display_pte_writes++;
	}
	parity_gt_ggtt_flush(gm);
	display_set(gm, first, span, 0);
	gm->display_allocated_pages -= span;
	o->bound = 0;
	o->display = 0;
	o->display_guard = 0u;
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
void
parity_gt_clflush(const volatile void *address, size_t bytes)
{
#if defined(__x86_64__) || defined(__i386__)
	const volatile char *p = (const volatile char *)((uintptr_t)address & ~(uintptr_t)63u);
	const volatile char *end = (const volatile char *)address + bytes;

	__asm__ volatile("mfence" : : : "memory");
	for (; p < end; p += 64)
		__asm__ volatile("clflush (%0)" : : "r"(p) : "memory");
	__asm__ volatile("mfence" : : : "memory");
#else
	(void)address; (void)bytes;
#endif
}

/* fill_page_dma(): memset64 + drm_clflush_virt_range(vaddr, PAGE_SIZE). */
static void
fill_px(struct parity_gt_object *o, uint64_t value, unsigned count)
{
	uint64_t *p = (uint64_t *)o->cpu;
	unsigned i;

	for (i = 0u; i < count; i++)
		p[i] = value;
	parity_gt_clflush(o->cpu, PARITY_GT_PAGE_BYTES);
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
	for (i = pp->n_tables; i-- > 0u; ) {
		if (pp->tables[i].obj != 0)
			parity_gt_object_destroy(gm, pp->tables[i].obj);
		pp->tables[i].obj = 0;
	}
	pp->n_tables = 0u;
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

/* ---------------- allocate_va_range / foreach / insert_page ---------------- */

/*
 * gen8_ppgtt.c index helpers, on PTE indices (address >> 12): each level of
 * the 4-level tree indexes with 9 bits, level 0 being the PT.
 */
static unsigned
pd_range(uint64_t start, uint64_t end, int lvl, unsigned *idx)
{
	const unsigned shift = (unsigned)lvl * 9u;
	const uint64_t mask = ~(uint64_t)0 << ((unsigned)(lvl + 1) * 9u);

	end += (~mask) >> 9;
	*idx = (unsigned)((start >> shift) & 511u);
	if (((start ^ end) & mask) != 0u)
		return 512u - *idx;
	return (unsigned)((end >> shift) & 511u) - *idx;
}

static unsigned
pt_count(uint64_t start, uint64_t end)
{
	if (((start ^ end) >> 9) != 0u)
		return 512u - (unsigned)(start & 511u);
	return (unsigned)(end - start);
}

static struct parity_gt_ppgtt_table *
child_of(struct parity_gt_ppgtt *pp, const struct parity_gt_object *parent, unsigned idx)
{
	unsigned i;

	for (i = 0u; i < pp->n_tables; i++)
		if (pp->tables[i].parent == parent && pp->tables[i].idx == idx)
			return &pp->tables[i];
	return 0;
}

/* __gen8_ppgtt_alloc(): `lvl` is the level of `pd`'s entries' targets + 1. */
static int
alloc_level(struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp,
	struct parity_gt_object *pd, uint64_t *start, uint64_t end, int lvl)
{
	unsigned idx, len;
	int rc;

	len = pd_range(*start, end, lvl--, &idx);
	do {
		struct parity_gt_ppgtt_table *t = child_of(pp, pd, idx);

		if (t == 0) {
			uint64_t dma = 0;

			/* "allocating new tree": a page of the level below's scratch. */
			if (pp->n_tables == PARITY_PPGTT_MAX_TABLES)
				return -ENOSPC;
			t = &pp->tables[pp->n_tables];
			t->obj = parity_gt_object_create(gm, PARITY_GT_PAGE_BYTES);
			if (t->obj == 0)
				return -ENOMEM;
			fill_px(t->obj, pp->scratch_encode[lvl], PARITY_GT_PTES_PER_PAGE);
			rc = parity_gt_object_page_dma(t->obj, 0u, &dma);
			if (rc == 0 && !parity_dma_in_range(dma, (uint64_t)PARITY_GT_PAGE_BYTES,
					gm->dma_mask))
				rc = -ERANGE;
			if (rc != 0) {
				parity_gt_object_destroy(gm, t->obj);
				t->obj = 0;
				return rc;
			}
			t->parent = pd;
			t->idx = idx;
			t->lvl = lvl;
			t->dma = dma;
			pp->n_tables++;
			/* set_pd_entry() -> write_dma_entry(): the entry, then clflush it. */
			((uint64_t *)pd->cpu)[idx] = parity_gen8_pde_encode_cached(dma);
			parity_gt_clflush(&((uint64_t *)pd->cpu)[idx], sizeof(uint64_t));
		}
		if (lvl != 0) {
			rc = alloc_level(gm, pp, t->obj, start, end, lvl);
			if (rc != 0)
				return rc;
		} else {
			*start += pt_count(*start, end);
		}
	} while (idx++, --len);
	return 0;
}

int
parity_gt_ppgtt_alloc_range(struct parity_gt_mem *gm, struct parity_gt_ppgtt *pp,
	uint64_t start, uint64_t length)
{
	if (gm == 0 || pp == 0 || !pp->inited || length == 0u ||
	    ((start | length) & (PARITY_GT_PAGE_BYTES - 1u)) != 0u ||
	    start + length < start || (start + length) >> 48 != 0u)
		return -EINVAL;
	start >>= 12;
	length >>= 12;
	return alloc_level(gm, pp, pp->top_pd, &start, start + length, pp->top);
}

/* __gen8_ppgtt_foreach() */
static int
foreach_level(struct parity_gt_ppgtt *pp, struct parity_gt_object *pd,
	uint64_t *start, uint64_t end, int lvl, parity_gt_ppgtt_pt_fn fn, void *data)
{
	unsigned idx, len;
	int rc;

	len = pd_range(*start, end, lvl--, &idx);
	do {
		struct parity_gt_ppgtt_table *t = child_of(pp, pd, idx);

		if (t == 0)
			return -ENOENT;   /* the reference walks only allocated ranges */
		if (lvl != 0) {
			rc = foreach_level(pp, t->obj, start, end, lvl, fn, data);
			if (rc != 0)
				return rc;
		} else {
			fn(pp, t->obj, t->dma, data);
			*start += pt_count(*start, end);
		}
	} while (idx++, --len);
	return 0;
}

int
parity_gt_ppgtt_foreach_pt(struct parity_gt_ppgtt *pp, uint64_t start,
	uint64_t length, parity_gt_ppgtt_pt_fn fn, void *data)
{
	if (pp == 0 || !pp->inited || fn == 0 || length == 0u ||
	    ((start | length) & (PARITY_GT_PAGE_BYTES - 1u)) != 0u)
		return -EINVAL;
	start >>= 12;
	length >>= 12;
	return foreach_level(pp, pp->top_pd, &start, start + length, pp->top, fn, data);
}

int
parity_gt_ppgtt_insert_page(struct parity_gt_ppgtt *pp, uint64_t dma,
	uint64_t offset, unsigned pat_index)
{
	struct parity_gt_object *table;
	struct parity_gt_ppgtt_table *t;
	uint64_t idx;
	int lvl;

	if (pp == 0 || !pp->inited || (offset & (PARITY_GT_PAGE_BYTES - 1u)) != 0u)
		return -EINVAL;
	idx = offset >> 12;
	table = pp->top_pd;
	for (lvl = pp->top; lvl > 0; lvl--) {
		t = child_of(pp, table, (unsigned)((idx >> ((unsigned)lvl * 9u)) & 511u));
		if (t == 0)
			return -ENOENT;
		table = t->obj;
	}
	((uint64_t *)table->cpu)[idx & 511u] =
		parity_gen12_ppgtt_pte_encode(dma, pat_index);
	/* gen8_ppgtt_insert_entry(): drm_clflush_virt_range(&vaddr[idx], 8). */
	parity_gt_clflush(&((uint64_t *)table->cpu)[idx & 511u], sizeof(uint64_t));
	return 0;
}

int
parity_gt_ppgtt_insert_scratch(struct parity_gt_ppgtt *pp, uint64_t offset)
{
	struct parity_gt_object *table;
	struct parity_gt_ppgtt_table *t;
	uint64_t idx;
	int lvl;

	if (pp == 0 || !pp->inited || (offset & (PARITY_GT_PAGE_BYTES - 1u)) != 0u)
		return -EINVAL;
	idx = offset >> 12;
	table = pp->top_pd;
	for (lvl = pp->top; lvl > 0; lvl--) {
		t = child_of(pp, table, (unsigned)((idx >> ((unsigned)lvl * 9u)) & 511u));
		if (t == 0)
			return -ENOENT;
		table = t->obj;
	}
	((uint64_t *)table->cpu)[idx & 511u] = pp->scratch_encode[0];
	parity_gt_clflush(&((uint64_t *)table->cpu)[idx & 511u], sizeof(uint64_t));
	return 0;
}

#define PTE_ADDR_MASK   0x0000fffffffff000ull

static struct parity_gt_ppgtt_table *
table_by_dma(struct parity_gt_ppgtt *pp, uint64_t dma)
{
	unsigned i;

	for (i = 0u; i < pp->n_tables; i++)
		if (pp->tables[i].dma == dma)
			return &pp->tables[i];
	return 0;
}

int
parity_gt_ppgtt_walk(struct parity_gt_ppgtt *pp, uint64_t va,
	struct parity_gt_ppgtt_walk *w)
{
	struct parity_gt_object *table;
	uint64_t idx;
	int lvl, level;

	if (pp == 0 || !pp->inited || w == 0)
		return -EINVAL;
	memset(w, 0, sizeof(*w));
	w->va = va;
	w->top_dma = pp->top_pd_dma;
	idx = va >> 12;
	table = pp->top_pd;
	for (lvl = pp->top, level = 0; lvl >= 0; lvl--, level++) {
		unsigned i = (unsigned)((idx >> ((unsigned)lvl * 9u)) & 511u);
		uint64_t raw;

		w->idx[level] = i;
		/* Read the entry the walker would read: the table as submitted. */
		parity_gt_clflush(&((uint64_t *)table->cpu)[i], sizeof(uint64_t));
		raw = ((volatile uint64_t *)table->cpu)[i];
		w->raw[level] = raw;
		w->levels = level + 1;
		w->scratch[level] = (lvl > 0) ? (raw == pp->scratch_encode[lvl]) :
			(raw == pp->scratch_encode[0]);
		if (lvl == 0) {
			w->leaf_dma = raw & PTE_ADDR_MASK;
			w->leaf_present = (raw & PARITY_GEN8_PAGE_PRESENT_B) != 0u;
			w->leaf_rw = (raw & PARITY_GEN8_PAGE_RW_B) != 0u;
			w->leaf_pat = ((raw & PARITY_GEN12_PTE_PAT0) ? 1u : 0u) |
				((raw & PARITY_GEN12_PTE_PAT1) ? 2u : 0u) |
				((raw & PARITY_GEN12_PTE_PAT2) ? 4u : 0u);
			break;
		}
		w->child_dma[level] = raw & PTE_ADDR_MASK;
		{
			struct parity_gt_ppgtt_table *t = table_by_dma(pp, w->child_dma[level]);

			if (t == 0)
				break;   /* scratch or foreign: the walk ends here */
			w->child_known[level] = 1;
			table = t->obj;
		}
	}
	return 0;
}

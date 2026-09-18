/*
 * WS031 Linux-parity — GPU-free kernel checks of the scanout buffer: layout, aligned and
 * guarded GGTT placement, coexistence with the GT window's objects, failure unwinding and
 * the refusal to release a buffer the display still reads.  Real DMA allocations, a
 * sentinel-filled PTE table standing in for the GGTT.  zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <string.h>
#include <errno.h>
#include "../gt_mem.h"
#include "scanout.h"
#include "scanout_ktest.h"

#define TABLE_ENTRIES 16384u
#define SENTINEL      0x5a5a5a5a5a5a5a5aull
#define SCRATCH_PTE   0x00000000dead0001ull

static uint64_t table[TABLE_ENTRIES];
static struct parity_gt_mem gm;
static struct parity_scanout so, so2;

static void table_fill(void)
{
	unsigned i;

	for (i = 0u; i < TABLE_ENTRIES; i++)
		table[i] = SENTINEL;
}

/* every entry outside [a0,a1) and [b0,b1) and the GT window's live objects must still be the sentinel */
static unsigned stray_writes(unsigned a0, unsigned a1, unsigned b0, unsigned b1, unsigned gt0, unsigned gt1)
{
	unsigned i, n = 0u;

	for (i = 0u; i < TABLE_ENTRIES; i++) {
		if ((i >= a0 && i < a1) || (i >= b0 && i < b1) || (i >= gt0 && i < gt1))
			continue;
		if (table[i] != SENTINEL)
			n++;
	}
	return n;
}

static int ptes_match(const struct parity_scanout *s)
{
	unsigned p;

	for (p = 0u; p < s->obj->pages; p++) {
		uint64_t dma = 0;

		if (parity_gt_object_page_dma(s->obj, p, &dma) != 0)
			return 0;
		/* independent expectation: page address | PRESENT, nothing else on this generation */
		if (table[s->obj->ggtt_page + p] != ((dma & ~0xfffull) | 1ull))
			return 0;
	}
	return 1;
}

static int guards_are_scratch(const struct parity_scanout *s)
{
	unsigned p;

	for (p = 0u; p < s->guard_pages; p++) {
		if (table[s->obj->ggtt_page - s->guard_pages + p] != SCRATCH_PTE ||
		    table[s->obj->ggtt_page + s->obj->pages + p] != SCRATCH_PTE)
			return 0;
	}
	return 1;
}

void parity_scanout_ktest(parity_scanout_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask)
{
	struct parity_gt_object *ring, *hwsp;
	uint64_t gt_snapshot[PARITY_GT_GGTT_PAGES];
	unsigned i, gt0, live_before, writes_before, alloc_before;
	int rc, rc2, same;

	/* ---- a GT window with live objects first: they must not notice the display side ---- */
	table_fill();
	rc = parity_gt_mem_init(&gm, dma, dma_mask, table, TABLE_ENTRIES, SCRATCH_PTE, 0);
	ring = rc == 0 ? parity_gt_object_create(&gm, 16u * 4096u) : 0;
	hwsp = rc == 0 ? parity_gt_object_create(&gm, 4096u) : 0;
	if (ring == 0 || hwsp == 0 || parity_gt_ggtt_bind(&gm, ring) != 0 || parity_gt_ggtt_bind(&gm, hwsp) != 0) {
		check(0, "scanout: SCANOUT-SETUP GT window objects");
		return;
	}
	gt0 = gm.window_first;
	for (i = 0u; i < PARITY_GT_GGTT_PAGES; i++)
		gt_snapshot[i] = table[gt0 + i];

	rc = parity_gt_display_window_init(&gm, PARITY_GT_DISPLAY_PAGES);
	check(rc == 0 && gm.display_first + gm.display_pages == gm.window_first &&
		parity_gt_display_window_init(&gm, PARITY_GT_DISPLAY_PAGES) == -EBUSY &&
		stray_writes(0u, 0u, 0u, 0u, gt0, gt0 + PARITY_GT_GGTT_PAGES) == 0u,
		"scanout: SCANOUT-WINDOW the display window sits directly below the GT window; claiming it writes no PTE");

	/* ---- layout: the MEMORY image, not the link's 18 bpp ---- */
	rc = parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so);
	check(rc == 0 && so.cpp == 4u && so.pitch == 7680u && so.stride_units == 120u && so.size == 8294400u &&
		so.obj->pages == 2025u && so.alignment == 262144u && so.guard_pages == 168u && so.cpu != 0 &&
		so.state == PARITY_SCANOUT_ALLOCATED,
		"scanout: SCANOUT-LAYOUT 1920x1080 XRGB8888 linear: 4 bytes/pixel, pitch 7680 (120 x 64), 8294400 bytes = 2025 pages, 256 KiB alignment, 168-PTE guards");
	if (rc != 0)
		return;

	/* ---- pin ---- */
	rc = parity_scanout_pin(&so, "ktest");
	check(rc == 0 && so.state == PARITY_SCANOUT_PINNED && (so.surf & 0x3ffffull) == 0u && (so.surf >> 32) == 0u &&
		so.obj->ggtt_page >= gm.display_first + so.guard_pages &&
		so.obj->ggtt_page + so.obj->pages + so.guard_pages <= gm.window_first &&
		so.publishes == 1u,
		"scanout: SCANOUT-PIN the surface address is 256 KiB aligned, below 4 GiB, inside the display window with room for both guards; the cache was flushed for the display");
	check(rc == 0 && ptes_match(&so) && guards_are_scratch(&so),
		"scanout: SCANOUT-PTES every backing page maps to its own DMA address (address | present); 168 scratch PTEs on each side");
	same = 1;
	for (i = 0u; i < PARITY_GT_GGTT_PAGES; i++)
		if (table[gt0 + i] != gt_snapshot[i])
			same = 0;
	check(rc == 0 && same && ring->bound && hwsp->bound &&
		stray_writes(so.obj->ggtt_page - so.guard_pages, so.obj->ggtt_page + so.obj->pages + so.guard_pages,
			0u, 0u, gt0, gt0 + PARITY_GT_GGTT_PAGES) == 0u,
		"scanout: SCANOUT-COEXIST the GT window's PTEs are byte-identical, its objects still bound, and no PTE outside the allocation was written");

	/* ---- a second buffer (the flip partner) fits next to it ---- */
	rc2 = parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so2);
	rc2 = rc2 == 0 ? parity_scanout_pin(&so2, "ktest-2") : rc2;
	check(rc2 == 0 && (so2.surf & 0x3ffffull) == 0u && ptes_match(&so2) && guards_are_scratch(&so2) && ptes_match(&so) &&
		(so2.obj->ggtt_page - so2.guard_pages >= so.obj->ggtt_page + so.obj->pages + so.guard_pages ||
		 so.obj->ggtt_page - so.guard_pages >= so2.obj->ggtt_page + so2.obj->pages + so2.guard_pages),
		"scanout: SCANOUT-SECOND a second full-HD buffer fits with its own alignment and guards, without overlap");

	/* ---- a buffer the display reads is not released ---- */
	rc = parity_scanout_begin(&so);
	check(rc == 0 && parity_scanout_unpin(&so) == -EBUSY && parity_scanout_destroy(&so) == -EBUSY &&
		so.refused_unpin == 1u && so.refused_destroy == 1u && ptes_match(&so),
		"scanout: SCANOUT-IN-USE unpin and destroy are refused while the display reads the buffer; its PTEs stay");
	parity_scanout_end(&so);
	check(parity_scanout_destroy(&so) == -EBUSY && parity_scanout_unpin(&so) == 0 &&
		table[gm.display_first + 0u] == SENTINEL,
		"scanout: SCANOUT-ORDER destroy before unpin is refused; after the scanout ended the unpin succeeds");
	{
		unsigned first = so2.obj->ggtt_page, pages = so2.obj->pages, scratch_ok = 1u;

		rc = parity_scanout_unpin(&so2);
		for (i = 0u; i < pages; i++)
			if (table[first + i] != SCRATCH_PTE)
				scratch_ok = 0u;
		check(rc == 0 && scratch_ok && gm.display_allocated_pages == 0u && so2.surf == 0u,
			"scanout: SCANOUT-UNPIN the pages' PTEs go back to scratch and the whole range (guards included) is free again");
	}
	live_before = gm.objects_live;
	check(parity_scanout_destroy(&so) == 0 && parity_scanout_destroy(&so2) == 0 && gm.objects_live + 2u == live_before,
		"scanout: SCANOUT-DESTROY both backings are returned");

	/* ---- refusals and unwinding ---- */
	live_before = gm.objects_live;
	check(parity_scanout_create(&gm, 1920u, 1080u, 0x34325241u /* AR24 */, PARITY_MOD_LINEAR, &so) == -EINVAL &&
		parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, 1ull /* X-tiled */, &so) == -EINVAL &&
		parity_scanout_create(&gm, 0u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so) == -EINVAL &&
		gm.objects_live == live_before,
		"scanout: SCANOUT-REFUSE another format, a tiled modifier or a zero size is refused before anything is allocated");

	/* a backing the GGTT cannot address: nothing reserved, no PTE written */
	rc = parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so);
	writes_before = gm.display_pte_writes;
	alloc_before = gm.display_allocated_pages;
	gm.dma_mask = 0xfffull;
	rc2 = rc == 0 ? parity_scanout_pin(&so, "ktest-range") : -1;
	gm.dma_mask = dma_mask;
	check(rc == 0 && rc2 == -ERANGE && so.state == PARITY_SCANOUT_ALLOCATED && gm.display_pte_writes == writes_before &&
		gm.display_allocated_pages == alloc_before && so.obj->bound == 0,
		"scanout: SCANOUT-UNWIND a page outside the DMA range fails the pin with nothing reserved and no PTE written");

	/* a window too small for the second buffer: -ENOSPC leaves the first intact */
	gm.display_pages = 4096u;             /* nothing is allocated now: shrink what the allocator may use (test only) */
	gm.display_first = gm.window_first - 4096u;
	rc = rc == 0 ? parity_scanout_pin(&so, "ktest") : rc;
	rc2 = parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so2);
	alloc_before = gm.display_allocated_pages;
	writes_before = gm.display_pte_writes;
	rc2 = rc2 == 0 ? parity_scanout_pin(&so2, "ktest-2") : rc2;
	check(rc == 0 && rc2 == -ENOSPC && gm.display_allocated_pages == alloc_before &&
		gm.display_pte_writes == writes_before && ptes_match(&so) && guards_are_scratch(&so),
		"scanout: SCANOUT-FULL no room for another buffer is -ENOSPC with nothing reserved or written; the pinned one is untouched");
	(void)parity_scanout_destroy(&so2);
	(void)parity_scanout_unpin(&so);
	gm.display_pages = PARITY_GT_DISPLAY_PAGES;
	gm.display_first = gm.window_first - PARITY_GT_DISPLAY_PAGES;
	(void)parity_scanout_pin(&so, "ktest");

	/* ---- a stop that could not be confirmed: the buffer survives teardown ---- */
	(void)parity_scanout_begin(&so);
	parity_scanout_abandon(&so);
	check(so.state == PARITY_SCANOUT_ABANDONED && parity_scanout_unpin(&so) == -EBUSY &&
		parity_scanout_destroy(&so) == -EBUSY,
		"scanout: SCANOUT-ABANDON a buffer whose scanout stop was not confirmed can no longer be unpinned or destroyed");
	parity_gt_mem_fini(&gm);
	check(gm.kept_objects == 1u && so.obj->in_use && ptes_match(&so),
		"scanout: SCANOUT-KEPT teardown leaves its pages, mapping and PTEs alone and says so (the other objects are released)");
	/* test only: give the memory back now that nothing can read it */
	so.obj->keep = 0;
	gm.inited = 1;
	parity_gt_object_destroy(&gm, so.obj);
	gm.inited = 0;
}

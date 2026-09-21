/*
 * WS031 Linux-parity — the scanout buffer on the REAL GGTT (display test configuration):
 * claim the display window next to the live GT resources, allocate / pin / fill / publish one
 * full-HD XRGB8888 buffer, verify placement, every PTE and the GT window's PTEs, then unpin and
 * release it.  The display engine is NOT pointed at it here: no plane, pipe or link register is
 * touched, so nothing scans it out.  zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <string.h>
#include <errno.h>
#include "../gt_mem.h"
#include "scanout.h"
#include "lcd_pattern.h"
#include "lcd_hw_check.h"
#include "parity_lcd_calc.h"

/* FNV-1a 64 of the 1920x1080 picture for id 110, pinned from the host render (lcd-pattern-host.c) */
#define LCD_PATTERN_110_FNV 0xce63f20b23f91f85ull

int parity_lcd_scanout_hw_check(struct parity_gt_mem *gm)
{
	static struct parity_scanout so;
	static uint64_t gt_before[PARITY_GT_GGTT_PAGES];
	uint64_t hash;
	unsigned i, gt_changed = 0u, pte_bad = 0u, guard_bad = 0u, back_bad = 0u, first_page, pages;
	uint32_t verify_bad, fx = 0u, fy = 0u;
	int rc, pin_rc, unpin_rc, destroy_rc, pass, plane_rc, plane_ok;
	static struct parity_lcd_words pw;
	uint32_t p_ctl = 0u, p_stride = 0u, p_size = 0u, p_color = 0u, p_surf = 0u;

	if (gm == 0 || !gm->inited) {
		kern_logf("i915: parity SCANOUT-TEST verdict: FAIL (no GT memory)\n");
		return -1;
	}
	for (i = 0u; i < PARITY_GT_GGTT_PAGES; i++)
		gt_before[i] = parity_gt_ggtt_read_pte(gm, gm->window_first + i);

	rc = parity_gt_display_window_init(gm, PARITY_GT_DISPLAY_PAGES);
	kern_logf("i915: parity SCANOUT-TEST window: rc=%d ggtt_entries=%u gt_window=[%u,+%u) display_window=[%u,+%u) "
		"gt_pages_in_use=%u objects_live=%u\n", rc, gm->entries, gm->window_first, gm->window_pages,
		gm->display_first, gm->display_pages, gm->allocated_pages, gm->objects_live);
	if (rc != 0) {
		kern_logf("i915: parity SCANOUT-TEST verdict: FAIL (display window rc=%d)\n", rc);
		return -1;
	}

	rc = parity_scanout_create(gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so);
	if (rc != 0) {
		kern_logf("i915: parity SCANOUT-TEST verdict: FAIL (create rc=%d: backing of 8294400 bytes)\n", rc);
		return -1;
	}
	pin_rc = parity_scanout_pin(&so, "scanout-hw-check");
	kern_logf("i915: parity SCANOUT-TEST buffer: format=XR24 modifier=linear %ux%u cpp=%u pitch=%u (stride units %u) "
		"size=%u pages=%u align=0x%x guard=%u | pin rc=%d surf=0x%08llx ggtt_page=%u contiguous=%d\n",
		so.width, so.height, so.cpp, so.pitch, so.stride_units, so.size, so.obj->pages, so.alignment,
		so.guard_pages, pin_rc, (unsigned long long)so.surf, so.obj->ggtt_page, so.obj->contiguous);
	if (pin_rc != 0) {
		(void)parity_scanout_destroy(&so);
		kern_logf("i915: parity SCANOUT-TEST verdict: FAIL (pin rc=%d)\n", pin_rc);
		return -1;
	}

	/* every PTE read back from the real table against the page's own DMA address */
	for (i = 0u; i < so.obj->pages; i++) {
		uint64_t dma = 0;

		if (parity_gt_object_page_dma(so.obj, i, &dma) != 0 ||
		    parity_gt_ggtt_read_pte(gm, so.obj->ggtt_page + i) != ((dma & ~0xfffull) | 1ull))
			pte_bad++;
	}
	for (i = 0u; i < so.guard_pages; i++) {
		if (parity_gt_ggtt_read_pte(gm, so.obj->ggtt_page - so.guard_pages + i) != gm->scratch_pte ||
		    parity_gt_ggtt_read_pte(gm, so.obj->ggtt_page + so.obj->pages + i) != gm->scratch_pte)
			guard_bad++;
	}
	for (i = 0u; i < PARITY_GT_GGTT_PAGES; i++)
		if (parity_gt_ggtt_read_pte(gm, gm->window_first + i) != gt_before[i])
			gt_changed++;

	/* the LCD-B picture: drawn by the CPU, made visible to the display, read back */
	hash = parity_lcd_pattern_fill(so.cpu, so.pitch, so.width, so.height, 110u);
	parity_scanout_publish(&so);
	verify_bad = parity_lcd_pattern_verify(so.cpu, so.pitch, so.width, so.height, 110u, &fx, &fy);
	kern_logf("i915: parity SCANOUT-TEST check: pte_bad=%u/%u guard_bad=%u/%u gt_window_ptes_changed=%u | pattern id=110 "
		"fnv=%016llx (pinned %016llx) readback_bad=%u publishes=%u | first pte=0x%llx last pte=0x%llx\n",
		pte_bad, so.obj->pages, guard_bad, so.guard_pages, gt_changed, (unsigned long long)hash,
		(unsigned long long)LCD_PATTERN_110_FNV, verify_bad, so.publishes,
		(unsigned long long)parity_gt_ggtt_read_pte(gm, so.obj->ggtt_page),
		(unsigned long long)parity_gt_ggtt_read_pte(gm, so.obj->ggtt_page + so.obj->pages - 1u));

	/* the plane words for THIS buffer, from the reference's plane writers (computed; nothing is written) */
	plane_rc = parity_lcd_emit_plane(0, 0, so.format, so.modifier, so.width, so.height, so.pitch, (uint32_t)so.surf, &pw);
	plane_ok = plane_rc == 0 && parity_lcd_words_find(&pw, 0x70180u, &p_ctl) == 1u && p_ctl == 0x94000000u &&
		parity_lcd_words_find(&pw, 0x70188u, &p_stride) == 1u && p_stride == so.stride_units && p_stride == 0x78u &&
		parity_lcd_words_find(&pw, 0x70190u, &p_size) == 1u && p_size == 0x0437077fu &&
		parity_lcd_words_find(&pw, 0x701ccu, &p_color) == 1u && p_color == 0x2000u &&
		parity_lcd_words_find(&pw, 0x7019cu, &p_surf) == 1u && p_surf == (uint32_t)so.surf && pw.n >= 2u &&
		pw.w[pw.n - 1u].reg == 0x7019cu;
	kern_logf("i915: parity SCANOUT-TEST plane words (computed; NOT written): rc=%d n=%u PLANE_CTL=0x%08x STRIDE=0x%x SIZE=0x%08x "
		"COLOR_CTL=0x%x SURF=0x%08x (Linux dump: 0x94000000 0x78 0x0437077f 0x2000, its own surf) match=%d\n",
		plane_rc, pw.n, p_ctl, p_stride, p_size, p_color, p_surf, plane_ok);

	/* release: nothing scans it out, so the ordinary order applies */
	first_page = so.obj->ggtt_page;
	pages = so.obj->pages;
	unpin_rc = parity_scanout_unpin(&so);
	for (i = 0u; i < pages; i++)
		if (parity_gt_ggtt_read_pte(gm, first_page + i) != gm->scratch_pte)
			back_bad++;
	destroy_rc = parity_scanout_destroy(&so);
	for (i = 0u; i < PARITY_GT_GGTT_PAGES; i++)
		if (parity_gt_ggtt_read_pte(gm, gm->window_first + i) != gt_before[i])
			gt_changed++;

	pass = pte_bad == 0u && guard_bad == 0u && gt_changed == 0u && hash == LCD_PATTERN_110_FNV &&
		verify_bad == 0u && unpin_rc == 0 && destroy_rc == 0 && back_bad == 0u &&
		gm->display_allocated_pages == 0u && plane_ok;
	kern_logf("i915: parity SCANOUT-TEST verdict: %s (unpin=%d destroy=%d ptes_back_to_scratch_bad=%u "
		"display_pages_in_use=%u gt_window_ptes_changed=%u display_pte_writes=%u)\n", pass ? "PASS" : "FAIL",
		unpin_rc, destroy_rc, back_bad, gm->display_allocated_pages, gt_changed, gm->display_pte_writes);
	return pass ? 0 : -1;
}

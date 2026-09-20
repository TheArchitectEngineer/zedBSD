#!/usr/bin/env python3
"""WS031 E-119 round 50: the second frame -- the GPU redraws the buffer that is NOT on the display, then the synchronous
flip.  The full-HD draw takes the render target's VA, a texture variant and "already mapped"; a render-target binding
lives for the whole run (map once; at the end: every PTE to scratch, re-read, TLB, then the owner may free).  LCD-D mode
(-DPARITY_LCDD_TEST=1).  usage: round50.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
D = "src/drivers/gpu/i915/"
P = D + "parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new, cnt=1):
    assert s.count(old) == cnt, (s.count(old), old[:100])
    return s.replace(old, new)

# ---------------- the second render-target VA ----------------
f = load(D + "draw_fixture.h")
if "I915_TEX_FHD_RT_B_VA" not in f: f = rep(f, "#define I915_TEX_FHD_RT_VA        0x100800000ull",
        "#define I915_TEX_FHD_RT_VA        0x100800000ull" + NL +
        "#define I915_TEX_FHD_RT_B_VA      0x101000000ull   /* LCD-D: the second buffer's render-target VA (A ends at 0x100fe9000) */")
save(D + "draw_fixture.h", f)

# ---------------- eu_test.h ----------------
h = load(P + "eu_test.h")
if "parity_fhd_render_run_ex" not in h: h = rep(h, "	uint64_t image_hash;" + NL + "};" + NL + "int parity_fhd_render_run(",
        "	uint64_t image_hash;" + NL +
        "	uint64_t rt_va;                        /* where the target is in the PPGTT */" + NL +
        "	unsigned variant;                      /* the texture variant drawn (the expected image follows it) */" + NL +
        "	int rt_premapped;                      /* the target's PTEs belong to a parity_fhd_rt_map, not to this draw */" + NL +
        "};" + NL + "int parity_fhd_render_run(")
if "parity_fhd_render_run_ex" not in h: h = rep(h, "/* wrong pixels of the target against the expected image (no CPU write) */",
        r"""/*
 * The same draw into a target at `rt_va` (I915_TEX_FHD_RT_VA or I915_TEX_FHD_RT_B_VA) with texture `variant`.  With
 * rt_premapped the target's pages were inserted by parity_fhd_rt_map() and stay mapped after this draw's release (the
 * walk of the first / middle / last page is still checked); parity_fhd_render_run() = (I915_TEX_FHD_RT_VA, 0, 0).
 */
int parity_fhd_render_run_ex(struct parity_fhd_render *x, struct parity_gt_engines *es, struct parity_gt_ppgtt *vm,
	struct parity_gt_mem *gm, struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms,
	struct parity_gt_object *rt, uint64_t rt_va, unsigned variant, int rt_premapped);
/*
 * A render target mapped for a whole run (LCD-D: A and B, each at its own VA, drawn many times).  map: every page of the
 * object's own backing at `va`, the walk of the first / middle / last page checked.  unmap: every PTE back to scratch,
 * re-read from the tables on each call (never summed), then the GT TLB; only then released=1 and the owner may free the
 * object.  The caller must have shown the GPU done with every draw that used it.
 */
struct parity_fhd_rt_map {
	struct parity_gt_object *rt;
	uint64_t va;
	unsigned pages, mapped, scratch, unmap_calls;
	int walk_ok, tlb_rc, released;
	uint64_t first_unreleased_va;
};
struct parity_gt_tlb;
int parity_fhd_rt_map(struct parity_fhd_rt_map *b, struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm,
	struct parity_gt_object *rt, uint64_t va);
int parity_fhd_rt_unmap(struct parity_fhd_rt_map *b, struct parity_gt_ppgtt *vm, struct parity_gt_tlb *tlb,
	struct parity_gt_engines *es, struct osdep_mmio *m, struct spinlock *uncore_lock);
/* wrong pixels of the target against the expected image (no CPU write) */""")
save(P + "eu_test.h", h)

# ---------------- eu_test.c ----------------
c = load(P + "eu_test.c")
c = rep(c, """	for (p = 0u; p < x->rt_pages_mapped && n < cap; p++)
		va[n++] = I915_TEX_FHD_RT_VA + (uint64_t)p * 4096u;""",
        """	for (p = 0u; p < x->rt_pages_mapped && n < cap; p++)       /* 0 when the target belongs to a parity_fhd_rt_map */
		va[n++] = x->rt_va + (uint64_t)p * 4096u;""")
c = rep(c, """	{ "render target = scanout backing", I915_TEX_FHD_RT_VA, (I915_TEX_FHD_RT_BYTES + 4095u) & ~4095u, 4096u },""",
        """	{ "render target = scanout backing", I915_TEX_FHD_RT_VA, (I915_TEX_FHD_RT_BYTES + 4095u) & ~4095u, 4096u },
	{ "LCD-D second render target = scanout backing", I915_TEX_FHD_RT_B_VA, (I915_TEX_FHD_RT_BYTES + 4095u) & ~4095u, 4096u },""")
c = rep(c, """	(void)x;
	drv_i915_tex_fixture_pattern(pattern, 0u);""", """	drv_i915_tex_fixture_pattern(pattern, x != 0 ? x->variant : 0u);""")
c = rep(c, """int
parity_fhd_render_run(struct parity_fhd_render *x, struct parity_gt_engines *es, struct parity_gt_ppgtt *vm,
	struct parity_gt_mem *gm, struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms,
	struct parity_gt_object *rt)
{""", """int
parity_fhd_render_run(struct parity_fhd_render *x, struct parity_gt_engines *es, struct parity_gt_ppgtt *vm,
	struct parity_gt_mem *gm, struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms,
	struct parity_gt_object *rt)
{
	return parity_fhd_render_run_ex(x, es, vm, gm, m, uncore_lock, timeout_ms, rt, I915_TEX_FHD_RT_VA, 0u, 0);
}

int
parity_fhd_render_run_ex(struct parity_fhd_render *x, struct parity_gt_engines *es, struct parity_gt_ppgtt *vm,
	struct parity_gt_mem *gm, struct osdep_mmio *m, struct spinlock *uncore_lock, unsigned timeout_ms,
	struct parity_gt_object *rt, uint64_t rt_va, unsigned variant, int rt_premapped)
{""")
c = rep(c, """	x->first_bad_x = -1;
	x->first_bad_y = -1;
	if (parity_fhd_va_layout(0, 0) != 0)""", """	x->first_bad_x = -1;
	x->first_bad_y = -1;
	x->rt_va = rt_va;
	x->variant = variant;
	x->rt_premapped = rt_premapped;
	if (rt_va != I915_TEX_FHD_RT_VA && rt_va != I915_TEX_FHD_RT_B_VA)
		return fail(t, -EINVAL, "render target VA not in the layout");
	if (parity_fhd_va_layout(0, 0) != 0)""")
c = rep(c, """	if (rc == 0)
		rc = parity_gt_ppgtt_alloc_range(gm, vm, I915_TEX_FHD_RT_VA, (uint64_t)rt->pages * 4096u);""",
        """	if (rc == 0 && !rt_premapped)
		rc = parity_gt_ppgtt_alloc_range(gm, vm, rt_va, (uint64_t)rt->pages * 4096u);""")
c = rep(c, """	for (p = 0u; p < x->rt_pages && rc == 0; p++) {
		rc = parity_gt_object_page_dma(rt, p, &dma);
		if (rc == 0)
			rc = parity_gt_ppgtt_insert_page(vm, dma, I915_TEX_FHD_RT_VA + (uint64_t)p * 4096u, 0u);""",
        """	for (p = 0u; p < x->rt_pages && rc == 0 && !rt_premapped; p++) {
		rc = parity_gt_object_page_dma(rt, p, &dma);
		if (rc == 0)
			rc = parity_gt_ppgtt_insert_page(vm, dma, rt_va + (uint64_t)p * 4096u, 0u);""")
c = rep(c, "parity_gt_ppgtt_walk(vm, I915_TEX_FHD_RT_VA + (uint64_t)pg * 4096u, &w)", "parity_gt_ppgtt_walk(vm, rt_va + (uint64_t)pg * 4096u, &w)")
_i = c.index("parity_fhd_render_run_ex(struct parity_fhd_render *x")
c = c[:_i] + rep(c[_i:], """	x->mocs = drv_i915_draw_fixture_mocs();
	drv_i915_tex_fixture_pattern(pattern, 0u);
	tb = (volatile uint8_t *)x->tex->cpu;""", """	x->mocs = drv_i915_draw_fixture_mocs();
	drv_i915_tex_fixture_pattern(pattern, variant);
	tb = (volatile uint8_t *)x->tex->cpu;""")
c = rep(c, "drv_i915_tex_fixture_fhd_write_state(t->shared->cpu, I915_TEX_FHD_RT_VA, I915_TEX_FIXTURE_TEX_VA, x->mocs);",
        "drv_i915_tex_fixture_fhd_write_state(t->shared->cpu, rt_va, I915_TEX_FIXTURE_TEX_VA, x->mocs);")
c = rep(c, """			drv_i915_tex_fixture_pattern(pat, 0u);
			x->px_total""", """			drv_i915_tex_fixture_pattern(pat, variant);
			x->px_total""")
c = rep(c, "/* ---------------- T3: texture update, binding switch, redraw, new context ---------------- */",
        r"""int
parity_fhd_rt_map(struct parity_fhd_rt_map *b, struct parity_gt_mem *gm, struct parity_gt_ppgtt *vm,
	struct parity_gt_object *rt, uint64_t va)
{
	struct parity_gt_ppgtt_walk w;
	uint64_t dma;
	unsigned p, i;
	int rc;

	if (b == 0 || gm == 0 || vm == 0 || rt == 0 || (va != I915_TEX_FHD_RT_VA && va != I915_TEX_FHD_RT_B_VA))
		return -EINVAL;
	memset(b, 0, sizeof(*b));
	b->rt = rt;
	b->va = va;
	b->pages = (I915_TEX_FHD_RT_BYTES + 4095u) / 4096u;
	if (b->pages > rt->pages)
		b->pages = rt->pages;
	rc = parity_gt_ppgtt_alloc_range(gm, vm, va, (uint64_t)b->pages * 4096u);
	for (p = 0u; p < b->pages && rc == 0; p++) {
		rc = parity_gt_object_page_dma(rt, p, &dma);
		if (rc == 0)
			rc = parity_gt_ppgtt_insert_page(vm, dma, va + (uint64_t)p * 4096u, 0u);
		if (rc == 0)
			b->mapped++;            /* what the unmap must take down, even after a partial map */
	}
	if (rc != 0)
		return rc;
	b->walk_ok = 1;
	for (i = 0u; i < 3u; i++) {
		unsigned pg = i == 0u ? 0u : i == 1u ? b->pages / 2u : b->pages - 1u;

		if (parity_gt_object_page_dma(rt, pg, &dma) != 0 || parity_gt_ppgtt_walk(vm, va + (uint64_t)pg * 4096u, &w) != 0 ||
		    w.levels != 4 || !w.leaf_present || w.leaf_dma != (dma & ~0xfffull))
			b->walk_ok = 0;
	}
	return b->walk_ok ? 0 : -EFAULT;
}

int
parity_fhd_rt_unmap(struct parity_fhd_rt_map *b, struct parity_gt_ppgtt *vm, struct parity_gt_tlb *tlb,
	struct parity_gt_engines *es, struct osdep_mmio *m, struct spinlock *uncore_lock)
{
	struct parity_gt_ppgtt_walk w;
	unsigned p;

	if (b == 0 || vm == 0 || tlb == 0 || es == 0 || m == 0)
		return -EINVAL;
	if (b->released)
		return 0;
	b->unmap_calls++;
	for (p = 0u; p < b->mapped; p++)
		(void)parity_gt_ppgtt_insert_scratch(vm, b->va + (uint64_t)p * 4096u);
	b->scratch = 0u;
	b->first_unreleased_va = 0u;
	for (p = 0u; p < b->mapped; p++) {
		uint64_t va = b->va + (uint64_t)p * 4096u;

		if (parity_gt_ppgtt_walk(vm, va, &w) == 0 && w.levels == 4 && w.scratch[3])
			b->scratch++;
		else if (b->first_unreleased_va == 0u)
			b->first_unreleased_va = va;
	}
	if (b->scratch != b->mapped)
		return -EIO;
	b->tlb_rc = parity_gt_invalidate_tlb_full(tlb, es, m, uncore_lock);
	if (b->tlb_rc != 0)
		return b->tlb_rc;
	b->released = 1;
	b->rt = 0;
	return 0;
}

/* ---------------- T3: texture update, binding switch, redraw, new context ---------------- */""")
save(P + "eu_test.c", c)

# ---------------- ktest ----------------
t = load(L + "lcdg_ktest.c")
t = rep(t, """	memset(x, 0, sizeof(*x));
	x->t.shared = parity_gt_object_create(&gm, 4096u);""", """	memset(x, 0, sizeof(*x));
	x->rt_va = I915_TEX_FHD_RT_VA;
	x->t.shared = parity_gt_object_create(&gm, 4096u);""")
t = rep(t, """	if (rc == 0)
		rc = parity_gt_ppgtt_alloc_range(&gm, &vm, I915_TEX_FHD_RT_VA, (uint64_t)rt_pages * 4096u);""",
        """	if (rc == 0 && rt_pages != 0u)
		rc = parity_gt_ppgtt_alloc_range(&gm, &vm, I915_TEX_FHD_RT_VA, (uint64_t)rt_pages * 4096u);""")
t = rep(t, "void parity_lcdg_ktest(parity_lcdg_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask)", r"""static unsigned present_at(uint64_t va, unsigned n)
{
	struct parity_gt_ppgtt_walk w;
	unsigned i, k = 0u;

	for (i = 0u; i < n; i++)
		if (parity_gt_ppgtt_walk(&vm, va + (uint64_t)i * 4096u, &w) == 0 && w.levels == 4 && w.leaf_present && !w.scratch[3])
			k++;
	return k;
}

static struct parity_fhd_rt_map bmap;

void parity_lcdg_ktest(parity_lcdg_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask)""")
t = rep(t, """		"lcdg: REL-ONCE a further call after success changes nothing");
	parity_gt_object_destroy(&gm, rt);""", r"""		"lcdg: REL-ONCE a further call after success changes nothing");

	/* ---- LCD-D: a render target mapped for the whole run; the draws leave it, its own unmap takes it down ---- */
	{
		const struct parity_fhd_va *lay;
		unsigned nlay, live2;

		check(parity_fhd_va_layout(&lay, &nlay) == 0 && nlay == 5u && lay[4].va == I915_TEX_FHD_RT_B_VA,
			"lcdg: RTMAP-LAYOUT the second render target has its own VA range, no overlap with A or the draw's pages");
		rc = parity_fhd_rt_map(&bmap, &gm, &vm, rt, I915_TEX_FHD_RT_B_VA);
		check(rc == 0 && bmap.mapped == 4u && bmap.walk_ok && present_at(I915_TEX_FHD_RT_B_VA, 4u) == 4u,
			"lcdg: RTMAP-MAP every page of the target at B's VA, the walk names the object's own pages");
		rc = map_draw(&fr, rt, 0u);
		fr.rt_va = I915_TEX_FHD_RT_B_VA;
		fr.rt_premapped = 1;
		fr.gpu_done = 1;
		live2 = gm.objects_live;
		rrc = rc == 0 ? parity_fhd_render_release(&fr, &gm, &vm, &tlb, &es, &m, &ulock) : rc;
		check(rrc == 0 && fr.maps_total == 3u && fr.maps_scratch == 3u && present_ptes(0u) == 0u &&
			present_at(I915_TEX_FHD_RT_B_VA, 4u) == 4u && gm.objects_live == live2 - 3u && rt->in_use,
			"lcdg: RTMAP-DRAW a draw into the mapped target releases only its own 3 PTEs + objects; the target stays mapped");
		tf.stuck = 1;
		rrc = parity_fhd_rt_unmap(&bmap, &vm, &tlb, &es, &m, &ulock);
		check(rrc == -ETIMEDOUT && !bmap.released && bmap.scratch == 4u && present_at(I915_TEX_FHD_RT_B_VA, 4u) == 0u,
			"lcdg: RTMAP-TLB PTEs back to scratch but the TLB failed: not released (the owner must not free the target)");
		tf.stuck = 0;
		rrc = parity_fhd_rt_unmap(&bmap, &vm, &tlb, &es, &m, &ulock);
		check(rrc == 0 && bmap.released && bmap.scratch == 4u && bmap.unmap_calls == 2u &&
			parity_fhd_rt_unmap(&bmap, &vm, &tlb, &es, &m, &ulock) == 0 && bmap.unmap_calls == 2u,
			"lcdg: RTMAP-RETRY the retry re-verifies 4 of 4 (not 8), then the TLB; a further call changes nothing");
		check(parity_fhd_rt_map(&bmap, &gm, &vm, rt, 0x100000000ull) == -EINVAL,
			"lcdg: RTMAP-VA a VA outside the layout is refused");
	}
	parity_gt_object_destroy(&gm, rt);""")
save(L + "lcdg_ktest.c", t)

# ---------------- LCD-D in the kernel backend ----------------
k = load(L + "parity_lcd_kernel.c")
k = rep(k, "	struct parity_scanout *flip_a, *flip_b; unsigned flips_done;", "	struct parity_scanout *flip_a, *flip_b; unsigned flips_done, draws_ok;")
k = k.rstrip(NL) + NL + r'''
/* ---------------- LCD-D (-DPARITY_LCDD_TEST=1): the GPU redraws the buffer NOT on the display, then the flip ---------------- */

#define LCDD_ROUNDS 8u
#define LCDD_HOLD_MS 8000u
static struct parity_scanout lcdd_buf[2];
static struct parity_fhd_rt_map lcdd_map[2];
static struct parity_fhd_render lcdd_render, lcdd_check;
static struct parity_gt_tlb lcdd_tlb;
static unsigned lcdd_variant[2];
static int lcdd_gpu_kept;

static uint32_t lcdd_verify(void *ctx, const struct parity_scanout *so)
{
	(void)ctx;
	parity_gt_clflush(so->cpu, so->size);
	memset(&lcdd_check, 0, sizeof(lcdd_check));
	lcdd_check.variant = lcdd_variant[so == &lcdd_buf[0] ? 0 : 1];
	return parity_fhd_render_verify(&lcdd_check, so->cpu, so->pitch);
}

/*
 * One GPU draw into buffer i (never the one on the display), checked pixel by pixel after retire + park + clflush, then
 * the draw's own PTEs / TLB / objects released (the target's mapping stays).  0 = drawn and released.  GPU not shown
 * done: every object the request may use is kept and the device latch is set; no further GPU work or flip follows.
 */
static int lcdd_draw(struct lcd_kernel *k, unsigned i, unsigned variant, unsigned round)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_fhd_render *x = &lcdd_render;
	int rc, rrc;

	rc = parity_fhd_render_run_ex(x, d->es, d->vm, d->gm, d->mmio, d->uncore_lock, 2000u, lcdd_buf[i].obj, lcdd_map[i].va,
		variant, 1);
	kern_logf("i915: parity LCD-D draw %u: into buffer %c (not on the display) variant %u at 0x%llx | rc=%d outcome=%d gpu_done=%d | "
		"markers %08x/%08x/%08x ps %08x | pixels %u/%u (stale %u, first bad %d,%d) | tex changed %u guard bad %u | "
		"walk first/mid/last=%d | hash %016llx\n", round, 'A' + (int)i, variant, (unsigned long long)lcdd_map[i].va, rc,
		x->t.outcome, x->gpu_done, x->marker_before, x->marker_middraw, x->marker_after, x->ps_marker, x->px_match, x->px_total,
		x->px_stale, x->first_bad_x, x->first_bad_y, x->tex_changed_bytes, x->guard_bad_bytes, x->rt_walk_ok,
		(unsigned long long)x->image_hash);
	if (x->t.submitted && !x->gpu_done) {
		parity_fhd_render_keep(x);
		lcdd_gpu_kept = 1;
		parity_lcd_show_retain_gpu(d->gm, "an LCD-D draw was not shown to finish");
		return -5;
	}
	rrc = parity_fhd_render_release(x, d->gm, d->vm, &lcdd_tlb, d->es, d->mmio, d->uncore_lock);
	kern_logf("i915: parity LCD-D draw %u release: the draw's PTEs back to scratch %u/%u, TLB rc=%d, released=%d | the target "
		"stays mapped (%u pages at 0x%llx)\n", round, x->maps_scratch, x->maps_total, x->tlb_rc, x->released,
		lcdd_map[i].mapped, (unsigned long long)lcdd_map[i].va);
	if (rrc != 0) {
		lcdd_gpu_kept = 1;
		parity_lcd_show_retain_gpu(d->gm, "an LCD-D draw's mappings / TLB could not be shown released");
		return -5;
	}
	if (rc != 0 || x->t.outcome != PARITY_EU_PASS)
		return -5;
	lcdd_variant[i] = variant;
	k->draws_ok++;
	return 0;
}

static int lcdd_rounds(void *ctx, struct parity_lcd_observer *o)
{
	struct lcd_kernel *k = ctx;
	struct parity_lcd_flip_result fr;
	unsigned r, front = 0u;
	int rc;

	(void)o;
	kern_logf("i915: parity LCD-D shown 0: buffer A variant %u (GPU-drawn) -- take the photograph\n", lcdd_variant[0]);
	step_sleep(k, LCDD_HOLD_MS);
	for (r = 1u; r <= LCDD_ROUNDS; r++) {
		unsigned back = front ^ 1u, variant = r % 4u;

		if (lcdd_buf[back].state != PARITY_SCANOUT_PINNED)
			return -22;                     /* the display may still read it: never a draw target */
		if (lcdd_draw(k, back, variant, r) != 0)
			return -5;
		if (parity_scanout_begin(&lcdd_buf[back]) != 0)
			return -22;
		rc = parity_lcd_modeset_flip((uint32_t)lcdd_buf[back].surf, &fr);
		kern_logf("i915: parity LCD-D flip %u gen %u: surf 0x%08x -> 0x%08x | live 0x%08x -> 0x%08x | frame %u -> %u | event rc=%d | "
			"update errors %d | result %s\n", r, fr.gen, fr.old_surf, fr.new_surf, fr.live_before, fr.live_after, fr.frame_before,
			fr.frame_after, fr.event_rc, fr.update_errors,
			fr.result == PARITY_LCD_FLIP_DONE ? "DONE" : fr.result == PARITY_LCD_FLIP_NOT_LATCHED ? "NOT-LATCHED" :
			fr.result == PARITY_LCD_FLIP_TIMEOUT ? "TIMEOUT" : "REFUSED");
		if (rc != PARITY_LCD_MS_OK || fr.result != PARITY_LCD_FLIP_DONE)
			return -5;                      /* both buffers stay in use; the stop path decides */
		parity_scanout_end(&lcdd_buf[front]);   /* the old front is no longer read: the next draw target */
		front = back;
		k->flips_done++;
		kern_logf("i915: parity LCD-D shown %u: buffer %c variant %u (surf 0x%08x) -- take the photograph\n", r, 'A' + (int)front,
			variant, (uint32_t)lcdd_buf[front].surf);
		step_sleep(k, LCDD_HOLD_MS);
	}
	return 0;
}

int parity_lcd_kernel_lcdd_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	uint32_t bad[2] = { 0u, 0u };
	int rc, i, held = 0, released = 0, unmapped = 0, urc[2] = { -1, -1 };

	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || d->irq == 0 || d->es == 0 || d->vm == 0) {
		kern_logf("i915: parity LCD-D verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained() || lcdd_buf[0].state != PARITY_SCANOUT_NONE ||
	    lcdd_buf[1].state != PARITY_SCANOUT_NONE || lcdd_gpu_kept) {
		lcdb_summary.retained = 1;
		kern_logf("i915: parity LCD-D verdict: FAIL (refused before any initialisation: retained resources)\n");
		return -1;
	}
	if (!lcdb_locks_live) {
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_DPLL], LOCK_RANK_DEVICE, "parity-lcd-dpll");
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_BACKLIGHT], LOCK_RANK_DEVICE, "parity-lcd-backlight");
		lcdb_locks_live = 1;
	}
	memset(k, 0, sizeof(*k));
	k->locks = lcdb_locks;
	k->d = d;
	bind_ops(k);
	memset(&env, 0, sizeof(env));
	if (preflight(k) != 0 || fill_cfg(k, &env.cfg) != 0) {
		kern_logf("i915: parity LCD-D verdict: FAIL (preflight: nothing was written)\n");
		return -1;
	}
	rc = parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);
	if (rc != 0 && rc != -EBUSY)
		return -1;
	for (i = 0; i < 2 && (rc == 0 || rc == -EBUSY); i++) {
		rc = parity_scanout_create(d->gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &lcdd_buf[i]);
		rc = rc == 0 ? parity_scanout_pin(&lcdd_buf[i], i == 0 ? "lcd-d A" : "lcd-d B") : rc;
		/* the render-target mapping lives for the whole run: A at the first VA, B at the second */
		rc = rc == 0 ? parity_fhd_rt_map(&lcdd_map[i], d->gm, d->vm, lcdd_buf[i].obj, i == 0 ? I915_TEX_FHD_RT_VA :
			I915_TEX_FHD_RT_B_VA) : rc;
	}
	kern_logf("i915: parity LCD-D buffers: A surf 0x%08llx ggtt page %u -> PPGTT 0x%llx (%u pages, walk %d) | B surf 0x%08llx ggtt "
		"page %u -> PPGTT 0x%llx (%u pages, walk %d) | rc=%d\n", (unsigned long long)lcdd_buf[0].surf,
		lcdd_buf[0].obj != 0 ? lcdd_buf[0].obj->ggtt_page : 0u, (unsigned long long)lcdd_map[0].va, lcdd_map[0].mapped,
		lcdd_map[0].walk_ok, (unsigned long long)lcdd_buf[1].surf, lcdd_buf[1].obj != 0 ? lcdd_buf[1].obj->ggtt_page : 0u,
		(unsigned long long)lcdd_map[1].va, lcdd_map[1].mapped, lcdd_map[1].walk_ok, rc);
	/* the first picture: the GPU draws A (variant 0) before the display gets it */
	if (rc == 0)
		rc = lcdd_draw(k, 0u, 0u, 0u);
	if (rc != 0) {
		kern_logf("i915: parity LCD-D verdict: FAIL (setup / first draw rc=%d; the display was not started)\n", rc);
		goto reclaim;
	}

	env.hw = &k->ops;
	env.gm = d->gm;
	env.lcd = &d->edp->lcd;
	env.pipe = 0;
	env.first_frames_ms = 1000u;
	env.window_ms = 1000u;
	env.in_window = lcdd_rounds;
	env.in_window_ctx = k;
	env.at_stage = at_stage;
	env.at_stage_ctx = k;
	k->pattern_id = 0u;
	k->window_ms = env.window_ms;
	rc = parity_lcd_show_prepared(&env, &lcdd_buf[0], lcdd_verify, 0, &rep);
	log_trace(rep.trace);
	log_observer(&rep.obs);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];
	if (!rep.display_released && rep.display_acquired) {
		/* the stop is not confirmed: the display may read either buffer -- both kept with their mappings */
		for (i = 0; i < 2; i++)
			if (lcdd_buf[i].state >= PARITY_SCANOUT_PINNED && lcdd_buf[i].state != PARITY_SCANOUT_ABANDONED)
				parity_scanout_abandon(&lcdd_buf[i]);
		goto verdict;
	}
reclaim:
	/* the display provably reads neither buffer */
	for (i = 0; i < 2; i++)
		if (lcdd_buf[i].state == PARITY_SCANOUT_IN_USE)
			parity_scanout_end(&lcdd_buf[i]);
	if (lcdd_gpu_kept) {
		/* a draw was not shown done (or its release failed): the targets, their mappings and the draw stay */
		for (i = 0; i < 2; i++)
			if (lcdd_buf[i].state >= PARITY_SCANOUT_PINNED && lcdd_buf[i].state != PARITY_SCANOUT_ABANDONED)
				parity_scanout_abandon(&lcdd_buf[i]);
		goto verdict;
	}
	for (i = 0; i < 2; i++)
		if (lcdd_buf[i].obj != 0 && lcdd_buf[i].state == PARITY_SCANOUT_PINNED)
			bad[i] = lcdd_verify(0, &lcdd_buf[i]);
	unmapped = 1;
	for (i = 0; i < 2; i++) {
		urc[i] = lcdd_map[i].rt != 0 || lcdd_map[i].mapped != 0u ? parity_fhd_rt_unmap(&lcdd_map[i], d->vm, &lcdd_tlb, d->es,
			d->mmio, d->uncore_lock) : 0;
		if (urc[i] != 0)
			unmapped = 0;
	}
	kern_logf("i915: parity LCD-D release: A target PTEs back to scratch %u/%u rc=%d | B %u/%u rc=%d | TLB invalidations %u "
		"(timeouts %u)\n", lcdd_map[0].scratch, lcdd_map[0].mapped, urc[0], lcdd_map[1].scratch, lcdd_map[1].mapped, urc[1],
		lcdd_tlb.invalidations, lcdd_tlb.timeouts);
	if (!unmapped) {
		for (i = 0; i < 2; i++)
			if (lcdd_buf[i].state >= PARITY_SCANOUT_PINNED && lcdd_buf[i].state != PARITY_SCANOUT_ABANDONED)
				parity_scanout_abandon(&lcdd_buf[i]);
		parity_lcd_show_retain_gpu(d->gm, "an LCD-D render target could not be shown unmapped");
		goto verdict;
	}
	released = 1;
	for (i = 0; i < 2; i++)
		if (lcdd_buf[i].state != PARITY_SCANOUT_NONE &&
		    (parity_scanout_unpin(&lcdd_buf[i]) != 0 || parity_scanout_destroy(&lcdd_buf[i]) != 0))
			released = 0;
verdict:
	lcdb_summary.pass = rc == 0 && k->draws_ok == LCDD_ROUNDS + 1u && k->flips_done == LCDD_ROUNDS && released && held == 0 &&
		bad[0] == 0u && bad[1] == 0u && k->unresolved_steps == 0u && k->time_faults == 0u;
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.retained = parity_lcd_show_retained();
	kern_logf("i915: parity LCD-D verdict: %s (GPU draws %u/%u, flips done %u/%u, one modeset, stop %s, targets unmapped=%d, "
		"both buffers released=%d, pixels after: A bad %u B bad %u, power refs held %d, first anomaly: %s)\n",
		lcdb_summary.pass ? "PASS" : "FAIL", k->draws_ok, LCDD_ROUNDS + 1u, k->flips_done, LCDD_ROUNDS,
		rep.display_released ? "confirmed" : rep.display_acquired ? "NOT confirmed" : "not started", unmapped, released, bad[0],
		bad[1], held, rep.first_anomaly != 0 ? rep.first_anomaly : "none");
	return lcdb_summary.pass ? 0 : -1;
}
'''
save(L + "parity_lcd_kernel.c", k)
kh = load(L + "parity_lcd_kernel.h")
kh = rep(kh, "int parity_lcd_kernel_lcdc_run(const struct parity_lcd_kernel_deps *d);",
         "int parity_lcd_kernel_lcdc_run(const struct parity_lcd_kernel_deps *d);" + NL +
         "/* LCD-D (-DPARITY_LCDD_TEST=1): the GPU redraws the hidden buffer (A/B at their own PPGTT VAs, mapped for the run), then the flip; 8 rounds */" + NL +
         "int parity_lcd_kernel_lcdd_run(const struct parity_lcd_kernel_deps *d);")
save(L + "parity_lcd_kernel.h", kh)
b = load(P + "bios.h")
b = rep(b, "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST)",
        "#ifndef PARITY_LCDD_TEST" + NL + "#define PARITY_LCDD_TEST 0            /* LCD-D: GPU back-buffer redraw + synchronous flip */" + NL + "#endif" + NL +
        "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST || PARITY_LCDD_TEST)")
save(P + "bios.h", b)
pc = load(P + "probe.c")
pc = rep(pc, "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST) {",
         "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST || PARITY_LCDD_TEST) {")
pc = rep(pc, "			if (PARITY_LCDG_TEST) {", "			if (PARITY_LCDG_TEST || PARITY_LCDD_TEST) {")
pc = rep(pc, """				if (gheld == 5u)
					(void)parity_lcd_kernel_lcdg_run(&lcdb);
				else
					kern_logf("i915: parity LCD-G verdict: FAIL (forcewake rc=%d; nothing submitted)\\n", gfrc);""",
         """				if (gheld == 5u && PARITY_LCDD_TEST)
					(void)parity_lcd_kernel_lcdd_run(&lcdb);
				else if (gheld == 5u)
					(void)parity_lcd_kernel_lcdg_run(&lcdb);
				else
					kern_logf("i915: parity LCD-%c verdict: FAIL (forcewake rc=%d; nothing submitted)\\n",
						PARITY_LCDD_TEST ? 'D' : 'G', gfrc);""")
save(P + "probe.c", pc)
print("done")

#!/usr/bin/env python3
"""WS031 E-118 round 41: LCD-G (-DPARITY_LCDG_TEST=1): the GPU draws the full-HD textured image into the scanout
buffer's own backing (PPGTT), the CPU verifies every pixel without writing, the SAME object is shown through its GGTT
binding, stopped, and released only after both users are done.  GPU-free checks of the fixture, the VA layout, the
verifier and the prepared-buffer display entry.  usage: round41.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
I = "src/drivers/gpu/i915/"
P = I + "parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

kh = load(L + "parity_lcd_kernel.h")
kh = rep(kh, "struct parity_gt_mem;", "struct parity_gt_mem;" + NL + "struct parity_gt_engines;" + NL + "struct parity_gt_ppgtt;" + NL + "struct spinlock;")
kh = rep(kh, "	int ipc_enabled;                        /* skl_watermark_ipc_init()'s result */",
         "	int ipc_enabled;                        /* skl_watermark_ipc_init()'s result */" + NL +
         "	/* LCD-G only: the GT the draw is submitted to (forcewake is held by the caller) */" + NL +
         "	struct parity_gt_engines *es;" + NL + "	struct parity_gt_ppgtt *vm;" + NL + "	struct spinlock *uncore_lock;")
kh = rep(kh, "int parity_lcd_kernel_lcdr_run(const struct parity_lcd_kernel_deps *d);",
         "int parity_lcd_kernel_lcdr_run(const struct parity_lcd_kernel_deps *d);" + NL +
         "/* LCD-G (-DPARITY_LCDG_TEST=1): a GPU-drawn full-HD image shown from the same backing */" + NL +
         "int parity_lcd_kernel_lcdg_run(const struct parity_lcd_kernel_deps *d);")
save(L + "parity_lcd_kernel.h", kh)

k = load(L + "parity_lcd_kernel.c")
k = rep(k, '#include "parity_lcd_modeset.h"', '#include "parity_lcd_modeset.h"' + NL + '#include "../eu_test.h"' + NL + '#include "../../draw_fixture.h"')
k = k.rstrip(NL) + NL + r'''
/* ---------------- LCD-G (-DPARITY_LCDG_TEST=1) ---------------- */

static struct parity_scanout lcdg_scanout;      /* outlives the run when the buffer is abandoned */
static struct parity_fhd_render lcdg_render;

static uint32_t lcdg_verify(void *ctx, const struct parity_scanout *so)
{
	(void)ctx;
	parity_gt_clflush(so->cpu, so->size);   /* read what is in memory, not a stale CPU line */
	return parity_fhd_render_verify(&lcdg_render, so->cpu, so->pitch);
}

int parity_lcd_kernel_lcdg_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	struct parity_scanout *so = &lcdg_scanout;
	struct parity_fhd_render *fr = &lcdg_render;
	uint64_t ggtt_first = 0u, ggtt_last = 0u;
	int rc, same_backing, released = 0, render_released = 0, i, held = 0;

	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || d->es == 0 || d->vm == 0 || d->irq == 0) {
		kern_logf("i915: parity LCD-G verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained() || so->state != PARITY_SCANOUT_NONE) {
		lcdb_summary.retained = 1;
		lcdb_summary.first_anomaly = "refused: resources of an earlier run are retained";
		kern_logf("i915: parity LCD-G verdict: FAIL (refused before any initialisation: retained resources)\n");
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
		kern_logf("i915: parity LCD-G verdict: FAIL (preflight: nothing was written)\n");
		lcdb_summary.first_anomaly = "preflight refused (nothing written)";
		return -1;
	}

	/* ---- one backing: the scanout buffer (GGTT display binding) ---- */
	rc = parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);
	if (rc != 0 && rc != -EBUSY) {
		kern_logf("i915: parity LCD-G verdict: FAIL (display window rc=%d)\n", rc);
		return -1;
	}
	rc = parity_scanout_create(d->gm, I915_TEX_FHD_WIDTH, I915_TEX_FHD_HEIGHT, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, so);
	if (rc == 0 && (so->pitch != I915_TEX_FHD_PITCH || so->size != I915_TEX_FHD_RT_BYTES)) {
		kern_logf("i915: parity LCD-G: scanout layout pitch=%u size=%u differs from the render target's (isl: %u / %u)\n",
			so->pitch, so->size, I915_TEX_FHD_PITCH, I915_TEX_FHD_RT_BYTES);
		(void)parity_scanout_destroy(so);
		rc = -EINVAL;
	}
	if (rc == 0 && (rc = parity_scanout_pin(so, "lcd-g")) != 0)
		(void)parity_scanout_destroy(so);
	if (rc != 0) {
		kern_logf("i915: parity LCD-G verdict: FAIL (scanout buffer rc=%d)\n", rc);
		return -1;
	}

	/* ---- ... and its PPGTT mapping: the GPU draws into the same pages ---- */
	rc = parity_fhd_render_run(fr, d->es, d->vm, d->gm, d->mmio, d->uncore_lock, 2000u, so->obj);
	ggtt_first = parity_gt_ggtt_read_pte(d->gm, so->obj->ggtt_page);
	ggtt_last = parity_gt_ggtt_read_pte(d->gm, so->obj->ggtt_page + fr->rt_pages - 1u);
	same_backing = fr->rt == so->obj && fr->rt_walk_ok && (ggtt_first & ~0xfffull) == (fr->rt_first_dma & ~0xfffull) &&
		(ggtt_last & ~0xfffull) == (fr->rt_last_dma & ~0xfffull);
	kern_logf("i915: parity LCD-G render: rc=%d where=%s outcome=%s submitted=%d completed=%d parked=%d timed_out=%d | markers "
		"before=%08x middraw=%08x after=%08x ps=%08x | pixels match=%u/%u stale=%u first_bad=(%d,%d) expected=%08x observed=%08x | "
		"texture changed=%u guard_bad=%u | image fnv=%016llx | batch_dwords=%u pipesel rc=%d | rt mocs=%u\n", rc,
		fr->t.err_where != 0 ? fr->t.err_where : "-", fr->t.outcome == PARITY_EU_PASS ? "PASS" : fr->t.outcome == PARITY_EU_HANG ? "HANG" : "ERROR",
		fr->t.submitted, fr->t.completed, fr->t.parked, fr->t.timed_out, fr->marker_before, fr->marker_middraw, fr->marker_after,
		fr->ps_marker, fr->px_match, fr->px_total, fr->px_stale, fr->first_bad_x, fr->first_bad_y, fr->first_bad_expected,
		fr->first_bad_observed, fr->tex_changed_bytes, fr->guard_bad_bytes, (unsigned long long)fr->image_hash, fr->t.batch_dwords,
		fr->t.pipesel_rc, fr->rt_rss_mocs);
	kern_logf("i915: parity LCD-G same backing: object %p | PPGTT 0x%llx.. %u pages mapped, walk ok=%d, first page dma 0x%llx last 0x%llx | "
		"GGTT surf 0x%08llx: PTE first 0x%llx last 0x%llx -> %s\n", (void *)so->obj, (unsigned long long)I915_TEX_FHD_RT_VA,
		fr->rt_pages_mapped, fr->rt_walk_ok, (unsigned long long)fr->rt_first_dma, (unsigned long long)fr->rt_last_dma,
		(unsigned long long)so->surf, (unsigned long long)ggtt_first, (unsigned long long)ggtt_last,
		same_backing ? "SAME PAGES" : "DIFFERENT");
	if (rc != 0 || fr->t.outcome != PARITY_EU_PASS || !same_backing) {
		if (fr->gpu_done || !fr->t.submitted) {
			render_released = parity_fhd_render_release(fr, d->gm, d->vm) == 0;
			if (parity_scanout_unpin(so) == 0)
				(void)parity_scanout_destroy(so);
		} else {
			/* the GPU is not shown to have let go of the target: keep the buffer and the GPU objects */
			parity_scanout_abandon(so);
		}
		lcdb_summary.first_anomaly = "the GPU image is not the expected one (not shown)";
		lcdb_summary.first_anomaly_stage = "render";
		kern_logf("i915: parity LCD-G verdict: FAIL (the GPU draw did not produce the expected image; nothing was shown; "
			"render objects released=%d)\n", render_released);
		return -1;
	}

	/* ---- show THAT object: no CPU write since the draw ---- */
	env.hw = &k->ops;
	env.gm = d->gm;
	env.so = 0;
	env.lcd = &d->edp->lcd;
	env.pipe = 0;
	env.first_frames_ms = 1000u;
	env.window_ms = 12000u;
	env.at_stage = at_stage;
	env.at_stage_ctx = k;
	k->pattern_id = 0u;
	k->window_ms = env.window_ms;
	k->post_before = d->irq->vbl != 0 ? d->irq->vbl->post_enable_calls : 0u;
	k->pre_before = d->irq->vbl != 0 ? d->irq->vbl->pre_disable_calls : 0u;
	kern_logf("i915: parity LCD-G showing the GPU-drawn buffer (surf 0x%08llx, image fnv %016llx)\n",
		(unsigned long long)so->surf, (unsigned long long)fr->image_hash);
	rc = parity_lcd_show_prepared(&env, so, lcdg_verify, 0, &rep);

	log_trace(rep.trace);
	log_observer(&rep.obs);
	log_status("enable-returned", &rep.at_enable);
	log_status("disable-returned", &rep.at_disable);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];

	/* ---- both users done?  display: stop confirmed; GPU: retired and parked -> release in that order ---- */
	if (rep.display_released) {
		render_released = parity_fhd_render_release(fr, d->gm, d->vm) == 0;
		if (render_released && parity_scanout_unpin(so) == 0 && parity_scanout_destroy(so) == 0)
			released = 1;
	}
	kern_logf("i915: parity LCD-G stop / release: display released=%d abandoned=%d | render PTEs cleared %u/%u released=%d | "
		"buffer unpinned+destroyed=%d | image re-checked after the stop: wrong pixels=%u | display pages in use=%u | power refs "
		"held=%d | first anomaly: %s (stage %s)\n", rep.display_released, rep.abandoned, fr->rt_pages_cleared, fr->rt_pages_mapped,
		render_released, released, rep.readback_bad_after, d->gm->display_allocated_pages, held,
		rep.first_anomaly != 0 ? rep.first_anomaly : "none", stage_name(rep.first_anomaly_stage));
	lcdb_summary.pass = rc == 0 && released && held == 0 && k->unresolved_steps == 0u && k->time_faults == 0u;
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.stage = stage_name(rep.stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.retained = parity_lcd_show_retained();
	kern_logf("i915: parity LCD-G verdict: %s (GPU pixels %u/%u, same backing, shown from the GPU-written object, stop %s, released=%d; "
		"the photograph is separate evidence)\n", lcdb_summary.pass ? "PASS" : "FAIL", fr->px_match, fr->px_total,
		rep.display_released ? "confirmed" : "NOT confirmed", released);
	return lcdb_summary.pass ? 0 : -1;
}
'''
save(L + "parity_lcd_kernel.c", k)

b = load(P + "bios.h")
b = rep(b, "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST || PARITY_LCDR_TEST)",
        "#ifndef PARITY_LCDG_TEST" + NL + "#define PARITY_LCDG_TEST 0            /* LCD-G: a GPU-drawn full-HD image shown from the same backing */" + NL + "#endif" + NL +
        "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST)")
save(P + "bios.h", b)
pc = load(P + "probe.c")
pc = rep(pc, "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST) {", "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST) {")
pc = rep(pc, "		lcdb.irq = &irqdev; lcdb.gm = &gtmem; lcdb.ipc_enabled = dprobe.ipc_enabled;",
         "		lcdb.irq = &irqdev; lcdb.gm = &gtmem; lcdb.ipc_enabled = dprobe.ipc_enabled;" + NL +
         "		lcdb.es = &gteng; lcdb.vm = &gtpp; lcdb.uncore_lock = &uncore_lock;")
pc = rep(pc, "			if (PARITY_LCDR_TEST)" + NL + "				(void)parity_lcd_kernel_lcdr_run(&lcdb);",
         """			if (PARITY_LCDG_TEST) {
				/* like the TEX test: the GT's forcewake domains are held around the submission */
				static const int gfwd[5] = { OSDEP_FW_RENDER, OSDEP_FW_GT,
					OSDEP_FW_MEDIA_VDBOX0, OSDEP_FW_MEDIA_VDBOX2, OSDEP_FW_MEDIA_VEBOX0 };
				unsigned gheld = 0u;
				int gfrc = 0;

				while (gheld < 5u && (gfrc = osdep_fw_get(&mmio, gfwd[gheld])) == 0)
					gheld++;
				if (gheld == 5u)
					(void)parity_lcd_kernel_lcdg_run(&lcdb);
				else
					kern_logf("i915: parity LCD-G verdict: FAIL (forcewake rc=%d; nothing submitted)\\n", gfrc);
				while (gheld-- > 0u)
					osdep_fw_put(&mmio, gfwd[gheld]);
			} else if (PARITY_LCDR_TEST)
				(void)parity_lcd_kernel_lcdr_run(&lcdb);""")
save(P + "probe.c", pc)

# ---------------- GPU-free checks
sk = load(L + "lcd_show_ktest.c")
sk = rep(sk, '#include "../dp/dp_fixture_latitude5330.h"', '#include "../dp/dp_fixture_latitude5330.h"' + NL + '#include "../eu_test.h"' + NL + '#include "../../draw_fixture.h"')
sk = rep(sk, "	/* ---- B: early failure: nothing is shown, everything is given back, the first anomaly is the training ---- */",
         r'''	/* ---- E-118 G-FIXTURE: the full-HD draw's generated state, VA layout and verifier ---- */
	{
		static uint32_t state[1024], batch[1024];
		const struct parity_fhd_va *lay = 0;
		const uint32_t *rss = drv_i915_tex_fixture_fhd_rt_rss();
		unsigned nl = 0u, bd, di, rect_ok = 0u;

		check(parity_fhd_va_layout(&lay, &nl) == 0 && nl == 4u && lay[3].va == I915_TEX_FHD_RT_VA && lay[3].len >= I915_TEX_FHD_RT_BYTES,
			"lcd-g: G-VA the draw's GPU VA ranges (state, batch, texture, 8 MB render target) are aligned and do not overlap");
		check(drv_i915_tex_fixture_fhd_same_texture_state() && rss[2] == 0x0437077fu && rss[3] == I915_TEX_FHD_PITCH - 1u &&
			((rss[0] >> 18) & 0x1ffu) == 0xc0u && ((rss[1] >> 24) & 0x7fu) == drv_i915_draw_fixture_mocs() &&
			drv_i915_tex_fixture_fhd_ps_bytes() <= 1024u,
			"lcd-g: G-STATE isl render target 1920x1080 B8G8R8A8 pitch 7680 MOCS uncached; texture / sampler / packets = the T1 ones");
		drv_i915_tex_fixture_fhd_write_state(state, I915_TEX_FHD_RT_VA, I915_TEX_FIXTURE_TEX_VA, drv_i915_draw_fixture_mocs());
		bd = drv_i915_tex_fixture_fhd_build_batch(batch, 1024u, 0x100400000ull, drv_i915_draw_fixture_mocs());
		for (di = 0u; di + 1u < bd; di++)
			if (batch[di + 1u] == 0x0437077fu && batch[di] == 0u)
				rect_ok++;
		check(bd > 0u && bd < 1024u && rect_ok == 1u && state[2048u / 4u] == 0x44f00000u && state[2048u / 4u + 1u] == 0x44870000u &&
			state[64u / 4u + 8u] == (uint32_t)I915_TEX_FHD_RT_VA && state[64u / 4u + 9u] == (uint32_t)(I915_TEX_FHD_RT_VA >> 32),
			"lcd-g: G-BATCH drawing rectangle 1919x1079, RECTLIST (1920,1080), render target state names the 8 MB range");
		{
			struct parity_gt_object *img = parity_gt_object_create(&gm, I915_TEX_FHD_RT_BYTES);
			static struct parity_fhd_render fx;
			static uint8_t pat[I915_TEX_FIXTURE_TEX_BYTES];
			uint32_t x, y, bad0 = 1u, bad1 = 0u;

			if (img != 0) {
				uint32_t *px = (uint32_t *)img->cpu;

				drv_i915_tex_fixture_pattern(pat, 0u);
				for (y = 0u; y < I915_TEX_FHD_HEIGHT; y++)
					for (x = 0u; x < I915_TEX_FHD_WIDTH; x++)
						px[y * 1920u + x] = drv_i915_tex_fixture_expected_pixel(pat, 4u * (x / 240u), 4u * (y / 135u));
				bad0 = parity_fhd_render_verify(&fx, px, I915_TEX_FHD_PITCH);
				px[1079u * 1920u + 1919u] ^= 0x00010000u;
				px[0] ^= 0x1u;
				bad1 = parity_fhd_render_verify(&fx, px, I915_TEX_FHD_PITCH);
				parity_gt_object_destroy(&gm, img);
			}
			check(img != 0 && bad0 == 0u && bad1 == 2u,
				"lcd-g: G-VERIFY all 2,073,600 pixels against texel (x/240, y/135); two wrong pixels are counted as two");
		}
	}

	/* ---- E-118 G-PREPARED: show a buffer the test owns; the display part never creates, fills or frees it ---- */
	{
		static struct parity_scanout so_p;
		unsigned live;
		int rc2;

		rc = bring_up(&so_p);
		rc2 = parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so_p);
		check(rc == 0 && rc2 == 0 && parity_lcd_show_prepared(&show, &so_p, 0, 0, &rep) == -EINVAL && so_p.state == PARITY_SCANOUT_ALLOCATED,
			"lcd-g: G-PREPARED an unpinned buffer is refused; the buffer is left as it was");
		(void)parity_scanout_pin(&so_p, "owner");
		(void)parity_lcd_pattern_fill(so_p.cpu, so_p.pitch, so_p.width, so_p.height, 113u);
		parity_scanout_publish(&so_p);
		live = gm.objects_live;
		rc2 = parity_lcd_show_prepared(&show, &so_p, 0, 0, &rep);
		check(rc2 == 0 && rep.pass && rep.display_released && so_p.state == PARITY_SCANOUT_PINNED && so_p.pin_owner != 0 &&
			gm.objects_live == live && lcd.plane_surf_at_arm == (uint32_t)so_p.surf &&
			parity_lcd_pattern_verify(so_p.cpu, so_p.pitch, so_p.width, so_p.height, 113u, 0, 0) == 0u,
			"lcd-g: G-PREPARED shown and stopped; the buffer comes back PINNED to its owner with its pixels untouched");
		check(parity_scanout_unpin(&so_p) == 0 && parity_scanout_destroy(&so_p) == 0 && edp_released(),
			"lcd-g: G-PREPARED the OWNER releases it afterwards");
	}

	/* ---- B: early failure: nothing is shown, everything is given back, the first anomaly is the training ---- */''')
save(L + "lcd_show_ktest.c", sk)
print("done")

/*
 * WS031 Linux-parity -- GPU-free kernel checks of LCD-B's production body (parity_lcd_show.c): the SAME modeset
 * commits and the SAME scanout management as on the real GPU, with real kernel DMA allocations, a sentinel-filled
 * PTE table standing in for the GGTT, real mutexes behind the DPLL / backlight locks, and the register / sink
 * models as the display.  Three runs: the picture comes up and everything is given back; an early failure
 * (the sink never reports clock recovery) gives everything back without the buffer ever being shown; a pipe that
 * does not stop leaves the buffer ABANDONED -- pages, DMA mapping, PTEs, pin and owner all stay, releases are
 * refused, and the outer teardown (parity_gt_mem_fini) leaves it alone.  zedBSD project code.
 */
#include "../../internal.h"
#include <kern/klog.h>
#include <kern/lock.h>
#include <string.h>
#include <errno.h>
#include "../gt_mem.h"
#include "scanout.h"
#include "lcd_pattern.h"
#include "parity_lcd_show.h"
#include "lcd_fake_hw.h"
#include "lcd_show_ktest.h"
#include "../dp/parity_edp.h"
#include "../dp/dp_fake_hw.h"
#include "../dp/dp_fixture_latitude5330.h"
#include "../eu_test.h"
#include "../../draw_fixture.h"

#define TABLE_ENTRIES 16384u
#define SENTINEL      0x5a5a5a5a5a5a5a5aull
#define SCRATCH_PTE   0x00000000dead0001ull
#define PATTERN_110_FNV 0xce63f20b23f91f85ull

static uint64_t table[TABLE_ENTRIES];
static struct parity_gt_mem gm;
static struct parity_scanout so_a, so_b, so_c;
static struct dp_fake_hw dpf;
static struct parity_dp_env env;
static struct parity_edp_result res;
static struct lcd_fake_hw lcd;
static struct parity_lcd_state st;
static struct parity_lcd_show_env show;
static struct parity_lcd_show_report rep;
static struct mutex real_locks[2];
static unsigned real_lock_ops;
static void (*model_lock)(void *ctx, int which, int take);

/* the model keeps its own accounting; the lock itself is a real kernel mutex */
static void locked(void *ctx, int which, int take)
{
	if (which >= 0 && which <= 1) {
		if (take)
			mutex_lock(&real_locks[which]);
		real_lock_ops++;
	}
	model_lock(ctx, which, take);
	if (which >= 0 && which <= 1 && !take)
		mutex_unlock(&real_locks[which]);
}

static int bring_up(struct parity_scanout *so)
{
	struct parity_edp_config c;
	int rc;

	parity_lcd_dplls_reset();       /* a fresh device for this case: the shared DPLLs and the DBUF state start over */
	parity_lcd_dbuf_forget();

	memset(&c, 0, sizeof(c));
	c.rawclk_khz = 19200;
	c.t1_t3 = 2000; c.t8 = 800; c.t9 = 2000; c.t10 = 1100; c.t11_t12 = 5000;
	c.log_level = -1;
	dp_fake_init(&dpf, dp_fixture_dpcd_000, dp_fixture_dpcd_100, dp_fixture_dpcd_700, dp_fixture_edid, 128u);
	dp_fake_bind_env(&dpf, &env);
	rc = parity_edp_begin(&env, &c, &res);
	if (rc == 0)
		rc = parity_edp_init_late(&c, &res);
	if (rc == 0)
		rc = parity_lcd_compute(res.edid, res.dpcd, res.edp_dpcd, 18, 38400, &st);
	if (rc != 0)
		return rc;
	lcd_fake_init(&lcd, &dpf, 0, 0, 0, st.mode.vtotal);
	lcd.dbuf_size = 4096u;
	model_lock = lcd.ops.lock;
	lcd.ops.lock = locked;

	memset(&show, 0, sizeof(show));
	show.hw = &lcd.ops;
	show.gm = &gm;
	show.so = so;
	show.lcd = &st;
	show.pipe = 0;
	show.pattern_id = 110u;
	show.pattern_fnv = PATTERN_110_FNV;
	show.first_frames_ms = 1000u;
	show.window_ms = 2000u;
	memcpy(show.cfg.dpcd, res.dpcd, sizeof(show.cfg.dpcd));
	memcpy(show.cfg.edp_dpcd, res.edp_dpcd, sizeof(show.cfg.edp_dpcd));
	{
		static const uint16_t lat[8] = { 3, 54, 83, 102, 147, 147, 144, 144 };

		memcpy(show.cfg.wm_latency, lat, sizeof(lat));
	}
	show.cfg.wm_num_levels = 6; show.cfg.wm_ipc_enabled = 1; show.cfg.sagv_block_time_us = 35;
	show.cfg.dbuf_size = 4096u; show.cfg.dbuf_slice_mask = 0x0f; show.cfg.dbuf_enabled_slices = 0x01;
	show.cfg.dmc_fw_mask = 1u << 1;
	show.cfg.vbt_backlight_present = 1; show.cfg.vbt_backlight_pwm_freq_hz = 200; show.cfg.vbt_backlight_min_brightness = 6;
	show.cfg.rawclk_khz = 19200u;
	show.cfg.cdclk_khz = 179200u; show.cfg.cdclk_vco_khz = 537600u; show.cfg.cdclk_ref_khz = 38400u;
	show.cfg.cdclk_bypass_khz = 19200u; show.cfg.cdclk_max_khz = 652800u; show.cfg.qgv_allowed_bw = 11707u;
	return 0;
}

static int edp_released(void)
{
	int end = parity_edp_end(&res);

	dp_fake_flush_async(&dpf);
	return end == 0 && lcd.lock_held[0] == 0 && lcd.lock_held[1] == 0 && dpf.refs_core == 0 && dpf.refs_aux == 0 &&
		(dpf.pp_control & 9u) == 0u;
}

static unsigned live_ptes(unsigned first, unsigned pages)
{
	unsigned i, n = 0u;

	for (i = 0u; i < pages; i++)
		if ((table[first + i] & 1ull) != 0u && table[first + i] != SCRATCH_PTE && table[first + i] != SENTINEL)
			n++;
	return n;
}

static int fail_in_window(void *ctx, struct parity_lcd_observer *o)
{
	(void)ctx;
	(void)o;
	return -5;
}

static void stick_the_pipe(void *ctx, int stage)
{
	(void)ctx;
	if (stage == PARITY_LCD_SHOW_WINDOW_DONE)
		lcd.fault_pipe_stuck_on = 1;
}

void parity_lcd_show_ktest(parity_lcd_show_ktest_check check, struct drv_dma_device *dma, uint64_t dma_mask)
{
	unsigned i, first, pages, objects_before, display_before;
	int rc;

	for (i = 0u; i < TABLE_ENTRIES; i++)
		table[i] = SENTINEL;
	(void)mutex_init(&real_locks[0], LOCK_RANK_DEVICE, "lcd-show-ktest-dpll");
	(void)mutex_init(&real_locks[1], LOCK_RANK_DEVICE, "lcd-show-ktest-backlight");
	rc = parity_gt_mem_init(&gm, dma, dma_mask, table, TABLE_ENTRIES, SCRATCH_PTE, 0);
	if (rc != 0) {
		check(0, "lcd-show: SETUP gt_mem");
		return;
	}
	objects_before = gm.objects_live;

	/* ---- A: the picture comes up, stays for the window, and everything is given back ---- */
	rc = bring_up(&so_a);
	real_lock_ops = 0u;
	rc = rc == 0 ? parity_lcd_show_run(&show, &rep) : rc;
	check(rc == 0 && rep.pass && rep.stage == PARITY_LCD_SHOW_RELEASED && rep.first_anomaly == 0 && rep.released && !rep.abandoned,
		"lcd-show: A-PASS buffer -> prepare -> enable commit -> frames -> window -> disable commit -> stop confirmed -> released");
	check(rep.pattern_hash == PATTERN_110_FNV && rep.readback_bad_before == 0u && rep.readback_bad_after == 0u &&
		lcd.plane_surf_at_arm == (uint32_t)rep.surf && rep.surf != 0u && lcd.plane_arms == 1u,
		"lcd-show: A-BUFFER the plane was armed with THIS object's GGTT address; the picture is the pinned pattern before and after");
	check(rep.at_enable.cr_ok && rep.at_enable.eq_ok && rep.at_window_end.cr_ok && rep.first_frames_rc == 0 && rep.steady_rc == 0 &&
		rep.steady_rounds == 4u && rep.steady_frame_last > rep.frame_first && rep.stopped_rc == 0 &&
		(rep.transconf_after_stop & 0xc0000000u) == 0u,
		"lcd-show: A-OBSERVED the sink's link status, the advancing frame counter during the window, the standing counter after the stop");
	check(rep.obs.seen_transition == 0u && rep.obs.seen_steady == 0u && rep.obs.vblank_unmasked_seen == 0 && rep.obs.n >= 12u &&
		rep.obs.dropped == 0u && rep.trace->steps == 0u && rep.trace->dropped == 0u,
		"lcd-show: A-STATUS underrun status sampled at every point of both commits and in the window; vblank stayed masked; no unresolved step");
	check(lcd_fake_violations(&lcd) == 0u && lcd_fake_power_refs_total(&lcd) == 0 && lcd.dbuf_enabled == 0x01u &&
		rep.at_disable.crtc_domains_held == 0u && !rep.at_disable.dc_off_held && real_lock_ops >= 4u && edp_released(),
		"lcd-show: A-RETURNED no model violation; power references, DBUF slices, real mutexes and the eDP all back to the start state");
	check(gm.objects_live == objects_before && gm.display_allocated_pages == 0u && so_a.state == PARITY_SCANOUT_NONE,
		"lcd-show: A-MEMORY the scanout object and its GGTT range are gone");

	/* ---- A2: the same body again in the same lifetime (another picture), then an in-window hook that fails ---- */
	{
		static struct parity_scanout so_a2;
		int rc2;

		rc = bring_up(&so_a2);
		show.pattern_id = 111u;
		show.pattern_fnv = 0u;
		rc2 = rc == 0 ? parity_lcd_show_run(&show, &rep) : rc;
		check(rc2 == 0 && rep.pass && rep.released && gm.objects_live == objects_before && gm.display_allocated_pages == 0u &&
			edp_released(),
			"lcd-show: A2-AGAIN a second show / stop in the same lifetime with another picture passes and gives everything back");
		rc = bring_up(&so_a2);
		show.in_window = fail_in_window;
		rc2 = rc == 0 ? parity_lcd_show_run(&show, &rep) : rc;
		check(rc2 != 0 && rep.window_hook_rc == -5 && rep.first_anomaly != 0 && rep.first_anomaly_stage == PARITY_LCD_SHOW_PICTURE_UP &&
			rep.steady_rounds == 0u && rep.released && lcd_fake_power_refs_total(&lcd) == 0 && edp_released(),
			"lcd-show: A2-HOOK a failing in-window test is the first anomaly; the stop path still runs and gives everything back");
		show.in_window = 0;
	}

	/* ---- E-118 G-FIXTURE: the full-HD draw's generated state, VA layout and verifier ---- */
	{
		static uint32_t state[1024], batch[1024];
		const struct parity_fhd_va *lay = 0;
		const uint32_t *rss = drv_i915_tex_fixture_fhd_rt_rss();
		unsigned nl = 0u, bd, di, rect_ok = 0u;

		check(parity_fhd_va_layout(&lay, &nl) == 0 && nl == 5u && lay[3].va == I915_TEX_FHD_RT_VA && lay[3].len >= I915_TEX_FHD_RT_BYTES &&
		      lay[4].va == I915_TEX_FHD_RT_B_VA && lay[4].len >= I915_TEX_FHD_RT_BYTES,
			"lcd-g: G-VA the draw's GPU VA ranges (state, batch, texture, two 8 MB render targets) are aligned and do not overlap");
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

		/* the display part refuses at its check phase: it never acquired the buffer, which stays with its owner */
		rc = bring_up(&so_p);
		rc2 = parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so_p);
		(void)parity_scanout_pin(&so_p, "owner");
		show.cfg.qgv_allowed_bw = 0u;                   /* the check phase refuses an unknown memory bandwidth */
		live = gm.objects_live;
		{
			unsigned arms = lcd.plane_arms, writes = lcd.nregs;

			rc2 = parity_lcd_show_prepared(&show, &so_p, 0, 0, &rep);
			check(rc2 != 0 && rep.display_acquired == 0 && rep.display_released == 0 && !rep.abandoned && rep.prepare_rc != 0 &&
				so_p.state == PARITY_SCANOUT_PINNED && lcd.plane_arms == arms && lcd.nregs == writes && !parity_lcd_show_retained() &&
				gm.objects_live == live,
				"lcd-g: G-NOTSTARTED prepare refused: display_acquired=0 (not 'stop confirmed'), no register written, nothing retained");
		}
		check(parity_scanout_unpin(&so_p) == 0 && parity_scanout_destroy(&so_p) == 0 && edp_released(),
			"lcd-g: G-NOTSTARTED the owner reclaims the buffer at once, and the next run is not blocked");
	}

	/* ---- B: early failure: nothing is shown, everything is given back, the first anomaly is the training ---- */
	rc = bring_up(&so_b);
	lcd.fault_cr_never = 1;
	rc = rc == 0 ? parity_lcd_show_run(&show, &rep) : 0;
	check(rc != 0 && !rep.pass && rep.enable_rc == PARITY_LCD_MS_ERRORS && rep.first_anomaly != 0 &&
		rep.first_anomaly_stage == PARITY_LCD_SHOW_PREPARED && rep.first_error_trace_at >= 0 && lcd.plane_arms == 0u,
		"lcd-show: B-FAIL the sink never reports clock recovery: no success, the first anomaly is that one, the plane was never armed");
	check(rep.disable_rc == PARITY_LCD_MS_OK && rep.cleanup_errors == 0u && rep.stopped_rc == 0 && rep.released && !rep.abandoned &&
		lcd_fake_power_refs_total(&lcd) == 0 && lcd.dbuf_enabled == 0x01u && gm.display_allocated_pages == 0u && edp_released(),
		"lcd-show: B-CLEANUP the reference's disable path ran; its result is recorded apart and did not replace the first anomaly");

	/* ---- C: the pipe does not stop: the buffer is abandoned and nothing later frees it ---- */
	rc = bring_up(&so_c);
	show.at_stage = stick_the_pipe;
	display_before = gm.display_allocated_pages;
	rc = rc == 0 ? parity_lcd_show_run(&show, &rep) : 0;
	first = so_c.obj != 0 ? so_c.obj->ggtt_page : 0u;
	pages = so_c.obj != 0 ? so_c.obj->pages : 0u;
	check(rc != 0 && rep.stage == PARITY_LCD_SHOW_ABANDONED && rep.abandoned && !rep.released &&
		rep.disable_rc == PARITY_LCD_MS_ERRORS && rep.cleanup_errors >= 1u && rep.cleanup_first_error_trace_at > 0 &&
		rep.first_anomaly_stage == PARITY_LCD_SHOW_WINDOW_DONE,
		"lcd-show: C-STUCK a pipe that does not stop: the run ends ABANDONED; the anomaly is recorded at the disable, with the cleanup's own error index");
	check(so_c.state == PARITY_SCANOUT_ABANDONED && so_c.obj != 0 && so_c.obj->keep == 1 && so_c.obj->in_use && so_c.obj->bound &&
		so_c.pin_owner != 0 && so_c.surf == rep.surf && pages == 2025u && live_ptes(first, pages) == pages &&
		gm.display_allocated_pages > display_before &&
		parity_lcd_pattern_verify(so_c.cpu, so_c.pitch, so_c.width, so_c.height, 110u, 0, 0) == 0u,
		"lcd-show: C-KEPT backing pages (still holding the picture), CPU mapping, every PTE, the pin and its owner are all still there");
	check(parity_scanout_unpin(&so_c) == -EBUSY && parity_scanout_destroy(&so_c) == -EBUSY && so_c.refused_unpin == 1u &&
		so_c.refused_destroy == 1u && live_ptes(first, pages) == pages,
		"lcd-show: C-REFUSED unpin and destroy are refused for an abandoned buffer");
	check(rep.at_disable.stop_unconfirmed == 1 && rep.at_disable.dc_off_held == 1 && rep.at_disable.crtc_domains_held == 4u &&
		lcd.dbuf_enabled == 0x0fu && lcd.power_dropped_with_pipe_on == 0u && lcd.dbuf_shrunk_under_plane == 0u,
		"lcd-show: C-HELD DC_OFF, the crtc's power domains and the DBUF slices were not taken from under the pipe");
	show.at_stage = 0;
	show.so = &so_c;
	check(parity_lcd_show_run(&show, &rep) == -EBUSY, "lcd-show: C-NEXT a further run on the abandoned storage is refused before anything is touched");
	{
		unsigned live = gm.objects_live, disp = gm.display_allocated_pages;
		uint32_t obj_page = so_c.obj->ggtt_page;
		struct parity_gt_object *obj = so_c.obj;

		show.so = &so_b;                                /* fresh (NONE) storage: still refused by the device latch */
		check(parity_lcd_show_run(&show, &rep) == -EBUSY && parity_lcd_show_retained() == 1 && gm.objects_live == live &&
			gm.display_allocated_pages == disp && so_b.state == PARITY_SCANOUT_NONE,
			"lcd-show: C-LATCH a re-call with fresh storage is refused before anything is allocated; the latch stays set");
		show.so = &so_c;
		check(parity_scanout_create(&gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &so_c) == -EBUSY &&
			so_c.state == PARITY_SCANOUT_ABANDONED && so_c.obj == obj && so_c.pin_owner != 0,
			"lcd-show: C-CREATE create on an abandoned object is refused and leaves its record untouched");
		parity_gt_object_destroy(&gm, obj);
		parity_gt_display_unbind(&gm, obj);
		check(obj->in_use && obj->bound && obj->ggtt_page == obj_page && gm.keep_refusals == 2u && live_ptes(first, pages) == pages,
			"lcd-show: C-BELOW the object layer below the scanout wrapper refuses to destroy / unbind the kept object too");
	}
	(void)parity_edp_end(&res);
	dp_fake_flush_async(&dpf);

	/* the OUTER teardown: everything else goes, the abandoned buffer stays mapped */
	parity_gt_mem_fini(&gm);
	check(gm.kept_objects == 1u && live_ptes(first, pages) == pages && so_c.obj->in_use &&
		parity_lcd_pattern_verify(so_c.cpu, so_c.pitch, so_c.width, so_c.height, 110u, 0, 0) == 0u,
		"lcd-show: C-TEARDOWN parity_gt_mem_fini() leaves the abandoned buffer alone: PTEs live, pages readable, object kept");
	check(parity_lcd_show_retained() == 1 && parity_lcd_show_discard_model(&show) == 0 && parity_lcd_show_retained() == 0 &&
		parity_lcd_modeset_retained() == 0,
		"lcd-show: C-DISCARD the latch outlives the teardown; only discarding the model run clears it");
}

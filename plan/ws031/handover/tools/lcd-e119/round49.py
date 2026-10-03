#!/usr/bin/env python3
"""WS031 E-119 round 49: the flip ops on the real device and the LCD-C mode (-DPARITY_LCDC_TEST=1): one modeset,
buffer A shown, flips B -> A -> B -> A through the reference update, each completed by the pipe's vblank event and the
live surface, then the stop and the release of both buffers.  usage: round49.py <repo root>"""
import sys
NL = chr(10)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

k = load(L + "parity_lcd_kernel.c")
k = rep(k, "static void bind_ops(struct lcd_kernel *k)", r"""/* ---- the synchronous update's ops (pipe vblank reference, sleep until the next vblank, the event) ---- */
static uint32_t k_frame(void *ctx)
{
	struct lcd_kernel *k = ctx;

	return osdep_mmio_read32(k->d->mmio, parity_lcd_reg_by_name("PIPE_FRMCOUNT_G4X"));
}

static int k_vblank_get(void *ctx, int pipe)
{
	struct lcd_kernel *k = ctx;

	return parity_drm_vblank_get(k->d->irq, (unsigned)pipe);
}

static void k_vblank_put(void *ctx, int pipe)
{
	struct lcd_kernel *k = ctx;

	parity_drm_vblank_put(k->d->irq, (unsigned)pipe);
}

/* schedule_timeout() on the pipe's vblank wait queue: until the next vblank interrupt of the pipe or the timeout */
static long k_vblank_sleep(void *ctx, int pipe, long ticks)
{
	struct lcd_kernel *k = ctx;
	int rc = parity_wait_vblank(k->d->irq, (unsigned)pipe, 1u, (unsigned)ticks * 10u, k_frame, k, 0);

	k->vblank_sleeps++;
	if (rc == -EIO)
		parity_lcd_backend_fault("time base fault while waiting for a vblank\n");
	return rc == 0 ? (ticks > 1 ? ticks - 1 : 1) : 0;
}

static void k_irq_off(void *ctx)
{
	struct lcd_kernel *k = ctx;

	k->irq_was_on = kern_irq_disable();
}

static void k_irq_on(void *ctx)
{
	struct lcd_kernel *k = ctx;

	if (k->irq_was_on)
		kern_irq_enable();
}

/* drm_crtc_arm_vblank_event(): completes at the pipe's next vblank (the reference took the vblank reference) */
static void k_arm_event(void *ctx, int pipe)
{
	struct lcd_kernel *k = ctx;

	k->event_armed = 1;
	k->event_pipe = pipe;
	k->event_frame = k_frame(k);
}

static int k_wait_event(void *ctx, int pipe, unsigned timeout_ms)
{
	struct lcd_kernel *k = ctx;
	int rc;

	if (!k->event_armed || pipe != k->event_pipe)
		return -22;
	/* a NEW vblank interrupt of this pipe after the wait began, with the frame counter past the arm's frame */
	rc = parity_wait_vblank(k->d->irq, (unsigned)pipe, 1u, timeout_ms, k_frame, k, 0);
	if (rc == 0 && k_frame(k) == k->event_frame)
		rc = -110;
	if (rc == 0)
		k->event_armed = 0;
	return rc == 0 ? 0 : rc == -EIO ? -5 : -110;
}

static void bind_ops(struct lcd_kernel *k)""")
k = rep(k, "	k->ops.debug = k_debug;", "	k->ops.debug = k_debug;" + NL + "	k->ops.vblank_get = k_vblank_get;" + NL + "	k->ops.vblank_put = k_vblank_put;" + NL +
        "	k->ops.vblank_sleep = k_vblank_sleep;" + NL + "	k->ops.irq_off = k_irq_off;" + NL + "	k->ops.irq_on = k_irq_on;" + NL +
        "	k->ops.arm_event = k_arm_event;" + NL + "	k->ops.wait_event = k_wait_event;")
k = rep(k, "	uint32_t bl_freq0;", "	uint32_t bl_freq0;" + NL + "	int irq_was_on, event_armed, event_pipe; uint32_t event_frame; unsigned vblank_sleeps;" + NL +
        "	struct parity_scanout *flip_a, *flip_b; unsigned flips_done;")
k = rep(k, '#include "../gt_tlb.h"', '#include "../gt_tlb.h"' + NL + '#include <kern/irq.h>' + NL + '#include "lcd_pattern.h"')
k = k.rstrip(NL) + NL + r'''
/* ---------------- LCD-C (-DPARITY_LCDC_TEST=1): the synchronous flip between two CPU-prepared buffers ---------------- */

#define LCDC_PATTERN_A 121u
#define LCDC_PATTERN_B 122u
static struct parity_scanout lcdc_a, lcdc_b;

static uint32_t lcdc_verify_a(void *ctx, const struct parity_scanout *so)
{
	(void)ctx;
	parity_gt_clflush(so->cpu, so->size);
	return parity_lcd_pattern_verify(so->cpu, so->pitch, so->width, so->height, LCDC_PATTERN_A, 0, 0);
}

static int lcdc_flips(void *ctx, struct parity_lcd_observer *o)
{
	struct lcd_kernel *k = ctx;
	struct parity_scanout *seq[4] = { k->flip_b, k->flip_a, k->flip_b, k->flip_a };
	struct parity_lcd_flip_result fr;
	unsigned i;
	int rc;

	(void)o;
	step_sleep(k, 3000u);                           /* the first picture (A) for the camera */
	for (i = 0u; i < 4u; i++) {
		struct parity_scanout *to = seq[i], *from = to == k->flip_a ? k->flip_b : k->flip_a;

		if (to->state == PARITY_SCANOUT_PINNED && parity_scanout_begin(to) != 0)
			return -22;
		rc = parity_lcd_modeset_flip((uint32_t)to->surf, &fr);
		kern_logf("i915: parity LCD-C flip %u gen %u: surf 0x%08x -> 0x%08x | live 0x%08x -> 0x%08x | frame %u -> %u | event rc=%d | "
			"update errors %d | vblank sleeps %u | result %s\n", i + 1u, fr.gen, fr.old_surf, fr.new_surf, fr.live_before, fr.live_after,
			fr.frame_before, fr.frame_after, fr.event_rc, fr.update_errors, k->vblank_sleeps,
			fr.result == PARITY_LCD_FLIP_DONE ? "DONE" : fr.result == PARITY_LCD_FLIP_NOT_LATCHED ? "NOT-LATCHED" :
			fr.result == PARITY_LCD_FLIP_TIMEOUT ? "TIMEOUT" : "REFUSED");
		if (rc != PARITY_LCD_MS_OK || fr.result != PARITY_LCD_FLIP_DONE)
			return -5;                      /* both buffers stay in use; the stop path decides */
		/* the old buffer is no longer read by the display: it goes back to its owner */
		parity_scanout_end(from);
		k->flips_done++;
		kern_logf("i915: parity LCD-C shown %u: pattern %u (buffer %c, surf 0x%08x) -- take the photograph\n", i + 1u,
			to == k->flip_a ? LCDC_PATTERN_A : LCDC_PATTERN_B, to == k->flip_a ? 'A' : 'B', (uint32_t)to->surf);
		step_sleep(k, 3000u);
	}
	return 0;
}

int parity_lcd_kernel_lcdc_run(const struct parity_lcd_kernel_deps *d)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	uint32_t bad_a = 0u, bad_b = 0u;
	int rc, i, held = 0, released = 0;

	memset(&lcdb_summary, 0, sizeof(lcdb_summary));
	lcdb_summary.ran = 1;
	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || d->irq == 0) {
		kern_logf("i915: parity LCD-C verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained() || lcdc_a.state != PARITY_SCANOUT_NONE ||
	    lcdc_b.state != PARITY_SCANOUT_NONE) {
		lcdb_summary.retained = 1;
		kern_logf("i915: parity LCD-C verdict: FAIL (refused before any initialisation: retained resources)\n");
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
		kern_logf("i915: parity LCD-C verdict: FAIL (preflight: nothing was written)\n");
		return -1;
	}
	/* two buffers: separate backing, separate GGTT placement (guards and alignment each) */
	rc = parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);
	if (rc != 0 && rc != -EBUSY)
		return -1;
	rc = parity_scanout_create(d->gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &lcdc_a);
	rc = rc == 0 ? parity_scanout_pin(&lcdc_a, "lcd-c A") : rc;
	rc = rc == 0 ? parity_scanout_create(d->gm, 1920u, 1080u, PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &lcdc_b) : rc;
	rc = rc == 0 ? parity_scanout_pin(&lcdc_b, "lcd-c B") : rc;
	if (rc != 0) {
		kern_logf("i915: parity LCD-C verdict: FAIL (buffers rc=%d)\n", rc);
		return -1;
	}
	(void)parity_lcd_pattern_fill(lcdc_a.cpu, lcdc_a.pitch, lcdc_a.width, lcdc_a.height, LCDC_PATTERN_A);
	(void)parity_lcd_pattern_fill(lcdc_b.cpu, lcdc_b.pitch, lcdc_b.width, lcdc_b.height, LCDC_PATTERN_B);
	parity_scanout_publish(&lcdc_a);
	parity_scanout_publish(&lcdc_b);
	kern_logf("i915: parity LCD-C buffers: A surf 0x%08llx ggtt page %u (+%u, guard %u) | B surf 0x%08llx ggtt page %u (+%u) | "
		"overlap=%d\n", (unsigned long long)lcdc_a.surf, lcdc_a.obj->ggtt_page, lcdc_a.obj->pages, lcdc_a.guard_pages,
		(unsigned long long)lcdc_b.surf, lcdc_b.obj->ggtt_page, lcdc_b.obj->pages,
		!(lcdc_a.obj->ggtt_page + lcdc_a.obj->pages + lcdc_a.guard_pages <= lcdc_b.obj->ggtt_page - lcdc_b.guard_pages ||
		  lcdc_b.obj->ggtt_page + lcdc_b.obj->pages + lcdc_b.guard_pages <= lcdc_a.obj->ggtt_page - lcdc_a.guard_pages));
	k->flip_a = &lcdc_a;
	k->flip_b = &lcdc_b;

	env.hw = &k->ops;
	env.gm = d->gm;
	env.lcd = &d->edp->lcd;
	env.pipe = 0;
	env.first_frames_ms = 1000u;
	env.window_ms = 1000u;
	env.in_window = lcdc_flips;
	env.in_window_ctx = k;
	env.at_stage = at_stage;
	env.at_stage_ctx = k;
	k->pattern_id = LCDC_PATTERN_A;
	k->window_ms = env.window_ms;
	rc = parity_lcd_show_prepared(&env, &lcdc_a, lcdc_verify_a, 0, &rep);
	log_trace(rep.trace);
	log_observer(&rep.obs);
	for (i = 0; i < (int)POWER_DOMAIN_NUM; i++)
		held += k->power_refs[i];

	if (rep.display_released || !rep.display_acquired) {
		parity_scanout_end(&lcdc_b);            /* the display provably reads neither buffer */
		bad_b = parity_lcd_pattern_verify(lcdc_b.cpu, lcdc_b.pitch, lcdc_b.width, lcdc_b.height, LCDC_PATTERN_B, 0, 0);
		bad_a = rep.readback_bad_after;
		released = parity_scanout_unpin(&lcdc_a) == 0 && parity_scanout_destroy(&lcdc_a) == 0 &&
			parity_scanout_unpin(&lcdc_b) == 0 && parity_scanout_destroy(&lcdc_b) == 0;
	} else {
		if (lcdc_b.state >= PARITY_SCANOUT_PINNED && lcdc_b.state != PARITY_SCANOUT_ABANDONED)
			parity_scanout_abandon(&lcdc_b);
	}
	lcdb_summary.pass = rc == 0 && k->flips_done == 4u && released && held == 0 && bad_a == 0u && bad_b == 0u &&
		k->unresolved_steps == 0u && k->time_faults == 0u;
	lcdb_summary.first_anomaly = rep.first_anomaly;
	lcdb_summary.first_anomaly_stage = stage_name(rep.first_anomaly_stage);
	lcdb_summary.cleanup_rc = rep.disable_rc;
	lcdb_summary.retained = parity_lcd_show_retained();
	kern_logf("i915: parity LCD-C verdict: %s (flips done %u/4, one modeset, stop %s, both buffers released=%d, pixels after: A bad %u "
		"B bad %u, power refs held %d, first anomaly: %s)\n", lcdb_summary.pass ? "PASS" : "FAIL", k->flips_done,
		rep.display_released ? "confirmed" : "NOT confirmed", released, bad_a, bad_b, held,
		rep.first_anomaly != 0 ? rep.first_anomaly : "none");
	return lcdb_summary.pass ? 0 : -1;
}
'''
save(L + "parity_lcd_kernel.c", k)
kh = load(L + "parity_lcd_kernel.h")
kh = rep(kh, "int parity_lcd_kernel_lcdg_run(const struct parity_lcd_kernel_deps *d);",
         "int parity_lcd_kernel_lcdg_run(const struct parity_lcd_kernel_deps *d);" + NL +
         "/* LCD-C (-DPARITY_LCDC_TEST=1): one modeset, synchronous flips A -> B -> A -> B -> A, stop, both released */" + NL +
         "int parity_lcd_kernel_lcdc_run(const struct parity_lcd_kernel_deps *d);")
save(L + "parity_lcd_kernel.h", kh)
b = load(P + "bios.h")
b = rep(b, "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST)",
        "#ifndef PARITY_LCDC_TEST" + NL + "#define PARITY_LCDC_TEST 0            /* LCD-C: synchronous flips between two buffers */" + NL + "#endif" + NL +
        "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST)")
save(P + "bios.h", b)
pc = load(P + "probe.c")
pc = rep(pc, "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST) {", "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST || PARITY_LCDG_TEST || PARITY_LCDC_TEST) {")
pc = rep(pc, "			} else if (PARITY_LCDR_TEST)", "			} else if (PARITY_LCDC_TEST) {" + NL + "				(void)parity_lcd_kernel_lcdc_run(&lcdb);" + NL +
         "			} else if (PARITY_LCDR_TEST)")
save(P + "probe.c", pc)
print("done")

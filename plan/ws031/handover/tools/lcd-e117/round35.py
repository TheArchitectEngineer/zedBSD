#!/usr/bin/env python3
"""WS031 E-117 round 35: LCD reuse mode (-DPARITY_LCDR_TEST=1): in one driver lifetime, show / stop three times; in the
first picture: the pipe IRQ state the power-well hook restored, a real vblank IRQ wait, brightness steps, backlight
off / on with the scanout continuing.  The show body gets an in-window hook; the kernel run is split into one body.
usage: round35.py <repo root>"""
import sys
NL, BS = chr(10), chr(92)
root = sys.argv[1].rstrip("/") + "/"
P = "src/drivers/gpu/i915/parity/"
L = P + "lcd/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:100]
    return s.replace(old, new)

# ---------------- show body: in-window hook
h = load(L + "parity_lcd_show.h")
h = rep(h, "	void (*at_stage)(void *ctx, int stage);", """	/* optional: a test that runs while the picture is up (after the first frames, before the window); 0 = passed.
	 * A failure is recorded as the first anomaly (if none yet) and the reference stop path follows. */
	int (*in_window)(void *ctx, struct parity_lcd_observer *o);
	void *in_window_ctx;
	void (*at_stage)(void *ctx, int stage);""")
h = rep(h, "	unsigned steady_rounds;", "	unsigned steady_rounds;" + NL + "	int window_hook_rc;")
save(L + "parity_lcd_show.h", h)
s = load(L + "parity_lcd_show.c")
s = rep(s, "		reached(env, r, PARITY_LCD_SHOW_PICTURE_UP);" + NL,
        "		reached(env, r, PARITY_LCD_SHOW_PICTURE_UP);" + NL +
        "		if (env->in_window != 0) {" + NL +
        "			r->window_hook_rc = env->in_window(env->in_window_ctx, &r->obs);" + NL +
        "			if (r->window_hook_rc != 0)" + NL +
        '				anomaly(r, "the in-window test failed while the picture was up", r->window_hook_rc);' + NL + "		}" + NL)
s = rep(s, "		for (waited = 0u; waited < env->window_ms; waited += 500u) {",
        "		for (waited = 0u; r->window_hook_rc == 0 && waited < env->window_ms; waited += 500u) {")
s = rep(s, "	r->pass = r->first_anomaly == 0 && r->released", "	r->pass = r->first_anomaly == 0 && r->window_hook_rc == 0 && r->released")
save(L + "parity_lcd_show.c", s)

# ---------------- kernel: one body, two modes
k = load(L + "parity_lcd_kernel.c")
k = rep(k, "int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d)" + NL + "{" + NL +
        "	static struct parity_lcd_show_env env;" + NL + "	static struct parity_lcd_show_report rep;" + NL +
        "	struct lcd_kernel *k = &lk;" + NL + "	int rc, i, held = 0;" + NL,
        """struct lcd_run_params {
	unsigned pattern_id;
	uint64_t pattern_fnv;                   /* 0 = no pinned hash (the read-back check still runs) */
	unsigned window_ms;
	int (*in_window)(void *ctx, struct parity_lcd_observer *o);
};

static int lcd_run_one(const struct parity_lcd_kernel_deps *d, const struct lcd_run_params *p)
{
	static struct parity_lcd_show_env env;
	static struct parity_lcd_show_report rep;
	struct lcd_kernel *k = &lk;
	int rc, i, held = 0;
""")
k = rep(k, "	env.pattern_id = PARITY_LCDB_PATTERN_ID;" + NL + "	env.pattern_fnv = PARITY_LCDB_PATTERN_FNV;" + NL +
        "	env.first_frames_ms = 1000u;" + NL + "	env.window_ms = PARITY_LCDB_WINDOW_MS;",
        "	env.pattern_id = p->pattern_id;" + NL + "	env.pattern_fnv = p->pattern_fnv;" + NL +
        "	env.first_frames_ms = 1000u;" + NL + "	env.window_ms = p->window_ms;" + NL +
        "	env.in_window = p->in_window;" + NL + "	env.in_window_ctx = k;" + NL +
        "	k->pattern_id = p->pattern_id;" + NL + "	k->window_ms = p->window_ms;" + NL +
        "	k->post_before = d->irq->vbl != 0 ? d->irq->vbl->post_enable_calls : 0u;" + NL +
        "	k->pre_before = d->irq->vbl != 0 ? d->irq->vbl->pre_disable_calls : 0u;")
k = rep(k, "(unsigned long long)rep.pattern_hash, (unsigned long long)PARITY_LCDB_PATTERN_FNV,", "(unsigned long long)rep.pattern_hash, (unsigned long long)p->pattern_fnv,")
k = rep(k, "	if (rep.pass && (held != 0 || k->unresolved_steps != 0u || k->time_faults != 0u))" + NL + "		rep.pass = 0;",
        """	if (d->irq->vbl != 0)
		kern_logf("i915: parity LCD-B power-well IRQ hooks during this run: post_enable +%u pre_disable +%u (sync calls %u, "
			"sync timeouts %u) | pipe A IMR now 0x%08x (well off reads 0)\\n", d->irq->vbl->post_enable_calls - k->post_before,
			d->irq->vbl->pre_disable_calls - k->pre_before, d->irq->vbl->sync_calls, d->irq->vbl->sync_timeouts,
			osdep_mmio_read32(d->mmio, 0x44404u));
	if (rep.pass && (held != 0 || k->unresolved_steps != 0u || k->time_faults != 0u))
		rep.pass = 0;
	/* the pipe's well went on (post-enable restored its interrupt registers) and off again (pre-disable stopped them) */
	if (rep.pass && d->irq->vbl != 0 && (d->irq->vbl->post_enable_calls == k->post_before ||
	    d->irq->vbl->pre_disable_calls == k->pre_before || d->irq->vbl->sync_timeouts != 0u))
		rep.pass = 0;""")
k = rep(k, "	lcdb_summary.retained = parity_lcd_show_retained();" + NL + "	return rep.pass ? 0 : -1;" + NL + "}",
        """	lcdb_summary.retained = parity_lcd_show_retained();
	return rep.pass ? 0 : -1;
}

int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d)
{
	static const struct lcd_run_params p = { PARITY_LCDB_PATTERN_ID, PARITY_LCDB_PATTERN_FNV, PARITY_LCDB_WINDOW_MS, 0 };

	return lcd_run_one(d, &p);
}

/* ---------------- LCD reuse (-DPARITY_LCDR_TEST=1) ---------------- */

static uint32_t read_frame(void *ctx)
{
	struct lcd_kernel *k = ctx;

	return osdep_mmio_read32(k->d->mmio, parity_lcd_reg_by_name("PIPE_FRMCOUNT_G4X"));
}

static void step_sleep(struct lcd_kernel *k, unsigned ms)
{
	unsigned t;

	for (t = 0u; t < ms; t += 100u)
		k_usleep(k, 100000u);
}

/*
 * The pipe's interrupts, driver-managed: the registers the power-well post-enable hook restored, then a vblank
 * reference (IMR vblank bit cleared), three waits that each need a NEW pipe A vblank interrupt and a moving frame
 * counter, the last put (bit set again), and no further vblank interrupt once it is masked.
 */
static int irq_check(struct lcd_kernel *k, int verbose)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_irq_vblank *v = d->irq->vbl;
	uint32_t extra = 0x00000001u | parity_gen8_de_pipe_underrun_mask(13) | parity_gen8_de_pipe_flip_done_mask(13);
	uint32_t imr, ier, imr_on, imr_off, want_ier, f0, f1, seen[3] = { 0u, 0u, 0u };
	unsigned raw0, raw1, raw2, i;
	int rc[3] = { -1, -1, -1 }, get_rc, ok;

	if (v == 0 || !v->inited)
		return -22;
	imr = osdep_mmio_read32(d->mmio, 0x44404u);
	ier = osdep_mmio_read32(d->mmio, 0x4440cu);
	want_ier = ~d->irq->de_irq_mask[0] | extra;
	get_rc = parity_drm_vblank_get(d->irq, 0u);
	imr_on = osdep_mmio_read32(d->mmio, 0x44404u);
	raw0 = d->irq->de_vblank_count[0];
	f0 = read_frame(k);
	for (i = 0u; i < 3u && get_rc == 0; i++)
		rc[i] = parity_wait_vblank(d->irq, 0u, 1u, 100u, read_frame, k, &seen[i]);
	f1 = read_frame(k);
	raw1 = d->irq->de_vblank_count[0];
	parity_drm_vblank_put(d->irq, 0u);
	imr_off = osdep_mmio_read32(d->mmio, 0x44404u);
	step_sleep(k, 100u);
	raw2 = d->irq->de_vblank_count[0];
	ok = get_rc == 0 && imr == d->irq->de_irq_mask[0] && (imr & 1u) != 0u && ier == want_ier &&
		(imr_on & 1u) == 0u && rc[0] == 0 && rc[1] == 0 && rc[2] == 0 && raw1 - raw0 >= 3u && f1 != f0 &&
		(imr_off & 1u) != 0u && raw2 == raw1 && v->refs[0] == 0u;
	kern_logf("i915: parity LCD-R IRQ: after the well came on: GEN8_DE_PIPE_IMR(A)=0x%08x (driver's de_irq_mask 0x%08x) "
		"GEN8_DE_PIPE_IER(A)=0x%08x (expected ~mask|vblank|underrun|flip done = 0x%08x) | vblank get rc=%d IMR=0x%08x | waits rc=%d/%d/%d "
		"new-vblanks %u/%u/%u | handler vblank IRQs +%u frames %u->%u | put IMR=0x%08x | vblank IRQs in the next 100 ms after "
		"masking: %u | refs %u -> %s\\n", imr, d->irq->de_irq_mask[0], ier, want_ier, get_rc, imr_on, rc[0], rc[1], rc[2],
		seen[0], seen[1], seen[2], raw1 - raw0, f0, f1, imr_off, raw2 - raw1, v->refs[0], ok ? "OK" : "FAIL");
	(void)verbose;
	return ok ? 0 : -5;
}

static int brightness_step(struct lcd_kernel *k, unsigned step, const char *name, int op, uint32_t level, uint32_t max,
	unsigned hold_ms)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	struct parity_lcd_modeset_status s;
	uint32_t f0, f1;
	int rc;

	rc = op == 0 ? parity_lcd_modeset_brightness(level, max) : parity_lcd_modeset_backlight(op > 0);
	f0 = read_frame(k);
	parity_lcd_modeset_status(&s);
	kern_logf("i915: parity LCD-R step=%u %s rc=%d | BXT_BLC_PWM_CTL=0x%08x FREQ=0x%08x DUTY=0x%08x (level %u of [%u,%u]) "
		"backlight_enabled=%d PP_CONTROL backlight bit via eDP | crtc_active=%d plane_armed=%d (take the photograph)\\n",
		step, name, rc, osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_CTL")),
		osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_FREQ")),
		osdep_mmio_read32(d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_DUTY")), s.backlight_level, s.backlight_min,
		s.backlight_max, s.backlight_enabled, s.crtc_active, s.plane_armed);
	step_sleep(k, hold_ms);
	f1 = read_frame(k);
	kern_logf("i915: parity LCD-R step=%u %s done: frame counter %u -> %u over %u ms (the scanout %s)\\n", step, name, f0, f1, hold_ms,
		f1 != f0 ? "continued" : "STOPPED");
	return rc == PARITY_LCD_MS_OK && f1 != f0 && s.crtc_active && s.plane_armed ? 0 : -5;
}

static int window_first(void *ctx, struct parity_lcd_observer *o)
{
	struct lcd_kernel *k = ctx;
	struct parity_lcd_modeset_status s;
	uint32_t mx, orig, duty;
	int rc;

	(void)o;
	if ((rc = irq_check(k, 1)) != 0)
		return rc;
	parity_lcd_modeset_status(&s);
	mx = s.backlight_max;
	orig = s.backlight_level;
	kern_logf("i915: parity LCD-R brightness: VBT min_brightness %u (a 0..255 coefficient) -> backlight.min %u of max %u "
		"(get_backlight_min_vbt); current level %u; user range [0, %u] (intel_backlight_device_register)\\n",
		d_vbt_min(k), s.backlight_min, mx, orig, mx);
	if ((rc = brightness_step(k, 0u, "max", 0, mx, mx, 7000u)) != 0 ||
	    (rc = brightness_step(k, 1u, "half", 0, mx / 2u, mx, 7000u)) != 0 ||
	    (rc = brightness_step(k, 2u, "min(user 0)", 0, 0u, mx, 7000u)) != 0 ||
	    (rc = brightness_step(k, 3u, "half-again", 0, mx / 2u, mx, 1000u)) != 0 ||
	    (rc = brightness_step(k, 4u, "backlight-off(scanout continues)", -1, 0u, mx, 7000u)) != 0 ||
	    (rc = brightness_step(k, 5u, "backlight-on", 1, 0u, mx, 7000u)) != 0)
		return rc;
	parity_lcd_modeset_status(&s);
	duty = osdep_mmio_read32(k->d->mmio, parity_lcd_reg_by_name("BXT_BLC_PWM_DUTY"));
	if (s.backlight_level != mx / 2u + 0u && s.backlight_level != duty)
		kern_logf("i915: parity LCD-R note: level after backlight-on %u, duty 0x%08x\\n", s.backlight_level, duty);
	return brightness_step(k, 6u, "restore", 0, orig, mx, 1000u);
}

static int window_again(void *ctx, struct parity_lcd_observer *o)
{
	(void)o;
	return irq_check(ctx, 0);
}

int parity_lcd_kernel_lcdr_run(const struct parity_lcd_kernel_deps *d)
{
	static const struct lcd_run_params cyc[3] = {
		{ 110u, PARITY_LCDB_PATTERN_FNV, 2000u, window_first },
		{ 111u, 0u, 3000u, window_again },
		{ 112u, 0u, 3000u, window_again },
	};
	unsigned i, passed = 0u;

	kern_logf("i915: parity LCD-R begin: three show / stop cycles in one driver lifetime (patterns 110, 111, 112); one display "
		"owner, run serially (the modeset object is global; the DPLL / backlight mutexes do not make the whole modeset reentrant)\\n");
	for (i = 0u; i < 3u; i++) {
		int rc;

		kern_logf("i915: parity LCD-R cycle %u begin (pattern %u)\\n", i + 1u, cyc[i].pattern_id);
		rc = lcd_run_one(d, &cyc[i]);
		kern_logf("i915: parity LCD-R cycle %u verdict: %s\\n", i + 1u, rc == 0 ? "PASS" : "FAIL");
		if (rc != 0)
			break;                  /* first anomaly: no further cycle */
		passed++;
	}
	kern_logf("i915: parity LCD-R verdict: %s (cycles passed %u/3; photographs are separate evidence)\\n", passed == 3u ? "PASS" : "FAIL",
		passed);
	lcdb_summary.pass = passed == 3u;
	return passed == 3u ? 0 : -1;
}""")
k = rep(k, "	int phase_cleanup;", "	int phase_cleanup;" + NL + "	unsigned pattern_id, window_ms, post_before, pre_before;")
k = rep(k, '			"(take the photograph)\\n", PARITY_LCDB_PATTERN_ID, PARITY_LCDB_WINDOW_MS);',
        '			"(take the photograph)\\n", k->pattern_id, k->window_ms);')
# the VBT minimum for the log (the connector's copy)
k = rep(k, "static int window_first(void *ctx, struct parity_lcd_observer *o)",
        "static unsigned d_vbt_min(struct lcd_kernel *k)" + NL + "{" + NL + "	return k->vbt_min;" + NL + "}" + NL + NL +
        "static int window_first(void *ctx, struct parity_lcd_observer *o)")
k = rep(k, "	unsigned pattern_id, window_ms, post_before, pre_before;", "	unsigned pattern_id, window_ms, post_before, pre_before, vbt_min;")
k = rep(k, "	c->vbt_backlight_min_brightness = pn.bl_min_brightness;", "	c->vbt_backlight_min_brightness = pn.bl_min_brightness;" + NL + "	k->vbt_min = pn.bl_min_brightness;")
k = rep(k, '#include "parity_lcd_kernel.h"', '#include "parity_lcd_kernel.h"' + NL + '#include "parity_lcd_modeset.h"')
save(L + "parity_lcd_kernel.c", k)
kh = load(L + "parity_lcd_kernel.h")
kh = rep(kh, "int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d);",
         "int parity_lcd_kernel_lcdb_run(const struct parity_lcd_kernel_deps *d);" + NL +
         "/* LCD reuse (-DPARITY_LCDR_TEST=1): three show / stop cycles, pipe IRQ + vblank, brightness, backlight off / on */" + NL +
         "int parity_lcd_kernel_lcdr_run(const struct parity_lcd_kernel_deps *d);")
save(L + "parity_lcd_kernel.h", kh)

# ---------------- mode flag + probe
b = load(P + "bios.h")
b = rep(b, "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST)",
        "#ifndef PARITY_LCDR_TEST" + NL + "#define PARITY_LCDR_TEST 0            /* LCD reuse: three cycles + IRQ + brightness (implies the explicit VBT) */" + NL + "#endif" + NL +
        "#ifndef PARITY_VBT_EXPLICIT" + NL + "#define PARITY_VBT_EXPLICIT (PARITY_AUX_TEST || PARITY_LCDB_TEST || PARITY_LCDR_TEST)")
save(P + "bios.h", b)
pc = load(P + "probe.c")
pc = rep(pc, "	if (PARITY_LCDB_TEST) {", "	if (PARITY_LCDB_TEST || PARITY_LCDR_TEST) {")
pc = rep(pc, "			(void)parity_lcd_kernel_lcdb_run(&lcdb);", "			if (PARITY_LCDR_TEST)" + NL + "				(void)parity_lcd_kernel_lcdr_run(&lcdb);" + NL +
         "			else" + NL + "				(void)parity_lcd_kernel_lcdb_run(&lcdb);")
save(P + "probe.c", pc)

# ---------------- GPU-free: the show body twice in one lifetime
sk = load(L + "lcd_show_ktest.c")
sk = rep(sk, "	/* ---- B: early failure: nothing is shown, everything is given back, the first anomaly is the training ---- */",
         """	/* ---- A2: the same body again in the same lifetime (another picture), then an in-window hook that fails ---- */
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

	/* ---- B: early failure: nothing is shown, everything is given back, the first anomaly is the training ---- */""")
sk = rep(sk, "static void stick_the_pipe(void *ctx, int stage)",
         "static int fail_in_window(void *ctx, struct parity_lcd_observer *o)" + NL + "{" + NL + "	(void)ctx;" + NL + "	(void)o;" + NL +
         "	return -5;" + NL + "}" + NL + NL + "static void stick_the_pipe(void *ctx, int stage)")
save(L + "lcd_show_ktest.c", sk)
print("done")

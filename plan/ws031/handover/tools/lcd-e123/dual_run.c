/* ---------------- two screens at once (-DPARITY_DUAL_TEST=1) ---------------- */

static void step_sleep(struct lcd_kernel *k, unsigned ms);      /* below: the run's own sleep */

/* the pipe's frame counter (PIPE_FRMCOUNT_G4X of that pipe) */
static uint32_t dual_frame(const struct parity_lcd_kernel_deps *d, int pipe)
{
	return osdep_mmio_read32(d->mmio, 0x70040u + 0x1000u * (uint32_t)pipe);
}

struct dual_screen {
	unsigned idx;                           /* the modeset object */
	const char *name;
	int pipe;
	struct parity_scanout so;
	struct parity_scanout *sop;             /* the buffer this screen reads (its own, or one shared with the other) */
	struct parity_lcd_modeset_cfg cfg;
	const struct parity_lcd_state *state;
	unsigned pattern_id;
	uint64_t pattern_hash;
	int prepare_rc, enable_rc, disable_rc, begun, armed, released;
	uint32_t frame_first, frame_last;
};

static int dual_bring_up(struct lcd_kernel *k, struct dual_screen *sc)
{
	const struct parity_lcd_kernel_deps *d = k->d;
	int rc;

	rc = parity_scanout_create(d->gm, (uint32_t)sc->state->mode.hdisplay, (uint32_t)sc->state->mode.vdisplay,
		PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &sc->so);
	if (rc != 0) {
		kern_logf("i915: parity DUAL %s: scanout create rc=%d\n", sc->name, rc);
		return -1;
	}
	rc = parity_scanout_pin(&sc->so, sc->name);
	if (rc != 0) {
		kern_logf("i915: parity DUAL %s: scanout pin rc=%d\n", sc->name, rc);
		(void)parity_scanout_destroy(&sc->so);
		return -1;
	}
	sc->pattern_hash = parity_lcd_pattern_fill(sc->so.cpu, sc->so.pitch, sc->so.width, sc->so.height, sc->pattern_id);
	parity_scanout_publish(&sc->so);
	sc->cfg.fb_fourcc = sc->so.format;
	sc->cfg.fb_modifier = sc->so.modifier;
	sc->cfg.fb_width = sc->so.width;
	sc->cfg.fb_height = sc->so.height;
	sc->cfg.fb_pitch = sc->so.pitch;
	sc->cfg.fb_surf = (uint32_t)sc->so.surf;

	(void)parity_lcd_modeset_select(sc->idx);
	sc->prepare_rc = parity_lcd_modeset_prepare(sc->state, &sc->cfg, &k->ops);
	if (sc->prepare_rc != 0) {
		kern_logf("i915: parity DUAL %s: prepare rc=%d\n", sc->name, sc->prepare_rc);
		return -1;
	}
	sc->begun = parity_scanout_begin(&sc->so) == 0;      /* the display takes the buffer */
	if (!sc->begun) {
		kern_logf("i915: parity DUAL %s: the display could not take the buffer\n", sc->name);
		return -1;
	}
	sc->enable_rc = parity_lcd_modeset_commit_enable();
	sc->armed = sc->enable_rc == PARITY_LCD_MS_OK;
	kern_logf("i915: parity DUAL %s: enabled rc=%d pattern=%u surf=0x%08x %ux%u pitch=%u\n", sc->name, sc->enable_rc,
		sc->pattern_id, (unsigned)sc->so.surf, sc->so.width, sc->so.height, sc->so.pitch);
	return sc->enable_rc == PARITY_LCD_MS_OK ? 0 : -1;
}

static void dual_log_state(const char *when, struct dual_screen *sc)
{
	struct parity_lcd_modeset_status s;

	(void)parity_lcd_modeset_select(sc->idx);
	parity_lcd_modeset_status(&s);
	kern_logf("i915: parity DUAL %s [%s]: crtc_active=%d plane_armed=%d DPLL%d on=%d active_mask=0x%x pipe_mask=0x%x | "
		"io wakeref=%d crtc domains=%u | DBUF slices=0x%x mbus_joined=%d | ddb %u..%u | wm0 enable=%u blocks=%u lines=%u | errors=%u\n",
		sc->name, when, s.crtc_active, s.plane_armed, s.pll_id, s.pll_on, s.pll_active_mask, s.pll_pipe_mask,
		s.ddi_io_wakeref, s.crtc_domains_held, s.dbuf_slices_now, s.mbus_joined_now, s.ddb_start, s.ddb_end,
		s.wm0_enable, s.wm0_blocks, s.wm0_lines, s.errors);
}

static int dual_stop(struct lcd_kernel *k, struct dual_screen *sc)
{
	int prc, rc;

	(void)k;
	(void)parity_lcd_modeset_select(sc->idx);
	prc = parity_lcd_modeset_plane_disable();
	rc = parity_lcd_modeset_commit_disable();
	sc->disable_rc = rc != PARITY_LCD_MS_OK ? rc : prc;
	if (sc->disable_rc == PARITY_LCD_MS_OK) {
		parity_lcd_modeset_plane_released();            /* the caller watched the pipe stand still (below) */
		parity_scanout_end(sc->sop != 0 ? sc->sop : &sc->so);
	}
	kern_logf("i915: parity DUAL %s: stopped rc=%d\n", sc->name, sc->disable_rc);
	if (sc->disable_rc != PARITY_LCD_MS_OK) {
		struct parity_lcd_modeset_status s;

		parity_lcd_modeset_status(&s);
		kern_logf("i915: parity DUAL %s: what the stop left: crtc_active=%d plane_armed=%d DPLL%d on=%d "
			"active_mask=0x%x pipe_mask=0x%x io wakeref=%d aux wakeref=%d crtc domains=%u dc_off_held=%d "
			"stop_unconfirmed=%d errors=%u first=%s\n", sc->name, s.crtc_active, s.plane_armed, s.pll_id, s.pll_on,
			s.pll_active_mask, s.pll_pipe_mask, s.ddi_io_wakeref, s.aux_wakeref, s.crtc_domains_held, s.dc_off_held,
			s.stop_unconfirmed, s.errors, s.first_error != 0 ? s.first_error : "-");
	}
	return sc->disable_rc == PARITY_LCD_MS_OK ? 0 : -1;
}

static void dual_release(struct dual_screen *sc)
{
	int unpin = parity_scanout_unpin(&sc->so);
	int destroy = unpin == 0 ? parity_scanout_destroy(&sc->so) : -EBUSY;

	sc->released = unpin == 0 && destroy == 0;
	kern_logf("i915: parity DUAL %s: buffer unpin=%d destroy=%d released=%d\n", sc->name, unpin, destroy, sc->released);
}

/*
 * DUAL: the panel (pipe A, its own picture) and the external HDMI display (pipe B, another picture) at the same
 * time.  Then the external one is stopped while the panel keeps running -- the point of the test: one screen's stop
 * must not take the other's PLL, power domains or buffer with it.
 */
int parity_lcd_kernel_dual_run(const struct parity_lcd_kernel_deps *d)
{
	static struct dual_screen a, b;
	static struct parity_lcd_state hdmi_state;
	static const struct parity_lcd_mode cea4 = { 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 1, 1, 0, 0, 0, 8 };
	struct lcd_kernel *k = &lk;
	const struct parity_vbt_encoder *ve;
	uint32_t fa0, fb0, fa1, fb1, fa2, fa3;
	unsigned t;
	int rc, pass = 1;

	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || !d->gm->inited) {
		kern_logf("i915: parity DUAL verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained()) {
		kern_logf("i915: parity DUAL verdict: FAIL (an earlier run's resources are retained)\n");
		return -1;
	}
	if (!lcdb_locks_live) {
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_DPLL], LOCK_RANK_DEVICE, "parity-lcd-dpll");
		(void)mutex_init(&lcdb_locks[PARITY_LCD_LOCK_BACKLIGHT], LOCK_RANK_DEVICE, "parity-lcd-backlight");
		lcdb_locks_live = 1;
	}
	memset(k, 0, sizeof(*k));
	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));
	k->locks = lcdb_locks;
	k->d = d;
	bind_ops(k);
	parity_lcd_dplls_reset();
	parity_lcd_dbuf_forget();
	(void)parity_gt_display_window_init(d->gm, PARITY_GT_DISPLAY_PAGES);

	/* screen A: the resident panel, as LCD-B drives it */
	a.idx = 0; a.name = "LCD"; a.pipe = 0; a.state = &d->edp->lcd; a.pattern_id = PARITY_LCDB_PATTERN_ID;
	a.sop = &a.so; b.sop = &b.so;                   /* each screen has its own buffer in this test */
	if (!d->edp->connector_live || d->edp->lcd_rc != 0) {
		kern_logf("i915: parity DUAL verdict: FAIL (the resident eDP is not live)\n");
		return -1;
	}
	if (fill_cfg(k, &a.cfg) != 0) {
		kern_logf("i915: parity DUAL verdict: FAIL (the panel's configuration could not be built)\n");
		return -1;
	}
	a.cfg.also_active_pipes = 1u << 1;              /* pipe B is part of this configuration */

	/* screen B: the external HDMI display */
	b.idx = 1; b.name = "HDMI"; b.pipe = 1; b.pattern_id = 111u;
	rc = parity_lcd_compute_hdmi(&cea4, 38400, &hdmi_state);
	if (rc != 0) {
		kern_logf("i915: parity DUAL verdict: FAIL (the WRPLL calculation refused the TMDS clock: rc=%d)\n", rc);
		return -1;
	}
	b.state = &hdmi_state;
	b.cfg = a.cfg;                                  /* the device-wide inputs are the same */
	b.cfg.output_hdmi = 1;
	b.cfg.port = 1; b.cfg.pipe = 1; b.cfg.cpu_transcoder = 1; b.cfg.aux_ch = 1;
	b.cfg.dpll_id = 0;                              /* only the caller's expectation: the rule decides */
	b.cfg.saved_port_bits = osdep_mmio_read32(d->mmio, 0x64100u) & ((1u << 16) | (1u << 4));
	b.cfg.vbt_backlight_present = 0;
	ve = parity_vbt_encoder_for_port(&d->edp->vbt->parsed, 1);
	b.cfg.vbt_hdmi_level_shift = ve != 0 ? ve->hdmi_level_shift : -1;
	b.cfg.also_active_pipes = 1u << 0;              /* pipe A is part of this configuration */

	kern_logf("i915: parity DUAL: LCD %ux%u pattern %u on pipe A | HDMI %ux%u pattern %u on pipe B (VBT level shift %d)\n",
		a.state->mode.hdisplay, a.state->mode.vdisplay, a.pattern_id, b.state->mode.hdisplay, b.state->mode.vdisplay,
		b.pattern_id, b.cfg.vbt_hdmi_level_shift);

	if (dual_bring_up(k, &a) != 0 || dual_bring_up(k, &b) != 0) {
		pass = 0;
		goto stop;
	}
	dual_log_state("both up", &a);
	dual_log_state("both up", &b);

	/* both pipes must be scanning out */
	fa0 = dual_frame(d, 0); fb0 = dual_frame(d, 1);
	step_sleep(k, 500u);
	fa1 = dual_frame(d, 0); fb1 = dual_frame(d, 1);
	a.frame_first = fa0; b.frame_first = fb0;
	kern_logf("i915: parity DUAL frames after enable: pipe A %u -> %u, pipe B %u -> %u\n", fa0, fa1, fb0, fb1);
	if (fa1 == fa0 || fb1 == fb0) {
		kern_logf("i915: parity DUAL: a pipe's frame counter does not advance\n");
		pass = 0;
	}

	/* the window: both pictures are up (the photograph is separate evidence) */
	for (t = 0u; t < PARITY_DUAL_WINDOW_MS / 5000u; t++) {
		step_sleep(k, 5000u);
		kern_logf("i915: parity DUAL window t=%us: pipe A frame %u, pipe B frame %u\n", (t + 1u) * 5u,
			dual_frame(d, 0), dual_frame(d, 1));
	}

	/* the external display stops; the panel must keep running with everything it owns */
	if (dual_stop(k, &b) != 0)
		pass = 0;
	fa2 = dual_frame(d, 0);
	step_sleep(k, 500u);
	fa3 = dual_frame(d, 0);
	a.frame_last = fa3;
	b.frame_last = dual_frame(d, 1);
	dual_log_state("after the HDMI stop", &a);
	kern_logf("i915: parity DUAL after the HDMI stop: pipe A frame %u -> %u (keeps running), pipe B frame %u (stopped)\n",
		fa2, fa3, b.frame_last);
	if (fa3 == fa2) {
		kern_logf("i915: parity DUAL: the panel stopped when the external display did\n");
		pass = 0;
	}
	{
		struct parity_lcd_modeset_status sa;

		(void)parity_lcd_modeset_select(a.idx);
		parity_lcd_modeset_status(&sa);
		if (!sa.crtc_active || !sa.plane_armed || !sa.pll_on || sa.crtc_domains_held == 0u) {
			kern_logf("i915: parity DUAL: the panel lost its own resources when the external display stopped\n");
			pass = 0;
		}
	}
	step_sleep(k, 3000u);                           /* the panel alone, for the photograph */

stop:
	if (a.armed && dual_stop(k, &a) != 0)
		pass = 0;
	if (b.begun && b.disable_rc != PARITY_LCD_MS_OK)
		pass = 0;
	if (a.so.state != PARITY_SCANOUT_NONE)
		dual_release(&a);
	if (b.so.state != PARITY_SCANOUT_NONE)
		dual_release(&b);
	if (!a.released || !b.released)
		pass = 0;
	kern_logf("i915: parity DUAL verdict: %s (LCD: enable rc=%d disable rc=%d released=%d | HDMI: enable rc=%d "
		"disable rc=%d released=%d | the photograph is separate evidence)\n", pass ? "PASS" : "FAIL", a.enable_rc,
		a.disable_rc, a.released, b.enable_rc, b.disable_rc, b.released);
	lcdb_summary.ran = 1;
	lcdb_summary.pass = pass;
	lcdb_summary.stage = pass ? "released" : "dual";
	lcdb_summary.first_anomaly = pass ? 0 : "see the DUAL lines";
	(void)parity_lcd_modeset_select(0);
	return pass ? 0 : -1;
}

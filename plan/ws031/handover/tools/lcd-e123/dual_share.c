/* ---------------- one buffer on both screens (-DPARITY_DUAL_SHARE_TEST=1) ---------------- */

/*
 * The same buffer feeds both pipes.  The panel shows all of it (1920x1080); the external display reads the SAME
 * address with the SAME pitch, so it shows the top-left 1280x720 of the same rows -- a partial view of a shared
 * buffer.  It is not a scaled mirror: that needs a pipe scaler, which this path does not program.
 * What the test is about: the buffer belongs to both screens at once, and only the LAST of them releases it.
 */
int parity_lcd_kernel_dual_share_run(const struct parity_lcd_kernel_deps *d)
{
	static struct dual_screen a, b;
	static struct parity_scanout shared;
	static struct parity_lcd_state hdmi_state;
	static const struct parity_lcd_mode cea4 = { 74250, 1280, 1390, 1430, 1650, 720, 725, 730, 750, 1, 1, 0, 0, 0, 8 };
	struct lcd_kernel *k = &lk;
	const struct parity_vbt_encoder *ve;
	uint32_t fa0, fb0, fa1, fb1;
	unsigned t;
	int rc, pass = 1;

	if (d == 0 || d->edp == 0 || d->mmio == 0 || d->gm == 0 || !d->gm->inited) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (a dependency is missing)\n");
		return -1;
	}
	if (parity_lcd_show_retained() || parity_lcd_modeset_retained()) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (an earlier run's resources are retained)\n");
		return -1;
	}
	if (!d->edp->connector_live || d->edp->lcd_rc != 0) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (the resident eDP is not live)\n");
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

	a.idx = 0; a.name = "LCD"; a.pipe = 0; a.state = &d->edp->lcd; a.pattern_id = PARITY_LCDB_PATTERN_ID;
	b.idx = 1; b.name = "HDMI"; b.pipe = 1; b.pattern_id = PARITY_LCDB_PATTERN_ID;
	a.sop = &shared; b.sop = &shared;               /* both screens read the SAME buffer */
	if (fill_cfg(k, &a.cfg) != 0) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (the panel's configuration could not be built)\n");
		return -1;
	}
	a.cfg.also_active_pipes = 1u << 1;
	rc = parity_lcd_compute_hdmi(&cea4, 38400, &hdmi_state);
	if (rc != 0) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (the WRPLL calculation refused the TMDS clock: rc=%d)\n", rc);
		return -1;
	}
	b.state = &hdmi_state;
	b.cfg = a.cfg;
	b.cfg.output_hdmi = 1;
	b.cfg.port = 1; b.cfg.pipe = 1; b.cfg.cpu_transcoder = 1; b.cfg.aux_ch = 1;
	b.cfg.saved_port_bits = osdep_mmio_read32(d->mmio, 0x64100u) & ((1u << 16) | (1u << 4));
	b.cfg.vbt_backlight_present = 0;
	ve = parity_vbt_encoder_for_port(&d->edp->vbt->parsed, 1);
	b.cfg.vbt_hdmi_level_shift = ve != 0 ? ve->hdmi_level_shift : -1;
	b.cfg.also_active_pipes = 1u << 0;

	/* ONE buffer, the panel's size */
	rc = parity_scanout_create(d->gm, (uint32_t)a.state->mode.hdisplay, (uint32_t)a.state->mode.vdisplay,
		PARITY_FOURCC_XRGB8888, PARITY_MOD_LINEAR, &shared);
	if (rc == 0)
		rc = parity_scanout_pin(&shared, "dual-shared");
	if (rc != 0) {
		kern_logf("i915: parity DUAL-SHARED verdict: FAIL (the shared buffer could not be made: rc=%d)\n", rc);
		return -1;
	}
	(void)parity_lcd_pattern_fill(shared.cpu, shared.pitch, shared.width, shared.height, a.pattern_id);
	parity_scanout_publish(&shared);
	kern_logf("i915: parity DUAL-SHARED: one buffer surf=0x%08x %ux%u pitch=%u pattern %u | LCD shows all of it, "
		"HDMI the top-left %ux%u of the same rows (partial view, not a scaled mirror)\n", (unsigned)shared.surf,
		shared.width, shared.height, shared.pitch, a.pattern_id, b.state->mode.hdisplay, b.state->mode.vdisplay);

	a.cfg.fb_fourcc = shared.format; a.cfg.fb_modifier = shared.modifier;
	a.cfg.fb_width = shared.width; a.cfg.fb_height = shared.height;
	a.cfg.fb_pitch = shared.pitch; a.cfg.fb_surf = (uint32_t)shared.surf;
	b.cfg.fb_fourcc = shared.format; b.cfg.fb_modifier = shared.modifier;
	b.cfg.fb_width = (uint32_t)b.state->mode.hdisplay;      /* the part this output shows */
	b.cfg.fb_height = (uint32_t)b.state->mode.vdisplay;
	b.cfg.fb_pitch = shared.pitch;                          /* the SAME rows: the buffer's own pitch */
	b.cfg.fb_surf = (uint32_t)shared.surf;

	/* both screens take the buffer (the count is what keeps it alive) */
	(void)parity_lcd_modeset_select(a.idx);
	a.prepare_rc = parity_lcd_modeset_prepare(a.state, &a.cfg, &k->ops);
	a.begun = a.prepare_rc == 0 && parity_scanout_begin(&shared) == 0;
	a.enable_rc = a.begun ? parity_lcd_modeset_commit_enable() : -99;
	a.armed = a.enable_rc == PARITY_LCD_MS_OK;
	(void)parity_lcd_modeset_select(b.idx);
	b.prepare_rc = parity_lcd_modeset_prepare(b.state, &b.cfg, &k->ops);
	b.begun = b.prepare_rc == 0 && parity_scanout_begin(&shared) == 0;
	b.enable_rc = b.begun ? parity_lcd_modeset_commit_enable() : -99;
	b.armed = b.enable_rc == PARITY_LCD_MS_OK;
	kern_logf("i915: parity DUAL-SHARED: LCD prepare=%d enable=%d | HDMI prepare=%d enable=%d | buffer users=%u state=%d\n",
		a.prepare_rc, a.enable_rc, b.prepare_rc, b.enable_rc, shared.users, shared.state);
	if (!a.armed || !b.armed) {
		pass = 0;
		goto stop;
	}
	dual_log_state("shared, both up", &a);
	dual_log_state("shared, both up", &b);

	fa0 = dual_frame(d, 0); fb0 = dual_frame(d, 1);
	step_sleep(k, 500u);
	fa1 = dual_frame(d, 0); fb1 = dual_frame(d, 1);
	kern_logf("i915: parity DUAL-SHARED frames: pipe A %u -> %u, pipe B %u -> %u (both read surf 0x%08x)\n",
		fa0, fa1, fb0, fb1, (unsigned)shared.surf);
	if (fa1 == fa0 || fb1 == fb0)
		pass = 0;
	for (t = 0u; t < 3u; t++) {
		step_sleep(k, 5000u);
		kern_logf("i915: parity DUAL-SHARED window t=%us: pipe A frame %u, pipe B frame %u\n", (t + 1u) * 5u,
			dual_frame(d, 0), dual_frame(d, 1));
	}

	/* the external display lets go: the buffer must stay, because the panel still reads it */
	if (dual_stop(k, &b) != 0)
		pass = 0;
	kern_logf("i915: parity DUAL-SHARED after the HDMI stop: buffer users=%u state=%d (IN_USE=3) | pipe A frame %u\n",
		shared.users, shared.state, dual_frame(d, 0));
	if (shared.users != 1u || shared.state != PARITY_SCANOUT_IN_USE) {
		kern_logf("i915: parity DUAL-SHARED: the buffer was given up while the panel still reads it\n");
		pass = 0;
	}
	{
		int unpin_while_used = parity_scanout_unpin(&shared);

		kern_logf("i915: parity DUAL-SHARED: an unpin while the panel reads it is refused: rc=%d (0 would be wrong)\n",
			unpin_while_used);
		if (unpin_while_used == 0)
			pass = 0;
	}
	step_sleep(k, 3000u);

stop:
	if (a.armed && dual_stop(k, &a) != 0)
		pass = 0;
	kern_logf("i915: parity DUAL-SHARED after both stops: buffer users=%u state=%d\n", shared.users, shared.state);
	{
		int unpin = parity_scanout_unpin(&shared);
		int destroy = unpin == 0 ? parity_scanout_destroy(&shared) : -EBUSY;

		a.released = b.released = unpin == 0 && destroy == 0;
		kern_logf("i915: parity DUAL-SHARED: buffer unpin=%d destroy=%d released=%d\n", unpin, destroy, a.released);
		if (!a.released)
			pass = 0;
	}
	kern_logf("i915: parity DUAL-SHARED verdict: %s (one buffer: LCD enable rc=%d disable rc=%d | HDMI enable rc=%d "
		"disable rc=%d | released=%d; the photograph is separate evidence)\n", pass ? "PASS" : "FAIL", a.enable_rc,
		a.disable_rc, b.enable_rc, b.disable_rc, a.released);
	lcdb_summary.ran = 1;
	lcdb_summary.pass = pass;
	lcdb_summary.stage = pass ? "released" : "dual-shared";
	lcdb_summary.first_anomaly = pass ? 0 : "see the DUAL-SHARED lines";
	(void)parity_lcd_modeset_select(0);
	return pass ? 0 : -1;
}

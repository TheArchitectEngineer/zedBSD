#!/usr/bin/env python3
"""WS031 E-116 round 23: tests go through the two commits; new checks (commit outer, missing settings, observation, ownership).
usage: round23.py <repo root>"""
import sys, re
NL, TAB, BS = chr(10), chr(9), chr(92)
root = sys.argv[1].rstrip("/") + "/"
L = "src/drivers/gpu/i915/parity/lcd/"
T = "plan/ws031/tests/lcd-modeset-host-test.c"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

# ---- model: a hook so that the production observer runs on it
h = load(L + "lcd_fake_hw.h")
h = rep(h, "	uint8_t obs[48]; unsigned nobs;", "	void (*on_observe)(void *ctx, int point); void *on_observe_ctx;      /* e.g. parity_lcd_observer_point */" + NL + "	uint8_t obs[48]; unsigned nobs;")
save(L + "lcd_fake_hw.h", h)
c = load(L + "lcd_fake_hw.c")
c = rep(c, "		hw->obs[hw->nobs++] = (uint8_t)point;", "		hw->obs[hw->nobs++] = (uint8_t)point;" + NL + "	if (hw->on_observe != 0)" + NL + "		hw->on_observe(hw->on_observe_ctx, point);")
save(L + "lcd_fake_hw.c", c)

# ---- the modeset: the caller took over what an unconfirmed stop left behind
a = load(L + "parity_lcd_modeset.h")
a = rep(a, "int parity_lcd_modeset_commit_disable(void);", "int parity_lcd_modeset_commit_disable(void);" + NL +
        "/* after an unconfirmed stop the caller recorded what stays held for ever (buffer, power, DBUF): the object forgets it */" + NL +
        "void parity_lcd_modeset_abandoned(void);")
save(L + "parity_lcd_modeset.h", a)
r = load(L + "parity_lcd_modeset.c")
r = r.rstrip(NL) + NL + """
void parity_lcd_modeset_abandoned(void)
{
	ms.prepared = 0;
	ms.plane_armed = 0;
	ms.dc_off_held = 0;
	ms.stop_unconfirmed = 0;
	ms.crtc.active = false;
}
"""
save(L + "parity_lcd_modeset.c", r)

CFG = ("	/* the state the target's normal initialisation leaves (real-machine log): CDCLK 179200 kHz from VCO 537600 on a 38.4 MHz" + NL +
       "	 * reference, voltage level 0; MBUS not joined; SAGV off with QGV point 2 allowed (derated bandwidth 11707 MB/s) */" + NL +
       "	c.cdclk_khz = 179200; c.cdclk_vco_khz = 537600; c.cdclk_ref_khz = 38400; c.cdclk_bypass_khz = 19200; c.cdclk_max_khz = 652800;" + NL +
       "	c.cdclk_voltage_level = 0; c.mbus_joined = 0; c.qgv_allowed_bw = 11707;" + NL)

t = load(T)
t = rep(t, '#include "parity_lcd_modeset.h"', '#include "parity_lcd_modeset.h"' + NL + '#include "parity_lcd_observe.h"' + NL + '#include "lcd_power_domain_enum.h"')
t = rep(t, "	c.dmc_fw_mask = 1u << 1;", CFG + "	c.dmc_fw_mask = 1u << 1;")
t = rep(t, 'static const char *kind_name[] = { "?", "write", "rmw", "read", "wait", "dpcd-w", "dpcd-r", "panel", "pw-get", "pw-put", "STEP", "ERROR", "PHASE", "decide" };',
        'static const char *kind_name[] = { "?", "write", "rmw", "read", "wait", "dpcd-w", "dpcd-r", "panel", "pw-get", "pw-put", "STEP", "ERROR", "PHASE", "decide", "dbuf", "observ" };')
t = rep(t, "		if (e->name != 0 && e->kind >= PARITY_LCD_T_STEP)" + NL, "		if (e->name != 0 && e->kind >= PARITY_LCD_T_STEP && e->kind <= PARITY_LCD_T_DECIDED)" + NL)
t = rep(t, "		if (e->name != 0 && e->kind >= PARITY_LCD_T_STEP && e->name[strlen(e->name) - 1] != '" + BS + "n')",
        "		if (e->name != 0 && e->kind >= PARITY_LCD_T_STEP && e->kind <= PARITY_LCD_T_DECIDED && e->name[strlen(e->name) - 1] != '" + BS + "n')")
# A: one commit up ...
t = rep(t, '	parity_lcd_trace_phase(&trace, "crtc enable");' + NL + "	rc = parity_lcd_modeset_enable();",
        '	parity_lcd_trace_phase(&trace, "commit: enable");' + NL + "	rc = parity_lcd_modeset_commit_enable();")
t = rep(t, "s.ddi_io_wakeref != 0 && s.aux_wakeref != 0 && lcd_fake_power_refs_total(&lcd) == 2," + NL +
        '	      "A: the enable owns: the PLL (pipe A), the DDI IO and the AUX power references");',
        "s.ddi_io_wakeref != 0 && s.aux_wakeref != 0 && lcd_fake_power_refs_total(&lcd) == 6," + NL +
        '	      "A: the enable owns: the PLL (pipe A), the DDI IO and the AUX power references, and the crtc its four domains");')
t = rep(t, "	parity_lcd_trace_init(&trace, &lcd.ops);        /* a fresh log for the plane phase (the backend keeps its state) */" + NL +
        "	rc = parity_lcd_modeset_plane_update();" + NL, "	CHECK(trace.dropped == 0, \"A: the run log holds the whole commit\");" + NL)
# ... and one commit down
t = rep(t, "	rc = parity_lcd_modeset_plane_disable();" + NL + "	CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_disarms == 1",
        "	parity_lcd_trace_init(&trace, &lcd.ops);        /* a fresh log for the second commit (the backend keeps its state) */" + NL +
        '	parity_lcd_trace_phase(&trace, "commit: disable");' + NL +
        "	rc = parity_lcd_modeset_commit_disable();" + NL + "	CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_disarms == 1")
t = rep(t, "	parity_lcd_modeset_plane_released();            /* the caller saw a further frame pass with the plane off */" + NL +
        '	parity_lcd_trace_phase(&trace, "crtc disable");' + NL + "	rc = parity_lcd_modeset_disable();" + NL + "	parity_lcd_modeset_status(&s);" + NL,
        "	parity_lcd_modeset_plane_released();            /* the caller saw the pipe stand still */" + NL)
# everything else: the stages become the commits
t = t.replace("	rc = rc == 0 ? parity_lcd_modeset_plane_update() : rc;" + NL, "")
t = t.replace("	(void)parity_lcd_modeset_plane_disable();" + NL, "")
t = t.replace("parity_lcd_modeset_enable()", "parity_lcd_modeset_commit_enable()")
t = t.replace("parity_lcd_modeset_disable()", "parity_lcd_modeset_commit_disable()")
t = rep(t, "	parity_lcd_modeset_plane_released();" + NL + "	(void)parity_lcd_modeset_commit_disable();" + NL + "	(void)parity_edp_end(&res);",
        "	(void)parity_lcd_modeset_commit_disable();" + NL + "	parity_lcd_modeset_plane_released();" + NL + "	(void)parity_edp_end(&res);")
# C: what stays held
t = rep(t, '	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16, "C: and a new modeset is refused while that is so");',
        """	CHECK(s.stop_unconfirmed == 1 && s.dc_off_held == 1 && s.crtc_domains_held == 4 && lcd.dbuf_enabled == 0x0f &&
	      (lcd_fake_reg(&lcd, 0x4438c) & 0x80000000u) != 0u && lcd.power_refs[POWER_DOMAIN_DC_OFF] == 1 &&
	      lcd.power_refs[POWER_DOMAIN_PIPE_A] == 1 && lcd.power_dropped_with_pipe_on == 0 && lcd.dbuf_shrunk_under_plane == 0,
	      "C: nothing was taken away from under the pipe: DC_OFF, the crtc's power domains, the DBUF slices and the MBUS joining all stay");
	CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -16, "C: and a new modeset is refused while that is so");
	parity_lcd_modeset_abandoned();""")
t = rep(t, '	printf("lcd_modeset_host_test: %u checks, %u failures' + BS + 'n", checks, failures);', """	/* ================= D. the commit's outer part ================= */
	{
		static struct parity_lcd_observer obs;
		uint32_t f0 = 0, f1 = 0;
		int dc, dom, mbus, dbuf1, dbox, pll, surf, dbuf2, put;
		unsigned i, n_dbuf = 0;

		bring_up();
		parity_lcd_observer_init(&obs, &lcd.ops, 0);
		lcd.on_observe = parity_lcd_observer_point; lcd.on_observe_ctx = &obs;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		parity_lcd_modeset_status(&s);
		printf("  D: cdclk: crtc min %d, bw min %d -> required %d kHz vco %d level %d, change needed %d | data rate %u MB/s" "\\n", s.cdclk_crtc_min,
		       s.cdclk_bw_min, s.cdclk_required_khz, s.cdclk_required_vco, s.cdclk_required_level, s.cdclk_change_needed, s.bw_data_rate);
		CHECK(rc == 0 && s.cdclk_crtc_min == 70400 && s.cdclk_bw_min == 11000 && s.cdclk_required_khz == 179200 && s.cdclk_required_vco == 537600 &&
		      s.cdclk_required_level == 0 && s.cdclk_change_needed == 0,
		      "D: CDCLK the reference computes for this state (pixel rate / 2, plane, bandwidth -> first table entry at 38.4 MHz) = the current 179200 kHz: its no-change path");
		CHECK(s.bw_data_rate == 564, "D: memory bandwidth of the one XRGB plane: 140.8 MHz x 4 bytes = 564 MB/s, below the allowed QGV point");
		rc = parity_lcd_modeset_commit_enable();
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && s.errors == 0 && lcd_fake_violations(&lcd) == 0, "D: the enable commit succeeds without a model violation");
		CHECK(s.dc_off_held == 0 && lcd.power_refs[POWER_DOMAIN_DC_OFF] == 0 && lcd.async_puts == 1 && lcd.last_async_delay_ms == 17,
		      "D: DC_OFF was held around the whole commit and dropped at its end the reference's way (asynchronous, 17 ms)");
		CHECK(s.crtc_domains_held == 4 && lcd.power_refs[POWER_DOMAIN_PIPE_A] == 1 && lcd.power_refs[POWER_DOMAIN_TRANSCODER_A] == 1 &&
		      lcd.power_refs[POWER_DOMAIN_PORT_DDI_LANES_A] == 1 && lcd.power_refs[POWER_DOMAIN_DISPLAY_CORE] == 1,
		      "D: get_crtc_power_domains(): pipe A, transcoder A, the encoder's DDI lanes, display core (shared DPLL) -- each held once");
		CHECK(lcd.power_refs[POWER_DOMAIN_PORT_DDI_IO_A] == 1 && lcd.power_refs[POWER_DOMAIN_AUX_IO_A] + lcd.power_refs[POWER_DOMAIN_AUX_A] == 1,
		      "D: next to them the encoder's own references (DDI IO, AUX): different owners, no double acquisition");
		CHECK(lcd.dbuf_enabled == 0x0f && s.dbuf_slices_now == 0x0f && s.mbus_joined_now == 1 &&
		      (lcd_fake_reg(&lcd, 0x4438c) & 0xfc000000u) == 0xdc000000u && (lcd_fake_reg(&lcd, 0x45008) & 0xc0070000u) == 0xc0030000u,
		      "D: DBUF: four slices on, MBUS joined (MBUS_CTL upper bits 0xdc = Linux's dump), tracker state service 3 (= dump's DBUF_CTL_S1)");
		printf("  D: PIPE_MBUS_DBOX_CTL_A=0x%08x MBUS_CTL=0x%08x DBUF_CTL_S1..4=0x%08x 0x%08x 0x%08x 0x%08x" "\\n", lcd_fake_reg(&lcd, 0x7003c),
		       lcd_fake_reg(&lcd, 0x4438c), lcd_fake_reg(&lcd, 0x45008), lcd_fake_reg(&lcd, 0x44fe8), lcd_fake_reg(&lcd, 0x44300), lcd_fake_reg(&lcd, 0x44304));
		CHECK(lcd_fake_reg(&lcd, 0x7003c) == 0x01038608u, "D: MBUS DBOX credits of ADL-P with joined MBUS (A 6, BW 2, B 8, B2B 16 / delay 1 / regulate)");
		dc = parity_lcd_trace_find(&trace, PARITY_LCD_T_POWER_GET, POWER_DOMAIN_DC_OFF, 0, 0);
		dom = parity_lcd_trace_find(&trace, PARITY_LCD_T_POWER_GET, POWER_DOMAIN_PIPE_A, 0, 0);
		mbus = parity_lcd_trace_find(&trace, PARITY_LCD_T_RMW, 0x4438c, 0, 0);
		dbuf1 = parity_lcd_trace_find(&trace, PARITY_LCD_T_DBUF, 0x0f, 0, 0);
		dbox = parity_lcd_trace_find(&trace, PARITY_LCD_T_WRITE, 0x7003c, 0, 0);
		pll = parity_lcd_trace_find(&trace, PARITY_LCD_T_WAIT, 0x46010, 0, 0);
		surf = parity_lcd_trace_find(&trace, PARITY_LCD_T_WRITE, 0x7019c, 0, 0);
		dbuf2 = parity_lcd_trace_find(&trace, PARITY_LCD_T_DBUF, 0x0f, 0, (unsigned)(surf > 0 ? surf : 0));
		put = parity_lcd_trace_find(&trace, PARITY_LCD_T_POWER_PUT, POWER_DOMAIN_DC_OFF, 0, 0);
		for (i = 0; i < trace.n; i++)
			n_dbuf += trace.e[i].kind == PARITY_LCD_T_DBUF;
		CHECK(dc >= 0 && dc < dom && dom < mbus && mbus < dbuf1 && dbuf1 < dbox && dbox < pll && pll < surf && surf < dbuf2 && dbuf2 < put &&
		      n_dbuf == 2 && trace.e[put].d == 1u && trace.dropped == 0,
		      "D: order from the run: DC_OFF < crtc domains < MBUS_CTL < DBUF slices (old|new) < DBOX < PLL ... plane arm < DBUF slices (new) < DC_OFF put");
		CHECK(lcd.nobs == 5 && lcd.obs[0] == PARITY_LCD_OBS_COMMIT_BEGIN && lcd.obs[1] == PARITY_LCD_OBS_UNDERRUN_ARM &&
		      lcd.obs[2] == PARITY_LCD_OBS_PIPE_ENABLED && lcd.obs[3] == PARITY_LCD_OBS_PLANE_ARMED && lcd.obs[4] == PARITY_LCD_OBS_COMMIT_END,
		      "D: the observer is called at the commit's points, the underrun clear at the reference's position inside the crtc enable");
		rc = parity_lcd_observer_frames(&obs, 500, 10, &f0, &f1);
		parity_lcd_observer_steady_begin(&obs);
		CHECK(rc == 0 && f1 - f0 >= 10u, "D: frames advance (from the frame counter)");
		rc = parity_lcd_observer_frames(&obs, 1000, 30, &f0, &f1);
		parity_lcd_observer_steady_sample(&obs);
		CHECK(rc == 0 && obs.seen_transition == 0u && obs.seen_steady == 0u && obs.vblank_unmasked_seen == 0 && obs.dropped == 0,
		      "D: no underrun in either period; the pipe's vblank interrupt was masked at every sample");
		parity_lcd_trace_init(&trace, &lcd.ops);
		rc = parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_status(&s);
		CHECK(rc == PARITY_LCD_MS_OK && s.crtc_domains_held == 0 && s.dc_off_held == 0 && lcd_fake_power_refs_total(&lcd) == 0 &&
		      lcd.dbuf_enabled == 0x01 && s.dbuf_slices_now == 0x01 && s.mbus_joined_now == 0 && (lcd_fake_reg(&lcd, 0x4438c) & 0x80000000u) == 0u &&
		      lcd_fake_violations(&lcd) == 0,
		      "D: the disable commit: domains returned, DBUF back to slice 1 only AFTER the pipe stopped, MBUS un-joined, no violation");
		{
			int off = parity_lcd_trace_find(&trace, PARITY_LCD_T_WAIT, 0x70008, 0, 0);
			int shrink = parity_lcd_trace_find(&trace, PARITY_LCD_T_DBUF, 0x01, 0, 0);

			CHECK(off >= 0 && shrink > off && parity_lcd_trace_find(&trace, PARITY_LCD_T_DBUF, 0x0f, 0, 0) > off,
			      "D: order from the run: the pipe-off wait comes before both DBUF updates (old|new, then new)");
		}
		CHECK(parity_lcd_observer_stopped(&obs, 100, &f0, &f1) == 0, "D: after the stop the frame counter stands still");
		parity_lcd_modeset_plane_released();
		CHECK(all_released("D"), "D: nothing held after the eDP ends");

		/* a missing setting is noticed (the model really depends on the new parts) */
		bring_up();
		lcd.fault_drop_dbuf_update = 1;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_armed_outside_slices == 1 && lcd_fake_violations(&lcd) == 1,
		      "model self-test: DBUF slice requests lost -> the plane's DDB lies in slices that are off: counted");
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);
		bring_up();
		lcd.fault_drop_write_reg = 0x4438c;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		CHECK(rc == PARITY_LCD_MS_OK && lcd.plane_armed_outside_slices == 1 && lcd_fake_violations(&lcd) == 1,
		      "model self-test: MBUS_CTL write lost -> a DDB beyond the un-joined half: counted");
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);

		/* what the commit cannot do is refused in the check phase */
		bring_up();
		cfg = ms_cfg(); cfg.cdclk_khz = 307200; cfg.cdclk_vco_khz = 614400;
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		parity_lcd_modeset_status(&s);
		CHECK(rc == -22 && trace.writes + trace.rmws == 0 && s.cdclk_change_needed == 1 && s.first_error != 0 && strstr(s.first_error, "CDCLK") != 0,
		      "a current CDCLK other than the required one (the reference would reprogram it) is refused with its reason, nothing touched");
		cfg = ms_cfg(); cfg.qgv_allowed_bw = 0;
		CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -22 && trace.writes + trace.rmws == 0, "an unknown memory bandwidth is refused, nothing touched");
		cfg = ms_cfg(); trace.ops.dbuf_slices_update = 0;
		CHECK(parity_lcd_modeset_prepare(&st, &cfg, &trace.ops) == -22, "a backend without the DBUF hook is refused");
		(void)parity_edp_end(&res);

		/* observation: progress is what the frame counter says, never elapsed time */
		bring_up();
		parity_lcd_observer_init(&obs, &lcd.ops, 0);
		lcd.on_observe = parity_lcd_observer_point; lcd.on_observe_ctx = &obs;
		lcd.fault_frame_counter_frozen = 1;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		{
			uint64_t t0 = dpf.now_us;

			CHECK(rc == PARITY_LCD_MS_OK && parity_lcd_observer_frames(&obs, 500, 10, &f0, &f1) == -110 && f0 == f1 && dpf.now_us - t0 >= 500000u,
			      "observation: half a second passed, the frame counter did not move -> NOT progress");
		}
		(void)parity_lcd_modeset_commit_disable();
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);

		/* observation: an underrun while the pipe starts is recorded as such, and is not lost by the clear */
		bring_up();
		parity_lcd_observer_init(&obs, &lcd.ops, 0);
		lcd.on_observe = parity_lcd_observer_point; lcd.on_observe_ctx = &obs;
		lcd.fault_underrun_at_arm = 1;
		*(uint32_t *)&lcd.regs[0].val |= 0u;
		cfg = ms_cfg();
		rc = parity_lcd_modeset_prepare(&st, &cfg, &trace.ops);
		rc = rc == 0 ? parity_lcd_modeset_commit_enable() : -99;
		(void)parity_lcd_observer_frames(&obs, 500, 10, &f0, &f1);
		parity_lcd_observer_steady_begin(&obs);
		(void)parity_lcd_observer_frames(&obs, 1000, 30, &f0, &f1);
		parity_lcd_observer_steady_sample(&obs);
		CHECK(rc == PARITY_LCD_MS_OK && (obs.seen_transition & 0x80000000u) != 0u && obs.seen_steady == 0u && lcd_fake_reg(&lcd, 0x70058) == 0u,
		      "observation: an underrun at the plane arm is kept in the START record; the steady record stays clean; the status was cleared after recording");
		/* ... and one while the picture stands goes to the steady record */
		lcd.fault_underrun_steady_after = (int)lcd.frame_reads_after_arm + 2;
		(void)parity_lcd_observer_frames(&obs, 1000, 30, &f0, &f1);
		parity_lcd_observer_steady_sample(&obs);
		(void)parity_lcd_modeset_commit_disable();
		CHECK((obs.seen_steady & 0x80000000u) != 0u && obs.n >= 8u && obs.dropped == 0,
		      "observation: an underrun during the steady picture is kept in the STEADY record (and survives the stop's samples)");
		parity_lcd_modeset_plane_released();
		(void)parity_edp_end(&res);
	}

	printf("lcd_modeset_host_test: %u checks, %u failures""" + BS + """n", checks, failures);""")
t = t.replace("		*(uint32_t *)&lcd.regs[0].val |= 0u;" + NL, "")
t = rep(t, '	CHECK(rc == PARITY_LCD_MS_OK && all_released("B3"), "B3: disable + released");', '	parity_lcd_modeset_plane_released();' + NL + '	CHECK(rc == PARITY_LCD_MS_OK && all_released("B3"), "B3: disable + released");')
save(T, t)

# ---- the GPU-free kernel test
k = load(L + "lcd_modeset_ktest.c")
k = rep(k, "	cfg.rawclk_khz = 19200u;" + NL, "	cfg.rawclk_khz = 19200u;" + NL +
        "	cfg.cdclk_khz = 179200u; cfg.cdclk_vco_khz = 537600u; cfg.cdclk_ref_khz = 38400u; cfg.cdclk_bypass_khz = 19200u;" + NL +
        "	cfg.cdclk_max_khz = 652800u; cfg.cdclk_voltage_level = 0u; cfg.mbus_joined = 0; cfg.qgv_allowed_bw = 11707u;" + NL)
k = rep(k, "	rc = parity_lcd_modeset_enable();" + NL + "	parity_lcd_modeset_status(&s);", "	rc = parity_lcd_modeset_commit_enable();" + NL + "	parity_lcd_modeset_status(&s);")
k = rep(k, "		lcd_fake_power_refs_total(&lcd) == 2," + NL +
        '		"lcd-ms: A-ORDER no ordering violation seen by the model; the enable owns the PLL and two power references");' + NL +
        "	rc = parity_lcd_modeset_plane_update();" + NL,
        "		lcd_fake_power_refs_total(&lcd) == 6 && s.crtc_domains_held == 4u && s.dc_off_held == 0 && lcd.async_puts == 1u &&" + NL +
        "		lcd.dbuf_enabled == 0x0fu && s.mbus_joined_now == 1 && s.cdclk_required_khz == 179200 && s.cdclk_change_needed == 0," + NL +
        '		"lcd-ms: A-ORDER no violation; PLL + DDI IO + AUX + the crtc\'s four domains held, DC_OFF dropped, DBUF 0xf joined, CDCLK unchanged");' + NL)
k = rep(k, "	rc = parity_lcd_modeset_plane_disable();" + NL + "	parity_lcd_modeset_status(&s);" + NL +
        '	check(rc == PARITY_LCD_MS_OK && s.plane_armed == 1, "lcd-ms: A-PLANE-OFF the modeset does not declare the buffer free on its own");' + NL +
        "	parity_lcd_modeset_plane_released();" + NL + "	rc = parity_lcd_modeset_disable();" + NL + "	parity_lcd_modeset_status(&s);" + NL,
        "	rc = parity_lcd_modeset_commit_disable();" + NL + "	parity_lcd_modeset_status(&s);" + NL +
        '	check(rc == PARITY_LCD_MS_OK && s.plane_armed == 1, "lcd-ms: A-PLANE-OFF the modeset does not declare the buffer free on its own");' + NL +
        "	parity_lcd_modeset_plane_released();" + NL)
k = rep(k, "(dpf.pp_control & 5u) == 0u && lcd_fake_violations(&lcd) == 0u,", "(dpf.pp_control & 5u) == 0u && lcd_fake_violations(&lcd) == 0u &&" + NL +
        "		s.crtc_domains_held == 0u && lcd.dbuf_enabled == 0x01u && s.mbus_joined_now == 0,")
k = rep(k, "	rc = rc == 0 ? parity_lcd_modeset_enable() : rc;" + NL + "	rc = rc == 0 ? parity_lcd_modeset_plane_update() : rc;" + NL,
        "	rc = rc == 0 ? parity_lcd_modeset_commit_enable() : rc;" + NL)
k = rep(k, "	(void)parity_lcd_modeset_plane_disable();" + NL + "	rc = rc == 0 ? parity_lcd_modeset_disable() : -99;",
        "	rc = rc == 0 ? parity_lcd_modeset_commit_disable() : -99;")
k = rep(k, "	check(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0 && s.plane_armed == 1 &&",
        "	check(rc == PARITY_LCD_MS_ERRORS && s.first_error != 0 && s.plane_armed == 1 && s.stop_unconfirmed == 1 && s.dc_off_held == 1 &&" + NL +
        "		s.crtc_domains_held == 4u && lcd.dbuf_enabled == 0x0fu &&")
k = rep(k, "	parity_lcd_modeset_plane_released();" + NL + "}", "	parity_lcd_modeset_abandoned();" + NL + "}")
save(L + "lcd_modeset_ktest.c", k)
print("done")

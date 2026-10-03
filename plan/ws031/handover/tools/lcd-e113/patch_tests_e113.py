#!/usr/bin/env python3
"""WS031 E-113: host checks for the enable sequence.  usage: <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
p = root + "plan/ws031/tests/lcd-host-test.c"
t = open(p).read()
old = TAB + 'printf("lcd_host_test: %u checks, %u failures\\n", checks, failures);'
assert t.count(old) == 1
new = r'''	/* ---- the modeset enable sequence, driven by the reference's own callers ---- */
	{
		static struct parity_lcd_words w;
		unsigned i, writes = 0, rmws = 0, steps = 0;
		int a, b, c, d, e, f, g, h2;
		uint32_t v = 0;

		rc = parity_lcd_compute(edid, d000, d700, 18, 38400, &s);
		rc = rc == 0 ? parity_lcd_emit_enable_sequence(&s, 0, 0, 0, 1920, 1080, 0, &w) : rc;
		for (i = 0; i < w.n; i++) {
			if (w.w[i].step != 0) {
				printf("  seq[%2u] STEP  %s\n", i, w.w[i].step);
				steps++;
			} else if (w.w[i].rmw) {
				printf("  seq[%2u] rmw   0x%05x clear=0x%08x set=0x%08x\n", i, w.w[i].reg, w.w[i].clear, w.w[i].value);
				rmws++;
			} else {
				printf("  seq[%2u] write 0x%05x = 0x%08x\n", i, w.w[i].reg, w.w[i].value);
				writes++;
			}
		}
		printf("  seq: %u entries = %u writes + %u rmw + %u steps not ported\n", w.n, writes, rmws, steps);
		CHECK(rc == 0 && w.overflow == 0 && w.n > 40, "the enable sequence is produced, nothing dropped");

		/* hsw_crtc_enable: pre_pll_enable -> shared DPLL -> pre_enable -> PIPESRC -> pipe misc -> cpu transcoder -> ... -> enable */
		a = parity_lcd_words_step(&w, "> intel_encoders_pre_pll_enable", 0);
		b = parity_lcd_words_step(&w, "intel_enable_shared_dpll", 0);
		c = parity_lcd_words_step(&w, "> intel_encoders_pre_enable", 0);
		d = parity_lcd_words_step(&w, "bdw_set_pipe_misc", 0);
		e = parity_lcd_words_step(&w, "intel_initial_watermarks", 0);
		f = parity_lcd_words_step(&w, "> intel_encoders_enable", 0);
		CHECK(a >= 0 && a < b && b < c && c < d && d < e && e < f, "hsw_crtc_enable: pre_pll_enable < shared DPLL < pre_enable < pipe misc < watermarks < encoder enable");
		for (i = 0, g = -1; i < w.n; i++)
			if (w.w[i].step == 0 && w.w[i].rmw == 0 && w.w[i].reg == 0x6001c)
				g = (int)i;
		for (i = 0, h2 = -1; i < w.n; i++)
			if (w.w[i].step == 0 && w.w[i].rmw == 0 && w.w[i].reg == 0x60030)
				h2 = (int)i;
		CHECK(g > c && g < d && h2 > d, "PIPESRC is written after the encoder pre_enable and BEFORE the pipe misc step and the cpu transcoder (M/N)");

		/* tgl_ddi_pre_enable_dp: panel power before the port clock, signal levels before training, MSA after training */
		a = parity_lcd_words_step(&w, "intel_pps_on", 0);
		b = parity_lcd_words_step(&w, "intel_ddi_enable_clock", 0);
		c = parity_lcd_words_step(&w, "encoder->set_signal_levels (icl_combo_phy_set_signal_levels)", 0);
		d = parity_lcd_words_step(&w, "intel_dp_start_link_train", 0);
		e = parity_lcd_words_step(&w, "intel_dp_stop_link_train", 0);
		CHECK(a >= 0 && a < b && b < c && c < d && d < e, "pre_enable: panel power on < DDI clock < signal levels < start link train < stop link train");
		for (i = 0, g = -1, h2 = -1; i < w.n; i++) {
			if (w.w[i].step == 0 && w.w[i].reg == 0x60400 && g < 0) g = (int)i;          /* first TRANS_DDI_FUNC_CTL write */
			if (w.w[i].step == 0 && w.w[i].reg == 0x60400) h2 = (int)i;                 /* last one */
		}
		CHECK(g >= 0 && g < c && w.w[g].value == 0x0a210002u, "TRANS_DDI_FUNC_CTL is first CONFIGURED without the enable bit (0x0a210002), before the signal levels / training");
		CHECK(h2 > g && w.w[h2].value == 0x8a210002u && h2 > parity_lcd_words_step(&w, "> intel_encoders_enable", 0) &&
		      h2 < parity_lcd_words_step(&w, "intel_enable_transcoder", 0),
		      "... and ENABLED (0x8a210002 = Linux's dump) in the encoder enable, before intel_enable_transcoder");
		CHECK(parity_lcd_words_find(&w, 0x60410, &v) == 1 && v == 1u, "one TRANS_MSA_MISC write");
		for (i = 0, g = -1; i < w.n; i++)
			if (w.w[i].step == 0 && w.w[i].reg == 0x60410) g = (int)i;
		CHECK(g > e && g < parity_lcd_words_step(&w, "intel_dsc_enable", 0), "MSA is written after link training, still inside the encoder pre_enable");
		/* the backlight comes last of all, after the transcoder is enabled and vblank is on */
		a = parity_lcd_words_step(&w, "intel_enable_transcoder", 0);
		b = parity_lcd_words_step(&w, "intel_crtc_vblank_on", 0);
		c = parity_lcd_words_step(&w, "intel_edp_backlight_on", 0);
		CHECK(a >= 0 && a < b && b < c, "encoder enable: transcoder on < vblank on < eDP backlight on");
		/* Type-C / HDMI / big-joiner / pre-TGL branches are not taken on this configuration */
		CHECK(parity_lcd_words_step(&w, "intel_tc_port_get_link", 0) < 0 && parity_lcd_words_step(&w, "intel_ddi_pre_enable_hdmi", 0) < 0 &&
		      parity_lcd_words_step(&w, "icl_ddi_bigjoiner_pre_enable", 0) < 0 && parity_lcd_words_step(&w, "hsw_ddi_pre_enable_dp", 0) < 0 &&
		      parity_lcd_words_step(&w, "intel_ddi_config_transcoder_dp2", 0) < 0,
		      "branches for Type-C, HDMI, big joiner, pre-TGL and DP 2.0 are not taken");
		CHECK(parity_lcd_emit_enable_sequence(&s, 3, 0, 0, 1920, 1080, 0, &w) == -22, "a Type-C port is refused");
	}

''' + old
t = t.replace(old, new)
open(p, "w").write(t)
print("patched lcd-host-test.c")

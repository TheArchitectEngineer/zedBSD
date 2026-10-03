#!/usr/bin/env python3
"""WS031 E-111a: host checks for the caller-driven cpu-transcoder words and the DDI words.  usage: <repo root>"""
import sys
NL, TAB = chr(10), chr(9)
root = sys.argv[1].rstrip("/") + "/"
def load(p): return open(root + p).read()
def save(p, s):
    open(root + p, "w").write(s); print("patched", p)
def rep(s, old, new):
    assert s.count(old) == 1, old[:90]
    return s.replace(old, new)

sh = load("plan/ws031/tests/run-lcd-host-test.sh")
sh = rep(sh, '"$D/intel_display_port.c intel_ddi_port.c intel_vrr_port.c"', '"$D/intel_display_port.c" "$D/intel_ddi_port.c" "$D/intel_vrr_port.c"')
save("plan/ws031/tests/run-lcd-host-test.sh", sh)

t = load("plan/ws031/tests/lcd-host-test.c")
old = TAB + 'printf("lcd_host_test: %u checks, %u failures\\n", checks, failures);'
new = r'''	/* ---- hsw_configure_cpu_transcoder(): the reference's own caller fixes the order between the writers ---- */
	{
		struct parity_lcd_words w;
		unsigned i;
		uint32_t v = 0xdeadbeef;

		rc = parity_lcd_compute(edid, d000, d700, 18, 38400, &s);
		rc = rc == 0 ? parity_lcd_emit_cpu_transcoder(&s, 0, 0, &w) : rc;
		for (i = 0; i < w.n; i++)
			printf("  cpu-transcoder[%2u] %s 0x%05x %s0x%08x%s\n", i, w.w[i].rmw ? "rmw  " : "write", w.w[i].reg,
			       w.w[i].rmw ? "set=" : "", w.w[i].value, w.w[i].rmw ? " (clear mask in .clear)" : "");
		CHECK(rc == 0 && w.overflow == 0 && w.n == 17, "hsw_configure_cpu_transcoder: 17 register operations, none dropped");
		/* M/N first (LINK_N last of the four), no M2/N2 on this platform, then the timings */
		CHECK(w.w[0].reg == 0x60030 && w.w[0].value == 0x7e4b17e4 && w.w[3].reg == 0x60044 && w.w[3].value == 0x00080000 &&
		      w.w[4].reg == 0x6007c && w.w[11].reg == 0x60014 && w.w[11].value == 0x0448043a,
		      "M/N (LINK_N last) then the timings, equal to Linux's dump; no M2/N2 set on display version 13");
		CHECK(parity_lcd_words_find(&w, 0x6001c, &v) == 0, "PIPESRC is NOT part of hsw_configure_cpu_transcoder (hsw_crtc_enable writes it afterwards)");
		/* VRR not in use: the ADL CHICKEN_TRANS bit is set by rmw (nothing cleared), TRANS_VRR_CTL is zeroed */
		CHECK(w.w[12].rmw == 1 && w.w[12].reg == 0x420c0 && w.w[12].clear == 0 && w.w[12].value == 0x80000000u,
		      "CHICKEN_TRANS_A: rmw sets PIPE_VBLANK_WITH_DELAY, clears nothing");
		CHECK(w.w[13].rmw == 0 && w.w[13].reg == 0x60420 && w.w[13].value == 0, "TRANS_VRR_CTL_A = 0 (no flipline)");
		CHECK(w.w[14].rmw == 0 && w.w[14].reg == 0x6002c && w.w[14].value == 0, "TRANS_MULT_A = pixel_multiplier - 1 = 0");
		CHECK(w.w[15].rmw == 1 && w.w[15].reg == 0x420c0 && w.w[15].clear == 0x18000000u && w.w[15].value == 0,
		      "CHICKEN_TRANS_A: frame start delay field cleared, (1 - 1) written");
		/* a modeset: the enable bit is not set here; Linux's dump 0xc0000000 = enable + state, progressive (pf-pd = 0) */
		CHECK(w.w[16].rmw == 0 && w.w[16].reg == 0x70008 && w.w[16].value == 0,
		      "TRANSCONF_A last, 0: progressive, no enable bit during a modeset (dump 0xc0000000 = enable | state afterwards)");
		rc = parity_lcd_emit_cpu_transcoder(&s, 1, 1, &w);
		CHECK(rc == 0 && w.w[12].reg == 0x420c4 && w.w[13].reg == 0x61420 && w.w[16].reg == 0x71008,
		      "transcoder B: CHICKEN_TRANS_B (not evenly spaced), VRR and TRANSCONF at +0x1000");
	}

	/* ---- the DDI side: MSA, TRANS_DDI_FUNC_CTL[2], and the DDI_BUF_CTL value ---- */
	{
		struct parity_lcd_words w;
		uint32_t buf = 0xdeadbeef, v = 0;
		unsigned i;

		rc = parity_lcd_compute(edid, d000, d700, 18, 38400, &s);
		rc = rc == 0 ? parity_lcd_emit_ddi(&s, 0, 0, 0, 0, &w, &buf) : rc;
		for (i = 0; i < w.n; i++)
			printf("  ddi[%u] write 0x%05x = 0x%08x\n", i, w.w[i].reg, w.w[i].value);
		printf("  ddi: DDI_BUF_CTL value (intel_dp->DP) = 0x%08x\n", buf);
		CHECK(rc == 0 && w.n == 3 && w.overflow == 0, "three DDI-side writes");
		/* Linux's dump: PIPE_DDI_FUNC_CTL_A 0x8a210002 = enable | DDI A (TGL+ encoding) | DP SST | 6 bpc | +HSync | x2 */
		CHECK(parity_lcd_words_find(&w, 0x60400, &v) == 1 && v == 0x8a210002u, "TRANS_DDI_FUNC_CTL_A equals Linux's dump (0x8a210002)");
		CHECK(w.w[1].reg == 0x60404 && w.w[1].value == 0 && w.w[2].reg == 0x60400, "TRANS_DDI_FUNC_CTL2 (no port sync) is written before TRANS_DDI_FUNC_CTL");
		/* DP_MSA_MISC_SYNC_CLOCK | 6 bpc (the dump has no MSA readout: the expected value is from the DP MSA field layout) */
		CHECK(w.w[0].reg == 0x60410 && w.w[0].value == 0x00000001u, "TRANS_MSA_MISC_A = synchronous clock, 6 bpc, RGB, no VSC SDP");
		/* Linux's dump: DDI_BUF_CTL_A 0x80000002 = the value here + DDI_BUF_CTL_ENABLE (set by the link-training preparation) */
		CHECK(buf == 0x00000002u && (buf | 0x80000000u) == 0x80000002u, "DDI_BUF_CTL value: x2, not reversed; + enable = Linux's dump (0x80000002)");
		/* 4 lanes / 8 bpc / both syncs negative / port B: every field moves */
		s.link.lanes = 4; s.link.bpp = 24; s.mode.hsync_positive = 0; s.mode.vsync_positive = 0;
		rc = parity_lcd_emit_ddi(&s, 1, 0, 0, 0x00010000u, &w, &buf);
		CHECK(rc == 0 && parity_lcd_words_find(&w, 0x60400, &v) == 1 && v == (0x80000000u | (2u << 27) | (2u << 24) | (0u << 20) | (3u << 1)),
		      "port B, 8 bpc, 4 lanes, -HSync -VSync: port select 2, bpc field 0, width 3, no sync bits");
		CHECK(buf == (0x00010000u | (3u << 1)) && w.w[0].value == (1u | (1u << 5)), "lane reversal kept from saved_port_bits; MSA 8 bpc");
		CHECK(parity_lcd_emit_ddi(&s, 3, 0, 0, 0, &w, &buf) == -22, "a Type-C port is refused (no TC state in this slice)");
	}

''' + old
t = rep(t, old, new)
save("plan/ws031/tests/lcd-host-test.c", t)

/*
 * WS031 host test: LCD-A first slice.  Input = the target panel's captured EDID / DPCD and the VBT's
 * colour depth; expected output = what Linux programmed on the same machine, read from
 * plan/ws031/display-ref/ (regs-selected.txt, README.md) -- NOT values produced by this code.
 *
 *   sh plan/ws031/tests/run-lcd-host-test.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parity_lcd_calc.h"

int parity_lcd_fmtcheck(const char *fmt, ...) { (void)fmt; return 0; }

static unsigned checks, failures;
#define CHECK(cond, msg) do { checks++; if (!(cond)) { failures++; printf("FAIL: %s (line %d)\n", msg, __LINE__); } } while (0)

static void slurp(const char *dir, const char *name, unsigned char *buf, size_t n)
{
	char path[512];
	FILE *f;

	snprintf(path, sizeof(path), "%s/%s", dir, name);
	f = fopen(path, "rb");
	if (!f || fread(buf, 1, n, f) != n) { perror(path); exit(2); }
	fclose(f);
}

/* E-124: the OpRegion unit is kernel-only in this build; the boundary the readout calls (notify of a
 * sanitized encoder) answers as a firmware without the SWSCI mailbox does. */
int parity_opregion_notify_encoder(int port, int output_type, int enable);
int parity_opregion_notify_encoder(int port, int output_type, int enable)
{
	(void)port; (void)output_type; (void)enable;
	return 0;
}

/* E-124: the kernel log of the driver; the host tests print nothing */
void kern_logf(const char *fmt, ...);
void kern_logf(const char *fmt, ...)
{
	(void)fmt;
}

/* E-124: the diagnostic trace flag of the N1 readout (the kernel build owns it) */
int parity_lcd_reg_trace;

int main(int argc, char **argv)
{
	static unsigned char edid[128], d000[256], d700[256], bad[128];
	struct parity_lcd_state s;
	int rc;

	if (argc < 2) { fprintf(stderr, "usage: %s <display-ref dir>\n", argv[0]); return 2; }
	slurp(argv[1], "edid-eDP-1.bin", edid, 128);
	slurp(argv[1], "dpcd-drm_dp_aux0-000.bin", d000, 256);
	slurp(argv[1], "dpcd-drm_dp_aux0-700.bin", d700, 256);

	/* VBT: 18 bpp (igt intel_vbt_decode); reference clock 38.4 MHz (non-SSC) on the target */
	rc = parity_lcd_compute(edid, d000, d700, 18, 38400, &s);
	printf("rc=%d mode %dx%d clock=%d kHz h %u/%u/%u/%u v %u/%u/%u/%u sync %c%c %ux%u mm dtd#%u bpc=%d | "
	       "link %d kHz x%d bpp=%d need=%d have=%d TU=%u M/N data %u/%u link %u/%u | pll ref=%d cfgcr0=0x%08x cfgcr1=0x%08x div0=0x%x\n",
	       rc, s.mode.hdisplay, s.mode.vdisplay, s.mode.clock_khz, s.mode.hdisplay, s.mode.hsync_start,
	       s.mode.hsync_end, s.mode.htotal, s.mode.vdisplay, s.mode.vsync_start, s.mode.vsync_end, s.mode.vtotal,
	       s.mode.hsync_positive ? '+' : '-', s.mode.vsync_positive ? '+' : '-', s.mode.width_mm, s.mode.height_mm,
	       s.mode.descriptor_index, s.mode.edid_bpc, s.link.rate_khz, s.link.lanes, s.link.bpp,
	       s.link.required_kbps, s.link.available_kbps, s.link.tu, s.link.data_m, s.link.data_n, s.link.link_m,
	       s.link.link_n, s.pll.ref_khz, s.pll.cfgcr0, s.pll.cfgcr1, s.pll.div0);
	CHECK(rc == 0, "the target's panel data computes");
	/* Linux: eDP-1 1920x1080@60.01, 140.8 MHz; TRANS_HTOTAL_A.. = 1920/2080, 1936/1952, 1080/1128, 1083/1097 */
	CHECK(s.mode.clock_khz == 140800 && s.mode.hdisplay == 1920 && s.mode.htotal == 2080 &&
	      s.mode.hsync_start == 1936 && s.mode.hsync_end == 1952, "horizontal timing == Linux's transcoder A values");
	CHECK(s.mode.vdisplay == 1080 && s.mode.vtotal == 1128 && s.mode.vsync_start == 1083 && s.mode.vsync_end == 1097,
	      "vertical timing == Linux's transcoder A values");
	/* Linux: 6 bpc on the DDI (TRANS_DDI_FUNC_CTL 0x8a210002), VBT 18 bpp */
	CHECK(s.mode.edid_bpc == 6 && s.link.bpp == 18, "6 bpc sink, 18 bpp pipe");
	/* Linux: DDI A x2 HBR; DPCD 0x100/0x101 = 0x0a / 0x02 */
	CHECK(s.link.rate_khz == 270000 && s.link.lanes == 2 && s.link.use_max_params == 1, "HBR x 2 lanes (use_max_params)");
	/* independent arithmetic: 140800 kHz x 18 bit / 8 = 316800 kBps; 2 lanes x 2.7 Gbit/s x 8/10 (8b/10b) / 8 = 540000 kBps */
	CHECK(s.link.required_kbps == 316800 && s.link.available_kbps == 540000, "316800 kBps needed of 540000 available");
	/* Linux regs: DATA_M1 0x7e4b17e4 (TU 64, M 0x4b17e4) DATA_N1 0x800000 LINK_M1 273406 LINK_N1 524288 */
	CHECK(s.link.tu == 64 && s.link.data_m == 0x4b17e4u && s.link.data_n == 0x800000u, "data M/N == Linux's PIPE_DATA_M1/N1");
	CHECK(s.link.link_m == 273406u && s.link.link_n == 524288u, "link M/N == Linux's PIPE_LINK_M1/N1");
	/* Linux regs: DPLL0 CFGCR0 0x00e001a5 CFGCR1 0x00000088 */
	CHECK(s.pll.cfgcr0 == 0x00e001a5u && s.pll.cfgcr1 == 0x00000088u && s.pll.div0 == 0,
	      "combo PLL words == Linux's DPLL0 CFGCR0 / CFGCR1 (the 38.4 MHz DCO-fraction workaround included)");
	CHECK(s.notes == 0, "the reference text had nothing to complain about");

	/* the same port clock on a 24 MHz reference takes the other table and no workaround */
	rc = parity_lcd_compute(edid, d000, d700, 18, 24000, &s);
	CHECK(rc == 0 && s.pll.cfgcr0 != 0x00e001a5u && (s.pll.cfgcr0 & 0x3ffu) == 0x151u,
	      "24 MHz reference: the 24 MHz table (DCO integer 0x151 for HBR), fraction not halved");

	/* a deeper VBT value does not raise the sink's depth; no VBT value leaves the sink's */
	rc = parity_lcd_compute(edid, d000, d700, 24, 38400, &s);
	CHECK(rc == 0 && s.link.bpp == 18, "VBT 24 bpp does not raise a 6 bpc sink");
	rc = parity_lcd_compute(edid, d000, d700, 0, 38400, &s);
	CHECK(rc == 0 && s.link.bpp == 18, "no VBT depth: the sink's 18 bpp");

	/* one lane of RBR cannot carry the mode */
	memcpy(bad, d000, 16);
	bad[1] = 0x06; bad[2] = 0x01;
	rc = parity_lcd_compute(edid, bad, d700, 18, 38400, &s);
	CHECK(rc == -28 && s.link.required_kbps > s.link.available_kbps, "RBR x1 is refused: the mode does not fit");
	bad[1] = 0x07;
	CHECK(parity_lcd_compute(edid, bad, d700, 18, 38400, &s) == -22, "an unknown DPCD link-rate code is refused");
	bad[1] = 0x0a; bad[2] = 0x03;
	CHECK(parity_lcd_compute(edid, bad, d700, 18, 38400, &s) == -22, "a lane count of 3 is refused");
	/* eDP 1.4 sinks use rate tables this slice does not port */
	memcpy(bad, d700, 16);
	bad[0] = 0x03;
	CHECK(parity_lcd_compute(edid, d000, bad, 18, 38400, &s) == -22, "an eDP 1.4 sink is refused (rate-select not ported), not approximated");
	/* an EDID without any detailed timing */
	memcpy(bad, edid, 128);
	memset(bad + 0x36, 0, 72);
	CHECK(parity_lcd_compute(bad, d000, d700, 18, 38400, &s) == -22, "an EDID with no detailed timing is refused");
	/* a detailed timing with a zero sync width is rejected by the reference, not repaired */
	memcpy(bad, edid, 128);
	bad[0x36 + 9] = 0; bad[0x36 + 11] &= 0xcf;      /* hsync pulse width: low byte and its two high bits */
	CHECK(parity_lcd_compute(bad, d000, d700, 18, 38400, &s) == -22, "a zero hsync pulse width is refused");
	/* an unknown port clock has no PLL table entry */
	CHECK(parity_lcd_compute(edid, d000, d700, 18, 19200, &s) == 0, "19.2 MHz reference uses the same table as 38.4 MHz");

	/* ---- the register words: the reference's writer functions against a recorder ---- */
	{
		/* Linux on the target (plan/ws031/display-ref/regs-selected.txt), transcoder A / pipe A */
		static const struct parity_lcd_regwrite want[] = {
			{ 0x60030, 0x7e4b17e4 },   /* PIPEA_DATA_M1: TU 64 | M */
			{ 0x60034, 0x00800000 },   /* PIPEA_DATA_N1 */
			{ 0x60040, 0x00042bfe },   /* PIPEA_LINK_M1 */
			{ 0x60044, 0x00080000 },   /* PIPEA_LINK_N1 -- written last: it arms the M/N update */
			{ 0x6007c, 0x00000000 },   /* TRANS_SET_CONTEXT_LATENCY: vblank start - vdisplay (not in the dump; 0 by the mode) */
			{ 0x60028, 0x00000000 },   /* VSYNCSHIFT_A */
			{ 0x60000, 0x081f077f },   /* HTOTAL_A */
			{ 0x60004, 0x081f077f },   /* HBLANK_A */
			{ 0x60008, 0x079f078f },   /* HSYNC_A */
			{ 0x6000c, 0x04670437 },   /* VTOTAL_A */
			{ 0x60010, 0x04670000 },   /* VBLANK_A: the start field is unused on ADL+ and written as 1 - 1 */
			{ 0x60014, 0x0448043a },   /* VSYNC_A */
			{ 0x6001c, 0x077f0437 },   /* PIPEASRC */
		};
		struct parity_lcd_words words;
		unsigned i, same = 1;

		rc = parity_lcd_compute(edid, d000, d700, 18, 38400, &s);
		rc = rc == 0 ? parity_lcd_emit_transcoder(&s, 0, 0, 1920, 1080, &words) : rc;
		for (i = 0; i < words.n; i++)
			printf("  write[%2u] 0x%05x = 0x%08x\n", i, words.w[i].reg, words.w[i].value);
		CHECK(rc == 0 && words.n == sizeof(want) / sizeof(want[0]) && words.overflow == 0, "13 register writes, none dropped");
		for (i = 0; i < words.n && i < sizeof(want) / sizeof(want[0]); i++)
			if (words.w[i].reg != want[i].reg || words.w[i].value != want[i].value)
				same = 0;
		CHECK(same, "transcoder A M/N, timing and PIPESRC words == Linux's register dump, in the reference's write order");
		CHECK(words.n >= 4 && words.w[3].reg == 0x60044, "LINK_N is the last of the four M/N writes");
		/* transcoder B is the same words one register block up */
		rc = parity_lcd_emit_transcoder(&s, 1, 1, 1920, 1080, &words);
		CHECK(rc == 0 && words.w[6].reg == 0x61000 && words.w[6].value == 0x081f077f && words.w[12].reg == 0x6101c,
		      "transcoder B / pipe B: the same words at +0x1000");
		CHECK(parity_lcd_emit_transcoder(&s, 0, 4, 1920, 1080, &words) == -22 &&
		      parity_lcd_emit_transcoder(&s, 0, 0, 0, 1080, &words) == -22, "a transcoder outside A..D or an empty source is refused");
	}

	/* ---- hsw_configure_cpu_transcoder(): the reference's own caller fixes the order between the writers ---- */
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
		CHECK(parity_lcd_words_find(&w, 0x6001c, &v) == 0, "PIPESRC is NOT part of hsw_configure_cpu_transcoder (hsw_crtc_enable writes it, and the pipe misc word, BEFORE calling it)");
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

	/* ---- the universal plane: one full-screen primary plane on the scanout buffer's layout ---- */
	{
		struct parity_lcd_words w;
		uint32_t v = 0;
		unsigned i, surf_at = 99, ctl_at = 99, wm_at = 99, color_at = 99;
		const uint32_t XR24 = 0x34325258u;      /* 'X','R','2','4' */

		rc = parity_lcd_emit_plane(0, 0, XR24, 0, 1920, 1080, 7680, 0xfdfc0000u, &w);
		for (i = 0; i < w.n; i++) {
			if (w.w[i].step != 0)
				printf("  plane[%2u] STEP  %s (reference callee, not ported)\n", i, w.w[i].step);
			else
				printf("  plane[%2u] write 0x%05x = 0x%08x\n", i, w.w[i].reg, w.w[i].value);
			if (w.w[i].step == 0 && w.w[i].reg == 0x7027c) wm_at = i;      /* PLANE_BUF_CFG: the last write of skl_write_plane_wm() */
			if (w.w[i].step == 0 && w.w[i].reg == 0x7019c) surf_at = i;
			if (w.w[i].step == 0 && w.w[i].reg == 0x70180) ctl_at = i;
			if (w.w[i].step == 0 && w.w[i].reg == 0x701cc) color_at = i;
		}
		CHECK(rc == 0 && w.overflow == 0, "plane words produced, none dropped");
		/* Linux's dump (regs-selected.txt): DSPACNTR 0x94000000, DSPASTRIDE 0x78, 0x70190 0x0437077f, 0x701cc 0x2000 */
		CHECK(parity_lcd_words_find(&w, 0x70180, &v) == 1 && v == 0x94000000u,
		      "PLANE_CTL_1_A = enable | XRGB8888 | ADL-P arbitration slots for 4 bytes/pixel = Linux's dump (0x94000000)");
		CHECK(parity_lcd_words_find(&w, 0x70188, &v) == 1 && v == 0x78u, "PLANE_STRIDE = pitch / 64 = 120 = Linux's dump");
		CHECK(parity_lcd_words_find(&w, 0x70190, &v) == 1 && v == 0x0437077fu, "PLANE_SIZE = (1080-1) << 16 | (1920-1) = Linux's dump");
		CHECK(parity_lcd_words_find(&w, 0x7018c, &v) == 1 && v == 0u, "PLANE_POS = 0,0 (dump 0)");
		CHECK(parity_lcd_words_find(&w, 0x701cc, &v) == 1 && v == 0x00002000u, "PLANE_COLOR_CTL = plane gamma disable, alpha disabled (no alpha channel) = Linux's dump");
		CHECK(parity_lcd_words_find(&w, 0x7019c, &v) == 1 && v == 0xfdfc0000u, "PLANE_SURF = the scanout buffer's GGTT address (Linux's differs: its own buffer)");
		CHECK(parity_lcd_words_find(&w, 0x701a4, &v) == 1 && v == 0u && parity_lcd_words_find(&w, 0x701c0, &v) == 1 && v == 0u,
		      "PLANE_OFFSET 0 and PLANE_AUX_DIST 0 (no aux plane; ADL-P is not a flat-CCS device so the register is written)");
		CHECK(parity_lcd_words_find(&w, 0x701a0, &v) == 1 && v == 0xff000000u && parity_lcd_words_find(&w, 0x70198, &v) == 1 && v == 0u,
		      "colour key off: KEYMAX carries plane alpha 0xff, KEYMSK 0 (opaque, so no alpha-enable bit)");
		CHECK(parity_lcd_words_find(&w, 0x701c8, &v) == 1 && v == 0u, "PLANE_CUS_CTL = 0 (the primary plane is an HDR plane; no chroma upsampler for RGB)");
		CHECK(surf_at == w.n - 1 && ctl_at == w.n - 2, "PLANE_CTL then PLANE_SURF are the last two operations (the surface write arms the update)");
		CHECK(wm_at != 99 && color_at != 99 && wm_at > color_at && wm_at < ctl_at,
		      "skl_write_plane_wm() runs for real between PLANE_COLOR_CTL and the arm (here with an empty watermark state: the words-only API computes none)");
		/* pipe B: the same block one pipe up */
		rc = parity_lcd_emit_plane(1, 0, XR24, 0, 1920, 1080, 7680, 0x00100000u, &w);
		CHECK(rc == 0 && parity_lcd_words_find(&w, 0x71180, &v) == 1 && v == 0x94000000u && parity_lcd_words_find(&w, 0x7119c, &v) == 1 && v == 0x00100000u,
		      "pipe B: PLANE_CTL_1_B / PLANE_SURF_1_B");
		/* what this slice does not cover is refused before the reference code runs */
		CHECK(parity_lcd_emit_plane(0, 0, 0x34324241u /* AB24 */, 0, 1920, 1080, 7680, 0x100000u, &w) == -22 &&
		      parity_lcd_emit_plane(0, 0, XR24, 0x0100000000000002ull /* I915 Y-tiled */, 1920, 1080, 7680, 0x100000u, &w) == -22 &&
		      parity_lcd_emit_plane(0, 1, XR24, 0, 1920, 1080, 7680, 0x100000u, &w) == -22 &&
		      parity_lcd_emit_plane(0, 0, XR24, 0, 1920, 1080, 7000, 0x100000u, &w) == -22 &&
		      parity_lcd_emit_plane(0, 0, XR24, 0, 1920, 1080, 7680, 0x100800u, &w) == -22,
		      "another format, a tiled modifier, a sprite plane, a pitch that is not x64 and an unaligned surface are refused");
	}

	/* the modeset enable / disable sequence is exercised end to end by lcd-modeset-host-test.c */

	printf("lcd_host_test: %u checks, %u failures\n", checks, failures);
	return failures != 0;
}

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the LCD-A first slice (display/state.c: the state
 * calculation and the register words of the modeset writers).
 *
 * The input is the target panel's captured EDID and DPCD and the VBT's
 * colour depth; the expected output is what Linux programmed on the same
 * machine, read from plan/ws031/display-ref/ (regs-selected.txt,
 * README.md) -- not values produced by this code.
 *
 *   sh plan/ws031/tests/run-lcd-host-test.sh
 */

#include "host-test.h"

#include "../../display/internal.h"
#include "../../display/modeset.h"
#include "../../display/state.h"
#include "../../display/watermark.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The fourcc of XRGB8888 ('X', 'R', '2', '4'). */
#define I915_LCD_TEST_XR24 0x34325258U

/*
 * The captured panel: the EDID base block, DPCD pages 0x000 and 0x700, and
 * a scratch copy the refusal cases edit.
 *
 * Read once by main(); only the scratch copy changes.
 */
static uint8_t i915_lcd_edid[128];
static uint8_t i915_lcd_d000[256];
static uint8_t i915_lcd_d700[256];
static uint8_t i915_lcd_bad[128];

/*
 * The display that owns the modeset world, and the world.
 *
 * Created by main() for the whole run.
 */
static struct i915_display i915_lcd_display;
static struct i915_lcd_world *i915_lcd_world;

/*
 * Linux's transcoder A / pipe A words on the target (display-ref/regs-selected.txt),
 * in the reference's write order.
 */
static const struct i915_lcd_regwrite i915_lcd_linux_transcoder[] = {
	{ 0x60030, 0x7e4b17e4, 0, 0, NULL },   /* PIPEA_DATA_M1: TU 64 | M */
	{ 0x60034, 0x00800000, 0, 0, NULL },   /* PIPEA_DATA_N1 */
	{ 0x60040, 0x00042bfe, 0, 0, NULL },   /* PIPEA_LINK_M1 */
	{ 0x60044, 0x00080000, 0, 0, NULL },   /* PIPEA_LINK_N1 -- written last: it arms the M/N update */
	{ 0x6007c, 0x00000000, 0, 0, NULL },   /* TRANS_SET_CONTEXT_LATENCY: vblank start - vdisplay (not in the dump; 0 by the mode) */
	{ 0x60028, 0x00000000, 0, 0, NULL },   /* VSYNCSHIFT_A */
	{ 0x60000, 0x081f077f, 0, 0, NULL },   /* HTOTAL_A */
	{ 0x60004, 0x081f077f, 0, 0, NULL },   /* HBLANK_A */
	{ 0x60008, 0x079f078f, 0, 0, NULL },   /* HSYNC_A */
	{ 0x6000c, 0x04670437, 0, 0, NULL },   /* VTOTAL_A */
	{ 0x60010, 0x04670000, 0, 0, NULL },   /* VBLANK_A: the start field is unused on ADL+ and written as 1 - 1 */
	{ 0x60014, 0x0448043a, 0, 0, NULL },   /* VSYNC_A */
	{ 0x6001c, 0x077f0437, 0, 0, NULL },   /* PIPEASRC */
};

static void i915_lcd_test_compute(void);
static void i915_lcd_test_refusals(void);
static void i915_lcd_test_transcoder(void);
static void i915_lcd_test_cpu_transcoder(void);
static void i915_lcd_test_ddi(void);
static void i915_lcd_test_plane(void);
static int i915_lcd_same_words(const struct i915_lcd_words *words, const struct i915_lcd_regwrite *want, unsigned count);

/*
 * Computes the target panel's state and words and compares them with Linux.
 *
 * Usage: host-lcd-test <display-ref directory> [-v]
 */
int
main(
	int argc,
	char **argv)
{
	int error;
	int status;

	/* Needs the reference directory; a second argument makes the run verbose. */
	if (argc < 2) {
		fprintf(stderr, "usage: %s <display-ref dir>\n", argv[0]);
		return 2;
	}

	if (argc > 2)
		i915_host_verbose = 1;

	/* Reads the captured panel. */
	i915_host_read_reference(argv[1], "edid-eDP-1.bin", i915_lcd_edid, sizeof(i915_lcd_edid));
	i915_host_read_reference(argv[1], "dpcd-drm_dp_aux0-000.bin", i915_lcd_d000, sizeof(i915_lcd_d000));
	i915_host_read_reference(argv[1], "dpcd-drm_dp_aux0-700.bin", i915_lcd_d700, sizeof(i915_lcd_d700));

	/* Creates the modeset world the calculation runs in, and the watermark world its plane writer reads. */
	error = drv_i915_lcd_world_create(&i915_lcd_display);
	if (error != 0) {
		printf("lcd world: error %d\n", error);
		return 2;
	}

	error = drv_i915_wm_world_create(&i915_lcd_display);
	if (error != 0) {
		printf("wm world: error %d\n", error);
		return 2;
	}

	i915_lcd_world = i915_lcd_display.lcd_world;

	/* Runs the cases. */
	i915_lcd_test_compute();
	i915_lcd_test_refusals();
	i915_lcd_test_transcoder();
	i915_lcd_test_cpu_transcoder();
	i915_lcd_test_ddi();
	i915_lcd_test_plane();

	/* Releases the worlds. */
	drv_i915_wm_world_destroy(&i915_lcd_display);
	drv_i915_lcd_world_destroy(&i915_lcd_display);

	/* Reports the tally. */
	status = i915_host_report("lcd_host_test");
	if (status != 0)
		return status;

	/* Succeeded: every check passed. */
	return 0;
}

/* The target's mode, link, M/N and PLL values against Linux's. */
static void
i915_lcd_test_compute(void)
{
	struct i915_lcd_state s;
	int error;

	/* VBT: 18 bpp (igt intel_vbt_decode); reference clock 38.4 MHz (non-SSC) on the target. */
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_d700, 18, 38400, &s);
	printf("rc=%d mode %dx%d clock=%d kHz h %u/%u/%u/%u v %u/%u/%u/%u sync %c%c %ux%u mm dtd#%u bpc=%d | "
	       "link %d kHz x%d bpp=%d need=%d have=%d TU=%u M/N data %u/%u link %u/%u | pll ref=%d cfgcr0=0x%08x cfgcr1=0x%08x div0=0x%x\n",
	       error,
	       s.mode.hdisplay,
	       s.mode.vdisplay,
	       s.mode.clock_khz,
	       s.mode.hdisplay,
	       s.mode.hsync_start,
	       s.mode.hsync_end,
	       s.mode.htotal,
	       s.mode.vdisplay,
	       s.mode.vsync_start,
	       s.mode.vsync_end,
	       s.mode.vtotal,
	       s.mode.hsync_positive ? '+' : '-',
	       s.mode.vsync_positive ? '+' : '-',
	       s.mode.width_mm,
	       s.mode.height_mm,
	       s.mode.descriptor_index,
	       s.mode.edid_bpc,
	       s.link.rate_khz,
	       s.link.lanes,
	       s.link.bpp,
	       s.link.required_kbps,
	       s.link.available_kbps,
	       s.link.tu,
	       s.link.data_m,
	       s.link.data_n,
	       s.link.link_m,
	       s.link.link_n,
	       s.pll.ref_khz,
	       s.pll.cfgcr0,
	       s.pll.cfgcr1,
	       s.pll.div0);
	i915_host_check(error == 0, "the target's panel data computes");

	/* Linux: eDP-1 1920x1080@60.01, 140.8 MHz; TRANS_HTOTAL_A.. = 1920/2080, 1936/1952, 1080/1128, 1083/1097. */
	i915_host_check(s.mode.clock_khz == 140800 &&
			s.mode.hdisplay == 1920U &&
			s.mode.htotal == 2080U &&
			s.mode.hsync_start == 1936U &&
			s.mode.hsync_end == 1952U,
			"horizontal timing == Linux's transcoder A values");
	i915_host_check(s.mode.vdisplay == 1080U &&
			s.mode.vtotal == 1128U &&
			s.mode.vsync_start == 1083U &&
			s.mode.vsync_end == 1097U,
			"vertical timing == Linux's transcoder A values");

	/* Linux: 6 bpc on the DDI (TRANS_DDI_FUNC_CTL 0x8a210002), VBT 18 bpp. */
	i915_host_check(s.mode.edid_bpc == 6 && s.link.bpp == 18, "6 bpc sink, 18 bpp pipe");

	/* Linux: DDI A x2 HBR; DPCD 0x100/0x101 = 0x0a / 0x02. */
	i915_host_check(s.link.rate_khz == 270000 && s.link.lanes == 2 && s.link.use_max_params == 1, "HBR x 2 lanes (use_max_params)");

	/* Independent arithmetic: 140800 kHz x 18 bit / 8 = 316800 kBps; 2 lanes x 2.7 Gbit/s x 8/10 / 8 = 540000 kBps. */
	i915_host_check(s.link.required_kbps == 316800 && s.link.available_kbps == 540000, "316800 kBps needed of 540000 available");

	/* Linux regs: DATA_M1 0x7e4b17e4 (TU 64, M 0x4b17e4) DATA_N1 0x800000 LINK_M1 273406 LINK_N1 524288. */
	i915_host_check(s.link.tu == 64U && s.link.data_m == 0x4b17e4U && s.link.data_n == 0x800000U,
			"data M/N == Linux's PIPE_DATA_M1/N1");
	i915_host_check(s.link.link_m == 273406U && s.link.link_n == 524288U, "link M/N == Linux's PIPE_LINK_M1/N1");

	/* Linux regs: DPLL0 CFGCR0 0x00e001a5 CFGCR1 0x00000088. */
	i915_host_check(s.pll.cfgcr0 == 0x00e001a5U && s.pll.cfgcr1 == 0x00000088U && s.pll.div0 == 0U,
			"combo PLL words == Linux's DPLL0 CFGCR0 / CFGCR1 (the 38.4 MHz DCO-fraction workaround included)");
	i915_host_check(s.notes == 0U, "the reference text had nothing to complain about");

	/* The same port clock on a 24 MHz reference takes the other table and no workaround. */
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_d700, 18, 24000, &s);
	i915_host_check(error == 0 && s.pll.cfgcr0 != 0x00e001a5U && (s.pll.cfgcr0 & 0x3ffU) == 0x151U,
			"24 MHz reference: the 24 MHz table (DCO integer 0x151 for HBR), fraction not halved");

	/* A deeper VBT value does not raise the sink's depth; no VBT value leaves the sink's. */
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_d700, 24, 38400, &s);
	i915_host_check(error == 0 && s.link.bpp == 18, "VBT 24 bpp does not raise a 6 bpc sink");
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_d700, 0, 38400, &s);
	i915_host_check(error == 0 && s.link.bpp == 18, "no VBT depth: the sink's 18 bpp");
}

/* Panel data the calculation must refuse rather than approximate. */
static void
i915_lcd_test_refusals(void)
{
	struct i915_lcd_state s;
	int error;

	/* One lane of RBR cannot carry the mode. */
	memcpy(i915_lcd_bad, i915_lcd_d000, 16U);
	i915_lcd_bad[1] = 0x06U;
	i915_lcd_bad[2] = 0x01U;
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_bad, i915_lcd_d700, 18, 38400, &s);
	i915_host_check(error == ENOSPC && s.link.required_kbps > s.link.available_kbps, "RBR x1 is refused: the mode does not fit");

	/* An unknown link-rate code and a lane count of 3. */
	i915_lcd_bad[1] = 0x07U;
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_bad, i915_lcd_d700, 18, 38400, &s);
	i915_host_check(error == EINVAL, "an unknown DPCD link-rate code is refused");
	i915_lcd_bad[1] = 0x0aU;
	i915_lcd_bad[2] = 0x03U;
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_bad, i915_lcd_d700, 18, 38400, &s);
	i915_host_check(error == EINVAL, "a lane count of 3 is refused");

	/* eDP 1.4 sinks use rate tables this slice does not port. */
	memcpy(i915_lcd_bad, i915_lcd_d700, 16U);
	i915_lcd_bad[0] = 0x03U;
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_bad, 18, 38400, &s);
	i915_host_check(error == EINVAL, "an eDP 1.4 sink is refused (rate-select not ported), not approximated");

	/* An EDID without any detailed timing. */
	memcpy(i915_lcd_bad, i915_lcd_edid, 128U);
	memset(i915_lcd_bad + 0x36, 0, 72U);
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_bad, i915_lcd_d000, i915_lcd_d700, 18, 38400, &s);
	i915_host_check(error == EINVAL, "an EDID with no detailed timing is refused");

	/* A detailed timing with a zero hsync pulse width (low byte and its two high bits) is rejected, not repaired. */
	memcpy(i915_lcd_bad, i915_lcd_edid, 128U);
	i915_lcd_bad[0x36 + 9] = 0U;
	i915_lcd_bad[0x36 + 11] &= 0xcfU;
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_bad, i915_lcd_d000, i915_lcd_d700, 18, 38400, &s);
	i915_host_check(error == EINVAL, "a zero hsync pulse width is refused");

	/* 19.2 MHz shares the 38.4 MHz table. */
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_d700, 18, 19200, &s);
	i915_host_check(error == 0, "19.2 MHz reference uses the same table as 38.4 MHz");
}

/* The transcoder M/N, timing and pipe-source words against Linux's register dump. */
static void
i915_lcd_test_transcoder(void)
{
	struct i915_lcd_state s;
	struct i915_lcd_words words;
	unsigned want_count;
	unsigned i;
	int error;
	int same;

	/* Records the words of transcoder A / pipe A for the 1920x1080 source. */
	want_count = sizeof(i915_lcd_linux_transcoder) / sizeof(i915_lcd_linux_transcoder[0]);
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_d700, 18, 38400, &s);
	if (error == 0)
		error = drv_i915_lcd_emit_transcoder(i915_lcd_world, &s, 0, 0, 1920U, 1080U, &words);

	for (i = 0U; i < words.n; i++)
		printf("  write[%2u] 0x%05x = 0x%08x\n", i, words.w[i].reg, words.w[i].value);

	i915_host_check(error == 0 && words.n == want_count && words.overflow == 0U, "13 register writes, none dropped");

	/* The words are Linux's, in the reference's write order. */
	same = i915_lcd_same_words(&words, i915_lcd_linux_transcoder, want_count);
	i915_host_check(same, "transcoder A M/N, timing and PIPESRC words == Linux's register dump, in the reference's write order");
	i915_host_check(words.n >= 4U && words.w[3].reg == 0x60044U, "LINK_N is the last of the four M/N writes");

	/* Transcoder B is the same words one register block up. */
	error = drv_i915_lcd_emit_transcoder(i915_lcd_world, &s, 1, 1, 1920U, 1080U, &words);
	i915_host_check(error == 0 &&
			words.w[6].reg == 0x61000U &&
			words.w[6].value == 0x081f077fU &&
			words.w[12].reg == 0x6101cU,
			"transcoder B / pipe B: the same words at +0x1000");

	/* A transcoder outside A..D and an empty source are refused. */
	error = drv_i915_lcd_emit_transcoder(i915_lcd_world, &s, 0, 4, 1920U, 1080U, &words);
	if (error == EINVAL)
		error = drv_i915_lcd_emit_transcoder(i915_lcd_world, &s, 0, 0, 0U, 1080U, &words);

	i915_host_check(error == EINVAL, "a transcoder outside A..D or an empty source is refused");
}

/* hsw_configure_cpu_transcoder(): the reference's own caller fixes the order between the writers. */
static void
i915_lcd_test_cpu_transcoder(void)
{
	struct i915_lcd_state s;
	struct i915_lcd_words w;
	uint32_t value;
	unsigned hits;
	unsigned i;
	int error;

	/* Records the operations for transcoder A. */
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_d700, 18, 38400, &s);
	if (error == 0)
		error = drv_i915_lcd_emit_cpu_transcoder(i915_lcd_world, &s, 0, 0, &w);

	for (i = 0U; i < w.n; i++) {
		printf("  cpu-transcoder[%2u] %s 0x%05x %s0x%08x%s\n",
		       i,
		       w.w[i].rmw ? "rmw  " : "write",
		       w.w[i].reg,
		       w.w[i].rmw ? "set=" : "",
		       w.w[i].value,
		       w.w[i].rmw ? " (clear mask in .clear)" : "");
	}

	i915_host_check(error == 0 && w.overflow == 0U && w.n == 17U, "hsw_configure_cpu_transcoder: 17 register operations, none dropped");

	/* M/N first (LINK_N last of the four), no M2/N2 on this platform, then the timings. */
	i915_host_check(w.w[0].reg == 0x60030U &&
			w.w[0].value == 0x7e4b17e4U &&
			w.w[3].reg == 0x60044U &&
			w.w[3].value == 0x00080000U &&
			w.w[4].reg == 0x6007cU &&
			w.w[11].reg == 0x60014U &&
			w.w[11].value == 0x0448043aU,
			"M/N (LINK_N last) then the timings, equal to Linux's dump; no M2/N2 set on display version 13");

	/* hsw_crtc_enable writes PIPESRC and the pipe misc word before calling it. */
	value = 0xdeadbeefU;
	hits = drv_i915_lcd_words_find(&w, 0x6001cU, &value);
	i915_host_check(hits == 0U,
			"PIPESRC is NOT part of hsw_configure_cpu_transcoder (hsw_crtc_enable writes it, and the pipe misc word, BEFORE calling it)");

	/* VRR not in use: the ADL CHICKEN_TRANS bit is set by rmw (nothing cleared), TRANS_VRR_CTL is zeroed. */
	i915_host_check(w.w[12].rmw == 1U &&
			w.w[12].reg == 0x420c0U &&
			w.w[12].clear == 0U &&
			w.w[12].value == 0x80000000U,
			"CHICKEN_TRANS_A: rmw sets PIPE_VBLANK_WITH_DELAY, clears nothing");
	i915_host_check(w.w[13].rmw == 0U && w.w[13].reg == 0x60420U && w.w[13].value == 0U, "TRANS_VRR_CTL_A = 0 (no flipline)");
	i915_host_check(w.w[14].rmw == 0U && w.w[14].reg == 0x6002cU && w.w[14].value == 0U, "TRANS_MULT_A = pixel_multiplier - 1 = 0");
	i915_host_check(w.w[15].rmw == 1U &&
			w.w[15].reg == 0x420c0U &&
			w.w[15].clear == 0x18000000U &&
			w.w[15].value == 0U,
			"CHICKEN_TRANS_A: frame start delay field cleared, (1 - 1) written");

	/* A modeset: the enable bit is not set here; Linux's dump 0xc0000000 = enable + state, progressive. */
	i915_host_check(w.w[16].rmw == 0U && w.w[16].reg == 0x70008U && w.w[16].value == 0U,
			"TRANSCONF_A last, 0: progressive, no enable bit during a modeset (dump 0xc0000000 = enable | state afterwards)");

	/* Transcoder B: CHICKEN_TRANS_B is not evenly spaced; VRR and TRANSCONF move by 0x1000. */
	error = drv_i915_lcd_emit_cpu_transcoder(i915_lcd_world, &s, 1, 1, &w);
	i915_host_check(error == 0 &&
			w.w[12].reg == 0x420c4U &&
			w.w[13].reg == 0x61420U &&
			w.w[16].reg == 0x71008U,
			"transcoder B: CHICKEN_TRANS_B (not evenly spaced), VRR and TRANSCONF at +0x1000");
}

/* The DDI side: MSA, TRANS_DDI_FUNC_CTL[2], and the DDI_BUF_CTL value. */
static void
i915_lcd_test_ddi(void)
{
	struct i915_lcd_state s;
	struct i915_lcd_words w;
	uint32_t buf;
	uint32_t value;
	unsigned hits;
	unsigned i;
	int error;

	/* Records the words for port A, pipe A, transcoder A. */
	buf = 0xdeadbeefU;
	value = 0U;
	error = drv_i915_lcd_compute(i915_lcd_world, i915_lcd_edid, i915_lcd_d000, i915_lcd_d700, 18, 38400, &s);
	if (error == 0)
		error = drv_i915_lcd_emit_ddi(i915_lcd_world, &s, 0, 0, 0, 0U, &w, &buf);

	for (i = 0U; i < w.n; i++)
		printf("  ddi[%u] write 0x%05x = 0x%08x\n", i, w.w[i].reg, w.w[i].value);

	printf("  ddi: DDI_BUF_CTL value (intel_dp->DP) = 0x%08x\n", buf);
	i915_host_check(error == 0 && w.n == 3U && w.overflow == 0U, "three DDI-side writes");

	/* Linux's dump: PIPE_DDI_FUNC_CTL_A 0x8a210002 = enable | DDI A (TGL+ encoding) | DP SST | 6 bpc | +HSync | x2. */
	hits = drv_i915_lcd_words_find(&w, 0x60400U, &value);
	i915_host_check(hits == 1U && value == 0x8a210002U, "TRANS_DDI_FUNC_CTL_A equals Linux's dump (0x8a210002)");
	i915_host_check(w.w[1].reg == 0x60404U && w.w[1].value == 0U && w.w[2].reg == 0x60400U,
			"TRANS_DDI_FUNC_CTL2 (no port sync) is written before TRANS_DDI_FUNC_CTL");

	/* DP_MSA_MISC_SYNC_CLOCK | 6 bpc (no MSA readout in the dump: the expected value is from the DP MSA field layout). */
	i915_host_check(w.w[0].reg == 0x60410U && w.w[0].value == 0x00000001U,
			"TRANS_MSA_MISC_A = synchronous clock, 6 bpc, RGB, no VSC SDP");

	/* Linux's dump: DDI_BUF_CTL_A 0x80000002 = this value + DDI_BUF_CTL_ENABLE (set by the link-training preparation). */
	i915_host_check(buf == 0x00000002U && (buf | 0x80000000U) == 0x80000002U,
			"DDI_BUF_CTL value: x2, not reversed; + enable = Linux's dump (0x80000002)");

	/* 4 lanes, 8 bpc, both syncs negative, port B: every field moves. */
	s.link.lanes = 4;
	s.link.bpp = 24;
	s.mode.hsync_positive = 0;
	s.mode.vsync_positive = 0;
	error = drv_i915_lcd_emit_ddi(i915_lcd_world, &s, 1, 0, 0, 0x00010000U, &w, &buf);
	hits = 0U;
	if (error == 0)
		hits = drv_i915_lcd_words_find(&w, 0x60400U, &value);

	i915_host_check(error == 0 &&
			hits == 1U &&
			value == (0x80000000U | (2U << 27) | (2U << 24) | (0U << 20) | (3U << 1)),
			"port B, 8 bpc, 4 lanes, -HSync -VSync: port select 2, bpc field 0, width 3, no sync bits");
	i915_host_check(buf == (0x00010000U | (3U << 1)) && w.w[0].value == (1U | (1U << 5)),
			"lane reversal kept from saved_port_bits; MSA 8 bpc");

	/* A Type-C port needs the TC state this slice does not have. */
	error = drv_i915_lcd_emit_ddi(i915_lcd_world, &s, 3, 0, 0, 0U, &w, &buf);
	i915_host_check(error == EINVAL, "a Type-C port is refused (no TC state in this slice)");
}

/* The universal plane: one full-screen primary plane on the scanout buffer's layout. */
static void
i915_lcd_test_plane(void)
{
	struct i915_lcd_words w;
	uint32_t value;
	uint32_t value2;
	unsigned hits;
	unsigned hits2;
	unsigned i;
	unsigned surf_at;
	unsigned ctl_at;
	unsigned wm_at;
	unsigned color_at;
	int error;

	/* Records the plane words and where the arming writes and the watermark write are. */
	surf_at = 99U;
	ctl_at = 99U;
	wm_at = 99U;
	color_at = 99U;
	error = drv_i915_lcd_emit_plane(i915_lcd_world, 0, 0, I915_LCD_TEST_XR24, 0U, 1920U, 1080U, 7680U, 0xfdfc0000U, &w);
	for (i = 0U; i < w.n; i++) {
		if (w.w[i].step != NULL) {
			printf("  plane[%2u] STEP  %s (reference callee, not ported)\n", i, w.w[i].step);
			continue;
		}

		printf("  plane[%2u] write 0x%05x = 0x%08x\n", i, w.w[i].reg, w.w[i].value);

		/* PLANE_BUF_CFG is the last write of skl_write_plane_wm(). */
		if (w.w[i].reg == 0x7027cU)
			wm_at = i;
		if (w.w[i].reg == 0x7019cU)
			surf_at = i;
		if (w.w[i].reg == 0x70180U)
			ctl_at = i;
		if (w.w[i].reg == 0x701ccU)
			color_at = i;
	}

	i915_host_check(error == 0 && w.overflow == 0U, "plane words produced, none dropped");

	/* Linux's dump (regs-selected.txt): DSPACNTR 0x94000000, DSPASTRIDE 0x78, 0x70190 0x0437077f, 0x701cc 0x2000. */
	hits = drv_i915_lcd_words_find(&w, 0x70180U, &value);
	i915_host_check(hits == 1U && value == 0x94000000U,
			"PLANE_CTL_1_A = enable | XRGB8888 | ADL-P arbitration slots for 4 bytes/pixel = Linux's dump (0x94000000)");
	hits = drv_i915_lcd_words_find(&w, 0x70188U, &value);
	i915_host_check(hits == 1U && value == 0x78U, "PLANE_STRIDE = pitch / 64 = 120 = Linux's dump");
	hits = drv_i915_lcd_words_find(&w, 0x70190U, &value);
	i915_host_check(hits == 1U && value == 0x0437077fU, "PLANE_SIZE = (1080-1) << 16 | (1920-1) = Linux's dump");
	hits = drv_i915_lcd_words_find(&w, 0x7018cU, &value);
	i915_host_check(hits == 1U && value == 0U, "PLANE_POS = 0,0 (dump 0)");
	hits = drv_i915_lcd_words_find(&w, 0x701ccU, &value);
	i915_host_check(hits == 1U && value == 0x00002000U,
			"PLANE_COLOR_CTL = plane gamma disable, alpha disabled (no alpha channel) = Linux's dump");
	hits = drv_i915_lcd_words_find(&w, 0x7019cU, &value);
	i915_host_check(hits == 1U && value == 0xfdfc0000U,
			"PLANE_SURF = the scanout buffer's GGTT address (Linux's differs: its own buffer)");

	/* No aux plane; ADL-P is not a flat-CCS device so PLANE_AUX_DIST is written. */
	hits = drv_i915_lcd_words_find(&w, 0x701a4U, &value);
	hits2 = drv_i915_lcd_words_find(&w, 0x701c0U, &value2);
	i915_host_check(hits == 1U && value == 0U && hits2 == 1U && value2 == 0U,
			"PLANE_OFFSET 0 and PLANE_AUX_DIST 0 (no aux plane; ADL-P is not a flat-CCS device so the register is written)");

	/* The colour key is off. */
	hits = drv_i915_lcd_words_find(&w, 0x701a0U, &value);
	hits2 = drv_i915_lcd_words_find(&w, 0x70198U, &value2);
	i915_host_check(hits == 1U && value == 0xff000000U && hits2 == 1U && value2 == 0U,
			"colour key off: KEYMAX carries plane alpha 0xff, KEYMSK 0 (opaque, so no alpha-enable bit)");
	hits = drv_i915_lcd_words_find(&w, 0x701c8U, &value);
	i915_host_check(hits == 1U && value == 0U, "PLANE_CUS_CTL = 0 (the primary plane is an HDR plane; no chroma upsampler for RGB)");

	/* The surface write arms the update; the watermarks are written before it. */
	i915_host_check(surf_at == w.n - 1U && ctl_at == w.n - 2U,
			"PLANE_CTL then PLANE_SURF are the last two operations (the surface write arms the update)");
	i915_host_check(wm_at != 99U &&
			color_at != 99U &&
			wm_at > color_at &&
			wm_at < ctl_at,
			"skl_write_plane_wm() runs for real between PLANE_COLOR_CTL and the arm (here with an empty watermark state: the words-only API computes none)");

	/* Pipe B: the same block one pipe up. */
	error = drv_i915_lcd_emit_plane(i915_lcd_world, 1, 0, I915_LCD_TEST_XR24, 0U, 1920U, 1080U, 7680U, 0x00100000U, &w);
	hits = 0U;
	hits2 = 0U;
	if (error == 0) {
		hits = drv_i915_lcd_words_find(&w, 0x71180U, &value);
		hits2 = drv_i915_lcd_words_find(&w, 0x7119cU, &value2);
	}

	i915_host_check(error == 0 &&
			hits == 1U &&
			value == 0x94000000U &&
			hits2 == 1U &&
			value2 == 0x00100000U,
			"pipe B: PLANE_CTL_1_B / PLANE_SURF_1_B");

	/*
	 * What this slice does not cover is refused before the reference code
	 * runs: another format (AB24), a tiled modifier (I915 Y-tiled), a
	 * sprite plane, a pitch that is not x64 and an unaligned surface.
	 */
	error = drv_i915_lcd_emit_plane(i915_lcd_world, 0, 0, 0x34324241U, 0U, 1920U, 1080U, 7680U, 0x100000U, &w);
	if (error == EINVAL)
		error = drv_i915_lcd_emit_plane(i915_lcd_world, 0, 0, I915_LCD_TEST_XR24, 0x0100000000000002ULL, 1920U, 1080U, 7680U, 0x100000U, &w);
	if (error == EINVAL)
		error = drv_i915_lcd_emit_plane(i915_lcd_world, 0, 1, I915_LCD_TEST_XR24, 0U, 1920U, 1080U, 7680U, 0x100000U, &w);
	if (error == EINVAL)
		error = drv_i915_lcd_emit_plane(i915_lcd_world, 0, 0, I915_LCD_TEST_XR24, 0U, 1920U, 1080U, 7000U, 0x100000U, &w);
	if (error == EINVAL)
		error = drv_i915_lcd_emit_plane(i915_lcd_world, 0, 0, I915_LCD_TEST_XR24, 0U, 1920U, 1080U, 7680U, 0x100800U, &w);

	i915_host_check(error == EINVAL,
			"another format, a tiled modifier, a sprite plane, a pitch that is not x64 and an unaligned surface are refused");
}

/* Tells whether recorded words equal the wanted ones, register and value, in order. */
static int
i915_lcd_same_words(
	const struct i915_lcd_words *words,
	const struct i915_lcd_regwrite *want,
	unsigned count)
{
	unsigned i;

	/* Compares the words both lists have. */
	for (i = 0U; i < words->n && i < count; i++) {
		if (words->w[i].reg != want[i].reg)
			return 0;
		if (words->w[i].value != want[i].value)
			return 0;
	}

	/* Succeeded: every compared word is the same. */
	return 1;
}

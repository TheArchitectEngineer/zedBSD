/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of a screen's plan (display/xd-screen.c): the 5330's
 * panel against the numbers the older check phase reports for it (the
 * CD clock 179.2 MHz from a 70.4 MHz pixel need and an 11 MHz bandwidth
 * need, 564 MB/s, four slices joined), the refusals (another CD clock,
 * more than the highest, another voltage level, the QGV point, the
 * sinks the plan does not drive, a link too slow), an HDMI sink's link
 * and WRPLL on DDI B, an external DisplayPort sink's link on a Type-C
 * port with a branch's limits, and Tiger Lake's pixel rate rule.
 */

#include "host-test.h"

#include "../../display/xd-clock.h"
#include "../../display/xd-dp.h"
#include "../../display/xd-platform.h"
#include "../../display/xd-screen.h"
#include "../../display/display-internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The devices the checks describe: the 5330's Alder Lake-P and a Tiger Lake. */
#define I915_XD_SCREEN_TEST_ADLP	0x46a6U
#define I915_XD_SCREEN_TEST_ADLP_REVID	0x0cU
#define I915_XD_SCREEN_TEST_TGL		0x9a49U
#define I915_XD_SCREEN_TEST_TGL_REVID	0x01U

static const struct i915_xd_platform *i915_xd_screen_test_platform(uint16_t device, uint8_t revid);
static void i915_xd_screen_test_mode(struct i915_display_mode *mode, int clock_khz);
static void i915_xd_screen_test_panel_input(struct i915_xd_plan_input *input, const struct i915_xd_platform *platform, const struct i915_display_mode *mode);
static void i915_xd_screen_test_panel(void);
static void i915_xd_screen_test_refusals(void);
static void i915_xd_screen_test_sinks(void);
static void i915_xd_screen_test_hdmi(void);
static void i915_xd_screen_test_dp(void);
static void i915_xd_screen_test_tgl(void);

/*
 * Runs every check of the plan and reports.
 */
int
main(void)
{
	int failures;

	/* Each group of checks. */
	i915_xd_screen_test_panel();
	i915_xd_screen_test_refusals();
	i915_xd_screen_test_sinks();
	i915_xd_screen_test_hdmi();
	i915_xd_screen_test_dp();
	i915_xd_screen_test_tgl();

	/* Reports the result. */
	failures = i915_host_report("host_xd_screen_test");
	if (failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Returns a device's description, or NULL. */
static const struct i915_xd_platform *
i915_xd_screen_test_platform(
	uint16_t device,
	uint8_t revid)
{
	struct i915_xd_choice choice;
	int error;

	/* The description by the product. */
	error = drv_i915_xd_platform_choose(device, revid, 0, 0U, &choice);
	if (error != 0)
		return NULL;

	/* Succeeded: the description. */
	return choice.platform;
}

/* A 1920x1080 mode at a clock. */
static void
i915_xd_screen_test_mode(
	struct i915_display_mode *mode,
	int clock_khz)
{
	/* The timings of the 5330's panel. */
	memset(mode, 0, sizeof(*mode));
	mode->clock_khz = clock_khz;
	mode->hdisplay = 1920U;
	mode->hsync_start = 1968U;
	mode->hsync_end = 2000U;
	mode->htotal = 2080U;
	mode->vdisplay = 1080U;
	mode->vsync_start = 1083U;
	mode->vsync_end = 1088U;
	mode->vtotal = 1128U;
}

/* The 5330's panel: HBR on four lanes, eDP 1.3, a 6 bpc EDID, the CD clock 179.2 MHz from a 38.4 MHz reference. */
static void
i915_xd_screen_test_panel_input(
	struct i915_xd_plan_input *input,
	const struct i915_xd_platform *platform,
	const struct i915_display_mode *mode)
{
	/* The output, its sink and the platform. */
	memset(input, 0, sizeof(*input));
	input->kind = I915_XD_SCREEN_PANEL;
	input->ddi = 0U;
	input->pipe = 0U;
	input->mode = mode;
	input->sink.rate_code = 0x0aU;
	input->sink.max_lanes = 4U;
	input->sink.edp_revision = 0x02U;
	input->platform = platform;
	input->ref_khz = 38400U;
	input->run_pipes = 0x1U;

	/* The CD clock the start left, its highest step, the QGV point, the plane's format. */
	input->cdclk.ref_khz = 38400U;
	input->cdclk.vco_khz = 537600U;
	input->cdclk.cdclk_khz = 179200U;
	input->cdclk.bypass_khz = 19200U;
	input->cdclk.voltage = 0U;
	input->cdclk_max_khz = 652800U;
	input->qgv_mbps = 20000U;
	input->cpp = 4U;
}

/* The 5330's panel against the older check phase's numbers. */
static void
i915_xd_screen_test_panel(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_plan_input input;
	struct i915_display_mode mode;
	struct i915_xd_dp_m_n m_n;
	struct i915_xd_plan plan;
	unsigned refused;

	/* The panel at 140.8 MHz, 6 bpc. */
	platform = i915_xd_screen_test_platform(I915_XD_SCREEN_TEST_ADLP, I915_XD_SCREEN_TEST_ADLP_REVID);
	i915_host_check(platform != NULL, "panel: the 5330's description");
	if (platform == NULL)
		return;
	i915_xd_screen_test_mode(&mode, 140800);
	mode.edid_bpc = 6;
	i915_xd_screen_test_panel_input(&input, platform, &mode);

	/* The plan. */
	refused = drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_OK && plan.refused == I915_XD_PLAN_OK, "panel: accepted");
	i915_host_check(plan.rate_khz == 270000U && plan.lanes == 4U && plan.bpp == 18U, "panel: HBR, four lanes, 18 bpp");
	drv_i915_xd_dp_m_n(18U * 16U, 4U, 140800U, 270000U, 1000000U, &m_n);
	i915_host_check(memcmp(&m_n, &plan.m_n, sizeof(m_n)) == 0 && plan.m_n.data_n != 0U, "panel: the link's M/N");
	i915_host_check(plan.pll_set && plan.pll.cfgcr0 != 0U, "panel: DPLL words for DDI A's combo PHY");
	i915_host_check(plan.min_cdclk_khz == 70400U && plan.cdclk_khz == 179200U && plan.vco_khz == 537600U && plan.voltage == 0U,
			"panel: needs 70.4 MHz, gets the running 179.2 MHz at VCO 537.6 MHz, level 0 (the older check's numbers)");
	i915_host_check(plan.data_mbps == 564U, "panel: 564 MB/s (the older check's data rate)");
	i915_host_check(plan.slices == 0xfU && plan.joined == 1U, "panel: four slices, the bus joined (pipe A alone)");

	/* With pipe B too: the bus is not joined. */
	input.run_pipes = 0x3U;
	(void)drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(plan.joined == 0U && plan.slices != 0U && plan.slices != 0xfU, "panel: pipes A and B: not joined, part of the slices");

	/* A VBT cap and an EDID that names no depth. */
	mode.edid_bpc = 0;
	input.sink.vbt_bpp = 18U;
	input.run_pipes = 0x1U;
	(void)drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(plan.bpp == 18U, "panel: 24 bpp capped by the VBT's 18");
	input.sink.vbt_bpp = 0U;
	(void)drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(plan.bpp == 24U, "panel: 24 bpp when the EDID names no depth");
}

/* The check's refusals. */
static void
i915_xd_screen_test_refusals(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_plan_input input;
	struct i915_display_mode mode;
	struct i915_xd_plan plan;
	unsigned refused;

	platform = i915_xd_screen_test_platform(I915_XD_SCREEN_TEST_ADLP, I915_XD_SCREEN_TEST_ADLP_REVID);
	if (platform == NULL)
		return;

	/* A mode that needs a higher CD clock (on four HBR3 lanes). */
	i915_xd_screen_test_mode(&mode, 600000);
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.sink.rate_code = 0x1eU;
	refused = drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_CDCLK && plan.cdclk_khz == 307200U, "refusal: 600 MHz needs 307.2 MHz, not the running 179.2");

	/* More than the highest step. */
	i915_xd_screen_test_mode(&mode, 140800);
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.cdclk_max_khz = 60000U;
	refused = drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_CDCLK && plan.cdclk_khz == 0U, "refusal: a need above the highest step");

	/* Another voltage level. */
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.cdclk.voltage = 1U;
	refused = drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_VOLTAGE, "refusal: the running level is not the mode's");

	/* The QGV point. */
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.qgv_mbps = 500U;
	refused = drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_QGV, "refusal: 564 MB/s against a 500 MB/s QGV point");
	input.qgv_mbps = 0U;
	refused = drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_QGV, "refusal: an unknown QGV point");

	/* A link refused is not checked. */
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.sink.rate_code = 0x06U;
	input.sink.max_lanes = 1U;
	refused = drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_LINK && plan.cdclk_khz == 0U, "refusal: one RBR lane cannot carry 140.8 MHz at 24 bpp; no check");
}

/* The panels the plan does not drive. */
static void
i915_xd_screen_test_sinks(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_plan_input input;
	struct i915_display_mode mode;
	struct i915_xd_plan plan;
	unsigned refused;

	platform = i915_xd_screen_test_platform(I915_XD_SCREEN_TEST_ADLP, I915_XD_SCREEN_TEST_ADLP_REVID);
	if (platform == NULL)
		return;
	i915_xd_screen_test_mode(&mode, 140800);

	/* eDP 1.4, a rate code that is not one of the four, three lanes, a mode without a clock. */
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.sink.edp_revision = 0x03U;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_SINK, "sinks: an eDP 1.4 panel's own rates are not taken");
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.sink.rate_code = 0x0cU;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_SINK, "sinks: rate code 0x0c");
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.sink.max_lanes = 3U;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_SINK, "sinks: three lanes");
	mode.clock_khz = 0;
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_LINK, "sinks: a mode without a clock");
}

/* An HDMI sink on DDI B. */
static void
i915_xd_screen_test_hdmi(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_plan_input input;
	struct i915_display_mode mode;
	struct i915_xd_dpll_words words;
	struct i915_xd_plan plan;
	unsigned refused;
	int error;

	platform = i915_xd_screen_test_platform(I915_XD_SCREEN_TEST_ADLP, I915_XD_SCREEN_TEST_ADLP_REVID);
	if (platform == NULL)
		return;

	/* 1080p60 on DDI B, pipe B. */
	i915_xd_screen_test_mode(&mode, 148500);
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.kind = I915_XD_SCREEN_HDMI;
	input.ddi = 1U;
	input.pipe = 1U;
	input.run_pipes = 0x3U;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	error = drv_i915_xd_dpll_hdmi_words(platform, 38400U, 148500U, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(refused == I915_XD_PLAN_OK && plan.rate_khz == 148500U && plan.lanes == 4U && plan.bpp == 24U, "hdmi: the TMDS clock on four lanes, 24 bpp");
	i915_host_check(error == 0 && plan.pll_set && plan.pll.cfgcr0 == words.cfgcr0 && plan.pll.cfgcr1 == words.cfgcr1, "hdmi: the WRPLL's words");
	i915_host_check(plan.m_n.data_n == 0U, "hdmi: no M/N");
}

/* An external DisplayPort sink on a Type-C port. */
static void
i915_xd_screen_test_dp(void)
{
	static const uint32_t rates[] = { 162000U, 270000U, 540000U };
	const struct i915_xd_platform *platform;
	struct i915_xd_plan_input input;
	struct i915_display_mode mode;
	struct i915_xd_plan plan;
	unsigned refused;

	platform = i915_xd_screen_test_platform(I915_XD_SCREEN_TEST_ADLP, I915_XD_SCREEN_TEST_ADLP_REVID);
	if (platform == NULL)
		return;

	/* 1080p60 on TC1. */
	i915_xd_screen_test_mode(&mode, 148500);
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	input.kind = I915_XD_SCREEN_DP;
	input.ddi = 3U;
	input.sink.rates = rates;
	input.sink.nrates = 3U;
	input.sink.max_lanes = 4U;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_OK && plan.bpp == 24U && plan.need_kbps <= plan.carry_kbps && plan.rate_khz == 162000U && plan.lanes == 4U,
			"dp: the lowest rate first: RBR on four lanes carries 1080p60 at 24 bpp");
	i915_host_check(plan.pll_set == 0 && plan.m_n.data_n != 0U, "dp: no combo PLL on a Type-C port, the M/N");

	/* Two lanes at most. */
	input.sink.lane_limit = 2U;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_OK && plan.lanes <= 2U && plan.rate_khz == 270000U, "dp: two lanes at most: HBR");

	/* A branch's limits, and a sink without rates. */
	input.sink.lane_limit = 0U;
	input.sink.max_dotclock_khz = 100000U;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_LINK, "dp: above a branch's dot clock");
	input.sink.max_dotclock_khz = 0U;
	input.sink.max_bpc = 6U;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_OK && plan.bpp == 18U, "dp: a branch's 6 bpc cap");
	input.sink.nrates = 0U;
	refused = drv_i915_xd_screen_plan_link(&input, &plan);
	i915_host_check(refused == I915_XD_PLAN_SINK, "dp: no shared rate");
}

/* Tiger Lake: the pixel rate itself is the need. */
static void
i915_xd_screen_test_tgl(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_plan_input input;
	struct i915_display_mode mode;
	struct i915_xd_plan plan;

	/* The panel at 140.8 MHz on a Tiger Lake. */
	platform = i915_xd_screen_test_platform(I915_XD_SCREEN_TEST_TGL, I915_XD_SCREEN_TEST_TGL_REVID);
	i915_host_check(platform != NULL, "tgl: a Tiger Lake description");
	if (platform == NULL)
		return;
	i915_xd_screen_test_mode(&mode, 140800);
	i915_xd_screen_test_panel_input(&input, platform, &mode);
	(void)drv_i915_xd_screen_plan(&input, &plan);
	i915_host_check(plan.min_cdclk_khz == 140800U && plan.cdclk_khz >= 140800U, "tgl: needs the pixel rate itself");
	i915_host_check(plan.refused == I915_XD_PLAN_CDCLK, "tgl: the running 179.2 MHz is not a Tiger Lake step: another CD clock");
}

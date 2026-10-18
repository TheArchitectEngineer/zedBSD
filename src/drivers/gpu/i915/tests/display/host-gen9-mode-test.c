/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Gen9 mode set's arithmetic (display/gen9-mode.c):
 * the CD clock, the choice of a mode from an EDID, the DP
 * link and its M and N, the DPLL words of DP rates and HDMI clocks, the
 * DDI buffer tables and signal levels, the training adjustments, and the
 * watermarks and DDB of one, two and three pipes.
 *
 * The expected numbers come from a separate model of the Linux kernel
 * 6.8.12's computations (intel_link_compute_m_n, skl_ddi_calculate_wrpll,
 * skl_compute_plane_wm, skl_cursor_allocation), not from this code; the
 * WRPLL word for 148.5 MHz (0x80400173, 0x3a5) is the one Skylake parts
 * program for 1080p HDMI.
 */

#include "host-test.h"

#include "../../display/gen9-mode.h"
#include "../../device-info.h"
#include "../../intel/display-regs-gen9.h"
#include "../../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>

#include <stdio.h>
#include <string.h>

static void i915_host_cdclk(void);
static void i915_host_pick_mode(void);
static void i915_host_dp_link(void);
static void i915_host_dpll(void);
static void i915_host_buf_trans(void);
static void i915_host_training(void);
static void i915_host_watermarks(void);
static int i915_host_level_is(const struct i915_skl_wm_level *level, uint32_t blocks, uint32_t lines);

/* The memory latencies the watermark checks use, in microseconds. */
static const uint16_t i915_host_latency[8] = { 2U, 4U, 10U, 18U, 28U, 40U, 60U, 90U };

/*
 * Checks the Gen9 mode set's arithmetic.
 */
int
main(void)
{
	int status;

	/* Each part. */
	i915_host_cdclk();
	i915_host_pick_mode();
	i915_host_dp_link();
	i915_host_dpll();
	i915_host_buf_trans();
	i915_host_training();
	i915_host_watermarks();

	/* The tally. */
	status = i915_host_report("gen9_mode_host_test");
	if (status != 0)
		return status;

	/* Succeeded: the arithmetic is the one gen9-mode.c describes. */
	return 0;
}

/* Checks the CD clock of both VCOs and each select, and a DPLL0 that is off or unlocked. */
static void
i915_host_cdclk(void)
{
	uint32_t locked;
	uint32_t vco_8640;
	uint32_t vco_8100;

	/* A running, locked DPLL0 at either VCO. */
	locked = (1U << 31) | (1U << 30);
	vco_8640 = I915_SKL_LINK_RATE_1080 << I915_SKL_DPLL_CTRL1_RATE_SHIFT;
	vco_8100 = I915_SKL_LINK_RATE_1350 << I915_SKL_DPLL_CTRL1_RATE_SHIFT;

	/* The 8640 MHz VCO. */
	i915_host_check(drv_i915_skl_cdclk_khz(locked, vco_8640, 0U << 26) == 432000U, "cdclk 8640 select 0 is 432 MHz");
	i915_host_check(drv_i915_skl_cdclk_khz(locked, vco_8640, 2U << 26) == 308571U, "cdclk 8640 select 2 is 308.571 MHz");
	i915_host_check(drv_i915_skl_cdclk_khz(locked, vco_8640, 3U << 26) == 617143U, "cdclk 8640 select 3 is 617.143 MHz");

	/* The 8100 MHz VCO. */
	i915_host_check(drv_i915_skl_cdclk_khz(locked, vco_8100, 1U << 26) == 540000U, "cdclk 8100 select 1 is 540 MHz");
	i915_host_check(drv_i915_skl_cdclk_khz(locked, vco_8100, 3U << 26) == 675000U, "cdclk 8100 select 3 is 675 MHz");

	/* DPLL0 off or not locked. */
	i915_host_check(drv_i915_skl_cdclk_khz(0U, vco_8100, 0U) == 0U, "cdclk is 0 with DPLL0 off");
	i915_host_check(drv_i915_skl_cdclk_khz(1U << 31, vco_8100, 0U) == 0U, "cdclk is 0 with DPLL0 unlocked");
}

/* Checks the mode chosen from a base block's 1080p timing, under a low clock limit, and without an EDID. */
static void
i915_host_pick_mode(void)
{
	static const uint8_t dtd_1080p[18] = {
		0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58,
		0x2c, 0x45, 0x00, 0xc4, 0x8e, 0x21, 0x00, 0x00, 0x1e
	};
	struct i915_display_mode mode;
	uint8_t edid[256];
	int error;

	/* A base block whose first descriptor is 1080p at 148.5 MHz. */
	memset(edid, 0, sizeof(edid));
	memcpy(edid + 54, dtd_1080p, sizeof(dtd_1080p));

	/* The timing fits a 540 MHz CD clock. */
	error = drv_i915_skl_pick_mode(edid, 128U, 540000U, &mode);
	i915_host_check(error == 0 && mode.clock_khz == 148500 && mode.hdisplay == 1920U && mode.vdisplay == 1080U, "mode 1080p from the base block");
	i915_host_check(mode.hsync_start == 2008U && mode.hsync_end == 2052U && mode.htotal == 2200U, "mode 1080p horizontal timing");
	i915_host_check(mode.vsync_start == 1084U && mode.vsync_end == 1089U && mode.vtotal == 1125U, "mode 1080p vertical timing");
	i915_host_check(mode.hsync_positive && mode.vsync_positive, "mode 1080p positive syncs");

	/* Under 100 MHz the safe mode is taken. */
	error = drv_i915_skl_pick_mode(edid, 128U, 100000U, &mode);
	i915_host_check(error == 0 && mode.clock_khz == 65000 && mode.hdisplay == 1024U && mode.vtotal == 806U, "mode safe 1024x768 above the limit");

	/* No EDID, and a limit below even the safe mode. */
	error = drv_i915_skl_pick_mode(NULL, 0U, 540000U, &mode);
	i915_host_check(error == 0 && mode.hdisplay == 1024U, "mode safe without an EDID");
	error = drv_i915_skl_pick_mode(NULL, 0U, 50000U, &mode);
	i915_host_check(error == EINVAL, "mode EINVAL below the safe mode");
}

/* Checks the link chosen for 1080p and 4K60, the M and N, and the DPCD codes and delays. */
static void
i915_host_dp_link(void)
{
	struct i915_skl_link link;
	int error;

	/* 1080p at 148.5 MHz: RBR on four lanes, data M/N 0x580000/0x800000, link 0x75555/0x80000. */
	error = drv_i915_skl_dp_link(I915_SKL_DP_HBR2_KHZ, 4U, 148500U, &link);
	i915_host_check(error == 0 && link.port_khz == I915_SKL_DP_RBR_KHZ && link.lanes == 4U, "link 1080p on RBR x4");
	i915_host_check(link.data_m == 0x580000U && link.data_n == 0x800000U && link.tu == 64U, "link 1080p data M/N");
	i915_host_check(link.link_m == 0x75555U && link.link_n == 0x80000U, "link 1080p link M/N");

	/* 4K60 at 594 MHz: HBR2 on four lanes, data 0x699999/0x800000, link 0x8cccc/0x80000. */
	error = drv_i915_skl_dp_link(I915_SKL_DP_HBR2_KHZ, 4U, 594000U, &link);
	i915_host_check(error == 0 && link.port_khz == I915_SKL_DP_HBR2_KHZ && link.lanes == 4U, "link 4K60 on HBR2 x4");
	i915_host_check(link.data_m == 0x699999U && link.data_n == 0x800000U, "link 4K60 data M/N");
	i915_host_check(link.link_m == 0x8ccccU && link.link_n == 0x80000U, "link 4K60 link M/N");

	/* A sink of HBR on four lanes cannot carry 4K60; one of two lanes takes 1080p on HBR. */
	error = drv_i915_skl_dp_link(I915_SKL_DP_HBR_KHZ, 4U, 594000U, &link);
	i915_host_check(error == ENOSPC, "link 4K60 ENOSPC on HBR");
	error = drv_i915_skl_dp_link(I915_SKL_DP_HBR2_KHZ, 2U, 148500U, &link);
	i915_host_check(error == 0 && link.port_khz == I915_SKL_DP_HBR_KHZ && link.lanes == 2U, "link 1080p on HBR x2 for a two-lane sink");

	/* The DPCD codes both ways. */
	i915_host_check(drv_i915_skl_dpcd_rate_khz(0x06U) == I915_SKL_DP_RBR_KHZ && drv_i915_skl_dpcd_rate_khz(0x14U) == I915_SKL_DP_HBR2_KHZ, "dpcd rate codes");
	i915_host_check(drv_i915_skl_dpcd_rate_khz(0x1eU) == I915_SKL_DP_HBR2_KHZ && drv_i915_skl_dpcd_rate_khz(0x00U) == 0U, "dpcd HBR3 capped, 0 unknown");
	i915_host_check(drv_i915_skl_dpcd_rate_code(I915_SKL_DP_HBR_KHZ) == 0x0aU && drv_i915_skl_dpcd_rate_code(300000U) == 0U, "dpcd LINK_BW_SET codes");

	/* The training delays. */
	i915_host_check(drv_i915_skl_dp_train_delay_us(0x12U, 0U, 0) == 100U && drv_i915_skl_dp_train_delay_us(0x12U, 0U, 1) == 400U, "train delays without an interval");
	i915_host_check(drv_i915_skl_dp_train_delay_us(0x12U, 2U, 0) == 8000U && drv_i915_skl_dp_train_delay_us(0x14U, 2U, 0) == 100U, "train delays CR with an interval");
	i915_host_check(drv_i915_skl_dp_train_delay_us(0x14U, 0x82U, 1) == 8000U, "train delay EQ ignores the extended flag");
}

/* Checks the DPLL words of the DP rates and of five HDMI clocks. */
static void
i915_host_dpll(void)
{
	struct i915_skl_dpll_words words;
	int error;

	/* DP: the override and the code of half the port clock. */
	error = drv_i915_skl_dpll_dp(I915_SKL_DP_RBR_KHZ, &words);
	i915_host_check(error == 0 && words.ctrl1 == 0x5U && words.cfgcr1 == 0U, "dpll DP RBR");
	error = drv_i915_skl_dpll_dp(I915_SKL_DP_HBR_KHZ, &words);
	i915_host_check(error == 0 && words.ctrl1 == 0x3U, "dpll DP HBR");
	error = drv_i915_skl_dpll_dp(I915_SKL_DP_HBR2_KHZ, &words);
	i915_host_check(error == 0 && words.ctrl1 == 0x1U, "dpll DP HBR2");
	error = drv_i915_skl_dpll_dp(300000U, &words);
	i915_host_check(error == EINVAL, "dpll DP EINVAL for 3 GHz");

	/* HDMI: the WRPLL words. */
	error = drv_i915_skl_dpll_hdmi(25175U, I915_SKL_REF_KHZ, &words);
	i915_host_check(error == 0 && words.ctrl1 == 0x21U && words.cfgcr1 == 0x809aab8eU && words.cfgcr2 == 0x13a4U, "dpll HDMI 25.175 MHz");
	error = drv_i915_skl_dpll_hdmi(65000U, I915_SKL_REF_KHZ, &words);
	i915_host_check(error == 0 && words.cfgcr1 == 0x802aab7bU && words.cfgcr2 == 0x7a4U, "dpll HDMI 65 MHz");
	error = drv_i915_skl_dpll_hdmi(74250U, I915_SKL_REF_KHZ, &words);
	i915_host_check(error == 0 && words.cfgcr1 == 0x80400173U && words.cfgcr2 == 0x6a5U, "dpll HDMI 74.25 MHz");
	error = drv_i915_skl_dpll_hdmi(148500U, I915_SKL_REF_KHZ, &words);
	i915_host_check(error == 0 && words.cfgcr1 == 0x80400173U && words.cfgcr2 == 0x3a5U, "dpll HDMI 148.5 MHz");
	error = drv_i915_skl_dpll_hdmi(297000U, I915_SKL_REF_KHZ, &words);
	i915_host_check(error == 0 && words.cfgcr1 == 0x80400173U && words.cfgcr2 == 0x145U, "dpll HDMI 297 MHz");
}

/* Checks the power class, the table choice and counts, and the signal levels. */
static void
i915_host_buf_trans(void)
{
	const struct i915_xd9_buf_trans *table;
	unsigned count;

	/* The power classes. */
	i915_host_check(drv_i915_skl_sku(0x5916U, I915_PLATFORM_KABYLAKE) == I915_SKL_SKU_ULT, "sku KBL GT2 U is ULT");
	i915_host_check(drv_i915_skl_sku(0x591eU, I915_PLATFORM_KABYLAKE) == I915_SKL_SKU_ULX, "sku KBL GT2 Y is ULX");
	i915_host_check(drv_i915_skl_sku(0x3e92U, I915_PLATFORM_COFFEELAKE) == I915_SKL_SKU_H, "sku CFL GT2 S is H");

	/* Skylake H DP on port B: nine entries, the first 0x2016/0xa0. */
	table = drv_i915_skl_buf_trans(I915_SKL_SKU_H, I915_PLATFORM_SKYLAKE, I915_SKL_OUTPUT_DP, I915_SKL_PORT_B, &count);
	i915_host_check(count == 9U && table[0].trans1 == 0x00002016U && table[0].trans2 == 0x000000a0U, "trans SKL DP port B");
	i915_host_check(table[3].iboost == 1U && table[8].trans1 == 0x80005012U, "trans SKL DP boost entries");

	/* HDMI: eleven entries, the default entry 8 boosted. */
	table = drv_i915_skl_buf_trans(I915_SKL_SKU_ULT, I915_PLATFORM_KABYLAKE, I915_SKL_OUTPUT_HDMI, I915_SKL_PORT_C, &count);
	i915_host_check(count == 11U && drv_i915_skl_hdmi_level(I915_SKL_SKU_ULT, I915_PLATFORM_KABYLAKE) == 8U, "trans HDMI eleven entries, level 8");
	i915_host_check(table[8].trans1 == 0x80003015U && table[8].trans2 == 0x000000cdU && table[8].iboost == 1U, "trans HDMI entry 8");

	/* eDP on port A keeps ten entries; elsewhere nine. */
	(void)drv_i915_skl_buf_trans(I915_SKL_SKU_H, I915_PLATFORM_SKYLAKE, I915_SKL_OUTPUT_EDP, I915_SKL_PORT_A, &count);
	i915_host_check(count == 10U, "trans eDP port A ten entries");
	(void)drv_i915_skl_buf_trans(I915_SKL_SKU_H, I915_PLATFORM_SKYLAKE, I915_SKL_OUTPUT_EDP, I915_SKL_PORT_D, &count);
	i915_host_check(count == 9U, "trans eDP port D nine entries");

	/* The signal levels and the swing ceilings. */
	i915_host_check(drv_i915_skl_signal_level(0x00U) == 0U && drv_i915_skl_signal_level(0x09U) == 5U && drv_i915_skl_signal_level(0x03U) == 9U, "signal levels");
	i915_host_check(drv_i915_skl_signal_level(0x0aU | 0x04U | 0x20U) == 8U, "signal level ignores the reached flags");
	i915_host_check(drv_i915_skl_voltage_max(9U) == 2U && drv_i915_skl_voltage_max(10U) == 3U, "voltage max of nine and ten entries");
}

/* Checks the adjustments and the clock recovery and equalisation tests. */
static void
i915_host_training(void)
{
	uint8_t status[6];
	uint8_t set;

	/* Lane 1 asks for swing 1, lane 2 for pre-emphasis 1. */
	memset(status, 0, sizeof(status));
	status[4] = 0x10U;
	status[5] = 0x04U;
	set = drv_i915_skl_adjust_train(status, 4U, 2U);
	i915_host_check(set == 0x09U, "adjust takes the highest of the lanes");

	/* Lanes beyond the count are ignored. */
	set = drv_i915_skl_adjust_train(status, 1U, 2U);
	i915_host_check(set == 0x00U, "adjust ignores unused lanes");

	/* Swing 3 on a nine-entry table stops at 2 and is marked reached. */
	status[4] = 0x03U;
	status[5] = 0x00U;
	set = drv_i915_skl_adjust_train(status, 2U, 2U);
	i915_host_check(set == (0x02U | 0x04U), "adjust caps swing at the table");

	/* Pre-emphasis 2 leaves room for swing 1 only. */
	status[4] = 0x0bU;
	set = drv_i915_skl_adjust_train(status, 1U, 3U);
	i915_host_check(set == 0x11U, "adjust caps swing by pre-emphasis");

	/* Pre-emphasis 3 is marked reached and allows swing 0. */
	status[4] = 0x0fU;
	set = drv_i915_skl_adjust_train(status, 1U, 3U);
	i915_host_check(set == (0x18U | 0x20U), "adjust marks pre-emphasis 3 reached");

	/* Clock recovery on two lanes; equalisation needs the alignment too. */
	memset(status, 0, sizeof(status));
	status[0] = 0x11U;
	i915_host_check(drv_i915_skl_dp_cr_ok(status, 2U) && !drv_i915_skl_dp_cr_ok(status, 4U), "CR done on two of four lanes");
	status[0] = 0x77U;
	status[1] = 0x77U;
	i915_host_check(!drv_i915_skl_dp_eq_ok(status, 4U), "EQ waits for the alignment");
	status[2] = 0x01U;
	i915_host_check(drv_i915_skl_dp_eq_ok(status, 4U), "EQ done with the alignment");
	status[1] = 0x37U;
	i915_host_check(!drv_i915_skl_dp_eq_ok(status, 4U), "EQ needs symbol lock on every lane");
}

/* Checks the DDB split and levels of 1080p alone, 1080p with 720p, and three 4K pipes, and the words. */
static void
i915_host_watermarks(void)
{
	struct i915_skl_pipe_wm pipes[3];
	struct i915_skl_wm_level level;
	unsigned pipe;
	int error;

	/* One 1080p pipe: the whole DBUF, 32 cursor blocks, every level. */
	memset(pipes, 0, sizeof(pipes));
	pipes[0].active = 1;
	pipes[0].hdisplay = 1920U;
	pipes[0].htotal = 2200U;
	pipes[0].pixel_khz = 148500U;
	pipes[0].plane_width = 1920U;
	error = drv_i915_skl_wm_compute(pipes, i915_host_latency);
	i915_host_check(error == 0 && pipes[0].ddb_start == 0U && pipes[0].ddb_end == 892U, "wm one pipe takes the DBUF");
	i915_host_check(pipes[0].plane_start == 0U && pipes[0].plane_end == 860U && pipes[0].cursor_start == 860U && pipes[0].cursor_end == 892U, "wm one pipe plane and cursor");
	i915_host_check(i915_host_level_is(&pipes[0].level[0], 4U, 0U) && i915_host_level_is(&pipes[0].level[1], 17U, 1U), "wm one pipe levels 0 and 1");
	i915_host_check(i915_host_level_is(&pipes[0].level[3], 23U, 2U) && i915_host_level_is(&pipes[0].level[7], 107U, 7U), "wm one pipe levels 3 and 7");
	i915_host_check(pipes[0].linetime == 119U, "wm one pipe line time");

	/* 1080p and 720p: split by width, eight-block cursors at least. */
	pipes[1].active = 1;
	pipes[1].hdisplay = 1280U;
	pipes[1].htotal = 1650U;
	pipes[1].pixel_khz = 74250U;
	pipes[1].plane_width = 1280U;
	error = drv_i915_skl_wm_compute(pipes, i915_host_latency);
	i915_host_check(error == 0 && pipes[0].ddb_end == 535U && pipes[0].cursor_start == 511U && pipes[0].plane_end == 511U, "wm two pipes first share");
	i915_host_check(pipes[1].ddb_start == 535U && pipes[1].ddb_end == 892U && pipes[1].cursor_start == 874U && pipes[1].plane_end == 874U, "wm two pipes second share");
	i915_host_check(i915_host_level_is(&pipes[1].level[0], 3U, 0U) && i915_host_level_is(&pipes[1].level[7], 55U, 5U) && pipes[1].linetime == 178U, "wm two pipes second levels");

	/* Three 4K pipes: levels 6 and 7 do not fit and are off. */
	memset(pipes, 0, sizeof(pipes));
	for (pipe = 0U; pipe < 3U; pipe++) {
		pipes[pipe].active = 1;
		pipes[pipe].hdisplay = 3840U;
		pipes[pipe].htotal = 4400U;
		pipes[pipe].pixel_khz = 594000U;
		pipes[pipe].plane_width = 3840U;
	}

	/* Their split. */
	error = drv_i915_skl_wm_compute(pipes, i915_host_latency);
	i915_host_check(error == 0 && pipes[1].ddb_start == 297U && pipes[1].ddb_end == 594U && pipes[1].cursor_start == 552U, "wm three pipes split");
	i915_host_check(i915_host_level_is(&pipes[2].level[5], 188U, 6U) && !pipes[2].level[6].enable && !pipes[2].level[7].enable, "wm three pipes trim levels 6 and 7");
	i915_host_check(pipes[0].linetime == 60U, "wm three pipes line time");

	/* The words. */
	memset(&level, 0, sizeof(level));
	level.enable = 1;
	level.blocks = 107U;
	level.lines = 7U;
	i915_host_check(drv_i915_skl_wm_word(&level) == (0x80000000U | (7U << 14) | 107U), "wm word");
	i915_host_check(drv_i915_skl_ddb_word(535U, 874U) == ((873U << 16) | 535U) && drv_i915_skl_ddb_word(0U, 0U) == 0U, "ddb words");
}

/* Reports nonzero when a level is enabled with these blocks and lines. */
static int
i915_host_level_is(
	const struct i915_skl_wm_level *level,
	uint32_t blocks,
	uint32_t lines)
{
	/* Each field. */
	if (!level->enable)
		return 0;
	if (level->blocks != blocks || level->lines != lines)
		return 0;

	/* Succeeded: the level matches. */
	return 1;
}

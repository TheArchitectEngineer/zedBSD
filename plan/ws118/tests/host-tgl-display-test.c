/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Tiger Lake display answers (ws118-p006).
 *
 * The takeover of the 5320's firmware display failed because the modeset
 * environment answered IS_DISPLAY_VER() with a fixed 13: on display 12 the
 * combo PHYs were denied, the readout found no PLL, and the sanitize turned
 * off the PLL the firmware's pipe ran on.  This test checks the answers the
 * fix changed, on display 12 and on display 13 (which must not change):
 * the combo PHY predicate, the P5 DPLL tables, the CDCLK hooks, and the
 * pipe scaler readout and disable on a register map.
 *
 *   sh plan/ws118/tests/run-tgl-display-host-test.sh
 */

#include "drivers/gpu/i915/tests/display/host-test.h"

#include "drivers/gpu/i915/display/modeset-internal.h"
#include "drivers/gpu/i915/display/clock.h"
#include "drivers/gpu/i915/display/modeset.h"
#include "drivers/gpu/i915/display/pipe.h"
#include "drivers/gpu/i915/display/takeover.h"

#include <stdio.h>
#include <string.h>

/* The registers of the map: pipe A's two scalers and their windows. */
#define TGL_TEST_REGS 6

/*
 * A small register map standing in for the display.
 *
 * Reads answer the stored word (0 for a register not in the map); writes
 * store it and count.
 */
struct tgl_test_regs {
	uint32_t reg[TGL_TEST_REGS];
	uint32_t value[TGL_TEST_REGS];
	unsigned writes;
};

/*
 * The display the test builds its world on.
 *
 * It lives for the whole run; only its modeset world is used.
 */
static struct i915_display tgl_test_display;

static int tgl_test_find(struct tgl_test_regs *regs, uint32_t reg);
static uint32_t tgl_test_read32(void *ctx, uint32_t reg);
static void tgl_test_write32(void *ctx, uint32_t reg, uint32_t value);
static void tgl_test_step(void *ctx, const char *name);
static void tgl_test_combo(void);
static void tgl_test_dplls(void);
static void tgl_test_cdclk(void);
static void tgl_test_scaler(void);

/*
 * Runs every check and reports the tally.
 */
int
main(void)
{
	int error;
	int status;

	/* The modeset world the platform predicates answer from. */
	error = drv_i915_lcd_world_create(&tgl_test_display);
	if (error != 0) {
		printf("host-tgl-display: no modeset world (%d)\n", error);
		return 1;
	}

	/* Runs the checks. */
	tgl_test_combo();
	tgl_test_dplls();
	tgl_test_cdclk();
	tgl_test_scaler();

	/* Reports the tally. */
	drv_i915_lcd_world_destroy(&tgl_test_display);
	status = i915_host_report("host-tgl-display");

	/* Succeeded or failed as the tally says. */
	return status;
}

/* Finds a register's slot in the map, or -1. */
static int
tgl_test_find(
	struct tgl_test_regs *regs,
	uint32_t reg)
{
	int index;

	/* Looks the register up. */
	for (index = 0; index < TGL_TEST_REGS; index++) {
		if (regs->reg[index] == reg)
			return index;
	}

	/* Not in the map. */
	return -1;
}

/* Reads a register of the map. */
static uint32_t
tgl_test_read32(
	void *ctx,
	uint32_t reg)
{
	struct tgl_test_regs *regs;
	int index;

	/* A register outside the map reads 0. */
	regs = ctx;
	index = tgl_test_find(regs, reg);
	if (index < 0)
		return 0;

	/* Succeeded: reports the stored word. */
	return regs->value[index];
}

/* Writes a register of the map and counts the write. */
static void
tgl_test_write32(
	void *ctx,
	uint32_t reg,
	uint32_t value)
{
	struct tgl_test_regs *regs;
	int index;

	/* Counts every write, stored or not. */
	regs = ctx;
	regs->writes++;

	/* Stores the word of a register in the map. */
	index = tgl_test_find(regs, reg);
	if (index >= 0)
		regs->value[index] = value;
}

/* Reports an unported step the tested code reached (none is expected). */
static void
tgl_test_step(
	void *ctx,
	const char *name)
{
	(void)ctx;

	/* An unported step is a failure of this test. */
	printf("host-tgl-display: unexpected step %s\n", name);
	i915_host_check(0, "no unported step is reached");
}

/* Checks the combo PHY predicate on display 12 and 13. */
static void
tgl_test_combo(void)
{
	bool phy_a;
	bool phy_b;
	bool phy_c;

	/* Tiger Lake: PHY A and B are combo PHYs, C is not. */
	drv_i915_lcd_set_display_ver(&tgl_test_display, 12);
	phy_a = drv_i915_phy_is_combo(NULL, PHY_A);
	phy_b = drv_i915_phy_is_combo(NULL, PHY_B);
	phy_c = drv_i915_phy_is_combo(NULL, PHY_C);
	i915_host_check(phy_a && phy_b && !phy_c, "display 12: PHY A and B are combo PHYs, C is not");

	/* Alder Lake-P keeps the same answer. */
	drv_i915_lcd_set_display_ver(&tgl_test_display, 13);
	phy_a = drv_i915_phy_is_combo(NULL, PHY_A);
	phy_b = drv_i915_phy_is_combo(NULL, PHY_B);
	phy_c = drv_i915_phy_is_combo(NULL, PHY_C);
	i915_host_check(phy_a && phy_b && !phy_c, "display 13: PHY A and B are combo PHYs, C is not");

	/* IS_DISPLAY_VER() answers from the device's version. */
	drv_i915_lcd_set_display_ver(&tgl_test_display, 12);
	i915_host_check(I915_LCD_IS_DISPLAY_VER(NULL, 10, 12) && !I915_LCD_IS_DISPLAY_VER(NULL, 13, 13),
			"display 12: IS_DISPLAY_VER(10, 12) holds, (13, 13) does not");
	drv_i915_lcd_set_display_ver(&tgl_test_display, 13);
	i915_host_check(!I915_LCD_IS_DISPLAY_VER(NULL, 10, 12) && I915_LCD_IS_DISPLAY_VER(NULL, 12, 13),
			"display 13: IS_DISPLAY_VER(10, 12) does not hold, (12, 13) does");
}

/* Checks the P5 DPLL tables of both platforms. */
static void
tgl_test_dplls(void)
{
	static struct i915_display_nogem nogem;

	/* Tiger Lake: tgl_plls[], the Type-C PLLs at MG_PLL_ENABLE. */
	memset(&nogem, 0, sizeof(nogem));
	drv_i915_shared_dpll_init(&nogem, 12, 0);
	i915_host_check(nogem.dpll_mgr_present == 1 &&
			nogem.num_dplls == 9U &&
			nogem.dplls[0].enable_reg == 0x46010U &&
			nogem.dplls[1].enable_reg == 0x46014U &&
			nogem.dplls[2].enable_reg == 0x46020U &&
			nogem.dplls[3].enable_reg == 0x46030U &&
			nogem.dplls[8].enable_reg == 0x46044U &&
			nogem.dplls[8].id == 8,
			"display 12: tgl_plls = DPLL0/1, TBT, TC1..6 at 0x46030 + 4n");

	/* Alder Lake-P keeps adlp_plls[]. */
	memset(&nogem, 0, sizeof(nogem));
	drv_i915_shared_dpll_init(&nogem, 13, 1);
	i915_host_check(nogem.dpll_mgr_present == 1 &&
			nogem.num_dplls == 7U &&
			nogem.dplls[3].enable_reg == 0x46038U &&
			nogem.dplls[6].enable_reg == 0x46050U,
			"display 13: adlp_plls = DPLL0/1, TBT, TC1..4 at 0x46038 + 8n");
}

/* Checks the CDCLK hooks of both platforms. */
static void
tgl_test_cdclk(void)
{
	static struct i915_cdclk_dev cd;

	/* Tiger Lake: tgl functions, no crawl. */
	memset(&cd, 0, sizeof(cd));
	drv_i915_init_cdclk_hooks(&cd, 12, 0, 0);
	i915_host_check(cd.funcs == I915_CDCLK_FUNCS_TGL && cd.has_cdclk_crawl == 0 && cd.has_cdclk_squash == 0,
			"display 12: tgl CDCLK functions, no crawl, no squash");

	/* Alder Lake-P crawls. */
	memset(&cd, 0, sizeof(cd));
	drv_i915_init_cdclk_hooks(&cd, 13, 3, 1);
	i915_host_check(cd.funcs == I915_CDCLK_FUNCS_TGL && cd.has_cdclk_crawl == 1 && cd.has_cdclk_squash == 0,
			"display 13: tgl CDCLK functions, crawl, no squash");
}

/* Checks the scaler readout and disable on pipe A. */
static void
tgl_test_scaler(void)
{
	static struct drm_i915_private i915;
	static struct intel_crtc crtc;
	static struct intel_crtc_state crtc_state;
	static struct i915_lcd_emit emit;
	static struct tgl_test_regs regs;
	int index;
	int cleared;

	/* The registers: scaler 1 of pipe A off, scaler 2 fitting the pipe into 1366x768 at (0, 0). */
	memset(&regs, 0, sizeof(regs));
	regs.reg[0] = 0x68180U;
	regs.reg[1] = 0x68170U;
	regs.reg[2] = 0x68174U;
	regs.reg[3] = 0x68280U;
	regs.reg[4] = 0x68270U;
	regs.reg[5] = 0x68274U;
	regs.value[3] = 0x80000000U;
	regs.value[4] = 0x00000000U;
	regs.value[5] = (1366U << 16) | 768U;

	/* The device and the crtc of pipe A, reading through the map. */
	memset(&emit, 0, sizeof(emit));
	emit.ctx = &regs;
	emit.read32 = tgl_test_read32;
	emit.write32 = tgl_test_write32;
	emit.step = tgl_test_step;
	memset(&i915, 0, sizeof(i915));
	i915.emit = &emit;
	memset(&crtc, 0, sizeof(crtc));
	crtc.base.dev = &i915.drm;
	crtc.pipe = PIPE_A;
	memset(&crtc_state, 0, sizeof(crtc_state));
	crtc_state.uapi.crtc = &crtc.base;
	crtc_state.scaler_state.scaler_id = -1;

	/* The readout finds scaler 2 and its window. */
	drv_i915_lcd_set_display_ver(&tgl_test_display, 12);
	drv_i915_skl_scaler_get_config(&crtc_state);
	i915_host_check(crtc_state.pch_pfit.enabled &&
			crtc_state.scaler_state.scaler_id == 1 &&
			crtc_state.scaler_state.scalers[1].in_use &&
			!crtc_state.scaler_state.scalers[0].in_use &&
			(crtc_state.scaler_state.scaler_users & 0x80000000U) != 0U,
			"readout: scaler 2 of pipe A is the pipe's panel fitter");
	i915_host_check(crtc_state.pch_pfit.dst.x1 == 0 &&
			crtc_state.pch_pfit.dst.y1 == 0 &&
			crtc_state.pch_pfit.dst.x2 == 1366 &&
			crtc_state.pch_pfit.dst.y2 == 768,
			"readout: the fitter's window is 1366x768 at (0, 0)");

	/* A scaler bound to a plane is not the pipe's. */
	regs.value[3] = 0x80000000U | (1U << 25);
	memset(&crtc_state.pch_pfit, 0, sizeof(crtc_state.pch_pfit));
	memset(&crtc_state.scaler_state, 0, sizeof(crtc_state.scaler_state));
	drv_i915_skl_scaler_get_config(&crtc_state);
	i915_host_check(!crtc_state.pch_pfit.enabled && crtc_state.scaler_state.scaler_id == -1,
			"readout: a plane's scaler is not a panel fitter");

	/* The disable clears both scalers of the pipe and their windows. */
	regs.value[3] = 0x80000000U;
	regs.writes = 0U;
	drv_i915_skl_scaler_disable(&crtc_state);
	cleared = 1;
	for (index = 0; index < TGL_TEST_REGS; index++) {
		if (regs.value[index] != 0U)
			cleared = 0;
	}
	i915_host_check(cleared && regs.writes == 6U, "disable: both scalers of pipe A and their windows are cleared (6 writes)");

	/* Pipe B's scalers are 0x800 above pipe A's. */
	crtc.pipe = PIPE_B;
	regs.reg[0] = 0x68980U;
	regs.value[0] = 0x80000000U;
	drv_i915_skl_scaler_disable(&crtc_state);
	i915_host_check(regs.value[0] == 0U, "disable: pipe B's scaler 1 is at 0x68980");
}

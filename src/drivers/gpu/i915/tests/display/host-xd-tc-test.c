/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the xd Type-C PHYs (display/xd-tc.c) and of the
 * Type-C PLLs of the xd clock (display/xd-clock.c), on the register model
 * (xd-fake-regs.c) with a Dekel PHY behind the windows that follows the
 * bank index: the windows and bank index of the ports, the lock around
 * each access, another route to the PHY; the microcontroller's health;
 * the lanes' DisplayPort mode of each pin assignment; the swing tables and
 * the transmit words of Alder Lake-P and Tiger Lake, with the load
 * generators; the soft reset; the PLL words of the 5330's USB-C link (TC
 * PLL 2 at 1.62 GHz on 38.4 MHz), of other rates, references and HDMI
 * clocks, and the link clocks they give; the enable and read of a TC PLL.
 */

#include "host-test.h"
#include "xd-fake-regs.h"

#include "../../display/xd-clock.h"
#include "../../display/xd-io.h"
#include "../../display/xd-platform.h"
#include "../../display/xd-tc.h"
#include "../../intel/display-regs-xd.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The GPUs of the Latitude 5330 (Alder Lake-P) and of a Tiger Lake laptop. */
#define I915_XD_TC_TEST_ADLP		0x46a6U
#define I915_XD_TC_TEST_ADLP_REVID	0x0cU
#define I915_XD_TC_TEST_TGL		0x9a49U
#define I915_XD_TC_TEST_TGL_REVID	0x01U

/* The windows of the six ports, their PHY's register space, and the 5330's USB-C port (TC2, 1 from 0). */
#define I915_XD_TC_TEST_WINDOWS		0x168000U
#define I915_XD_TC_TEST_WINDOWS_END	0x16e000U
#define I915_XD_TC_TEST_PHY_WORDS	0x4000U
#define I915_XD_TC_TEST_M3_TC		1U

/*
 * The 5330's TC PLL 2 for its USB-C DisplayPort at 1.62 GHz on a 38.4 MHz
 * reference, as Linux left it (plan/ws051/tests/m3-5330-20261007, debugfs
 * i915_shared_dplls_info), and the raw registers behind those words.
 */
static const struct i915_xd_tc_pll_words i915_xd_tc_test_m3 = {
	0x100U, 0xa00U, 0x6200U, 0x84269U, 0x1c0027U, 0x40002000U, 0x5e000000U, 0x52U, 0
};

/*
 * A Dekel PHY per port behind the windows: its register space by PHY
 * offset, the bank index each index register holds, the time the
 * microcontroller reports health from (never when 0), and the accesses of
 * another route.
 */
static struct {
	uint32_t phy[I915_XD_TC_PORTS][I915_XD_TC_TEST_PHY_WORDS];
	uint32_t index[2];
	uint64_t health_at;
	struct i915_xd_io_hooks fake;
	unsigned routed;
} i915_xd_tc_test_model;

/* The window and the Dekel PHY windows the checks share. */
static struct i915_xd_io i915_xd_tc_test_io;
static struct i915_xd_dkl i915_xd_tc_test_dkl;

static const struct i915_xd_platform *i915_xd_tc_test_setup(uint16_t device, uint8_t revid);
static uint32_t *i915_xd_tc_test_phy(unsigned tc, uint32_t phy_offset);
static int i915_xd_tc_test_window(uint32_t offset, unsigned *tc, uint32_t *phy_offset);
static uint32_t i915_xd_tc_test_read(void *context, uint32_t offset);
static void i915_xd_tc_test_write(void *context, uint32_t offset, uint32_t value);
static uint32_t i915_xd_tc_test_route_read(void *context, unsigned tc, uint32_t phy_offset);
static void i915_xd_tc_test_route_write(void *context, unsigned tc, uint32_t phy_offset, uint32_t value);
static void i915_xd_tc_test_route_rmw(void *context, unsigned tc, uint32_t phy_offset, uint32_t clear, uint32_t set);
static int i915_xd_tc_test_words_equal(const struct i915_xd_tc_pll_words *a, const struct i915_xd_tc_pll_words *b);
static void i915_xd_tc_test_windows(void);
static void i915_xd_tc_test_health(void);
static void i915_xd_tc_test_dp_mode(void);
static void i915_xd_tc_test_swing(void);
static void i915_xd_tc_test_soft_reset(void);
static void i915_xd_tc_test_pll_words(void);
static void i915_xd_tc_test_pll_enable(void);
static void i915_xd_tc_test_pll_read(void);

/*
 * Runs every check of the xd Type-C PHYs and PLLs and reports.
 */
int
main(void)
{
	int failures;

	/* Runs each group of checks. */
	i915_xd_tc_test_windows();
	i915_xd_tc_test_health();
	i915_xd_tc_test_dp_mode();
	i915_xd_tc_test_swing();
	i915_xd_tc_test_soft_reset();
	i915_xd_tc_test_pll_words();
	i915_xd_tc_test_pll_enable();
	i915_xd_tc_test_pll_read();

	/* Reports the result. */
	failures = i915_host_report("host_xd_tc_test");
	if (failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Starts a fresh model with empty PHYs whose PLL power and lock follow
 * their requests, and windows that reach it; returns the description of
 * a device.
 */
static const struct i915_xd_platform *
i915_xd_tc_test_setup(
	uint16_t device,
	uint8_t revid)
{
	static const uint32_t enables[] = {
		0x46030U, 0x46034U, 0x46038U, 0x4603cU, 0x46040U, 0x46044U, 0x46048U, 0x4604cU, 0x46050U, 0x46054U
	};
	struct i915_xd_io_hooks hooks;
	struct i915_xd_choice choice;
	unsigned index;
	int error;

	/* A fresh model and PHYs. */
	i915_xd_fake_reset();
	memset(&i915_xd_tc_test_model, 0, sizeof(i915_xd_tc_test_model));
	i915_xd_fake_hooks(&i915_xd_tc_test_model.fake);

	/* The window reaches the PHYs through the test's hooks, everything else the model. */
	hooks = i915_xd_tc_test_model.fake;
	hooks.read = i915_xd_tc_test_read;
	hooks.write = i915_xd_tc_test_write;
	drv_i915_xd_io_init(&i915_xd_tc_test_io, NULL, NULL);
	drv_i915_xd_io_set_hooks(&i915_xd_tc_test_io, &hooks);
	drv_i915_xd_dkl_init(&i915_xd_tc_test_dkl, &i915_xd_tc_test_io);

	/* The PLL enables of both platforms: the power state follows the request, the lock the enable. */
	for (index = 0U; index < sizeof(enables) / sizeof(enables[0]); index++) {
		i915_xd_fake_rule_mirror(enables[index], I915_XD_PLL_POWER_ENABLE, enables[index], -1, 0U);
		i915_xd_fake_rule_mirror(enables[index], I915_XD_PLL_ENABLE, enables[index], -1, 0U);
	}

	/* The description. */
	error = drv_i915_xd_platform_choose(device, revid, 0, 0U, &choice);
	if (error != 0)
		return NULL;

	/* Succeeded: the description. */
	return choice.platform;
}

/* The word of a port's PHY at a PHY offset. */
static uint32_t *
i915_xd_tc_test_phy(
	unsigned tc,
	uint32_t phy_offset)
{
	/* Succeeded: the word. */
	return &i915_xd_tc_test_model.phy[tc][(phy_offset / 4U) % I915_XD_TC_TEST_PHY_WORDS];
}

/* Tells whether a register is in a port's window, and which PHY offset the bank index makes of it. */
static int
i915_xd_tc_test_window(
	uint32_t offset,
	unsigned *tc,
	uint32_t *phy_offset)
{
	uint32_t bank;

	/* Outside the windows. */
	if (offset < I915_XD_TC_TEST_WINDOWS || offset >= I915_XD_TC_TEST_WINDOWS_END)
		return 0;

	/* The port, and the bank its byte of its index register holds. */
	*tc = (offset - I915_XD_TC_TEST_WINDOWS) / 0x1000U;
	bank = (i915_xd_tc_test_model.index[*tc / 4U] >> (8U * (*tc % 4U))) & 0xffU;
	*phy_offset = (bank << 12) | (offset & 0xfffU);

	/* Succeeded: a window. */
	return 1;
}

/* Reads a register: a PHY through its window, or the model. */
static uint32_t
i915_xd_tc_test_read(
	void *context,
	uint32_t offset)
{
	uint32_t phy_offset;
	unsigned tc;
	int window;

	/* The microcontroller's health comes at its time. */
	if (i915_xd_tc_test_model.health_at != 0U && i915_xd_fake_now() >= i915_xd_tc_test_model.health_at) {
		for (tc = 0U; tc < I915_XD_TC_PORTS; tc++)
			*i915_xd_tc_test_phy(tc, I915_XD_DKL_CMN_UC_DW27) |= I915_XD_DKL_UC_HEALTH;
	}

	/* A window reads the PHY. */
	window = i915_xd_tc_test_window(offset, &tc, &phy_offset);
	if (window)
		return *i915_xd_tc_test_phy(tc, phy_offset);

	/* Succeeded: the model's register. */
	return i915_xd_tc_test_model.fake.read(context, offset);
}

/* Writes a register: a PHY through its window, the index registers and the rest into the model. */
static void
i915_xd_tc_test_write(
	void *context,
	uint32_t offset,
	uint32_t value)
{
	uint32_t phy_offset;
	unsigned tc;
	int window;

	/* A window writes the PHY. */
	window = i915_xd_tc_test_window(offset, &tc, &phy_offset);
	if (window) {
		*i915_xd_tc_test_phy(tc, phy_offset) = value;
		return;
	}

	/* The index registers keep their banks. */
	if (offset == 0x1010a0U)
		i915_xd_tc_test_model.index[0] = value;
	if (offset == 0x1010a4U)
		i915_xd_tc_test_model.index[1] = value;

	/* The model takes it. */
	i915_xd_tc_test_model.fake.write(context, offset, value);
}

/* Another route's read: the PHY directly, counted. */
static uint32_t
i915_xd_tc_test_route_read(
	void *context,
	unsigned tc,
	uint32_t phy_offset)
{
	(void)context;

	/* Succeeded: counted and read. */
	i915_xd_tc_test_model.routed++;
	return *i915_xd_tc_test_phy(tc, phy_offset);
}

/* Another route's write: the PHY directly, counted. */
static void
i915_xd_tc_test_route_write(
	void *context,
	unsigned tc,
	uint32_t phy_offset,
	uint32_t value)
{
	(void)context;

	/* Counted and written. */
	i915_xd_tc_test_model.routed++;
	*i915_xd_tc_test_phy(tc, phy_offset) = value;
}

/* Another route's change: the PHY directly, counted. */
static void
i915_xd_tc_test_route_rmw(
	void *context,
	unsigned tc,
	uint32_t phy_offset,
	uint32_t clear,
	uint32_t set)
{
	uint32_t *word;

	(void)context;

	/* Counted and changed. */
	i915_xd_tc_test_model.routed++;
	word = i915_xd_tc_test_phy(tc, phy_offset);
	*word = (*word & ~clear) | set;
}

/* Tells whether two sets of PLL words are the same. */
static int
i915_xd_tc_test_words_equal(
	const struct i915_xd_tc_pll_words *a,
	const struct i915_xd_tc_pll_words *b)
{
	/* Word by word. */
	if (a->refclkin != b->refclkin || a->coreclk != b->coreclk || a->hsclk != b->hsclk)
		return 0;
	if (a->div0 != b->div0 || a->div1 != b->div1 || a->ssc != b->ssc)
		return 0;
	if (a->bias != b->bias || a->tdc != b->tdc)
		return 0;

	/* Succeeded: the same. */
	return 1;
}

/*
 * The windows: where a PHY register of a port is reached and the bank
 * its index register takes; a read and a write through the windows of
 * two ports under the window lock; another route takes every access.
 */
static void
i915_xd_tc_test_windows(void)
{
	struct i915_xd_dkl_hooks hooks;
	uint32_t index_reg;
	uint32_t index_value;
	uint32_t window;
	uint32_t value;
	unsigned writes;

	/* TC1's CMN_UC_DW27: bank 2 in byte 0 of the low index register. */
	window = drv_i915_xd_dkl_window(0U, 0x236cU, &index_reg, &index_value);
	i915_host_check(window == 0x16836cU, "windows: TC1's microcontroller word is at 0x16836c");
	i915_host_check(index_reg == 0x1010a0U && index_value == 0x2U, "windows: TC1 selects bank 2 in byte 0 of 0x1010a0");

	/* TC2: byte 1. */
	window = drv_i915_xd_dkl_window(1U, 0x236cU, &index_reg, &index_value);
	i915_host_check(window == 0x16936cU && index_reg == 0x1010a0U && index_value == 0x200U, "windows: TC2's window and bank");

	/* TC5: the high index register, byte 0. */
	window = drv_i915_xd_dkl_window(4U, 0x1014U, &index_reg, &index_value);
	i915_host_check(window == 0x16c014U && index_reg == 0x1010a4U && index_value == 0x1U, "windows: TC5's window and bank");

	/* A write and a read through the windows reach the bank the index names. */
	(void)i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
	drv_i915_xd_dkl_write(&i915_xd_tc_test_dkl, 1U, 0x2204U, 0x1234U);
	drv_i915_xd_dkl_write(&i915_xd_tc_test_dkl, 5U, 0x10a0U, 0x55U);
	value = drv_i915_xd_dkl_read(&i915_xd_tc_test_dkl, 1U, 0x2204U);
	i915_host_check(value == 0x1234U && *i915_xd_tc_test_phy(1U, 0x2204U) == 0x1234U, "windows: TC2's bank 2 word written and read back");
	i915_host_check(*i915_xd_tc_test_phy(5U, 0x10a0U) == 0x55U && i915_xd_fake_get(0x1010a4U) == 0x100U,
		"windows: TC6's bank 1 word, byte 1 of the high index register");
	drv_i915_xd_dkl_rmw(&i915_xd_tc_test_dkl, 1U, 0x2204U, 0x34U, 0x1U);
	i915_host_check(*i915_xd_tc_test_phy(1U, 0x2204U) == 0x1201U, "windows: a change keeps the other bits");
	i915_host_check(i915_xd_fake_lock_violations() == 0U && i915_xd_fake_locks_held() == 0U,
		"windows: every access took and gave back the window lock");

	/* Windows that are not ready read all-ones and write nothing. */
	i915_xd_tc_test_dkl.ready = 0;
	writes = i915_xd_fake_writes();
	value = drv_i915_xd_dkl_read(&i915_xd_tc_test_dkl, 1U, 0x2204U);
	drv_i915_xd_dkl_write(&i915_xd_tc_test_dkl, 1U, 0x2204U, 0U);
	i915_host_check(value == 0xffffffffU && i915_xd_fake_writes() == writes && *i915_xd_tc_test_phy(1U, 0x2204U) == 0x1201U,
		"windows: windows that are not ready read all-ones and write nothing");

	/* Another route takes every access; no index register is written. */
	(void)i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
	hooks.read = i915_xd_tc_test_route_read;
	hooks.write = i915_xd_tc_test_route_write;
	hooks.rmw = i915_xd_tc_test_route_rmw;
	hooks.context = NULL;
	drv_i915_xd_dkl_set_hooks(&i915_xd_tc_test_dkl, &hooks);
	drv_i915_xd_dkl_write(&i915_xd_tc_test_dkl, 0U, 0x00a0U, 0x7U);
	drv_i915_xd_dkl_rmw(&i915_xd_tc_test_dkl, 0U, 0x00a0U, 0x1U, 0x8U);
	value = drv_i915_xd_dkl_read(&i915_xd_tc_test_dkl, 0U, 0x00a0U);
	i915_host_check(value == 0xeU && i915_xd_tc_test_model.routed == 3U && i915_xd_fake_writes() == 0U,
		"windows: another route takes the write, the change and the read, and no register is written");
}

/*
 * The microcontroller's health: a PHY that reports it after 200 us, and
 * one that never does.
 */
static void
i915_xd_tc_test_health(void)
{
	uint64_t start;
	int error;

	/* Health after 200 us: the wait ends then. */
	(void)i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
	start = i915_xd_fake_now();
	i915_xd_tc_test_model.health_at = start + 200U;
	error = drv_i915_xd_tc_wait_health(&i915_xd_tc_test_dkl, I915_XD_TC_TEST_M3_TC, I915_XD_DKL_HEALTH_US);
	i915_host_check(error == 0, "health: the microcontroller reports after 200 us");
	i915_host_check(i915_xd_fake_now() - start >= 200U && i915_xd_fake_now() - start < 220U, "health: the wait ends within a poll of the report");
	i915_host_check(i915_xd_fake_get(0x1010a0U) == 0x200U, "health: TC2 reads bank 2");

	/* No health: the wait runs out after a millisecond. */
	(void)i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
	start = i915_xd_fake_now();
	error = drv_i915_xd_tc_wait_health(&i915_xd_tc_test_dkl, 0U, I915_XD_DKL_HEALTH_US);
	i915_host_check(error == ETIMEDOUT && i915_xd_fake_now() - start >= I915_XD_DKL_HEALTH_US, "health: a silent microcontroller times out after 1 ms");
}

/*
 * The lanes' DisplayPort mode: each pin assignment and lane count, the
 * other bits of the mode words kept, an assignment the PHY has no mode
 * for.
 */
static void
i915_xd_tc_test_dp_mode(void)
{
	static const struct {
		unsigned pin;
		unsigned lanes;
		uint32_t lane0;
		uint32_t lane1;
		const char *what;
	} cases[] = {
		{ I915_XD_TC_PIN_LEGACY, 1U, 0U, I915_XD_DKL_DP_MODE_X1, "dp mode: legacy x1 is lane 1's single transmitter" },
		{ I915_XD_TC_PIN_LEGACY, 4U, I915_XD_DKL_DP_MODE_X2, I915_XD_DKL_DP_MODE_X2, "dp mode: legacy x4 doubles both lanes" },
		{ I915_XD_TC_PIN_A, 4U, I915_XD_DKL_DP_MODE_X2, I915_XD_DKL_DP_MODE_X2, "dp mode: assignment A x4 doubles both lanes" },
		{ I915_XD_TC_PIN_A, 2U, 0U, 0U, "dp mode: assignment A x2 sets no mode" },
		{ I915_XD_TC_PIN_B, 2U, I915_XD_DKL_DP_MODE_X2, I915_XD_DKL_DP_MODE_X2, "dp mode: assignment B x2 doubles both lanes" },
		{ I915_XD_TC_PIN_B, 4U, 0U, 0U, "dp mode: assignment B x4 sets no mode" },
		{ I915_XD_TC_PIN_C, 4U, I915_XD_DKL_DP_MODE_X2, I915_XD_DKL_DP_MODE_X2, "dp mode: assignment C x4 doubles both lanes" },
		{ I915_XD_TC_PIN_D, 2U, I915_XD_DKL_DP_MODE_X2, I915_XD_DKL_DP_MODE_X2, "dp mode: assignment D x2 doubles both lanes" },
		{ I915_XD_TC_PIN_E, 1U, I915_XD_DKL_DP_MODE_X1, I915_XD_DKL_DP_MODE_X1, "dp mode: assignment E x1 is both lanes single" },
		{ I915_XD_TC_PIN_F, 1U, I915_XD_DKL_DP_MODE_X1, I915_XD_DKL_DP_MODE_X1, "dp mode: assignment F x1 is both lanes single" },
	};
	uint32_t kept;
	unsigned index;
	int error;

	/* Each case from mode words that hold both modes and another bit. */
	kept = 0x5U;
	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++) {
		/* The PHY of the 5330's port with both modes set. */
		(void)i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
		*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_DP_MODE) = kept | I915_XD_DKL_DP_MODE_X1 | I915_XD_DKL_DP_MODE_X2;
		*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_DP_MODE + I915_XD_DKL_LANE_STRIDE) =
			kept | I915_XD_DKL_DP_MODE_X1 | I915_XD_DKL_DP_MODE_X2;

		/* The modes of the case, the other bit kept. */
		error = drv_i915_xd_tc_dp_mode(&i915_xd_tc_test_dkl, I915_XD_TC_TEST_M3_TC, cases[index].pin, cases[index].lanes);
		i915_host_check(error == 0 &&
			*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_DP_MODE) == (kept | cases[index].lane0) &&
			*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_DP_MODE + I915_XD_DKL_LANE_STRIDE) == (kept | cases[index].lane1),
			cases[index].what);
	}

	/* An assignment the PHY has no mode for: refused, the lanes left without a mode. */
	error = drv_i915_xd_tc_dp_mode(&i915_xd_tc_test_dkl, I915_XD_TC_TEST_M3_TC, 7U, 4U);
	i915_host_check(error == EINVAL && *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_DP_MODE) == kept,
		"dp mode: assignment 7 is refused and leaves no mode");
}

/*
 * The swing: the tables each output and rate takes on Alder Lake-P and
 * Tiger Lake, and the transmit words of a DisplayPort link (each lane's
 * row, the 20-bit mode off, the load generators of Alder Lake-P) and of
 * HDMI.
 */
static void
i915_xd_tc_test_swing(void)
{
	static const uint8_t dp_levels[4] = { 0U, 1U, 2U, 3U };
	static const uint8_t hdmi_levels[4] = { 9U, 9U, 9U, 12U };
	const struct i915_xd_dkl_swing_table *table;
	const struct i915_xd_platform *platform;
	uint32_t word;

	/* Alder Lake-P: DisplayPort up to 2.7 GHz and above, HDMI's table with row 9 as its default. */
	platform = i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
	table = drv_i915_xd_tc_swing_table(platform, 0, 162000U);
	i915_host_check(table != NULL && table->count == 10U && table->rows[0].vswing == 0x7U && table->rows[0].de_emphasis == 0x01U,
		"swing: Alder Lake-P RBR takes its own low table (row 0: 0x7, 0x01)");
	table = drv_i915_xd_tc_swing_table(platform, 0, 540000U);
	i915_host_check(table != NULL && table->rows[1].vswing == 0x5U && table->rows[1].de_emphasis == 0x04U,
		"swing: Alder Lake-P HBR2 takes its high table (row 1: 0x5, 0x04)");
	table = drv_i915_xd_tc_swing_table(platform, 1, 297000U);
	i915_host_check(table != NULL && table->hdmi_default == 9U && table->rows[9].de_emphasis == 0x0aU,
		"swing: HDMI takes the Tiger Lake HDMI table (default row 9: de-emphasis 0xa)");

	/* A DisplayPort link at RBR: rows 0..3 in the two transmitters of the two lanes, load generators 0. */
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_PMD_LANE_SUS) = 0xffU;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL2) = 0x80000000U | I915_XD_DKL_TX_DP20BITMODE | 0x78U;
	table = drv_i915_xd_tc_swing_table(platform, 0, 162000U);
	drv_i915_xd_tc_signal_levels(&i915_xd_tc_test_dkl, platform, I915_XD_TC_TEST_M3_TC, table, dp_levels, 0);
	i915_host_check(*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_PMD_LANE_SUS) == 0U,
		"swing: lane 0 leaves its suspend");
	word = *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL0);
	i915_host_check(word == ((0x01U << 8) | 0x7U), "swing: lane 0's first transmitter takes row 0 (0x7, 0x01, 0)");
	word = *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL1);
	i915_host_check(word == ((0x06U << 8) | 0x5U), "swing: lane 0's second transmitter takes row 1 (0x5, 0x06, 0)");
	word = *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL0 + I915_XD_DKL_LANE_STRIDE);
	i915_host_check(word == ((0x0bU << 8) | 0x2U), "swing: lane 1's first transmitter takes row 2 (0x2, 0x0b, 0)");
	word = *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL1 + I915_XD_DKL_LANE_STRIDE);
	i915_host_check(word == (0x17U << 8), "swing: lane 1's second transmitter takes row 3 (0x0, 0x17, 0)");
	word = *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL2);
	i915_host_check(word == 0x80000000U, "swing: lane 0 leaves the 20-bit mode and selects load generators 0, other bits kept");

	/* HDMI on Alder Lake-P: row 9, a level past the table its last row, the HDMI load generators. */
	table = drv_i915_xd_tc_swing_table(platform, 1, 297000U);
	drv_i915_xd_tc_signal_levels(&i915_xd_tc_test_dkl, platform, I915_XD_TC_TEST_M3_TC, table, hdmi_levels, 1);
	word = *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL1 + I915_XD_DKL_LANE_STRIDE);
	i915_host_check(word == (0x0aU << 8), "swing: a level past the HDMI table takes its last row");
	word = *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL2);
	i915_host_check((word & 0x78U) == 0x40U, "swing: HDMI lane 0 selects load generators 0 and 2");
	word = *i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_TX_DPCNTL2 + I915_XD_DKL_LANE_STRIDE);
	i915_host_check((word & 0x78U) == 0x78U, "swing: HDMI lane 1 selects load generators 3 and 3");

	/* Tiger Lake: its own tables, no load generator select. */
	platform = i915_xd_tc_test_setup(I915_XD_TC_TEST_TGL, I915_XD_TC_TEST_TGL_REVID);
	table = drv_i915_xd_tc_swing_table(platform, 0, 540000U);
	i915_host_check(table != NULL && table->rows[3].de_emphasis == 0x19U, "swing: Tiger Lake HBR2 takes its high table (row 3: de-emphasis 0x19)");
	*i915_xd_tc_test_phy(0U, I915_XD_DKL_TX_DPCNTL2) = I915_XD_DKL_TX_DP20BITMODE | 0x78U;
	drv_i915_xd_tc_signal_levels(&i915_xd_tc_test_dkl, platform, 0U, table, dp_levels, 0);
	i915_host_check(*i915_xd_tc_test_phy(0U, I915_XD_DKL_TX_DPCNTL2) == 0x78U, "swing: Tiger Lake keeps the load generators it found");
}

/*
 * The soft reset of both lanes' PCS: the bit cleared, the rest kept.
 */
static void
i915_xd_tc_test_soft_reset(void)
{
	/* Both lanes in reset with other bits. */
	(void)i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PCS_DW5) = I915_XD_DKL_PCS_DW5_CORE_SOFTRESET | 0x3U;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PCS_DW5 + I915_XD_DKL_LANE_STRIDE) = I915_XD_DKL_PCS_DW5_CORE_SOFTRESET;

	/* Released. */
	drv_i915_xd_tc_release_soft_reset(&i915_xd_tc_test_dkl, I915_XD_TC_TEST_M3_TC);
	i915_host_check(*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PCS_DW5) == 0x3U &&
		*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PCS_DW5 + I915_XD_DKL_LANE_STRIDE) == 0U,
		"soft reset: both lanes leave it, other bits kept");
}

/*
 * The PLL words: the 5330's USB-C link, the link clocks of the four
 * DisplayPort rates on the three references, an HDMI clock (the words
 * Linux v6.8.12's own calculation gives), the VBT's AFC startup, and the
 * refusals.
 */
static void
i915_xd_tc_test_pll_words(void)
{
	static const uint32_t refs[] = { 19200U, 24000U, 38400U };
	static const uint32_t rates[] = { 162000U, 270000U, 540000U, 810000U };
	struct i915_xd_tc_pll_words words;
	unsigned ref;
	unsigned rate;
	unsigned wrong;
	uint32_t port_khz;
	int error;

	/* The 5330's TC PLL 2 at RBR on 38.4 MHz. */
	error = drv_i915_xd_tc_pll_words(38400U, 162000U, 0, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(error == 0 && i915_xd_tc_test_words_equal(&words, &i915_xd_tc_test_m3), "pll words: the 5330's TC PLL 2 words at RBR");
	i915_host_check(words.refclkin == 0x100U && words.coreclk == 0xa00U && words.hsclk == 0x6200U, "pll words: clock tops 0x100, 0xa00, 0x6200");
	i915_host_check(words.div0 == 0x84269U && words.div1 == 0x1c0027U, "pll words: DIV0 0x84269, DIV1 0x1c0027");
	i915_host_check(words.ssc == 0x40002000U && words.bias == 0x5e000000U && words.tdc == 0x52U, "pll words: SSC 0x40002000, BIAS 0x5e000000, TDC 0x52");
	port_khz = drv_i915_xd_tc_pll_port_khz(38400U, &words);
	i915_host_check(port_khz == 162000U, "pll words: the 5330's words give 162000 kHz");

	/* Every DisplayPort rate on every reference gives its own rate back. */
	wrong = 0U;
	for (ref = 0U; ref < sizeof(refs) / sizeof(refs[0]); ref++) {
		/* The rates of this reference. */
		for (rate = 0U; rate < sizeof(rates) / sizeof(rates[0]); rate++) {
			error = drv_i915_xd_tc_pll_words(refs[ref], rates[rate], 0, I915_XD_DPLL_AFC_NONE, &words);
			port_khz = drv_i915_xd_tc_pll_port_khz(refs[ref], &words);
			if (error != 0 || port_khz != rates[rate])
				wrong++;
		}
	}
	i915_host_check(wrong == 0U, "pll words: RBR, HBR, HBR2 and HBR3 on 19.2, 24 and 38.4 MHz give their rates back");

	/* An eDP rate between them puts no DCO at 8.1 GHz. */
	error = drv_i915_xd_tc_pll_words(38400U, 216000U, 0, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(error == EINVAL, "pll words: 2.16 GHz has no dividers on a Type-C port");

	/* HDMI 148.5 MHz on 38.4 MHz: Linux's words. */
	error = drv_i915_xd_tc_pll_words(38400U, 148500U, 1, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(error == 0 && words.hsclk == 0x15400U && words.coreclk == 0x500U, "pll words: HDMI 148.5 MHz clock tops 0x15400, 0x500");
	i915_host_check(words.div0 == 0x84274U && words.bias == 0x41000000U && words.tdc == 0x4aU, "pll words: HDMI 148.5 MHz DIV0 0x84274, BIAS 0x41000000, TDC 0x4a");
	port_khz = drv_i915_xd_tc_pll_port_khz(38400U, &words);
	i915_host_check(port_khz == 148500U, "pll words: HDMI 148.5 MHz gives 148500 kHz back");

	/* The VBT's AFC startup goes into DIV0 and is marked to be written. */
	error = drv_i915_xd_tc_pll_words(38400U, 162000U, 0, 5, &words);
	i915_host_check(error == 0 && words.div0 == (0x84269U | (5U << 25)) && words.afc_set == 1, "pll words: the VBT's AFC startup 5 in DIV0");

	/* Refusals: a reference the PHY does not take, a clock no divider fits. */
	error = drv_i915_xd_tc_pll_words(25000U, 162000U, 0, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(error == EINVAL, "pll words: a 25 MHz reference is refused");
	error = drv_i915_xd_tc_pll_words(38400U, 100000U, 0, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(error == EINVAL, "pll words: a DisplayPort rate of 1 GHz has no dividers");
}

/*
 * The enable of a TC PLL: Alder Lake-P's TC PLL 2 powered, written into
 * TC2's PHY in bank 2 (other bits of the registers kept, DIV0's AFC
 * startup kept when the words do not carry it), enabled and locked; Tiger
 * Lake's TC PLL 1 at its own enable; a PLL that is not a Type-C one.
 */
static void
i915_xd_tc_test_pll_enable(void)
{
	const struct i915_xd_platform *platform;
	const struct i915_xd_pll_desc *pll;
	struct i915_xd_tc_pll_words words;
	unsigned late;
	int enabled;

	/* Alder Lake-P's TC PLL 2 (select 4) with the PHY's other bits set. */
	platform = i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
	pll = drv_i915_xd_dpll_desc(platform, 4U);
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_DIV0) = 0x7e000000U;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_REFCLKIN_CTL) = 0x1U;
	late = drv_i915_xd_tc_pll_enable(&i915_xd_tc_test_io, &i915_xd_tc_test_dkl, pll, &i915_xd_tc_test_m3);
	i915_host_check(pll != NULL && pll->enable == 0x46040U && late == 0U, "pll enable: TC PLL 2 at 0x46040 powers and locks in time");
	i915_host_check((i915_xd_fake_get(0x46040U) & (I915_XD_PLL_POWER_ENABLE | I915_XD_PLL_ENABLE)) == (I915_XD_PLL_POWER_ENABLE | I915_XD_PLL_ENABLE),
		"pll enable: power and enable requested");
	i915_host_check(i915_xd_fake_get(0x46034U) == 0U, "pll enable: Tiger Lake's MG PLL 2 enable is not touched");
	i915_host_check(i915_xd_fake_get(0x1010a0U) == 0x200U, "pll enable: TC2 selects bank 2");
	i915_host_check(*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_DIV0) == 0x7e084269U,
		"pll enable: DIV0 keeps the AFC startup and takes 0x84269");
	i915_host_check(*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_REFCLKIN_CTL) == 0x101U,
		"pll enable: REFCLKIN keeps bit 0 and takes 0x100");
	i915_host_check(*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_TDC_COLDST_BIAS) == 0x52U &&
		*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_BIAS) == 0x5e000000U,
		"pll enable: BIAS and TDC written");

	/* Read back, the words are the same. */
	memset(&words, 0, sizeof(words));
	enabled = drv_i915_xd_tc_pll_read(&i915_xd_tc_test_io, &i915_xd_tc_test_dkl, pll, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(enabled == 1 && i915_xd_tc_test_words_equal(&words, &i915_xd_tc_test_m3), "pll enable: the read gives the written words back");
	i915_host_check(i915_xd_fake_lock_violations() == 0U && i915_xd_fake_locks_held() == 0U, "pll enable: the window lock was taken and given back");

	/* The disable through the xd clock clears it. */
	late = drv_i915_xd_dpll_disable(&i915_xd_tc_test_io, pll);
	i915_host_check(late == 0U && (i915_xd_fake_get(0x46040U) & (I915_XD_PLL_POWER_ENABLE | I915_XD_PLL_ENABLE)) == 0U,
		"pll enable: the disable clears the power and the enable");

	/* Tiger Lake's TC PLL 1 (select 3) at its MG PLL enable. */
	platform = i915_xd_tc_test_setup(I915_XD_TC_TEST_TGL, I915_XD_TC_TEST_TGL_REVID);
	pll = drv_i915_xd_dpll_desc(platform, 3U);
	late = drv_i915_xd_tc_pll_enable(&i915_xd_tc_test_io, &i915_xd_tc_test_dkl, pll, &i915_xd_tc_test_m3);
	i915_host_check(pll != NULL && late == 0U && (i915_xd_fake_get(0x46030U) & I915_XD_PLL_ENABLE) != 0U &&
		*i915_xd_tc_test_phy(0U, I915_XD_DKL_PLL_DIV0) == 0x84269U,
		"pll enable: Tiger Lake's TC PLL 1 at 0x46030, written into TC1's PHY");

	/* The Thunderbolt PLL is not a Type-C PLL. */
	pll = drv_i915_xd_dpll_desc(platform, 2U);
	late = drv_i915_xd_tc_pll_enable(&i915_xd_tc_test_io, &i915_xd_tc_test_dkl, pll, &i915_xd_tc_test_m3);
	i915_host_check(late == (I915_XD_DPLL_LATE_POWER_ON | I915_XD_DPLL_LATE_LOCK), "pll enable: the Thunderbolt PLL is refused");
}

/*
 * The read of a TC PLL: the 5330's registers as Linux left them (the
 * fields the PLL does not use set) give its words; a PLL that is off has
 * none.
 */
static void
i915_xd_tc_test_pll_read(void)
{
	const struct i915_xd_platform *platform;
	const struct i915_xd_pll_desc *pll;
	struct i915_xd_tc_pll_words words;
	uint32_t port_khz;
	int enabled;

	/* The 5330's TC2 registers (plan/ws051/tests/m3-5330-20261007). */
	platform = i915_xd_tc_test_setup(I915_XD_TC_TEST_ADLP, I915_XD_TC_TEST_ADLP_REVID);
	pll = drv_i915_xd_dpll_desc(platform, 4U);
	i915_xd_fake_set(0x46040U, 0xcc000000U);
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_REFCLKIN_CTL) = 0x00000101U;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_CLKTOP2_HSCLKCTL) = 0x0000621dU;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_CLKTOP2_CORECLKCTL1) = 0x10080a10U;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_DIV0) = 0x7e284269U;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_DIV1) = 0x0cdcc427U;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_SSC) = 0x400020ffU;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_BIAS) = 0xde000000U;
	*i915_xd_tc_test_phy(I915_XD_TC_TEST_M3_TC, I915_XD_DKL_PLL_TDC_COLDST_BIAS) = 0x00000052U;
	memset(&words, 0, sizeof(words));
	enabled = drv_i915_xd_tc_pll_read(&i915_xd_tc_test_io, &i915_xd_tc_test_dkl, pll, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(enabled == 1, "pll read: TC PLL 2 reads as enabled");
	i915_host_check(i915_xd_tc_test_words_equal(&words, &i915_xd_tc_test_m3), "pll read: the read words are debugfs's TC PLL 2 words");
	port_khz = drv_i915_xd_tc_pll_port_khz(38400U, &words);
	i915_host_check(port_khz == 162000U, "pll read: the read words give 162000 kHz");

	/* With the AFC startup asked for, DIV0 carries it. */
	enabled = drv_i915_xd_tc_pll_read(&i915_xd_tc_test_io, &i915_xd_tc_test_dkl, pll, 0, &words);
	i915_host_check(enabled == 1 && words.div0 == 0x0e084269U && words.afc_set == 1, "pll read: the AFC startup is read when asked for");

	/* Off: no state. */
	i915_xd_fake_set(0x46040U, 0x0U);
	enabled = drv_i915_xd_tc_pll_read(&i915_xd_tc_test_io, &i915_xd_tc_test_dkl, pll, I915_XD_DPLL_AFC_NONE, &words);
	i915_host_check(enabled == 0, "pll read: TC PLL 2 with its enable clear reads as off");
}

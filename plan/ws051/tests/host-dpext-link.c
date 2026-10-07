/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws051-p004b: the host test of an external DP display's link
 * (drv_i915_lcd_compute_dp_ext(), src/drivers/gpu/i915/display/state.c):
 *
 *   - the 5330's USB-C monitor (DP-2: RBR, HBR and HBR2 shared, 4 lanes,
 *     1920x1280 at 164.36 MHz) takes RBR x4 at 24 bpp, the link Linux
 *     trained on the machine, and TC PLL words equal to the ones Linux
 *     left (plan/ws051/tests/m3-5330-20261007);
 *   - the fallback's limits move the link (2 lanes: HBR x2), and limits no
 *     link fits under refuse with ENOSPC;
 *   - a branch's downstream bits per colour lower the depth, its dot clock
 *     limit refuses the mode, and a sink without a shared rate is invalid.
 *
 *   sh plan/ws051/tests/host-dpext-link.sh
 */

#include "drivers/gpu/i915/display/modeset-internal.h"
#include "drivers/gpu/i915/display/state.h"
#include "drivers/gpu/i915/tests/display/host-test.h"

#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>

/* The reference clock of the 5330 (kHz). */
#define M3_REF_KHZ	38400

static void monitor_sink(struct i915_dp_ext_sink *sink);
static void monitor_mode(struct i915_lcd_mode *mode);
static void test_monitor(void);
static void test_limits(void);
static void test_branch(void);

/*
 * Runs the checks and reports the tally.
 */
int
main(int argc, char **argv)
{
	int report;
	int verbose_asked;

	/* -v prints every check. */
	verbose_asked = 0;
	if (argc > 1)
		verbose_asked = (strcmp(argv[1], "-v") == 0);
	if (verbose_asked)
		i915_host_verbose = 1;

	/* Runs each part. */
	test_monitor();
	test_limits();
	test_branch();

	/* Reports the tally. */
	report = i915_host_report("host-dpext-link");
	return report;
}

/* The 5330's USB-C monitor as the probe found it: RBR, HBR, HBR2 shared, 4 lanes, not a branch. */
static void
monitor_sink(
	struct i915_dp_ext_sink *sink)
{
	/* A connected DPCD 1.4 sink. */
	memset(sink, 0, sizeof(*sink));
	sink->status = I915_DP_EXT_CONNECTED;
	sink->dpcd[0] = 0x14;
	sink->dpcd_valid = 1;

	/* The shared rates and lanes. */
	sink->common_rates[0] = 162000;
	sink->common_rates[1] = 270000;
	sink->common_rates[2] = 540000;
	sink->num_common_rates = 3;
	sink->max_rate = 540000;
	sink->max_sink_lanes = 4;
	sink->max_lanes = 4;
}

/* The monitor's preferred mode: 1920x1280 at 164.36 MHz. */
static void
monitor_mode(
	struct i915_lcd_mode *mode)
{
	/* The timing of the EDID's first descriptor. */
	memset(mode, 0, sizeof(*mode));
	mode->clock_khz = 164360;
	mode->hdisplay = 1920;
	mode->hsync_start = 1968;
	mode->hsync_end = 2000;
	mode->htotal = 2080;
	mode->vdisplay = 1280;
	mode->vsync_start = 1283;
	mode->vsync_end = 1293;
	mode->vtotal = 1317;
	mode->edid_bpc = 8;
}

/* The monitor's link is the one Linux trained, and its TC PLL the one Linux programmed. */
static void
test_monitor(void)
{
	struct i915_dp_ext_sink sink;
	struct i915_lcd_mode mode;
	struct i915_lcd_state state;
	int error;

	/* Computes the link without limits. */
	monitor_sink(&sink);
	monitor_mode(&mode);
	error = drv_i915_lcd_compute_dp_ext(&mode, &sink, 0, 0, M3_REF_KHZ, &state);
	i915_host_check(error == 0, "monitor: the link is computed");
	i915_host_check(state.link.rate_khz == 162000 && state.link.lanes == 4, "monitor: RBR x4 (the link Linux trained, TC PLL 2 at RBR)");
	i915_host_check(state.link.bpp == 24, "monitor: 24 bpp");
	i915_host_check(state.link.required_kbps <= state.link.available_kbps, "monitor: the link carries the mode");
	i915_host_check(state.link.data_n != 0 && state.link.link_n != 0, "monitor: M/N computed");
	i915_host_check(state.mode.hdisplay == 1920 && state.mode.vdisplay == 1280, "monitor: the mode is kept");

	/* The TC PLL words of RBR on the 38.4 MHz reference (debugfs i915_shared_dplls_info). */
	i915_host_check(state.pll.dkl.div0 == 0x84269u && state.pll.dkl.div1 == 0x1c0027u, "monitor: TC PLL div0/div1 as Linux left them");
	i915_host_check(state.pll.dkl.refclkin_ctl == 0x100u && state.pll.dkl.clktop2_hsclkctl == 0x6200u, "monitor: TC PLL clock tops as Linux left them");
	i915_host_check(state.pll.ref_khz == M3_REF_KHZ, "monitor: the reference clock is kept");
}

/* The fallback's limits move the link, and limits no link fits under refuse. */
static void
test_limits(void)
{
	struct i915_dp_ext_sink sink;
	struct i915_lcd_mode mode;
	struct i915_lcd_state state;
	int error;

	/* Two lanes: HBR x2 is the first that carries the mode. */
	monitor_sink(&sink);
	monitor_mode(&mode);
	error = drv_i915_lcd_compute_dp_ext(&mode, &sink, 540000, 2, M3_REF_KHZ, &state);
	i915_host_check(error == 0 && state.link.rate_khz == 270000 && state.link.lanes == 2, "limits: two lanes give HBR x2");

	/* RBR at two lanes carries nothing deep enough, not even 18 bpp. */
	error = drv_i915_lcd_compute_dp_ext(&mode, &sink, 162000, 2, M3_REF_KHZ, &state);
	i915_host_check(error == ENOSPC, "limits: RBR x2 carries no depth of the mode (ENOSPC)");

	/* RBR at four lanes is the link when HBR is excluded. */
	error = drv_i915_lcd_compute_dp_ext(&mode, &sink, 162000, 4, M3_REF_KHZ, &state);
	i915_host_check(error == 0 && state.link.rate_khz == 162000 && state.link.lanes == 4, "limits: RBR x4 under the RBR limit");

	/* A sink that shares no rate is not a sink to drive. */
	sink.num_common_rates = 0;
	error = drv_i915_lcd_compute_dp_ext(&mode, &sink, 0, 0, M3_REF_KHZ, &state);
	i915_host_check(error == EINVAL, "limits: no shared rate is EINVAL");
}

/* A branch's downstream limits lower the depth or refuse the mode. */
static void
test_branch(void)
{
	struct i915_dp_ext_sink sink;
	struct i915_lcd_mode mode;
	struct i915_lcd_state state;
	int error;

	/* A branch that passes 6 bits per colour: 18 bpp. */
	monitor_sink(&sink);
	monitor_mode(&mode);
	sink.branch = 1;
	sink.max_bpc = 6;
	error = drv_i915_lcd_compute_dp_ext(&mode, &sink, 0, 0, M3_REF_KHZ, &state);
	i915_host_check(error == 0 && state.link.bpp == 18, "branch: 6 bpc downstream gives 18 bpp");

	/* A deep-colour EDID is still driven at 8 bpc. */
	sink.max_bpc = 0;
	mode.edid_bpc = 10;
	error = drv_i915_lcd_compute_dp_ext(&mode, &sink, 0, 0, M3_REF_KHZ, &state);
	i915_host_check(error == 0 && state.link.bpp == 24, "branch: a 10 bpc display is driven at 24 bpp");

	/* A dot clock limit below the mode refuses it. */
	sink.max_dotclock_khz = 148500;
	error = drv_i915_lcd_compute_dp_ext(&mode, &sink, 0, 0, M3_REF_KHZ, &state);
	i915_host_check(error == ENOSPC, "branch: a mode above the downstream dot clock is ENOSPC");
}

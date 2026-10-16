/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A screen of the display 12 and 13 code: one output, its pipe and the
 * plan of its mode set (xd-screen.c).
 *
 * The plan is a pure function of the mode, the sink, the platform's
 * description and what the display runs now: the link (rate, lanes, bits
 * per pixel, M/N), the combo PLL's words, the display buffer's slices for
 * the pipes that run together, and the check that the mode needs no
 * other CD clock, no other voltage level and no more memory bandwidth
 * than the allowed QGV point gives.  Nothing is written.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_XD_SCREEN_H
#define DRIVERS_GPU_I915_DISPLAY_XD_SCREEN_H

#include <stdint.h>

#include "display-internal.h"
#include "xd-clock.h"
#include "xd-dp.h"
#include "xd-platform.h"

/* The kinds of output a screen drives: the built-in panel, an HDMI or DVI sink, an external DisplayPort sink. */
#define I915_XD_SCREEN_PANEL		1U
#define I915_XD_SCREEN_HDMI		2U
#define I915_XD_SCREEN_DP		3U

/*
 * Why a plan was refused: a sink the plan does not drive (a rate code or
 * lane count it does not know, an eDP 1.4 panel's rate table), a mode no
 * link carries, a link rate without PLL words, a mode that needs another
 * CD clock (or more than the highest), one that needs another voltage
 * level, and one that needs more memory bandwidth than the allowed QGV
 * point.
 */
#define I915_XD_PLAN_OK			0U
#define I915_XD_PLAN_SINK		1U
#define I915_XD_PLAN_LINK		2U
#define I915_XD_PLAN_PLL		3U
#define I915_XD_PLAN_CDCLK		4U
#define I915_XD_PLAN_VOLTAGE		5U
#define I915_XD_PLAN_QGV		6U

/*
 * What the plan knows of the sink.  A panel: its DPCD rate code and lane
 * count, its eDP revision, and the VBT's bits per pixel cap (0: none).  An
 * external DisplayPort sink: the rates both ends share (lowest first),
 * its lanes, a branch's bits per colour cap and dot clock and TMDS clock
 * limits (0: none), and the fallback's rate and lane limits (0: none).
 */
struct i915_xd_sink {
	uint8_t rate_code;
	unsigned max_lanes;
	uint8_t edp_revision;
	unsigned vbt_bpp;
	const uint32_t *rates;
	unsigned nrates;
	unsigned max_bpc;
	uint32_t max_dotclock_khz;
	uint32_t min_tmds_khz;
	uint32_t max_tmds_khz;
	uint32_t rate_limit_khz;
	unsigned lane_limit;
};

/*
 * What a plan is computed from: the kind of output, its DDI and pipe, the
 * mode, the sink, the platform's description and the PLL reference, the
 * pipes that run together (this one among them), and for the check the
 * CD clock now and its highest step, the bandwidth of the allowed QGV
 * point (MB/s) and the plane's bytes per pixel.
 */
struct i915_xd_plan_input {
	unsigned kind;
	unsigned ddi;
	unsigned pipe;
	const struct i915_display_mode *mode;
	struct i915_xd_sink sink;
	const struct i915_xd_platform *platform;
	uint32_t ref_khz;
	uint8_t run_pipes;
	struct i915_xd_cdclk cdclk;
	uint32_t cdclk_max_khz;
	uint32_t qgv_mbps;
	unsigned cpp;
};

/*
 * A plan: the link, its M/N, the combo PLL's words (pll_set zero when the
 * port's PLL is not a combo DPLL), the display buffer's slices and
 * whether the bus is joined, the check's numbers (pixel and data rates,
 * the CD clock the mode needs and the one it gets, its VCO and voltage
 * level), and why it was refused (I915_XD_PLAN_OK when it was not).
 */
struct i915_xd_plan {
	unsigned kind;
	unsigned pipe;
	struct i915_display_mode mode;
	uint32_t rate_khz;
	unsigned lanes;
	unsigned bpp;
	uint32_t need_kbps;
	uint32_t carry_kbps;
	struct i915_xd_dp_m_n m_n;
	struct i915_xd_dpll_words pll;
	int pll_set;
	uint8_t slices;
	uint8_t joined;
	uint32_t pixel_khz;
	uint32_t data_mbps;
	uint32_t min_cdclk_khz;
	uint32_t cdclk_khz;
	uint32_t vco_khz;
	uint8_t min_voltage;
	uint8_t voltage;
	unsigned refused;
};

unsigned drv_i915_xd_screen_plan_link(const struct i915_xd_plan_input *input, struct i915_xd_plan *plan);
unsigned drv_i915_xd_screen_plan_check(const struct i915_xd_plan_input *input, struct i915_xd_plan *plan);
unsigned drv_i915_xd_screen_plan(const struct i915_xd_plan_input *input, struct i915_xd_plan *plan);
const char *drv_i915_xd_screen_refusal_name(unsigned refused);

#endif /* DRIVERS_GPU_I915_DISPLAY_XD_SCREEN_H */

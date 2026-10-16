/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A screen of the display 12 and 13 code (see xd-screen.h).
 */

#include "xd-screen.h"
#include "xd-clock.h"
#include "xd-dbuf.h"
#include "xd-dp.h"
#include "xd-platform.h"
#include "display-internal.h"

#include "../intel/display-dpcd-xd.h"

#include <kern/kcrt.h>

#include <stddef.h>
#include <stdint.h>

/*
 * The eDP revision from which a panel lists its own link rates (eDP 1.4),
 * and the bandwidth overhead of a single stream link in parts per
 * million of the payload (none on an 8b/10b link).
 *
 * Values referenced: Linux v6.8.12 include/drm/display/drm_dp.h
 * (DP_EDP_14), drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_bw_fec_overhead without FEC or DSC).
 */
#define I915_XD_SCREEN_EDP_14		0x03U
#define I915_XD_SCREEN_SST_OVERHEAD	1000000U

/*
 * The bits per colour an external DisplayPort sink is driven at, at most
 * and at least; the bits per pixel of a sink that names no depth and of
 * an HDMI sink.
 */
#define I915_XD_SCREEN_DP_MAX_BPC	8U
#define I915_XD_SCREEN_DP_MIN_BPC	6U
#define I915_XD_SCREEN_DEFAULT_BPP	24U

/*
 * The port clock above which display 12 and 13 need voltage level 2, and
 * that level.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (tgl_ddi_min_voltage_level).
 */
#define I915_XD_SCREEN_HIGH_PORT_KHZ	594000U
#define I915_XD_SCREEN_HIGH_PORT_LEVEL	2U

/*
 * The CD clock a pipe's memory read bandwidth needs: its data rate times
 * 10 over 512, from display 12 on.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_bw.c
 * (intel_bw_crtc_min_cdclk).
 */
#define I915_XD_SCREEN_BW_CDCLK_MUL	10U
#define I915_XD_SCREEN_BW_CDCLK_DIV	512U

static unsigned i915_xd_screen_panel_link(const struct i915_xd_plan_input *input, struct i915_xd_plan *plan);
static unsigned i915_xd_screen_hdmi_link(const struct i915_xd_plan_input *input, struct i915_xd_plan *plan);
static unsigned i915_xd_screen_dp_link(const struct i915_xd_plan_input *input, struct i915_xd_plan *plan);
static unsigned i915_xd_screen_combo_pll(const struct i915_xd_plan_input *input, struct i915_xd_plan *plan);
static uint32_t i915_xd_screen_round_up(uint64_t value, uint32_t divisor);

/*
 * Computes the link part of a plan: the link that carries the mode, its
 * M/N and the combo PLL's words.  Returns the refusal (I915_XD_PLAN_OK
 * when the link was found), also kept in plan->refused.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_compute_link_config, intel_dp_compute_link_config_wide,
 * intel_dp_link_required, intel_dp_max_data_rate), intel_hdmi.c
 * (intel_hdmi_compute_config, intel_hdmi_tmds_clock), intel_dpll_mgr.c
 * (icl_calc_dp_combo_pll, icl_calc_wrpll).
 */
unsigned
drv_i915_xd_screen_plan_link(
	const struct i915_xd_plan_input *input,
	struct i915_xd_plan *plan)
{
	unsigned refused;

	/* Starts from the mode and nothing chosen. */
	kern_memset(plan, 0, sizeof(*plan));
	plan->kind = input->kind;
	plan->pipe = input->pipe;
	plan->mode = *input->mode;

	/* A mode without a clock is no mode a link carries. */
	if (input->mode->clock_khz <= 0) {
		plan->refused = I915_XD_PLAN_LINK;
		return plan->refused;
	}

	/* The link of the kind of output. */
	if (input->kind == I915_XD_SCREEN_PANEL) {
		refused = i915_xd_screen_panel_link(input, plan);
	} else if (input->kind == I915_XD_SCREEN_HDMI) {
		refused = i915_xd_screen_hdmi_link(input, plan);
	} else {
		refused = i915_xd_screen_dp_link(input, plan);
	}
	if (refused != I915_XD_PLAN_OK) {
		plan->refused = refused;
		return refused;
	}

	/* A DisplayPort link's M/N. */
	if (input->kind != I915_XD_SCREEN_HDMI)
		drv_i915_xd_dp_m_n(plan->bpp * 16U, plan->lanes, (uint32_t)input->mode->clock_khz, plan->rate_khz, I915_XD_SCREEN_SST_OVERHEAD, &plan->m_n);

	/* The PLL's words, when the port runs on a combo DPLL. */
	refused = i915_xd_screen_combo_pll(input, plan);
	plan->refused = refused;

	/* Succeeded: the refusal, or none. */
	return refused;
}

/*
 * Computes the check part of a plan whose link is found: the pixel and
 * data rates, the CD clock the mode needs (the pixel rate's, the plane's
 * and the memory read bandwidth's; on Tiger Lake the pixel rate itself),
 * the step that gives it and its voltage level with the port's own need;
 * the display buffer's slices for the pipes that run.  The mode is
 * refused when it needs another CD clock or more than the highest,
 * another voltage level, or more bandwidth than the allowed QGV point.
 * Returns the refusal, also kept in plan->refused.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_cdclk.c
 * (intel_crtc_compute_min_cdclk, intel_pixel_rate_to_cdclk,
 * intel_planes_min_cdclk, bxt_calc_cdclk, bxt_modeset_calc_cdclk,
 * intel_cdclk_changed), skl_universal_plane.c (icl_plane_min_cdclk),
 * intel_bw.c (intel_bw_crtc_data_rate, intel_bw_crtc_min_cdclk,
 * intel_bw_check_qgv_points), skl_watermark.c (skl_compute_ddb).
 */
unsigned
drv_i915_xd_screen_plan_check(
	const struct i915_xd_plan_input *input,
	struct i915_xd_plan *plan)
{
	const struct i915_xd_platform *platform;
	uint32_t data_kbps;
	uint32_t need;
	uint32_t bandwidth;
	uint8_t level;

	/* A plan already refused is not checked. */
	if (plan->refused != I915_XD_PLAN_OK)
		return plan->refused;

	/* The display buffer's slices for the pipes that run together. */
	platform = input->platform;
	plan->joined = drv_i915_xd_dbuf_joined(platform, input->run_pipes);
	plan->slices = drv_i915_xd_dbuf_slices(platform, input->pipe, input->run_pipes, plan->joined);

	/* The pixel rate (no scaling) and the plane's data rate. */
	plan->pixel_khz = (uint32_t)input->mode->clock_khz;
	data_kbps = plan->pixel_khz * input->cpp;
	plan->data_mbps = i915_xd_screen_round_up(data_kbps, 1000U);

	/* Two pixels per CD clock for the pipe and for its plane; the memory read bandwidth. */
	need = i915_xd_screen_round_up(plan->pixel_khz, 2U);
	bandwidth = i915_xd_screen_round_up((uint64_t)data_kbps * I915_XD_SCREEN_BW_CDCLK_MUL, I915_XD_SCREEN_BW_CDCLK_DIV);
	if (bandwidth > need)
		need = bandwidth;

	/* Tiger Lake can underrun at half the pixel rate: the pixel rate itself, up to the highest step. */
	if (platform->kind == I915_XD_KIND_TGL) {
		if (plan->pixel_khz < input->cdclk_max_khz && plan->pixel_khz > need)
			need = plan->pixel_khz;
		if (plan->pixel_khz >= input->cdclk_max_khz && input->cdclk_max_khz > need)
			need = input->cdclk_max_khz;
	}
	plan->min_cdclk_khz = need;

	/* No step above the highest. */
	if (need > input->cdclk_max_khz) {
		plan->refused = I915_XD_PLAN_CDCLK;
		return plan->refused;
	}

	/* The step that gives it, its VCO and its voltage level with the port's need. */
	plan->cdclk_khz = drv_i915_xd_cdclk_lowest(platform, input->cdclk.ref_khz, need);
	if (plan->cdclk_khz == 0U) {
		plan->refused = I915_XD_PLAN_CDCLK;
		return plan->refused;
	}
	plan->vco_khz = drv_i915_xd_cdclk_vco(platform, &input->cdclk, plan->cdclk_khz);
	plan->min_voltage = 0U;
	if (plan->rate_khz > I915_XD_SCREEN_HIGH_PORT_KHZ)
		plan->min_voltage = I915_XD_SCREEN_HIGH_PORT_LEVEL;
	level = drv_i915_xd_cdclk_voltage(platform, plan->cdclk_khz);
	if (level < plan->min_voltage)
		level = plan->min_voltage;
	plan->voltage = level;

	/* The CD clock is not changed by a mode set: it must be the one running. */
	if (plan->cdclk_khz != input->cdclk.cdclk_khz || plan->vco_khz != input->cdclk.vco_khz) {
		plan->refused = I915_XD_PLAN_CDCLK;
		return plan->refused;
	}
	if (plan->voltage != input->cdclk.voltage) {
		plan->refused = I915_XD_PLAN_VOLTAGE;
		return plan->refused;
	}

	/* The allowed QGV point must carry the plane. */
	if (input->qgv_mbps == 0U || plan->data_mbps > input->qgv_mbps) {
		plan->refused = I915_XD_PLAN_QGV;
		return plan->refused;
	}

	/* Succeeded: the mode needs nothing the display does not give. */
	return I915_XD_PLAN_OK;
}

/*
 * Computes a whole plan: its link, then its check (see the two parts).
 * Returns the refusal, also kept in plan->refused.
 */
unsigned
drv_i915_xd_screen_plan(
	const struct i915_xd_plan_input *input,
	struct i915_xd_plan *plan)
{
	unsigned refused;

	/* The link, then the check. */
	refused = drv_i915_xd_screen_plan_link(input, plan);
	if (refused != I915_XD_PLAN_OK)
		return refused;

	/* Succeeded: the check's refusal, or none. */
	return drv_i915_xd_screen_plan_check(input, plan);
}

/*
 * Names a plan's refusal for the log.
 */
const char *
drv_i915_xd_screen_refusal_name(
	unsigned refused)
{
	/* One name per refusal. */
	switch (refused) {
	case I915_XD_PLAN_OK:
		return "none";
	case I915_XD_PLAN_SINK:
		return "a sink the plan does not drive";
	case I915_XD_PLAN_LINK:
		return "no link carries the mode";
	case I915_XD_PLAN_PLL:
		return "no PLL words for the link rate";
	case I915_XD_PLAN_CDCLK:
		return "another CD clock";
	case I915_XD_PLAN_VOLTAGE:
		return "another voltage level";
	case I915_XD_PLAN_QGV:
		return "more memory bandwidth than the QGV point";
	default:
		break;
	}

	/* Succeeded: a value no refusal has. */
	return "unknown";
}

/*
 * The panel's link: the sink's highest rate and lanes (an eDP panel older
 * than 1.4 is trained at its maximum), the bits per pixel of its EDID
 * depth capped by the VBT; refused when the link cannot carry the mode.
 */
static unsigned
i915_xd_screen_panel_link(
	const struct i915_xd_plan_input *input,
	struct i915_xd_plan *plan)
{
	const struct i915_display_mode *mode;

	/* The sink's rate and lanes: one of the four 8b/10b codes, one, two or four lanes. */
	mode = input->mode;
	plan->rate_khz = drv_i915_xd_dp_code_rate(input->sink.rate_code);
	plan->lanes = input->sink.max_lanes;
	switch (input->sink.rate_code) {
	case I915_XD_DPCD_BW_RBR:
	case I915_XD_DPCD_BW_HBR:
	case I915_XD_DPCD_BW_HBR2:
	case I915_XD_DPCD_BW_HBR3:
		break;
	default:
		return I915_XD_PLAN_SINK;
	}
	if (plan->lanes != 1U && plan->lanes != 2U && plan->lanes != 4U)
		return I915_XD_PLAN_SINK;

	/* An eDP 1.4 panel lists its own rates, which the plan does not take. */
	if (input->sink.edp_revision >= I915_XD_SCREEN_EDP_14)
		return I915_XD_PLAN_SINK;

	/* The bits per pixel: the EDID's depth (24 when it names none), capped by the VBT. */
	plan->bpp = I915_XD_SCREEN_DEFAULT_BPP;
	if (mode->edid_bpc > 0)
		plan->bpp = 3U * (unsigned)mode->edid_bpc;
	if (input->sink.vbt_bpp != 0U && input->sink.vbt_bpp < plan->bpp)
		plan->bpp = input->sink.vbt_bpp;

	/* What the mode needs, and what the link carries. */
	plan->need_kbps = drv_i915_xd_dp_need_kbps((uint32_t)mode->clock_khz, plan->bpp);
	plan->carry_kbps = drv_i915_xd_dp_carry_kbps(plan->rate_khz, plan->lanes);
	if (plan->need_kbps > plan->carry_kbps)
		return I915_XD_PLAN_LINK;

	/* Succeeded: the link carries the mode. */
	return I915_XD_PLAN_OK;
}

/* An HDMI or DVI sink's link: four lanes at the TMDS clock of 8 bits per colour (the pixel clock). */
static unsigned
i915_xd_screen_hdmi_link(
	const struct i915_xd_plan_input *input,
	struct i915_xd_plan *plan)
{
	/* The TMDS clock is the pixel clock. */
	plan->rate_khz = (uint32_t)input->mode->clock_khz;
	plan->lanes = 4U;
	plan->bpp = I915_XD_SCREEN_DEFAULT_BPP;

	/* Succeeded: the link. */
	return I915_XD_PLAN_OK;
}

/*
 * An external DisplayPort sink's link: within a branch's clock limits,
 * the first depth (from the EDID's, at most 8 and a branch's cap, down to
 * 6), rate (lowest first) and lane count that carry the mode.
 */
static unsigned
i915_xd_screen_dp_link(
	const struct i915_xd_plan_input *input,
	struct i915_xd_plan *plan)
{
	const struct i915_xd_sink *sink;
	struct i915_xd_dp_choice choice;
	uint32_t clock;
	unsigned max_bpc;
	int error;

	/* A sink that shares no rate or lane. */
	sink = &input->sink;
	if (sink->nrates == 0U || sink->max_lanes == 0U)
		return I915_XD_PLAN_SINK;

	/* A branch's clock limits. */
	clock = (uint32_t)input->mode->clock_khz;
	if (sink->max_dotclock_khz != 0U && clock > sink->max_dotclock_khz)
		return I915_XD_PLAN_LINK;
	if (sink->min_tmds_khz != 0U && clock < sink->min_tmds_khz)
		return I915_XD_PLAN_LINK;
	if (sink->max_tmds_khz != 0U && clock > sink->max_tmds_khz)
		return I915_XD_PLAN_LINK;

	/* The deepest colour: the EDID's (8 when it names none), at most 8 and a branch's cap. */
	max_bpc = I915_XD_SCREEN_DP_MAX_BPC;
	if (input->mode->edid_bpc > 0 && (unsigned)input->mode->edid_bpc < max_bpc)
		max_bpc = (unsigned)input->mode->edid_bpc;
	if (sink->max_bpc > 0U && sink->max_bpc < max_bpc)
		max_bpc = sink->max_bpc;

	/* The first depth, rate and lanes that carry the mode. */
	error = drv_i915_xd_dp_choose(clock, max_bpc, I915_XD_SCREEN_DP_MIN_BPC, sink->rates, sink->nrates, sink->rate_limit_khz,
	    sink->lane_limit != 0U && sink->lane_limit < sink->max_lanes ? sink->lane_limit : sink->max_lanes, &choice);
	if (error != 0)
		return I915_XD_PLAN_LINK;

	/* The link the choice names. */
	plan->rate_khz = choice.rate_khz;
	plan->lanes = choice.lanes;
	plan->bpp = choice.bpp;
	plan->need_kbps = choice.need_kbps;
	plan->carry_kbps = choice.carry_kbps;

	/* Succeeded: the link carries the mode. */
	return I915_XD_PLAN_OK;
}

/* The combo DPLL's words of the link, when the port's PHY is a combo PHY; a Type-C port's PLL is not planned here. */
static unsigned
i915_xd_screen_combo_pll(
	const struct i915_xd_plan_input *input,
	struct i915_xd_plan *plan)
{
	const struct i915_xd_port_desc *port;
	int error;

	/* A port without a combo PHY runs on a Type-C or Thunderbolt PLL. */
	port = drv_i915_xd_platform_port(input->platform, input->ddi);
	if (port == NULL || port->phy_kind != I915_XD_PHY_COMBO)
		return I915_XD_PLAN_OK;

	/* The DisplayPort table's words, or the HDMI dividers'. */
	if (input->kind == I915_XD_SCREEN_HDMI) {
		error = drv_i915_xd_dpll_hdmi_words(input->platform, input->ref_khz, plan->rate_khz, I915_XD_DPLL_AFC_NONE, &plan->pll);
	} else {
		error = drv_i915_xd_dpll_dp_words(input->platform, input->ref_khz, plan->rate_khz, I915_XD_DPLL_AFC_NONE, &plan->pll);
	}
	if (error != 0)
		return I915_XD_PLAN_PLL;

	/* Succeeded: the words. */
	plan->pll_set = 1;
	return I915_XD_PLAN_OK;
}

/* Divides, rounding up. */
static uint32_t
i915_xd_screen_round_up(
	uint64_t value,
	uint32_t divisor)
{
	/* The quotient, one more when a remainder is left. */
	return (uint32_t)((value + divisor - 1U) / divisor);
}

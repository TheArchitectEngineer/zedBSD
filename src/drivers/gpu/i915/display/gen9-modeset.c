/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register sequences of the Gen9 mode set (see gen9-modeset.h): power
 * well 2 and the DDI IO wells, the DPLLs, the pipe, transcoder and DDI of
 * an output and the way back, the plane and its watermarks, and the
 * hotplug sense.
 *
 * Not done: the pipe's gamma and CSC (the plane bypasses them), audio and
 * infoframes on HDMI, HDMI 2.0 scrambling (the clock stays at most
 * 300 MHz), DP MST, and a CD clock change (a mode above the CD clock is
 * not chosen).
 */

#include "gen9-modeset.h"
#include "gen9-ddc.h"
#include <kern/kcrt.h>

#include "../intel/display-regs-gen9.h"
#include "../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>

/* How long a power well, its fuses and a DPLL's lock may take, in microseconds. */
#define I915_SKL_PW_WAIT_US		1000U
#define I915_SKL_FUSE_WAIT_US		1000U
#define I915_SKL_DPLL_LOCK_US		5000U

/* How long a transcoder may take to start or stop, in microseconds. */
#define I915_SKL_TRANSCONF_WAIT_US	100000U

/* How long a vertical blank may take to come, in milliseconds (a 24 Hz mode with room). */
#define I915_SKL_VBLANK_WAIT_MS		100U

/* The time the DDI buffer takes to leave idle on display 9, in microseconds (> 518 us). */
#define I915_SKL_BUF_ACTIVE_US		600U

/* Power gate 2, whose fuses follow power well 2. */
#define I915_SKL_PG2			2U

/* The DDI function's port field and the payload allocation bit cleared on the way back. */
#define I915_SKL_TRANS_DDI_PORT_FIELD	(7U << 28)

/* The PLANE_CTL bit that leaves the plane's gamma off (pre-GLK). */
#define I915_SKL_PLANE_CTL_PLANE_GAMMA_DISABLE	(1U << 13)

/* The DDI buffer translation entry HDMI uses (entry 9). */
#define I915_SKL_HDMI_TRANS_ENTRY	9U

static uint32_t i915_skl_dpll_ctl(unsigned id);
static void i915_skl_ddi_clock(const struct i915_skl_io *io, unsigned port, unsigned dpll, int on);
static void i915_skl_transcoder_timings(const struct i915_skl_io *io, unsigned transcoder, const struct i915_display_mode *mode);
static void i915_skl_transcoder_m_n(const struct i915_skl_io *io, unsigned transcoder, const struct i915_skl_link *link);
static uint32_t i915_skl_ddi_function(const struct i915_skl_output_config *config);
static void i915_skl_hdmi_buffer_on(const struct i915_skl_io *io, const struct i915_skl_output_config *config);
static void i915_skl_iboost(const struct i915_skl_io *io, unsigned port, uint8_t boost);

/*
 * Turns power well 2 on: the request, its state, then power gate 2's
 * fuses.  A well already up is left alone.  Returns 0, or ETIMEDOUT.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (hsw_power_well_enable).
 */
int
drv_i915_skl_pw2_on(
	const struct i915_skl_io *io)
{
	uint32_t power;
	uint32_t dc_state;
	int error;

	/* The DMC may not take the display into DC5 or DC6 while power well 2 is used (gen9_set_dc_state, the "DC off" well). */
	dc_state = drv_i915_skl_read(io, I915_SKL_DC_STATE_EN);
	if ((dc_state & I915_SKL_DC_STATE_MASK) != 0U) {
		drv_i915_skl_write(io, I915_SKL_DC_STATE_EN, dc_state & ~I915_SKL_DC_STATE_MASK);
		(void)drv_i915_skl_read(io, I915_SKL_DC_STATE_EN);
	}

	/* A well already up needs nothing. */
	power = drv_i915_skl_read(io, I915_SKL_PWR_WELL_CTL2);
	if ((power & I915_SKL_PW_2_STATE) != 0U)
		return 0;

	/* The request, and the state. */
	(void)drv_i915_skl_rmw(io, I915_SKL_PWR_WELL_CTL2, 0U, I915_SKL_PW_2_REQUEST);
	error = drv_i915_skl_wait(io, I915_SKL_PWR_WELL_CTL2, I915_SKL_PW_2_STATE, I915_SKL_PW_2_STATE, I915_SKL_PW_WAIT_US, NULL);
	if (error != 0)
		return error;

	/* Power gate 2's fuses are distributed. */
	error = drv_i915_skl_wait(io, I915_SKL_FUSE_STATUS, I915_SKL_FUSE_PG_DIST(I915_SKL_PG2), I915_SKL_FUSE_PG_DIST(I915_SKL_PG2), I915_SKL_FUSE_WAIT_US, NULL);
	if (error != 0)
		return error;

	/* Succeeded: pipes B and C and the AUX channels B to D have power. */
	return 0;
}

/*
 * Turns a DDI port's IO power well on or off: 0, or ETIMEDOUT.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (hsw_power_well_enable).
 */
int
drv_i915_skl_ddi_io_power(
	const struct i915_skl_io *io,
	unsigned port,
	int on)
{
	uint32_t state;
	int error;

	/* The well's state bit. */
	state = I915_SKL_PW_DDI_STATE(port);

	/* On: the request and the state. */
	if (on) {
		(void)drv_i915_skl_rmw(io, I915_SKL_PWR_WELL_CTL2, 0U, I915_SKL_PW_DDI_REQUEST(port));
		error = drv_i915_skl_wait(io, I915_SKL_PWR_WELL_CTL2, state, state, I915_SKL_PW_WAIT_US, NULL);
		if (error != 0)
			return error;
		return 0;
	}

	/* Off: the request dropped; the state may stay up for another requester. */
	(void)drv_i915_skl_rmw(io, I915_SKL_PWR_WELL_CTL2, I915_SKL_PW_DDI_REQUEST(port), 0U);

	/* Succeeded: the request is as asked. */
	return 0;
}

/*
 * Starts a DPLL with its words (skl_ddi_pll_enable): its six DPLL_CTRL1
 * bits, its configuration words, its enable, and the wait for its lock.
 * Returns 0, or ETIMEDOUT (the DPLL is left enabled for the caller to
 * stop).
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dpll_mgr.c (skl_ddi_pll_enable).
 */
int
drv_i915_skl_dpll_on(
	const struct i915_skl_io *io,
	unsigned id,
	const struct i915_skl_dpll_words *words)
{
	uint32_t shift;
	uint32_t cfgcr1;
	int error;

	/* Only DPLL1 to DPLL3: DPLL0 also clocks the CD clock and stays the firmware's. */
	if (id == 0U || id > 3U)
		return EINVAL;

	/* The DPLL's six bits of DPLL_CTRL1: HDMI mode, SSC, link rate and override. */
	shift = id * I915_SKL_DPLL_CTRL1_BITS;
	(void)drv_i915_skl_rmw(io, I915_SKL_DPLL_CTRL1, 0x3fU << shift, words->ctrl1 << shift);
	(void)drv_i915_skl_read(io, I915_SKL_DPLL_CTRL1);

	/* The configuration words (zero for DP). */
	cfgcr1 = I915_SKL_DPLL1_CFGCR1 + (id - 1U) * I915_SKL_DPLL_CFGCR_STRIDE;
	drv_i915_skl_write(io, cfgcr1, words->cfgcr1);
	drv_i915_skl_write(io, cfgcr1 + 4U, words->cfgcr2);
	(void)drv_i915_skl_read(io, cfgcr1);
	(void)drv_i915_skl_read(io, cfgcr1 + 4U);

	/* The enable, and the lock. */
	(void)drv_i915_skl_rmw(io, i915_skl_dpll_ctl(id), 0U, I915_SKL_DPLL_ENABLE);
	error = drv_i915_skl_wait(io, I915_SKL_DPLL_STATUS, I915_SKL_DPLL_LOCK(id), I915_SKL_DPLL_LOCK(id), I915_SKL_DPLL_LOCK_US, NULL);
	if (error != 0)
		return error;

	/* Succeeded: the DPLL runs. */
	return 0;
}

/*
 * Stops a DPLL (skl_ddi_pll_disable).
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dpll_mgr.c (skl_ddi_pll_disable).
 */
void
drv_i915_skl_dpll_off(
	const struct i915_skl_io *io,
	unsigned id)
{
	/* DPLL0 is never stopped here. */
	if (id == 0U || id > 3U)
		return;

	/* The enable off. */
	(void)drv_i915_skl_rmw(io, i915_skl_dpll_ctl(id), I915_SKL_DPLL_ENABLE, 0U);
	(void)drv_i915_skl_read(io, i915_skl_dpll_ctl(id));
}

/*
 * Lights an external output on its pipe (hsw_crtc_enable for display 9).
 * Returns 0, or an errno with what was started stopped again.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display.c (hsw_crtc_enable,
 * intel_set_pipe_src_size, bdw_set_pipe_misc, hsw_configure_cpu_transcoder,
 * intel_set_m_n, hsw_set_transconf) and intel_ddi.c (hsw_ddi_pre_enable_dp,
 * intel_ddi_pre_enable_hdmi, intel_enable_ddi, intel_enable_ddi_hdmi with
 * Display WA #1143).
 */
int
drv_i915_skl_output_on(
	const struct i915_skl_io *io,
	const struct i915_skl_output_config *config,
	struct i915_skl_dp_port *dp)
{
	uint32_t transcoder_offset;
	uint32_t pipe_offset;
	uint32_t value;
	int error;

	/* The transcoder's and the pipe's registers. */
	transcoder_offset = config->pipe * I915_SKL_TRANSCODER_STRIDE;
	pipe_offset = config->pipe * I915_SKL_PIPE_STRIDE;

	/* The DPLL. */
	error = drv_i915_skl_dpll_on(io, config->dpll, &config->dpll_words);
	if (error != 0) {
		drv_i915_skl_dpll_off(io, config->dpll);
		return error;
	}

	/* DP before the transcoder: the buffer control word, the clock, the IO power, the buffers, the sink's power and the training. */
	if (config->type == I915_SKL_OUTPUT_DP) {
		value = drv_i915_skl_read(io, I915_SKL_DDI_BUF_CTL_A + config->port * I915_SKL_PORT_STRIDE);
		dp->buf_ctl = (value & I915_SKL_DDI_BUF_PORT_REVERSAL) | ((config->link.lanes - 1U) << I915_SKL_DDI_PORT_WIDTH_SHIFT);
		i915_skl_ddi_clock(io, config->port, config->dpll, 1);
		error = drv_i915_skl_ddi_io_power(io, config->port, 1);
		if (error == 0) {
			drv_i915_skl_ddi_buffers(io, dp);
			drv_i915_skl_ddi_levels(io, dp, 0U);
			(void)drv_i915_skl_dp_sink_power(io, dp, I915_DPCD_SET_POWER_D0);
			error = drv_i915_skl_dp_train(io, dp, &config->link);
		}

		/* A training that failed stops what was started. */
		if (error != 0) {
			drv_i915_skl_ddi_buffer_off(io, config->port);
			(void)drv_i915_skl_ddi_io_power(io, config->port, 0);
			i915_skl_ddi_clock(io, config->port, config->dpll, 0);
			drv_i915_skl_dpll_off(io, config->dpll);
			return error;
		}

		/* The transcoder's clock from the port, and its MSA. */
		drv_i915_skl_write(io, I915_SKL_TRANS_CLK_SEL_A + config->pipe * I915_SKL_TRANS_CLK_SEL_STRIDE, (config->port + 1U) << I915_SKL_TRANS_CLK_SEL_PORT_SHIFT);
		drv_i915_skl_write(io, I915_SKL_TRANS_MSA_MISC_A + transcoder_offset, I915_SKL_MSA_MISC_SYNC_CLOCK | I915_SKL_MSA_MISC_8_BPC);
	} else {
		/* HDMI before the transcoder: the clock, the IO power and the transcoder's clock. */
		i915_skl_ddi_clock(io, config->port, config->dpll, 1);
		error = drv_i915_skl_ddi_io_power(io, config->port, 1);
		if (error != 0) {
			i915_skl_ddi_clock(io, config->port, config->dpll, 0);
			drv_i915_skl_dpll_off(io, config->dpll);
			return error;
		}

		/* The transcoder's clock from the port. */
		drv_i915_skl_write(io, I915_SKL_TRANS_CLK_SEL_A + config->pipe * I915_SKL_TRANS_CLK_SEL_STRIDE, (config->port + 1U) << I915_SKL_TRANS_CLK_SEL_PORT_SHIFT);
	}

	/* The pipe's source size and colour depth. */
	drv_i915_skl_write(io, I915_SKL_PIPESRC_A + config->pipe * I915_SKL_TRANSCODER_STRIDE,
	    (((uint32_t)config->mode.hdisplay - 1U) << 16) | ((uint32_t)config->mode.vdisplay - 1U));
	drv_i915_skl_write(io, I915_SKL_PIPE_MISC_A + pipe_offset, I915_SKL_PIPE_MISC_BPC_8);

	/* The transcoder: M and N for DP, the timings, no pixel multiplier, progressive and off. */
	if (config->type == I915_SKL_OUTPUT_DP)
		i915_skl_transcoder_m_n(io, config->pipe, &config->link);
	i915_skl_transcoder_timings(io, config->pipe, &config->mode);
	drv_i915_skl_write(io, I915_SKL_TRANS_MULT_A + transcoder_offset, 0U);
	drv_i915_skl_write(io, I915_SKL_TRANSCONF_A + pipe_offset, 0U);
	(void)drv_i915_skl_read(io, I915_SKL_TRANSCONF_A + pipe_offset);

	/* The DDI function, then the transcoder on, running. */
	drv_i915_skl_write(io, I915_SKL_TRANS_DDI_FUNC_CTL_A + transcoder_offset, i915_skl_ddi_function(config));
	(void)drv_i915_skl_rmw(io, I915_SKL_TRANSCONF_A + pipe_offset, 0U, I915_SKL_TRANSCONF_ENABLE);
	(void)drv_i915_skl_read(io, I915_SKL_TRANSCONF_A + pipe_offset);
	error = drv_i915_skl_wait(io, I915_SKL_TRANSCONF_A + pipe_offset, I915_SKL_TRANSCONF_STATE, I915_SKL_TRANSCONF_STATE, I915_SKL_TRANSCONF_WAIT_US, NULL);
	if (error != 0) {
		drv_i915_skl_output_off(io, config, dp);
		return error;
	}

	/* HDMI after the transcoder: its buffer. */
	if (config->type == I915_SKL_OUTPUT_HDMI)
		i915_skl_hdmi_buffer_on(io, config);

	/* The plane on the first surface. */
	drv_i915_skl_plane_on(io, config->pipe, config->surf, config->stride_units, config->mode.hdisplay, config->mode.vdisplay, config->rgbx);

	/* Succeeded: the output shows the surface. */
	return 0;
}

/*
 * Darkens an external output (intel_ddi_post_disable for display 9): the
 * plane, the transcoder, the DDI function, the sink's power, the clocks,
 * the buffer, the IO power and the DPLL.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_ddi.c (intel_ddi_post_disable).
 */
void
drv_i915_skl_output_off(
	const struct i915_skl_io *io,
	const struct i915_skl_output_config *config,
	struct i915_skl_dp_port *dp)
{
	uint32_t transcoder_offset;
	uint32_t pipe_offset;

	/* The transcoder's and the pipe's registers. */
	transcoder_offset = config->pipe * I915_SKL_TRANSCODER_STRIDE;
	pipe_offset = config->pipe * I915_SKL_PIPE_STRIDE;

	/* The plane off at a vertical blank. */
	drv_i915_skl_plane_off(io, config->pipe);
	(void)drv_i915_skl_wait_vblank(io, config->pipe);

	/* The transcoder off, and stopped. */
	(void)drv_i915_skl_rmw(io, I915_SKL_TRANSCONF_A + pipe_offset, I915_SKL_TRANSCONF_ENABLE, 0U);
	(void)drv_i915_skl_wait(io, I915_SKL_TRANSCONF_A + pipe_offset, I915_SKL_TRANSCONF_STATE, 0U, I915_SKL_TRANSCONF_WAIT_US, NULL);

	/* The DDI function off (its port too). */
	(void)drv_i915_skl_rmw(io, I915_SKL_TRANS_DDI_FUNC_CTL_A + transcoder_offset, I915_SKL_TRANS_DDI_FUNC_ENABLE | I915_SKL_TRANS_DDI_PORT_FIELD, 0U);

	/* A DP sink goes to D3 before its port stops. */
	if (config->type == I915_SKL_OUTPUT_DP && dp != NULL)
		(void)drv_i915_skl_dp_sink_power(io, dp, I915_DPCD_SET_POWER_D3);

	/* The transcoder's clock, the buffer, the IO power, the port's clock and the DPLL. */
	drv_i915_skl_write(io, I915_SKL_TRANS_CLK_SEL_A + config->pipe * I915_SKL_TRANS_CLK_SEL_STRIDE, 0U);
	drv_i915_skl_ddi_buffer_off(io, config->port);
	(void)drv_i915_skl_ddi_io_power(io, config->port, 0);
	i915_skl_ddi_clock(io, config->port, config->dpll, 0);
	drv_i915_skl_dpll_off(io, config->dpll);
}

/*
 * Writes a pipe's first plane's watermarks (eight levels and no
 * transition one), its DDB, the cursor's watermarks (off) and DDB, and the
 * line time.  The plane's surface write arms them.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/skl_watermark.c (skl_write_plane_wm).
 */
void
drv_i915_skl_write_pipe_wm(
	const struct i915_skl_io *io,
	unsigned pipe,
	const struct i915_skl_pipe_wm *wm)
{
	struct i915_skl_wm_level off;
	uint32_t offset;
	unsigned level;

	/* The pipe's registers, and a level that is off. */
	offset = pipe * I915_SKL_PIPE_STRIDE;
	kern_memset(&off, 0, sizeof(off));

	/* The plane's levels, its transition watermark (unused on display 9) and its DDB. */
	for (level = 0U; level < I915_SKL_WM_LEVELS; level++)
		drv_i915_skl_write(io, I915_SKL_PLANE_WM_1_A_0 + offset + level * 4U, drv_i915_skl_wm_word(&wm->level[level]));
	drv_i915_skl_write(io, I915_SKL_PLANE_WM_TRANS_1_A + offset, 0U);
	drv_i915_skl_write(io, I915_SKL_PLANE_BUF_CFG_1_A + offset, drv_i915_skl_ddb_word(wm->plane_start, wm->plane_end));
	drv_i915_skl_write(io, I915_SKL_PLANE_NV12_BUF_CFG_1_A + offset, 0U);

	/* The cursor's levels (it stays off) and its DDB. */
	for (level = 0U; level < I915_SKL_WM_LEVELS; level++)
		drv_i915_skl_write(io, I915_SKL_CUR_WM_A_0 + offset + level * 4U, drv_i915_skl_wm_word(&off));
	drv_i915_skl_write(io, I915_SKL_CUR_WM_TRANS_A + offset, 0U);
	drv_i915_skl_write(io, I915_SKL_CUR_BUF_CFG_A + offset, drv_i915_skl_ddb_word(wm->cursor_start, wm->cursor_end));

	/* The line time. */
	drv_i915_skl_write(io, I915_SKL_WM_LINETIME_A + pipe * I915_SKL_WM_LINETIME_STRIDE, wm->linetime & I915_SKL_LINETIME_MASK);
}

/*
 * Shows a surface on a pipe's first plane (skl_plane_ctl for a linear
 * XRGB8888 surface, plane gamma off, no pipe gamma or CSC): the control,
 * the stride, the position, the size and the offset, then the surface,
 * whose write arms them for the next vertical blank.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/skl_universal_plane.c (skl_plane_ctl).
 */
void
drv_i915_skl_plane_on(
	const struct i915_skl_io *io,
	unsigned pipe,
	uint32_t surf,
	uint32_t stride_units,
	uint32_t width,
	uint32_t height,
	int rgbx)
{
	uint32_t offset;
	uint32_t control;

	/* The pipe's registers. */
	offset = pipe * I915_SKL_PIPE_STRIDE;

	/* The control: on, XRGB8888 in either order, linear, upright. */
	control = I915_SKL_PLANE_CTL_ENABLE | I915_SKL_PLANE_CTL_PLANE_GAMMA_DISABLE | (I915_SKL_PLANE_CTL_FORMAT_XRGB_8888 << I915_SKL_PLANE_CTL_FORMAT_SHIFT);
	if (rgbx)
		control |= I915_SKL_PLANE_CTL_ORDER_RGBX;

	/* The layout, then the control and the surface last. */
	drv_i915_skl_write(io, I915_SKL_PLANE_STRIDE_1_A + offset, stride_units);
	drv_i915_skl_write(io, I915_SKL_PLANE_POS_1_A + offset, 0U);
	drv_i915_skl_write(io, I915_SKL_PLANE_SIZE_1_A + offset, ((height - 1U) << I915_SKL_PLANE_SIZE_HEIGHT_SHIFT) | (width - 1U));
	drv_i915_skl_write(io, I915_SKL_PLANE_OFFSET_1_A + offset, 0U);
	drv_i915_skl_write(io, I915_SKL_PLANE_CTL_1_A + offset, control);
	drv_i915_skl_write(io, I915_SKL_PLANE_SURF_1_A + offset, surf);
	(void)drv_i915_skl_read(io, I915_SKL_PLANE_SURF_1_A + offset);
}

/* Turns a pipe's first plane off (skl_plane_disable_arm): the control and the surface 0. */
void
drv_i915_skl_plane_off(
	const struct i915_skl_io *io,
	unsigned pipe)
{
	uint32_t offset;

	/* The pipe's registers. */
	offset = pipe * I915_SKL_PIPE_STRIDE;

	/* The control, then the surface which arms it. */
	drv_i915_skl_write(io, I915_SKL_PLANE_CTL_1_A + offset, 0U);
	drv_i915_skl_write(io, I915_SKL_PLANE_SURF_1_A + offset, 0U);
	(void)drv_i915_skl_read(io, I915_SKL_PLANE_SURF_1_A + offset);
}

/* Waits for the next vertical blank of a running pipe: its frame counter moves.  0, or ETIMEDOUT. */
int
drv_i915_skl_wait_vblank(
	const struct i915_skl_io *io,
	unsigned pipe)
{
	uint32_t counter;
	uint32_t first;
	uint32_t now;
	unsigned waited;

	/* The pipe's frame counter. */
	counter = I915_SKL_PIPE_FRMCOUNT_A + pipe * I915_SKL_PIPE_STRIDE;

	/* The counter now, then again every millisecond. */
	first = drv_i915_skl_read(io, counter);
	for (waited = 0U; waited < I915_SKL_VBLANK_WAIT_MS; waited++) {
		io->msleep(io->context, 1U);
		now = drv_i915_skl_read(io, counter);
		if (now != first)
			return 0;
	}

	/* Succeeded in learning that the pipe does not run. */
	return ETIMEDOUT;
}

/*
 * Turns on the PCH's hotplug sense of ports B to D
 * (spt_hpd_detection_setup).
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_hotplug_irq.c
 * (spt_hpd_detection_setup).
 */
void
drv_i915_skl_hotplug_enable(
	const struct i915_skl_io *io)
{
	/* The three ports' enables. */
	(void)drv_i915_skl_rmw(io, I915_SKL_PCH_PORT_HOTPLUG, 0U, I915_SKL_PORTB_HOTPLUG_ENABLE | I915_SKL_PORTC_HOTPLUG_ENABLE | I915_SKL_PORTD_HOTPLUG_ENABLE);
}

/* Reports the ports the board has, from SFUSE_STRAP: bit 1 port B, bit 2 C, bit 3 D. */
unsigned
drv_i915_skl_ports_present(
	const struct i915_skl_io *io)
{
	uint32_t strap;
	unsigned ports;

	/* The strap's detected bits. */
	strap = drv_i915_skl_read(io, I915_SKL_SFUSE_STRAP);
	ports = 0U;
	if ((strap & I915_SKL_SFUSE_STRAP_DDIB_DETECTED) != 0U)
		ports |= 1U << I915_SKL_PORT_B;
	if ((strap & I915_SKL_SFUSE_STRAP_DDIC_DETECTED) != 0U)
		ports |= 1U << I915_SKL_PORT_C;
	if ((strap & I915_SKL_SFUSE_STRAP_DDID_DETECTED) != 0U)
		ports |= 1U << I915_SKL_PORT_D;

	/* Succeeded: the ports. */
	return ports;
}

/* Reports the ports a sink is attached to now, from the south display's live state (SDEISR). */
unsigned
drv_i915_skl_ports_connected(
	const struct i915_skl_io *io)
{
	uint32_t live;
	unsigned ports;

	/* The live hotplug bits. */
	live = drv_i915_skl_read(io, I915_SKL_SDEISR);
	ports = 0U;
	if ((live & I915_SKL_SDE_PORTB_HOTPLUG) != 0U)
		ports |= 1U << I915_SKL_PORT_B;
	if ((live & I915_SKL_SDE_PORTC_HOTPLUG) != 0U)
		ports |= 1U << I915_SKL_PORT_C;
	if ((live & I915_SKL_SDE_PORTD_HOTPLUG) != 0U)
		ports |= 1U << I915_SKL_PORT_D;

	/* Succeeded: the attached ports. */
	return ports;
}

/* Reports a DPLL's enable register (LCPLL2_CTL, WRPLL_CTL1, WRPLL_CTL2 for DPLL1 to 3). */
static uint32_t
i915_skl_dpll_ctl(
	unsigned id)
{
	/* DPLL1 is LCPLL2; DPLL2 and DPLL3 are the WRPLLs. */
	if (id == 1U)
		return I915_SKL_DPLL1_CTL;
	if (id == 2U)
		return I915_SKL_DPLL2_CTL;

	/* Succeeded: DPLL3. */
	return I915_SKL_DPLL3_CTL;
}

/* Gives a port the DPLL's clock (skl_ddi_enable_clock), or takes it away (skl_ddi_disable_clock). */
static void
i915_skl_ddi_clock(
	const struct i915_skl_io *io,
	unsigned port,
	unsigned dpll,
	int on)
{
	uint32_t shift;

	/* Off: the port's clock off bit. */
	if (!on) {
		(void)drv_i915_skl_rmw(io, I915_SKL_DPLL_CTRL2, 0U, I915_SKL_DPLL_CTRL2_CLK_OFF(port));
		return;
	}

	/* On: the off bit and the old select cleared, the DPLL selected with the override. */
	shift = port * I915_SKL_DPLL_CTRL2_BITS;
	(void)drv_i915_skl_rmw(io, I915_SKL_DPLL_CTRL2,
	    I915_SKL_DPLL_CTRL2_CLK_OFF(port) | (I915_SKL_DPLL_CTRL2_SELECT_MASK << (shift + I915_SKL_DPLL_CTRL2_SELECT_SHIFT)),
	    (dpll << (shift + I915_SKL_DPLL_CTRL2_SELECT_SHIFT)) | (1U << shift));
}

/* Writes a transcoder's timings (intel_set_transcoder_timings, progressive). */
static void
i915_skl_transcoder_timings(
	const struct i915_skl_io *io,
	unsigned transcoder,
	const struct i915_display_mode *mode)
{
	uint32_t offset;

	/* The transcoder's registers. */
	offset = transcoder * I915_SKL_TRANSCODER_STRIDE;

	/* No vertical sync shift for a progressive mode. */
	drv_i915_skl_write(io, I915_SKL_TRANS_VSYNCSHIFT_A + offset, 0U);

	/* Horizontal: total and active, blank (from the active end to the total), sync. */
	drv_i915_skl_write(io, I915_SKL_TRANS_HTOTAL_A + offset, (((uint32_t)mode->htotal - 1U) << 16) | ((uint32_t)mode->hdisplay - 1U));
	drv_i915_skl_write(io, I915_SKL_TRANS_HBLANK_A + offset, (((uint32_t)mode->htotal - 1U) << 16) | ((uint32_t)mode->hdisplay - 1U));
	drv_i915_skl_write(io, I915_SKL_TRANS_HSYNC_A + offset, (((uint32_t)mode->hsync_end - 1U) << 16) | ((uint32_t)mode->hsync_start - 1U));

	/* Vertical: total and active, blank, sync. */
	drv_i915_skl_write(io, I915_SKL_TRANS_VTOTAL_A + offset, (((uint32_t)mode->vtotal - 1U) << 16) | ((uint32_t)mode->vdisplay - 1U));
	drv_i915_skl_write(io, I915_SKL_TRANS_VBLANK_A + offset, (((uint32_t)mode->vtotal - 1U) << 16) | ((uint32_t)mode->vdisplay - 1U));
	drv_i915_skl_write(io, I915_SKL_TRANS_VSYNC_A + offset, (((uint32_t)mode->vsync_end - 1U) << 16) | ((uint32_t)mode->vsync_start - 1U));
}

/* Writes a transcoder's data and link M and N, link N last since it arms them (intel_set_m_n). */
static void
i915_skl_transcoder_m_n(
	const struct i915_skl_io *io,
	unsigned transcoder,
	const struct i915_skl_link *link)
{
	uint32_t offset;

	/* The transcoder's registers. */
	offset = transcoder * I915_SKL_TRANSCODER_STRIDE;

	/* Data M with the transfer unit, data N, link M, then link N. */
	drv_i915_skl_write(io, I915_SKL_DATA_M1_A + offset, ((link->tu - 1U) << I915_SKL_TU_SIZE_SHIFT) | link->data_m);
	drv_i915_skl_write(io, I915_SKL_DATA_N1_A + offset, link->data_n);
	drv_i915_skl_write(io, I915_SKL_LINK_M1_A + offset, link->link_m);
	drv_i915_skl_write(io, I915_SKL_LINK_N1_A + offset, link->link_n);
}

/* Builds the DDI function word (intel_ddi_transcoder_func_reg_val_get for display 9, transcoders A to C). */
static uint32_t
i915_skl_ddi_function(
	const struct i915_skl_output_config *config)
{
	uint32_t value;

	/* On, the port, 8 bits a channel, the sync polarities. */
	value = I915_SKL_TRANS_DDI_FUNC_ENABLE | (config->port << I915_SKL_TRANS_DDI_PORT_SHIFT) | I915_SKL_TRANS_DDI_BPC_8;
	if (config->mode.vsync_positive)
		value |= I915_SKL_TRANS_DDI_PVSYNC;
	if (config->mode.hsync_positive)
		value |= I915_SKL_TRANS_DDI_PHSYNC;

	/* The mode: HDMI, or DP single-stream with the lanes. */
	if (config->type == I915_SKL_OUTPUT_HDMI) {
		value |= I915_SKL_TRANS_DDI_MODE_HDMI << I915_SKL_TRANS_DDI_MODE_SHIFT;
	} else {
		value |= I915_SKL_TRANS_DDI_MODE_DP_SST << I915_SKL_TRANS_DDI_MODE_SHIFT;
		value |= (config->link.lanes - 1U) << I915_SKL_DDI_PORT_WIDTH_SHIFT;
	}

	/* Succeeded: the word. */
	return value;
}

/*
 * Starts an HDMI port's buffer (intel_enable_ddi_hdmi for display 9): the
 * HDMI level's translation into entry 9, its boost, the training override
 * toggle of Display WA #1143, and the buffer on.
 */
static void
i915_skl_hdmi_buffer_on(
	const struct i915_skl_io *io,
	const struct i915_skl_output_config *config)
{
	const struct i915_skl_buf_trans *table;
	uint32_t trans;
	uint32_t chicken;
	uint32_t buf_ctl;
	uint32_t value;
	unsigned count;
	unsigned level;

	/* The HDMI table's level, written as entry 9 (hsw_prepare_hdmi_ddi_buffers). */
	table = drv_i915_skl_buf_trans(config->sku, config->platform, I915_SKL_OUTPUT_HDMI, config->port, &count);
	level = drv_i915_skl_hdmi_level(config->sku, config->platform);
	if (level >= count)
		level = count - 1U;
	trans = I915_SKL_DDI_BUF_TRANS_A + config->port * I915_SKL_DDI_BUF_TRANS_STRIDE + I915_SKL_HDMI_TRANS_ENTRY * 8U;
	drv_i915_skl_write(io, trans, table[level].trans1);
	drv_i915_skl_write(io, trans + 4U, table[level].trans2);

	/* The entry's boost. */
	i915_skl_iboost(io, config->port, table[level].iboost);

	/* Display WA #1143: the training override of the port's transcoder chicken register, on for a microsecond. */
	chicken = I915_SKL_CHICKEN_TRANS_A + (config->port - 1U) * 4U;
	value = drv_i915_skl_rmw(io, chicken, 0U, I915_SKL_DDI_TRAINING_OVERRIDE_ENABLE | I915_SKL_DDI_TRAINING_OVERRIDE_VALUE);
	(void)drv_i915_skl_read(io, chicken);
	io->udelay(io->context, 1U);
	drv_i915_skl_write(io, chicken, value & ~(I915_SKL_DDI_TRAINING_OVERRIDE_ENABLE | I915_SKL_DDI_TRAINING_OVERRIDE_VALUE));

	/* The buffer on, keeping the lane reversal, and the time it takes to leave idle. */
	buf_ctl = I915_SKL_DDI_BUF_CTL_A + config->port * I915_SKL_PORT_STRIDE;
	value = drv_i915_skl_read(io, buf_ctl);
	drv_i915_skl_write(io, buf_ctl, (value & I915_SKL_DDI_BUF_PORT_REVERSAL) | I915_SKL_DDI_BUF_ENABLE);
	io->udelay(io->context, I915_SKL_BUF_ACTIVE_US);
}

/* Sets a port's current boost: 1, 3 or 7 into its balance leg, 0 turns the leg off. */
static void
i915_skl_iboost(
	const struct i915_skl_io *io,
	unsigned port,
	uint8_t boost)
{
	/* No boost: the leg off. */
	if (boost == 0U) {
		(void)drv_i915_skl_rmw(io, I915_SKL_DISPIO_CR_TX_BMU_CR0, I915_SKL_BALANCE_LEG_MASK(port), I915_SKL_BALANCE_LEG_DISABLE(port));
		return;
	}

	/* A valid boost into the leg. */
	if (boost == 1U || boost == 3U || boost == 7U) {
		(void)drv_i915_skl_rmw(io, I915_SKL_DISPIO_CR_TX_BMU_CR0,
		    I915_SKL_BALANCE_LEG_MASK(port) | I915_SKL_BALANCE_LEG_DISABLE(port),
		    (uint32_t)boost << I915_SKL_BALANCE_LEG_SHIFT(port));
	}
}

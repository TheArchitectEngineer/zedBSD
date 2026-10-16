/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_dp_link_training.c),
 * which carries the following notice.
 *
 * Copyright © 2008-2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_dp.c),
 * which carries the following notice.
 *
 * Copyright © 2008 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Keith Packard <keithp@keithp.com>
 */

/*
 * The DP link of the one-screen modeset path (see dp.h).
 *
 * The link training, the link status and the link arithmetic are the xd
 * DisplayPort code's (xd-dp.c): the functions here hand the modeset
 * environment's port and crtc state to it.  The sink's DPCD is reached
 * through the environment's DPCD accessors on the current device of the
 * world the modeset object belongs to, the source's levels, pattern,
 * preparation and idle pattern through the port's hooks.  What stays
 * here follows the Linux 6.8.12 text of intel_dp.c (the sink power state,
 * the source OUI, the eDP backlight enable, the DP infoframe enables) and
 * of intel_dp_link_training.c (the request for a fallback after a failed
 * training).
 *
 * The environment's note, error and warning sinks and I915_LCD_DISPLAY_VER()
 * do not evaluate the Linux device argument.  Where the Linux text computed
 * a device only for them, no device is computed and NULL stands in its
 * place.
 *
 * lt_dbg() and lt_err() hand only their format string to the debug and
 * error hooks; their arguments are checked against the format at compile
 * time and never evaluated.
 */

#include "internal.h"
#include "modeset-internal.h"
#include "dp.h"
#include "panel-backlight.h"
#include "xd-dp.h"
#include <kern/kcrt.h>

#include <uapi/errno.h>

/* The highest rate of the 8b/10b coding: a port clock above it is a 128b/132b link. */
#define I915_DP_HIGHEST_8B10B_KHZ 810000

/*
 * The port and crtc state of one training, which the hooks of the xd
 * link reach through their context.
 */
struct i915_dp_train_ctx {
	struct intel_dp *intel_dp;
	const struct intel_crtc_state *crtc_state;
	struct drm_i915_private *cur_i915;
};

static struct drm_i915_private *i915_dp_cur_i915(const struct drm_dp_aux *aux);
static int i915_dp_sink_read(void *context, uint32_t address, uint8_t *buffer, unsigned size);
static int i915_dp_sink_write(void *context, uint32_t address, const uint8_t *buffer, unsigned size);
static int i915_dp_sink_probe(void *context, uint32_t address);
static int i915_dp_sink_read_caps(void *context, uint8_t *caps);
static void i915_dp_bind_sink(struct drm_dp_aux *aux, struct i915_xd_dp_sink *sink);
static void i915_dp_source_levels(void *context, const uint8_t set[4]);
static void i915_dp_source_pattern(void *context, uint8_t pattern);
static void i915_dp_source_prepare(void *context);
static void i915_dp_source_idle(void *context);
static void i915_dp_source_sleep(void *context, unsigned min_us, unsigned max_us);
static void i915_dp_source_note(void *context, unsigned kind, const char *text);
static void i915_dp_bind_link(struct i915_dp_train_ctx *ctx, struct i915_xd_dp_link *link);
static void i915_dp_schedule_fallback_link_training(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
static bool i915_downstream_hpd_needs_d0(struct intel_dp *intel_dp);
static void i915_edp_init_source_oui(struct intel_dp *intel_dp, bool careful);

/*
 * Tells whether the channel equalization of every lane is done: the lanes
 * aligned, and each lane in use with the clock, equalized and locked.
 */
bool
drv_i915_drm_dp_channel_eq_ok(
	const u8 link_status[DP_LINK_STATUS_SIZE],
	int lane_count)
{
	int equalized;

	/* The xd link judges the status. */
	equalized = drv_i915_xd_dp_equalized(link_status, (unsigned)lane_count);
	if (!equalized)
		return false;

	/* Succeeded: every lane in use is equalized and the lanes are aligned. */
	return true;
}

/*
 * Tells whether the clock recovery of every lane in use is done.
 */
bool
drv_i915_drm_dp_clock_recovery_ok(
	const u8 link_status[DP_LINK_STATUS_SIZE],
	int lane_count)
{
	int recovered;

	/* The xd link judges the status. */
	recovered = drv_i915_xd_dp_clock_recovered(link_status, (unsigned)lane_count);
	if (!recovered)
		return false;

	/* Succeeded: every lane in use has recovered the clock. */
	return true;
}

/*
 * Reads the link status of the DPRX or of one LTTPR PHY, in the DPRX
 * layout.  The sink is the one the current device of the modeset object
 * that owns the AUX channel reaches.
 *
 * Returns 0 when the status was read, or the positive magnitude of the
 * negative Linux errno the DPCD read reported.
 */
int
drv_i915_drm_dp_dpcd_read_phy_link_status(
	struct drm_dp_aux *aux,
	enum dp_phy dp_phy,
	u8 link_status[DP_LINK_STATUS_SIZE])
{
	struct i915_xd_dp_sink sink;
	int error;

	/* The xd link reads the status through the channel's sink. */
	i915_dp_bind_sink(aux, &sink);
	error = drv_i915_xd_dp_read_status(&sink, (unsigned)dp_phy, link_status);
	if (error != 0)
		return error;

	/* Succeeded: the status is in place. */
	return 0;
}

/*
 * Stops the link training of a DP port: the sink and then the port leave
 * the training pattern.  Must be called after drv_i915_dp_start_link_train().
 */
void
drv_i915_dp_stop_link_train(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state)
{
	struct i915_dp_train_ctx ctx;
	struct i915_xd_dp_link link;

	/*
	 * link_trained tells the LTTPR detection that the link is active, so
	 * the next detection keeps the LTTPR mode instead of changing it.
	 */
	intel_dp->link_trained = true;

	/* The xd link ends the training. */
	ctx.intel_dp = intel_dp;
	ctx.crtc_state = crtc_state;
	ctx.cur_i915 = i915_dp_cur_i915(&intel_dp->aux);
	i915_dp_bind_link(&ctx, &link);
	drv_i915_xd_dp_train_end(&link);
}

/*
 * Starts the link training of a DP port: the xd link finds the LTTPRs,
 * reads the DPRX capabilities again, prepares the link and trains every
 * LTTPR and the DPRX; a failed training asks for a fallback.
 * drv_i915_dp_stop_link_train() must be called afterwards.
 */
void
drv_i915_dp_start_link_train(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;
	struct i915_dp_train_ctx ctx;
	struct i915_xd_dp_link link;
	int error;

	/* Finds the device the port belongs to. */
	i915 = i915_lcd_dp_to_i915(intel_dp);

	/* Trains the link; the port keeps the levels it ended with. */
	ctx.intel_dp = intel_dp;
	ctx.crtc_state = crtc_state;
	ctx.cur_i915 = i915_dp_cur_i915(&intel_dp->aux);
	i915_dp_bind_link(&ctx, &link);
	error = drv_i915_xd_dp_train(&link);
	kern_memcpy(intel_dp->train_set, link.set, sizeof(intel_dp->train_set));

	/*
	 * A failed training asks for a fallback, unless long HPDs are ignored:
	 * in fixed environments like CI, a link failure that follows ignored
	 * long HPDs is ignored as well.
	 */
	if (error != 0) {
		if (i915->display.hotplug.ignore_long_hpd) {
			lt_dbg(intel_dp, DP_PHY_DPRX, "Ignore the link failure\n");
			return;
		}

		i915_dp_schedule_fallback_link_training(intel_dp, crtc_state);
	}
}

/*
 * Tells whether the link rate of a crtc state is UHBR, and thus 128b/132b.
 */
bool
drv_i915_dp_is_uhbr(
	const struct intel_crtc_state *crtc_state)
{
	/* The 128b/132b rates start above the highest 8b/10b rate. */
	if (crtc_state->port_clock <= I915_DP_HIGHEST_8B10B_KHZ)
		return false;

	/* Succeeded: the link uses 128b/132b channel coding. */
	return true;
}

/*
 * Tells whether a crtc state needs the DP VSC SDP.
 *
 * The Linux intel_dp_needs_vsc_sdp(): as per DP 1.4a spec section 2.2.4.3
 * [MSA Field for Indication of Color Encoding Format and Content Color
 * Gamut], YCbCr 4:2:0 and the BT.2020 / YCC colorimetries are sent through
 * the VSC SDP.
 */
bool
drv_i915_dp_needs_vsc_sdp(
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	/* YCbCr 4:2:0 output is signalled in the VSC SDP. */
	if (crtc_state->output_format == INTEL_OUTPUT_FORMAT_YCBCR420)
		return true;

	/* So are the colorimetries the MSA cannot indicate. */
	switch (conn_state->colorspace) {
	case MODE_COLORIMETRY_SYCC_601:
	case MODE_COLORIMETRY_OPYCC_601:
	case MODE_COLORIMETRY_BT2020_YCC:
	case MODE_COLORIMETRY_BT2020_RGB:
	case MODE_COLORIMETRY_BT2020_CYCC:
		return true;
	default:
		break;
	}

	/* Succeeded: the MSA carries the colour format. */
	return false;
}

/*
 * Tells whether a DP port drives an eDP panel.
 *
 * The Linux intel_dp_is_edp(): not safe to use before the encoder type is
 * set.
 */
bool
drv_i915_dp_is_edp(
	struct intel_dp *intel_dp)
{
	struct intel_digital_port *dig_port;

	/* Finds the port whose encoder type decides. */
	dig_port = i915_lcd_dp_to_dig_port(intel_dp);
	if (dig_port->base.type != INTEL_OUTPUT_EDP)
		return false;

	/* Succeeded: the port drives an eDP panel. */
	return true;
}

/*
 * Records the link parameters of a DP port.
 *
 * The Linux intel_dp_set_link_params(): the training set starts from zero
 * and the link counts as not trained.
 */
void
drv_i915_dp_set_link_params(
	struct intel_dp *intel_dp,
	int link_rate,
	int lane_count)
{
	/* Starts the training set from the lowest levels. */
	kern_memset(intel_dp->train_set, 0, sizeof(intel_dp->train_set));

	/*
	 * link_trained tells the LTTPR detection that the link is not active
	 * yet, so it may switch the LTTPR mode.
	 */
	intel_dp->link_trained = false;

	/* Records the rate and lane count the training will use. */
	intel_dp->link_rate = link_rate;
	intel_dp->lane_count = lane_count;
}

/*
 * Sets the power state of a DP sink, if the sink supports it.
 *
 * The Linux intel_dp_set_power().  Waking a sink writes the source OUI of
 * an eDP panel first and retries for 1 ms to give the sink time to wake
 * up.  The sink and the sleeps are the current device's.
 */
void
drv_i915_dp_set_power(
	struct intel_dp *intel_dp,
	u8 mode)
{
	struct intel_encoder *encoder;
	struct drm_i915_private *cur_i915;
	struct intel_lspcon *lspcon;
	i915_lcd_ssize_t ret;
	bool needs_d0;
	bool edp;
	int i;

	/* Finds the port's encoder, which the failure note names. */
	encoder = &i915_lcd_dp_to_dig_port(intel_dp)->base;

	/* Should have a valid DPCD by this point */
	if (intel_dp->dpcd[DP_DPCD_REV] < 0x11)
		return;

	/* Finds the device whose sink the port reaches. */
	cur_i915 = i915_dp_cur_i915(&intel_dp->aux);

	/* Puts the sink to sleep, or wakes it up. */
	if (mode != DP_SET_POWER_D0) {
		/* A branch device that signals downstream HPD only in D0 stays awake. */
		needs_d0 = i915_downstream_hpd_needs_d0(intel_dp);
		if (needs_d0)
			return;

		/* Writes the new power state. */
		ret = I915_LCD_DRM_DP_DPCD_WRITEB(cur_i915, &intel_dp->aux, DP_SET_POWER, mode);
	} else {
		/* Resumes an LSPCON (none is behind these ports). */
		lspcon = dp_to_lspcon(intel_dp);
		lspcon_resume(i915_lcd_dp_to_dig_port(intel_dp));

		/* Write the source OUI as early as possible */
		edp = drv_i915_dp_is_edp(intel_dp);
		if (edp)
			i915_edp_init_source_oui(intel_dp, false);

		/*
		 * When turning on, we need to retry for 1ms to give the sink
		 * time to wake up.
		 */
		for (i = 0; i < 3; i++) {
			ret = I915_LCD_DRM_DP_DPCD_WRITEB(cur_i915, &intel_dp->aux, DP_SET_POWER, mode);
			if (ret == 1)
				break;

			i915_lcd_msleep(cur_i915, 1);
		}

		/* An awake LSPCON is waited for until it is in PCON mode. */
		if (ret == 1 && lspcon->active)
			lspcon_wait_pcon_mode(lspcon);
	}

	/* Notes a sink that did not take the power state. */
	if (ret != 1) {
		I915_LCD_DRM_DBG_KMS(NULL,
				     "[ENCODER:%d:%s] Set power to %s failed\n",
				     encoder->base.base.id,
				     encoder->base.name,
				     mode == DP_SET_POWER_D0 ? "D0" : "D3");
	}
}

/*
 * Enables the backlight PWM and the backlight PP control of an eDP panel.
 *
 * The Linux intel_edp_backlight_on().  The PP control is the current
 * device's panel power sequencer.
 */
void
drv_i915_edp_backlight_on(
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct intel_dp *intel_dp;
	struct drm_i915_private *cur_i915;
	bool edp;

	/* Finds the DP port of the connector's encoder. */
	intel_dp = enc_to_intel_dp(to_intel_encoder(conn_state->best_encoder));

	/* Only an eDP panel has a backlight here. */
	edp = drv_i915_dp_is_edp(intel_dp);
	if (!edp)
		return;

	/* Notes the enable. */
	I915_LCD_DRM_DBG_KMS(NULL, "\n");

	/* Enables the PWM, then the backlight enable bit of the sequencer. */
	drv_i915_backlight_enable(crtc_state, conn_state);
	cur_i915 = i915_dp_cur_i915(&intel_dp->aux);
	i915_lcd_intel_pps_backlight_on(cur_i915, intel_dp);
}

/*
 * Disables the backlight PP control and the backlight PWM of an eDP panel.
 *
 * The Linux intel_edp_backlight_off().  The PP control is the current
 * device's panel power sequencer.
 */
void
drv_i915_edp_backlight_off(
	const struct drm_connector_state *old_conn_state)
{
	struct intel_dp *intel_dp;
	struct drm_i915_private *cur_i915;
	bool edp;

	/* Finds the DP port of the connector's encoder. */
	intel_dp = enc_to_intel_dp(to_intel_encoder(old_conn_state->best_encoder));

	/* Only an eDP panel has a backlight here. */
	edp = drv_i915_dp_is_edp(intel_dp);
	if (!edp)
		return;

	/* Notes the disable. */
	I915_LCD_DRM_DBG_KMS(NULL, "\n");

	/* Clears the backlight enable bit of the sequencer, then disables the PWM. */
	cur_i915 = i915_dp_cur_i915(&intel_dp->aux);
	i915_lcd_intel_pps_backlight_off(cur_i915, intel_dp);
	drv_i915_backlight_disable(old_conn_state);
}

/*
 * Enables or disables the DP infoframes (SDPs) of a transcoder.
 *
 * The Linux intel_dp_set_infoframes(): the DIP enables of the transcoder
 * are cleared, the VSC DIP is kept only for PSR, and on enable the VSC and
 * the gamut metadata SDPs are written.
 */
void
drv_i915_dp_set_infoframes(
	struct intel_encoder *encoder,
	bool enable,
	const struct intel_crtc_state *crtc_state,
	const struct drm_connector_state *conn_state)
{
	struct drm_i915_private *dev_priv;
	i915_reg_t reg;
	u32 dip_enable;
	u32 val;

	UNUSED_PARAMETER(conn_state);

	/* Finds the device and the transcoder's DIP control register. */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	reg = HSW_TVIDEO_DIP_CTL(crtc_state->cpu_transcoder);

	/* The DIP enables this function owns. */
	dip_enable = VIDEO_DIP_ENABLE_AVI_HSW | VIDEO_DIP_ENABLE_GCP_HSW |
		     VIDEO_DIP_ENABLE_VS_HSW | VIDEO_DIP_ENABLE_GMP_HSW |
		     VIDEO_DIP_ENABLE_SPD_HSW | VIDEO_DIP_ENABLE_DRM_GLK;

	/* Reads the control word without those enables. */
	val = i915_lcd_intel_de_read(dev_priv, reg);
	val &= ~dip_enable;

	/* TODO: Sanitize DSC enabling wrt. intel_dsc_dp_pps_write(). */
	if (!enable && HAS_DSC(dev_priv))
		val &= ~VDIP_ENABLE_PPS;

	/*
	 * This routine disables VSC DIP if the function is called
	 * to disable SDP or if it does not have PSR
	 */
	if (!enable) {
		val &= ~VIDEO_DIP_ENABLE_VSC_HSW;
	} else if (!crtc_state->has_psr) {
		val &= ~VIDEO_DIP_ENABLE_VSC_HSW;
	}

	/* Writes the control word and flushes it. */
	i915_lcd_intel_de_write(dev_priv, reg, val);
	i915_lcd_intel_de_posting_read(dev_priv, reg);

	/* A disable is complete. */
	if (!enable)
		return;

	/* When PSR is enabled, VSC SDP is handled by PSR routine */
	if (!crtc_state->has_psr)
		intel_write_dp_sdp(encoder, crtc_state, DP_SDP_VSC);

	/* Writes the gamut metadata SDP. */
	intel_write_dp_sdp(encoder, crtc_state, HDMI_PACKET_TYPE_GAMUT_METADATA);
}

/*
 * The device the Linux text reached through the file-scope current-device
 * pointer: the current device of the world of the modeset object that owns
 * the AUX channel.
 */
static struct drm_i915_private *
i915_dp_cur_i915(
	const struct drm_dp_aux *aux)
{
	struct i915_lcd_modeset *ms;

	/* Finds the modeset object the port's AUX channel is part of. */
	ms = container_of(aux, struct i915_lcd_modeset, dig_port.dp.aux);

	/* Succeeded: reports the device its world's entry point recorded. */
	return ms->world->i915_lcd_cur_i915;
}

/* Reads DPCD bytes of the training's sink: the bytes read, or a negative errno. */
static int
i915_dp_sink_read(
	void *context,
	uint32_t address,
	uint8_t *buffer,
	unsigned size)
{
	struct drm_dp_aux *aux;
	long transferred;

	/* The DPCD accessor of the channel's current device. */
	aux = context;
	transferred = I915_LCD_DRM_DP_DPCD_READ(i915_dp_cur_i915(aux), aux, address, buffer, size);

	/* Succeeded: the accessor's answer. */
	return (int)transferred;
}

/* Writes DPCD bytes of the training's sink: the bytes written, or a negative errno. */
static int
i915_dp_sink_write(
	void *context,
	uint32_t address,
	const uint8_t *buffer,
	unsigned size)
{
	struct drm_dp_aux *aux;
	long transferred;

	/* The DPCD accessor of the channel's current device. */
	aux = context;
	transferred = I915_LCD_DRM_DP_DPCD_WRITE(i915_dp_cur_i915(aux), aux, address, buffer, size);

	/* Succeeded: the accessor's answer. */
	return (int)transferred;
}

/* Wakes the training's sink to answer at an address: 0, or the probe's error. */
static int
i915_dp_sink_probe(
	void *context,
	uint32_t address)
{
	struct drm_dp_aux *aux;
	int error;

	/* The probe of the channel's current device. */
	aux = context;
	error = I915_LCD_DRM_DP_DPCD_PROBE(i915_dp_cur_i915(aux), aux, address);
	if (error != 0)
		return error;

	/* Succeeded: the sink answers. */
	return 0;
}

/* Reads the training's sink's receiver capabilities: 0, or the read's error. */
static int
i915_dp_sink_read_caps(
	void *context,
	uint8_t *caps)
{
	struct drm_dp_aux *aux;
	int error;

	/* The capability read of the channel's current device. */
	aux = context;
	error = I915_LCD_DRM_DP_READ_DPCD_CAPS(i915_dp_cur_i915(aux), aux, caps);
	if (error != 0)
		return error;

	/* Succeeded: the capabilities are in place. */
	return 0;
}

/* Binds the DPCD hooks of an xd link's sink to an AUX channel. */
static void
i915_dp_bind_sink(
	struct drm_dp_aux *aux,
	struct i915_xd_dp_sink *sink)
{
	/* The channel is the hooks' context. */
	sink->read = i915_dp_sink_read;
	sink->write = i915_dp_sink_write;
	sink->probe = i915_dp_sink_probe;
	sink->read_caps = i915_dp_sink_read_caps;
	sink->context = aux;
}

/* Programs the port's signal levels from the training's level bytes. */
static void
i915_dp_source_levels(
	void *context,
	const uint8_t set[4])
{
	void (*set_signal_levels)(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state);
	struct i915_dp_train_ctx *ctx;
	struct intel_encoder *encoder;

	/* The port's hook reads the levels from the training set. */
	ctx = context;
	kern_memcpy(ctx->intel_dp->train_set, set, sizeof(ctx->intel_dp->train_set));
	encoder = &i915_lcd_dp_to_dig_port(ctx->intel_dp)->base;
	set_signal_levels = encoder->set_signal_levels;
	set_signal_levels(encoder, ctx->crtc_state);
}

/* Programs a training pattern on the port. */
static void
i915_dp_source_pattern(
	void *context,
	uint8_t pattern)
{
	void (*set_link_train)(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state, u8 dp_train_pat);
	struct i915_dp_train_ctx *ctx;

	/* The port's hook. */
	ctx = context;
	set_link_train = ctx->intel_dp->set_link_train;
	set_link_train(ctx->intel_dp, ctx->crtc_state, pattern);
}

/* Lets the port prepare the retraining (the DDI enables the port here). */
static void
i915_dp_source_prepare(
	void *context)
{
	void (*prepare_link_retrain)(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
	struct i915_dp_train_ctx *ctx;

	/* The port's hook, when it has one. */
	ctx = context;
	prepare_link_retrain = ctx->intel_dp->prepare_link_retrain;
	if (prepare_link_retrain != NULL)
		prepare_link_retrain(ctx->intel_dp, ctx->crtc_state);
}

/* Lets the port send the idle pattern after the training. */
static void
i915_dp_source_idle(
	void *context)
{
	void (*set_idle_link_train)(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
	struct i915_dp_train_ctx *ctx;

	/* The port's hook, when it has one. */
	ctx = context;
	set_idle_link_train = ctx->intel_dp->set_idle_link_train;
	if (set_idle_link_train != NULL)
		set_idle_link_train(ctx->intel_dp, ctx->crtc_state);
}

/* Sleeps between two steps of the training on the current device. */
static void
i915_dp_source_sleep(
	void *context,
	unsigned min_us,
	unsigned max_us)
{
	struct i915_dp_train_ctx *ctx;

	/* The current device's sleep. */
	ctx = context;
	i915_lcd_usleep_range(ctx->cur_i915, min_us, max_us);
}

/* Hands a note of the training to the environment's debug or error sink. */
static void
i915_dp_source_note(
	void *context,
	unsigned kind,
	const char *text)
{
	UNUSED_PARAMETER(context);

	/* An error is counted; a debug note is only printed. */
	if (kind == I915_XD_DP_NOTE_ERROR) {
		drv_i915_lcd_error(text);
	} else {
		drv_i915_lcd_debug(text);
	}
}

/*
 * Describes a port's training to the xd link: the crtc state's rate and
 * lanes, the source's limits from the port's hooks, the sink's
 * capabilities the port keeps, and the hooks of both sides.
 */
static void
i915_dp_bind_link(
	struct i915_dp_train_ctx *ctx,
	struct i915_xd_dp_link *link)
{
	u8 (*voltage_max)(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);
	u8 (*preemph_max)(struct intel_dp *intel_dp);
	struct intel_dp *intel_dp;
	const struct intel_crtc_state *crtc_state;
	bool edp;
	int display_ver;
	int index;

	/* The link the crtc state asks for. */
	intel_dp = ctx->intel_dp;
	crtc_state = ctx->crtc_state;
	kern_memset(link, 0, sizeof(*link));
	link->rate_khz = (uint32_t)crtc_state->port_clock;
	link->lanes = (unsigned)crtc_state->lane_count;
	link->enhanced = crtc_state->enhanced_framing ? 1 : 0;
	link->msa_ignore = crtc_state->vrr.flipline ? 1 : 0;

	/* An eDP 1.4 sink set by its rate list takes the rate's index in the list (0 when it is not listed). */
	link->rate_index = -1;
	if (intel_dp->use_rate_select) {
		link->rate_index = 0;
		for (index = 0; index < intel_dp->num_sink_rates; index++) {
			if (intel_dp->sink_rates[index] == crtc_state->port_clock)
				link->rate_index = index;
		}
	}

	/* The source's highest levels, from the port's hooks. */
	voltage_max = intel_dp->voltage_max;
	preemph_max = intel_dp->preemph_max;
	if (voltage_max != NULL)
		link->swing_max = (uint8_t)(voltage_max(intel_dp, crtc_state) & DP_TRAIN_VOLTAGE_SWING_MASK);
	if (preemph_max != NULL)
		link->preemph_max = (uint8_t)((preemph_max(intel_dp) & DP_TRAIN_PRE_EMPHASIS_MASK) >> DP_TRAIN_PRE_EMPHASIS_SHIFT);

	/* Display 11 and later set the levels per lane; display 10 and later know patterns 3 and 4. */
	display_ver = I915_LCD_DISPLAY_VER(NULL);
	link->per_lane = display_ver >= 11 ? 1 : 0;
	link->tps3 = display_ver >= 9 ? 1 : 0;
	link->tps4 = display_ver >= 10 ? 1 : 0;

	/* LTTPRs are looked for from display 10, not behind an eDP panel; an active link keeps their mode. */
	edp = drv_i915_dp_is_edp(intel_dp);
	link->find_repeaters = (!edp && display_ver >= 10) ? 1 : 0;
	link->active = intel_dp->link_trained ? 1 : 0;

	/* The capabilities the port keeps between trainings. */
	link->caps = intel_dp->dpcd;
	link->repeater_caps = intel_dp->lttpr_common_caps;
	link->repeater_phy_caps = intel_dp->lttpr_phy_caps;

	/* The sink through the port's AUX channel, the source through the port's hooks. */
	i915_dp_bind_sink(&intel_dp->aux, &link->sink);
	link->source.levels = i915_dp_source_levels;
	link->source.pattern = i915_dp_source_pattern;
	link->source.prepare = i915_dp_source_prepare;
	link->source.idle = i915_dp_source_idle;
	link->source.sleep = i915_dp_source_sleep;
	link->source.note = i915_dp_source_note;
	link->source.context = ctx;
}

/*
 * Asks for a retraining with reduced link parameters after a failed
 * training (the Linux intel_dp_schedule_fallback_link_training()).
 *
 * The fallback is not ported: the environment records the request as an
 * error and answers that no fallback values exist, and the modeset-retry
 * work is a named step on the current device.
 */
static void
i915_dp_schedule_fallback_link_training(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *cur_i915;
	bool connected;
	int fallback;

	/* The fallback values of the environment do not read the link parameters. */
	UNUSED_PARAMETER(crtc_state);

	/* A disconnected sink is not retrained. */
	connected = i915_lcd_intel_digital_port_connected(&i915_lcd_dp_to_dig_port(intel_dp)->base);
	if (!connected) {
		lt_dbg(intel_dp, DP_PHY_DPRX, "Link Training failed on disconnected sink.\n");
		return;
	}

	/* HOBL is given up first; otherwise lower link parameters are looked for. */
	if (intel_dp->hobl_active) {
		lt_dbg(intel_dp, DP_PHY_DPRX,
		       "Link Training failed with HOBL active, not enabling it from now on\n");

		/*
		 * hobl_failed keeps the buffer translation from using HOBL on
		 * the next training.
		 */
		intel_dp->hobl_failed = true;
	} else {
		fallback = intel_dp_get_link_train_fallback_values(intel_dp,
								   crtc_state->port_clock,
								   crtc_state->lane_count);
		if (fallback)
			return;
	}

	/*
	 * Schedule a Hotplug Uevent to userspace to start modeset.  The queue
	 * and the work are not evaluated: the step is recorded on the device.
	 */
	cur_i915 = i915_dp_cur_i915(&intel_dp->aux);
	I915_LCD_QUEUE_WORK(cur_i915, NULL, &intel_dp->attached_connector->modeset_retry_work);
}

/*
 * Tells whether a sink must stay in D0 to signal downstream HPD (the Linux
 * downstream_hpd_needs_d0()).
 *
 * DPCD 1.2+ should support BRANCH_DEVICE_CTRL, and thus be capable of
 * signalling downstream hpd with a long pulse.  Whether or not that means
 * D3 is safe to use is not clear, but let's assume so until proven
 * otherwise.
 *
 * FIXME should really check all downstream ports...
 */
static bool
i915_downstream_hpd_needs_d0(
	struct intel_dp *intel_dp)
{
	bool branch;

	/* Only a DPCD 1.1 sink needs it. */
	if (intel_dp->dpcd[DP_DPCD_REV] != 0x11)
		return false;

	/* Only a branch device has downstream ports. */
	branch = dp_is_branch(intel_dp->dpcd);
	if (!branch)
		return false;

	/* Only a downstream port with HPD needs it. */
	if ((intel_dp->downstream_ports[0] & DP_DS_PORT_HPD) == 0)
		return false;

	/* Succeeded: the sink must stay in D0. */
	return true;
}

/*
 * Writes the source OUI to an eDP sink (the Linux
 * intel_edp_init_source_oui()); a careful write leaves an OUI that is
 * already set alone.
 */
static void
i915_edp_init_source_oui(
	struct intel_dp *intel_dp,
	bool careful)
{
	struct drm_i915_private *cur_i915;
	u8 oui[] = { 0x00, 0xaa, 0x01 };
	u8 buf[3];
	i915_lcd_ssize_t transferred;
	int differs;

	kern_memset(buf, 0, sizeof(buf));

	/* Finds the device whose sink the port reaches. */
	cur_i915 = i915_dp_cur_i915(&intel_dp->aux);

	/*
	 * During driver init, we want to be careful and avoid changing the source OUI if it's
	 * already set to what we want, so as to avoid clearing any state by accident
	 */
	if (careful) {
		transferred = I915_LCD_DRM_DP_DPCD_READ(cur_i915, &intel_dp->aux, DP_SOURCE_OUI, buf, sizeof(buf));
		if (transferred < 0)
			I915_LCD_DRM_ERR(NULL, "Failed to read source OUI\n");

		/* Assume the OUI was written now. */
		differs = kern_memcmp(oui, buf, sizeof(oui));
		if (differs == 0) {
			intel_dp->last_oui_write = I915_LCD_JIFFIES;
			return;
		}
	}

	/* Writes the OUI. */
	transferred = I915_LCD_DRM_DP_DPCD_WRITE(cur_i915, &intel_dp->aux, DP_SOURCE_OUI, oui, sizeof(oui));
	if (transferred < 0)
		I915_LCD_DRM_ERR(NULL, "Failed to write source OUI\n");

	/* Remembers when the OUI was written (the wait that reads it belongs to the PPS side). */
	intel_dp->last_oui_write = I915_LCD_JIFFIES;
}

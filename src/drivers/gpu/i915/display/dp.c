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
static void i915_dp_schedule_fallback_link_training(struct intel_dp *intel_dp, const struct intel_crtc_state *crtc_state);

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
 * Binds an xd sink's DPCD hooks to a DP port's AUX channel, for a caller
 * that trains or wakes the sink itself.
 */
void
drv_i915_dp_sink_bind(
	struct intel_dp *intel_dp,
	struct i915_xd_dp_sink *sink)
{
	/* The port's channel. */
	i915_dp_bind_sink(&intel_dp->aux, sink);
}

/*
 * Hears that a DP port's link did not train for a caller that trained it
 * itself: unless long HPDs are ignored, a lower link is asked for.
 */
void
drv_i915_dp_link_failed(
	struct intel_dp *intel_dp,
	const struct intel_crtc_state *crtc_state)
{
	struct drm_i915_private *i915;

	/* Finds the device the port belongs to. */
	i915 = i915_lcd_dp_to_i915(intel_dp);

	/* In fixed environments like CI, a link failure after ignored long HPDs is ignored too. */
	if (i915->display.hotplug.ignore_long_hpd) {
		lt_dbg(intel_dp, DP_PHY_DPRX, "Ignore the link failure\n");
		return;
	}

	/* Asks for the fallback. */
	i915_dp_schedule_fallback_link_training(intel_dp, crtc_state);
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

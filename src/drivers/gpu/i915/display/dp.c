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

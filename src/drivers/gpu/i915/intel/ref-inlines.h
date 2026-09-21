/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2006-2007 Intel Corporation
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * The crtc state helpers transcoder_is_dsi(), intel_crtc_has_type(),
 * intel_crtc_has_dp_encoder() and intel_crtc_needs_modeset() of Linux
 * intel_display.h and intel_display_types.h.  A header of its own because
 * the modeset environment includes it after struct intel_crtc_state, at the
 * end of display/modeset-internal.h.
 *
 * zedBSD WS031: the inline helpers transcoder_is_dsi (display/intel_display.h), intel_crtc_has_type,
 * intel_crtc_has_dp_encoder and intel_crtc_needs_modeset (display/intel_display_types.h) extracted textually
 * from the Linux v6.8.12 i915 reference (MIT; Copyright Intel Corporation -- the full notice is kept in
 * intel_display_port.c) by tools/port_lcd_calc.py.
 */

#ifndef DRIVERS_GPU_I915_INTEL_REF_INLINES_H
#define DRIVERS_GPU_I915_INTEL_REF_INLINES_H

static inline bool transcoder_is_dsi(enum transcoder transcoder)
{
	return transcoder == TRANSCODER_DSI_A || transcoder == TRANSCODER_DSI_C;
}

/* intel_display.c */
static inline bool
intel_crtc_has_type(const struct intel_crtc_state *crtc_state,
		    enum intel_output_type type)
{
	return crtc_state->output_types & BIT(type);
}

static inline bool
intel_crtc_has_dp_encoder(const struct intel_crtc_state *crtc_state)
{
	return crtc_state->output_types &
		(BIT(INTEL_OUTPUT_DP) |
		 BIT(INTEL_OUTPUT_DP_MST) |
		 BIT(INTEL_OUTPUT_EDP));
}

static inline bool
intel_crtc_needs_modeset(const struct intel_crtc_state *crtc_state)
{
	return drm_atomic_crtc_needs_modeset(&crtc_state->uapi);
}

#endif /* DRIVERS_GPU_I915_INTEL_REF_INLINES_H */

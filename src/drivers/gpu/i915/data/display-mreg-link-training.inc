/*
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
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (sha256 af257fa29746e832225a5bb909d96d3398093cfe6345d0e35ba95aec991f2b2e) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_mreg_link_training.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_MREG_LINK_TRAINING_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_MREG_LINK_TRAINING_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_MREG_LINK_TRAINING_INC

#define TRAIN_REQ_FMT "%d/%d/%d/%d"
#define _TRAIN_REQ_VSWING_ARGS(link_status, lane) \
	(drm_dp_get_adjust_request_voltage((link_status), (lane)) >> DP_TRAIN_VOLTAGE_SWING_SHIFT)
#define TRAIN_REQ_VSWING_ARGS(link_status) \
	_TRAIN_REQ_VSWING_ARGS(link_status, 0), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 1), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 2), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 3)
#define _TRAIN_REQ_PREEMPH_ARGS(link_status, lane) \
	(drm_dp_get_adjust_request_pre_emphasis((link_status), (lane)) >> DP_TRAIN_PRE_EMPHASIS_SHIFT)
#define TRAIN_REQ_PREEMPH_ARGS(link_status) \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 0), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 1), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 2), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 3)
#define _TRAIN_REQ_TX_FFE_ARGS(link_status, lane) \
	drm_dp_get_adjust_tx_ffe_preset((link_status), (lane))
#define TRAIN_REQ_TX_FFE_ARGS(link_status) \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 0), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 1), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 2), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 3)
#define TRAIN_SET_FMT "%d%s/%d%s/%d%s/%d%s"
#define _TRAIN_SET_VSWING_ARGS(train_set) \
	((train_set) & DP_TRAIN_VOLTAGE_SWING_MASK) >> DP_TRAIN_VOLTAGE_SWING_SHIFT, \
	(train_set) & DP_TRAIN_MAX_SWING_REACHED ? "(max)" : ""
#define TRAIN_SET_VSWING_ARGS(train_set) \
	_TRAIN_SET_VSWING_ARGS((train_set)[0]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[1]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[2]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[3])
#define _TRAIN_SET_PREEMPH_ARGS(train_set) \
	((train_set) & DP_TRAIN_PRE_EMPHASIS_MASK) >> DP_TRAIN_PRE_EMPHASIS_SHIFT, \
	(train_set) & DP_TRAIN_MAX_PRE_EMPHASIS_REACHED ? "(max)" : ""
#define TRAIN_SET_PREEMPH_ARGS(train_set) \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[0]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[1]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[2]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[3])
#define _TRAIN_SET_TX_FFE_ARGS(train_set) \
	((train_set) & DP_TX_FFE_PRESET_VALUE_MASK), ""
#define TRAIN_SET_TX_FFE_ARGS(train_set) \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[0]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[1]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[2]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[3])

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_MREG_LINK_TRAINING_INC */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright © 2019 Intel Corporation
 * Copyright © 2022 Intel Corporation
 * Copyright © 2014-2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The GT power-management registers the driver quotes from the Linux i915
 * headers: RC6 control, state and thresholds, coarse power gating, the
 * power-management interrupt mask, the frequency capabilities and request,
 * and the PCODE command that reports the efficient frequency.
 *
 * The RC6 and RPS registers (Linux 6.8.12).
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12 and
 * reformatted; values are unchanged.  Sources:
 *   drivers/gpu/drm/i915/gt/intel_gt_regs.h
 *   drivers/gpu/drm/i915/gt/intel_engine_regs.h
 *   drivers/gpu/drm/i915/intel_mchbar_regs.h
 *   drivers/gpu/drm/i915/i915_reg.h
 *   drivers/gpu/drm/i915/gt/intel_rps.c
 *   drivers/gpu/drm/i915/gt/intel_rps.h
 *   drivers/gpu/drm/i915/gt/intel_engine_types.h
 *   drivers/gpu/drm/i915/gt/uc/intel_guc_reg.h
 * Rewrites: _MMIO(x) -> (x); REG_BIT/REG_GENMASK are expanded to their
 * constant values; integer constants gain a U suffix.
 * Only the registers the Alder Lake-P RC6 and RPS paths use remain.
 */

#ifndef DRIVERS_GPU_I915_INTEL_GT_POWER_H
#define DRIVERS_GPU_I915_INTEL_GT_POWER_H

#include <stdint.h>

/* RC6 control, state and thresholds. */
#define I915_GT_GEN6_RC_CONTROL				0xa090U
#define I915_GT_GEN6_RC_STATE				0xa094U
#define I915_GT_GEN6_RC6_WAKE_RATE_LIMIT		0xa09cU
#define I915_GT_GEN10_MEDIA_WAKE_RATE_LIMIT		0xa0a0U
#define I915_GT_GEN6_RC_EVALUATION_INTERVAL		0xa0a8U
#define I915_GT_GEN6_RC_IDLE_HYSTERSIS			0xa0acU
#define I915_GT_GEN6_RC_SLEEP				0xa0b0U
#define I915_GT_GEN6_RC6_THRESHOLD			0xa0b8U
#define I915_GT_GEN6_RC_CTL_RC6_ENABLE			(1U << 18)
#define I915_GT_GUC_MAX_IDLE_COUNT			0xc3e4U
#define I915_GT_RING_MAX_IDLE(base)			((base) + 0x54U)

/* Coarse power gating. */
#define I915_GT_GEN9_MEDIA_PG_IDLE_HYSTERESIS		0xa0c4U
#define I915_GT_GEN9_RENDER_PG_IDLE_HYSTERESIS		0xa0c8U
#define I915_GT_GEN9_PG_ENABLE				0xa210U
#define I915_GT_GEN9_RENDER_PG_ENABLE			(1U << 0)
#define I915_GT_GEN9_MEDIA_PG_ENABLE			(1U << 1)
#define I915_GT_GEN11_MEDIA_SAMPLER_PG_ENABLE		(1U << 2)
#define I915_GT_VDN_HCP_POWERGATE_ENABLE(n)		(1U << (3U + 2U * (unsigned)(n)))
#define I915_GT_VDN_MFX_POWERGATE_ENABLE(n)		(1U << (4U + 2U * (unsigned)(n)))

/* The power-management interrupt mask. */
#define I915_GT_GEN6_PMINTRMSK				0xa168U

/* The frequency capabilities, read through the MCHBAR mirror. */
#define I915_GT_MCHBAR_MIRROR_BASE_SNB			0x140000U
#define I915_GT_GEN6_RP_STATE_CAP			(I915_GT_MCHBAR_MIRROR_BASE_SNB + 0x5998U)
#define I915_GT_GEN10_FREQ_INFO_REC			(I915_GT_MCHBAR_MIRROR_BASE_SNB + 0x5ef0U)
#define I915_GT_RPE_MASK				0x0000ff00U
#define I915_GT_GEN9_FREQ_SCALER			3U

/* The frequency request and the up/down hysteresis. */
#define I915_GT_GEN6_RPNSWREQ				0xa008U
#define I915_GT_GEN9_FREQUENCY(frequency)		((uint32_t)(frequency) << 23)

/* I915_GT_GEN6_RC_VIDEO_FREQ, which Gen9 gives RP1 (gen9_rps_enable(), Linux 6.8.12 intel_gt_regs.h). */
#define I915_GT_GEN6_RC_VIDEO_FREQ			0xa00cU

/* The Gen9 RP1 field of I915_GT_GEN6_RP_STATE_CAP (__gen6_rps_get_freq_caps()). */
#define I915_GT_GEN9_RP1_CAP_SHIFT			8U
#define I915_GT_GEN6_RP_IDLE_HYSTERSIS			0xa070U

/* The frequency limits the interrupts are given between, and the RP control. */
#define I915_GT_GEN6_RP_INTERRUPT_LIMITS		0xa014U
#define I915_GT_GEN6_RP_CONTROL				0xa024U
#define I915_GT_GEN6_RP_MEDIA_HW_NORMAL_MODE		(2U << 9)
#define I915_GT_GEN6_RP_MEDIA_IS_GFX			(1U << 8)
#define I915_GT_GEN6_RP_ENABLE				(1U << 7)
#define I915_GT_GEN6_RP_UP_BUSY_AVG			(0x2U << 3)
#define I915_GT_GEN6_RP_DOWN_IDLE_AVG			(0x2U << 0)

/* The up and down busyness thresholds and their evaluation intervals. */
#define I915_GT_GEN6_RP_UP_THRESHOLD			0xa02cU
#define I915_GT_GEN6_RP_DOWN_THRESHOLD			0xa030U
#define I915_GT_GEN6_RP_UP_EI				0xa068U
#define I915_GT_GEN6_RP_DOWN_EI				0xa06cU

/* The actual GT frequency (CAGF), in 16.67 MHz units, for the diagnostics. */
#define I915_GT_GEN12_RPSTAT1				0x1381b4U
#define I915_GT_GEN12_CAGF_SHIFT			11U
#define I915_GT_GEN12_CAGF_MASK				(0x1ffU << 11)

/* The PCODE mailbox command that reports the efficient frequency. */
#define I915_GT_HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL	0x1aU

/* How many video decode engines the power-gating mask can name. */
#define I915_MAX_VCS				8U

#endif /* DRIVERS_GPU_I915_INTEL_GT_POWER_H */

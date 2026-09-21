/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright © 2019 Intel Corporation
 * Copyright © 2022 Intel Corporation
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
 *
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12 and
 * reformatted; values are unchanged.  Sources:
 *   drivers/gpu/drm/i915/gt/intel_gt_regs.h
 *   drivers/gpu/drm/i915/gt/intel_engine_regs.h
 *   drivers/gpu/drm/i915/intel_mchbar_regs.h
 *   drivers/gpu/drm/i915/i915_reg.h
 *   drivers/gpu/drm/i915/gt/intel_rps.c
 *   drivers/gpu/drm/i915/gt/intel_engine_types.h
 * Rewrites: _MMIO(x) -> (x); REG_BIT/REG_GENMASK are expanded to their
 * constant values; integer constants gain a U suffix.
 * Only the registers the Alder Lake-P RC6 and RPS paths use remain.
 */

#ifndef DRIVERS_GPU_I915_LINUX_I915_GT_POWER_INC
#define DRIVERS_GPU_I915_LINUX_I915_GT_POWER_INC

/* RC6 control, state and thresholds. */
#define GEN6_RC_CONTROL				0xa090U
#define GEN6_RC_STATE				0xa094U
#define GEN6_RC6_WAKE_RATE_LIMIT		0xa09cU
#define GEN10_MEDIA_WAKE_RATE_LIMIT		0xa0a0U
#define GEN6_RC_EVALUATION_INTERVAL		0xa0a8U
#define GEN6_RC_IDLE_HYSTERSIS			0xa0acU
#define GEN6_RC_SLEEP				0xa0b0U
#define GEN6_RC6_THRESHOLD			0xa0b8U
#define GEN6_RC_CTL_RC6_ENABLE			(1U << 18)
#define GUC_MAX_IDLE_COUNT			0xc3e4U
#define RING_MAX_IDLE(base)			((base) + 0x54U)

/* Coarse power gating. */
#define GEN9_MEDIA_PG_IDLE_HYSTERESIS		0xa0c4U
#define GEN9_RENDER_PG_IDLE_HYSTERESIS		0xa0c8U
#define GEN9_PG_ENABLE				0xa210U
#define GEN9_RENDER_PG_ENABLE			(1U << 0)
#define GEN9_MEDIA_PG_ENABLE			(1U << 1)
#define GEN11_MEDIA_SAMPLER_PG_ENABLE		(1U << 2)
#define VDN_HCP_POWERGATE_ENABLE(n)		(1U << (3U + 2U * (unsigned)(n)))
#define VDN_MFX_POWERGATE_ENABLE(n)		(1U << (4U + 2U * (unsigned)(n)))

/* The power-management interrupt mask. */
#define GEN6_PMINTRMSK				0xa168U

/* The frequency capabilities, read through the MCHBAR mirror. */
#define MCHBAR_MIRROR_BASE_SNB			0x140000U
#define GEN6_RP_STATE_CAP			(MCHBAR_MIRROR_BASE_SNB + 0x5998U)
#define GEN10_FREQ_INFO_REC			(MCHBAR_MIRROR_BASE_SNB + 0x5ef0U)
#define RPE_MASK				0x0000ff00U
#define GEN9_FREQ_SCALER			3U

/* The frequency request and the up/down hysteresis. */
#define GEN6_RPNSWREQ				0xa008U
#define GEN9_FREQUENCY(frequency)		((uint32_t)(frequency) << 23)
#define GEN6_RP_IDLE_HYSTERSIS			0xa070U

/* The PCODE mailbox command that reports the efficient frequency. */
#define HSW_PCODE_DYNAMIC_DUTY_CYCLE_CONTROL	0x1aU

/* How many video decode engines the power-gating mask can name. */
#define I915_MAX_VCS				8U

#endif

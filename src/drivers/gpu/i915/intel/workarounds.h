/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2014-2018 Intel Corporation
 * Copyright © 2022 Intel Corporation
 */

/*
 * The workaround registers and values the driver quotes from Linux
 * intel_workarounds.c: multicast register steering, the GT, render engine and
 * render context workarounds of Alder Lake-P, and the registers the
 * whitelist opens to non-privileged batches.
 *
 * The workaround registers and values (Linux 6.8.12).
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12 and
 * reformatted; values are unchanged.  Sources:
 *   drivers/gpu/drm/i915/gt/intel_workarounds.c
 *   drivers/gpu/drm/i915/gt/intel_gt_regs.h
 *   drivers/gpu/drm/i915/gt/intel_engine_regs.h
 * Rewrites: _MMIO(x) -> (x); REG_BIT/REG_GENMASK/REG_FIELD_PREP are expanded
 * to their constant values; integer constants gain a U suffix.
 * Only the registers and fields the Alder Lake-P workaround and whitelist
 * programming uses remain.  The MOCS and PAT values of the same excerpt are in
 * mocs.h; RING_CTX_TIMESTAMP, which the whitelist also opens, is in
 * gt-regs.h.
 */

#ifndef DRIVERS_GPU_I915_INTEL_WORKAROUNDS_H
#define DRIVERS_GPU_I915_INTEL_WORKAROUNDS_H

#include <stdint.h>

/* Multicast register steering (icl_wa_init_mcr). */
#define GEN8_MCR_SELECTOR			0x0fdcU
#define GEN11_MCR_SLICE_MASK			0x78000000U
#define GEN11_MCR_SUBSLICE_MASK			0x07000000U
#define GEN11_MCR_SLICE(slice)			((((uint32_t)(slice)) & 0xfU) << 27)
#define GEN11_MCR_SUBSLICE(subslice)		((((uint32_t)(subslice)) & 0x7U) << 24)

/* gen12_gt_workarounds_init: Wa_14011060649. */
#define VDBOX_CGCTL3F10(base)			((base) + 0x3f10U)
#define IECPUNIT_CLKGATE_DIS			(1U << 22)

/* gen12_gt_workarounds_init: Wa_14011059788 (a multicast register). */
#define GEN10_DFR_RATIO_EN_AND_CHICKEN		0x9550U
#define DFR_DISABLE				(1U << 9)

/* gen12_gt_workarounds_init: Wa_14015795083. */
#define GEN7_MISCCPCTL				0x9424U
#define GEN12_DOP_CLOCK_GATE_RENDER_ENABLE	(1U << 1)

/* rcs_engine_wa_init. */
#define GEN9_CS_DEBUG_MODE1			0x20ecU
#define FF_DOP_CLOCK_GATE_DISABLE		(1U << 1)
#define GEN8_ROW_CHICKEN2			0xe4f4U
#define GEN12_DISABLE_EARLY_READ		(1U << 14)
#define GEN12_PUSH_CONST_DEREF_HOLD_DIS		(1U << 8)
#define GEN7_FF_THREAD_MODE			0x20a0U
#define GEN12_FF_TESSELATION_DOP_GATE_DISABLE	(1U << 19)
#define GEN10_SAMPLER_MODE			0xe18cU
#define ENABLE_SMALLPL				(1U << 15)
#define GEN9_ROW_CHICKEN4			0xe48cU
#define GEN12_DISABLE_TDL_PUSH			(1U << 9)
#define RING_PSMI_CTL(base)			((base) + 0x50U)
#define GEN8_RC_SEMA_IDLE_MSG_DISABLE		(1U << 12)
#define GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE	(1U << 7)
#define GEN7_FF_SLICE_CS_CHICKEN1		0x20e0U
#define GEN9_FFSC_PERCTX_PREEMPT_CTRL		(1U << 14)

/* gen12_ctx_workarounds_init. */
#define GEN11_COMMON_SLICE_CHICKEN3		0x7304U
#define GEN12_DISABLE_CPS_AWARE_COLOR_PIPE	(1U << 9)
#define GEN8_CS_CHICKEN1			0x2580U
#define GEN9_PREEMPT_GPGPU_LEVEL_MASK		0x6U
#define GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL	0x2U
#define GEN12_FF_MODE2				0x6604U
#define FF_MODE2_GS_TIMER_224			(224U << 24)
#define FF_MODE2_TDS_TIMER_128			(4U << 16)
#define HIZ_CHICKEN				0x7018U
#define HZ_DEPTH_TEST_LE_GE_OPT_DISABLE		(1U << 13)
#define COMMON_SLICE_CHICKEN4			0x7300U
#define DISABLE_TDC_LOAD_BALANCING_CALC		(1U << 6)

/* tgl_whitelist_build and allow_read_ctx_timestamp. */
#define PS_INVOCATION_COUNT			0x2348U
#define GEN7_COMMON_SLICE_CHICKEN1		0x7010U
#define RING_FORCE_TO_NONPRIV_ACCESS_RD		(1U << 28)
#define RING_FORCE_TO_NONPRIV_RANGE_4		(1U << 0)

/* intel_engine_apply_whitelist: the per-engine non-privileged slots. */
#define RING_FORCE_TO_NONPRIV(base, slot)	((base) + 0x4d0U + (unsigned)(slot) * 4U)
#define RING_NOPID(base)			((base) + 0x94U)
#define RING_MAX_NONPRIV_SLOTS			12U

#endif /* DRIVERS_GPU_I915_INTEL_WORKAROUNDS_H */

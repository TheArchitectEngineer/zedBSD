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
 * mocs.h; I915_GT_RING_CTX_TIMESTAMP, which the whitelist also opens, is in
 * gt-regs.h.
 */

#ifndef DRIVERS_GPU_I915_INTEL_WORKAROUNDS_H
#define DRIVERS_GPU_I915_INTEL_WORKAROUNDS_H

#include <stdint.h>

/* Multicast register steering (icl_wa_init_mcr). */
#define I915_GT_GEN8_MCR_SELECTOR			0x0fdcU
#define I915_GT_GEN11_MCR_SLICE_MASK			0x78000000U
#define I915_GT_GEN11_MCR_SUBSLICE_MASK			0x07000000U
#define I915_GT_GEN11_MCR_SLICE(slice)			((((uint32_t)(slice)) & 0xfU) << 27)
#define I915_GT_GEN11_MCR_SUBSLICE(subslice)		((((uint32_t)(subslice)) & 0x7U) << 24)

/* gen12_gt_workarounds_init: Wa_14011060649. */
#define I915_GT_VDBOX_CGCTL3F10(base)			((base) + 0x3f10U)
#define I915_GT_IECPUNIT_CLKGATE_DIS			(1U << 22)

/* gen12_gt_workarounds_init: Wa_14011059788 (a multicast register). */
#define I915_GT_GEN10_DFR_RATIO_EN_AND_CHICKEN		0x9550U
#define I915_GT_DFR_DISABLE				(1U << 9)

/* gen12_gt_workarounds_init: Wa_14015795083. */
#define I915_GT_GEN7_MISCCPCTL				0x9424U
#define I915_GT_GEN12_DOP_CLOCK_GATE_RENDER_ENABLE	(1U << 1)

/* rcs_engine_wa_init. */
#define I915_GT_GEN9_CS_DEBUG_MODE1			0x20ecU
#define I915_GT_FF_DOP_CLOCK_GATE_DISABLE		(1U << 1)
#define I915_GT_GEN8_ROW_CHICKEN2			0xe4f4U
#define I915_GT_GEN12_DISABLE_EARLY_READ		(1U << 14)
#define I915_GT_GEN12_PUSH_CONST_DEREF_HOLD_DIS		(1U << 8)
#define I915_GT_GEN7_FF_THREAD_MODE			0x20a0U
#define I915_GT_GEN12_FF_TESSELATION_DOP_GATE_DISABLE	(1U << 19)
#define I915_GT_GEN10_SAMPLER_MODE			0xe18cU
#define I915_GT_ENABLE_SMALLPL				(1U << 15)
#define I915_GT_GEN9_ROW_CHICKEN4			0xe48cU
#define I915_GT_GEN12_DISABLE_TDL_PUSH			(1U << 9)
#define I915_GT_RING_PSMI_CTL(base)			((base) + 0x50U)
#define I915_GT_GEN8_RC_SEMA_IDLE_MSG_DISABLE		(1U << 12)
#define I915_GT_GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE	(1U << 7)
#define I915_GT_GEN7_FF_SLICE_CS_CHICKEN1		0x20e0U
#define I915_GT_GEN9_FFSC_PERCTX_PREEMPT_CTRL		(1U << 14)

/* gen12_ctx_workarounds_init. */
#define I915_GT_GEN11_COMMON_SLICE_CHICKEN3		0x7304U
#define I915_GT_GEN12_DISABLE_CPS_AWARE_COLOR_PIPE	(1U << 9)
#define I915_GT_GEN8_CS_CHICKEN1			0x2580U
#define I915_GT_GEN9_PREEMPT_GPGPU_LEVEL_MASK		0x6U
#define I915_GT_GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL	0x2U
#define I915_GT_GEN12_FF_MODE2				0x6604U
#define I915_GT_FF_MODE2_GS_TIMER_224			(224U << 24)
#define I915_GT_FF_MODE2_TDS_TIMER_128			(4U << 16)
#define I915_GT_HIZ_CHICKEN				0x7018U
#define I915_GT_HZ_DEPTH_TEST_LE_GE_OPT_DISABLE		(1U << 13)
#define I915_GT_COMMON_SLICE_CHICKEN4			0x7300U
#define I915_GT_DISABLE_TDC_LOAD_BALANCING_CALC		(1U << 6)

/* tgl_whitelist_build and allow_read_ctx_timestamp. */
#define I915_GT_PS_INVOCATION_COUNT			0x2348U
#define I915_GT_GEN7_COMMON_SLICE_CHICKEN1		0x7010U
#define I915_GT_RING_FORCE_TO_NONPRIV_ACCESS_RD		(1U << 28)
#define I915_GT_RING_FORCE_TO_NONPRIV_RANGE_4		(1U << 0)

/* intel_engine_apply_whitelist: the per-engine non-privileged slots. */
#define I915_GT_RING_FORCE_TO_NONPRIV(base, slot)	((base) + 0x4d0U + (unsigned)(slot) * 4U)
#define I915_GT_RING_NOPID(base)			((base) + 0x94U)
#define I915_GT_RING_MAX_NONPRIV_SLOTS			12U

/*
 * The Gen9 (Skylake, Kaby Lake, Coffee Lake, Comet Lake) workaround,
 * whitelist and GTT-workaround registers, excerpted by hand from
 * the Linux 6.8.12 reference; values are unchanged.  Sources (SHA-256):
 *   drivers/gpu/drm/i915/gt/intel_gt_regs.h
 *     e0b8207e1799fc7e061eb38fe6ebc3e87bea8eca63040f4214358436ca115a83
 *   drivers/gpu/drm/i915/i915_reg.h
 *     efcb6385e32031ca3c5b97f7691c5ed1706c2a6709e3875ff78c2cf2eff0e626
 *   drivers/gpu/drm/i915/gt/intel_engine_regs.h (I915_GT_RING_SEMA_WAIT_POLL)
 */

/* gen9_wa_init_mcr: the Gen8 layout of the steering selector. */
#define I915_GT_GEN8_MCR_SLICE(slice)			((((uint32_t)(slice)) & 0x3U) << 26)
#define I915_GT_GEN8_MCR_SLICE_MASK			I915_GT_GEN8_MCR_SLICE(3U)
#define I915_GT_GEN8_MCR_SUBSLICE(subslice)		((((uint32_t)(subslice)) & 0x3U) << 24)
#define I915_GT_GEN8_MCR_SUBSLICE_MASK			I915_GT_GEN8_MCR_SUBSLICE(3U)

/* gen9_gt_workarounds_init and the skl, kbl and cfl GT lists. */
#define I915_GT_GAM_ECOCHK				0x4090U
#define I915_GT_BDW_DISABLE_HDC_INVALIDATION		(1U << 25)
#define I915_GT_ECOCHK_DIS_TLB				(1U << 8)
#define I915_GT_MMCD_MISC_CTRL				0x4ddcU
#define I915_GT_MMCD_PCLA				(1U << 31)
#define I915_GT_MMCD_HOTSPOT_EN				(1U << 27)
#define I915_GT_GEN7_UCGCTL4				0x940cU
#define I915_GT_GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE	(1U << 14)
#define I915_GT_GEN9_GAMT_ECO_REG_RW_IA			0x4ab0U
#define I915_GT_GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS	(1U << 18)
#define I915_GT_GAMT_CHKN_BIT_REG			0x4ab8U
#define I915_GT_GAMT_CHKN_DISABLE_DYNAMIC_CREDIT_SHARING	(1U << 28)

/* gen9_ctx_workarounds_init and the kbl and cfl context lists (COMMON_SLICE_CHICKEN2 is in gt-regs.h). */
#define I915_GT_GEN9_PBE_COMPRESSED_HASH_SELECTION	(1U << 13)
#define I915_GT_GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION	(1U << 8)
#define I915_GT_GEN9_HALF_SLICE_CHICKEN7		0xe194U
#define I915_GT_GEN9_SAMPLER_HASH_COMPRESSED_READ_ADDR	(1U << 8)
#define I915_GT_GEN9_ENABLE_YV12_BUGFIX			(1U << 4)
#define I915_GT_GEN9_ENABLE_GPGPU_PREEMPTION		(1U << 2)
#define I915_GT_GEN8_ROW_CHICKEN			0xe4f0U
#define I915_GT_FLOW_CONTROL_ENABLE			(1U << 15)
#define I915_GT_PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE	(1U << 8)
#define I915_GT_CACHE_MODE_1				0x7004U
#define I915_GT_GEN8_4x4_STC_OPTIMIZATION_DISABLE	(1U << 6)
#define I915_GT_GEN9_PARTIAL_RESOLVE_IN_VC_DISABLE	(1U << 1)
#define I915_GT_GEN9_HALF_SLICE_CHICKEN5		0xe188U
#define I915_GT_GEN9_CCS_TLB_PREFETCH_ENABLE		(1U << 3)
#define I915_GT_HDC_CHICKEN0				0x7300U
#define I915_GT_HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT	(1U << 5)
#define I915_GT_HDC_FORCE_CSR_NON_COHERENT_OVR_DISABLE	(1U << 15)
#define I915_GT_HDC_FORCE_NON_COHERENT			(1U << 4)
#define I915_GT_GEN8_HALF_SLICE_CHICKEN3		0xe184U
#define I915_GT_GEN8_SAMPLER_POWER_BYPASS_DIS		(1U << 1)
#define I915_GT_HALF_SLICE_CHICKEN2			0xe180U
#define I915_GT_GEN8_ST_PO_DISABLE			(1U << 13)
#define I915_GT_GEN9_PREEMPT_3D_OBJECT_LEVEL		(1U << 0)
#define I915_GT_GEN9_PREEMPT_GPGPU_COMMAND_LEVEL	0x4U
#define I915_GT_GEN8_HALF_SLICE_CHICKEN1		0xe100U
#define I915_GT_GEN7_SBE_SS_CACHE_DISPATCH_PORT_SHARING_DISABLE	(1U << 4)

/* rcs_engine_wa_init and xcs_engine_wa_init for graphics version 9 (L3SQCREG4 is in gt-regs.h). */
#define I915_GT_GEN8_GARBCNTL				0xb004U
#define I915_GT_GEN9_GAPS_TSV_CREDIT_DISABLE		(1U << 7)
#define I915_GT_GEN9_CSFE_CHICKEN1_RCS			0x20d4U
#define I915_GT_GEN9_PREEMPT_GPGPU_SYNC_SWITCH_DISABLE	(1U << 2)
#define I915_GT_BDW_SCRATCH1				0xb11cU
#define I915_GT_GEN9_LBS_SLA_RETRY_TIMER_DECREMENT_ENABLE	(1U << 2)
#define I915_GT_EVICTION_PERF_FIX_ENABLE		(1U << 8)
#define I915_GT_GEN8_LQSQ_NONIA_COHERENT_ATOMICS_ENABLE	(1U << 22)
#define I915_GT_GEN9_SCRATCH_LNCF1			0xb008U
#define I915_GT_GEN9_LNCF_NONIA_COHERENT_ATOMICS_ENABLE	(1U << 0)
#define I915_GT_RING_SEMA_WAIT_POLL(base)		((base) + 0x24cU)

/* gen9_whitelist_build and its skl, kbl, cfl and cml callers. */
#define I915_GT_GEN9_CTX_PREEMPT_REG			0x2248U
#define I915_GT_GEN8_HDC_CHICKEN1			0x7304U

/* gtt_write_workarounds for graphics version 9. */
#define I915_GT_GEN8_L3_LRA_1_GPGPU			0x4dd4U
#define I915_GT_GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_SKL	0x67F1427FU
#define I915_GT_GEN8_GAMW_ECO_DEV_RW_IA			0x4080U
#define I915_GT_GAMW_ECO_ENABLE_64K_IPS_FIELD		0xFU
#define I915_GT_HSW_GTT_CACHE_EN			0x4024U
#define I915_GT_GTT_CACHE_EN_ALL			0xF0007FFFU

#endif /* DRIVERS_GPU_I915_INTEL_WORKAROUNDS_H */

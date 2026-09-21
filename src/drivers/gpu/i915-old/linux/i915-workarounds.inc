/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Alder Lake-P render workarounds, transcribed from Linux 7.1
 * drivers/gpu/drm/i915/gt/intel_workarounds.c (gen12_gt_workarounds_init,
 * rcs_engine_wa_init, general_render_compute_wa_init, engine_fake_wa_init
 * and gen12_ctx_workarounds_init) and intel_gt_regs.h / intel_engine_regs.h.
 *
 * Linux applies the first group to the hardware once per engine through
 * MMIO and the second group inside every render context through
 * MI_LOAD_REGISTER_IMM, because those registers are saved and restored with
 * the context.  Mesa runs on top of both groups, so every 3D command
 * sequence it produces assumes they are in place.
 *
 * A masked register takes the bits to change in its upper half; a value of
 * ((bits) << 16) | (bits) sets them and ((bits) << 16) clears them.
 */

#ifndef I915_WORKAROUNDS_INC
#define I915_WORKAROUNDS_INC

#define I915_WA_MASKED_ENABLE(bits)		(((bits) << 16) | (bits))
#define I915_WA_MASKED_FIELD(mask, value)	(((mask) << 16) | (value))

/* Multicast register steering: writes reach every slice and subslice. */
#define GEN8_MCR_SELECTOR			0x0fdcU
#define GEN11_MCR_MULTICAST			(1U << 31)

/* Wa_14015795083: render DOP clock gating stays off (GEN7_MISCCPCTL). */
#define GEN7_MISCCPCTL				0x9424U
#define GEN12_DOP_CLOCK_GATE_RENDER_ENABLE	(1U << 1)

/*
 * Wa_1606700617 / Wa_22010271021 / Wa_14010826681: fixed-function DOP clock
 * gating disabled (GEN9_CS_DEBUG_MODE1, masked).
 */
#define GEN9_CS_DEBUG_MODE1			0x20ecU
#define FF_DOP_CLOCK_GATE_DISABLE		(1U << 1)

/* Wa_14010919138 and friends: tessellation DOP gate disabled (GEN7_FF_THREAD_MODE). */
#define GEN7_FF_THREAD_MODE			0x20a0U
#define GEN12_FF_TESSELATION_DOP_GATE_DISABLE	(1U << 19)

/* Wa_1607297627: no power-down while waiting for events (RING_PSMI_CTL, masked). */
#define RING_PSMI_CTL(base)			((base) + 0x50U)
#define GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE	(1U << 7)
#define GEN8_RC_SEMA_IDLE_MSG_DISABLE		(1U << 12)

/* Per-context preemption control (GEN7_FF_SLICE_CS_CHICKEN1, masked). */
#define GEN7_FF_SLICE_CS_CHICKEN1		0x20e0U
#define GEN9_FFSC_PERCTX_PREEMPT_CTRL		(1U << 14)

/*
 * The command streamer's own accesses use an explicit MOCS (RING_CMD_CCTL,
 * masked fields; both fields carry the index shifted left by one).
 */
#define RING_CMD_CCTL(base)			((base) + 0xc4U)
#define CMD_CCTL_WRITE_OVERRIDE_MASK		(0x7fU << 7)
#define CMD_CCTL_READ_OVERRIDE_MASK		0x7fU
#define CMD_CCTL_MOCS_OVERRIDE(write, read)	((((write) << 1) << 7) | ((read) << 1))

/* Wa_1606931601 / Wa_1409804808 (GEN8_ROW_CHICKEN2, multicast, masked). */
#define GEN8_ROW_CHICKEN2			0xe4f4U
#define GEN12_DISABLE_EARLY_READ		(1U << 14)
#define GEN12_PUSH_CONST_DEREF_HOLD_DIS		(1U << 8)

/* Wa_14010229206 (GEN9_ROW_CHICKEN4, multicast, masked). */
#define GEN9_ROW_CHICKEN4			0xe48cU
#define GEN12_DISABLE_TDL_PUSH			(1U << 9)

/* Wa_1406941453 and the gen11+ indirect state base override (GEN10_SAMPLER_MODE). */
#define GEN10_SAMPLER_MODE			0xe18cU
#define ENABLE_SMALLPL				(1U << 15)
#define GEN11_INDIRECT_STATE_BASE_ADDR_OVERRIDE	(1U << 0)

/* Context workarounds: Wa_1409142259 and ten siblings (GEN11_COMMON_SLICE_CHICKEN3). */
#define GEN11_COMMON_SLICE_CHICKEN3		0x7304U
#define GEN12_DISABLE_CPS_AWARE_COLOR_PIPE	(1U << 9)

/*
 * GEN8_CS_CHICKEN1: Linux sets GPGPU preemption to thread-group level;
 * Mesa's anv additionally selects mid-command-buffer replay and, on this
 * generation, disables preemption pausing at 3DPRIMITIVE.
 */
#define GEN8_CS_CHICKEN1			0x2580U
#define GEN9_PREEMPT_GPGPU_LEVEL_MASK		(3U << 1)
#define GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL	(1U << 1)
#define CS_CHICKEN1_REPLAY_MODE_MASK		(1U << 0)
#define CS_CHICKEN1_REPLAY_MIDCMDBUFFER		(1U << 0)
#define CS_CHICKEN1_NO_3DPRIM_PAUSE		(1U << 10)

/*
 * Wa_16011163337: the fixed-function timers (GEN12_FF_MODE2).  The values
 * are the ones a running Mesa stack ends up with: GS and HS timers 224,
 * TDS timer 4, VS timer 0.
 */
#define GEN12_FF_MODE2				0x6604U
#define GEN12_FF_MODE2_MESA_VALUE		((224U << 24) | (4U << 16) | (224U << 8))

/* Wa_1806527549 (HIZ_CHICKEN, masked). */
#define HIZ_CHICKEN				0x7018U
#define HZ_DEPTH_TEST_LE_GE_OPT_DISABLE		(1U << 13)

/* Wa_1606376872 (COMMON_SLICE_CHICKEN4, masked). */
#define COMMON_SLICE_CHICKEN4			0x7300U
#define DISABLE_TDC_LOAD_BALANCING_CALC		(1U << 6)

/* Wa_1508744258, applied by Mesa on this generation (COMMON_SLICE_CHICKEN1, masked). */
#define COMMON_SLICE_CHICKEN1			0x7010U
#define RCC_RHWO_OPTIMIZATION_DISABLE		(1U << 14)

/*
 * Render power and clock state (GEN8_R_PWR_CLK_STATE, saved in the context).
 *
 * From intel_sseu_make_rpcs: "Starting in Gen9, render power gating can leave
 * slice/subslice/EU in a partially enabled state.  We must make an explicit
 * request through RPCS for full enablement."  Tiger Lake and Alder Lake
 * support slice-level power gating only, so the request names the slice
 * count (gen11+ field) and nothing else.  The slice count comes from the
 * GEN11_GT_SLICE_ENABLE fuse, whose low byte is one bit per slice.
 */
#define GEN11_GT_SLICE_ENABLE			0x9138U
#define GEN11_GT_S_ENA_MASK			0xffU
#define GEN8_RPCS_ENABLE			(1U << 31)
#define GEN8_RPCS_S_CNT_ENABLE			(1U << 18)
#define GEN11_RPCS_S_CNT_SHIFT			12U
#define GEN11_RPCS_S_CNT_MASK			(0x3fU << GEN11_RPCS_S_CNT_SHIFT)
#define GEN12_RPCS_SLICES(slices) \
	(GEN8_RPCS_ENABLE | GEN8_RPCS_S_CNT_ENABLE | \
	 (((slices) << GEN11_RPCS_S_CNT_SHIFT) & GEN11_RPCS_S_CNT_MASK))

#endif /* I915_WORKAROUNDS_INC */

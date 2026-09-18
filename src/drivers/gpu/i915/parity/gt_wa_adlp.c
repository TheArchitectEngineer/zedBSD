/*
 * WS031 Linux-parity — the ADL-P workaround tables, re-derived from the 6.8.12
 * reference (the expert's decision: the big-bang tree's tables are transcribed
 * from Linux 7.1, a different kernel, and are NOT inherited here).
 *
 * Every entry names its Wa_ number so the manifest can be diffed against a
 * Linux-side dump.  Only the arms ADL-P actually takes are built; the others
 * are deliberately absent rather than written and skipped.
 */
#include "gt_init.h"
#include "gt_mmio.h"
#include <kern/klog.h>

/* ---------------- registers ---------------- */

/* Group A: gen12_gt_workarounds_init */
#define GEN8_MCR_SELECTOR             0x0fdcu
#define GEN11_MCR_SLICE_MASK          0x78000000u
#define GEN11_MCR_SUBSLICE_MASK       0x07000000u
#define GEN11_MCR_SLICE(s)            ((((uint32_t)(s)) & 0xfu) << 27)
#define GEN11_MCR_SUBSLICE(ss)        ((((uint32_t)(ss)) & 0x7u) << 24)

#define VDBOX_CGCTL3F10(base)         ((base) + 0x3f10u)
#define IECPUNIT_CLKGATE_DIS          (1u << 22)

#define GEN10_DFR_RATIO_EN_AND_CHICKEN 0x9550u   /* MCR */
#define DFR_DISABLE                   (1u << 9)

#define GEN7_MISCCPCTL                0x9424u
#define GEN12_DOP_CLOCK_GATE_RENDER_ENABLE (1u << 1)

/* Group B: rcs_engine_wa_init */
#define GEN9_CS_DEBUG_MODE1           0x20ecu
#define FF_DOP_CLOCK_GATE_DISABLE     (1u << 1)
#define GEN8_ROW_CHICKEN2             0xe4f4u   /* MCR */
#define GEN12_DISABLE_EARLY_READ      (1u << 14)
#define GEN12_PUSH_CONST_DEREF_HOLD_DIS (1u << 8)
#define GEN7_FF_THREAD_MODE           0x20a0u
#define GEN12_FF_TESSELATION_DOP_GATE_DISABLE (1u << 19)
#define GEN10_SAMPLER_MODE            0xe18cu   /* MCR */
#define ENABLE_SMALLPL                (1u << 15)
#define GEN9_ROW_CHICKEN4             0xe48cu   /* MCR */
#define GEN12_DISABLE_TDL_PUSH        (1u << 9)
#define RING_PSMI_CTL(base)           ((base) + 0x50u)
#define GEN8_RC_SEMA_IDLE_MSG_DISABLE (1u << 12)
#define GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE (1u << 7)
#define GEN7_FF_SLICE_CS_CHICKEN1     0x20e0u
#define GEN9_FFSC_PERCTX_PREEMPT_CTRL (1u << 14)

/* Group C: gen12_ctx_workarounds_init */
#define GEN11_COMMON_SLICE_CHICKEN3   0x7304u
#define GEN12_DISABLE_CPS_AWARE_COLOR_PIPE (1u << 9)
#define GEN8_CS_CHICKEN1              0x2580u
#define GEN9_PREEMPT_GPGPU_LEVEL_MASK 0x6u
#define GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL 0x2u
#define GEN12_FF_MODE2                0x6604u
#define FF_MODE2_GS_TIMER_224         (224u << 24)
#define FF_MODE2_TDS_TIMER_128        (4u << 16)
#define HIZ_CHICKEN                   0x7018u
#define HZ_DEPTH_TEST_LE_GE_OPT_DISABLE (1u << 13)
#define COMMON_SLICE_CHICKEN4         0x7300u
#define DISABLE_TDC_LOAD_BALANCING_CALC (1u << 6)

/* Group D: engine_fake_wa_init / gen12_ctx_gt_mocs_init */
#define RING_CMD_CCTL(base)           ((base) + 0xc4u)
#define CMD_CCTL_MOCS_MASK            0x00003fffu
#define CMD_CCTL_MOCS_OVERRIDE(w, r) \
	(((((uint32_t)(w) << 1) << 7) & 0x3f80u) | (((uint32_t)(r) << 1) & 0x7fu))
#define BLIT_CCTL(base)               ((base) + 0x204u)
#define BLIT_CCTL_MASK                0x00007f7fu
#define BLIT_CCTL_MOCS(d, s) \
	(((((uint32_t)(d) << 1) << 8) & 0x7f00u) | (((uint32_t)(s) << 1) & 0x7fu))

/* Group E: tgl_whitelist_build */
#define PS_INVOCATION_COUNT           0x2348u
#define GEN7_COMMON_SLICE_CHICKEN1    0x7010u
#define RING_FORCE_TO_NONPRIV_ACCESS_RD (1u << 28)
#define RING_FORCE_TO_NONPRIV_RANGE_4   (1u << 0)
#define RING_CTX_TIMESTAMP(base)      ((base) + 0x3a8u)

/* ---------------- intel_gt_init_workarounds ---------------- */

/*
 * icl_wa_init_mcr(): steer MCR reads at the LOWEST subslice that is not fused
 * off.  With render power gating on, forcewake alone only powers the minconfig
 * subslice, so steering at a higher one reads back zeros or garbage.  The slice
 * is always 0 on ADL-P.
 */
static void
icl_wa_init_mcr(struct parity_wa_list *wal, const struct parity_gt_mmio *g)
{
	unsigned subslice = 0u;
	unsigned i;

	for (i = 0u; i < 16u; i++)
		if (g != 0 && (g->sseu.subslice_mask & (1u << i))) { subslice = i; break; }

	parity_wa_write_clr_set(wal, GEN8_MCR_SELECTOR,
		GEN11_MCR_SLICE_MASK | GEN11_MCR_SUBSLICE_MASK,
		GEN11_MCR_SLICE(0) | GEN11_MCR_SUBSLICE(subslice),
		0, "MCR steering (icl_wa_init_mcr)");
}

/* Wa_14011060649: even-instance VDBOX engines only. */
static void
wa_14011060649(struct parity_wa_list *wal, const struct parity_gt_mmio *g)
{
	unsigned i;

	if (g == 0)
		return;

	for (i = 0u; i < g->num_engines; i++) {
		const struct parity_engine *e = &g->engines[i];

		if (e->class != PARITY_VIDEO_DECODE_CLASS || (e->instance % 2) != 0)
			continue;
		parity_wa_write_or(wal, VDBOX_CGCTL3F10(e->mmio_base),
			IECPUNIT_CLKGATE_DIS, 0, "Wa_14011060649");
	}
}

void
parity_gt_init_workarounds_adlp(struct parity_wa_list *wal,
	const struct parity_gt_mmio *g)
{
	wal->count = 0u;
	wal->overflow = 0u;
	wal->name = "gt_wa";

	/* gt_tuning_settings(): only MTL / PVC / DG2 -- nothing for ADL-P. */

	icl_wa_init_mcr(wal, g);

	/* Wa_14011060649:tgl,rkl,dg1,adl-s,adl-p */
	wa_14011060649(wal, g);

	/* Wa_14011059788:tgl,rkl,adl-s,dg1,adl-p */
	parity_wa_write_or(wal, GEN10_DFR_RATIO_EN_AND_CHICKEN, DFR_DISABLE,
		1 /* MCR */, "Wa_14011059788");

	/*
	 * Wa_14015795083.  NOTE the reference's argument order:
	 *   wa_add(wal, GEN7_MISCCPCTL, GEN12_DOP_CLOCK_GATE_RENDER_ENABLE, 0, 0, false)
	 * is (reg, CLEAR, set, read_mask, masked) -- it CLEARS the bit, and the
	 * readback is not verified because firmware may have locked the register.
	 */
	parity_wa_add_no_verify(wal, GEN7_MISCCPCTL,
		GEN12_DOP_CLOCK_GATE_RENDER_ENABLE, 0u, 0, "Wa_14015795083");
}

void
parity_gt_init_workarounds(struct parity_wa_list *wal, int graphics_ver)
{
	(void)graphics_ver;
	parity_gt_init_workarounds_adlp(wal, 0);
}

/* ---------------- intel_engine_init_workarounds ---------------- */

void
parity_engine_init_workarounds(struct parity_wa_list *wal,
	const struct parity_engine *e, int graphics_ver, unsigned mocs_uc_index)
{
	wal->count = 0u;
	wal->overflow = 0u;
	wal->name = e->name;

	if (graphics_ver < 4)
		return;

	/*
	 * engine_fake_wa_init(): RING_CMD_CCTL names the default MOCS entry the
	 * command streamer uses.  ADL-P has no L3 CCS read, so both halves take
	 * the uncached index.
	 */
	parity_wa_masked_field_set(wal, RING_CMD_CCTL(e->mmio_base),
		CMD_CCTL_MOCS_MASK,
		CMD_CCTL_MOCS_OVERRIDE(mocs_uc_index, mocs_uc_index),
		0, "fake: RING_CMD_CCTL MOCS");

	/*
	 * general_render_compute_wa_init() / ccs_engine_wa_mode() are DG2 / PVC /
	 * MTL only; ADL-P adds nothing there.
	 */

	if (e->class != PARITY_RENDER_CLASS) {
		/*
		 * xcs_engine_wa_init(): the KBL semaphore poll and the XeHP
		 * fastcolor BLT WA; neither applies to ADL-P.
		 */
		return;
	}

	/* rcs_engine_wa_init(), the arms ADL-P takes. */

	/* Wa_1606700617 / Wa_22010271021 / Wa_14010826681 */
	parity_wa_masked_en(wal, GEN9_CS_DEBUG_MODE1, FF_DOP_CLOCK_GATE_DISABLE,
		0, "Wa_1606700617");

	/* Wa_1606931601 */
	parity_wa_masked_en(wal, GEN8_ROW_CHICKEN2, GEN12_DISABLE_EARLY_READ,
		1, "Wa_1606931601");
	/* Wa_14010919138 (and the tgl/dg1 aliases) */
	parity_wa_write_or(wal, GEN7_FF_THREAD_MODE,
		GEN12_FF_TESSELATION_DOP_GATE_DISABLE, 0, "Wa_14010919138");
	/* Wa_1406941453 */
	parity_wa_masked_en(wal, GEN10_SAMPLER_MODE, ENABLE_SMALLPL, 1,
		"Wa_1406941453");

	/* Wa_1409804808 -- merges into the GEN8_ROW_CHICKEN2 entry above. */
	parity_wa_masked_en(wal, GEN8_ROW_CHICKEN2, GEN12_PUSH_CONST_DEREF_HOLD_DIS,
		1, "Wa_1409804808");
	/* Wa_14010229206 */
	parity_wa_masked_en(wal, GEN9_ROW_CHICKEN4, GEN12_DISABLE_TDL_PUSH, 1,
		"Wa_14010229206");

	/* Wa_1607297627 */
	parity_wa_masked_en(wal, RING_PSMI_CTL(e->mmio_base),
		GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE |
		GEN8_RC_SEMA_IDLE_MSG_DISABLE, 0, "Wa_1607297627");

	/* GRAPHICS_VER >= 9: per-context preemption control. */
	if (graphics_ver >= 9)
		parity_wa_masked_en(wal, GEN7_FF_SLICE_CS_CHICKEN1,
			GEN9_FFSC_PERCTX_PREEMPT_CTRL, 0, "perctx preempt ctrl");
}

/* ---------------- intel_engine_init_ctx_wa ---------------- */

void
parity_engine_init_ctx_wa(struct parity_wa_list *wal,
	const struct parity_engine *e, int graphics_ver, unsigned mocs_uc_index)
{
	wal->count = 0u;
	wal->overflow = 0u;
	wal->name = "ctx_wa";

	/*
	 * gen12_ctx_gt_fake_wa_init() applies to ALL engines.  Its
	 * fakewa_disable_nestedbb_mode half needs IP_VER >= 12.55 (ADL-P is
	 * 12.10, so not taken); gen12_ctx_gt_mocs_init touches BLIT_CCTL and
	 * ONLY for the copy engine.
	 */
	if (graphics_ver >= 12 && e->class == PARITY_COPY_ENGINE_CLASS)
		parity_wa_write_clr_set(wal, BLIT_CCTL(e->mmio_base),
			BLIT_CCTL_MASK, BLIT_CCTL_MOCS(mocs_uc_index, mocs_uc_index),
			0, "gen12_ctx_gt_mocs_init: BLIT_CCTL");

	if (e->class != PARITY_RENDER_CLASS)
		return;

	/* gen12_ctx_workarounds_init() */

	/* Wa_1409142259 and the other nine aliases */
	parity_wa_masked_en(wal, GEN11_COMMON_SLICE_CHICKEN3,
		GEN12_DISABLE_CPS_AWARE_COLOR_PIPE, 0, "Wa_1409142259");

	/* WaDisableGPGPUMidThreadPreemption:gen12 */
	parity_wa_masked_field_set(wal, GEN8_CS_CHICKEN1,
		GEN9_PREEMPT_GPGPU_LEVEL_MASK,
		GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL, 0,
		"WaDisableGPGPUMidThreadPreemption");

	/*
	 * Wa_16011163337 (GS_TIMER) + TDS_TIMER.  FF_MODE2 reads back the wrong
	 * value from the CPU (Wa_1608008084), so the clear mask is ~0 -- write
	 * the wanted value outright -- and the readback is NOT verified.
	 */
	parity_wa_add_no_verify(wal, GEN12_FF_MODE2, 0xffffffffu,
		FF_MODE2_TDS_TIMER_128 | FF_MODE2_GS_TIMER_224, 0,
		"Wa_16011163337 (FF_MODE2)");

	/* !IS_DG1 -> ADL-P takes both of these. */
	/* Wa_1806527549 */
	parity_wa_masked_en(wal, HIZ_CHICKEN, HZ_DEPTH_TEST_LE_GE_OPT_DISABLE, 0,
		"Wa_1806527549");
	/* Wa_1606376872 */
	parity_wa_masked_en(wal, COMMON_SLICE_CHICKEN4,
		DISABLE_TDC_LOAD_BALANCING_CALC, 0, "Wa_1606376872");
}

/* ---------------- intel_engine_init_whitelist ---------------- */

/*
 * whitelist_reg_ext() stores the WHITELISTED ADDRESS plus its access flags in
 * the entry's reg field; the apply pass writes that word straight into a
 * RING_FORCE_TO_NONPRIV slot.  It is not a register to program.
 */
static void
whitelist_reg_ext(struct parity_wa_list *wal, uint32_t reg, uint32_t flags,
	const char *name)
{
	if (wal->count >= (unsigned)PARITY_WA_MAX) { wal->overflow++; return; }
	wal->list[wal->count].reg = reg | flags;
	wal->list[wal->count].clr = 0u;
	wal->list[wal->count].set = 0u;
	wal->list[wal->count].read_mask = 0u;
	wal->list[wal->count].kind = PARITY_WA_PLAIN;
	wal->list[wal->count].is_mcr = 0;
	wal->list[wal->count].name = name;
	wal->count++;
}

void
parity_engine_init_whitelist(struct parity_wa_list *wal,
	const struct parity_engine *e, int graphics_ver)
{
	wal->count = 0u;
	wal->overflow = 0u;
	wal->name = "whitelist";

	if (graphics_ver != 12)
		return;   /* only tgl_whitelist_build is ported */

	/* allow_read_ctx_timestamp(): non-render engines only. */
	if (e->class != PARITY_RENDER_CLASS) {
		whitelist_reg_ext(wal, RING_CTX_TIMESTAMP(e->mmio_base),
			RING_FORCE_TO_NONPRIV_ACCESS_RD, "ctx timestamp RD");
		return;
	}

	/*
	 * WaAllowPMDepthAndInvocationCountAccessFromUMD / Wa_1408556865: four
	 * consecutive registers, hence RANGE_4.
	 */
	whitelist_reg_ext(wal, PS_INVOCATION_COUNT,
		RING_FORCE_TO_NONPRIV_ACCESS_RD | RING_FORCE_TO_NONPRIV_RANGE_4,
		"Wa_1408556865 (PS_INVOCATION_COUNT x4)");

	/* Wa_1808121037 / Wa_1508744258 */
	whitelist_reg_ext(wal, GEN7_COMMON_SLICE_CHICKEN1, 0u, "Wa_1508744258");

	/* Wa_1806527549 */
	whitelist_reg_ext(wal, HIZ_CHICKEN, 0u, "Wa_1806527549 (whitelist)");

	/* Required by the recommended tuning setting (not a workaround). */
	whitelist_reg_ext(wal, GEN11_COMMON_SLICE_CHICKEN3, 0u, "tuning");
}

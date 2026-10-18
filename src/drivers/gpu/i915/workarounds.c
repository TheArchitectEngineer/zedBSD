/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * GT, engine and context workarounds, the register whitelist, MOCS and PAT
 * (see workarounds.h).
 *
 * The lists hold only the arms Alder Lake-P takes in the Linux 6.8.12
 * reference, and the arms of the Gen9 parts: Skylake, Kaby
 * Lake, Coffee Lake and Comet Lake.  The other platforms' arms are absent
 * rather than built and skipped.  Every register access goes through the held accessors: the
 * device start holds every GT forcewake domain across the GT initialization
 * and resume that call these functions.
 */

#include "i915.h"
#include "workarounds.h"
#include "gt-power.h"
#include "mmio.h"
#include "device-info.h"

#include <kern/klog.h>

#include <stddef.h>
#include <stdint.h>

#include "intel/gt-regs.h"
#include "intel/mocs.h"
#include "intel/workarounds.h"

/* How many PAT entries the Gen12 private PAT has. */
#define I915_PAT_ENTRIES	8U

/*
 * One row of a platform MOCS table: the entry index and its two values.
 */
struct i915_mocs_row {
	unsigned idx;
	uint32_t control;
	uint16_t l3cc;
};

/*
 * The Alder Lake-P MOCS table.
 *
 * Alder Lake-P takes the "graphics version 12 or later" arm of Linux
 * get_mocs_settings(), which is gen12_mocs_table, not tgl_mocs_table (that
 * arm is Tiger Lake and Rocket Lake only).  The table never changes.
 */
static const struct i915_mocs_row i915_gen12_mocs_table[] = {
#include "intel/mocs-table.inc"
};

/*
 * The Gen9 MOCS table (skl_mocs_table), which every engine is programmed
 * with on its own.  The table never changes.
 */
static const struct i915_mocs_row i915_gen9_mocs_table[] = {
#include "intel/mocs-table-gen9.inc"
};

/*
 * The Gen12 private PAT: the memory type of every PAT index.
 *
 * Tiger Lake and later do not support the LLC or age settings, so each
 * entry carries only a memory type.  The table never changes.
 */
static const uint32_t i915_tgl_private_ppat[I915_PAT_ENTRIES] = {
	GEN8_PPAT_WB,
	GEN8_PPAT_WC,
	GEN8_PPAT_WT,
	GEN8_PPAT_UC,
	GEN8_PPAT_WB,
	GEN8_PPAT_WB,
	GEN8_PPAT_WB,
	GEN8_PPAT_WB
};

static void i915_wa_add_entry(struct i915_wa_list *wal, uint32_t reg, uint32_t clr, uint32_t set, uint32_t read_mask, int kind, int is_mcr, const char *name);
static void i915_wa_list_reset(struct i915_wa_list *wal, const char *name);
static void i915_wa_apply_result_reset(struct i915_wa_apply_result *result);
static void i915_icl_wa_init_mcr(struct i915_wa_list *wal, const struct i915_gt_info *gt);
static void i915_wa_14011060649(struct i915_wa_list *wal, const struct i915_gt_info *gt);
static void i915_whitelist_reg_ext(struct i915_wa_list *wal, uint32_t reg, uint32_t flags, const char *name);
static void i915_gen9_wa_init_mcr(struct i915_wa_list *wal, const struct i915_gt_info *gt);
static void i915_gen9_engine_workarounds(struct i915_wa_list *wal, const struct i915_gt_info *gt, const struct i915_engine_info *engine);
static void i915_gen9_ctx_workarounds(struct i915_wa_list *wal, const struct i915_gt_info *gt, const struct i915_engine_info *engine);
static void i915_gen9_whitelist(struct i915_wa_list *wal, const struct i915_gt_info *gt, const struct i915_engine_info *engine);
static void i915_gen9_gtt_write_workarounds(struct i915_mmio *mmio);
static void i915_mocs_fill(struct i915_mocs *table, const struct i915_mocs_row *rows, unsigned row_count);

/*
 * Adds a workaround that sets bits and verifies them.
 *
 * Linux wa_write_or().
 */
void
drv_i915_wa_write_or(
	struct i915_wa_list *wal,
	uint32_t reg,
	uint32_t set,
	int is_mcr,
	const char *name)
{
	/* Clears and verifies exactly the bits it sets. */
	i915_wa_add_entry(wal, reg, set, set, set, I915_WA_PLAIN, is_mcr, name);
}

/*
 * Adds a workaround that clears a field and sets a value inside it.
 *
 * Linux wa_write_clr_set().
 */
void
drv_i915_wa_write_clr_set(
	struct i915_wa_list *wal,
	uint32_t reg,
	uint32_t clr,
	uint32_t set,
	int is_mcr,
	const char *name)
{
	/* Verifies both the cleared field and the set value. */
	i915_wa_add_entry(wal, reg, clr, set, clr | set, I915_WA_PLAIN, is_mcr, name);
}

/*
 * Adds a workaround that enables bits of a masked register.
 *
 * Linux wa_masked_en().  The entry clears nothing and sets the bits with
 * their mask in the upper half; the verify pass compares the low half.
 */
void
drv_i915_wa_masked_en(
	struct i915_wa_list *wal,
	uint32_t reg,
	uint32_t bits,
	int is_mcr,
	const char *name)
{
	/* Writes the bits under their own mask and verifies them. */
	i915_wa_add_entry(wal, reg, 0U, (bits << 16) | bits, bits, I915_WA_MASKED, is_mcr, name);
}

/*
 * Adds a workaround that sets a field of a masked register.
 *
 * Linux wa_masked_field_set().
 */
void
drv_i915_wa_masked_field_set(
	struct i915_wa_list *wal,
	uint32_t reg,
	uint32_t mask,
	uint32_t value,
	int is_mcr,
	const char *name)
{
	/* Writes the value under the field mask and verifies the field. */
	i915_wa_add_entry(wal, reg, 0U, (mask << 16) | value, mask, I915_WA_MASKED, is_mcr, name);
}

/*
 * Adds a workaround whose readback is not trusted.
 *
 * Linux wa_add() with a read mask of 0: the register is written, and the
 * verify pass does not compare it.
 */
void
drv_i915_wa_add_no_verify(
	struct i915_wa_list *wal,
	uint32_t reg,
	uint32_t clr,
	uint32_t set,
	int is_mcr,
	const char *name)
{
	/* Writes the value and leaves nothing to compare. */
	i915_wa_add_entry(wal, reg, clr, set, 0U, I915_WA_NO_VERIFY, is_mcr, name);
}

/*
 * Builds the Alder Lake-P GT workaround list.
 *
 * Linux intel_gt_init_workarounds(): gt_tuning_settings() adds nothing for
 * Alder Lake-P (it is MTL, PVC and DG2 only), then gen12_gt_workarounds_init().
 * The list needs the GT state: the MCR steering picks the lowest subslice
 * that is not fused off, and Wa_14011060649 walks the video decode engines.
 */
void
drv_i915_gt_init_workarounds_adlp(
	struct i915_wa_list *wal,
	const struct i915_gt_info *gt)
{
	/* Starts the GT list empty. */
	i915_wa_list_reset(wal, "gt_wa");

	/* Steers multicast reads at a subslice that is powered. */
	i915_icl_wa_init_mcr(wal, gt);

	/* Wa_14011060649:tgl,rkl,dg1,adl-s,adl-p */
	i915_wa_14011060649(wal, gt);

	/* Wa_14011059788:tgl,rkl,adl-s,dg1,adl-p */
	drv_i915_wa_write_or(wal, GEN10_DFR_RATIO_EN_AND_CHICKEN, DFR_DISABLE, 1, "Wa_14011059788");

	/*
	 * Wa_14015795083.  Linux calls wa_add(wal, GEN7_MISCCPCTL,
	 * GEN12_DOP_CLOCK_GATE_RENDER_ENABLE, 0, 0, false), whose arguments are
	 * (register, clear, set, read mask, masked): it clears the bit, and the
	 * readback is not verified because firmware may have locked the
	 * register.
	 */
	drv_i915_wa_add_no_verify(wal, GEN7_MISCCPCTL, GEN12_DOP_CLOCK_GATE_RENDER_ENABLE, 0U, 0, "Wa_14015795083");
}

/*
 * Builds the Gen9 GT workaround list.
 *
 * Linux intel_gt_init_workarounds() for Skylake, Kaby Lake, Coffee Lake and
 * Comet Lake: gen9_gt_workarounds_init() with the steering at the first
 * enabled slice and subslice, then the platform's own entries.  Every part
 * has an LLC.  The stepping is the PCI revision.
 */
void
drv_i915_gt_init_workarounds_gen9(
	struct i915_wa_list *wal,
	const struct i915_gt_info *gt)
{
	/* Starts the GT list empty. */
	i915_wa_list_reset(wal, "gt_wa");

	/* Steers slice-specific reads at a powered slice and subslice (WaProgramMgsrForCorrectSliceSpecificMmioReads). */
	i915_gen9_wa_init_mcr(wal, gt);

	/* WaDisableKillLogic:skl,kbl */
	if (gt->platform == I915_PLATFORM_SKYLAKE || gt->platform == I915_PLATFORM_KABYLAKE)
		drv_i915_wa_write_or(wal, GAM_ECOCHK, ECOCHK_DIS_TLB, 0, "WaDisableKillLogic");

	/* WaCompressedResourceSamplerPbeMediaNewHashMode:skl,kbl, which matches the display engine. */
	drv_i915_wa_write_or(wal, MMCD_MISC_CTRL, MMCD_PCLA | MMCD_HOTSPOT_EN, 0, "WaCompressedResourceSamplerPbeMediaNewHashMode");

	/* WaDisableHDCInvalidation:skl,kbl,cfl */
	drv_i915_wa_write_or(wal, GAM_ECOCHK, BDW_DISABLE_HDC_INVALIDATION, 0, "WaDisableHDCInvalidation");

	/* WaDisableDynamicCreditSharing:kbl, before stepping C0 (revision 2). */
	if (gt->platform == I915_PLATFORM_KABYLAKE && gt->revision < 2U)
		drv_i915_wa_write_or(wal, GAMT_CHKN_BIT_REG, GAMT_CHKN_DISABLE_DYNAMIC_CREDIT_SHARING, 0, "WaDisableDynamicCreditSharing");

	/* WaDisableGafsUnitClkGating:skl,kbl,cfl */
	drv_i915_wa_write_or(wal, GEN7_UCGCTL4, GEN8_EU_GAUNIT_CLOCK_GATE_DISABLE, 0, "WaDisableGafsUnitClkGating");

	/*
	 * WaInPlaceDecompressionHang: every Kaby Lake, Coffee Lake and Comet
	 * Lake, and Skylake before stepping H0 (revision 7).
	 */
	if (gt->platform != I915_PLATFORM_SKYLAKE) {
		drv_i915_wa_write_or(wal, GEN9_GAMT_ECO_REG_RW_IA, GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS, 0, "WaInPlaceDecompressionHang");
	} else if (gt->revision < 7U) {
		drv_i915_wa_write_or(wal, GEN9_GAMT_ECO_REG_RW_IA, GAMT_ECO_ENABLE_IN_PLACE_DECOMPRESS, 0, "WaInPlaceDecompressionHang");
	}
}

/*
 * Builds the workaround list of one engine.
 *
 * Linux intel_engine_init_workarounds(): engine_fake_wa_init(), then
 * rcs_engine_wa_init() for the render engine.
 */
void
drv_i915_engine_init_workarounds(
	struct i915_wa_list *wal,
	const struct i915_engine_info *engine,
	int graphics_ver,
	unsigned mocs_uc_index)
{
	/* Starts the engine list empty, named after the engine. */
	i915_wa_list_reset(wal, engine->name);

	/* Graphics versions before 4 have no engine workarounds. */
	if (graphics_ver < 4)
		return;

	/*
	 * engine_fake_wa_init(): RING_CMD_CCTL names the default MOCS entry the
	 * command streamer uses.  Alder Lake-P has no L3 CCS read, so both
	 * halves take the uncached index.
	 */
	drv_i915_wa_masked_field_set(
		wal,
		RING_CMD_CCTL(engine->mmio_base),
		CMD_CCTL_MOCS_MASK,
		CMD_CCTL_MOCS_OVERRIDE(mocs_uc_index, mocs_uc_index),
		0,
		"fake: RING_CMD_CCTL MOCS");

	/*
	 * general_render_compute_wa_init() and ccs_engine_wa_mode() are DG2,
	 * PVC and MTL only, and xcs_engine_wa_init() (the KBL semaphore poll
	 * and the XeHP fast-colour blit) does not apply either, so a non-render
	 * engine gets nothing more.
	 */
	if (engine->class != I915_RENDER_CLASS)
		return;

	/* Wa_1606700617 / Wa_22010271021 / Wa_14010826681 */
	drv_i915_wa_masked_en(wal, GEN9_CS_DEBUG_MODE1, FF_DOP_CLOCK_GATE_DISABLE, 0, "Wa_1606700617");

	/* Wa_1606931601 */
	drv_i915_wa_masked_en(wal, GEN8_ROW_CHICKEN2, GEN12_DISABLE_EARLY_READ, 1, "Wa_1606931601");

	/* Wa_14010919138 (and the tgl/dg1 aliases) */
	drv_i915_wa_write_or(wal, GEN7_FF_THREAD_MODE, GEN12_FF_TESSELATION_DOP_GATE_DISABLE, 0, "Wa_14010919138");

	/* Wa_1406941453 */
	drv_i915_wa_masked_en(wal, GEN10_SAMPLER_MODE, ENABLE_SMALLPL, 1, "Wa_1406941453");

	/* Wa_1409804808, which merges into the GEN8_ROW_CHICKEN2 entry above. */
	drv_i915_wa_masked_en(wal, GEN8_ROW_CHICKEN2, GEN12_PUSH_CONST_DEREF_HOLD_DIS, 1, "Wa_1409804808");

	/* Wa_14010229206 */
	drv_i915_wa_masked_en(wal, GEN9_ROW_CHICKEN4, GEN12_DISABLE_TDL_PUSH, 1, "Wa_14010229206");

	/* Wa_1607297627 */
	drv_i915_wa_masked_en(
		wal,
		RING_PSMI_CTL(engine->mmio_base),
		GEN12_WAIT_FOR_EVENT_POWER_DOWN_DISABLE | GEN8_RC_SEMA_IDLE_MSG_DISABLE,
		0,
		"Wa_1607297627");

	/* Graphics version 9 and later: per-context preemption control. */
	if (graphics_ver >= 9) {
		drv_i915_wa_masked_en(
			wal,
			GEN7_FF_SLICE_CS_CHICKEN1,
			GEN9_FFSC_PERCTX_PREEMPT_CTRL,
			0,
			"perctx preempt ctrl");
	}
}

/*
 * Builds the context workaround list of one engine.
 *
 * Linux intel_engine_init_ctx_wa(): the workarounds applied inside every
 * context by MI_LOAD_REGISTER_IMM.
 */
void
drv_i915_engine_init_ctx_wa(
	struct i915_wa_list *wal,
	const struct i915_engine_info *engine,
	int graphics_ver,
	unsigned mocs_uc_index)
{
	/* Starts the context list empty. */
	i915_wa_list_reset(wal, "ctx_wa");

	/*
	 * gen12_ctx_gt_fake_wa_init() applies to every engine.  Its
	 * fakewa_disable_nestedbb_mode half needs IP version 12.55 or later
	 * (Alder Lake-P is 12.10, so it is not taken); gen12_ctx_gt_mocs_init()
	 * touches BLIT_CCTL, and only for the copy engine.
	 */
	if (graphics_ver >= 12 && engine->class == I915_COPY_ENGINE_CLASS) {
		drv_i915_wa_write_clr_set(
			wal,
			BLIT_CCTL(engine->mmio_base),
			BLIT_CCTL_MASK,
			BLIT_CCTL_MOCS(mocs_uc_index, mocs_uc_index),
			0,
			"gen12_ctx_gt_mocs_init: BLIT_CCTL");
	}

	/* gen12_ctx_workarounds_init() is for the render engine only. */
	if (engine->class != I915_RENDER_CLASS)
		return;

	/* Wa_1409142259 and the other nine aliases */
	drv_i915_wa_masked_en(wal, GEN11_COMMON_SLICE_CHICKEN3, GEN12_DISABLE_CPS_AWARE_COLOR_PIPE, 0, "Wa_1409142259");

	/* WaDisableGPGPUMidThreadPreemption:gen12 */
	drv_i915_wa_masked_field_set(
		wal,
		GEN8_CS_CHICKEN1,
		GEN9_PREEMPT_GPGPU_LEVEL_MASK,
		GEN9_PREEMPT_GPGPU_THREAD_GROUP_LEVEL,
		0,
		"WaDisableGPGPUMidThreadPreemption");

	/*
	 * Wa_16011163337 (GS_TIMER) and TDS_TIMER.  FF_MODE2 reads back the
	 * wrong value from the CPU (Wa_1608008084), so the clear mask is all
	 * ones -- the wanted value is written outright -- and the readback is
	 * not verified.
	 */
	drv_i915_wa_add_no_verify(
		wal,
		GEN12_FF_MODE2,
		0xffffffffU,
		FF_MODE2_TDS_TIMER_128 | FF_MODE2_GS_TIMER_224,
		0,
		"Wa_16011163337 (FF_MODE2)");

	/* Wa_1806527549, which Alder Lake-P takes because it is not DG1. */
	drv_i915_wa_masked_en(wal, HIZ_CHICKEN, HZ_DEPTH_TEST_LE_GE_OPT_DISABLE, 0, "Wa_1806527549");

	/* Wa_1606376872, which Alder Lake-P takes because it is not DG1. */
	drv_i915_wa_masked_en(wal, COMMON_SLICE_CHICKEN4, DISABLE_TDC_LOAD_BALANCING_CALC, 0, "Wa_1606376872");
}

/*
 * Builds the register whitelist of one engine.
 *
 * Linux intel_engine_init_whitelist() -> tgl_whitelist_build(), the only
 * builder this driver has.
 */
void
drv_i915_engine_init_whitelist(
	struct i915_wa_list *wal,
	const struct i915_engine_info *engine,
	int graphics_ver)
{
	/* Starts the whitelist empty. */
	i915_wa_list_reset(wal, "whitelist");

	/* Only the graphics version 12 whitelist is built. */
	if (graphics_ver != 12)
		return;

	/* allow_read_ctx_timestamp(): a non-render engine whitelists only that. */
	if (engine->class != I915_RENDER_CLASS) {
		i915_whitelist_reg_ext(wal, RING_CTX_TIMESTAMP(engine->mmio_base), RING_FORCE_TO_NONPRIV_ACCESS_RD, "ctx timestamp RD");
		return;
	}

	/*
	 * WaAllowPMDepthAndInvocationCountAccessFromUMD / Wa_1408556865: four
	 * consecutive registers, hence RANGE_4.
	 */
	i915_whitelist_reg_ext(
		wal,
		PS_INVOCATION_COUNT,
		RING_FORCE_TO_NONPRIV_ACCESS_RD | RING_FORCE_TO_NONPRIV_RANGE_4,
		"Wa_1408556865 (PS_INVOCATION_COUNT x4)");

	/* Wa_1808121037 / Wa_1508744258 */
	i915_whitelist_reg_ext(wal, GEN7_COMMON_SLICE_CHICKEN1, 0U, "Wa_1508744258");

	/* Wa_1806527549 */
	i915_whitelist_reg_ext(wal, HIZ_CHICKEN, 0U, "Wa_1806527549 (whitelist)");

	/* Required by the recommended tuning setting, not by a workaround. */
	i915_whitelist_reg_ext(wal, GEN11_COMMON_SLICE_CHICKEN3, 0U, "tuning");
}

/*
 * Programs one workaround list into the hardware, and reads it back.
 *
 * Linux wa_list_apply().  A masked entry is always written, since its value
 * carries its own mask.  A plain entry is read, modified and written, and is
 * written only when the value changes or when there is nothing to clear.
 * With verify set, every entry that can be verified is read back and
 * compared; Linux does this only under CONFIG_DRM_I915_DEBUG_GEM, and here
 * it always runs as a diagnostic that changes no hardware state.
 */
void
drv_i915_wa_list_apply(
	const struct i915_wa_list *wal,
	struct i915_mmio *mmio,
	int verify,
	struct i915_wa_apply_result *result)
{
	const struct i915_wa *entry;
	const char *entry_name;
	uint32_t old_value;
	uint32_t new_value;
	uint32_t readback;
	uint32_t expected;
	unsigned index;
	int write;

	/* Starts the counts from zero. */
	if (result != NULL)
		i915_wa_apply_result_reset(result);

	/* Applies, and verifies, the entries in list order. */
	for (index = 0U; index < wal->count; index++) {
		entry = &wal->list[index];

		/*
		 * Computes the value to write: a masked entry is its own value, a
		 * plain one merges into what the register holds.
		 */
		old_value = 0U;
		if (entry->kind == I915_WA_MASKED) {
			new_value = entry->set;
		} else {
			/* Only an entry that clears something needs the old value. */
			if (entry->clr != 0U)
				old_value = drv_i915_read32(mmio, entry->reg);

			new_value = (old_value & ~entry->clr) | entry->set;
		}

		/* Decides whether the register has to be written. */
		write = 0;
		if (entry->kind == I915_WA_MASKED) {
			/* A masked value is always written. */
			write = 1;
		} else if (new_value != old_value) {
			/* The value changes. */
			write = 1;
		} else if (entry->clr == 0U) {
			/* Nothing was read, so nothing proves the write unnecessary. */
			write = 1;
		}

		/* Writes the register, or counts it as already right. */
		if (write != 0) {
			drv_i915_write32(mmio, entry->reg, new_value);

			/* Counts the write. */
			if (result != NULL)
				result->written++;
		} else if (result != NULL) {
			result->skipped_unchanged++;
		}

		/* Without verification the entry is done. */
		if (verify == 0)
			continue;

		/* An entry with a read mask of 0 has a readback that is not trusted. */
		if (entry->read_mask == 0U) {
			/* Counts the entry as one that could not be verified. */
			if (result != NULL)
				result->not_verifiable++;

			continue;
		}

		/* Reads the register back; a masked entry is compared in its low half. */
		readback = drv_i915_read32(mmio, entry->reg);
		expected = entry->set;
		if (entry->kind == I915_WA_MASKED)
			expected = entry->set & 0xffffU;

		/* Reports a workaround the hardware did not keep. */
		if ((readback & entry->read_mask) != (expected & entry->read_mask)) {
			/* Names the entry, or marks it unnamed. */
			entry_name = "?";
			if (entry->name != NULL)
				entry_name = entry->name;

			kern_logf("i915: WA MISMATCH %s reg=0x%05x expected 0x%08x got 0x%08x (mask 0x%08x)\n",
			    entry_name,
			    entry->reg,
			    expected,
			    readback,
			    entry->read_mask);

			/* Counts the lost workaround. */
			if (result != NULL)
				result->mismatched++;
		} else if (result != NULL) {
			result->verified++;
		}
	}
}

/*
 * Programs the register whitelist of one engine.
 *
 * Linux intel_engine_apply_whitelist(): each entry fills one
 * RING_FORCE_TO_NONPRIV slot, and every remaining slot is pointed at
 * RING_NOPID so no garbage stays whitelisted.
 */
void
drv_i915_engine_apply_whitelist(
	const struct i915_wa_list *wal,
	const struct i915_engine_info *engine,
	struct i915_mmio *mmio)
{
	unsigned slot;

	/* An empty whitelist leaves the slots as they are. */
	if (wal->count == 0U)
		return;

	/* Writes each whitelisted address with its access flags into a slot. */
	for (slot = 0U; slot < wal->count && slot < RING_MAX_NONPRIV_SLOTS; slot++) {
		drv_i915_write32(
			mmio,
			RING_FORCE_TO_NONPRIV(engine->mmio_base, slot),
			wal->list[slot].reg | wal->list[slot].set);
	}

	/* Points the slots that are left at a harmless register. */
	for (;
	     slot < RING_MAX_NONPRIV_SLOTS;
	     slot++)
		drv_i915_write32(mmio, RING_FORCE_TO_NONPRIV(engine->mmio_base, slot), RING_NOPID(engine->mmio_base));
}

/*
 * Logs one workaround list in manifest form.
 *
 * The manifest is compared line by line with a dump taken under Linux.
 */
void
drv_i915_wa_list_dump(
	const struct i915_wa_list *wal,
	const char *what)
{
	const struct i915_wa *entry;
	const char *list_name;
	const char *overflow_note;
	const char *entry_name;
	unsigned index;

	/* Names the list and says whether entries were lost to overflow. */
	list_name = "?";
	if (wal->name != NULL)
		list_name = wal->name;
	overflow_note = "";
	if (wal->overflow != 0U)
		overflow_note = " (OVERFLOW)";

	kern_logf("i915: WA-MANIFEST %s '%s': %u entries%s\n",
	    what,
	    list_name,
	    wal->count,
	    overflow_note);

	/* Logs every entry with its masks, kind and name. */
	for (index = 0U; index < wal->count; index++) {
		entry = &wal->list[index];

		/* Names the entry, or leaves the name empty. */
		entry_name = "";
		if (entry->name != NULL)
			entry_name = entry->name;

		kern_logf("i915: WA %s reg=0x%05x clr=0x%08x set=0x%08x rmask=0x%08x kind=%d mcr=%d %s\n",
		    what,
		    entry->reg,
		    entry->clr,
		    entry->set,
		    entry->read_mask,
		    entry->kind,
		    entry->is_mcr,
		    entry_name);
	}
}

/*
 * Builds the MOCS table of the platform.
 *
 * Linux get_mocs_settings() for Alder Lake-P: gen12_mocs_table with the
 * uncached index 3 and the unused-entries index 2.  Every index the table
 * does not define takes the values of the unused-entries index.
 */
void
drv_i915_get_mocs_settings(
	struct i915_mocs *table,
	int graphics_ver)
{
	unsigned row_count;
	unsigned index;

	/* Starts from an empty, invalid table. */
	for (index = 0U; index < (unsigned)I915_MOCS_ENTRIES; index++) {
		table->control[index] = 0U;
		table->l3cc[index] = 0U;
	}

	table->n_entries = 0U;
	table->uc_index = 0U;
	table->unused_entries_index = 0U;
	table->valid = 0;
	table->engine_mocs = 0;

	/*
	 * Gen9 takes skl_mocs_table with the uncached index 0 and the
	 * unused-entries index 1 (I915_MOCS_PTE), one table per engine.
	 */
	if (graphics_ver == 9) {
		table->n_entries = GEN9_NUM_MOCS_ENTRIES;
		table->uc_index = GEN9_MOCS_UC_INDEX;
		table->unused_entries_index = GEN9_MOCS_UNUSED_ENTRIES_INDEX;
		table->engine_mocs = 1;
		row_count = (unsigned)(sizeof(i915_gen9_mocs_table) / sizeof(i915_gen9_mocs_table[0]));
		i915_mocs_fill(table, i915_gen9_mocs_table, row_count);
		return;
	}

	/* Only the graphics version 9 and 12 tables exist in this driver. */
	if (graphics_ver < 12) {
		kern_logf("i915: MOCS: only the gen9 and gen12 tables are ported\n");
		return;
	}

	/* Takes the size and the two special indices of the Gen12 table. */
	table->n_entries = GEN9_NUM_MOCS_ENTRIES;
	table->uc_index = GEN12_MOCS_UC_INDEX;
	table->unused_entries_index = GEN12_MOCS_UNUSED_ENTRIES_INDEX;

	/* Fills the table from the Gen12 rows. */
	row_count = (unsigned)(sizeof(i915_gen12_mocs_table) / sizeof(i915_gen12_mocs_table[0]));
	i915_mocs_fill(table, i915_gen12_mocs_table, row_count);
}

/*
 * Programs the global MOCS table and the L3 cache control table.
 *
 * Linux intel_mocs_init(): Alder Lake-P has global MOCS, so
 * __init_mocs_table() writes the GLOBAL_MOCS registers, followed by
 * init_l3cc_table().
 */
void
drv_i915_mocs_init(
	const struct i915_mocs *table,
	struct i915_mmio *mmio,
	unsigned *global_writes,
	unsigned *l3cc_writes)
{
	unsigned global_count;
	unsigned l3cc_count;
	unsigned index;

	/* Nothing has been written yet. */
	global_count = 0U;
	l3cc_count = 0U;

	/* A table that was never built is not programmed. */
	if (table->valid == 0)
		return;

	/* Writes every control value into the global MOCS registers; Gen9 has per-engine tables instead. */
	if (table->engine_mocs == 0) {
		for (index = 0U; index < table->n_entries; index++) {
			drv_i915_write32(mmio, GEN12_GLOBAL_MOCS(index), table->control[index]);
			global_count++;
		}
	}

	/* Writes the L3 cache control pairs. */
	drv_i915_init_l3cc_table(table, mmio, &l3cc_count);

	/* Reports how many registers of each kind were written. */
	if (global_writes != NULL)
		*global_writes = global_count;
	if (l3cc_writes != NULL)
		*l3cc_writes = l3cc_count;
}

/*
 * Programs the L3 cache control table.
 *
 * Linux init_l3cc_table(): two 16-bit entries are packed per register.  It
 * runs once for the GT and again when the render engine resumes.
 */
void
drv_i915_init_l3cc_table(
	const struct i915_mocs *table,
	struct i915_mmio *mmio,
	unsigned *writes)
{
	uint32_t pair;
	unsigned write_count;
	unsigned index;

	/* Nothing has been written yet. */
	write_count = 0U;

	/* A table that was never built is not programmed. */
	if (table->valid == 0)
		return;

	/* Writes each pair of entries into one LNCFCMOCS register. */
	for (index = 0U; index < (table->n_entries + 1U) / 2U; index++) {
		pair = (uint32_t)table->l3cc[2U * index] | ((uint32_t)table->l3cc[2U * index + 1U] << 16);
		drv_i915_write32(mmio, GEN9_LNCFCMOCS(index), pair);
		write_count++;
	}

	/* Reports how many registers were written. */
	if (writes != NULL)
		*writes = write_count;
}

/*
 * Programs one engine's own MOCS table (intel_mocs_init_engine(), Gen9).
 *
 * A platform with global MOCS has nothing to write per engine.  The caller
 * holds every forcewake domain.
 */
void
drv_i915_mocs_init_engine(
	const struct i915_mocs *table,
	const struct i915_engine_info *engine,
	struct i915_mmio *mmio,
	unsigned *writes)
{
	uint32_t base;
	unsigned index;
	unsigned count;

	/* Nothing has been written yet. */
	count = 0U;
	if (writes != NULL)
		*writes = 0U;

	/* A table that was never built, or a global one, is not written per engine. */
	if (table->valid == 0 || table->engine_mocs == 0)
		return;

	/* Picks the engine's table (mocs_offset()). */
	switch (engine->id) {
	case I915_RCS0:
		base = GEN9_RCS0_MOCS0;
		break;
	case I915_BCS0:
		base = GEN9_BCS0_MOCS0;
		break;
	case I915_VCS0:
		base = GEN9_VCS0_MOCS0;
		break;
	case I915_VECS0:
		base = GEN9_VECS0_MOCS0;
		break;
	default:
		/* An engine without a Gen9 table is left alone. */
		return;
	}

	/* Writes every control value into the engine's registers. */
	for (index = 0U; index < table->n_entries; index++) {
		drv_i915_write32(mmio, base + index * 4U, table->control[index]);
		count++;
	}

	/* Reports how many registers were written. */
	if (writes != NULL)
		*writes = count;
}

/*
 * Programs the Gen8 private PAT that Gen9 uses (bdw_setup_private_ppat()).
 *
 * Index 0 is write-back in the LLC, 1 write-combining, 2 write-back with the
 * eLLC override, 3 uncached, 4 to 7 write-back in LLC and eLLC with the four
 * ages.  The eight one-byte entries fill the low and high registers.
 */
void
drv_i915_bdw_setup_private_ppat(
	struct i915_mmio *mmio,
	unsigned *writes)
{
	uint32_t low;
	uint32_t high;

	/* Entries 0 to 3. */
	low = (GEN8_PPAT_WB | GEN8_PPAT_LLC) << 0;
	low |= (GEN8_PPAT_WC | GEN8_PPAT_LLCELLC) << 8;
	low |= (GEN8_PPAT_WB | GEN8_PPAT_ELLC_OVERRIDE) << 16;
	low |= GEN8_PPAT_UC << 24;

	/* Entries 4 to 7. */
	high = (GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(0)) << 0;
	high |= (GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(1)) << 8;
	high |= (GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(2)) << 16;
	high |= (GEN8_PPAT_WB | GEN8_PPAT_LLCELLC | GEN8_PPAT_AGE(3)) << 24;

	/* Writes both halves. */
	drv_i915_write32(mmio, GEN8_PRIVATE_PAT_LO, low);
	drv_i915_write32(mmio, GEN8_PRIVATE_PAT_HI, high);

	/* Reports that every entry was written. */
	if (writes != NULL)
		*writes = I915_PAT_ENTRIES;
}

/*
 * Programs the Gen12 private PAT.
 *
 * Linux setup_private_pat() -> tgl_setup_private_ppat().  It belongs with
 * the page tables and moves to ppgtt.c when those are integrated.
 */
void
drv_i915_tgl_setup_private_ppat(
	struct i915_mmio *mmio,
	unsigned *writes)
{
	unsigned index;

	/* Writes the memory type of every PAT index. */
	for (index = 0U; index < I915_PAT_ENTRIES; index++)
		drv_i915_write32(mmio, GEN12_PAT_INDEX(index), i915_tgl_private_ppat[index]);

	/* Reports that every entry was written. */
	if (writes != NULL)
		*writes = I915_PAT_ENTRIES;
}

/*
 * Builds every workaround list, the MOCS table, and the RC6 and RPS state.
 *
 * Nothing is programmed yet; the only hardware accesses are the RC6
 * disable and the frequency and PCODE reads that RPS needs.
 */
void
drv_i915_gt_init_tables(
	struct i915_gt_init *gt_init,
	const struct i915_gt_info *gt,
	int graphics_ver,
	struct mutex *sb_lock,
	struct i915_mmio *mmio)
{
	unsigned index;

	/*
	 * Builds the MOCS table first: Linux intel_set_mocs_index() runs before
	 * the engines are set up, because engine_fake_wa_init() needs the
	 * uncached index.
	 */
	drv_i915_get_mocs_settings(&gt_init->mocs, graphics_ver);

	/* Builds the GT workaround list of the graphics version. */
	if (graphics_ver == 9) {
		drv_i915_gt_init_workarounds_gen9(&gt_init->gt_wa, gt);
	} else {
		drv_i915_gt_init_workarounds_adlp(&gt_init->gt_wa, gt);
	}

	/* Builds the engine, whitelist and context lists of every engine. */
	for (index = 0U; index < gt->num_engines && index < (unsigned)I915_WA_ENGINES; index++) {
		/* A Gen9 engine takes the Gen9 lists, which depend on the platform and stepping. */
		if (graphics_ver == 9) {
			i915_gen9_engine_workarounds(&gt_init->engine_wa[index], gt, &gt->engines[index]);
			i915_gen9_whitelist(&gt_init->whitelist[index], gt, &gt->engines[index]);
			i915_gen9_ctx_workarounds(&gt_init->ctx_wa[index], gt, &gt->engines[index]);
			continue;
		}

		drv_i915_engine_init_workarounds(&gt_init->engine_wa[index], &gt->engines[index], graphics_ver, gt_init->mocs.uc_index);
		drv_i915_engine_init_whitelist(&gt_init->whitelist[index], &gt->engines[index], graphics_ver);
		drv_i915_engine_init_ctx_wa(&gt_init->ctx_wa[index], &gt->engines[index], graphics_ver, gt_init->mocs.uc_index);
	}

	/* Prepares RC6, then RPS, as Linux intel_gt_pm_init() does. */
	drv_i915_rc6_init(&gt_init->rc6, mmio);
	drv_i915_rps_init(&gt_init->rps, sb_lock, mmio);

	/*
	 * A Gen9 GT reads RP1 from the capabilities (it has no
	 * GEN10_FREQ_INFO_REC) and keeps its frequency fixed in its first
	 * version.
	 */
	if (graphics_ver == 9)
		drv_i915_rps_init_gen9(&gt_init->rps, mmio);

	/* The tables can now be programmed. */
	gt_init->tables_built = 1;
}

/*
 * Programs the PAT, the GT workarounds and the MOCS tables.
 *
 * Linux intel_gt_init_hw() for Alder Lake-P, preceded by the PAT.  The
 * caller holds forcewake, as Linux holds FORCEWAKE_ALL across the whole of
 * intel_gt_init() and intel_gt_init_hw().
 */
void
drv_i915_gt_init_hw_core(
	struct i915_gt_init *gt_init,
	const struct i915_gt_info *gt,
	struct i915_mmio *mmio)
{
	unsigned pat_writes;

	/*
	 * Programs the PAT.  Linux setup_private_pat() runs in i915_gem_init(),
	 * before intel_gt_init(); it is kept at the head of the GT programming
	 * so it precedes everything that relies on a PAT index.  Gen9 has the
	 * Gen8 private PAT.
	 */
	pat_writes = 0U;
	if (gt->graphics_ver == 9) {
		drv_i915_bdw_setup_private_ppat(mmio, &pat_writes);
	} else {
		drv_i915_tgl_setup_private_ppat(mmio, &pat_writes);
	}

	gt_init->pat_programmed = 0;
	if (pat_writes == I915_PAT_ENTRIES)
		gt_init->pat_programmed = 1;

	/*
	 * Applies the GT workarounds and verifies them.  Linux verifies only
	 * under CONFIG_DRM_I915_DEBUG_GEM; here the readback always runs as a
	 * diagnostic that changes no hardware state.
	 */
	drv_i915_wa_list_apply(&gt_init->gt_wa, mmio, 1, &gt_init->gt_wa_applied);

	/* Programs the Gen9 GTT workarounds of i915_ppgtt_init_hw(); Gen12 has none. */
	if (gt->graphics_ver == 9)
		i915_gen9_gtt_write_workarounds(mmio);

	/*
	 * Programs the MOCS tables.  intel_gt_init_swizzling() and
	 * init_unused_rings() are gen3 and older only, and
	 * i915_ppgtt_init_hw()'s gtt_write_workarounds() has no gen12 arm, so
	 * nothing happens between the GT workarounds and MOCS.
	 * intel_uc_init_hw() is __uc_check_hw() with the GuC disabled, which
	 * does nothing.
	 */
	drv_i915_mocs_init(&gt_init->mocs, mmio, &gt_init->mocs_global_writes, &gt_init->mocs_l3cc_writes);

	/* The GT now holds its workarounds and MOCS. */
	gt_init->hw_programmed = 1;
}

/*
 * Applies the workarounds and the whitelist of one engine on resume.
 *
 * The first half of Linux intel_engine_resume(): apply the engine
 * workarounds, apply the whitelist, then the caller resumes the engine.
 */
void
drv_i915_engine_apply_resume_wa(
	struct i915_gt_init *gt_init,
	const struct i915_gt_info *gt,
	unsigned index,
	struct i915_mmio *mmio)
{
	/* An engine the GT does not have is left alone. */
	if (index >= gt->num_engines)
		return;

	/* An engine beyond the lists that were built is left alone. */
	if (index >= (unsigned)I915_WA_ENGINES)
		return;

	/* Applies and verifies the engine workarounds. */
	drv_i915_wa_list_apply(&gt_init->engine_wa[index], mmio, 1, &gt_init->engine_wa_applied[index]);

	/* Programs the engine's whitelist. */
	drv_i915_engine_apply_whitelist(&gt_init->whitelist[index], &gt->engines[index], mmio);

	/*
	 * Counts every non-privileged slot as written and the engine as
	 * resumed.  The slot count is added even for an empty whitelist, which
	 * writes nothing.
	 */
	gt_init->whitelist_writes += RING_MAX_NONPRIV_SLOTS;
	gt_init->engines_resumed++;
}

/* Adds one entry to a list, merging it into an entry for the same register. */
static void
i915_wa_add_entry(
	struct i915_wa_list *wal,
	uint32_t reg,
	uint32_t clr,
	uint32_t set,
	uint32_t read_mask,
	int kind,
	int is_mcr,
	const char *name)
{
	struct i915_wa *entry;
	unsigned index;

	/* Merges into an entry for the same register, as Linux _wa_add() does. */
	for (index = 0U; index < wal->count; index++) {
		entry = &wal->list[index];

		/* Another register is not merged into. */
		if (entry->reg != reg)
			continue;

		/* The same offset with the other multicast flag is another register. */
		if (entry->is_mcr != is_mcr)
			continue;

		/*
		 * Adds the new clear bits, replaces the cleared bits of the value
		 * with the new set bits, and widens the verified bits.  The merged
		 * entry keeps its first name.
		 */
		entry->clr |= clr;
		entry->set = (entry->set & ~clr) | set;
		entry->read_mask |= read_mask;

		/* An entry that cannot be verified makes the merged entry unverifiable. */
		if (kind == I915_WA_NO_VERIFY)
			entry->kind = I915_WA_NO_VERIFY;

		return;
	}

	/* Refuses, and counts, an entry that does not fit. */
	if (wal->count >= (unsigned)I915_WA_MAX) {
		wal->overflow++;
		return;
	}

	/* Appends the entry. */
	entry = &wal->list[wal->count];
	entry->reg = reg;
	entry->clr = clr;
	entry->set = set;
	entry->read_mask = read_mask;
	entry->kind = kind;
	entry->is_mcr = is_mcr;
	entry->name = name;
	wal->count++;
}

/* Empties a list and gives it its name. */
static void
i915_wa_list_reset(
	struct i915_wa_list *wal,
	const char *name)
{
	wal->count = 0U;
	wal->overflow = 0U;
	wal->name = name;
}

/* Zeroes the counts of an apply result. */
static void
i915_wa_apply_result_reset(
	struct i915_wa_apply_result *result)
{
	result->written = 0U;
	result->skipped_unchanged = 0U;
	result->verified = 0U;
	result->mismatched = 0U;
	result->not_verifiable = 0U;
}

/*
 * Steers multicast reads at the lowest subslice that is not fused off.
 *
 * Linux icl_wa_init_mcr().  With render power gating on, forcewake alone
 * powers only the minimum-configuration subslice, so steering at a higher
 * one reads back zeros or garbage.  The slice is always 0 on Alder Lake-P.
 */
static void
i915_icl_wa_init_mcr(
	struct i915_wa_list *wal,
	const struct i915_gt_info *gt)
{
	unsigned subslice;
	unsigned candidate;

	/* Finds the lowest enabled subslice; without GT state it is 0. */
	subslice = 0U;
	if (gt != NULL) {
		for (candidate = 0U; candidate < 16U; candidate++) {
			/* A fused-off subslice is not powered. */
			if ((gt->sseu.subslice_mask & (1U << candidate)) == 0U)
				continue;

			subslice = candidate;
			break;
		}
	}

	/* Adds the steering to the GT list. */
	drv_i915_wa_write_clr_set(
		wal,
		GEN8_MCR_SELECTOR,
		GEN11_MCR_SLICE_MASK | GEN11_MCR_SUBSLICE_MASK,
		GEN11_MCR_SLICE(0) | GEN11_MCR_SUBSLICE(subslice),
		0,
		"MCR steering (icl_wa_init_mcr)");
}

/* Adds Wa_14011060649 for every even-instance video decode engine. */
static void
i915_wa_14011060649(
	struct i915_wa_list *wal,
	const struct i915_gt_info *gt)
{
	const struct i915_engine_info *engine;
	unsigned index;

	/* Without GT state there are no engines to walk. */
	if (gt == NULL)
		return;

	/* Disables the IECP unit clock gating of each even video decode engine. */
	for (index = 0U; index < gt->num_engines; index++) {
		engine = &gt->engines[index];

		/* Only a video decode engine has the IECP unit. */
		if (engine->class != I915_VIDEO_DECODE_CLASS)
			continue;

		/* The odd instances share the unit of the even one. */
		if ((engine->instance % 2) != 0)
			continue;

		drv_i915_wa_write_or(wal, VDBOX_CGCTL3F10(engine->mmio_base), IECPUNIT_CLKGATE_DIS, 0, "Wa_14011060649");
	}
}

/*
 * Adds one whitelisted register with its access flags.
 *
 * Linux whitelist_reg_ext(): the entry's register field holds the address
 * and the flags, the word the apply pass writes into a non-privileged slot.
 * It is not a register to program.
 */
static void
i915_whitelist_reg_ext(
	struct i915_wa_list *wal,
	uint32_t reg,
	uint32_t flags,
	const char *name)
{
	struct i915_wa *entry;

	/* Refuses, and counts, an entry that does not fit. */
	if (wal->count >= (unsigned)I915_WA_MAX) {
		wal->overflow++;
		return;
	}

	/* Appends the address and flags; nothing is cleared, set or verified. */
	entry = &wal->list[wal->count];
	entry->reg = reg | flags;
	entry->clr = 0U;
	entry->set = 0U;
	entry->read_mask = 0U;
	entry->kind = I915_WA_PLAIN;
	entry->is_mcr = 0;
	entry->name = name;
	wal->count++;
}

/*
 * Steers slice-specific reads at the first enabled slice and subslice
 * (gen9_wa_init_mcr()).
 *
 * Gen9 has no multicast default: a read of a slice-specific register goes
 * to the slice and subslice the selector names, and a fused-off one answers
 * with garbage.
 */
static void
i915_gen9_wa_init_mcr(
	struct i915_wa_list *wal,
	const struct i915_gt_info *gt)
{
	unsigned slice;
	unsigned subslice;
	unsigned candidate;

	/* Finds the lowest enabled slice. */
	slice = 0U;
	for (candidate = 0U; candidate < 3U; candidate++) {
		if ((gt->sseu.slice_mask & (1U << candidate)) == 0U)
			continue;

		/* The lowest enabled slice is where the steering points. */
		slice = candidate;
		break;
	}

	/* Finds the lowest enabled subslice, which every enabled slice has. */
	subslice = 0U;
	for (candidate = 0U; candidate < 4U; candidate++) {
		if ((gt->sseu.subslice_mask & (1U << candidate)) == 0U)
			continue;

		/* The lowest enabled subslice is where the steering points. */
		subslice = candidate;
		break;
	}

	/* Adds the steering to the GT list. */
	drv_i915_wa_write_clr_set(
		wal,
		GEN8_MCR_SELECTOR,
		GEN8_MCR_SLICE_MASK | GEN8_MCR_SUBSLICE_MASK,
		GEN8_MCR_SLICE(slice) | GEN8_MCR_SUBSLICE(subslice),
		0,
		"MCR steering (gen9_wa_init_mcr)");
}

/*
 * Builds the workaround list of one Gen9 engine (rcs_engine_wa_init() and
 * xcs_engine_wa_init() for graphics version 9).
 *
 * Gen9 has no RING_CMD_CCTL default MOCS (engine_fake_wa_init() is Gen12+).
 */
static void
i915_gen9_engine_workarounds(
	struct i915_wa_list *wal,
	const struct i915_gt_info *gt,
	const struct i915_engine_info *engine)
{
	/* Starts the engine list empty, named after the engine. */
	i915_wa_list_reset(wal, engine->name);

	/*
	 * WaKBLVECSSemaphoreWaitPoll:kbl, steppings A0 to before F0: the
	 * revisions 0 to 3 and the C0 respin 5.
	 */
	if (engine->class != I915_RENDER_CLASS) {
		if (gt->platform != I915_PLATFORM_KABYLAKE)
			return;
		if (gt->revision == 4U || gt->revision > 5U)
			return;

		/* Sets the semaphore wait poll of a Kaby Lake non-render engine, per the workaround above. */
		drv_i915_wa_write_clr_set(wal, RING_SEMA_WAIT_POLL(engine->mmio_base), 0xffffffffU, 1U, 0, "WaKBLVECSSemaphoreWaitPoll");
		return;
	}

	/* Per-context preemption control (graphics version 9 and later). */
	drv_i915_wa_masked_en(wal, GEN7_FF_SLICE_CS_CHICKEN1, GEN9_FFSC_PERCTX_PREEMPT_CTRL, 0, "perctx preempt ctrl");

	/* WaEnableGapsTsvCreditFix:skl,kbl,cfl */
	drv_i915_wa_write_or(wal, GEN8_GARBCNTL, GEN9_GAPS_TSV_CREDIT_DISABLE, 0, "WaEnableGapsTsvCreditFix");

	/* WaContextSwitchWithConcurrentTLBInvalidate:skl,kbl,cfl */
	drv_i915_wa_masked_en(wal, GEN9_CSFE_CHICKEN1_RCS, GEN9_PREEMPT_GPGPU_SYNC_SWITCH_DISABLE, 0, "WaContextSwitchWithConcurrentTLBInvalidate");

	/* WaEnableLbsSlaRetryTimerDecrement:skl,kbl,cfl */
	drv_i915_wa_write_or(wal, BDW_SCRATCH1, GEN9_LBS_SLA_RETRY_TIMER_DECREMENT_ENABLE, 1, "WaEnableLbsSlaRetryTimerDecrement");

	/* WaOCLCoherentLineFlush:skl,kbl,cfl */
	drv_i915_wa_write_or(wal, GEN8_L3SQCREG4, GEN8_LQSC_FLUSH_COHERENT_LINES, 1, "WaOCLCoherentLineFlush");

	/* Disables the L3 atomics, which can hang the GPU unrecoverably. */
	drv_i915_wa_write_clr_set(wal, GEN9_SCRATCH_LNCF1, GEN9_LNCF_NONIA_COHERENT_ATOMICS_ENABLE, 0U, 0, "L3 atomics off (LNCF1)");
	drv_i915_wa_write_clr_set(wal, GEN8_L3SQCREG4, GEN8_LQSQ_NONIA_COHERENT_ATOMICS_ENABLE, 0U, 1, "L3 atomics off (L3SQCREG4)");
	drv_i915_wa_write_clr_set(wal, BDW_SCRATCH1, EVICTION_PERF_FIX_ENABLE, 0U, 1, "L3 atomics off (SCRATCH1)");
}

/*
 * Builds the context workaround list of one Gen9 engine
 * (gen9_ctx_workarounds_init() and the kbl and cfl additions).
 *
 * Only the render engine has a context list.  skl_tune_iz_hashing(), which
 * balances parts with exactly one 7-EU subslice, is not done.
 */
static void
i915_gen9_ctx_workarounds(
	struct i915_wa_list *wal,
	const struct i915_gt_info *gt,
	const struct i915_engine_info *engine)
{
	/* Starts the context list empty. */
	i915_wa_list_reset(wal, "ctx_wa");

	/* A non-render engine has no context workarounds on Gen9. */
	if (engine->class != I915_RENDER_CLASS)
		return;

	/* WaCompressedResourceSamplerPbeMediaNewHashMode:skl,kbl, which matches the display engine. */
	drv_i915_wa_masked_en(wal, GEN9_COMMON_SLICE_CHICKEN2, GEN9_PBE_COMPRESSED_HASH_SELECTION, 0, "WaCompressedResourceSamplerPbeMediaNewHashMode");
	drv_i915_wa_masked_en(wal, GEN9_HALF_SLICE_CHICKEN7, GEN9_SAMPLER_HASH_COMPRESSED_READ_ADDR, 1, "WaCompressedResourceSamplerPbeMediaNewHashMode");

	/* WaClearFlowControlGpgpuContextSave, WaDisablePartialInstShootdown */
	drv_i915_wa_masked_en(wal, GEN8_ROW_CHICKEN, FLOW_CONTROL_ENABLE | PARTIAL_INSTRUCTION_SHOOTDOWN_DISABLE, 1, "WaDisablePartialInstShootdown");

	/* WaEnableYV12BugFixInHalfSliceChicken7, WaEnableSamplerGPGPUPreemptionSupport */
	drv_i915_wa_masked_en(wal, GEN9_HALF_SLICE_CHICKEN7, GEN9_ENABLE_YV12_BUGFIX | GEN9_ENABLE_GPGPU_PREEMPTION, 1, "WaEnableSamplerGPGPUPreemptionSupport");

	/* Wa4x4STCOptimizationDisable, WaDisablePartialResolveInVc */
	drv_i915_wa_masked_en(wal, CACHE_MODE_1, GEN8_4x4_STC_OPTIMIZATION_DISABLE | GEN9_PARTIAL_RESOLVE_IN_VC_DISABLE, 0, "WaDisablePartialResolveInVc");

	/* WaCcsTlbPrefetchDisable */
	drv_i915_wa_masked_field_set(wal, GEN9_HALF_SLICE_CHICKEN5, GEN9_CCS_TLB_PREFETCH_ENABLE, 0U, 1, "WaCcsTlbPrefetchDisable");

	/* WaForceContextSaveRestoreNonCoherent, then WaForceEnableNonCoherent on the same register. */
	drv_i915_wa_masked_en(wal, HDC_CHICKEN0, HDC_FORCE_CONTEXT_SAVE_RESTORE_NON_COHERENT | HDC_FORCE_CSR_NON_COHERENT_OVR_DISABLE, 0, "WaForceContextSaveRestoreNonCoherent");
	drv_i915_wa_masked_en(wal, HDC_CHICKEN0, HDC_FORCE_NON_COHERENT, 0, "WaForceEnableNonCoherent");

	/* WaDisableSamplerPowerBypassForSOPingPong:skl,kbl,cfl */
	drv_i915_wa_masked_en(wal, GEN8_HALF_SLICE_CHICKEN3, GEN8_SAMPLER_POWER_BYPASS_DIS, 1, "WaDisableSamplerPowerBypassForSOPingPong");

	/* WaDisableSTUnitPowerOptimization */
	drv_i915_wa_masked_en(wal, HALF_SLICE_CHICKEN2, GEN8_ST_PO_DISABLE, 1, "WaDisableSTUnitPowerOptimization");

	/* WaDisable3DMidCmdPreemption */
	drv_i915_wa_masked_field_set(wal, GEN8_CS_CHICKEN1, GEN9_PREEMPT_3D_OBJECT_LEVEL, 0U, 0, "WaDisable3DMidCmdPreemption");

	/* WaDisableGPGPUMidCmdPreemption */
	drv_i915_wa_masked_field_set(wal, GEN8_CS_CHICKEN1, GEN9_PREEMPT_GPGPU_LEVEL_MASK, GEN9_PREEMPT_GPGPU_COMMAND_LEVEL, 0, "WaDisableGPGPUMidCmdPreemption");

	/* Skylake has nothing more. */
	if (gt->platform == I915_PLATFORM_SKYLAKE)
		return;

	/*
	 * WaToEnableHwFixForPushConstHWBug: every Coffee Lake and Comet Lake,
	 * and Kaby Lake from stepping C0 (revision 2) on.
	 */
	if (gt->platform != I915_PLATFORM_KABYLAKE) {
		drv_i915_wa_masked_en(wal, GEN9_COMMON_SLICE_CHICKEN2, GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION, 0, "WaToEnableHwFixForPushConstHWBug");
	} else if (gt->revision >= 2U) {
		drv_i915_wa_masked_en(wal, GEN9_COMMON_SLICE_CHICKEN2, GEN8_SBE_DISABLE_REPLAY_BUF_OPTIMIZATION, 0, "WaToEnableHwFixForPushConstHWBug");
	}

	/* WaDisableSbeCacheDispatchPortSharing:kbl,cfl */
	drv_i915_wa_masked_en(wal, GEN8_HALF_SLICE_CHICKEN1, GEN7_SBE_SS_CACHE_DISPATCH_PORT_SHARING_DISABLE, 1, "WaDisableSbeCacheDispatchPortSharing");
}

/*
 * Builds the register whitelist of one Gen9 engine (skl, kbl, cfl and cml
 * whitelist_build()).
 */
static void
i915_gen9_whitelist(
	struct i915_wa_list *wal,
	const struct i915_gt_info *gt,
	const struct i915_engine_info *engine)
{
	/* Starts the whitelist empty. */
	i915_wa_list_reset(wal, "whitelist");

	/* Comet Lake lets every non-render engine read its context timestamp. */
	if (engine->class != I915_RENDER_CLASS) {
		if (gt->platform == I915_PLATFORM_COMETLAKE)
			i915_whitelist_reg_ext(wal, RING_CTX_TIMESTAMP(engine->mmio_base), RING_FORCE_TO_NONPRIV_ACCESS_RD, "ctx timestamp RD");
		return;
	}

	/* gen9_whitelist_build(): WaVFEStateAfterPipeControlwithMediaStateClear and the preemption, HDC and push-constant chickens. */
	i915_whitelist_reg_ext(wal, GEN9_CTX_PREEMPT_REG, 0U, "WaVFEStateAfterPipeControlwithMediaStateClear");
	i915_whitelist_reg_ext(wal, GEN8_CS_CHICKEN1, 0U, "WaEnablePreemptionGranularityControlByUMD");
	i915_whitelist_reg_ext(wal, GEN8_HDC_CHICKEN1, 0U, "WaAllowUMDToModifyHDCChicken1");
	i915_whitelist_reg_ext(wal, GEN9_COMMON_SLICE_CHICKEN2, 0U, "WaSendPushConstantsFromMMIO");

	/* Skylake and Kaby Lake: WaDisableLSQCROPERFforOCL. */
	if (gt->platform == I915_PLATFORM_SKYLAKE || gt->platform == I915_PLATFORM_KABYLAKE) {
		i915_whitelist_reg_ext(wal, GEN8_L3SQCREG4, 0U, "WaDisableLSQCROPERFforOCL");
		return;
	}

	/* Coffee Lake and Comet Lake: the four depth and invocation counters. */
	i915_whitelist_reg_ext(
		wal,
		PS_INVOCATION_COUNT,
		RING_FORCE_TO_NONPRIV_ACCESS_RD | RING_FORCE_TO_NONPRIV_RANGE_4,
		"WaAllowPMDepthAndInvocationCountAccessFromUMD");
}

/*
 * Programs the Gen9 GTT workarounds (gtt_write_workarounds()): more TLB
 * entries for the GPGPU, the 64 KiB page-size field of the PDE, and the GTT
 * cache, which Gen9 may use because it has no 2 MiB pages.
 */
static void
i915_gen9_gtt_write_workarounds(
	struct i915_mmio *mmio)
{
	uint32_t value;

	/* WaIncreaseDefaultTLBEntries:skl,kbl,cfl */
	drv_i915_write32(mmio, GEN8_L3_LRA_1_GPGPU, GEN9_L3_LRA_1_GPGPU_DEFAULT_VALUE_SKL);

	/* Lets the page walker honour the IPS bit of a PDE. */
	value = drv_i915_read32(mmio, GEN8_GAMW_ECO_DEV_RW_IA);
	drv_i915_write32(mmio, GEN8_GAMW_ECO_DEV_RW_IA, value | GAMW_ECO_ENABLE_64K_IPS_FIELD);

	/* WaGttCachingOffByDefault */
	drv_i915_write32(mmio, HSW_GTT_CACHE_EN, GTT_CACHE_EN_ALL);
}

/*
 * Fills a MOCS table from its platform rows: every index first takes the
 * values of the unused-entries index, then the rows overwrite theirs.
 */
static void
i915_mocs_fill(
	struct i915_mocs *table,
	const struct i915_mocs_row *rows,
	unsigned row_count)
{
	uint32_t unused_control;
	uint16_t unused_l3cc;
	unsigned index;
	unsigned row;

	/* Finds the values every undefined index takes. */
	unused_control = 0U;
	unused_l3cc = 0U;
	for (row = 0U; row < row_count; row++) {
		if (rows[row].idx == table->unused_entries_index) {
			unused_control = rows[row].control;
			unused_l3cc = rows[row].l3cc;
		}
	}

	/* Fills every index with the unused-entry values first. */
	for (index = 0U; index < (unsigned)I915_MOCS_ENTRIES; index++) {
		table->control[index] = unused_control;
		table->l3cc[index] = unused_l3cc;
	}

	/* Then overwrites the indices the table defines. */
	for (row = 0U; row < row_count; row++) {
		index = rows[row].idx;

		/* A row beyond the table size is ignored. */
		if (index >= (unsigned)I915_MOCS_ENTRIES)
			continue;

		/* Stores the row's control and L3 cache values. */
		table->control[index] = rows[row].control;
		table->l3cc[index] = rows[row].l3cc;
	}

	/* The table can now be programmed. */
	table->valid = 1;
}

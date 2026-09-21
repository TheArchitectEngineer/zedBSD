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
 * reference; the other platforms' arms are absent rather than built and
 * skipped.  Every register access goes through the held accessors: the
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

#include "data/i915-gt-workarounds.inc"

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
#include "data/i915-gt-mocs-table.inc"
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
	uint32_t unused_control;
	uint16_t unused_l3cc;
	unsigned row_count;
	unsigned index;
	unsigned row;

	/* Starts from an empty, invalid table. */
	for (index = 0U; index < (unsigned)I915_MOCS_ENTRIES; index++) {
		table->control[index] = 0U;
		table->l3cc[index] = 0U;
	}

	table->n_entries = 0U;
	table->uc_index = 0U;
	table->unused_entries_index = 0U;
	table->valid = 0;

	/* Only the graphics version 12 table exists in this driver. */
	if (graphics_ver < 12) {
		kern_logf("i915: MOCS: only the gen12 table is ported\n");
		return;
	}

	/* Takes the size and the two special indices of the Gen12 table. */
	table->n_entries = GEN9_NUM_MOCS_ENTRIES;
	table->uc_index = GEN12_MOCS_UC_INDEX;
	table->unused_entries_index = GEN12_MOCS_UNUSED_ENTRIES_INDEX;

	/* Finds the values every undefined index takes. */
	row_count = (unsigned)(sizeof(i915_gen12_mocs_table) / sizeof(i915_gen12_mocs_table[0]));
	unused_control = 0U;
	unused_l3cc = 0U;
	for (row = 0U; row < row_count; row++) {
		if (i915_gen12_mocs_table[row].idx == table->unused_entries_index) {
			unused_control = i915_gen12_mocs_table[row].control;
			unused_l3cc = i915_gen12_mocs_table[row].l3cc;
		}
	}

	/* Fills every index with the unused-entry values first. */
	for (index = 0U; index < (unsigned)I915_MOCS_ENTRIES; index++) {
		table->control[index] = unused_control;
		table->l3cc[index] = unused_l3cc;
	}

	/* Then overwrites the indices the table defines. */
	for (row = 0U; row < row_count; row++) {
		index = i915_gen12_mocs_table[row].idx;

		/* A row beyond the table size is ignored. */
		if (index >= (unsigned)I915_MOCS_ENTRIES)
			continue;

		table->control[index] = i915_gen12_mocs_table[row].control;
		table->l3cc[index] = i915_gen12_mocs_table[row].l3cc;
	}

	/* The table can now be programmed. */
	table->valid = 1;
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

	/* Writes every control value into the global MOCS registers. */
	for (index = 0U; index < table->n_entries; index++) {
		drv_i915_write32(mmio, GEN12_GLOBAL_MOCS(index), table->control[index]);
		global_count++;
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

	/* Builds the GT workaround list. */
	drv_i915_gt_init_workarounds_adlp(&gt_init->gt_wa, gt);

	/* Builds the engine, whitelist and context lists of every engine. */
	for (index = 0U; index < gt->num_engines && index < (unsigned)I915_WA_ENGINES; index++) {
		drv_i915_engine_init_workarounds(&gt_init->engine_wa[index], &gt->engines[index], graphics_ver, gt_init->mocs.uc_index);
		drv_i915_engine_init_whitelist(&gt_init->whitelist[index], &gt->engines[index], graphics_ver);
		drv_i915_engine_init_ctx_wa(&gt_init->ctx_wa[index], &gt->engines[index], graphics_ver, gt_init->mocs.uc_index);
	}

	/* Prepares RC6, then RPS, as Linux intel_gt_pm_init() does. */
	drv_i915_rc6_init(&gt_init->rc6, mmio);
	drv_i915_rps_init(&gt_init->rps, sb_lock, mmio);

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

	UNUSED_PARAMETER(gt);

	/*
	 * Programs the PAT.  Linux setup_private_pat() runs in i915_gem_init(),
	 * before intel_gt_init(); it is kept at the head of the GT programming
	 * so it precedes everything that relies on a PAT index.
	 */
	pat_writes = 0U;
	drv_i915_tgl_setup_private_ppat(mmio, &pat_writes);
	gt_init->pat_programmed = 0;
	if (pat_writes == I915_PAT_ENTRIES)
		gt_init->pat_programmed = 1;

	/*
	 * Applies the GT workarounds and verifies them.  Linux verifies only
	 * under CONFIG_DRM_I915_DEBUG_GEM; here the readback always runs as a
	 * diagnostic that changes no hardware state.
	 */
	drv_i915_wa_list_apply(&gt_init->gt_wa, mmio, 1, &gt_init->gt_wa_applied);

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

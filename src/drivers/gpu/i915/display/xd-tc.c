/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The Type-C PHYs of the display 12 and 13 code (see xd-tc.h): the
 * Dekel PHY windows and the lane programming of a Type-C port.
 */

#include "display-internal.h"
#include "xd-io.h"
#include "xd-platform.h"
#include "xd-tc.h"

#include "../intel/display-regs-xd.h"

#include <kern/lock.h>

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/* How long one poll of the microcontroller's health waits. */
#define I915_XD_DKL_POLL_US		10U

/*
 * The load generator selects of the two transmitters of each lane on the
 * platforms that select them: HDMI drives lane 0 with 0 and 2 and lane 1
 * with 3 and 3; DisplayPort uses 0 everywhere.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (tgl_dkl_phy_set_signal_levels: the DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX1
 * and DKL_TX_DPCNTL2_CFG_LOADGENSELECT_TX2 values).
 */
#define I915_XD_DKL_HDMI_LOADGEN_LN0_TX1	0U
#define I915_XD_DKL_HDMI_LOADGEN_LN0_TX2	2U
#define I915_XD_DKL_HDMI_LOADGEN_LN1_TX1	3U
#define I915_XD_DKL_HDMI_LOADGEN_LN1_TX2	3U

/* The rate above which a DisplayPort link takes the high-rate swing table. */
#define I915_XD_DKL_SWING_LOW_MAX_KHZ	270000U

static void i915_xd_dkl_lock(struct i915_xd_dkl *dkl);
static void i915_xd_dkl_unlock(struct i915_xd_dkl *dkl);
static uint32_t i915_xd_dkl_select(struct i915_xd_dkl *dkl, unsigned tc, uint32_t phy_offset);
static uint32_t i915_xd_tc_swing_word(const struct i915_xd_dkl_swing_table *table, uint8_t level);
static uint32_t i915_xd_tc_loadgen(int hdmi, unsigned lane);

/*
 * Prepares the Dekel PHY windows of a display: the register window they
 * are reached through and the lock the ports share.
 */
void
drv_i915_xd_dkl_init(
	struct i915_xd_dkl *dkl,
	struct i915_xd_io *io)
{
	/* The window and the lock of the bank index registers. */
	dkl->io = io;
	spin_init(&dkl->lock, LOCK_RANK_DEVICE, "i915 xd dkl");
	dkl->hooks.read = NULL;
	dkl->hooks.write = NULL;
	dkl->hooks.rmw = NULL;
	dkl->hooks.context = NULL;

	/* The windows may be used from here on. */
	dkl->ready = 1;
}

/*
 * Gives the PHY accesses another route (a host test's model, or the
 * older modeset's recorded writes).  The hooks' owner serializes them.
 */
void
drv_i915_xd_dkl_set_hooks(
	struct i915_xd_dkl *dkl,
	const struct i915_xd_dkl_hooks *hooks)
{
	/* Takes the route. */
	dkl->hooks = *hooks;
}

/*
 * Works out where a PHY register of a Type-C port is reached: returns the
 * MMIO register in the port's window, and stores the bank index register
 * and the whole value to write to it first.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dkl_phy_regs.h
 * (_DKL_PHY1_BASE, _DKL_PHY2_BASE, _DKL_BANK_SHIFT, _HIP_INDEX_REG0,
 * _HIP_INDEX_REG1, _HIP_INDEX_SHIFT).
 */
uint32_t
drv_i915_xd_dkl_window(
	unsigned tc,
	uint32_t phy_offset,
	uint32_t *index_reg,
	uint32_t *index_value)
{
	uint32_t bank;

	/* The bank goes into the port's byte of its index register. */
	bank = phy_offset >> I915_XD_DKL_BANK_SHIFT;
	*index_reg = I915_XD_HIP_INDEX_REG(tc);
	*index_value = bank << I915_XD_HIP_INDEX_SHIFT(tc);

	/* Succeeded: the register in the port's window. */
	return I915_XD_DKL_WINDOW(tc) + (phy_offset & I915_XD_DKL_OFFSET_IN_BANK);
}

/*
 * Reads a register of a Type-C port's Dekel PHY (phy_offset is the PHY's
 * own offset, bank and register).  A struct that is not ready reads
 * all-ones.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dkl_phy.c
 * (dkl_phy_set_hip_idx, intel_dkl_phy_read).
 */
uint32_t
drv_i915_xd_dkl_read(
	struct i915_xd_dkl *dkl,
	unsigned tc,
	uint32_t phy_offset)
{
	uint32_t offset;
	uint32_t value;

	/* Another route reads it whole. */
	if (dkl->hooks.read != NULL) {
		value = dkl->hooks.read(dkl->hooks.context, tc, phy_offset);
		return value;
	}

	/* Windows that are not ready have no registers. */
	if (!dkl->ready)
		return 0xffffffffU;

	/* Selects the bank and reads the register under the window lock. */
	i915_xd_dkl_lock(dkl);

	offset = i915_xd_dkl_select(dkl, tc, phy_offset);
	value = drv_i915_xd_io_read(dkl->io, offset);

	i915_xd_dkl_unlock(dkl);

	/* Succeeded: the register's value. */
	return value;
}

/*
 * Writes a register of a Type-C port's Dekel PHY.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dkl_phy.c
 * (intel_dkl_phy_write).
 */
void
drv_i915_xd_dkl_write(
	struct i915_xd_dkl *dkl,
	unsigned tc,
	uint32_t phy_offset,
	uint32_t value)
{
	uint32_t offset;

	/* Another route writes it whole. */
	if (dkl->hooks.write != NULL) {
		dkl->hooks.write(dkl->hooks.context, tc, phy_offset, value);
		return;
	}

	/* Windows that are not ready have no registers. */
	if (!dkl->ready)
		return;

	/* Selects the bank and writes the register under the window lock. */
	i915_xd_dkl_lock(dkl);

	offset = i915_xd_dkl_select(dkl, tc, phy_offset);
	drv_i915_xd_io_write(dkl->io, offset, value);

	i915_xd_dkl_unlock(dkl);
}

/*
 * Clears and sets bits of a register of a Type-C port's Dekel PHY, the
 * read and the write under one hold of the window lock.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dkl_phy.c
 * (intel_dkl_phy_rmw).
 */
void
drv_i915_xd_dkl_rmw(
	struct i915_xd_dkl *dkl,
	unsigned tc,
	uint32_t phy_offset,
	uint32_t clear,
	uint32_t set)
{
	uint32_t offset;

	/* Another route changes it whole. */
	if (dkl->hooks.rmw != NULL) {
		dkl->hooks.rmw(dkl->hooks.context, tc, phy_offset, clear, set);
		return;
	}

	/* Windows that are not ready have no registers. */
	if (!dkl->ready)
		return;

	/* Selects the bank and changes the register under the window lock. */
	i915_xd_dkl_lock(dkl);

	offset = i915_xd_dkl_select(dkl, tc, phy_offset);
	(void)drv_i915_xd_io_rmw(dkl->io, offset, clear, set);

	i915_xd_dkl_unlock(dkl);
}

/*
 * Waits for the microcontroller of a Type-C port's PHY to report itself
 * healthy, as it must after the port's AUX well came up.  Returns 0,
 * ETIMEDOUT when it did not within timeout_us, or EIO when the time base
 * failed.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dkl_phy_regs.h
 * (_DKL_CMN_UC_DW27, DKL_CMN_UC_DW27_UC_HEALTH).
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (icl_tc_phy_aux_power_well_enable).
 */
int
drv_i915_xd_tc_wait_health(
	struct i915_xd_dkl *dkl,
	unsigned tc,
	unsigned timeout_us)
{
	uint32_t status;
	unsigned waited;
	int error;

	/* Reads the health bit until it is set or the time is up; the last read decides. */
	waited = 0U;
	for (;;) {
		/* The microcontroller reports itself healthy. */
		status = drv_i915_xd_dkl_read(dkl, tc, I915_XD_DKL_CMN_UC_DW27);
		if ((status & I915_XD_DKL_UC_HEALTH) != 0U)
			break;

		/* The time is up. */
		if (waited >= timeout_us)
			return ETIMEDOUT;

		/* Lets a little time pass before the next read. */
		error = drv_i915_xd_io_delay(dkl->io, I915_XD_DKL_POLL_US);
		if (error != 0)
			return EIO;
		waited += I915_XD_DKL_POLL_US;
	}

	/* Succeeded: the microcontroller is healthy. */
	return 0;
}

/*
 * Sets the DisplayPort mode of the two lanes of a Type-C port for its
 * pin assignment and the lanes the link uses: one or two transmitters
 * of each lane.  A Thunderbolt port is not set here (the caller skips
 * it).  Returns 0, or EINVAL for a pin assignment the PHY has no mode
 * for (the lanes are then written with no mode).
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_mg_phy_regs.h
 * (MG_DP_MODE_CFG_DP_X1_MODE, MG_DP_MODE_CFG_DP_X2_MODE), and
 * drivers/gpu/drm/i915/display/intel_dkl_phy_regs.h (_DKL_DP_MODE_LN0).
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (icl_program_mg_dp_mode).
 */
int
drv_i915_xd_tc_dp_mode(
	struct i915_xd_dkl *dkl,
	unsigned tc,
	unsigned pin_assignment,
	unsigned lanes)
{
	uint32_t lane0;
	uint32_t lane1;
	uint32_t modes;
	uint32_t mode0;
	uint32_t mode1;
	int error;

	/* Reads both lanes' mode words without their modes. */
	modes = I915_XD_DKL_DP_MODE_X1 | I915_XD_DKL_DP_MODE_X2;
	lane0 = drv_i915_xd_dkl_read(dkl, tc, I915_XD_DKL_DP_MODE);
	lane1 = drv_i915_xd_dkl_read(dkl, tc, I915_XD_DKL_DP_MODE + I915_XD_DKL_LANE_STRIDE);
	lane0 &= ~modes;
	lane1 &= ~modes;

	/* Chooses the modes the pin assignment and the lane count call for. */
	mode0 = 0U;
	mode1 = 0U;
	error = 0;
	switch (pin_assignment) {
	case I915_XD_TC_PIN_LEGACY:
		/* A legacy port: one lane is lane 1's single transmitter, more are both lanes doubled. */
		if (lanes == 1U) {
			mode1 = I915_XD_DKL_DP_MODE_X1;
		} else {
			mode0 = I915_XD_DKL_DP_MODE_X2;
			mode1 = I915_XD_DKL_DP_MODE_X2;
		}
		break;
	case I915_XD_TC_PIN_A:
		/* Assignment A doubles both lanes only for four lanes. */
		if (lanes == 4U) {
			mode0 = I915_XD_DKL_DP_MODE_X2;
			mode1 = I915_XD_DKL_DP_MODE_X2;
		}
		break;
	case I915_XD_TC_PIN_B:
		/* Assignment B doubles both lanes only for two lanes. */
		if (lanes == 2U) {
			mode0 = I915_XD_DKL_DP_MODE_X2;
			mode1 = I915_XD_DKL_DP_MODE_X2;
		}
		break;
	case I915_XD_TC_PIN_C:
	case I915_XD_TC_PIN_D:
	case I915_XD_TC_PIN_E:
	case I915_XD_TC_PIN_F:
		/* Assignments C to F: one lane is both lanes single, more are both doubled. */
		if (lanes == 1U) {
			mode0 = I915_XD_DKL_DP_MODE_X1;
			mode1 = I915_XD_DKL_DP_MODE_X1;
		} else {
			mode0 = I915_XD_DKL_DP_MODE_X2;
			mode1 = I915_XD_DKL_DP_MODE_X2;
		}
		break;
	default:
		/* The PHY has no mode for it. */
		error = EINVAL;
		break;
	}

	/* Writes both lanes' modes. */
	drv_i915_xd_dkl_write(dkl, tc, I915_XD_DKL_DP_MODE, lane0 | mode0);
	drv_i915_xd_dkl_write(dkl, tc, I915_XD_DKL_DP_MODE + I915_XD_DKL_LANE_STRIDE, lane1 | mode1);

	/* Reports a pin assignment without a mode. */
	if (error != 0)
		return error;

	/* Succeeded: the lanes are in the link's mode. */
	return 0;
}

/*
 * Chooses the Dekel swing table of a Type-C port: HDMI's, or DisplayPort's
 * up to 2.7 GHz or above it.  NULL on a platform without Dekel ports.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c
 * (tgl_get_dkl_buf_trans, adlp_get_dkl_buf_trans).
 */
const struct i915_xd_dkl_swing_table *
drv_i915_xd_tc_swing_table(
	const struct i915_xd_platform *platform,
	int hdmi,
	uint32_t rate_khz)
{
	const struct i915_xd_swing_set *set;

	/* HDMI has its own table. */
	set = platform->swing;
	if (hdmi)
		return set->dkl_hdmi;

	/* DisplayPort above 2.7 GHz. */
	if (rate_khz > I915_XD_DKL_SWING_LOW_MAX_KHZ)
		return set->dkl_dp_high;

	/* Succeeded: DisplayPort up to 2.7 GHz. */
	return set->dkl_dp_low;
}

/*
 * Programs the transmit swing of the four transmitters of a Type-C port:
 * each PHY lane leaves its suspend, its first transmitter takes the row
 * of the even link lane's level and its second the odd one's, the lane
 * leaves the 20-bit DisplayPort mode, and on a platform that selects them
 * it takes the load generators of HDMI or DisplayPort.  A level past the
 * table takes the table's last row.  A Thunderbolt port is not
 * programmed here (the caller skips it).
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dkl_phy_regs.h
 * (_DKL_TX_PMD_LANE_SUS_LN0, _DKL_TX_DPCNTL0_LN0, _DKL_TX_DPCNTL1_LN0,
 * _DKL_TX_DPCNTL2_LN0, DKL_TX_DP20BITMODE).
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (tgl_dkl_phy_set_signal_levels).
 */
void
drv_i915_xd_tc_signal_levels(
	struct i915_xd_dkl *dkl,
	const struct i915_xd_platform *platform,
	unsigned tc,
	const struct i915_xd_dkl_swing_table *table,
	const uint8_t levels[4],
	int hdmi)
{
	uint32_t fields;
	uint32_t lane_offset;
	uint32_t word;
	unsigned lane;

	/* The fields of the swing word a row sets. */
	fields = I915_XD_DKL_TX_PRESHOOT | I915_XD_DKL_TX_DE_EMPHASIS | I915_XD_DKL_TX_VSWING;

	/* Programs each PHY lane. */
	for (lane = 0U; lane < I915_XD_DKL_LANES; lane++) {
		/* Takes the lane out of its suspend. */
		lane_offset = lane * I915_XD_DKL_LANE_STRIDE;
		drv_i915_xd_dkl_write(dkl, tc, I915_XD_DKL_TX_PMD_LANE_SUS + lane_offset, 0U);

		/* The first transmitter carries the even link lane. */
		word = i915_xd_tc_swing_word(table, levels[2U * lane]);
		drv_i915_xd_dkl_rmw(dkl, tc, I915_XD_DKL_TX_DPCNTL0 + lane_offset, fields, word);

		/* The second transmitter carries the odd link lane. */
		word = i915_xd_tc_swing_word(table, levels[2U * lane + 1U]);
		drv_i915_xd_dkl_rmw(dkl, tc, I915_XD_DKL_TX_DPCNTL1 + lane_offset, fields, word);

		/* The lane leaves the 20-bit DisplayPort mode. */
		drv_i915_xd_dkl_rmw(dkl, tc, I915_XD_DKL_TX_DPCNTL2 + lane_offset, I915_XD_DKL_TX_DP20BITMODE, 0U);

		/* A platform without the load generator select is done with the lane. */
		if ((platform->wa & I915_XD_WA_DKL_LOADGEN) == 0U)
			continue;

		/* The load generators of the output. */
		word = i915_xd_tc_loadgen(hdmi, lane);
		drv_i915_xd_dkl_rmw(dkl, tc, I915_XD_DKL_TX_DPCNTL2 + lane_offset, I915_XD_DKL_TX_LOADGEN_TX1 | I915_XD_DKL_TX_LOADGEN_TX2,
			word);
	}
}

/*
 * Releases the soft reset of the PCS of both lanes of a Type-C port,
 * which a port switched from Thunderbolt to DisplayPort alternate or
 * legacy mode needs on display 13 before its buffer is enabled.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dkl_phy_regs.h
 * (_DKL_PCS_DW5_LN0, DKL_PCS_DW5_CORE_SOFTRESET).
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (adlp_tbt_to_dp_alt_switch_wa).
 */
void
drv_i915_xd_tc_release_soft_reset(
	struct i915_xd_dkl *dkl,
	unsigned tc)
{
	unsigned lane;

	/* Clears the bit in each lane. */
	for (lane = 0U; lane < I915_XD_DKL_LANES; lane++)
		drv_i915_xd_dkl_rmw(dkl, tc, I915_XD_DKL_PCS_DW5 + lane * I915_XD_DKL_LANE_STRIDE, I915_XD_DKL_PCS_DW5_CORE_SOFTRESET, 0U);
}

/* Takes the window lock, telling a host test so. */
static void
i915_xd_dkl_lock(
	struct i915_xd_dkl *dkl)
{
	/* Notes the lock, then takes it. */
	drv_i915_xd_io_lock_note(dkl->io, I915_XD_LOCK_DKL, 1);
	spin_lock(&dkl->lock);
}

/* Releases the window lock. */
static void
i915_xd_dkl_unlock(
	struct i915_xd_dkl *dkl)
{
	/* Releases the lock, then notes it. */
	spin_unlock(&dkl->lock);
	drv_i915_xd_io_lock_note(dkl->io, I915_XD_LOCK_DKL, 0);
}

/*
 * Selects the bank of a PHY offset in the port's index register and
 * reports the window register that reaches it.  Runs under the window
 * lock.
 */
static uint32_t
i915_xd_dkl_select(
	struct i915_xd_dkl *dkl,
	unsigned tc,
	uint32_t phy_offset)
{
	uint32_t index_reg;
	uint32_t index_value;
	uint32_t window;

	/* Writes the bank into the port's byte of its index register. */
	window = drv_i915_xd_dkl_window(tc, phy_offset, &index_reg, &index_value);
	drv_i915_xd_io_write(dkl->io, index_reg, index_value);

	/* Succeeded: the register in the port's window. */
	return window;
}

/* Builds the swing word of one table row; a level past the table takes its last row. */
static uint32_t
i915_xd_tc_swing_word(
	const struct i915_xd_dkl_swing_table *table,
	uint8_t level)
{
	const struct i915_xd_dkl_swing *row;
	uint32_t word;
	unsigned index;

	/* The row of the level, kept inside the table. */
	index = level;
	if (index >= table->count)
		index = table->count - 1U;
	row = &table->rows[index];

	/* Places the preshoot, de-emphasis and swing control fields. */
	word = i915_xd_field_put(I915_XD_DKL_TX_PRESHOOT, row->preshoot);
	word |= i915_xd_field_put(I915_XD_DKL_TX_DE_EMPHASIS, row->de_emphasis);
	word |= i915_xd_field_put(I915_XD_DKL_TX_VSWING, row->vswing);

	/* Succeeded: the row's swing word. */
	return word;
}

/* Builds the load generator selects of a PHY lane for HDMI or DisplayPort. */
static uint32_t
i915_xd_tc_loadgen(
	int hdmi,
	unsigned lane)
{
	uint32_t word;

	/* DisplayPort selects 0 for both transmitters. */
	if (!hdmi)
		return 0U;

	/* HDMI: lane 0 and lane 1 have their own selects. */
	if (lane == 0U) {
		word = i915_xd_field_put(I915_XD_DKL_TX_LOADGEN_TX1, I915_XD_DKL_HDMI_LOADGEN_LN0_TX1);
		word |= i915_xd_field_put(I915_XD_DKL_TX_LOADGEN_TX2, I915_XD_DKL_HDMI_LOADGEN_LN0_TX2);
	} else {
		word = i915_xd_field_put(I915_XD_DKL_TX_LOADGEN_TX1, I915_XD_DKL_HDMI_LOADGEN_LN1_TX1);
		word |= i915_xd_field_put(I915_XD_DKL_TX_LOADGEN_TX2, I915_XD_DKL_HDMI_LOADGEN_LN1_TX2);
	}

	/* Succeeded: the selects of the lane. */
	return word;
}

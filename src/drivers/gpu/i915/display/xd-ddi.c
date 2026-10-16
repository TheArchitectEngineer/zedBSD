/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DDIs of the display 12 and 13 code (see xd-ddi.h).
 */

#include "xd-ddi.h"
#include "xd-io.h"
#include "xd-platform.h"

#include "../intel/display-regs-xd.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The fixed transmit settings of a combo port's swing: the scaling mode
 * and termination of DW5, the compensation scalar of DW2; the rate above
 * which no lane takes the load generator.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (icl_ddi_combo_vswing_program, icl_combo_phy_loadgen_select).
 */
#define I915_XD_COMBO_SCALING_MODE	0x2U
#define I915_XD_COMBO_RTERM		0x6U
#define I915_XD_COMBO_RCOMP_SCALAR	0x98U
#define I915_XD_COMBO_LOADGEN_KHZ	600000U

/*
 * The lanes a port powers down in DW10, by its lane count: one lane,
 * two lanes, and the same reversed.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_combo_phy_regs.h (PWR_DOWN_LN_3_2_1,
 * PWR_DOWN_LN_3_2, PWR_DOWN_LN_2_1_0, PWR_DOWN_LN_1_0, PWR_UP_ALL_LANES).
 */
#define I915_XD_LANES_1			0xeU
#define I915_XD_LANES_2			0xcU
#define I915_XD_LANES_1_REVERSED	0x7U
#define I915_XD_LANES_2_REVERSED	0x3U

/*
 * How long a DDI buffer may take to go idle, and to leave idle (a Type-C
 * PHY's longer); how long the idle patterns may take.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_wait_ddi_buf_idle, intel_wait_ddi_buf_active,
 * intel_ddi_set_idle_link_train).
 */
#define I915_XD_DDI_IDLE_US		8U
#define I915_XD_DDI_ACTIVE_US		1000U
#define I915_XD_DDI_ACTIVE_TC_US	3000U
#define I915_XD_DDI_IDLE_PATTERNS_US	1000U

/* The bytes of COMP_DW1 that hold the process and voltage reference. */
#define I915_XD_COMP_DW1_PROCMON	0x00ff00ffU

static uint32_t i915_xd_combo_base(unsigned phy);
static int i915_xd_combo_has(uint8_t mask, unsigned phy);
static int i915_xd_combo_check(struct i915_xd_io *io, uint32_t offset, uint32_t mask, uint32_t value);
static void i915_xd_combo_init_one(struct i915_xd_io *io, const struct i915_xd_platform *platform, unsigned phy);
static uint32_t i915_xd_combo_loadgen(const struct i915_xd_combo_levels *levels, unsigned lane);
static void i915_xd_combo_swing(struct i915_xd_io *io, const struct i915_xd_platform *platform, const struct i915_xd_combo_levels *levels, uint32_t base);
static unsigned i915_xd_ddi_wait_active(struct i915_xd_io *io, const struct i915_xd_ddi_link *link);

/*
 * Tells whether a combo PHY reads as brought up: its IO compensation
 * powered (where the PHY has the word) and started; on display 12 on the
 * duty cycle corrector set to run once; the process and voltage reference
 * values of what the PHY reports; the reference current on a PHY that
 * owns the compensation resistor; its common lane allowed to power down.
 * Every word is read even after one that is wrong.  Returns 1 or 0.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_combo_phy.c
 * (icl_combo_phy_verify_state, icl_combo_phy_enabled,
 * icl_verify_procmon_ref_values, phy_is_master, has_phy_misc).
 */
int
drv_i915_xd_combo_phy_ready(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	unsigned phy)
{
	const struct i915_xd_procmon *procmon;
	uint32_t base;
	int ready;

	/* A PHY whose compensation is powered down or not started is not brought up. */
	base = i915_xd_combo_base(phy);
	if (i915_xd_combo_has(platform->phy_misc, phy) &&
	    (drv_i915_xd_io_read(io, I915_XD_PHY_MISC(phy)) & I915_XD_PHY_MISC_IO_COMP_POWER_DOWN) != 0U)
		return 0;
	if ((drv_i915_xd_io_read(io, I915_XD_PORT_COMP_DW(base, 0U)) & I915_XD_COMP_DW0_INIT) == 0U)
		return 0;

	/* The duty cycle corrector of display 12 on. */
	ready = 1;
	if (platform->display_ver >= 12U) {
		ready &= i915_xd_combo_check(io, I915_XD_PORT_TX_DW_LN(base, 0U, 8U),
			I915_XD_TX_DW8_ODCC_CLOCK_SELECT | I915_XD_TX_DW8_ODCC_DIVIDER,
			I915_XD_TX_DW8_ODCC_CLOCK_SELECT | i915_xd_field_put(I915_XD_TX_DW8_ODCC_DIVIDER, I915_XD_TX_DW8_ODCC_DIVIDE_BY_2));
		ready &= i915_xd_combo_check(io, I915_XD_PORT_PCS_DW_LN(base, 0U, 1U), I915_XD_PCS_DW1_DCC_MODE,
			i915_xd_field_put(I915_XD_PCS_DW1_DCC_MODE, I915_XD_PCS_DW1_DCC_RUN_ONCE));
	}

	/* The reference values of the process and voltage the PHY reports. */
	procmon = drv_i915_xd_procmon(drv_i915_xd_io_read(io, I915_XD_PORT_COMP_DW(base, 3U)));
	ready &= i915_xd_combo_check(io, I915_XD_PORT_COMP_DW(base, 1U), I915_XD_COMP_DW1_PROCMON, procmon->dw1);
	ready &= i915_xd_combo_check(io, I915_XD_PORT_COMP_DW(base, 9U), 0xffffffffU, procmon->dw9);
	ready &= i915_xd_combo_check(io, I915_XD_PORT_COMP_DW(base, 10U), 0xffffffffU, procmon->dw10);

	/* The reference current of a PHY with the resistor, and the common lane's power down. */
	if (i915_xd_combo_has(platform->comp_masters, phy))
		ready &= i915_xd_combo_check(io, I915_XD_PORT_COMP_DW(base, 8U), I915_XD_COMP_DW8_IREFGEN, I915_XD_COMP_DW8_IREFGEN);
	ready &= i915_xd_combo_check(io, I915_XD_PORT_CL_DW(base, 5U), I915_XD_CL_DW5_POWER_DOWN_ENABLE, I915_XD_CL_DW5_POWER_DOWN_ENABLE);

	/* Succeeded: whether every word held. */
	return ready;
}

/*
 * Brings up every combo PHY of the platform that does not read as
 * brought up.  Returns the mask of the PHYs it wrote.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_combo_phy.c
 * (icl_combo_phys_init).
 */
unsigned
drv_i915_xd_combo_phy_init(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform)
{
	unsigned written;
	unsigned phy;

	/* Each combo PHY, first to last; one that is brought up is left alone. */
	written = 0U;
	for (phy = 0U; phy < 8U; phy++) {
		if (!i915_xd_combo_has(platform->combo_phys, phy))
			continue;
		if (drv_i915_xd_combo_phy_ready(io, platform, phy))
			continue;

		i915_xd_combo_init_one(io, platform, phy);
		written |= 1U << phy;
	}

	/* Succeeded: the PHYs written. */
	return written;
}

/*
 * Stops every combo PHY of the platform, last to first: the IO
 * compensation powered down where the PHY has the word, the compensation
 * stopped.  Returns 1 when PHY A did not read as brought up before (the
 * caller reports it; old firmware leaves it so on some platforms).
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_combo_phy.c
 * (icl_combo_phys_uninit).
 */
unsigned
drv_i915_xd_combo_phy_uninit(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform)
{
	unsigned phy;
	unsigned a_not_ready;

	/* Each combo PHY, last to first. */
	a_not_ready = 0U;
	for (phy = 8U; phy-- > 0U;) {
		if (!i915_xd_combo_has(platform->combo_phys, phy))
			continue;

		/* PHY A is expected to read as brought up. */
		if (phy == 0U && !drv_i915_xd_combo_phy_ready(io, platform, phy))
			a_not_ready = 1U;

		/* Powers the compensation down, then stops it. */
		if (i915_xd_combo_has(platform->phy_misc, phy))
			(void)drv_i915_xd_io_rmw(io, I915_XD_PHY_MISC(phy), 0U, I915_XD_PHY_MISC_IO_COMP_POWER_DOWN);
		(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_COMP_DW(i915_xd_combo_base(phy), 0U), I915_XD_COMP_DW0_INIT, 0U);
	}

	/* Succeeded: whether PHY A was down before. */
	return a_not_ready;
}

/*
 * Powers up the lanes a port's link uses on its combo PHY: one or two
 * lanes keep the others down (from the other end when the lanes are
 * reversed); four, or a count the PHY does not know, power up every lane.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_combo_phy.c
 * (intel_combo_phy_power_up_lanes).
 */
void
drv_i915_xd_combo_lanes(
	struct i915_xd_io *io,
	unsigned phy,
	unsigned lanes,
	int reversed)
{
	uint32_t down;

	/* The lanes kept down. */
	down = 0U;
	if (lanes == 1U)
		down = reversed ? I915_XD_LANES_1_REVERSED : I915_XD_LANES_1;
	else if (lanes == 2U)
		down = reversed ? I915_XD_LANES_2_REVERSED : I915_XD_LANES_2;

	/* Writes the field. */
	(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_CL_DW(i915_xd_combo_base(phy), 10U), I915_XD_CL_DW10_LANE_POWER_DOWN,
		i915_xd_field_put(I915_XD_CL_DW10_LANE_POWER_DOWN, down));
}

/*
 * Chooses the swing table of a combo port: HDMI's; for eDP above 5.4 GHz
 * the eDP high rate table, with the VBT's HOBL setting the HOBL table,
 * with its low swing the eDP low table, otherwise as DisplayPort; for
 * DisplayPort the table above 2.7 GHz or the one up to it.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c
 * (tgl_get_combo_buf_trans, tgl_get_combo_buf_trans_edp,
 * tgl_get_combo_buf_trans_dp, adlp_get_combo_buf_trans,
 * adlp_get_combo_buf_trans_edp, adlp_get_combo_buf_trans_dp,
 * use_edp_hobl, use_edp_low_vswing).
 */
const struct i915_xd_combo_swing_table *
drv_i915_xd_combo_swing_table(
	const struct i915_xd_platform *platform,
	unsigned kind,
	uint32_t rate_khz,
	unsigned vbt)
{
	const struct i915_xd_swing_set *set;

	/* HDMI has its own table. */
	set = platform->swing;
	if (kind == I915_XD_OUTPUT_HDMI)
		return set->hdmi;

	/* eDP by rate and the VBT. */
	if (kind == I915_XD_OUTPUT_EDP) {
		if (rate_khz > 540000U)
			return set->edp_hbr3;
		if ((vbt & I915_XD_SWING_HOBL) != 0U)
			return set->edp_hobl;
		if ((vbt & I915_XD_SWING_LOW) != 0U)
			return set->edp_low_swing;
	}

	/* DisplayPort by rate. */
	if (rate_khz > 270000U)
		return set->dp_high;

	/* Succeeded: up to 2.7 GHz. */
	return set->dp_low;
}

/* Tells whether a swing table is the platform's eDP HOBL table. */
int
drv_i915_xd_combo_swing_is_hobl(
	const struct i915_xd_platform *platform,
	const struct i915_xd_combo_swing_table *table)
{
	/* Only that table. */
	return table == platform->swing->edp_hobl ? 1 : 0;
}

/*
 * Finds the swing table row of a DisplayPort lane's level byte.  The rows
 * of a DisplayPort or eDP table follow the swing and pre-emphasis pairs a
 * sink may ask for, by swing and then by pre-emphasis, the two adding up
 * to 3 at most.  Returns 0 with the row, or EINVAL for a pair no table
 * has (row 0).
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (index_to_dp_signal_levels).
 */
int
drv_i915_xd_combo_level_row(
	uint8_t level,
	unsigned *row)
{
	/*
	 * The first row of each swing level: four pre-emphasis levels at
	 * swing 0, three at swing 1, two at swing 2 and one at swing 3.
	 */
	static const uint8_t first_row[4] = { 0U, 4U, 7U, 9U };
	unsigned swing;
	unsigned preemph;

	/* The two levels of the byte. */
	swing = (unsigned)(level & 0x3U);
	preemph = (unsigned)((level >> 3) & 0x3U);
	*row = 0U;

	/* A pair beyond the table. */
	if (swing + preemph > 3U)
		return EINVAL;

	/* Succeeded: the pair's row. */
	*row = first_row[swing] + preemph;
	return 0;
}

/*
 * Returns the highest swing level a DisplayPort or eDP table reaches:
 * that of its last row (a table of more rows than pairs is taken as
 * every pair, one of none as the first).
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_ddi_dp_voltage_max).
 */
uint8_t
drv_i915_xd_combo_swing_max(
	const struct i915_xd_combo_swing_table *table)
{
	unsigned last;

	/* The last row, within the pairs. */
	last = 0U;
	if (table->count > 0U)
		last = table->count - 1U;
	if (last > 9U)
		last = 9U;

	/* The swing of the row: rows 9, 7 to 8, 4 to 6 and 0 to 3. */
	if (last >= 9U)
		return 3U;
	if (last >= 7U)
		return 2U;
	if (last >= 4U)
		return 1U;

	/* Succeeded: the lowest swing. */
	return 0U;
}

/*
 * Programs the signal levels of a combo port: the common keeper (on for
 * DisplayPort and eDP, off for HDMI), the load generator of each lane,
 * the SUS clock, the training enable cleared, the swing of each lane from
 * its table row, and the training enable set to latch it.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (icl_combo_phy_set_signal_levels).
 */
void
drv_i915_xd_combo_signal_levels(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	const struct i915_xd_combo_levels *levels)
{
	uint32_t base;
	uint32_t value;
	unsigned lane;

	/* The common keeper, read from lane 0 and written to the group. */
	base = i915_xd_combo_base(levels->phy);
	value = drv_i915_xd_io_read(io, I915_XD_PORT_PCS_DW_LN(base, 0U, 1U));
	if (levels->kind == I915_XD_OUTPUT_HDMI)
		value &= ~I915_XD_PCS_DW1_COMMON_KEEPER;
	else
		value |= I915_XD_PCS_DW1_COMMON_KEEPER;
	drv_i915_xd_io_write(io, I915_XD_PORT_PCS_DW_GRP(base, 1U), value);

	/* The load generator of each lane (the group word would overwrite them). */
	for (lane = 0U; lane < 4U; lane++)
		(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_TX_DW_LN(base, lane, 4U), I915_XD_TX_DW4_LOADGEN, i915_xd_combo_loadgen(levels, lane));

	/* The SUS clock, then the training enable cleared. */
	(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_CL_DW(base, 5U), 0U, I915_XD_CL_DW5_SUS_CLOCK_CONFIG);
	value = drv_i915_xd_io_read(io, I915_XD_PORT_TX_DW_LN(base, 0U, 5U));
	drv_i915_xd_io_write(io, I915_XD_PORT_TX_DW_GRP(base, 5U), value & ~I915_XD_TX_DW5_TRAINING);

	/* The swing. */
	i915_xd_combo_swing(io, platform, levels, base);

	/* The training enable set, which latches the swing. */
	value = drv_i915_xd_io_read(io, I915_XD_PORT_TX_DW_LN(base, 0U, 5U));
	drv_i915_xd_io_write(io, I915_XD_PORT_TX_DW_GRP(base, 5U), value | I915_XD_TX_DW5_TRAINING);
}

/*
 * Reports the register block of a combo PHY.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_combo_phy_regs.h (_ICL_COMBOPHY_A,
 * _ICL_COMBOPHY_B, _EHL_COMBOPHY_C, _RKL_COMBOPHY_D, _ADL_COMBOPHY_E).
 */
static uint32_t
i915_xd_combo_base(
	unsigned phy)
{
	static const uint32_t bases[] = {
		I915_XD_COMBO_PHY_A,
		I915_XD_COMBO_PHY_B,
		I915_XD_COMBO_PHY_C,
		I915_XD_COMBO_PHY_D,
		I915_XD_COMBO_PHY_E
	};

	/* A PHY past the last has PHY A's block (no platform names one). */
	if (phy >= sizeof(bases) / sizeof(bases[0]))
		return bases[0];

	/* Succeeded: the PHY's block. */
	return bases[phy];
}

/* Tells whether a PHY is in a mask of PHYs of the platform. */
static int
i915_xd_combo_has(
	uint8_t mask,
	unsigned phy)
{
	/* One bit per PHY. */
	if (phy >= 8U)
		return 0;

	/* Succeeded: the PHY's bit. */
	return (mask & (1U << phy)) != 0U ? 1 : 0;
}

/* Tells whether a word's bits under a mask read as a value (1 or 0). */
static int
i915_xd_combo_check(
	struct i915_xd_io *io,
	uint32_t offset,
	uint32_t mask,
	uint32_t value)
{
	/* Reads the word. */
	return (drv_i915_xd_io_read(io, offset) & mask) == value ? 1 : 0;
}

/*
 * Brings up one combo PHY: the IO compensation powered (where the PHY has
 * the word); on display 12 on the duty cycle corrector (read from lane 0,
 * written to the group); the process and voltage reference values; the
 * reference current on a PHY with the resistor; the compensation started
 * and the common lane allowed to power down.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_combo_phy.c
 * (icl_combo_phys_init, icl_get_procmon_ref_values,
 * icl_set_procmon_ref_values).
 */
static void
i915_xd_combo_init_one(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	unsigned phy)
{
	const struct i915_xd_procmon *procmon;
	uint32_t base;
	uint32_t value;

	/* Powers the IO compensation. */
	base = i915_xd_combo_base(phy);
	if (i915_xd_combo_has(platform->phy_misc, phy)) {
		value = drv_i915_xd_io_read(io, I915_XD_PHY_MISC(phy));
		drv_i915_xd_io_write(io, I915_XD_PHY_MISC(phy), value & ~I915_XD_PHY_MISC_IO_COMP_POWER_DOWN);
	}

	/* The duty cycle corrector: the ODCC clock divided by 2, the DCC run once. */
	if (platform->display_ver >= 12U) {
		value = drv_i915_xd_io_read(io, I915_XD_PORT_TX_DW_LN(base, 0U, 8U));
		value &= ~I915_XD_TX_DW8_ODCC_DIVIDER;
		value |= I915_XD_TX_DW8_ODCC_CLOCK_SELECT | i915_xd_field_put(I915_XD_TX_DW8_ODCC_DIVIDER, I915_XD_TX_DW8_ODCC_DIVIDE_BY_2);
		drv_i915_xd_io_write(io, I915_XD_PORT_TX_DW_GRP(base, 8U), value);

		value = drv_i915_xd_io_read(io, I915_XD_PORT_PCS_DW_LN(base, 0U, 1U));
		value &= ~I915_XD_PCS_DW1_DCC_MODE;
		value |= i915_xd_field_put(I915_XD_PCS_DW1_DCC_MODE, I915_XD_PCS_DW1_DCC_RUN_ONCE);
		drv_i915_xd_io_write(io, I915_XD_PORT_PCS_DW_GRP(base, 1U), value);
	}

	/* The reference values of the process and voltage the PHY reports. */
	procmon = drv_i915_xd_procmon(drv_i915_xd_io_read(io, I915_XD_PORT_COMP_DW(base, 3U)));
	(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_COMP_DW(base, 1U), I915_XD_COMP_DW1_PROCMON, procmon->dw1);
	drv_i915_xd_io_write(io, I915_XD_PORT_COMP_DW(base, 9U), procmon->dw9);
	drv_i915_xd_io_write(io, I915_XD_PORT_COMP_DW(base, 10U), procmon->dw10);

	/* The reference current, the compensation started, the common lane's power down. */
	if (i915_xd_combo_has(platform->comp_masters, phy))
		(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_COMP_DW(base, 8U), 0U, I915_XD_COMP_DW8_IREFGEN);
	(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_COMP_DW(base, 0U), 0U, I915_XD_COMP_DW0_INIT);
	(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_CL_DW(base, 5U), 0U, I915_XD_CL_DW5_POWER_DOWN_ENABLE);
}

/*
 * Reports the load generator bit of a lane: none above 6 GHz; with four
 * lanes on lanes 1 to 3; with fewer on lanes 1 and 2.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (icl_combo_phy_loadgen_select).
 */
static uint32_t
i915_xd_combo_loadgen(
	const struct i915_xd_combo_levels *levels,
	unsigned lane)
{
	/* No lane above 6 GHz. */
	if (levels->rate_khz > I915_XD_COMBO_LOADGEN_KHZ)
		return 0U;

	/* Four lanes: all but lane 0. */
	if (levels->lanes == 4U)
		return lane >= 1U ? I915_XD_TX_DW4_LOADGEN : 0U;

	/* Succeeded: lanes 1 and 2. */
	return (lane == 1U || lane == 2U) ? I915_XD_TX_DW4_LOADGEN : 0U;
}

/*
 * Writes the swing of a combo port: for eDP the 4K2K mode override (on
 * with the HOBL table); the scaling mode, termination and taps of DW5 for
 * the group; then each lane's swing select and compensation scalar (DW2),
 * cursor coefficients (DW4) and N scalar (DW7) from its table row.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (icl_ddi_combo_vswing_program).
 */
static void
i915_xd_combo_swing(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	const struct i915_xd_combo_levels *levels,
	uint32_t base)
{
	const struct i915_xd_combo_swing *row;
	uint32_t value;
	uint32_t override;
	unsigned lane;

	/* eDP: the 4K2K mode override follows the HOBL table. */
	if (levels->kind == I915_XD_OUTPUT_EDP) {
		override = I915_XD_CL_DW10_EDP4K2K_OVERRIDE | I915_XD_CL_DW10_EDP4K2K_OPTIMIZED;
		if (drv_i915_xd_combo_swing_is_hobl(platform, levels->table))
			(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_CL_DW(base, 10U), override, override);
		else
			(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_CL_DW(base, 10U), override, 0U);
	}

	/* DW5 for the group: the scaling mode, the termination, tap 3 off. */
	value = drv_i915_xd_io_read(io, I915_XD_PORT_TX_DW_LN(base, 0U, 5U));
	value &= ~(I915_XD_TX_DW5_SCALING_MODE | I915_XD_TX_DW5_RTERM | I915_XD_TX_DW5_TAP2_DISABLE | I915_XD_TX_DW5_TAP3_DISABLE);
	value |= i915_xd_field_put(I915_XD_TX_DW5_SCALING_MODE, I915_XD_COMBO_SCALING_MODE);
	value |= i915_xd_field_put(I915_XD_TX_DW5_RTERM, I915_XD_COMBO_RTERM);
	value |= I915_XD_TX_DW5_TAP3_DISABLE;
	drv_i915_xd_io_write(io, I915_XD_PORT_TX_DW_GRP(base, 5U), value);

	/* DW2 of each lane: the swing select (its bit 3 apart) and the scalar. */
	for (lane = 0U; lane < 4U; lane++) {
		row = &levels->table->rows[levels->level[lane]];
		(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_TX_DW_LN(base, lane, 2U),
			I915_XD_TX_DW2_SWING_UPPER | I915_XD_TX_DW2_SWING_LOWER | I915_XD_TX_DW2_RCOMP_SCALAR,
			i915_xd_field_put(I915_XD_TX_DW2_SWING_UPPER, (uint32_t)row->swing_select >> 3) |
			i915_xd_field_put(I915_XD_TX_DW2_SWING_LOWER, row->swing_select) |
			i915_xd_field_put(I915_XD_TX_DW2_RCOMP_SCALAR, I915_XD_COMBO_RCOMP_SCALAR));
	}

	/* DW4 of each lane: the cursor coefficients. */
	for (lane = 0U; lane < 4U; lane++) {
		row = &levels->table->rows[levels->level[lane]];
		(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_TX_DW_LN(base, lane, 4U),
			I915_XD_TX_DW4_POST_CURSOR_1 | I915_XD_TX_DW4_POST_CURSOR_2 | I915_XD_TX_DW4_CURSOR_COEFF,
			i915_xd_field_put(I915_XD_TX_DW4_POST_CURSOR_1, row->post_cursor_1) |
			i915_xd_field_put(I915_XD_TX_DW4_POST_CURSOR_2, row->post_cursor_2) |
			i915_xd_field_put(I915_XD_TX_DW4_CURSOR_COEFF, row->cursor_coeff));
	}

	/* DW7 of each lane: the N scalar. */
	for (lane = 0U; lane < 4U; lane++) {
		row = &levels->table->rows[levels->level[lane]];
		(void)drv_i915_xd_io_rmw(io, I915_XD_PORT_TX_DW_LN(base, lane, 7U), I915_XD_TX_DW7_N_SCALAR,
			i915_xd_field_put(I915_XD_TX_DW7_N_SCALAR, row->n_scalar));
	}
}

/*
 * Works out the DisplayPort word of a DDI buffer (without the enable):
 * the bits the port keeps, the translation select 0, the port width; on
 * a display 13 Type-C port the PHY link rate of the link.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (ddi_buf_phy_link_rate), and drivers/gpu/drm/i915/i915_reg.h
 * (DDI_PORT_WIDTH).
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_ddi_init_dp_buf_reg).
 */
uint32_t
drv_i915_xd_ddi_dp_word(
	const struct i915_xd_platform *platform,
	const struct i915_xd_ddi_link *link)
{
	static const uint32_t rates[][2] = {
		{ 162000U, 0U }, { 216000U, 4U }, { 243000U, 5U }, { 270000U, 1U },
		{ 324000U, 6U }, { 432000U, 7U }, { 540000U, 2U }, { 810000U, 3U }
	};
	uint32_t word;
	uint32_t width;
	unsigned index;

	/* The kept bits and the width (three lanes are written as four). */
	width = link->lanes == 3U ? 4U : link->lanes - 1U;
	word = (link->kept & ~I915_XD_DDI_BUF_TC_OWNED) | i915_xd_field_put(I915_XD_DDI_BUF_WIDTH, width);

	/* A display 13 Type-C port carries the PHY link rate (a rate it does not know as 1.62 GHz's). */
	if (link->tc && platform->tc_flow == I915_XD_TC_FLOW_TCSS) {
		for (index = 0U; index < sizeof(rates) / sizeof(rates[0]); index++) {
			if (rates[index][0] == link->rate_khz)
				word |= i915_xd_field_put(I915_XD_DDI_BUF_PHY_LINK_RATE, rates[index][1]);
		}
	}

	/* Succeeded: the word. */
	return word;
}

/*
 * Starts a DisplayPort link: a link already running is stopped (the
 * buffer, then the transport, then the wait for idle); the transport is
 * enabled in training pattern 1 (single stream with its enhanced framing,
 * or multi-stream); the caller's step before the buffer runs (a Type-C
 * PHY's workaround); the buffer is enabled with the word (a Type-C port
 * keeping the PHY ownership the buffer holds) and the wait for it to
 * leave idle runs.  Returns the waits that ran out.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_wait_ddi_buf_idle, intel_wait_ddi_buf_active: 8 us, 1 ms, 3 ms
 * on a Type-C PHY).
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_ddi_prepare_link_retrain, intel_wait_ddi_buf_idle,
 * intel_wait_ddi_buf_active).
 */
unsigned
drv_i915_xd_ddi_dp_start(
	struct i915_xd_io *io,
	const struct i915_xd_ddi_link *link,
	uint32_t word,
	void (*before_buffer)(void *context),
	void *context)
{
	uint32_t transport;
	uint32_t buffer;
	unsigned late;
	int was_on;

	/* A running link stops first: the buffer, the transport, the wait for idle. */
	late = 0U;
	transport = drv_i915_xd_io_read(io, I915_XD_DP_TP_CTL(link->trans));
	if ((transport & I915_XD_DP_TP_ENABLE) != 0U) {
		was_on = 0;
		buffer = drv_i915_xd_io_read(io, I915_XD_DDI_BUF_CTL(link->ddi));
		if ((buffer & I915_XD_DDI_BUF_ENABLE) != 0U) {
			drv_i915_xd_io_write(io, I915_XD_DDI_BUF_CTL(link->ddi), buffer & ~I915_XD_DDI_BUF_ENABLE);
			was_on = 1;
		}
		drv_i915_xd_io_write(io, I915_XD_DP_TP_CTL(link->trans), transport & ~I915_XD_DP_TP_ENABLE);
		drv_i915_xd_io_posting_read(io, I915_XD_DP_TP_CTL(link->trans));
		if (was_on && drv_i915_xd_io_poll(io, I915_XD_DDI_BUF_CTL(link->ddi), I915_XD_DDI_BUF_IDLE, I915_XD_DDI_BUF_IDLE,
		    I915_XD_DDI_IDLE_US, NULL) != 0)
			late |= I915_XD_DDI_LATE_IDLE;
	}

	/* The transport in training pattern 1. */
	transport = I915_XD_DP_TP_ENABLE | i915_xd_field_put(I915_XD_DP_TP_PATTERN, I915_XD_DP_TP_PATTERN_1);
	if (link->mst)
		transport |= I915_XD_DP_TP_MST;
	else if (link->enhanced)
		transport |= I915_XD_DP_TP_ENHANCED_FRAMING;
	drv_i915_xd_io_write(io, I915_XD_DP_TP_CTL(link->trans), transport);
	drv_i915_xd_io_posting_read(io, I915_XD_DP_TP_CTL(link->trans));

	/* The caller's step before the buffer. */
	if (before_buffer != NULL)
		before_buffer(context);

	/* The buffer; a Type-C port's PHY ownership is what the buffer holds, whatever the word says. */
	buffer = word | I915_XD_DDI_BUF_ENABLE;
	if (link->tc) {
		buffer &= ~I915_XD_DDI_BUF_TC_OWNED;
		buffer |= drv_i915_xd_io_read(io, I915_XD_DDI_BUF_CTL(link->ddi)) & I915_XD_DDI_BUF_TC_OWNED;
	}
	drv_i915_xd_io_write(io, I915_XD_DDI_BUF_CTL(link->ddi), buffer);
	drv_i915_xd_io_posting_read(io, I915_XD_DDI_BUF_CTL(link->ddi));

	/* Waits for the buffer to leave idle. */
	late |= i915_xd_ddi_wait_active(io, link);

	/* Succeeded: the waits that ran out. */
	return late;
}

/*
 * Selects a training pattern in a transcoder's transport (1, 2, 3, 4,
 * idle or normal, as I915_XD_DP_TP_PATTERN_*).
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_ddi_set_link_train).
 */
void
drv_i915_xd_ddi_pattern(
	struct i915_xd_io *io,
	unsigned trans,
	unsigned pattern)
{
	uint32_t transport;

	/* Replaces the pattern field. */
	transport = drv_i915_xd_io_read(io, I915_XD_DP_TP_CTL(trans));
	transport &= ~I915_XD_DP_TP_PATTERN;
	transport |= i915_xd_field_put(I915_XD_DP_TP_PATTERN, pattern);
	drv_i915_xd_io_write(io, I915_XD_DP_TP_CTL(trans), transport);
}

/*
 * Selects the idle pattern and waits for the idle patterns to be sent.
 * Returns I915_XD_DDI_LATE_IDLE_PATTERNS when they were not.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_ddi_set_idle_link_train: 1 ms).
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_ddi_set_idle_link_train).
 */
unsigned
drv_i915_xd_ddi_idle_patterns(
	struct i915_xd_io *io,
	unsigned trans)
{
	int error;

	/* The idle pattern, then the wait. */
	(void)drv_i915_xd_io_rmw(io, I915_XD_DP_TP_CTL(trans), I915_XD_DP_TP_PATTERN,
		i915_xd_field_put(I915_XD_DP_TP_PATTERN, I915_XD_DP_TP_PATTERN_IDLE));
	error = drv_i915_xd_io_poll(io, I915_XD_DP_TP_STATUS(trans), I915_XD_DP_TP_STATUS_IDLE_DONE, I915_XD_DP_TP_STATUS_IDLE_DONE,
		I915_XD_DDI_IDLE_PATTERNS_US, NULL);
	if (error != 0)
		return I915_XD_DDI_LATE_IDLE_PATTERNS;

	/* Succeeded: the patterns were sent. */
	return 0U;
}

/*
 * Starts an HDMI link's buffer: the kept bits and the enable (a display
 * 13 Type-C port, which drives HDMI only in legacy mode, with the PHY
 * ownership), then the wait for it to leave idle.  Returns the waits that
 * ran out.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (intel_enable_ddi_hdmi).
 */
unsigned
drv_i915_xd_ddi_hdmi_start(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	const struct i915_xd_ddi_link *link)
{
	uint32_t buffer;

	/* The word. */
	buffer = link->kept | I915_XD_DDI_BUF_ENABLE;
	if (link->tc && platform->tc_flow == I915_XD_TC_FLOW_TCSS)
		buffer |= I915_XD_DDI_BUF_TC_OWNED;
	drv_i915_xd_io_write(io, I915_XD_DDI_BUF_CTL(link->ddi), buffer);

	/* Succeeded: the wait for the buffer to leave idle. */
	return i915_xd_ddi_wait_active(io, link);
}

/*
 * Stops a link's buffer: the buffer disabled when it is enabled; for
 * DisplayPort the transport disabled; FEC off when the link used it (with
 * a posting read); then the wait for the buffer to go idle, when it was
 * enabled.  Returns the waits that ran out.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi.c
 * (disable_ddi_buf, intel_ddi_disable_fec, intel_wait_ddi_buf_idle).
 */
unsigned
drv_i915_xd_ddi_stop(
	struct i915_xd_io *io,
	const struct i915_xd_ddi_link *link,
	int dp)
{
	uint32_t buffer;
	int was_on;

	/* The buffer. */
	was_on = 0;
	buffer = drv_i915_xd_io_read(io, I915_XD_DDI_BUF_CTL(link->ddi));
	if ((buffer & I915_XD_DDI_BUF_ENABLE) != 0U) {
		drv_i915_xd_io_write(io, I915_XD_DDI_BUF_CTL(link->ddi), buffer & ~I915_XD_DDI_BUF_ENABLE);
		was_on = 1;
	}

	/* The transport and FEC. */
	if (dp)
		(void)drv_i915_xd_io_rmw(io, I915_XD_DP_TP_CTL(link->trans), I915_XD_DP_TP_ENABLE, 0U);
	if (link->fec) {
		(void)drv_i915_xd_io_rmw(io, I915_XD_DP_TP_CTL(link->trans), I915_XD_DP_TP_FEC, 0U);
		drv_i915_xd_io_posting_read(io, I915_XD_DP_TP_CTL(link->trans));
	}

	/* Waits for the buffer to go idle. */
	if (was_on && drv_i915_xd_io_poll(io, I915_XD_DDI_BUF_CTL(link->ddi), I915_XD_DDI_BUF_IDLE, I915_XD_DDI_BUF_IDLE,
	    I915_XD_DDI_IDLE_US, NULL) != 0)
		return I915_XD_DDI_LATE_IDLE;

	/* Succeeded: no wait ran out. */
	return 0U;
}

/* Waits for a DDI buffer to leave idle (longer on a Type-C PHY); returns I915_XD_DDI_LATE_ACTIVE when it did not. */
static unsigned
i915_xd_ddi_wait_active(
	struct i915_xd_io *io,
	const struct i915_xd_ddi_link *link)
{
	unsigned timeout_us;
	int error;

	/* The time a Type-C or a combo buffer is given. */
	timeout_us = link->tc ? I915_XD_DDI_ACTIVE_TC_US : I915_XD_DDI_ACTIVE_US;
	error = drv_i915_xd_io_poll(io, I915_XD_DDI_BUF_CTL(link->ddi), I915_XD_DDI_BUF_IDLE, 0U, timeout_us, NULL);
	if (error != 0)
		return I915_XD_DDI_LATE_ACTIVE;

	/* Succeeded: the buffer is active. */
	return 0U;
}

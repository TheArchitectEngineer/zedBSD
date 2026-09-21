/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_combo_phy.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2018 Intel Corporation
 */

/*
 * Derived from the Linux kernel v6.8.12 (drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c),
 * which carries the following notice.
 *
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */

/*
 * The combo PHYs and their buffer translations (see phy.h).
 *
 * intel_combo_phy_init() -> icl_combo_phys_init() for combo PHYs A and B:
 * each PHY whose state already verifies (icl_combo_phy_verify_state()) is
 * left alone; any other is given PHY_MISC, the display version 12+ ODCC /
 * DCC bits, the procmon reference values chosen from the process and
 * voltage it reports, IREFGEN on the master PHY, COMP_INIT and
 * CL_POWER_DOWN_ENABLE, in the reference order.  Lane registers are read
 * and group registers written.  Every combo PHY of these platforms has
 * PHY_MISC, and only PHY A is a master.
 *
 * The lane power-up of a port and the buffer-translation hooks of an
 * encoder are the Linux text of intel_combo_phy.c and
 * intel_ddi_buf_trans.c.
 */

#include "modeset-internal.h"
#include "phy.h"

#include "../mmio.h"
#include "../trace.h"

#include <kern/klog.h>

/* The trace stage of the display noirq bring-up (the old probe's stage P3). */
#define I915_PHY_TRACE_STAGE_NOIRQ	4U

/* The per-PHY register bases and offsets (intel_combo_phy_regs.h). */
#define COMBOPHY_A	0x162000u
#define COMBOPHY_B	0x6C000u
#define PORT_COMP	0x100u	/* COMP_DW base within a PHY */
#define PORT_PCS_GRP	0x600u
#define PORT_PCS_LN0	0x800u
#define PORT_TX_GRP	0x680u
#define PORT_TX_LN0	0x880u
#define PHY_MISC_A	0x64C00u
#define PHY_MISC_B	0x64C04u

/* The field bits (intel_combo_phy_regs.h). */
#define COMP_INIT			(1u << 31)
#define IREFGEN				(1u << 24)
#define CL_POWER_DOWN_ENABLE		(1u << 4)
#define PROCESS_INFO_MASK		(7u << 26)
#define VOLTAGE_INFO_MASK		(3u << 24)
#define PHY_MISC_DE_IO_COMP_PWR_DOWN	(1u << 23)
#define TX_DW8_ODCC_CLK_SEL		(1u << 31)
#define TX_DW8_ODCC_CLK_DIV_SEL_MASK	(3u << 29)
#define TX_DW8_ODCC_CLK_DIV_SEL_DIV2	(1u << 29)
#define DCC_MODE_SELECT_MASK		(3u << 20)
#define RUN_DCC_ONCE			(0u << 20)

/* The process / voltage encodings of COMP_DW3 (VOLTAGE_INFO at bit 24, PROCESS_INFO at bit 26). */
#define PROCMON_0_85V_DOT_0	((0u << 24) | (0u << 26))
#define PROCMON_0_95V_DOT_0	((1u << 24) | (0u << 26))
#define PROCMON_0_95V_DOT_1	((1u << 24) | (1u << 26))
#define PROCMON_1_05V_DOT_0	((2u << 24) | (0u << 26))
#define PROCMON_1_05V_DOT_1	((2u << 24) | (1u << 26))

/*
 * The procmon reference values of one process and voltage
 * (icl_procmon_values[]: COMP_DW1, COMP_DW9, COMP_DW10).
 *
 * Instances are the rows of the constant table below.
 */
struct i915_procmon {
	uint32_t dw1;
	uint32_t dw9;
	uint32_t dw10;
};

/*
 * icl_procmon_values[]: the reference values per process and voltage.  It
 * never changes.
 */
static const struct i915_procmon i915_procmon_values[5] = {
	{ 0x00000000u, 0x62AB67BBu, 0x51914F96u },	/* 0.85V dot0 */
	{ 0x00000000u, 0x86E172C7u, 0x77CA5EABu },	/* 0.95V dot0 */
	{ 0x00000000u, 0x93F87FE1u, 0x8AE871C5u },	/* 0.95V dot1 */
	{ 0x00000000u, 0x98FA82DDu, 0x89E46DC1u },	/* 1.05V dot0 */
	{ 0x00440000u, 0x9A00AB25u, 0x8AE38FF1u },	/* 1.05V dot1 */
};

/* The buffer translation tables (Linux-derived, constant). */
#include "../data/display-phy-buf-trans.inc"

static unsigned i915_combophy_base(unsigned phy);
static unsigned i915_comp_dw(unsigned phy, unsigned dw);
static unsigned i915_cl_dw(unsigned phy, unsigned dw);
static unsigned i915_phy_misc(unsigned phy);
static unsigned i915_tx_dw8_ln0(unsigned phy);
static unsigned i915_tx_dw8_grp(unsigned phy);
static unsigned i915_pcs_dw1_ln0(unsigned phy);
static unsigned i915_pcs_dw1_grp(unsigned phy);
static int i915_has_phy_misc(unsigned phy);
static int i915_phy_is_master(unsigned phy);
static void i915_phy_rmw(struct i915_mmio *mmio, unsigned reg, uint32_t clear, uint32_t set);
static const struct i915_procmon *i915_get_procmon(struct i915_mmio *mmio, unsigned phy);
static int i915_check_phy_reg(struct i915_mmio *mmio, unsigned reg, uint32_t mask, uint32_t expected);
static int i915_verify_procmon(struct i915_mmio *mmio, unsigned phy);
static void i915_set_procmon(struct i915_mmio *mmio, unsigned phy);
static int i915_combo_phy_enabled(struct i915_mmio *mmio, unsigned phy);
static bool i915_use_edp_hobl(struct intel_encoder *encoder);
static bool i915_use_edp_low_vswing(struct intel_encoder *encoder);
static const struct intel_ddi_buf_trans *i915_intel_get_buf_trans(const struct intel_ddi_buf_trans *trans, int *num_entries);
static const struct intel_ddi_buf_trans *i915_tgl_get_combo_buf_trans_dp(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_tgl_get_combo_buf_trans_edp(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_tgl_get_combo_buf_trans(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_adlp_get_combo_buf_trans_dp(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_adlp_get_combo_buf_trans_edp(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);
static const struct intel_ddi_buf_trans *i915_adlp_get_combo_buf_trans(struct intel_encoder *encoder, const struct intel_crtc_state *crtc_state, int *n_entries);

/*
 * Initializes combo PHYs A and B (intel_combo_phy_init()).
 *
 * The reference is void: this returns 0 whatever it programmed.
 * initialised_out (optional) receives how many PHYs were programmed -- a
 * diagnostic, never an error count.
 */
int
drv_i915_combo_phy_init(
	struct i915_mmio *mmio,
	struct i915_trace *trace,
	unsigned *initialised_out)
{
	unsigned phy;
	unsigned initialised;
	int verified;

	/* for_each_combo_phy: PHY A, then PHY B. */
	initialised = 0u;
	for (phy = 0u; phy < I915_COMBO_PHY_NUM; phy++) {
		/* A PHY whose state is already right is not written again. */
		verified = drv_i915_combo_phy_verify_state(mmio, phy);
		if (verified)
			continue;

		/* Programs the PHY. */
		drv_i915_combo_phy_init_one(mmio, phy);
		initialised++;
	}

	/* Records and logs what was programmed. */
	drv_i915_trace_record(trace, I915_PHY_TRACE_STAGE_NOIRQ, I915_TRACE_ACQUIRE,
		"intel_combo_phy_init", (uint64_t)initialised, 0u);
	kern_logf("i915: P3 intel_combo_phy_init: combo PHYs A/B (%u initialised)\n",
		initialised);

	/* Hands the count to a caller that asked for it. */
	if (initialised_out != NULL)
		*initialised_out = initialised;

	/* Succeeded: the reference is void. */
	return 0;
}

/*
 * Verifies one combo PHY's state (icl_combo_phy_verify_state()): 1 when it
 * is right, 0 when it needs the init.
 *
 * Every check reads its register, whatever an earlier check found.
 */
int
drv_i915_combo_phy_verify_state(
	struct i915_mmio *mmio,
	unsigned phy)
{
	int ret;
	int enabled;
	int ok;
	int master;

	/* A PHY that is not enabled needs the init. */
	enabled = i915_combo_phy_enabled(mmio, phy);
	if (!enabled)
		return 0;

	/* Display version 12+: the ODCC bits. */
	ret = 1;
	ok = i915_check_phy_reg(mmio, i915_tx_dw8_ln0(phy),
		TX_DW8_ODCC_CLK_SEL | TX_DW8_ODCC_CLK_DIV_SEL_MASK,
		TX_DW8_ODCC_CLK_SEL | TX_DW8_ODCC_CLK_DIV_SEL_DIV2);
	if (!ok)
		ret = 0;

	/* And the DCC mode. */
	ok = i915_check_phy_reg(mmio, i915_pcs_dw1_ln0(phy), DCC_MODE_SELECT_MASK, RUN_DCC_ONCE);
	if (!ok)
		ret = 0;

	/* The procmon reference values. */
	ok = i915_verify_procmon(mmio, phy);
	if (!ok)
		ret = 0;

	/* IREFGEN on the master PHY. */
	master = i915_phy_is_master(phy);
	if (master) {
		ok = i915_check_phy_reg(mmio, i915_comp_dw(phy, 8u), IREFGEN, IREFGEN);
		if (!ok)
			ret = 0;
	}

	/* CL_POWER_DOWN_ENABLE. */
	ok = i915_check_phy_reg(mmio, i915_cl_dw(phy, 5u), CL_POWER_DOWN_ENABLE, CL_POWER_DOWN_ENABLE);
	if (!ok)
		ret = 0;

	/* Succeeded: reports whether every check held. */
	return ret;
}

/*
 * Programs one combo PHY (the body of icl_combo_phys_init() for one PHY).
 */
void
drv_i915_combo_phy_init_one(
	struct i915_mmio *mmio,
	unsigned phy)
{
	uint32_t value;
	int has_misc;
	int master;

	/* Clears the DE IO comp power-down (no JSL / EHL mux quirk here). */
	has_misc = i915_has_phy_misc(phy);
	if (has_misc) {
		value = drv_i915_raw_read32(mmio, i915_phy_misc(phy));
		value &= ~PHY_MISC_DE_IO_COMP_PWR_DOWN;
		drv_i915_raw_write32(mmio, i915_phy_misc(phy), value);
	}

	/* Display version 12+: ODCC, read from lane 0 and written to the TX group. */
	value = drv_i915_raw_read32(mmio, i915_tx_dw8_ln0(phy));
	value &= ~TX_DW8_ODCC_CLK_DIV_SEL_MASK;
	value |= TX_DW8_ODCC_CLK_SEL | TX_DW8_ODCC_CLK_DIV_SEL_DIV2;
	drv_i915_raw_write32(mmio, i915_tx_dw8_grp(phy), value);

	/* And DCC, read from lane 0 and written to the PCS group. */
	value = drv_i915_raw_read32(mmio, i915_pcs_dw1_ln0(phy));
	value &= ~DCC_MODE_SELECT_MASK;
	value |= RUN_DCC_ONCE;
	drv_i915_raw_write32(mmio, i915_pcs_dw1_grp(phy), value);

	/* The procmon reference values. */
	i915_set_procmon(mmio, phy);

	/* IREFGEN on the master PHY. */
	master = i915_phy_is_master(phy);
	if (master)
		i915_phy_rmw(mmio, i915_comp_dw(phy, 8u), 0u, IREFGEN);

	/* COMP_INIT and CL_POWER_DOWN_ENABLE. */
	i915_phy_rmw(mmio, i915_comp_dw(phy, 0u), 0u, COMP_INIT);
	i915_phy_rmw(mmio, i915_cl_dw(phy, 5u), 0u, CL_POWER_DOWN_ENABLE);
}

/*
 * Powers a combo PHY port's lanes up (intel_combo_phy_power_up_lanes()).
 */
void
drv_i915_combo_phy_power_up_lanes(
	struct drm_i915_private *dev_priv,
	enum phy phy,
	bool is_dsi,
	int lane_count,
	bool lane_reversal)
{
	u8 lane_mask;

	/* Picks the lanes to keep powered down. */
	if (is_dsi) {
		/* A DSI port has no lane reversal. */
		if (lane_reversal)
			drv_i915_lcd_error("WARN_ON(lane_reversal)\n");

		/* The DSI lanes of the lane count. */
		switch (lane_count) {
		case 1:
			lane_mask = PWR_DOWN_LN_3_1_0;
			break;
		case 2:
			lane_mask = PWR_DOWN_LN_3_1;
			break;
		case 3:
			lane_mask = PWR_DOWN_LN_3;
			break;
		case 4:
			lane_mask = PWR_UP_ALL_LANES;
			break;
		default:
			/* MISSING_CASE, then as for 4 lanes. */
			I915_LCD_MISSING_CASE(lane_count);
			lane_mask = PWR_UP_ALL_LANES;
			break;
		}
	} else {
		/* The DDI lanes of the lane count and reversal. */
		switch (lane_count) {
		case 1:
			lane_mask = PWR_DOWN_LN_3_2_1;
			if (lane_reversal)
				lane_mask = PWR_DOWN_LN_2_1_0;
			break;
		case 2:
			lane_mask = PWR_DOWN_LN_3_2;
			if (lane_reversal)
				lane_mask = PWR_DOWN_LN_1_0;
			break;
		case 4:
			lane_mask = PWR_UP_ALL_LANES;
			break;
		default:
			/* MISSING_CASE, then as for 4 lanes. */
			I915_LCD_MISSING_CASE(lane_count);
			lane_mask = PWR_UP_ALL_LANES;
			break;
		}
	}

	/* Writes the power-down field. */
	(void)i915_lcd_intel_de_rmw(dev_priv, ICL_PORT_CL_DW10(phy),
		PWR_DOWN_LN_MASK, lane_mask);
}

/*
 * Tells whether a buffer translation table is the eDP HOBL one
 * (is_hobl_buf_trans()).
 */
bool
drv_i915_is_hobl_buf_trans(
	const struct intel_ddi_buf_trans *table)
{
	/* Only the table object itself is the HOBL one. */
	if (table != &tgl_combo_phy_trans_edp_hbr2_hobl)
		return false;

	/* Succeeded: it is the HOBL table. */
	return true;
}

/*
 * Binds an encoder's buffer-translation hook as intel_ddi_buf_trans_init()
 * does for a combo PHY: Alder Lake-P (display version 13) and Tiger Lake
 * (version 12) have different translations.
 */
void
drv_i915_lcd_ms_bind_buf_trans(
	struct intel_encoder *encoder)
{
	int display_ver;

	/* Picks the platform's hook. */
	display_ver = drv_i915_lcd_display_ver();
	if (display_ver >= 13)
		encoder->get_buf_trans = i915_adlp_get_combo_buf_trans;
	else
		encoder->get_buf_trans = i915_tgl_get_combo_buf_trans;
}

/* The register base of a combo PHY. */
static unsigned
i915_combophy_base(
	unsigned phy)
{
	/* PHY A has its own block; PHY B the other. */
	if (phy == 0u)
		return COMBOPHY_A;

	/* Succeeded: PHY B's block. */
	return COMBOPHY_B;
}

/* A COMP_DW register of a combo PHY. */
static unsigned
i915_comp_dw(
	unsigned phy,
	unsigned dw)
{
	/* The COMP block of the PHY, one register per dword. */
	return i915_combophy_base(phy) + PORT_COMP + 4u * dw;
}

/* A CL_DW register of a combo PHY. */
static unsigned
i915_cl_dw(
	unsigned phy,
	unsigned dw)
{
	/* The CL block at the PHY's base, one register per dword. */
	return i915_combophy_base(phy) + 4u * dw;
}

/* The PHY_MISC register of a combo PHY. */
static unsigned
i915_phy_misc(
	unsigned phy)
{
	/* PHY A's register. */
	if (phy == 0u)
		return PHY_MISC_A;

	/* Succeeded: PHY B's register. */
	return PHY_MISC_B;
}

/* TX_DW8 of lane 0 (read). */
static unsigned
i915_tx_dw8_ln0(
	unsigned phy)
{
	/* The lane-0 TX block, dword 8. */
	return i915_combophy_base(phy) + PORT_TX_LN0 + 4u * 8u;
}

/* TX_DW8 of the group (written). */
static unsigned
i915_tx_dw8_grp(
	unsigned phy)
{
	/* The group TX block, dword 8. */
	return i915_combophy_base(phy) + PORT_TX_GRP + 4u * 8u;
}

/* PCS_DW1 of lane 0 (read). */
static unsigned
i915_pcs_dw1_ln0(
	unsigned phy)
{
	/* The lane-0 PCS block, dword 1. */
	return i915_combophy_base(phy) + PORT_PCS_LN0 + 4u * 1u;
}

/* PCS_DW1 of the group (written). */
static unsigned
i915_pcs_dw1_grp(
	unsigned phy)
{
	/* The group PCS block, dword 1. */
	return i915_combophy_base(phy) + PORT_PCS_GRP + 4u * 1u;
}

/* Tells whether a combo PHY has PHY_MISC (every one does here). */
static int
i915_has_phy_misc(
	unsigned phy)
{
	UNUSED_PARAMETER(phy);

	/* Succeeded: every combo PHY of these platforms has one. */
	return 1;
}

/* Tells whether a combo PHY is a master (only PHY A here). */
static int
i915_phy_is_master(
	unsigned phy)
{
	/* PHY A is the master. */
	if (phy == 0u)
		return 1;

	/* Succeeded: any other PHY is not. */
	return 0;
}

/* Clears and sets bits of a PHY register with raw accesses. */
static void
i915_phy_rmw(
	struct i915_mmio *mmio,
	unsigned reg,
	uint32_t clear,
	uint32_t set)
{
	uint32_t value;

	/* Reads, changes and writes the register back. */
	value = drv_i915_raw_read32(mmio, reg);
	drv_i915_raw_write32(mmio, reg, (value & ~clear) | set);
}

/* Chooses the procmon values from COMP_DW3's process and voltage (icl_get_procmon_ref_values()). */
static const struct i915_procmon *
i915_get_procmon(
	struct i915_mmio *mmio,
	unsigned phy)
{
	uint32_t value;

	/* Reads the process and voltage the PHY reports. */
	value = drv_i915_raw_read32(mmio, i915_comp_dw(phy, 3u));
	switch (value & (PROCESS_INFO_MASK | VOLTAGE_INFO_MASK)) {
	case PROCMON_0_85V_DOT_0:
		return &i915_procmon_values[0];
	case PROCMON_0_95V_DOT_0:
		return &i915_procmon_values[1];
	case PROCMON_0_95V_DOT_1:
		return &i915_procmon_values[2];
	case PROCMON_1_05V_DOT_0:
		return &i915_procmon_values[3];
	case PROCMON_1_05V_DOT_1:
		return &i915_procmon_values[4];
	default:
		break;
	}

	/* MISSING_CASE falls through to the 0.85V dot0 values (the reference). */
	return &i915_procmon_values[0];
}

/* Tells whether a PHY register's masked bits hold the expected value (1 or 0). */
static int
i915_check_phy_reg(
	struct i915_mmio *mmio,
	unsigned reg,
	uint32_t mask,
	uint32_t expected)
{
	uint32_t value;

	/* Reads the register. */
	value = drv_i915_raw_read32(mmio, reg);
	if ((value & mask) != expected)
		return 0;

	/* Succeeded: the bits hold the value. */
	return 1;
}

/* Verifies the procmon values (every register is read whatever an earlier one held). */
static int
i915_verify_procmon(
	struct i915_mmio *mmio,
	unsigned phy)
{
	const struct i915_procmon *procmon;
	int ret;
	int ok;

	/* Chooses the expected values. */
	procmon = i915_get_procmon(mmio, phy);

	/* COMP_DW1's two procmon bytes. */
	ret = i915_check_phy_reg(mmio, i915_comp_dw(phy, 1u), (0xffu << 16) | 0xffu, procmon->dw1);

	/* COMP_DW9 and COMP_DW10 in full. */
	ok = i915_check_phy_reg(mmio, i915_comp_dw(phy, 9u), 0xffffffffu, procmon->dw9);
	if (!ok)
		ret = 0;
	ok = i915_check_phy_reg(mmio, i915_comp_dw(phy, 10u), 0xffffffffu, procmon->dw10);
	if (!ok)
		ret = 0;

	/* Succeeded: reports whether all three held. */
	return ret;
}

/* Writes the procmon values. */
static void
i915_set_procmon(
	struct i915_mmio *mmio,
	unsigned phy)
{
	const struct i915_procmon *procmon;

	/* Chooses the values from what the PHY reports. */
	procmon = i915_get_procmon(mmio, phy);

	/* COMP_DW1's two bytes, then COMP_DW9 and COMP_DW10. */
	i915_phy_rmw(mmio, i915_comp_dw(phy, 1u), (0xffu << 16) | 0xffu, procmon->dw1);
	drv_i915_raw_write32(mmio, i915_comp_dw(phy, 9u), procmon->dw9);
	drv_i915_raw_write32(mmio, i915_comp_dw(phy, 10u), procmon->dw10);
}

/*
 * Tells whether a combo PHY is enabled (icl_combo_phy_enabled(): PHY_MISC's
 * power-down clear and COMP_DW0's COMP_INIT set): 1 or 0.
 */
static int
i915_combo_phy_enabled(
	struct i915_mmio *mmio,
	unsigned phy)
{
	uint32_t value;
	int has_misc;

	/* Without PHY_MISC only COMP_INIT counts. */
	has_misc = i915_has_phy_misc(phy);
	if (!has_misc) {
		value = drv_i915_raw_read32(mmio, i915_comp_dw(phy, 0u));
		if ((value & COMP_INIT) == 0u)
			return 0;

		/* Succeeded: COMP_INIT is set. */
		return 1;
	}

	/* A powered-down comp is not enabled (COMP_DW0 is then not read). */
	value = drv_i915_raw_read32(mmio, i915_phy_misc(phy));
	if ((value & PHY_MISC_DE_IO_COMP_PWR_DOWN) != 0u)
		return 0;

	/* COMP_INIT must be set. */
	value = drv_i915_raw_read32(mmio, i915_comp_dw(phy, 0u));
	if ((value & COMP_INIT) == 0u)
		return 0;

	/* Succeeded: the PHY is enabled. */
	return 1;
}

/* Tells whether the eDP HOBL table is used (use_edp_hobl(): the VBT asks for it and it has not failed). */
static bool
i915_use_edp_hobl(
	struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp;
	struct intel_connector *connector;

	/* Resolves the DP half and its connector. */
	intel_dp = enc_to_intel_dp(encoder);
	connector = intel_dp->attached_connector;

	/* The VBT must ask for HOBL. */
	if (!connector->panel.vbt.edp.hobl)
		return false;

	/* A link that failed with HOBL does not use it again. */
	if (intel_dp->hobl_failed)
		return false;

	/* Succeeded: HOBL is used. */
	return true;
}

/* Tells whether the VBT asks for the eDP low-vswing table (use_edp_low_vswing()). */
static bool
i915_use_edp_low_vswing(
	struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp;
	struct intel_connector *connector;

	/* Resolves the DP half and its connector. */
	intel_dp = enc_to_intel_dp(encoder);
	connector = intel_dp->attached_connector;

	/* The VBT's low-vswing flag. */
	if (!connector->panel.vbt.edp.low_vswing)
		return false;

	/* Succeeded: the low-vswing table is used. */
	return true;
}

/* Hands a table out with its length (intel_get_buf_trans()). */
static const struct intel_ddi_buf_trans *
i915_intel_get_buf_trans(
	const struct intel_ddi_buf_trans *trans,
	int *num_entries)
{
	/* Reports the length alongside the table. */
	*num_entries = trans->num_entries;

	/* Succeeded: the table is constant and shared. */
	return trans;
}

/* The Tiger Lake DP table of a link rate (tgl_get_combo_buf_trans_dp()). */
static const struct intel_ddi_buf_trans *
i915_tgl_get_combo_buf_trans_dp(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	struct drm_i915_private *dev_priv;
	const struct intel_ddi_buf_trans *table;

	/*
	 * Resolves the device.  IS_TIGERLAKE_UY() of this environment answers
	 * without reading it.
	 */
	dev_priv = i915_lcd_to_i915(encoder->base.dev);
	UNUSED_PARAMETER(dev_priv);

	/* Above HBR the HBR2 table (the UY SKUs have their own); HBR and below the HBR table. */
	if (crtc_state->port_clock > 270000) {
		if (IS_TIGERLAKE_UY(dev_priv)) {
			table = i915_intel_get_buf_trans(&tgl_uy_combo_phy_trans_dp_hbr2,
							 n_entries);
		} else {
			table = i915_intel_get_buf_trans(&tgl_combo_phy_trans_dp_hbr2,
							 n_entries);
		}
	} else {
		table = i915_intel_get_buf_trans(&tgl_combo_phy_trans_dp_hbr,
						 n_entries);
	}

	/* Succeeded: reports the table. */
	return table;
}

/* The Tiger Lake eDP table of a link rate and the panel's VBT (tgl_get_combo_buf_trans_edp()). */
static const struct intel_ddi_buf_trans *
i915_tgl_get_combo_buf_trans_edp(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;
	bool hobl;
	bool low_vswing;

	/* Above HBR2 the HBR3 table. */
	if (crtc_state->port_clock > 540000) {
		table = i915_intel_get_buf_trans(&icl_combo_phy_trans_dp_hbr2_edp_hbr3,
						 n_entries);
		return table;
	}

	/* HOBL when the VBT asks for it and it has not failed. */
	hobl = i915_use_edp_hobl(encoder);
	if (hobl) {
		table = i915_intel_get_buf_trans(&tgl_combo_phy_trans_edp_hbr2_hobl,
						 n_entries);
		return table;
	}

	/* The low-vswing table when the VBT asks for it. */
	low_vswing = i915_use_edp_low_vswing(encoder);
	if (low_vswing) {
		table = i915_intel_get_buf_trans(&icl_combo_phy_trans_edp_hbr2,
						 n_entries);
		return table;
	}

	/* Otherwise the DP table. */
	table = i915_tgl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

/* The Tiger Lake combo PHY table of an output (tgl_get_combo_buf_trans()). */
static const struct intel_ddi_buf_trans *
i915_tgl_get_combo_buf_trans(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;
	bool is_hdmi;
	bool is_edp;

	/* HDMI has its own table. */
	is_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (is_hdmi) {
		table = i915_intel_get_buf_trans(&icl_combo_phy_trans_hdmi, n_entries);
		return table;
	}

	/* eDP picks by rate and VBT, DP by rate. */
	is_edp = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP);
	if (is_edp)
		table = i915_tgl_get_combo_buf_trans_edp(encoder, crtc_state, n_entries);
	else
		table = i915_tgl_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

/* The Alder Lake-P DP table of a link rate (adlp_get_combo_buf_trans_dp()). */
static const struct intel_ddi_buf_trans *
i915_adlp_get_combo_buf_trans_dp(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;

	UNUSED_PARAMETER(encoder);

	/* Above HBR the HBR2 / HBR3 table, HBR and below the HBR table. */
	if (crtc_state->port_clock > 270000)
		table = i915_intel_get_buf_trans(&adlp_combo_phy_trans_dp_hbr2_hbr3, n_entries);
	else
		table = i915_intel_get_buf_trans(&adlp_combo_phy_trans_dp_hbr, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

/* The Alder Lake-P eDP table of a link rate and the panel's VBT (adlp_get_combo_buf_trans_edp()). */
static const struct intel_ddi_buf_trans *
i915_adlp_get_combo_buf_trans_edp(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;
	bool hobl;
	bool low_vswing;

	/* Above HBR2 the HBR3 table. */
	if (crtc_state->port_clock > 540000) {
		table = i915_intel_get_buf_trans(&adlp_combo_phy_trans_edp_hbr3,
						 n_entries);
		return table;
	}

	/* HOBL when the VBT asks for it and it has not failed. */
	hobl = i915_use_edp_hobl(encoder);
	if (hobl) {
		table = i915_intel_get_buf_trans(&tgl_combo_phy_trans_edp_hbr2_hobl,
						 n_entries);
		return table;
	}

	/* The low-vswing table when the VBT asks for it. */
	low_vswing = i915_use_edp_low_vswing(encoder);
	if (low_vswing) {
		table = i915_intel_get_buf_trans(&adlp_combo_phy_trans_edp_up_to_hbr2,
						 n_entries);
		return table;
	}

	/* Otherwise the DP table. */
	table = i915_adlp_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

/* The Alder Lake-P combo PHY table of an output (adlp_get_combo_buf_trans()). */
static const struct intel_ddi_buf_trans *
i915_adlp_get_combo_buf_trans(
	struct intel_encoder *encoder,
	const struct intel_crtc_state *crtc_state,
	int *n_entries)
{
	const struct intel_ddi_buf_trans *table;
	bool is_hdmi;
	bool is_edp;

	/* HDMI has its own table. */
	is_hdmi = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI);
	if (is_hdmi) {
		table = i915_intel_get_buf_trans(&icl_combo_phy_trans_hdmi, n_entries);
		return table;
	}

	/* eDP picks by rate and VBT, DP by rate. */
	is_edp = intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP);
	if (is_edp)
		table = i915_adlp_get_combo_buf_trans_edp(encoder, crtc_state, n_entries);
	else
		table = i915_adlp_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);

	/* Succeeded: reports the table. */
	return table;
}

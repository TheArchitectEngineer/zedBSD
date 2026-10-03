/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2022 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_combo_phy_regs.h
 * (sha256 b5f41421ca2590dd705ace3198f8db19ce60768a2a76cba58feb43930601ecff) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_MREG_COMBO_PHY_H
#define PARITY_LCD_MREG_COMBO_PHY_H

#define _ICL_COMBOPHY_A				0x162000
#define _ICL_COMBOPHY_B				0x6C000
#define _EHL_COMBOPHY_C				0x160000
#define _RKL_COMBOPHY_D				0x161000
#define _ADL_COMBOPHY_E				0x16B000
#define _ICL_COMBOPHY(phy)			_PICK(phy, _ICL_COMBOPHY_A, \
						      _ICL_COMBOPHY_B, \
						      _EHL_COMBOPHY_C, \
						      _RKL_COMBOPHY_D, \
						      _ADL_COMBOPHY_E)
#define _ICL_PORT_CL_DW(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 4 * (dw))
#define ICL_PORT_CL_DW5(phy)			_MMIO(_ICL_PORT_CL_DW(5, phy))
#define   SUS_CLOCK_CONFIG			(3 << 0)
#define ICL_PORT_CL_DW10(phy)			_MMIO(_ICL_PORT_CL_DW(10, phy))
#define  PWR_UP_ALL_LANES			(0x0 << 4)
#define  PWR_DOWN_LN_3_2_1			(0xe << 4)
#define  PWR_DOWN_LN_3_2			(0xc << 4)
#define  PWR_DOWN_LN_3				(0x8 << 4)
#define  PWR_DOWN_LN_2_1_0			(0x7 << 4)
#define  PWR_DOWN_LN_1_0			(0x3 << 4)
#define  PWR_DOWN_LN_3_1			(0xa << 4)
#define  PWR_DOWN_LN_3_1_0			(0xb << 4)
#define  PWR_DOWN_LN_MASK			(0xf << 4)
#define  EDP4K2K_MODE_OVRD_EN			(1 << 3)
#define  EDP4K2K_MODE_OVRD_OPTIMIZED		(1 << 2)
#define _ICL_PORT_PCS_GRP			0x600
#define _ICL_PORT_PCS_LN(ln)			(0x800 + (ln) * 0x100)
#define _ICL_PORT_PCS_DW_GRP(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_PCS_GRP + 4 * (dw))
#define _ICL_PORT_PCS_DW_LN(dw, ln, phy)	 (_ICL_COMBOPHY(phy) + \
						  _ICL_PORT_PCS_LN(ln) + 4 * (dw))
#define ICL_PORT_PCS_DW1_GRP(phy)		_MMIO(_ICL_PORT_PCS_DW_GRP(1, phy))
#define ICL_PORT_PCS_DW1_LN(ln, phy)		_MMIO(_ICL_PORT_PCS_DW_LN(1, ln, phy))
#define   COMMON_KEEPER_EN			(1 << 26)
#define _ICL_PORT_TX_GRP			0x680
#define _ICL_PORT_TX_LN(ln)			(0x880 + (ln) * 0x100)
#define _ICL_PORT_TX_DW_GRP(dw, phy)		(_ICL_COMBOPHY(phy) + \
						 _ICL_PORT_TX_GRP + 4 * (dw))
#define _ICL_PORT_TX_DW_LN(dw, ln, phy) 	(_ICL_COMBOPHY(phy) + \
						  _ICL_PORT_TX_LN(ln) + 4 * (dw))
#define ICL_PORT_TX_DW2_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(2, ln, phy))
#define   SWING_SEL_UPPER(x)			(((x) >> 3) << 15)
#define   SWING_SEL_UPPER_MASK			(1 << 15)
#define   SWING_SEL_LOWER(x)			(((x) & 0x7) << 11)
#define   SWING_SEL_LOWER_MASK			(0x7 << 11)
#define   RCOMP_SCALAR(x)			((x) << 0)
#define   RCOMP_SCALAR_MASK			(0xFF << 0)
#define ICL_PORT_TX_DW4_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(4, ln, phy))
#define   LOADGEN_SELECT			(1 << 31)
#define   POST_CURSOR_1(x)			((x) << 12)
#define   POST_CURSOR_1_MASK			(0x3F << 12)
#define   POST_CURSOR_2(x)			((x) << 6)
#define   POST_CURSOR_2_MASK			(0x3F << 6)
#define   CURSOR_COEFF(x)			((x) << 0)
#define   CURSOR_COEFF_MASK			(0x3F << 0)
#define ICL_PORT_TX_DW5_GRP(phy)		_MMIO(_ICL_PORT_TX_DW_GRP(5, phy))
#define ICL_PORT_TX_DW5_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(5, ln, phy))
#define   TX_TRAINING_EN			(1 << 31)
#define   TAP2_DISABLE				(1 << 30)
#define   TAP3_DISABLE				(1 << 29)
#define   SCALING_MODE_SEL(x)			((x) << 18)
#define   SCALING_MODE_SEL_MASK			(0x7 << 18)
#define   RTERM_SELECT(x)			((x) << 3)
#define   RTERM_SELECT_MASK			(0x7 << 3)
#define ICL_PORT_TX_DW7_LN(ln, phy)		_MMIO(_ICL_PORT_TX_DW_LN(7, ln, phy))
#define   N_SCALAR(x)				((x) << 24)
#define   N_SCALAR_MASK				(0x7F << 24)

#endif /* PARITY_LCD_MREG_COMBO_PHY_H */

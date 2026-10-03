/* SPDX-License-Identifier: MIT
 *
 * Copyright © 2023 Intel Corporation
 */

/*
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_cx0_phy_regs.h
 * (sha256 f81421088d8c61c7eb760177ae5752458985389a1a4c4a2f7b9e1026c7d934f7) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
#ifndef PARITY_LCD_MREG_CX0_H
#define PARITY_LCD_MREG_CX0_H

#define _XELPDP_PORT_BUF_CTL1_LN0_A			0x64004
#define _XELPDP_PORT_BUF_CTL1_LN0_B			0x64104
#define _XELPDP_PORT_BUF_CTL1_LN0_USBC1			0x16F200
#define _XELPDP_PORT_BUF_CTL1_LN0_USBC2			0x16F400
#define XELPDP_PORT_BUF_CTL1(port)			_MMIO(_PICK_EVEN_2RANGES(port, PORT_TC1, \
										 _XELPDP_PORT_BUF_CTL1_LN0_A, \
										 _XELPDP_PORT_BUF_CTL1_LN0_B, \
										 _XELPDP_PORT_BUF_CTL1_LN0_USBC1, \
										 _XELPDP_PORT_BUF_CTL1_LN0_USBC2))
#define   XELPDP_PORT_BUF_IO_SELECT_TBT			REG_BIT(11)
#define   XELPDP_PORT_BUF_PHY_IDLE			REG_BIT(7)

#endif /* PARITY_LCD_MREG_CX0_H */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright © 2006-2016 Intel Corporation
 * Copyright © 2008 Keith Packard
 * Copyright © 2022 Intel Corporation
 * Copyright © 2023 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * The display 9 registers and constants the Gen9 mode set
 * (display/gen9-modeset.c, gen9-link.c, gen9-ddc.c) programs: the power
 * wells and their fuses, the DPLLs, the DDI buffers and their translation
 * tables, the DP transport, the AUX channels, the GMBUS, the transcoder
 * timings and clocks, the pipe, the watermarks and the DDB, the south
 * display's hotplug, and the DisplayPort DPCD addresses the link training
 * uses.
 *
 * Modified for zedBSD: excerpted by hand from the Linux kernel 6.8.12 and
 * renamed (I915_SKL_*, I915_DPCD_*); per-port, per-pipe and per-DPLL forms
 * are written as offsets from the first one.  The translation tables are
 * copied row by row.  Values are unchanged.  Sources (SHA-256):
 *   drivers/gpu/drm/i915/i915_reg.h
 *     efcb6385e32031ca3c5b97f7691c5ed1706c2a6709e3875ff78c2cf2eff0e626
 *   drivers/gpu/drm/i915/display/intel_dp_aux_regs.h (SPDX MIT)
 *     0db768cbf192dbfe7539aa9d538ec4832aecb8e81f75a4c86b2e6c7a946ec748
 *   drivers/gpu/drm/i915/display/intel_gmbus_regs.h (SPDX MIT)
 *     ce5f6978df207e8322d0527a4604fef8426f7686d7265018368b19aa0d6823ba
 *   drivers/gpu/drm/i915/display/skl_watermark_regs.h (SPDX MIT)
 *     5e4dba2b538c68ea5139e1cee3b25ea9e5b6f0de14b2512e1924e5b773e3f058
 *   drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c (SPDX MIT; the SKL,
 *   KBL and eDP tables, lines 146-358)
 *     fcf9694d241716a994e07255869c23f31a9749da183b38b66f3d4ba76ce6ddfd
 *   include/drm/display/drm_dp.h (the Keith Packard notice: permission to
 *   use, copy, modify, distribute and sell)
 *     306a1a47ba001c417baa3dab1f3a58c7e5de3c7a0537d26806a130603b599c5f
 */

#ifndef DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_GEN9_MODESET_H
#define DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_GEN9_MODESET_H

/* The DDI ports: A (the panel's) to D; port E shares A's lanes. */
#define I915_SKL_PORT_A				0U
#define I915_SKL_PORT_B				1U
#define I915_SKL_PORT_C				2U
#define I915_SKL_PORT_D				3U
#define I915_SKL_PORTS				4U

/* The distance between two DDI ports' registers (DDI_BUF_CTL, DP_TP_CTL, AUX). */
#define I915_SKL_PORT_STRIDE			0x100U

/* The power well request bits beside their state bits (HSW_PWR_WELL_CTL_REQ of an index). */
#define I915_SKL_PW_2_REQUEST			(1U << 31)
#define I915_SKL_PW_DDI_REQUEST(port)		(2U << (2U * ((port) + 1U)))
#define I915_SKL_PW_DDI_STATE(port)		(1U << (2U * ((port) + 1U)))

/* The power gates' fuse distribution (SKL_FUSE_STATUS, SKL_FUSE_PG_DIST_STATUS of PG0 to PG2). */
#define I915_SKL_FUSE_STATUS			0x42000U
#define I915_SKL_FUSE_PG_DIST(pg)		(1U << (27U - (pg)))

/* The DPLL enables (LCPLL1_CTL for DPLL0, LCPLL2_CTL, WRPLL_CTL1, WRPLL_CTL2), bit 31 each. */
#define I915_SKL_DPLL0_CTL			0x46010U
#define I915_SKL_DPLL1_CTL			0x46014U
#define I915_SKL_DPLL2_CTL			0x46040U
#define I915_SKL_DPLL3_CTL			0x46060U
#define I915_SKL_DPLL_ENABLE			(1U << 31)

/* The DPLLs' configurations for HDMI (DPLL_CFGCR1 and _CFGCR2 of DPLL1 to DPLL3), 8 bytes apart. */
#define I915_SKL_DPLL1_CFGCR1			0x6c040U
#define I915_SKL_DPLL1_CFGCR2			0x6c044U
#define I915_SKL_DPLL_CFGCR_STRIDE		0x8U
#define I915_SKL_CFGCR1_FREQ_ENABLE		(1U << 31)
#define I915_SKL_CFGCR1_DCO_FRACTION_SHIFT	9U
#define I915_SKL_CFGCR2_QDIV_RATIO_SHIFT	8U
#define I915_SKL_CFGCR2_QDIV_MODE_SHIFT		7U
#define I915_SKL_CFGCR2_KDIV_SHIFT		5U
#define I915_SKL_CFGCR2_PDIV_SHIFT		2U

/* The DPLL lock status (DPLL_STATUS, DPLL_LOCK of a DPLL). */
#define I915_SKL_DPLL_STATUS			0x6c060U
#define I915_SKL_DPLL_LOCK(id)			(1U << ((id) * 8U))

/* DPLL_CTRL2's per-port clock off bit. */
#define I915_SKL_DPLL_CTRL2_CLK_OFF(port)	(1U << ((port) + 15U))

/* The DDI buffer (DDI_BUF_CTL of port A; another port is a stride further). */
#define I915_SKL_DDI_BUF_CTL_A			0x64000U
#define I915_SKL_DDI_BUF_ENABLE			(1U << 31)
#define I915_SKL_DDI_BUF_TRANS_SELECT_SHIFT	24U
#define I915_SKL_DDI_BUF_EMP_MASK		(0xfU << 24)
#define I915_SKL_DDI_BUF_PORT_REVERSAL		(1U << 16)
#define I915_SKL_DDI_BUF_IS_IDLE		(1U << 7)
#define I915_SKL_DDI_A_4_LANES			(1U << 4)
#define I915_SKL_DDI_PORT_WIDTH_SHIFT		1U
#define I915_SKL_DDI_INIT_DISPLAY_DETECTED	(1U << 0)

/* The DDI buffer translation entries (DDI_BUF_TRANS_LO and _HI of port A, entry i at 8 i), 0x60 apart per port. */
#define I915_SKL_DDI_BUF_TRANS_A		0x64e00U
#define I915_SKL_DDI_BUF_TRANS_STRIDE		0x60U
#define I915_SKL_DDI_BUF_BALANCE_LEG_ENABLE	(1U << 31)

/* The current boost (DISPIO_CR_TX_BMU_CR0): a port's balance leg and its disable. */
#define I915_SKL_DISPIO_CR_TX_BMU_CR0		0x6c00cU
#define I915_SKL_BALANCE_LEG_SHIFT(port)	(8U + 3U * (port))
#define I915_SKL_BALANCE_LEG_MASK(port)		(7U << (8U + 3U * (port)))
#define I915_SKL_BALANCE_LEG_DISABLE(port)	(1U << (23U + (port)))

/* The DP transport (DP_TP_CTL and DP_TP_STATUS of port A). */
#define I915_SKL_DP_TP_CTL_A			0x64040U
#define I915_SKL_DP_TP_STATUS_A			0x64044U
#define I915_SKL_DP_TP_ENABLE			(1U << 31)
#define I915_SKL_DP_TP_ENHANCED_FRAME		(1U << 18)
#define I915_SKL_DP_TP_TRAIN_MASK		(7U << 8)
#define I915_SKL_DP_TP_TRAIN_PAT1		(0U << 8)
#define I915_SKL_DP_TP_TRAIN_PAT2		(1U << 8)
#define I915_SKL_DP_TP_TRAIN_IDLE		(2U << 8)
#define I915_SKL_DP_TP_TRAIN_NORMAL		(3U << 8)
#define I915_SKL_DP_TP_TRAIN_PAT3		(4U << 8)

/* The AUX channel of port A (DP_AUX_CH_CTL, DP_AUX_CH_DATA1 to 5). */
#define I915_SKL_AUX_CTL_A			0x64010U
#define I915_SKL_AUX_DATA_A			0x64014U
#define I915_SKL_AUX_DATA_REGISTERS		5U
#define I915_SKL_AUX_SEND_BUSY			(1U << 31)
#define I915_SKL_AUX_DONE			(1U << 30)
#define I915_SKL_AUX_INTERRUPT			(1U << 29)
#define I915_SKL_AUX_TIME_OUT_ERROR		(1U << 28)
#define I915_SKL_AUX_TIME_OUT_MAX		(3U << 26)
#define I915_SKL_AUX_RECEIVE_ERROR		(1U << 25)
#define I915_SKL_AUX_MESSAGE_SIZE_SHIFT		20U
#define I915_SKL_AUX_MESSAGE_SIZE_MASK		0x1fU
#define I915_SKL_AUX_FW_SYNC_PULSE_SHIFT	5U
#define I915_SKL_AUX_SYNC_PULSE_SHIFT		0U

/* The AUX sync lengths (intel_dp_aux_sync_len, _fw_sync_len): precharge and preamble. */
#define I915_SKL_AUX_SYNC_LENGTH		32U
#define I915_SKL_AUX_FW_SYNC_LENGTH		18U

/* The GMBUS of the PCH (GMBUS0 to 5 at PCH_DISPLAY_BASE + 0x5100). */
#define I915_SKL_GMBUS0				0xc5100U
#define I915_SKL_GMBUS1				0xc5104U
#define I915_SKL_GMBUS2				0xc5108U
#define I915_SKL_GMBUS3				0xc510cU
#define I915_SKL_GMBUS4				0xc5110U
#define I915_SKL_GMBUS5				0xc5120U
#define I915_SKL_GMBUS_RATE_100KHZ		(0U << 8)
#define I915_SKL_GMBUS_SW_CLR_INT		(1U << 31)
#define I915_SKL_GMBUS_SW_RDY			(1U << 30)
#define I915_SKL_GMBUS_CYCLE_WAIT		(1U << 25)
#define I915_SKL_GMBUS_CYCLE_INDEX		(2U << 25)
#define I915_SKL_GMBUS_CYCLE_STOP		(4U << 25)
#define I915_SKL_GMBUS_BYTE_COUNT_SHIFT		16U
#define I915_SKL_GMBUS_SLAVE_INDEX_SHIFT	8U
#define I915_SKL_GMBUS_SLAVE_ADDR_SHIFT		1U
#define I915_SKL_GMBUS_SLAVE_READ		(1U << 0)
#define I915_SKL_GMBUS_HW_WAIT_PHASE		(1U << 14)
#define I915_SKL_GMBUS_HW_RDY			(1U << 11)
#define I915_SKL_GMBUS_SATOER			(1U << 10)
#define I915_SKL_GMBUS_ACTIVE			(1U << 9)

/* The GMBUS pins of the HDMI ports (gmbus_pins_skl: DPC 4, DPB 5, DPD 6). */
#define I915_SKL_GMBUS_PIN_DPC			4U
#define I915_SKL_GMBUS_PIN_DPB			5U
#define I915_SKL_GMBUS_PIN_DPD			6U

/* The PCH's GMBUS clock gating (SOUTH_DSPCLK_GATE_D, Display WA #0868). */
#define I915_SKL_SOUTH_DSPCLK_GATE_D		0xc2020U
#define I915_SKL_PCH_GMBUSUNIT_CLOCK_GATE_DISABLE	(1U << 31)

/* The rest of the transcoder timing (TRANS_HBLANK, _HSYNC, _VBLANK, _VSYNC, _VSYNCSHIFT, _MULT of A). */
#define I915_SKL_TRANS_HBLANK_A			0x60004U
#define I915_SKL_TRANS_HSYNC_A			0x60008U
#define I915_SKL_TRANS_VBLANK_A			0x60010U
#define I915_SKL_TRANS_VSYNC_A			0x60014U
#define I915_SKL_TRANS_VSYNCSHIFT_A		0x60028U
#define I915_SKL_TRANS_MULT_A			0x6002cU

/* The transcoder's data M and N (PIPE_DATA_M1, _N1 of A) and the TU size field. */
#define I915_SKL_DATA_M1_A			0x60030U
#define I915_SKL_DATA_N1_A			0x60034U
#define I915_SKL_TU_SIZE_SHIFT			25U
#define I915_SKL_DATA_LINK_N_MAX		0x800000U

/* The transcoder's MSA (TRANS_MSA_MISC of A) and its bits (DP_MSA_MISC_SYNC_CLOCK, _8_BPC). */
#define I915_SKL_TRANS_MSA_MISC_A		0x60410U
#define I915_SKL_MSA_MISC_SYNC_CLOCK		(1U << 0)
#define I915_SKL_MSA_MISC_8_BPC			(1U << 5)

/* The transcoder's clock (TRANS_CLK_SEL of A, B 4 bytes on): port x + 1 in 31:29, 0 off. */
#define I915_SKL_TRANS_CLK_SEL_A		0x46140U
#define I915_SKL_TRANS_CLK_SEL_STRIDE		0x4U
#define I915_SKL_TRANS_CLK_SEL_PORT_SHIFT	29U

/* The DDI function's port, mode and width fields beside those of display-regs-gen9.h. */
#define I915_SKL_TRANS_DDI_MODE_HDMI		0U
#define I915_SKL_TRANS_DDI_MODE_DVI		1U
#define I915_SKL_TRANS_DDI_BPC_8		(0U << 20)
#define I915_SKL_TRANS_DDI_PVSYNC		(1U << 17)
#define I915_SKL_TRANS_DDI_PHSYNC		(1U << 16)

/* The pipe's colour depth and dither (PIPE_MISC of A). */
#define I915_SKL_PIPE_MISC_A			0x70030U
#define I915_SKL_PIPE_MISC_BPC_8		(0U << 5)

/* A transcoder's chicken bits (CHICKEN_TRANS A 0x420c0, B 0x420c4, C 0x420c8, EDP 0x420cc) and DDI training override. */
#define I915_SKL_CHICKEN_TRANS_A		0x420c0U
#define I915_SKL_CHICKEN_TRANS_EDP		0x420ccU
#define I915_SKL_DDI_TRAINING_OVERRIDE_ENABLE	(1U << 19)
#define I915_SKL_DDI_TRAINING_OVERRIDE_VALUE	(1U << 18)

/* The cursor plane's control (CURACNTR), the pipe line time (WM_LINETIME) and its field. */
#define I915_SKL_CURCNTR_A			0x70080U
#define I915_SKL_WM_LINETIME_A			0x45270U
#define I915_SKL_WM_LINETIME_STRIDE		0x4U
#define I915_SKL_LINETIME_MASK			0x1ffU

/* The watermarks of the first plane (PLANE_WM_1 level 0, _TRANS) and of the cursor (CUR_WM 0, _TRANS). */
#define I915_SKL_PLANE_WM_1_A_0			0x70240U
#define I915_SKL_PLANE_WM_TRANS_1_A		0x70268U
#define I915_SKL_CUR_WM_A_0			0x70140U
#define I915_SKL_CUR_WM_TRANS_A			0x70168U
#define I915_SKL_WM_ENABLE			(1U << 31)
#define I915_SKL_WM_LINES_SHIFT			14U
#define I915_SKL_WM_BLOCKS_MASK			0xfffU

/* The DDB of the first plane and of the cursor (PLANE_BUF_CFG_1, CUR_BUF_CFG): start 11:0, end 27:16. */
#define I915_SKL_PLANE_BUF_CFG_1_A		0x7027cU
#define I915_SKL_PLANE_NV12_BUF_CFG_1_A		0x70278U
#define I915_SKL_CUR_BUF_CFG_A			0x7017cU
#define I915_SKL_BUF_END_SHIFT			16U

/* The DBUF of display 9: 896 blocks less 4 for the bypass path, one slice (DBUF_CTL). */
#define I915_SKL_DBUF_BLOCKS			892U
#define I915_SKL_DBUF_CTL			0x45008U
#define I915_SKL_DBUF_POWER_REQUEST		(1U << 31)
#define I915_SKL_DBUF_POWER_STATE		(1U << 30)

/* The watermark levels of display 9 and the most lines a level holds. */
#define I915_SKL_WM_LEVELS			8U
#define I915_SKL_WM_MAX_LINES			31U

/* The PCODE read of the memory latencies (GEN9_PCODE_READ_MEM_LATENCY). */
#define I915_SKL_PCODE_READ_MEM_LATENCY		0x6U

/* The south display's interrupts (SDEISR, _IMR, _IIR, _IER) and the ports' hotplug bits. */
#define I915_SKL_SDEISR				0xc4000U
#define I915_SKL_SDEIMR				0xc4004U
#define I915_SKL_SDEIIR				0xc4008U
#define I915_SKL_SDEIER				0xc400cU
#define I915_SKL_SDE_PORTB_HOTPLUG		(1U << 21)
#define I915_SKL_SDE_PORTC_HOTPLUG		(1U << 22)
#define I915_SKL_SDE_PORTD_HOTPLUG		(1U << 23)

/* The PCH's digital hotplug (PCH_PORT_HOTPLUG) and its per-port enables. */
#define I915_SKL_PCH_PORT_HOTPLUG		0xc4030U
#define I915_SKL_PORTB_HOTPLUG_ENABLE		(1U << 4)
#define I915_SKL_PORTC_HOTPLUG_ENABLE		(1U << 12)
#define I915_SKL_PORTD_HOTPLUG_ENABLE		(1U << 20)

/* The ports the board has (SFUSE_STRAP): B, C and D detected. */
#define I915_SKL_SFUSE_STRAP			0xc2014U
#define I915_SKL_SFUSE_STRAP_DDIB_DETECTED	(1U << 2)
#define I915_SKL_SFUSE_STRAP_DDIC_DETECTED	(1U << 1)
#define I915_SKL_SFUSE_STRAP_DDID_DETECTED	(1U << 0)

/*
 * The DPCD (drm_dp.h): the receiver capabilities, the link configuration,
 * the training and the link status.
 */
#define I915_DPCD_REV				0x000U
#define I915_DPCD_MAX_LINK_RATE			0x001U
#define I915_DPCD_MAX_LANE_COUNT		0x002U
#define I915_DPCD_LANE_COUNT_MASK		0x1fU
#define I915_DPCD_ENHANCED_FRAME_CAP		(1U << 7)
#define I915_DPCD_TPS3_SUPPORTED		(1U << 6)
#define I915_DPCD_TRAINING_AUX_RD_INTERVAL	0x00eU
#define I915_DPCD_TRAINING_AUX_RD_MASK		0x7fU
#define I915_DPCD_RECEIVER_CAP_SIZE		0x0fU
#define I915_DPCD_LINK_BW_SET			0x100U
#define I915_DPCD_LANE_COUNT_SET		0x101U
#define I915_DPCD_LANE_COUNT_ENHANCED_FRAME_EN	(1U << 7)
#define I915_DPCD_TRAINING_PATTERN_SET		0x102U
#define I915_DPCD_TRAINING_PATTERN_DISABLE	0U
#define I915_DPCD_TRAINING_PATTERN_1		1U
#define I915_DPCD_TRAINING_PATTERN_2		2U
#define I915_DPCD_TRAINING_PATTERN_3		3U
#define I915_DPCD_LINK_SCRAMBLING_DISABLE	(1U << 5)
#define I915_DPCD_TRAINING_LANE0_SET		0x103U
#define I915_DPCD_TRAIN_VOLTAGE_SWING_MASK	0x3U
#define I915_DPCD_TRAIN_MAX_SWING_REACHED	(1U << 2)
#define I915_DPCD_TRAIN_PRE_EMPHASIS_SHIFT	3U
#define I915_DPCD_TRAIN_PRE_EMPHASIS_MASK	(3U << 3)
#define I915_DPCD_TRAIN_MAX_PRE_EMPHASIS_REACHED	(1U << 5)
#define I915_DPCD_DOWNSPREAD_CTRL		0x107U
#define I915_DPCD_MAIN_LINK_CHANNEL_CODING_SET	0x108U
#define I915_DPCD_SET_ANSI_8B10B		1U
#define I915_DPCD_LANE0_1_STATUS		0x202U
#define I915_DPCD_LINK_STATUS_SIZE		6U
#define I915_DPCD_LANE_CR_DONE			(1U << 0)
#define I915_DPCD_LANE_CHANNEL_EQ_DONE		(1U << 1)
#define I915_DPCD_LANE_SYMBOL_LOCKED		(1U << 2)
#define I915_DPCD_INTERLANE_ALIGN_DONE		(1U << 0)
#define I915_DPCD_SET_POWER			0x600U
#define I915_DPCD_SET_POWER_D0			1U
#define I915_DPCD_SET_POWER_D3			2U

/* The DPCD link rate codes (DP_LINK_BW_1_62, _2_7, _5_4). */
#define I915_DPCD_LINK_BW_1_62			0x06U
#define I915_DPCD_LINK_BW_2_7			0x0aU
#define I915_DPCD_LINK_BW_5_4			0x14U

/* The AUX requests and replies (DP_AUX_I2C_WRITE, _READ, _MOT, NATIVE_WRITE, _READ, the replies' fields). */
#define I915_DP_AUX_I2C_WRITE			0x0U
#define I915_DP_AUX_I2C_READ			0x1U
#define I915_DP_AUX_I2C_MOT			0x4U
#define I915_DP_AUX_NATIVE_WRITE		0x8U
#define I915_DP_AUX_NATIVE_READ			0x9U
#define I915_DP_AUX_NATIVE_REPLY_MASK		0x3U
#define I915_DP_AUX_NATIVE_REPLY_ACK		0x0U
#define I915_DP_AUX_NATIVE_REPLY_DEFER		0x2U
#define I915_DP_AUX_I2C_REPLY_MASK		(0x3U << 2)
#define I915_DP_AUX_I2C_REPLY_ACK		(0x0U << 2)
#define I915_DP_AUX_I2C_REPLY_DEFER		(0x2U << 2)

/* The DDC address of an EDID, and the size of one EDID block. */
#define I915_DDC_ADDR				0x50U
#define I915_EDID_BLOCK_BYTES			128U

#endif /* DRIVERS_GPU_I915_INTEL_DISPLAY_REGS_GEN9_MODESET_H */

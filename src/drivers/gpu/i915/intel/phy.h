/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2020 Intel Corporation
 */

/*
 * The DDI buffer translation of Linux intel_ddi_buf_trans.h/.c: the entry
 * and table types, for the modeset environment, and the DKL PHY tables of
 * the Type-C ports, which display/ddi.c reads.  Every table is constant
 * and shared by every device.
 *
 * The buffer translation types (Linux intel_ddi_buf_trans.h).
 * Definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_ddi_buf_trans.h
 * (sha256 ec4433d9f675b37e769fc219c753ee57f7404742812dbf0fe39d4c494fbdd09c) by tools/port_lcd_calc.py:
 * buffer translation entry types .. struct intel_ddi_buf_trans.
 * The notice above is the source file's own.
 */

#ifndef DRIVERS_GPU_I915_INTEL_PHY_H
#define DRIVERS_GPU_I915_INTEL_PHY_H

/* The buffer translation types (Linux intel_ddi_buf_trans.h). */

struct hsw_ddi_buf_trans {
	u32 trans1;	/* balance leg enable, de-emph level */
	u32 trans2;	/* vref sel, vswing */
	u8 i_boost;	/* SKL: I_boost; valid: 0x0, 0x1, 0x3, 0x7 */
};

struct bxt_ddi_buf_trans {
	u8 margin;	/* swing value */
	u8 scale;	/* scale value */
	u8 enable;	/* scale enable */
	u8 deemphasis;
};

struct icl_ddi_buf_trans {
	u8 dw2_swing_sel;
	u8 dw7_n_scalar;
	u8 dw4_cursor_coeff;
	u8 dw4_post_cursor_2;
	u8 dw4_post_cursor_1;
};

struct icl_mg_phy_ddi_buf_trans {
	u8 cri_txdeemph_override_11_6;
	u8 cri_txdeemph_override_5_0;
	u8 cri_txdeemph_override_17_12;
};

struct tgl_dkl_phy_ddi_buf_trans {
	u8 vswing;
	u8 preshoot;
	u8 de_emphasis;
};

struct dg2_snps_phy_buf_trans {
	u8 vswing;
	u8 pre_cursor;
	u8 post_cursor;
};

union intel_ddi_buf_trans_entry {
	struct hsw_ddi_buf_trans hsw;
	struct bxt_ddi_buf_trans bxt;
	struct icl_ddi_buf_trans icl;
	struct icl_mg_phy_ddi_buf_trans mg;
	struct tgl_dkl_phy_ddi_buf_trans dkl;
	struct dg2_snps_phy_buf_trans snps;
};

struct intel_ddi_buf_trans {
	const union intel_ddi_buf_trans_entry *entries;
	u8 num_entries;
	u8 hdmi_default_entry;
};

/*
 * The DKL PHY tables of the Type-C ports (Linux intel_ddi_buf_trans.c):
 * Tiger Lake's DP and HDMI tables and Alder Lake-P's DP tables (Alder
 * Lake-P's HDMI uses Tiger Lake's).  Taken without change to any value
 * from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c
 * (sha256 fcf9694d241716a994e07255869c23f31a9749da183b38b66f3d4ba76ce6ddfd)
 * the notice above is the source file's own.
 */

static const union intel_ddi_buf_trans_entry _tgl_dkl_phy_trans_dp_hbr[] = {
					/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ .dkl = { 0x7, 0x0, 0x00 } },	/* 0	0	400mV		0 dB */
	{ .dkl = { 0x5, 0x0, 0x05 } },	/* 0	1	400mV		3.5 dB */
	{ .dkl = { 0x2, 0x0, 0x0B } },	/* 0	2	400mV		6 dB */
	{ .dkl = { 0x0, 0x0, 0x18 } },	/* 0	3	400mV		9.5 dB */
	{ .dkl = { 0x5, 0x0, 0x00 } },	/* 1	0	600mV		0 dB */
	{ .dkl = { 0x2, 0x0, 0x08 } },	/* 1	1	600mV		3.5 dB */
	{ .dkl = { 0x0, 0x0, 0x14 } },	/* 1	2	600mV		6 dB */
	{ .dkl = { 0x2, 0x0, 0x00 } },	/* 2	0	800mV		0 dB */
	{ .dkl = { 0x0, 0x0, 0x0B } },	/* 2	1	800mV		3.5 dB */
	{ .dkl = { 0x0, 0x0, 0x00 } },	/* 3	0	1200mV		0 dB HDMI default */
};

static const struct intel_ddi_buf_trans tgl_dkl_phy_trans_dp_hbr = {
	.entries = _tgl_dkl_phy_trans_dp_hbr,
	.num_entries = ARRAY_SIZE(_tgl_dkl_phy_trans_dp_hbr),
};

static const union intel_ddi_buf_trans_entry _tgl_dkl_phy_trans_dp_hbr2[] = {
					/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ .dkl = { 0x7, 0x0, 0x00 } },	/* 0	0	400mV		0 dB */
	{ .dkl = { 0x5, 0x0, 0x05 } },	/* 0	1	400mV		3.5 dB */
	{ .dkl = { 0x2, 0x0, 0x0B } },	/* 0	2	400mV		6 dB */
	{ .dkl = { 0x0, 0x0, 0x19 } },	/* 0	3	400mV		9.5 dB */
	{ .dkl = { 0x5, 0x0, 0x00 } },	/* 1	0	600mV		0 dB */
	{ .dkl = { 0x2, 0x0, 0x08 } },	/* 1	1	600mV		3.5 dB */
	{ .dkl = { 0x0, 0x0, 0x14 } },	/* 1	2	600mV		6 dB */
	{ .dkl = { 0x2, 0x0, 0x00 } },	/* 2	0	800mV		0 dB */
	{ .dkl = { 0x0, 0x0, 0x0B } },	/* 2	1	800mV		3.5 dB */
	{ .dkl = { 0x0, 0x0, 0x00 } },	/* 3	0	1200mV		0 dB HDMI default */
};

static const struct intel_ddi_buf_trans tgl_dkl_phy_trans_dp_hbr2 = {
	.entries = _tgl_dkl_phy_trans_dp_hbr2,
	.num_entries = ARRAY_SIZE(_tgl_dkl_phy_trans_dp_hbr2),
};

static const union intel_ddi_buf_trans_entry _tgl_dkl_phy_trans_hdmi[] = {
					/* HDMI Preset	VS	Pre-emph */
	{ .dkl = { 0x7, 0x0, 0x0 } },	/* 1		400mV	0dB */
	{ .dkl = { 0x6, 0x0, 0x0 } },	/* 2		500mV	0dB */
	{ .dkl = { 0x4, 0x0, 0x0 } },	/* 3		650mV	0dB */
	{ .dkl = { 0x2, 0x0, 0x0 } },	/* 4		800mV	0dB */
	{ .dkl = { 0x0, 0x0, 0x0 } },	/* 5		1000mV	0dB */
	{ .dkl = { 0x0, 0x0, 0x5 } },	/* 6		Full	-1.5 dB */
	{ .dkl = { 0x0, 0x0, 0x6 } },	/* 7		Full	-1.8 dB */
	{ .dkl = { 0x0, 0x0, 0x7 } },	/* 8		Full	-2 dB */
	{ .dkl = { 0x0, 0x0, 0x8 } },	/* 9		Full	-2.5 dB */
	{ .dkl = { 0x0, 0x0, 0xA } },	/* 10		Full	-3 dB */
};

static const struct intel_ddi_buf_trans tgl_dkl_phy_trans_hdmi = {
	.entries = _tgl_dkl_phy_trans_hdmi,
	.num_entries = ARRAY_SIZE(_tgl_dkl_phy_trans_hdmi),
	.hdmi_default_entry = ARRAY_SIZE(_tgl_dkl_phy_trans_hdmi) - 1,
};

static const union intel_ddi_buf_trans_entry _adlp_dkl_phy_trans_dp_hbr[] = {
					/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ .dkl = { 0x7, 0x0, 0x01 } },	/* 0	0	400mV		0 dB */
	{ .dkl = { 0x5, 0x0, 0x06 } },	/* 0	1	400mV		3.5 dB */
	{ .dkl = { 0x2, 0x0, 0x0B } },	/* 0	2	400mV		6 dB */
	{ .dkl = { 0x0, 0x0, 0x17 } },	/* 0	3	400mV		9.5 dB */
	{ .dkl = { 0x5, 0x0, 0x00 } },	/* 1	0	600mV		0 dB */
	{ .dkl = { 0x2, 0x0, 0x08 } },	/* 1	1	600mV		3.5 dB */
	{ .dkl = { 0x0, 0x0, 0x14 } },	/* 1	2	600mV		6 dB */
	{ .dkl = { 0x2, 0x0, 0x00 } },	/* 2	0	800mV		0 dB */
	{ .dkl = { 0x0, 0x0, 0x0B } },	/* 2	1	800mV		3.5 dB */
	{ .dkl = { 0x0, 0x0, 0x00 } },	/* 3	0	1200mV		0 dB */
};

static const struct intel_ddi_buf_trans adlp_dkl_phy_trans_dp_hbr = {
	.entries = _adlp_dkl_phy_trans_dp_hbr,
	.num_entries = ARRAY_SIZE(_adlp_dkl_phy_trans_dp_hbr),
};

static const union intel_ddi_buf_trans_entry _adlp_dkl_phy_trans_dp_hbr2_hbr3[] = {
					/* VS	pre-emp	Non-trans mV	Pre-emph dB */
	{ .dkl = { 0x7, 0x0, 0x00 } },	/* 0	0	400mV		0 dB */
	{ .dkl = { 0x5, 0x0, 0x04 } },	/* 0	1	400mV		3.5 dB */
	{ .dkl = { 0x2, 0x0, 0x0A } },	/* 0	2	400mV		6 dB */
	{ .dkl = { 0x0, 0x0, 0x18 } },	/* 0	3	400mV		9.5 dB */
	{ .dkl = { 0x5, 0x0, 0x00 } },	/* 1	0	600mV		0 dB */
	{ .dkl = { 0x2, 0x0, 0x06 } },	/* 1	1	600mV		3.5 dB */
	{ .dkl = { 0x0, 0x0, 0x14 } },	/* 1	2	600mV		6 dB */
	{ .dkl = { 0x2, 0x0, 0x00 } },	/* 2	0	800mV		0 dB */
	{ .dkl = { 0x0, 0x0, 0x09 } },	/* 2	1	800mV		3.5 dB */
	{ .dkl = { 0x0, 0x0, 0x00 } },	/* 3	0	1200mV		0 dB */
};

static const struct intel_ddi_buf_trans adlp_dkl_phy_trans_dp_hbr2_hbr3 = {
	.entries = _adlp_dkl_phy_trans_dp_hbr2_hbr3,
	.num_entries = ARRAY_SIZE(_adlp_dkl_phy_trans_dp_hbr2_hbr3),
};

#endif /* DRIVERS_GPU_I915_INTEL_PHY_H */

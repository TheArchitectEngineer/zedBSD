// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

/*
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_ddi_buf_trans.c
 * (sha256 fcf9694d241716a994e07255869c23f31a9749da183b38b66f3d4ba76ce6ddfd) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: is_hobl_buf_trans, use_edp_hobl, use_edp_low_vswing, intel_get_buf_trans,
 *    adlp_get_combo_buf_trans_dp, adlp_get_combo_buf_trans_edp, adlp_get_combo_buf_trans;
 *  - the includes are replaced by: lcd_compat.h, lcd_modeset_compat.h;
 *  - parity_buf_trans_glue.inc (zedBSD code) is included at the end of the file.
 */

#include "lcd_compat.h"
#include "lcd_modeset_compat.h"

static const union intel_ddi_buf_trans_entry _icl_combo_phy_trans_hdmi[] = {
							/* NT mV Trans mV db    */
	{ .icl = { 0xA, 0x60, 0x3F, 0x00, 0x00 } },	/* 450   450      0.0   */
	{ .icl = { 0xB, 0x73, 0x36, 0x00, 0x09 } },	/* 450   650      3.2   */
	{ .icl = { 0x6, 0x7F, 0x31, 0x00, 0x0E } },	/* 450   850      5.5   */
	{ .icl = { 0xB, 0x73, 0x3F, 0x00, 0x00 } },	/* 650   650      0.0   ALS */
	{ .icl = { 0x6, 0x7F, 0x37, 0x00, 0x08 } },	/* 650   850      2.3   */
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 850   850      0.0   */
	{ .icl = { 0x6, 0x7F, 0x35, 0x00, 0x0A } },	/* 600   850      3.0   */
};

static const struct intel_ddi_buf_trans icl_combo_phy_trans_hdmi = {
	.entries = _icl_combo_phy_trans_hdmi,
	.num_entries = ARRAY_SIZE(_icl_combo_phy_trans_hdmi),
	.hdmi_default_entry = ARRAY_SIZE(_icl_combo_phy_trans_hdmi) - 1,
};

static const union intel_ddi_buf_trans_entry _tgl_combo_phy_trans_edp_hbr2_hobl[] = {
							/* VS	pre-emp	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 0	0	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 0	1	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 0	2	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 0	3	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 1	0	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 1	1	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 1	2	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 2	0	*/
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 2	1	*/
};

static const struct intel_ddi_buf_trans tgl_combo_phy_trans_edp_hbr2_hobl = {
	.entries = _tgl_combo_phy_trans_edp_hbr2_hobl,
	.num_entries = ARRAY_SIZE(_tgl_combo_phy_trans_edp_hbr2_hobl),
};

static const union intel_ddi_buf_trans_entry _adlp_combo_phy_trans_dp_hbr[] = {
							/* NT mV Trans mV db    */
	{ .icl = { 0xA, 0x35, 0x3F, 0x00, 0x00 } },	/* 350   350      0.0   */
	{ .icl = { 0xA, 0x4F, 0x37, 0x00, 0x08 } },	/* 350   500      3.1   */
	{ .icl = { 0xC, 0x71, 0x31, 0x00, 0x0E } },	/* 350   700      6.0   */
	{ .icl = { 0x6, 0x7F, 0x2C, 0x00, 0x13 } },	/* 350   900      8.2   */
	{ .icl = { 0xA, 0x4C, 0x3F, 0x00, 0x00 } },	/* 500   500      0.0   */
	{ .icl = { 0xC, 0x73, 0x34, 0x00, 0x0B } },	/* 500   700      2.9   */
	{ .icl = { 0x6, 0x7F, 0x2F, 0x00, 0x10 } },	/* 500   900      5.1   */
	{ .icl = { 0xC, 0x7C, 0x3C, 0x00, 0x03 } },	/* 650   700      0.6   */
	{ .icl = { 0x6, 0x7F, 0x35, 0x00, 0x0A } },	/* 600   900      3.5   */
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 900   900      0.0   */
};

static const struct intel_ddi_buf_trans adlp_combo_phy_trans_dp_hbr = {
	.entries = _adlp_combo_phy_trans_dp_hbr,
	.num_entries = ARRAY_SIZE(_adlp_combo_phy_trans_dp_hbr),
};

static const union intel_ddi_buf_trans_entry _adlp_combo_phy_trans_dp_hbr2_hbr3[] = {
							/* NT mV Trans mV db    */
	{ .icl = { 0xA, 0x35, 0x3F, 0x00, 0x00 } },	/* 350   350      0.0   */
	{ .icl = { 0xA, 0x4F, 0x37, 0x00, 0x08 } },	/* 350   500      3.1   */
	{ .icl = { 0xC, 0x71, 0x30, 0x00, 0x0F } },	/* 350   700      6.0   */
	{ .icl = { 0x6, 0x7F, 0x2B, 0x00, 0x14 } },	/* 350   900      8.2   */
	{ .icl = { 0xA, 0x4C, 0x3F, 0x00, 0x00 } },	/* 500   500      0.0   */
	{ .icl = { 0xC, 0x73, 0x34, 0x00, 0x0B } },	/* 500   700      2.9   */
	{ .icl = { 0x6, 0x7F, 0x30, 0x00, 0x0F } },	/* 500   900      5.1   */
	{ .icl = { 0xC, 0x63, 0x3F, 0x00, 0x00 } },	/* 650   700      0.6   */
	{ .icl = { 0x6, 0x7F, 0x38, 0x00, 0x07 } },	/* 600   900      3.5   */
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 900   900      0.0   */
};

static const union intel_ddi_buf_trans_entry _adlp_combo_phy_trans_edp_hbr2[] = {
							/* NT mV Trans mV db    */
	{ .icl = { 0x4, 0x50, 0x38, 0x00, 0x07 } },	/* 200   200      0.0   */
	{ .icl = { 0x4, 0x58, 0x35, 0x00, 0x0A } },	/* 200   250      1.9   */
	{ .icl = { 0x4, 0x60, 0x34, 0x00, 0x0B } },	/* 200   300      3.5   */
	{ .icl = { 0x4, 0x6A, 0x32, 0x00, 0x0D } },	/* 200   350      4.9   */
	{ .icl = { 0x4, 0x5E, 0x38, 0x00, 0x07 } },	/* 250   250      0.0   */
	{ .icl = { 0x4, 0x61, 0x36, 0x00, 0x09 } },	/* 250   300      1.6   */
	{ .icl = { 0x4, 0x6B, 0x34, 0x00, 0x0B } },	/* 250   350      2.9   */
	{ .icl = { 0x4, 0x69, 0x39, 0x00, 0x06 } },	/* 300   300      0.0   */
	{ .icl = { 0x4, 0x73, 0x37, 0x00, 0x08 } },	/* 300   350      1.3   */
	{ .icl = { 0x4, 0x7A, 0x38, 0x00, 0x07 } },	/* 350   350      0.0   */
};

static const union intel_ddi_buf_trans_entry _adlp_combo_phy_trans_dp_hbr2_edp_hbr3[] = {
							/* NT mV Trans mV db    */
	{ .icl = { 0xA, 0x35, 0x3F, 0x00, 0x00 } },	/* 350   350      0.0   */
	{ .icl = { 0xA, 0x4F, 0x37, 0x00, 0x08 } },	/* 350   500      3.1   */
	{ .icl = { 0xC, 0x71, 0x30, 0x00, 0x0f } },	/* 350   700      6.0   */
	{ .icl = { 0x6, 0x7F, 0x2B, 0x00, 0x14 } },	/* 350   900      8.2   */
	{ .icl = { 0xA, 0x4C, 0x3F, 0x00, 0x00 } },	/* 500   500      0.0   */
	{ .icl = { 0xC, 0x73, 0x34, 0x00, 0x0B } },	/* 500   700      2.9   */
	{ .icl = { 0x6, 0x7F, 0x30, 0x00, 0x0F } },	/* 500   900      5.1   */
	{ .icl = { 0xC, 0x63, 0x3F, 0x00, 0x00 } },	/* 650   700      0.6   */
	{ .icl = { 0x6, 0x7F, 0x38, 0x00, 0x07 } },	/* 600   900      3.5   */
	{ .icl = { 0x6, 0x7F, 0x3F, 0x00, 0x00 } },	/* 900   900      0.0   */
};

static const struct intel_ddi_buf_trans adlp_combo_phy_trans_dp_hbr2_hbr3 = {
	.entries = _adlp_combo_phy_trans_dp_hbr2_hbr3,
	.num_entries = ARRAY_SIZE(_adlp_combo_phy_trans_dp_hbr2_hbr3),
};

static const struct intel_ddi_buf_trans adlp_combo_phy_trans_edp_hbr3 = {
	.entries = _adlp_combo_phy_trans_dp_hbr2_edp_hbr3,
	.num_entries = ARRAY_SIZE(_adlp_combo_phy_trans_dp_hbr2_edp_hbr3),
};

static const struct intel_ddi_buf_trans adlp_combo_phy_trans_edp_up_to_hbr2 = {
	.entries = _adlp_combo_phy_trans_edp_hbr2,
	.num_entries = ARRAY_SIZE(_adlp_combo_phy_trans_edp_hbr2),
};

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static bool use_edp_hobl(struct intel_encoder *encoder);
static bool use_edp_low_vswing(struct intel_encoder *encoder);
static const struct intel_ddi_buf_trans *
intel_get_buf_trans(const struct intel_ddi_buf_trans *trans, int *num_entries);
static const struct intel_ddi_buf_trans *
adlp_get_combo_buf_trans_dp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    int *n_entries);
static const struct intel_ddi_buf_trans *
adlp_get_combo_buf_trans_edp(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     int *n_entries);
static const struct intel_ddi_buf_trans *
adlp_get_combo_buf_trans(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 int *n_entries);

bool is_hobl_buf_trans(const struct intel_ddi_buf_trans *table)
{
	return table == &tgl_combo_phy_trans_edp_hbr2_hobl;
}

static bool use_edp_hobl(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_connector *connector = intel_dp->attached_connector;

	return connector->panel.vbt.edp.hobl && !intel_dp->hobl_failed;
}

static bool use_edp_low_vswing(struct intel_encoder *encoder)
{
	struct intel_dp *intel_dp = enc_to_intel_dp(encoder);
	struct intel_connector *connector = intel_dp->attached_connector;

	return connector->panel.vbt.edp.low_vswing;
}

static const struct intel_ddi_buf_trans *
intel_get_buf_trans(const struct intel_ddi_buf_trans *trans, int *num_entries)
{
	*num_entries = trans->num_entries;
	return trans;
}

static const struct intel_ddi_buf_trans *
adlp_get_combo_buf_trans_dp(struct intel_encoder *encoder,
			    const struct intel_crtc_state *crtc_state,
			    int *n_entries)
{
	if (crtc_state->port_clock > 270000)
		return intel_get_buf_trans(&adlp_combo_phy_trans_dp_hbr2_hbr3, n_entries);
	else
		return intel_get_buf_trans(&adlp_combo_phy_trans_dp_hbr, n_entries);
}

static const struct intel_ddi_buf_trans *
adlp_get_combo_buf_trans_edp(struct intel_encoder *encoder,
			     const struct intel_crtc_state *crtc_state,
			     int *n_entries)
{
	if (crtc_state->port_clock > 540000) {
		return intel_get_buf_trans(&adlp_combo_phy_trans_edp_hbr3,
					   n_entries);
	} else if (use_edp_hobl(encoder)) {
		return intel_get_buf_trans(&tgl_combo_phy_trans_edp_hbr2_hobl,
					   n_entries);
	} else if (use_edp_low_vswing(encoder)) {
		return intel_get_buf_trans(&adlp_combo_phy_trans_edp_up_to_hbr2,
					   n_entries);
	}

	return adlp_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}

static const struct intel_ddi_buf_trans *
adlp_get_combo_buf_trans(struct intel_encoder *encoder,
			 const struct intel_crtc_state *crtc_state,
			 int *n_entries)
{
	if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_HDMI))
		return intel_get_buf_trans(&icl_combo_phy_trans_hdmi, n_entries);
	else if (intel_crtc_has_type(crtc_state, INTEL_OUTPUT_EDP))
		return adlp_get_combo_buf_trans_edp(encoder, crtc_state, n_entries);
	else
		return adlp_get_combo_buf_trans_dp(encoder, crtc_state, n_entries);
}


#include "parity_buf_trans_glue.inc"

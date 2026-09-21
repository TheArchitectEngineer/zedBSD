/*
 * Copyright © 2006 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
 *
 */

/*
 * zedBSD display rebuild: the tables of Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_bios.c (sha256
 * c072adc5048e94fe45b7522905a81a38eb3604b36a7d2b4ebb819af212808fc5), as the
 * pre-rebuild tree's generated intel_bios_port.c carried them (commit
 * 7e7ff337).  No value is changed.  The four port tables were static locals of
 * dvo_port_to_port() and are file-scope tables here.
 *
 * Private to display/vbt.c, which includes it after display/vbt.h and
 * data/display-intel-vbt-defs.inc.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_VBT_TABLES_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_VBT_TABLES_INC

/* The BDB blocks init_bdb_blocks() copies, and the smallest size each may have. */
static const struct {
	enum bdb_block_id section_id;
	size_t min_size;
} bdb_blocks[] = {
	{ .section_id = BDB_GENERAL_FEATURES,
	  .min_size = sizeof(struct bdb_general_features), },
	{ .section_id = BDB_GENERAL_DEFINITIONS,
	  .min_size = sizeof(struct bdb_general_definitions), },
	{ .section_id = BDB_PSR,
	  .min_size = sizeof(struct bdb_psr), },
	{ .section_id = BDB_DRIVER_FEATURES,
	  .min_size = sizeof(struct bdb_driver_features), },
	{ .section_id = BDB_SDVO_LVDS_OPTIONS,
	  .min_size = sizeof(struct bdb_sdvo_lvds_options), },
	{ .section_id = BDB_SDVO_PANEL_DTDS,
	  .min_size = sizeof(struct bdb_sdvo_panel_dtds), },
	{ .section_id = BDB_EDP,
	  .min_size = sizeof(struct bdb_edp), },
	{ .section_id = BDB_LVDS_OPTIONS,
	  .min_size = sizeof(struct bdb_lvds_options), },
	/*
	 * BDB_LVDS_LFP_DATA depends on BDB_LVDS_LFP_DATA_PTRS,
	 * so keep the two ordered.
	 */
	{ .section_id = BDB_LVDS_LFP_DATA_PTRS,
	  .min_size = sizeof(struct bdb_lvds_lfp_data_ptrs), },
	{ .section_id = BDB_LVDS_LFP_DATA,
	  .min_size = 0, /* special case */ },
	{ .section_id = BDB_LVDS_BACKLIGHT,
	  .min_size = sizeof(struct bdb_lfp_backlight_data), },
	{ .section_id = BDB_LFP_POWER,
	  .min_size = sizeof(struct bdb_lfp_power), },
	{ .section_id = BDB_MIPI_CONFIG,
	  .min_size = sizeof(struct bdb_mipi_config), },
	{ .section_id = BDB_MIPI_SEQUENCE,
	  .min_size = sizeof(struct bdb_mipi_sequence) },
	{ .section_id = BDB_COMPRESSION_PARAMETERS,
	  .min_size = sizeof(struct bdb_compression_parameters), },
	{ .section_id = BDB_GENERIC_DTD,
	  .min_size = sizeof(struct bdb_generic_dtd), },
};

/* The VBT DDC pin of each GMBUS pin, per PCH generation (map_ddc_pin()). */
static const u8 cnp_ddc_pin_map[] = {
	[0] = 0, /* N/A */
	[GMBUS_PIN_1_BXT] = DDC_BUS_DDI_B,
	[GMBUS_PIN_2_BXT] = DDC_BUS_DDI_C,
	[GMBUS_PIN_4_CNP] = DDC_BUS_DDI_D, /* sic */
	[GMBUS_PIN_3_BXT] = DDC_BUS_DDI_F, /* sic */
};

static const u8 icp_ddc_pin_map[] = {
	[GMBUS_PIN_1_BXT] = ICL_DDC_BUS_DDI_A,
	[GMBUS_PIN_2_BXT] = ICL_DDC_BUS_DDI_B,
	[GMBUS_PIN_3_BXT] = TGL_DDC_BUS_DDI_C,
	[GMBUS_PIN_9_TC1_ICP] = ICL_DDC_BUS_PORT_1,
	[GMBUS_PIN_10_TC2_ICP] = ICL_DDC_BUS_PORT_2,
	[GMBUS_PIN_11_TC3_ICP] = ICL_DDC_BUS_PORT_3,
	[GMBUS_PIN_12_TC4_ICP] = ICL_DDC_BUS_PORT_4,
	[GMBUS_PIN_13_TC5_TGP] = TGL_DDC_BUS_PORT_5,
	[GMBUS_PIN_14_TC6_TGP] = TGL_DDC_BUS_PORT_6,
};

static const u8 rkl_pch_tgp_ddc_pin_map[] = {
	[GMBUS_PIN_1_BXT] = ICL_DDC_BUS_DDI_A,
	[GMBUS_PIN_2_BXT] = ICL_DDC_BUS_DDI_B,
	[GMBUS_PIN_9_TC1_ICP] = RKL_DDC_BUS_DDI_D,
	[GMBUS_PIN_10_TC2_ICP] = RKL_DDC_BUS_DDI_E,
};

static const u8 adls_ddc_pin_map[] = {
	[GMBUS_PIN_1_BXT] = ICL_DDC_BUS_DDI_A,
	[GMBUS_PIN_9_TC1_ICP] = ADLS_DDC_BUS_PORT_TC1,
	[GMBUS_PIN_10_TC2_ICP] = ADLS_DDC_BUS_PORT_TC2,
	[GMBUS_PIN_11_TC3_ICP] = ADLS_DDC_BUS_PORT_TC3,
	[GMBUS_PIN_12_TC4_ICP] = ADLS_DDC_BUS_PORT_TC4,
};

static const u8 gen9bc_tgp_ddc_pin_map[] = {
	[GMBUS_PIN_2_BXT] = DDC_BUS_DDI_B,
	[GMBUS_PIN_9_TC1_ICP] = DDC_BUS_DDI_C,
	[GMBUS_PIN_10_TC2_ICP] = DDC_BUS_DDI_D,
};

static const u8 adlp_ddc_pin_map[] = {
	[GMBUS_PIN_1_BXT] = ICL_DDC_BUS_DDI_A,
	[GMBUS_PIN_2_BXT] = ICL_DDC_BUS_DDI_B,
	[GMBUS_PIN_9_TC1_ICP] = ADLP_DDC_BUS_PORT_TC1,
	[GMBUS_PIN_10_TC2_ICP] = ADLP_DDC_BUS_PORT_TC2,
	[GMBUS_PIN_11_TC3_ICP] = ADLP_DDC_BUS_PORT_TC3,
	[GMBUS_PIN_12_TC4_ICP] = ADLP_DDC_BUS_PORT_TC4,
};

/*
 * The DVO port values of each DDI port (dvo_port_to_port()).
 *
 * Each DDI port can have more than one value on the "DVO Port" field,
 * so look for all the possible values for each port.
 */
static const int port_mapping[][3] = {
	[PORT_A] = { DVO_PORT_HDMIA, DVO_PORT_DPA, -1 },
	[PORT_B] = { DVO_PORT_HDMIB, DVO_PORT_DPB, -1 },
	[PORT_C] = { DVO_PORT_HDMIC, DVO_PORT_DPC, -1 },
	[PORT_D] = { DVO_PORT_HDMID, DVO_PORT_DPD, -1 },
	[PORT_E] = { DVO_PORT_HDMIE, DVO_PORT_DPE, DVO_PORT_CRT },
	[PORT_F] = { DVO_PORT_HDMIF, DVO_PORT_DPF, -1 },
	[PORT_G] = { DVO_PORT_HDMIG, DVO_PORT_DPG, -1 },
	[PORT_H] = { DVO_PORT_HDMIH, DVO_PORT_DPH, -1 },
	[PORT_I] = { DVO_PORT_HDMII, DVO_PORT_DPI, -1 },
};

/*
 * RKL VBT uses PHY based mapping. Combo PHYs A,B,C,D
 * map to DDI A,B,TC1,TC2 respectively.
 */
static const int rkl_port_mapping[][3] = {
	[PORT_A] = { DVO_PORT_HDMIA, DVO_PORT_DPA, -1 },
	[PORT_B] = { DVO_PORT_HDMIB, DVO_PORT_DPB, -1 },
	[PORT_C] = { -1 },
	[PORT_TC1] = { DVO_PORT_HDMIC, DVO_PORT_DPC, -1 },
	[PORT_TC2] = { DVO_PORT_HDMID, DVO_PORT_DPD, -1 },
};

/*
 * Alderlake S ports used in the driver are PORT_A, PORT_D, PORT_E,
 * PORT_F and PORT_G, we need to map that to correct VBT sections.
 */
static const int adls_port_mapping[][3] = {
	[PORT_A] = { DVO_PORT_HDMIA, DVO_PORT_DPA, -1 },
	[PORT_B] = { -1 },
	[PORT_C] = { -1 },
	[PORT_TC1] = { DVO_PORT_HDMIB, DVO_PORT_DPB, -1 },
	[PORT_TC2] = { DVO_PORT_HDMIC, DVO_PORT_DPC, -1 },
	[PORT_TC3] = { DVO_PORT_HDMID, DVO_PORT_DPD, -1 },
	[PORT_TC4] = { DVO_PORT_HDMIE, DVO_PORT_DPE, -1 },
};

static const int xelpd_port_mapping[][3] = {
	[PORT_A] = { DVO_PORT_HDMIA, DVO_PORT_DPA, -1 },
	[PORT_B] = { DVO_PORT_HDMIB, DVO_PORT_DPB, -1 },
	[PORT_C] = { DVO_PORT_HDMIC, DVO_PORT_DPC, -1 },
	[PORT_D_XELPD] = { DVO_PORT_HDMID, DVO_PORT_DPD, -1 },
	[PORT_E_XELPD] = { DVO_PORT_HDMIE, DVO_PORT_DPE, -1 },
	[PORT_TC1] = { DVO_PORT_HDMIF, DVO_PORT_DPF, -1 },
	[PORT_TC2] = { DVO_PORT_HDMIG, DVO_PORT_DPG, -1 },
	[PORT_TC3] = { DVO_PORT_HDMIH, DVO_PORT_DPH, -1 },
	[PORT_TC4] = { DVO_PORT_HDMII, DVO_PORT_DPI, -1 },
};

/* The VBT AUX channel value of each AUX channel, per platform (map_aux_ch()). */
static const u8 adlp_aux_ch_map[] = {
	[AUX_CH_A] = DP_AUX_A,
	[AUX_CH_B] = DP_AUX_B,
	[AUX_CH_C] = DP_AUX_C,
	[AUX_CH_D_XELPD] = DP_AUX_D,
	[AUX_CH_E_XELPD] = DP_AUX_E,
	[AUX_CH_USBC1] = DP_AUX_F,
	[AUX_CH_USBC2] = DP_AUX_G,
	[AUX_CH_USBC3] = DP_AUX_H,
	[AUX_CH_USBC4] = DP_AUX_I,
};

/*
 * ADL-S VBT uses PHY based mapping. Combo PHYs A,B,C,D,E
 * map to DDI A,TC1,TC2,TC3,TC4 respectively.
 */
static const u8 adls_aux_ch_map[] = {
	[AUX_CH_A] = DP_AUX_A,
	[AUX_CH_USBC1] = DP_AUX_B,
	[AUX_CH_USBC2] = DP_AUX_C,
	[AUX_CH_USBC3] = DP_AUX_D,
	[AUX_CH_USBC4] = DP_AUX_E,
};

/*
 * RKL/DG1 VBT uses PHY based mapping. Combo PHYs A,B,C,D
 * map to DDI A,B,TC1,TC2 respectively.
 */
static const u8 rkl_aux_ch_map[] = {
	[AUX_CH_A] = DP_AUX_A,
	[AUX_CH_B] = DP_AUX_B,
	[AUX_CH_USBC1] = DP_AUX_C,
	[AUX_CH_USBC2] = DP_AUX_D,
};

static const u8 direct_aux_ch_map[] = {
	[AUX_CH_A] = DP_AUX_A,
	[AUX_CH_B] = DP_AUX_B,
	[AUX_CH_C] = DP_AUX_C,
	[AUX_CH_D] = DP_AUX_D, /* aka AUX_CH_USBC1 */
	[AUX_CH_E] = DP_AUX_E, /* aka AUX_CH_USBC2 */
	[AUX_CH_F] = DP_AUX_F, /* aka AUX_CH_USBC3 */
	[AUX_CH_G] = DP_AUX_G, /* aka AUX_CH_USBC4 */
	[AUX_CH_H] = DP_AUX_H, /* aka AUX_CH_USBC5 */
	[AUX_CH_I] = DP_AUX_I, /* aka AUX_CH_USBC6 */
};

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_VBT_TABLES_INC */

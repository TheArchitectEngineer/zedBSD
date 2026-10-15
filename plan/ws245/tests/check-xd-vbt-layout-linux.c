/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Checks the VBT layout table of the xd display (intel/xd-vbt-layout.h)
 * against the Linux v6.8.12 VBT definitions it was taken from (run by
 * check-xd-vbt-layout-linux.sh, which points the include path at an
 * unpacked Linux tree).  Each field is found in the Linux structure by
 * setting all its bits in a zeroed instance and reading back which bytes
 * and bits changed; that place and mask must be the xd offset and mask.
 * The structure sizes and the code values (device types, DVO ports, AUX
 * codes, block ids, rate codes) are compared as well.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define __packed __attribute__((packed))
#define _INTEL_BIOS_PRIVATE

#include "display/intel_vbt_defs.h"

#include "intel/xd-vbt-layout.h"

/* The checks counted, and the ones that differed. */
static unsigned checks;
static unsigned differ;

/* All ones, read through a volatile so that storing it into a bit field is no constant overflow. */
static volatile unsigned long long all_ones = ~0ULL;

/*
 * Reports one place: the first changed byte of an instance and the
 * little-endian word of the changed bytes from there.
 */
static void
place(const char *name, const void *object, size_t size, unsigned offset, unsigned long long mask)
{
	const uint8_t *bytes;
	unsigned long long word;
	size_t first;
	size_t last;
	size_t at;

	bytes = object;
	first = size;
	last = 0;
	for (at = 0; at < size; at++) {
		if (bytes[at] != 0) {
			if (first == size)
				first = at;
			last = at;
		}
	}
	word = 0;
	for (at = last + 1; at > first; at--)
		word = (word << 8) | bytes[at - 1];

	/* An xd mask of a word whose low bytes are clear names the same bits from a later byte. */
	while (mask != 0 && (mask & 0xffU) == 0) {
		mask >>= 8;
		offset++;
	}
	checks++;
	if (first != offset || word != mask) {
		differ++;
		printf("check_xd_vbt_layout: %s DIFFERS: linux %zu/0x%llx, xd %u/0x%llx\n", name, first, word, offset, mask);
	}
}

/* Compares two values. */
static void
same(const char *name, unsigned long long linux_value, unsigned long long xd_value)
{
	checks++;
	if (linux_value != xd_value) {
		differ++;
		printf("check_xd_vbt_layout: %s DIFFERS: linux 0x%llx, xd 0x%llx\n", name, linux_value, xd_value);
	}
}

/* Sets one field of a zeroed instance of a structure and checks its place. */
#define FIELD(type, member, offset, mask) do { \
	type object; \
	memset(&object, 0, sizeof(object)); \
	object.member = all_ones; \
	place(#type "." #member, &object, sizeof(object), (offset), (mask)); \
} while (0)

/* Compares a Linux value and the xd value of the same meaning. */
#define SAME(linux_value, xd_value) same(#linux_value, (linux_value), (xd_value))

int
main(void)
{
	/* The headers. */
	FIELD(struct vbt_header, version, I915_XD_VBT_VERSION, 0xffffU);
	FIELD(struct vbt_header, header_size, I915_XD_VBT_HEADER_SIZE, 0xffffU);
	FIELD(struct vbt_header, vbt_size, I915_XD_VBT_VBT_SIZE, 0xffffU);
	FIELD(struct vbt_header, bdb_offset, I915_XD_VBT_BDB_OFFSET, 0xffffffffU);
	SAME(sizeof(struct vbt_header), I915_XD_VBT_HEADER_BYTES);
	FIELD(struct bdb_header, version, I915_XD_BDB_VERSION, 0xffffU);
	FIELD(struct bdb_header, header_size, I915_XD_BDB_HEADER_SIZE, 0xffffU);
	FIELD(struct bdb_header, bdb_size, I915_XD_BDB_SIZE, 0xffffU);
	SAME(sizeof(struct bdb_header), I915_XD_BDB_HEADER_BYTES);

	/* The block ids. */
	SAME(BDB_GENERAL_FEATURES, I915_XD_BLOCK_GENERAL_FEATURES);
	SAME(BDB_GENERAL_DEFINITIONS, I915_XD_BLOCK_GENERAL_DEFINITIONS);
	SAME(BDB_DRIVER_FEATURES, I915_XD_BLOCK_DRIVER_FEATURES);
	SAME(BDB_EDP, I915_XD_BLOCK_EDP);
	SAME(BDB_LVDS_OPTIONS, I915_XD_BLOCK_LFP_OPTIONS);
	SAME(BDB_LVDS_LFP_DATA_PTRS, I915_XD_BLOCK_LFP_DATA_POINTERS);
	SAME(BDB_LVDS_LFP_DATA, I915_XD_BLOCK_LFP_DATA);
	SAME(BDB_LVDS_BACKLIGHT, I915_XD_BLOCK_LFP_BACKLIGHT);
	SAME(BDB_LFP_POWER, I915_XD_BLOCK_LFP_POWER);
	SAME(BDB_MIPI_SEQUENCE, I915_XD_BLOCK_MIPI_SEQUENCE);
	SAME(BDB_GENERIC_DTD, I915_XD_BLOCK_GENERIC_DTD);

	/* The general features and definitions. */
	FIELD(struct bdb_general_features, enable_ssc, I915_XD_VBT_FEATURES_BITS2, I915_XD_VBT_FEATURES_SSC);
	FIELD(struct bdb_general_features, ssc_freq, I915_XD_VBT_FEATURES_BITS2, I915_XD_VBT_FEATURES_SSC_ALTERNATE);
	FIELD(struct bdb_general_features, display_clock_mode, I915_XD_VBT_FEATURES_BITS2, I915_XD_VBT_FEATURES_CLOCK_MODE);
	FIELD(struct bdb_general_features, afc_startup_config, I915_XD_VBT_FEATURES_BITS7, I915_XD_VBT_FEATURES_AFC_STARTUP);
	FIELD(struct bdb_general_definitions, crt_ddc_gmbus_pin, I915_XD_VBT_DEFS_CRT_DDC_PIN, 0xffU);
	FIELD(struct bdb_general_definitions, child_dev_size, I915_XD_VBT_DEFS_CHILD_SIZE, 0xffU);
	SAME(sizeof(struct bdb_general_definitions), I915_XD_VBT_DEFS_CHILDREN);

	/* The child device. */
	FIELD(struct child_device_config, handle, I915_XD_VBT_CHILD_HANDLE, 0xffffU);
	FIELD(struct child_device_config, device_type, I915_XD_VBT_CHILD_DEVICE_TYPE, 0xffffU);
	FIELD(struct child_device_config, hdmi_level_shifter_value, I915_XD_VBT_CHILD_HDMI_BITS, I915_XD_VBT_CHILD_HDMI_LEVEL_SHIFT);
	FIELD(struct child_device_config, hdmi_max_data_rate, I915_XD_VBT_CHILD_HDMI_BITS, I915_XD_VBT_CHILD_HDMI_MAX_RATE);
	FIELD(struct child_device_config, dvo_port, I915_XD_VBT_CHILD_DVO_PORT, 0xffU);
	FIELD(struct child_device_config, ddc_pin, I915_XD_VBT_CHILD_DDC_PIN, 0xffU);
	FIELD(struct child_device_config, lane_reversal, I915_XD_VBT_CHILD_FLAGS, I915_XD_VBT_CHILD_LANE_REVERSAL);
	FIELD(struct child_device_config, iboost, I915_XD_VBT_CHILD_FLAGS, I915_XD_VBT_CHILD_IBOOST);
	FIELD(struct child_device_config, hpd_invert, I915_XD_VBT_CHILD_FLAGS, I915_XD_VBT_CHILD_HPD_INVERT);
	FIELD(struct child_device_config, dp_max_lane_count, I915_XD_VBT_CHILD_FLAGS, I915_XD_VBT_CHILD_DP_MAX_LANES);
	FIELD(struct child_device_config, aux_channel, I915_XD_VBT_CHILD_AUX_CHANNEL, 0xffU);
	FIELD(struct child_device_config, dp_usb_type_c, I915_XD_VBT_CHILD_FLAGS2, I915_XD_VBT_CHILD_TYPE_C);
	FIELD(struct child_device_config, tbt, I915_XD_VBT_CHILD_FLAGS2, I915_XD_VBT_CHILD_TBT);
	FIELD(struct child_device_config, dp_iboost_level, I915_XD_VBT_CHILD_IBOOST_LEVELS, I915_XD_VBT_CHILD_DP_IBOOST);
	FIELD(struct child_device_config, hdmi_iboost_level, I915_XD_VBT_CHILD_IBOOST_LEVELS, I915_XD_VBT_CHILD_HDMI_IBOOST);
	FIELD(struct child_device_config, dp_max_link_rate, I915_XD_VBT_CHILD_DP_RATE_BITS, I915_XD_VBT_CHILD_DP_MAX_RATE);
	SAME(sizeof(struct child_device_config), I915_XD_VBT_CHILD_KNOWN_BYTES);
	SAME(LEGACY_CHILD_DEVICE_CONFIG_SIZE, I915_XD_VBT_CHILD_MIN_BYTES);
	SAME(DEVICE_HANDLE_LFP1, I915_XD_VBT_HANDLE_LFP1);
	SAME(DEVICE_HANDLE_LFP2, I915_XD_VBT_HANDLE_LFP2);
	SAME(DEVICE_TYPE_ANALOG_OUTPUT, I915_XD_VBT_TYPE_ANALOG);
	SAME(DEVICE_TYPE_DISPLAYPORT_OUTPUT, I915_XD_VBT_TYPE_DISPLAYPORT);
	SAME(DEVICE_TYPE_TMDS_DVI_SIGNALING, I915_XD_VBT_TYPE_TMDS);
	SAME(DEVICE_TYPE_MIPI_OUTPUT, I915_XD_VBT_TYPE_MIPI);
	SAME(DEVICE_TYPE_NOT_HDMI_OUTPUT, I915_XD_VBT_TYPE_NOT_HDMI);
	SAME(DEVICE_TYPE_INTERNAL_CONNECTOR, I915_XD_VBT_TYPE_INTERNAL);

	/* The DVO port and AUX channel codes. */
	SAME(DVO_PORT_HDMIA, I915_XD_DVO_HDMI_A);
	SAME(DVO_PORT_HDMIB, I915_XD_DVO_HDMI_B);
	SAME(DVO_PORT_HDMIC, I915_XD_DVO_HDMI_C);
	SAME(DVO_PORT_HDMID, I915_XD_DVO_HDMI_D);
	SAME(DVO_PORT_CRT, I915_XD_DVO_CRT);
	SAME(DVO_PORT_DPB, I915_XD_DVO_DP_B);
	SAME(DVO_PORT_DPC, I915_XD_DVO_DP_C);
	SAME(DVO_PORT_DPD, I915_XD_DVO_DP_D);
	SAME(DVO_PORT_DPA, I915_XD_DVO_DP_A);
	SAME(DVO_PORT_DPE, I915_XD_DVO_DP_E);
	SAME(DVO_PORT_HDMIE, I915_XD_DVO_HDMI_E);
	SAME(DVO_PORT_DPF, I915_XD_DVO_DP_F);
	SAME(DVO_PORT_HDMIF, I915_XD_DVO_HDMI_F);
	SAME(DVO_PORT_DPG, I915_XD_DVO_DP_G);
	SAME(DVO_PORT_HDMIG, I915_XD_DVO_HDMI_G);
	SAME(DVO_PORT_DPH, I915_XD_DVO_DP_H);
	SAME(DVO_PORT_HDMIH, I915_XD_DVO_HDMI_H);
	SAME(DVO_PORT_DPI, I915_XD_DVO_DP_I);
	SAME(DVO_PORT_HDMII, I915_XD_DVO_HDMI_I);
	SAME(DP_AUX_A, I915_XD_VBT_AUX_A);
	SAME(DP_AUX_B, I915_XD_VBT_AUX_B);
	SAME(DP_AUX_C, I915_XD_VBT_AUX_C);
	SAME(DP_AUX_D, I915_XD_VBT_AUX_D);
	SAME(DP_AUX_E, I915_XD_VBT_AUX_E);
	SAME(DP_AUX_F, I915_XD_VBT_AUX_F);
	SAME(DP_AUX_G, I915_XD_VBT_AUX_G);
	SAME(DP_AUX_H, I915_XD_VBT_AUX_H);
	SAME(DP_AUX_I, I915_XD_VBT_AUX_I);
	SAME(HDMI_MAX_DATA_RATE_PLATFORM, I915_XD_VBT_HDMI_RATE_PLATFORM);
	SAME(HDMI_MAX_DATA_RATE_297, I915_XD_VBT_HDMI_RATE_297);
	SAME(HDMI_MAX_DATA_RATE_165, I915_XD_VBT_HDMI_RATE_165);
	SAME(HDMI_MAX_DATA_RATE_594, I915_XD_VBT_HDMI_RATE_594);
	SAME(HDMI_MAX_DATA_RATE_340, I915_XD_VBT_HDMI_RATE_340);
	SAME(HDMI_MAX_DATA_RATE_300, I915_XD_VBT_HDMI_RATE_300);

	/* The driver features. */
	FIELD(struct bdb_driver_features, lvds_config, I915_XD_VBT_DRIVER_EXT_BITS, I915_XD_VBT_DRIVER_LFP_CONFIG);
	FIELD(struct bdb_driver_features, drrs_enabled, I915_XD_VBT_DRIVER_FLAGS, I915_XD_VBT_DRIVER_DRRS);
	FIELD(struct bdb_driver_features, psr_enabled, I915_XD_VBT_DRIVER_FLAGS, I915_XD_VBT_DRIVER_PSR);
	FIELD(struct bdb_driver_features, dmrrs_enabled, I915_XD_VBT_DRIVER_FLAGS, I915_XD_VBT_DRIVER_DMRRS);
	SAME(BDB_DRIVER_FEATURE_INT_LVDS, I915_XD_VBT_DRIVER_LFP_INTERNAL);

	/* The eDP block. */
	FIELD(struct bdb_edp, power_seqs[0].t1_t3, I915_XD_VBT_EDP_PPS + I915_XD_VBT_EDP_PPS_T1_T3, 0xffffU);
	FIELD(struct bdb_edp, power_seqs[0].t8, I915_XD_VBT_EDP_PPS + I915_XD_VBT_EDP_PPS_T8, 0xffffU);
	FIELD(struct bdb_edp, power_seqs[0].t9, I915_XD_VBT_EDP_PPS + I915_XD_VBT_EDP_PPS_T9, 0xffffU);
	FIELD(struct bdb_edp, power_seqs[0].t10, I915_XD_VBT_EDP_PPS + I915_XD_VBT_EDP_PPS_T10, 0xffffU);
	FIELD(struct bdb_edp, power_seqs[0].t11_t12, I915_XD_VBT_EDP_PPS + I915_XD_VBT_EDP_PPS_T11_T12, 0xffffU);
	FIELD(struct bdb_edp, power_seqs[1].t1_t3, I915_XD_VBT_EDP_PPS + I915_XD_VBT_EDP_PPS_STRIDE, 0xffffU);
	FIELD(struct bdb_edp, color_depth, I915_XD_VBT_EDP_COLOR_DEPTH, 0xffffffffU);
	FIELD(struct bdb_edp, fast_link_params[0].rate, I915_XD_VBT_EDP_LINK, I915_XD_VBT_EDP_LINK_RATE);
	FIELD(struct bdb_edp, fast_link_params[0].lanes, I915_XD_VBT_EDP_LINK, I915_XD_VBT_EDP_LINK_LANES);
	FIELD(struct bdb_edp, fast_link_params[0].preemphasis, I915_XD_VBT_EDP_LINK + 1U, I915_XD_VBT_EDP_LINK_PREEMPHASIS);
	FIELD(struct bdb_edp, fast_link_params[0].vswing, I915_XD_VBT_EDP_LINK + 1U, I915_XD_VBT_EDP_LINK_VSWING);
	FIELD(struct bdb_edp, fast_link_params[1].rate, I915_XD_VBT_EDP_LINK + I915_XD_VBT_EDP_LINK_STRIDE, I915_XD_VBT_EDP_LINK_RATE);
	FIELD(struct bdb_edp, edp_vswing_preemph, I915_XD_VBT_EDP_VSWING_PREEMPH, ~0ULL);
	FIELD(struct bdb_edp, edp_fast_link_training_rate[0], I915_XD_VBT_EDP_LINK_RATE_WORDS, 0xffffU);
	FIELD(struct bdb_edp, edp_max_port_link_rate[0], I915_XD_VBT_EDP_MAX_RATE_WORDS, 0xffffU);
	SAME(EDP_18BPP, I915_XD_VBT_EDP_BPP_18);
	SAME(EDP_24BPP, I915_XD_VBT_EDP_BPP_24);
	SAME(EDP_30BPP, I915_XD_VBT_EDP_BPP_30);
	SAME(EDP_RATE_1_62, I915_XD_VBT_EDP_RATE_1P62);
	SAME(EDP_RATE_2_7, I915_XD_VBT_EDP_RATE_2P7);
	SAME(EDP_RATE_5_4, I915_XD_VBT_EDP_RATE_5P4);
	SAME(EDP_LANE_1, I915_XD_VBT_EDP_LANES_1);
	SAME(EDP_LANE_2, I915_XD_VBT_EDP_LANES_2);
	SAME(EDP_LANE_4, I915_XD_VBT_EDP_LANES_4);

	/* The panel options, pointers, DVO timing and ids. */
	FIELD(struct bdb_lvds_options, panel_type, I915_XD_VBT_LFP_PANEL_TYPE, 0xffU);
	FIELD(struct bdb_lvds_options, panel_type2, I915_XD_VBT_LFP_PANEL_TYPE2, 0xffU);
	FIELD(struct bdb_lvds_options, pixel_dither, I915_XD_VBT_LFP_CAPS, I915_XD_VBT_LFP_DITHER);
	FIELD(struct bdb_lvds_options, dps_panel_type_bits, I915_XD_VBT_LFP_DRRS_BITS, 0xffffffffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, lvds_entries, I915_XD_VBT_PTRS_ENTRIES, 0xffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, ptr[0].fp_timing.offset, I915_XD_VBT_PTRS_TABLES + I915_XD_VBT_PTRS_FP + I915_XD_VBT_PTRS_OFFSET, 0xffffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, ptr[0].fp_timing.table_size, I915_XD_VBT_PTRS_TABLES + I915_XD_VBT_PTRS_FP + I915_XD_VBT_PTRS_TABLE_SIZE, 0xffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, ptr[0].dvo_timing.offset, I915_XD_VBT_PTRS_TABLES + I915_XD_VBT_PTRS_DVO + I915_XD_VBT_PTRS_OFFSET, 0xffffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, ptr[0].dvo_timing.table_size, I915_XD_VBT_PTRS_TABLES + I915_XD_VBT_PTRS_DVO + I915_XD_VBT_PTRS_TABLE_SIZE, 0xffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, ptr[0].panel_pnp_id.offset, I915_XD_VBT_PTRS_TABLES + I915_XD_VBT_PTRS_PNP + I915_XD_VBT_PTRS_OFFSET, 0xffffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, ptr[0].panel_pnp_id.table_size, I915_XD_VBT_PTRS_TABLES + I915_XD_VBT_PTRS_PNP + I915_XD_VBT_PTRS_TABLE_SIZE, 0xffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, ptr[1].fp_timing.offset, I915_XD_VBT_PTRS_TABLES + I915_XD_VBT_PTRS_STRIDE, 0xffffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, panel_name.offset, I915_XD_VBT_PTRS_PANEL_NAME + I915_XD_VBT_PTRS_OFFSET, 0xffffU);
	FIELD(struct bdb_lvds_lfp_data_ptrs, panel_name.table_size, I915_XD_VBT_PTRS_PANEL_NAME + I915_XD_VBT_PTRS_TABLE_SIZE, 0xffU);
	FIELD(struct lvds_dvo_timing, clock, I915_XD_DVO_CLOCK, 0xffffU);
	FIELD(struct lvds_dvo_timing, hactive_lo, I915_XD_DVO_HACTIVE_LO, 0xffU);
	FIELD(struct lvds_dvo_timing, hblank_lo, I915_XD_DVO_HBLANK_LO, 0xffU);
	FIELD(struct lvds_dvo_timing, hblank_hi, I915_XD_DVO_H_HI, I915_XD_DVO_HBLANK_HI);
	FIELD(struct lvds_dvo_timing, hactive_hi, I915_XD_DVO_H_HI, I915_XD_DVO_HACTIVE_HI);
	FIELD(struct lvds_dvo_timing, vactive_lo, I915_XD_DVO_VACTIVE_LO, 0xffU);
	FIELD(struct lvds_dvo_timing, vblank_lo, I915_XD_DVO_VBLANK_LO, 0xffU);
	FIELD(struct lvds_dvo_timing, vblank_hi, I915_XD_DVO_V_HI, I915_XD_DVO_VBLANK_HI);
	FIELD(struct lvds_dvo_timing, vactive_hi, I915_XD_DVO_V_HI, I915_XD_DVO_VACTIVE_HI);
	FIELD(struct lvds_dvo_timing, hsync_off_lo, I915_XD_DVO_HSYNC_OFF_LO, 0xffU);
	FIELD(struct lvds_dvo_timing, hsync_pulse_width_lo, I915_XD_DVO_HSYNC_WIDTH_LO, 0xffU);
	FIELD(struct lvds_dvo_timing, vsync_pulse_width_lo, I915_XD_DVO_VSYNC_LO, I915_XD_DVO_VSYNC_WIDTH_LO);
	FIELD(struct lvds_dvo_timing, vsync_off_lo, I915_XD_DVO_VSYNC_LO, I915_XD_DVO_VSYNC_OFF_LO);
	FIELD(struct lvds_dvo_timing, vsync_pulse_width_hi, I915_XD_DVO_SYNC_HI, I915_XD_DVO_VSYNC_WIDTH_HI);
	FIELD(struct lvds_dvo_timing, vsync_off_hi, I915_XD_DVO_SYNC_HI, I915_XD_DVO_VSYNC_OFF_HI);
	FIELD(struct lvds_dvo_timing, hsync_pulse_width_hi, I915_XD_DVO_SYNC_HI, I915_XD_DVO_HSYNC_WIDTH_HI);
	FIELD(struct lvds_dvo_timing, hsync_off_hi, I915_XD_DVO_SYNC_HI, I915_XD_DVO_HSYNC_OFF_HI);
	FIELD(struct lvds_dvo_timing, himage_lo, I915_XD_DVO_HIMAGE_LO, 0xffU);
	FIELD(struct lvds_dvo_timing, vimage_lo, I915_XD_DVO_VIMAGE_LO, 0xffU);
	FIELD(struct lvds_dvo_timing, vimage_hi, I915_XD_DVO_IMAGE_HI, I915_XD_DVO_VIMAGE_HI);
	FIELD(struct lvds_dvo_timing, himage_hi, I915_XD_DVO_IMAGE_HI, I915_XD_DVO_HIMAGE_HI);
	FIELD(struct lvds_dvo_timing, vsync_positive, I915_XD_DVO_FLAGS, I915_XD_DVO_VSYNC_POSITIVE);
	FIELD(struct lvds_dvo_timing, hsync_positive, I915_XD_DVO_FLAGS, I915_XD_DVO_HSYNC_POSITIVE);
	SAME(sizeof(struct bdb_lvds_lfp_data_ptrs) - I915_XD_VBT_PTRS_PANEL_NAME, 3U);
	SAME(sizeof(struct lvds_lfp_data_ptr), I915_XD_VBT_PTRS_STRIDE);
	SAME(sizeof(struct lvds_dvo_timing), I915_XD_VBT_DVO_TIMING_BYTES);
	SAME(sizeof(struct lvds_pnp_id), I915_XD_VBT_PNP_BYTES);
	SAME(sizeof(struct lvds_lfp_panel_name), I915_XD_VBT_PANEL_NAME_BYTES);

	/* The backlight block. */
	FIELD(struct bdb_lfp_backlight_data, entry_size, I915_XD_VBT_BL_ENTRY_SIZE, 0xffU);
	FIELD(struct bdb_lfp_backlight_data, data[0].type, I915_XD_VBT_BL_ENTRIES + I915_XD_VBT_BL_FLAGS, I915_XD_VBT_BL_TYPE);
	FIELD(struct bdb_lfp_backlight_data, data[0].active_low_pwm, I915_XD_VBT_BL_ENTRIES + I915_XD_VBT_BL_FLAGS, I915_XD_VBT_BL_ACTIVE_LOW);
	FIELD(struct bdb_lfp_backlight_data, data[0].pwm_freq_hz, I915_XD_VBT_BL_ENTRIES + I915_XD_VBT_BL_PWM_HZ, 0xffffU);
	FIELD(struct bdb_lfp_backlight_data, data[0].min_brightness, I915_XD_VBT_BL_ENTRIES + I915_XD_VBT_BL_OLD_MIN, 0xffU);
	FIELD(struct bdb_lfp_backlight_data, data[1].type, I915_XD_VBT_BL_ENTRIES + I915_XD_VBT_BL_ENTRY_BYTES, I915_XD_VBT_BL_TYPE);
	FIELD(struct bdb_lfp_backlight_data, level[0], I915_XD_VBT_BL_OLD_LEVELS, 0xffU);
	FIELD(struct bdb_lfp_backlight_data, backlight_control[0].type, I915_XD_VBT_BL_CONTROL, I915_XD_VBT_BL_CONTROL_TYPE);
	FIELD(struct bdb_lfp_backlight_data, backlight_control[0].controller, I915_XD_VBT_BL_CONTROL, I915_XD_VBT_BL_CONTROL_CONTROLLER);
	FIELD(struct bdb_lfp_backlight_data, brightness_level[0].level, I915_XD_VBT_BL_LEVELS, 0xffffU);
	FIELD(struct bdb_lfp_backlight_data, brightness_level[1].level, I915_XD_VBT_BL_LEVELS + I915_XD_VBT_BL_LEVEL_STRIDE, 0xffffU);
	FIELD(struct bdb_lfp_backlight_data, brightness_min_level[0].level, I915_XD_VBT_BL_MIN_LEVELS, 0xffffU);
	FIELD(struct bdb_lfp_backlight_data, brightness_precision_bits[0], I915_XD_VBT_BL_PRECISION, 0xffU);
	SAME(sizeof(struct lfp_backlight_data_entry), I915_XD_VBT_BL_ENTRY_BYTES);
	SAME(BDB_BACKLIGHT_TYPE_PWM, I915_XD_VBT_BL_TYPE_PWM);

	/* The power conservation block. */
	FIELD(struct bdb_lfp_power, drrs, I915_XD_VBT_POWER_DRRS, 0xffffU);
	FIELD(struct bdb_lfp_power, dmrrs, I915_XD_VBT_POWER_DMRRS, 0xffffU);
	FIELD(struct bdb_lfp_power, hobl, I915_XD_VBT_POWER_HOBL, 0xffffU);
	FIELD(struct bdb_lfp_power, vrr_feature_enabled, I915_XD_VBT_POWER_VRR, 0xffffU);

	/* The generic DTD block. */
	FIELD(struct bdb_generic_dtd, gdtd_size, I915_XD_VBT_GDTD_SIZE, 0xffffU);
	SAME(sizeof(struct bdb_generic_dtd), I915_XD_VBT_GDTD_ENTRIES);
	FIELD(struct generic_dtd_entry, pixel_clock, I915_XD_VBT_GDTD_CLOCK, 0xffffffffU);
	FIELD(struct generic_dtd_entry, hactive, I915_XD_VBT_GDTD_HACTIVE, 0xffffU);
	FIELD(struct generic_dtd_entry, hblank, I915_XD_VBT_GDTD_HBLANK, 0xffffU);
	FIELD(struct generic_dtd_entry, hfront_porch, I915_XD_VBT_GDTD_HFRONT, 0xffffU);
	FIELD(struct generic_dtd_entry, hsync, I915_XD_VBT_GDTD_HSYNC, 0xffffU);
	FIELD(struct generic_dtd_entry, vactive, I915_XD_VBT_GDTD_VACTIVE, 0xffffU);
	FIELD(struct generic_dtd_entry, vblank, I915_XD_VBT_GDTD_VBLANK, 0xffffU);
	FIELD(struct generic_dtd_entry, vfront_porch, I915_XD_VBT_GDTD_VFRONT, 0xffffU);
	FIELD(struct generic_dtd_entry, vsync, I915_XD_VBT_GDTD_VSYNC, 0xffffU);
	FIELD(struct generic_dtd_entry, width_mm, I915_XD_VBT_GDTD_WIDTH_MM, 0xffffU);
	FIELD(struct generic_dtd_entry, height_mm, I915_XD_VBT_GDTD_HEIGHT_MM, 0xffffU);
	FIELD(struct generic_dtd_entry, vsync_positive_polarity, I915_XD_VBT_GDTD_FLAGS, I915_XD_VBT_GDTD_VSYNC_POSITIVE);
	FIELD(struct generic_dtd_entry, hsync_positive_polarity, I915_XD_VBT_GDTD_FLAGS, I915_XD_VBT_GDTD_HSYNC_POSITIVE);
	SAME(sizeof(struct generic_dtd_entry), I915_XD_VBT_GDTD_ENTRY_BYTES);

	printf("check_xd_vbt_layout: %u values compared, %u differ\n", checks, differ);
	return differ != 0;
}

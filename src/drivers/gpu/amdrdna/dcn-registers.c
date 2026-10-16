/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The addresses of the display registers of the DCN 2.0 display engine.
 *
 * Each pipe's blocks repeat at a fixed distance, so a register is the
 * first instance's offset plus the instance times that distance, in the
 * display block's window the register belongs to.  Two families do not
 * repeat evenly (the legacy VGA controls of the six heads and the pixel
 * processors of pipes 4 and 5): those keep one base per instance, and a
 * register of the family lies at a fixed distance from its instance's base.
 */

#include <stddef.h>
#include <stdint.h>

#include "drivers/gpu/amdrdna/amdrdna-hw.h"

/*
 * Where each named display register lies: the display block's window, the
 * first instance's offset in it, and the distance between instances; or,
 * for the uneven families, one base per instance and the register's
 * distance from that base in the offset.
 */
struct dcn_place {
	uint8_t window;
	uint16_t offset;
	uint16_t stride;
	const uint16_t *instances;
};

/*
 * The legacy VGA controls of the six heads (window 1); bit 0 says the head
 * runs in VGA mode.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_0_0_offset.h
 * (mmD1VGA_CONTROL to mmD6VGA_CONTROL).
 */
static const uint16_t head_legacy_offsets[AMDRDNA_DISPLAY_PIPES] = {
	0x000cU,
	0x000eU,
	0x0038U,
	0x0039U,
	0x003aU,
	0x003bU
};

/*
 * The control words of the six pixel processors (window 2): pipes 0 to 3
 * repeat, pipes 4 and 5 lie elsewhere.
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_0_0_offset.h
 * (mmDPP_TOP0_DPP_CONTROL to mmDPP_TOP5_DPP_CONTROL).
 */
static const uint16_t pixel_processor_offsets[AMDRDNA_DISPLAY_PIPES] = {
	0x0cc5U,
	0x0e30U,
	0x0f9bU,
	0x1106U,
	0x35d0U,
	0x373bU
};

/*
 * The place of every named display register, in the order of
 * enum amdrdna_dcn_register: the clock controls in window 1, the pipe
 * blocks in window 2, with the plane (0xdc apart), the blend units (0x1b),
 * the blend tree roots (4), the combiners (0x10), the timing generators
 * (0x80), the encoders and their DisplayPort parts (0x100) and the clock
 * controls of the pixel rate and the DisplayPort clocks (4).
 *
 * Values referenced: Linux v6.19
 * drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_0_0_offset.h
 * (mmDENTIST_DISPCLK_CNTL, mmOTG0_PIXEL_RATE_CNTL, mmDP_DTO0_PHASE,
 * mmDP_DTO0_MODULO, mmHUBP0_DCSURF_SURFACE_CONFIG,
 * mmHUBP0_DCSURF_TILING_CONFIG, mmHUBP0_DCSURF_PRI_VIEWPORT_START,
 * mmHUBP0_DCSURF_PRI_VIEWPORT_DIMENSION, mmHUBP0_DCHUBP_CNTL,
 * mmHUBP0_HUBP_CLK_CNTL, mmHUBPREQ0_DCSURF_SURFACE_PITCH,
 * mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS,
 * mmHUBPREQ0_DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH,
 * mmHUBPREQ0_DCSURF_SURFACE_CONTROL, mmHUBPREQ0_DCSURF_FLIP_CONTROL,
 * mmHUBPRET0_HUBPRET_CONTROL, mmMPCC0_MPCC_TOP_SEL, mmMPCC0_MPCC_BOT_SEL,
 * mmMPCC0_MPCC_OPP_ID, mmMPCC0_MPCC_CONTROL, mmMPC_OUT0_MUX,
 * mmODM0_OPTC_DATA_SOURCE_SELECT, mmODM0_OPTC_INPUT_GLOBAL_CONTROL,
 * mmOTG0_OTG_H_TOTAL, mmOTG0_OTG_H_BLANK_START_END, mmOTG0_OTG_H_SYNC_A,
 * mmOTG0_OTG_H_SYNC_A_CNTL, mmOTG0_OTG_V_TOTAL,
 * mmOTG0_OTG_V_BLANK_START_END, mmOTG0_OTG_V_SYNC_A,
 * mmOTG0_OTG_V_SYNC_A_CNTL, mmOTG0_OTG_CONTROL,
 * mmOTG0_OTG_MASTER_UPDATE_LOCK, mmDIG0_DIG_FE_CNTL, mmDIG0_DIG_BE_CNTL,
 * mmDIG0_DIG_BE_EN_CNTL, mmDP0_DP_VID_STREAM_CNTL, mmDP0_DP_LINK_CNTL, and
 * the _BASE_IDX of each); the scaler's registers from the same header
 * (mmDSCL0_SCL_MODE, mmDSCL0_SCL_HORZ_FILTER_SCALE_RATIO,
 * mmDSCL0_SCL_VERT_FILTER_SCALE_RATIO, mmDSCL0_RECOUT_START,
 * mmDSCL0_RECOUT_SIZE, mmDSCL0_DSCL_AUTOCAL, and the same registers of
 * DSCL1 to DSCL5).
 * The registers the display's takeover adds, from the same header: the
 * plane's context select, flip interrupt and the two words of the address
 * it reads (mmHUBPREQ0_VMID_SETTINGS_0,
 * mmHUBPREQ0_DCSURF_SURFACE_FLIP_INTERRUPT,
 * mmHUBPREQ0_DCSURF_SURFACE_EARLIEST_INUSE,
 * mmHUBPREQ0_DCSURF_SURFACE_EARLIEST_INUSE_HIGH), the cursor's request
 * timing (mmHUBPREQ0_CURSOR_SETTINGS), the plane's cursor
 * (mmCURSOR0_0_CURSOR_CONTROL, mmCURSOR0_0_CURSOR_SURFACE_ADDRESS,
 * mmCURSOR0_0_CURSOR_SURFACE_ADDRESS_HIGH, mmCURSOR0_0_CURSOR_SIZE,
 * mmCURSOR0_0_CURSOR_POSITION, mmCURSOR0_0_CURSOR_HOT_SPOT,
 * mmCURSOR0_0_CURSOR_DST_OFFSET), the pixel processor's cursor
 * (mmCNVC_CUR0_CURSOR0_CONTROL, mmCNVC_CUR0_CURSOR0_FP_SCALE_BIAS and the
 * same registers of CNVC_CUR1 to CNVC_CUR5), the timing generator's
 * position, frame count and sync status (mmOTG0_OTG_STATUS_POSITION,
 * mmOTG0_OTG_STATUS_FRAME_COUNT, mmOTG0_OTG_GLOBAL_SYNC_STATUS) and the
 * hot-plug lines (mmHPD0_DC_HPD_INT_STATUS, mmHPD0_DC_HPD_INT_CONTROL).
 * derived: the distances are the differences between the offsets of
 * instance 1 and instance 0 in the same header; a scaler register's
 * distance from its pixel processor's control word is the same in all six
 * pipes (0x27, 0x2c, 0x30, 0x3d, 0x3e and 0x38), and so is a cursor
 * register's (0x1b and 0x1e).
 */
static const struct dcn_place dcn_places[AMDRDNA_DCN_REGISTER_COUNT] = {
	{ 1U, 0x0000U, 0x0000U, head_legacy_offsets },
	{ 1U, 0x0064U, 0x0000U, NULL },
	{ 1U, 0x0080U, 0x0004U, NULL },
	{ 1U, 0x0081U, 0x0004U, NULL },
	{ 1U, 0x0082U, 0x0004U, NULL },
	{ 2U, 0x05e5U, 0x00dcU, NULL },
	{ 2U, 0x05e7U, 0x00dcU, NULL },
	{ 2U, 0x05e9U, 0x00dcU, NULL },
	{ 2U, 0x05eaU, 0x00dcU, NULL },
	{ 2U, 0x05f3U, 0x00dcU, NULL },
	{ 2U, 0x05f4U, 0x00dcU, NULL },
	{ 2U, 0x0607U, 0x00dcU, NULL },
	{ 2U, 0x060aU, 0x00dcU, NULL },
	{ 2U, 0x060bU, 0x00dcU, NULL },
	{ 2U, 0x061aU, 0x00dcU, NULL },
	{ 2U, 0x061bU, 0x00dcU, NULL },
	{ 2U, 0x066cU, 0x00dcU, NULL },
	{ 2U, 0x0000U, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x1271U, 0x001bU, NULL },
	{ 2U, 0x1272U, 0x001bU, NULL },
	{ 2U, 0x1273U, 0x001bU, NULL },
	{ 2U, 0x1274U, 0x001bU, NULL },
	{ 2U, 0x1385U, 0x0004U, NULL },
	{ 2U, 0x1acbU, 0x0010U, NULL },
	{ 2U, 0x1acaU, 0x0010U, NULL },
	{ 2U, 0x1b2aU, 0x0080U, NULL },
	{ 2U, 0x1b2bU, 0x0080U, NULL },
	{ 2U, 0x1b2cU, 0x0080U, NULL },
	{ 2U, 0x1b2dU, 0x0080U, NULL },
	{ 2U, 0x1b2fU, 0x0080U, NULL },
	{ 2U, 0x1b36U, 0x0080U, NULL },
	{ 2U, 0x1b37U, 0x0080U, NULL },
	{ 2U, 0x1b38U, 0x0080U, NULL },
	{ 2U, 0x1b41U, 0x0080U, NULL },
	{ 2U, 0x1b8bU, 0x0080U, NULL },
	{ 2U, 0x2068U, 0x0100U, NULL },
	{ 2U, 0x20afU, 0x0100U, NULL },
	{ 2U, 0x20b0U, 0x0100U, NULL },
	{ 2U, 0x210cU, 0x0100U, NULL },
	{ 2U, 0x2108U, 0x0100U, NULL },
	{ 2U, 0x0027U, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x002cU, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x0030U, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x003dU, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x003eU, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x0038U, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x0609U, 0x00dcU, NULL },
	{ 2U, 0x0620U, 0x00dcU, NULL },
	{ 2U, 0x0625U, 0x00dcU, NULL },
	{ 2U, 0x0626U, 0x00dcU, NULL },
	{ 2U, 0x065eU, 0x00dcU, NULL },
	{ 2U, 0x0678U, 0x00dcU, NULL },
	{ 2U, 0x0679U, 0x00dcU, NULL },
	{ 2U, 0x067aU, 0x00dcU, NULL },
	{ 2U, 0x067bU, 0x00dcU, NULL },
	{ 2U, 0x067cU, 0x00dcU, NULL },
	{ 2U, 0x067dU, 0x00dcU, NULL },
	{ 2U, 0x067fU, 0x00dcU, NULL },
	{ 2U, 0x001bU, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x001eU, 0x0000U, pixel_processor_offsets },
	{ 2U, 0x1b4aU, 0x0080U, NULL },
	{ 2U, 0x1b4cU, 0x0080U, NULL },
	{ 2U, 0x1b8aU, 0x0080U, NULL },
	{ 2U, 0x1f14U, 0x0008U, NULL },
	{ 2U, 0x1f15U, 0x0008U, NULL }
};

/*
 * Reports the absolute dword index of one instance of a display register.
 *
 * Returns 0 for a register the table does not name, an instance past the
 * sixth pipe, or a display block without the register's window; a real
 * display register never lies at index 0.
 */
uint32_t
amdrdna_dcn_index(
	const struct amdrdna_ip *display,
	enum amdrdna_dcn_register which,
	unsigned instance)
{
	const struct dcn_place *place;
	uint32_t offset;

	/* A register or an instance the table does not hold has no address. */
	if ((unsigned)which >= AMDRDNA_DCN_REGISTER_COUNT)
		return 0U;
	if (instance >= AMDRDNA_DISPLAY_PIPES)
		return 0U;

	/* The display block must list the register's window. */
	place = &dcn_places[which];
	if (display->found == 0U)
		return 0U;
	if (place->window >= display->window_count)
		return 0U;

	/* An uneven family adds the register's distance to the instance's own base; the others repeat at their distance. */
	if (place->instances != NULL) {
		offset = place->instances[instance] + place->offset;
	} else {
		offset = place->offset + (uint32_t)place->stride * instance;
	}

	/* Succeeded: the window's start plus the offset. */
	return display->window[place->window] + offset;
}

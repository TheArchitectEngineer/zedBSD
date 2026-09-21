/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2008 Keith Packard
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright © 2019 Intel Corporation
 * Copyright © 2023 Intel Corporation
 * Copyright © 2020 Intel Corporation
 * Copyright © 2020-2021 Intel Corporation
 */

/*
 * Copyright © 2008-2015 Intel Corporation
 * Copyright © 2006-2019 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/*
 * All DisplayPort definitions the driver quotes: the DPCD, AUX, link
 * training and MSA definitions of Linux drm_dp.h, the DPCD capability
 * helpers of drm_dp_helper.h, the link training helpers of
 * intel_dp_link_training.h/.c, and the PPS and AUX registers of
 * intel_pps_regs.h and intel_dp_aux_regs.h, for the DP, modeset and hotplug
 * environments and display/ddi.c.
 *
 * The part after the first include guard (struct intel_pps of
 * intel_display_types.h and the PPS and AUX entry points of intel_pps.h and
 * intel_dp_aux.h) names the DP environment's own types and is compiled only
 * where display/dp-internal.h has defined I915_DISPLAY_WORLD_DP.
 *
 * The DRM prefix of the names is dropped: drm_dp_* is dp_* and enum
 * drm_dp_phy is enum dp_phy.  The partial copies of drm_dp.h the modeset,
 * hotplug and DDI code had (the DP_MSA_MISC_* bits, enum drm_dp_phy and two
 * macro extracts, each definition equal to the full copy) are not repeated.
 *
 * The DPCD, AUX, link training and MSA definitions (Linux include/drm/display/drm_dp.h).
 * zedBSD WS031: copied from Linux v6.8.12 include/drm/display/drm_dp.h
 * (sha256 306a1a47ba001c417baa3dab1f3a58c7e5de3c7a0537d26806a130603b599c5f) by tools/port_dp_aux_pps.py.
 * Change: the <linux/types.h> include is removed.
 *
 * The DPCD capability helpers (Linux include/drm/display/drm_dp_helper.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference include/drm/display/drm_dp_helper.h
 * (sha256 1969846dd3fdb5d7511319caf5f8e8481eacecad610fa429669450b9e5775b73) by tools/port_lcd_calc.py:
 * drm_dp_tps3_supported, drm_dp_tps4_supported, drm_dp_is_branch.
 * The notice above is the source file's own.
 *
 * The training pattern symbol helper (Linux intel_dp_link_training.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_dp_link_training.h
 * (sha256 0a4d31fd19d9db3d19fb5f44a21112457d6bd7495a43124fac5a1cb290a9a0cb) by tools/port_lcd_calc.py:
 * intel_dp_training_pattern_symbol.
 * The notice above is the source file's own.
 *
 * The link training log formats (Linux intel_dp_link_training.c).
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (sha256 af257fa29746e832225a5bb909d96d3398093cfe6345d0e35ba95aec991f2b2e) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.
 *
 * The panel power sequencer registers (Linux intel_pps_regs.h).
 * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_pps_regs.h
 * (sha256 ea2bc35d00482aa55408bb1f454639c5c5b9202b3ca188743c8b3b0a28b83d6e) by tools/port_dp_aux_pps.py.
 * Change: the include of intel_display_reg_defs.h is removed (dp_compat.h supplies the macros).
 *
 * The AUX channel registers (Linux intel_dp_aux_regs.h).
 * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp_aux_regs.h
 * (sha256 0db768cbf192dbfe7539aa9d538ec4832aecb8e81f75a4c86b2e6c7a946ec748) by tools/port_dp_aux_pps.py.
 * Change: the include of intel_display_reg_defs.h is removed (dp_compat.h supplies the macros).
 *
 * The panel power sequencer state (Linux intel_display_types.h).
 * zedBSD WS031: `struct intel_pps` extracted textually from the Linux v6.8.12 i915 reference
 * display/intel_display_types.h (MIT permission notice, Copyright Intel Corporation; the full
 * notice is kept in intel_pps_port.c's source header) by tools/port_dp_aux_pps.py.
 *
 * The panel power sequencer entry points (Linux intel_pps.h).
 * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_pps.h
 * (sha256 a8e8ad1455ad552081eb82897badf09722cc5bdb8aa543c3f90ff4eb5d54dff4) by tools/port_dp_aux_pps.py.
 * Change: the <linux/types.h> and intel_wakeref.h includes are removed.
 *
 * The AUX channel entry points (Linux intel_dp_aux.h).
 * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp_aux.h
 * (sha256 ea6e014ed7f352388bfed847de49c15b780ef0f770aa0803a7497eb079326767) by tools/port_dp_aux_pps.py.
 * Change: the <linux/types.h> include is removed.
 */

#ifndef DRIVERS_GPU_I915_INTEL_DP_H
#define DRIVERS_GPU_I915_INTEL_DP_H

/* The DPCD, AUX, link training and MSA definitions (Linux include/drm/display/drm_dp.h). */

/* zedBSD: types come from dp_compat.h, which includes this file */

/*
 * Unless otherwise noted, all values are from the DP 1.1a spec.  Note that
 * DP and DPCD versions are independent.  Differences from 1.0 are not noted,
 * 1.0 devices basically don't exist in the wild.
 *
 * Abbreviations, in chronological order:
 *
 * eDP: Embedded DisplayPort version 1
 * DPI: DisplayPort Interoperability Guideline v1.1a
 * 1.2: DisplayPort 1.2
 * MST: Multistream Transport - part of DP 1.2a
 *
 * 1.2 formally includes both eDP and DPI definitions.
 */

/* MSA (Main Stream Attribute) MISC bits (as MISC1<<8|MISC0) */
#define DP_MSA_MISC_SYNC_CLOCK			(1 << 0)
#define DP_MSA_MISC_INTERLACE_VTOTAL_EVEN	(1 << 8)
#define DP_MSA_MISC_STEREO_NO_3D		(0 << 9)
#define DP_MSA_MISC_STEREO_PROG_RIGHT_EYE	(1 << 9)
#define DP_MSA_MISC_STEREO_PROG_LEFT_EYE	(3 << 9)
/* bits per component for non-RAW */
#define DP_MSA_MISC_6_BPC			(0 << 5)
#define DP_MSA_MISC_8_BPC			(1 << 5)
#define DP_MSA_MISC_10_BPC			(2 << 5)
#define DP_MSA_MISC_12_BPC			(3 << 5)
#define DP_MSA_MISC_16_BPC			(4 << 5)
/* bits per component for RAW */
#define DP_MSA_MISC_RAW_6_BPC			(1 << 5)
#define DP_MSA_MISC_RAW_7_BPC			(2 << 5)
#define DP_MSA_MISC_RAW_8_BPC			(3 << 5)
#define DP_MSA_MISC_RAW_10_BPC			(4 << 5)
#define DP_MSA_MISC_RAW_12_BPC			(5 << 5)
#define DP_MSA_MISC_RAW_14_BPC			(6 << 5)
#define DP_MSA_MISC_RAW_16_BPC			(7 << 5)
/* pixel encoding/colorimetry format */
#define _DP_MSA_MISC_COLOR(misc1_7, misc0_21, misc0_3, misc0_4) \
	((misc1_7) << 15 | (misc0_4) << 4 | (misc0_3) << 3 | ((misc0_21) << 1))
#define DP_MSA_MISC_COLOR_RGB			_DP_MSA_MISC_COLOR(0, 0, 0, 0)
#define DP_MSA_MISC_COLOR_CEA_RGB		_DP_MSA_MISC_COLOR(0, 0, 1, 0)
#define DP_MSA_MISC_COLOR_RGB_WIDE_FIXED	_DP_MSA_MISC_COLOR(0, 3, 0, 0)
#define DP_MSA_MISC_COLOR_RGB_WIDE_FLOAT	_DP_MSA_MISC_COLOR(0, 3, 0, 1)
#define DP_MSA_MISC_COLOR_Y_ONLY		_DP_MSA_MISC_COLOR(1, 0, 0, 0)
#define DP_MSA_MISC_COLOR_RAW			_DP_MSA_MISC_COLOR(1, 1, 0, 0)
#define DP_MSA_MISC_COLOR_YCBCR_422_BT601	_DP_MSA_MISC_COLOR(0, 1, 1, 0)
#define DP_MSA_MISC_COLOR_YCBCR_422_BT709	_DP_MSA_MISC_COLOR(0, 1, 1, 1)
#define DP_MSA_MISC_COLOR_YCBCR_444_BT601	_DP_MSA_MISC_COLOR(0, 2, 1, 0)
#define DP_MSA_MISC_COLOR_YCBCR_444_BT709	_DP_MSA_MISC_COLOR(0, 2, 1, 1)
#define DP_MSA_MISC_COLOR_XVYCC_422_BT601	_DP_MSA_MISC_COLOR(0, 1, 0, 0)
#define DP_MSA_MISC_COLOR_XVYCC_422_BT709	_DP_MSA_MISC_COLOR(0, 1, 0, 1)
#define DP_MSA_MISC_COLOR_XVYCC_444_BT601	_DP_MSA_MISC_COLOR(0, 2, 0, 0)
#define DP_MSA_MISC_COLOR_XVYCC_444_BT709	_DP_MSA_MISC_COLOR(0, 2, 0, 1)
#define DP_MSA_MISC_COLOR_OPRGB			_DP_MSA_MISC_COLOR(0, 0, 1, 1)
#define DP_MSA_MISC_COLOR_DCI_P3		_DP_MSA_MISC_COLOR(0, 3, 1, 0)
#define DP_MSA_MISC_COLOR_COLOR_PROFILE		_DP_MSA_MISC_COLOR(0, 3, 1, 1)
#define DP_MSA_MISC_COLOR_VSC_SDP		(1 << 14)

#define DP_AUX_MAX_PAYLOAD_BYTES	16

#define DP_AUX_I2C_WRITE		0x0
#define DP_AUX_I2C_READ			0x1
#define DP_AUX_I2C_WRITE_STATUS_UPDATE	0x2
#define DP_AUX_I2C_MOT			0x4
#define DP_AUX_NATIVE_WRITE		0x8
#define DP_AUX_NATIVE_READ		0x9

#define DP_AUX_NATIVE_REPLY_ACK		(0x0 << 0)
#define DP_AUX_NATIVE_REPLY_NACK	(0x1 << 0)
#define DP_AUX_NATIVE_REPLY_DEFER	(0x2 << 0)
#define DP_AUX_NATIVE_REPLY_MASK	(0x3 << 0)

#define DP_AUX_I2C_REPLY_ACK		(0x0 << 2)
#define DP_AUX_I2C_REPLY_NACK		(0x1 << 2)
#define DP_AUX_I2C_REPLY_DEFER		(0x2 << 2)
#define DP_AUX_I2C_REPLY_MASK		(0x3 << 2)

/* DPCD Field Address Mapping */

/* Receiver Capability */
#define DP_DPCD_REV                         0x000
# define DP_DPCD_REV_10                     0x10
# define DP_DPCD_REV_11                     0x11
# define DP_DPCD_REV_12                     0x12
# define DP_DPCD_REV_13                     0x13
# define DP_DPCD_REV_14                     0x14

#define DP_MAX_LINK_RATE                    0x001

#define DP_MAX_LANE_COUNT                   0x002
# define DP_MAX_LANE_COUNT_MASK		    0x1f
# define DP_TPS3_SUPPORTED		    (1 << 6) /* 1.2 */
# define DP_ENHANCED_FRAME_CAP		    (1 << 7)

#define DP_MAX_DOWNSPREAD                   0x003
# define DP_MAX_DOWNSPREAD_0_5		    (1 << 0)
# define DP_STREAM_REGENERATION_STATUS_CAP  (1 << 1) /* 2.0 */
# define DP_NO_AUX_HANDSHAKE_LINK_TRAINING  (1 << 6)
# define DP_TPS4_SUPPORTED                  (1 << 7)

#define DP_NORP                             0x004

#define DP_DOWNSTREAMPORT_PRESENT           0x005
# define DP_DWN_STRM_PORT_PRESENT           (1 << 0)
# define DP_DWN_STRM_PORT_TYPE_MASK         0x06
# define DP_DWN_STRM_PORT_TYPE_DP           (0 << 1)
# define DP_DWN_STRM_PORT_TYPE_ANALOG       (1 << 1)
# define DP_DWN_STRM_PORT_TYPE_TMDS         (2 << 1)
# define DP_DWN_STRM_PORT_TYPE_OTHER        (3 << 1)
# define DP_FORMAT_CONVERSION               (1 << 3)
# define DP_DETAILED_CAP_INFO_AVAILABLE	    (1 << 4) /* DPI */

#define DP_MAIN_LINK_CHANNEL_CODING         0x006
# define DP_CAP_ANSI_8B10B		    (1 << 0)
# define DP_CAP_ANSI_128B132B               (1 << 1) /* 2.0 */

#define DP_DOWN_STREAM_PORT_COUNT	    0x007
# define DP_PORT_COUNT_MASK		    0x0f
# define DP_MSA_TIMING_PAR_IGNORED	    (1 << 6) /* eDP */
# define DP_OUI_SUPPORT			    (1 << 7)

#define DP_RECEIVE_PORT_0_CAP_0		    0x008
# define DP_LOCAL_EDID_PRESENT		    (1 << 1)
# define DP_ASSOCIATED_TO_PRECEDING_PORT    (1 << 2)
# define DP_HBLANK_EXPANSION_CAPABLE        (1 << 3)

#define DP_RECEIVE_PORT_0_BUFFER_SIZE	    0x009

#define DP_RECEIVE_PORT_1_CAP_0		    0x00a
#define DP_RECEIVE_PORT_1_BUFFER_SIZE       0x00b

#define DP_I2C_SPEED_CAP		    0x00c    /* DPI */
# define DP_I2C_SPEED_1K		    0x01
# define DP_I2C_SPEED_5K		    0x02
# define DP_I2C_SPEED_10K		    0x04
# define DP_I2C_SPEED_100K		    0x08
# define DP_I2C_SPEED_400K		    0x10
# define DP_I2C_SPEED_1M		    0x20

#define DP_EDP_CONFIGURATION_CAP            0x00d   /* XXX 1.2? */
# define DP_ALTERNATE_SCRAMBLER_RESET_CAP   (1 << 0)
# define DP_FRAMING_CHANGE_CAP		    (1 << 1)
# define DP_DPCD_DISPLAY_CONTROL_CAPABLE     (1 << 3) /* edp v1.2 or higher */

#define DP_TRAINING_AUX_RD_INTERVAL             0x00e   /* XXX 1.2? */
# define DP_TRAINING_AUX_RD_MASK                0x7F    /* DP 1.3 */
# define DP_EXTENDED_RECEIVER_CAP_FIELD_PRESENT	(1 << 7) /* DP 1.3 */

#define DP_ADAPTER_CAP			    0x00f   /* 1.2 */
# define DP_FORCE_LOAD_SENSE_CAP	    (1 << 0)
# define DP_ALTERNATE_I2C_PATTERN_CAP	    (1 << 1)

#define DP_SUPPORTED_LINK_RATES		    0x010 /* eDP 1.4 */
# define DP_MAX_SUPPORTED_RATES		     8	    /* 16-bit little-endian */

/* Multiple stream transport */
#define DP_FAUX_CAP			    0x020   /* 1.2 */
# define DP_FAUX_CAP_1			    (1 << 0)

#define DP_SINK_VIDEO_FALLBACK_FORMATS      0x020   /* 2.0 */
# define DP_FALLBACK_1024x768_60HZ_24BPP    (1 << 0)
# define DP_FALLBACK_1280x720_60HZ_24BPP    (1 << 1)
# define DP_FALLBACK_1920x1080_60HZ_24BPP   (1 << 2)

#define DP_MSTM_CAP			    0x021   /* 1.2 */
# define DP_MST_CAP			    (1 << 0)
# define DP_SINGLE_STREAM_SIDEBAND_MSG      (1 << 1) /* 2.0 */

#define DP_NUMBER_OF_AUDIO_ENDPOINTS	    0x022   /* 1.2 */

/* AV_SYNC_DATA_BLOCK                                  1.2 */
#define DP_AV_GRANULARITY		    0x023
# define DP_AG_FACTOR_MASK		    (0xf << 0)
# define DP_AG_FACTOR_3MS		    (0 << 0)
# define DP_AG_FACTOR_2MS		    (1 << 0)
# define DP_AG_FACTOR_1MS		    (2 << 0)
# define DP_AG_FACTOR_500US		    (3 << 0)
# define DP_AG_FACTOR_200US		    (4 << 0)
# define DP_AG_FACTOR_100US		    (5 << 0)
# define DP_AG_FACTOR_10US		    (6 << 0)
# define DP_AG_FACTOR_1US		    (7 << 0)
# define DP_VG_FACTOR_MASK		    (0xf << 4)
# define DP_VG_FACTOR_3MS		    (0 << 4)
# define DP_VG_FACTOR_2MS		    (1 << 4)
# define DP_VG_FACTOR_1MS		    (2 << 4)
# define DP_VG_FACTOR_500US		    (3 << 4)
# define DP_VG_FACTOR_200US		    (4 << 4)
# define DP_VG_FACTOR_100US		    (5 << 4)

#define DP_AUD_DEC_LAT0			    0x024
#define DP_AUD_DEC_LAT1			    0x025

#define DP_AUD_PP_LAT0			    0x026
#define DP_AUD_PP_LAT1			    0x027

#define DP_VID_INTER_LAT		    0x028

#define DP_VID_PROG_LAT			    0x029

#define DP_REP_LAT			    0x02a

#define DP_AUD_DEL_INS0			    0x02b
#define DP_AUD_DEL_INS1			    0x02c
#define DP_AUD_DEL_INS2			    0x02d
/* End of AV_SYNC_DATA_BLOCK */

#define DP_RECEIVER_ALPM_CAP		    0x02e   /* eDP 1.4 */
# define DP_ALPM_CAP			    (1 << 0)

#define DP_SINK_DEVICE_AUX_FRAME_SYNC_CAP   0x02f   /* eDP 1.4 */
# define DP_AUX_FRAME_SYNC_CAP		    (1 << 0)

#define DP_GUID				    0x030   /* 1.2 */

#define DP_DSC_SUPPORT                      0x060   /* DP 1.4 */
# define DP_DSC_DECOMPRESSION_IS_SUPPORTED  (1 << 0)
# define DP_DSC_PASSTHROUGH_IS_SUPPORTED    (1 << 1)
# define DP_DSC_DYNAMIC_PPS_UPDATE_SUPPORT_COMP_TO_COMP    (1 << 2)
# define DP_DSC_DYNAMIC_PPS_UPDATE_SUPPORT_UNCOMP_TO_COMP  (1 << 3)

#define DP_DSC_REV                          0x061
# define DP_DSC_MAJOR_MASK                  (0xf << 0)
# define DP_DSC_MINOR_MASK                  (0xf << 4)
# define DP_DSC_MAJOR_SHIFT                 0
# define DP_DSC_MINOR_SHIFT                 4

#define DP_DSC_RC_BUF_BLK_SIZE              0x062
# define DP_DSC_RC_BUF_BLK_SIZE_1           0x0
# define DP_DSC_RC_BUF_BLK_SIZE_4           0x1
# define DP_DSC_RC_BUF_BLK_SIZE_16          0x2
# define DP_DSC_RC_BUF_BLK_SIZE_64          0x3

#define DP_DSC_RC_BUF_SIZE                  0x063

#define DP_DSC_SLICE_CAP_1                  0x064
# define DP_DSC_1_PER_DP_DSC_SINK           (1 << 0)
# define DP_DSC_2_PER_DP_DSC_SINK           (1 << 1)
# define DP_DSC_4_PER_DP_DSC_SINK           (1 << 3)
# define DP_DSC_6_PER_DP_DSC_SINK           (1 << 4)
# define DP_DSC_8_PER_DP_DSC_SINK           (1 << 5)
# define DP_DSC_10_PER_DP_DSC_SINK          (1 << 6)
# define DP_DSC_12_PER_DP_DSC_SINK          (1 << 7)

#define DP_DSC_LINE_BUF_BIT_DEPTH           0x065
# define DP_DSC_LINE_BUF_BIT_DEPTH_MASK     (0xf << 0)
# define DP_DSC_LINE_BUF_BIT_DEPTH_9        0x0
# define DP_DSC_LINE_BUF_BIT_DEPTH_10       0x1
# define DP_DSC_LINE_BUF_BIT_DEPTH_11       0x2
# define DP_DSC_LINE_BUF_BIT_DEPTH_12       0x3
# define DP_DSC_LINE_BUF_BIT_DEPTH_13       0x4
# define DP_DSC_LINE_BUF_BIT_DEPTH_14       0x5
# define DP_DSC_LINE_BUF_BIT_DEPTH_15       0x6
# define DP_DSC_LINE_BUF_BIT_DEPTH_16       0x7
# define DP_DSC_LINE_BUF_BIT_DEPTH_8        0x8

#define DP_DSC_BLK_PREDICTION_SUPPORT       0x066
# define DP_DSC_BLK_PREDICTION_IS_SUPPORTED (1 << 0)
# define DP_DSC_RGB_COLOR_CONV_BYPASS_SUPPORT (1 << 1)

#define DP_DSC_MAX_BITS_PER_PIXEL_LOW       0x067   /* eDP 1.4 */

#define DP_DSC_MAX_BITS_PER_PIXEL_HI        0x068   /* eDP 1.4 */
# define DP_DSC_MAX_BITS_PER_PIXEL_HI_MASK  (0x3 << 0)
# define DP_DSC_MAX_BPP_DELTA_VERSION_MASK  (0x3 << 5)	/* eDP 1.5 & DP 2.0 */
# define DP_DSC_MAX_BPP_DELTA_AVAILABILITY  (1 << 7)	/* eDP 1.5 & DP 2.0 */

#define DP_DSC_DEC_COLOR_FORMAT_CAP         0x069
# define DP_DSC_RGB                         (1 << 0)
# define DP_DSC_YCbCr444                    (1 << 1)
# define DP_DSC_YCbCr422_Simple             (1 << 2)
# define DP_DSC_YCbCr422_Native             (1 << 3)
# define DP_DSC_YCbCr420_Native             (1 << 4)

#define DP_DSC_DEC_COLOR_DEPTH_CAP          0x06A
# define DP_DSC_8_BPC                       (1 << 1)
# define DP_DSC_10_BPC                      (1 << 2)
# define DP_DSC_12_BPC                      (1 << 3)

#define DP_DSC_PEAK_THROUGHPUT              0x06B
# define DP_DSC_THROUGHPUT_MODE_0_MASK      (0xf << 0)
# define DP_DSC_THROUGHPUT_MODE_0_SHIFT     0
# define DP_DSC_THROUGHPUT_MODE_0_UNSUPPORTED 0
# define DP_DSC_THROUGHPUT_MODE_0_340       (1 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_400       (2 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_450       (3 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_500       (4 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_550       (5 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_600       (6 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_650       (7 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_700       (8 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_750       (9 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_800       (10 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_850       (11 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_900       (12 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_950       (13 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_1000      (14 << 0)
# define DP_DSC_THROUGHPUT_MODE_0_170       (15 << 0) /* 1.4a */
# define DP_DSC_THROUGHPUT_MODE_1_MASK      (0xf << 4)
# define DP_DSC_THROUGHPUT_MODE_1_SHIFT     4
# define DP_DSC_THROUGHPUT_MODE_1_UNSUPPORTED 0
# define DP_DSC_THROUGHPUT_MODE_1_340       (1 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_400       (2 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_450       (3 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_500       (4 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_550       (5 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_600       (6 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_650       (7 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_700       (8 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_750       (9 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_800       (10 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_850       (11 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_900       (12 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_950       (13 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_1000      (14 << 4)
# define DP_DSC_THROUGHPUT_MODE_1_170       (15 << 4)

#define DP_DSC_MAX_SLICE_WIDTH              0x06C
#define DP_DSC_MIN_SLICE_WIDTH_VALUE        2560
#define DP_DSC_SLICE_WIDTH_MULTIPLIER       320

#define DP_DSC_SLICE_CAP_2                  0x06D
# define DP_DSC_16_PER_DP_DSC_SINK          (1 << 0)
# define DP_DSC_20_PER_DP_DSC_SINK          (1 << 1)
# define DP_DSC_24_PER_DP_DSC_SINK          (1 << 2)

#define DP_DSC_BITS_PER_PIXEL_INC           0x06F
# define DP_DSC_RGB_YCbCr444_MAX_BPP_DELTA_MASK 0x1f
# define DP_DSC_RGB_YCbCr420_MAX_BPP_DELTA_MASK 0xe0
# define DP_DSC_BITS_PER_PIXEL_1_16         0x0
# define DP_DSC_BITS_PER_PIXEL_1_8          0x1
# define DP_DSC_BITS_PER_PIXEL_1_4          0x2
# define DP_DSC_BITS_PER_PIXEL_1_2          0x3
# define DP_DSC_BITS_PER_PIXEL_1_1          0x4

#define DP_PSR_SUPPORT                      0x070   /* XXX 1.2? */
# define DP_PSR_IS_SUPPORTED                1
# define DP_PSR2_IS_SUPPORTED		    2	    /* eDP 1.4 */
# define DP_PSR2_WITH_Y_COORD_IS_SUPPORTED  3	    /* eDP 1.4a */
# define DP_PSR2_WITH_Y_COORD_ET_SUPPORTED  4	    /* eDP 1.5, adopted eDP 1.4b SCR */

#define DP_PSR_CAPS                         0x071   /* XXX 1.2? */
# define DP_PSR_NO_TRAIN_ON_EXIT            1
# define DP_PSR_SETUP_TIME_330              (0 << 1)
# define DP_PSR_SETUP_TIME_275              (1 << 1)
# define DP_PSR_SETUP_TIME_220              (2 << 1)
# define DP_PSR_SETUP_TIME_165              (3 << 1)
# define DP_PSR_SETUP_TIME_110              (4 << 1)
# define DP_PSR_SETUP_TIME_55               (5 << 1)
# define DP_PSR_SETUP_TIME_0                (6 << 1)
# define DP_PSR_SETUP_TIME_MASK             (7 << 1)
# define DP_PSR_SETUP_TIME_SHIFT            1
# define DP_PSR2_SU_Y_COORDINATE_REQUIRED   (1 << 4)  /* eDP 1.4a */
# define DP_PSR2_SU_GRANULARITY_REQUIRED    (1 << 5)  /* eDP 1.4b */
# define DP_PSR2_SU_AUX_FRAME_SYNC_NOT_NEEDED (1 << 6)/* eDP 1.5, adopted eDP 1.4b SCR */

#define DP_PSR2_SU_X_GRANULARITY	    0x072 /* eDP 1.4b */
#define DP_PSR2_SU_Y_GRANULARITY	    0x074 /* eDP 1.4b */

/*
 * 0x80-0x8f describe downstream port capabilities, but there are two layouts
 * based on whether DP_DETAILED_CAP_INFO_AVAILABLE was set.  If it was not,
 * each port's descriptor is one byte wide.  If it was set, each port's is
 * four bytes wide, starting with the one byte from the base info.  As of
 * DP interop v1.1a only VGA defines additional detail.
 */

/* offset 0 */
#define DP_DOWNSTREAM_PORT_0		    0x80
# define DP_DS_PORT_TYPE_MASK		    (7 << 0)
# define DP_DS_PORT_TYPE_DP		    0
# define DP_DS_PORT_TYPE_VGA		    1
# define DP_DS_PORT_TYPE_DVI		    2
# define DP_DS_PORT_TYPE_HDMI		    3
# define DP_DS_PORT_TYPE_NON_EDID	    4
# define DP_DS_PORT_TYPE_DP_DUALMODE        5
# define DP_DS_PORT_TYPE_WIRELESS           6
# define DP_DS_PORT_HPD			    (1 << 3)
# define DP_DS_NON_EDID_MASK		    (0xf << 4)
# define DP_DS_NON_EDID_720x480i_60	    (1 << 4)
# define DP_DS_NON_EDID_720x480i_50	    (2 << 4)
# define DP_DS_NON_EDID_1920x1080i_60	    (3 << 4)
# define DP_DS_NON_EDID_1920x1080i_50	    (4 << 4)
# define DP_DS_NON_EDID_1280x720_60	    (5 << 4)
# define DP_DS_NON_EDID_1280x720_50	    (7 << 4)
/* offset 1 for VGA is maximum megapixels per second / 8 */
/* offset 1 for DVI/HDMI is maximum TMDS clock in Mbps / 2.5 */
/* offset 2 for VGA/DVI/HDMI */
# define DP_DS_MAX_BPC_MASK	            (3 << 0)
# define DP_DS_8BPC		            0
# define DP_DS_10BPC		            1
# define DP_DS_12BPC		            2
# define DP_DS_16BPC		            3
/* HDMI2.1 PCON FRL CONFIGURATION */
# define DP_PCON_MAX_FRL_BW                 (7 << 2)
# define DP_PCON_MAX_0GBPS                  (0 << 2)
# define DP_PCON_MAX_9GBPS                  (1 << 2)
# define DP_PCON_MAX_18GBPS                 (2 << 2)
# define DP_PCON_MAX_24GBPS                 (3 << 2)
# define DP_PCON_MAX_32GBPS                 (4 << 2)
# define DP_PCON_MAX_40GBPS                 (5 << 2)
# define DP_PCON_MAX_48GBPS                 (6 << 2)
# define DP_PCON_SOURCE_CTL_MODE            (1 << 5)

/* offset 3 for DVI */
# define DP_DS_DVI_DUAL_LINK		    (1 << 1)
# define DP_DS_DVI_HIGH_COLOR_DEPTH	    (1 << 2)
/* offset 3 for HDMI */
# define DP_DS_HDMI_FRAME_SEQ_TO_FRAME_PACK (1 << 0)
# define DP_DS_HDMI_YCBCR422_PASS_THROUGH   (1 << 1)
# define DP_DS_HDMI_YCBCR420_PASS_THROUGH   (1 << 2)
# define DP_DS_HDMI_YCBCR444_TO_422_CONV    (1 << 3)
# define DP_DS_HDMI_YCBCR444_TO_420_CONV    (1 << 4)

/*
 * VESA DP-to-HDMI PCON Specification adds caps for colorspace
 * conversion in DFP cap DPCD 83h. Sec6.1 Table-3.
 * Based on the available support the source can enable
 * color conversion by writing into PROTOCOL_COVERTER_CONTROL_2
 * DPCD 3052h.
 */
# define DP_DS_HDMI_BT601_RGB_YCBCR_CONV    (1 << 5)
# define DP_DS_HDMI_BT709_RGB_YCBCR_CONV    (1 << 6)
# define DP_DS_HDMI_BT2020_RGB_YCBCR_CONV   (1 << 7)

#define DP_MAX_DOWNSTREAM_PORTS		    0x10

/* DP Forward error Correction Registers */
#define DP_FEC_CAPABILITY		    0x090    /* 1.4 */
# define DP_FEC_CAPABLE			    (1 << 0)
# define DP_FEC_UNCORR_BLK_ERROR_COUNT_CAP  (1 << 1)
# define DP_FEC_CORR_BLK_ERROR_COUNT_CAP    (1 << 2)
# define DP_FEC_BIT_ERROR_COUNT_CAP	    (1 << 3)
#define DP_FEC_CAPABILITY_1			0x091   /* 2.0 */

/* DP-HDMI2.1 PCON DSC ENCODER SUPPORT */
#define DP_PCON_DSC_ENCODER_CAP_SIZE        0xD	/* 0x92 through 0x9E */
#define DP_PCON_DSC_ENCODER                 0x092
# define DP_PCON_DSC_ENCODER_SUPPORTED      (1 << 0)
# define DP_PCON_DSC_PPS_ENC_OVERRIDE       (1 << 1)

/* DP-HDMI2.1 PCON DSC Version */
#define DP_PCON_DSC_VERSION                 0x093
# define DP_PCON_DSC_MAJOR_MASK		    (0xF << 0)
# define DP_PCON_DSC_MINOR_MASK		    (0xF << 4)
# define DP_PCON_DSC_MAJOR_SHIFT	    0
# define DP_PCON_DSC_MINOR_SHIFT	    4

/* DP-HDMI2.1 PCON DSC RC Buffer block size */
#define DP_PCON_DSC_RC_BUF_BLK_INFO	    0x094
# define DP_PCON_DSC_RC_BUF_BLK_SIZE	    (0x3 << 0)
# define DP_PCON_DSC_RC_BUF_BLK_1KB	    0
# define DP_PCON_DSC_RC_BUF_BLK_4KB	    1
# define DP_PCON_DSC_RC_BUF_BLK_16KB	    2
# define DP_PCON_DSC_RC_BUF_BLK_64KB	    3

/* DP-HDMI2.1 PCON DSC RC Buffer size */
#define DP_PCON_DSC_RC_BUF_SIZE		    0x095

/* DP-HDMI2.1 PCON DSC Slice capabilities-1 */
#define DP_PCON_DSC_SLICE_CAP_1		    0x096
# define DP_PCON_DSC_1_PER_DSC_ENC     (0x1 << 0)
# define DP_PCON_DSC_2_PER_DSC_ENC     (0x1 << 1)
# define DP_PCON_DSC_4_PER_DSC_ENC     (0x1 << 3)
# define DP_PCON_DSC_6_PER_DSC_ENC     (0x1 << 4)
# define DP_PCON_DSC_8_PER_DSC_ENC     (0x1 << 5)
# define DP_PCON_DSC_10_PER_DSC_ENC    (0x1 << 6)
# define DP_PCON_DSC_12_PER_DSC_ENC    (0x1 << 7)

#define DP_PCON_DSC_BUF_BIT_DEPTH	    0x097
# define DP_PCON_DSC_BIT_DEPTH_MASK	    (0xF << 0)
# define DP_PCON_DSC_DEPTH_9_BITS	    0
# define DP_PCON_DSC_DEPTH_10_BITS	    1
# define DP_PCON_DSC_DEPTH_11_BITS	    2
# define DP_PCON_DSC_DEPTH_12_BITS	    3
# define DP_PCON_DSC_DEPTH_13_BITS	    4
# define DP_PCON_DSC_DEPTH_14_BITS	    5
# define DP_PCON_DSC_DEPTH_15_BITS	    6
# define DP_PCON_DSC_DEPTH_16_BITS	    7
# define DP_PCON_DSC_DEPTH_8_BITS	    8

#define DP_PCON_DSC_BLOCK_PREDICTION	    0x098
# define DP_PCON_DSC_BLOCK_PRED_SUPPORT	    (0x1 << 0)

#define DP_PCON_DSC_ENC_COLOR_FMT_CAP	    0x099
# define DP_PCON_DSC_ENC_RGB		    (0x1 << 0)
# define DP_PCON_DSC_ENC_YUV444		    (0x1 << 1)
# define DP_PCON_DSC_ENC_YUV422_S	    (0x1 << 2)
# define DP_PCON_DSC_ENC_YUV422_N	    (0x1 << 3)
# define DP_PCON_DSC_ENC_YUV420_N	    (0x1 << 4)

#define DP_PCON_DSC_ENC_COLOR_DEPTH_CAP	    0x09A
# define DP_PCON_DSC_ENC_8BPC		    (0x1 << 1)
# define DP_PCON_DSC_ENC_10BPC		    (0x1 << 2)
# define DP_PCON_DSC_ENC_12BPC		    (0x1 << 3)

#define DP_PCON_DSC_MAX_SLICE_WIDTH	    0x09B

/* DP-HDMI2.1 PCON DSC Slice capabilities-2 */
#define DP_PCON_DSC_SLICE_CAP_2             0x09C
# define DP_PCON_DSC_16_PER_DSC_ENC	    (0x1 << 0)
# define DP_PCON_DSC_20_PER_DSC_ENC         (0x1 << 1)
# define DP_PCON_DSC_24_PER_DSC_ENC         (0x1 << 2)

/* DP-HDMI2.1 PCON HDMI TX Encoder Bits/pixel increment */
#define DP_PCON_DSC_BPP_INCR		    0x09E
# define DP_PCON_DSC_BPP_INCR_MASK	    (0x7 << 0)
# define DP_PCON_DSC_ONE_16TH_BPP	    0
# define DP_PCON_DSC_ONE_8TH_BPP	    1
# define DP_PCON_DSC_ONE_4TH_BPP	    2
# define DP_PCON_DSC_ONE_HALF_BPP	    3
# define DP_PCON_DSC_ONE_BPP		    4

/* DP Extended DSC Capabilities */
#define DP_DSC_BRANCH_OVERALL_THROUGHPUT_0  0x0a0   /* DP 1.4a SCR */
#define DP_DSC_BRANCH_OVERALL_THROUGHPUT_1  0x0a1
#define DP_DSC_BRANCH_MAX_LINE_WIDTH        0x0a2

/* DFP Capability Extension */
#define DP_DFP_CAPABILITY_EXTENSION_SUPPORT	0x0a3	/* 2.0 */

#define DP_PANEL_REPLAY_CAP                 0x0b0  /* DP 2.0 */
# define DP_PANEL_REPLAY_SUPPORT            (1 << 0)
# define DP_PANEL_REPLAY_SU_SUPPORT         (1 << 1)

/* Link Configuration */
#define	DP_LINK_BW_SET		            0x100
# define DP_LINK_RATE_TABLE		    0x00    /* eDP 1.4 */
# define DP_LINK_BW_1_62		    0x06
# define DP_LINK_BW_2_7			    0x0a
# define DP_LINK_BW_5_4			    0x14    /* 1.2 */
# define DP_LINK_BW_8_1			    0x1e    /* 1.4 */
# define DP_LINK_BW_10                      0x01    /* 2.0 128b/132b Link Layer */
# define DP_LINK_BW_13_5                    0x04    /* 2.0 128b/132b Link Layer */
# define DP_LINK_BW_20                      0x02    /* 2.0 128b/132b Link Layer */

#define DP_LANE_COUNT_SET	            0x101
# define DP_LANE_COUNT_MASK		    0x0f
# define DP_LANE_COUNT_ENHANCED_FRAME_EN    (1 << 7)

#define DP_TRAINING_PATTERN_SET	            0x102
# define DP_TRAINING_PATTERN_DISABLE	    0
# define DP_TRAINING_PATTERN_1		    1
# define DP_TRAINING_PATTERN_2		    2
# define DP_TRAINING_PATTERN_2_CDS	    3	    /* 2.0 E11 */
# define DP_TRAINING_PATTERN_3		    3	    /* 1.2 */
# define DP_TRAINING_PATTERN_4              7       /* 1.4 */
# define DP_TRAINING_PATTERN_MASK	    0x3
# define DP_TRAINING_PATTERN_MASK_1_4	    0xf

/* DPCD 1.1 only. For DPCD >= 1.2 see per-lane DP_LINK_QUAL_LANEn_SET */
# define DP_LINK_QUAL_PATTERN_11_DISABLE    (0 << 2)
# define DP_LINK_QUAL_PATTERN_11_D10_2	    (1 << 2)
# define DP_LINK_QUAL_PATTERN_11_ERROR_RATE (2 << 2)
# define DP_LINK_QUAL_PATTERN_11_PRBS7	    (3 << 2)
# define DP_LINK_QUAL_PATTERN_11_MASK	    (3 << 2)

# define DP_RECOVERED_CLOCK_OUT_EN	    (1 << 4)
# define DP_LINK_SCRAMBLING_DISABLE	    (1 << 5)

# define DP_SYMBOL_ERROR_COUNT_BOTH	    (0 << 6)
# define DP_SYMBOL_ERROR_COUNT_DISPARITY    (1 << 6)
# define DP_SYMBOL_ERROR_COUNT_SYMBOL	    (2 << 6)
# define DP_SYMBOL_ERROR_COUNT_MASK	    (3 << 6)

#define DP_TRAINING_LANE0_SET		    0x103
#define DP_TRAINING_LANE1_SET		    0x104
#define DP_TRAINING_LANE2_SET		    0x105
#define DP_TRAINING_LANE3_SET		    0x106

# define DP_TRAIN_VOLTAGE_SWING_MASK	    0x3
# define DP_TRAIN_VOLTAGE_SWING_SHIFT	    0
# define DP_TRAIN_MAX_SWING_REACHED	    (1 << 2)
# define DP_TRAIN_VOLTAGE_SWING_LEVEL_0 (0 << 0)
# define DP_TRAIN_VOLTAGE_SWING_LEVEL_1 (1 << 0)
# define DP_TRAIN_VOLTAGE_SWING_LEVEL_2 (2 << 0)
# define DP_TRAIN_VOLTAGE_SWING_LEVEL_3 (3 << 0)

# define DP_TRAIN_PRE_EMPHASIS_MASK	    (3 << 3)
# define DP_TRAIN_PRE_EMPH_LEVEL_0		(0 << 3)
# define DP_TRAIN_PRE_EMPH_LEVEL_1		(1 << 3)
# define DP_TRAIN_PRE_EMPH_LEVEL_2		(2 << 3)
# define DP_TRAIN_PRE_EMPH_LEVEL_3		(3 << 3)

# define DP_TRAIN_PRE_EMPHASIS_SHIFT	    3
# define DP_TRAIN_MAX_PRE_EMPHASIS_REACHED  (1 << 5)

# define DP_TX_FFE_PRESET_VALUE_MASK        (0xf << 0) /* 2.0 128b/132b Link Layer */

#define DP_DOWNSPREAD_CTRL		    0x107
# define DP_SPREAD_AMP_0_5		    (1 << 4)
# define DP_FIXED_VTOTAL_AS_SDP_EN_IN_PR_ACTIVE  (1 << 6)
# define DP_MSA_TIMING_PAR_IGNORE_EN	    (1 << 7) /* eDP */

#define DP_MAIN_LINK_CHANNEL_CODING_SET	    0x108
# define DP_SET_ANSI_8B10B		    (1 << 0)
# define DP_SET_ANSI_128B132B               (1 << 1)

#define DP_I2C_SPEED_CONTROL_STATUS	    0x109   /* DPI */
/* bitmask as for DP_I2C_SPEED_CAP */

#define DP_EDP_CONFIGURATION_SET            0x10a   /* XXX 1.2? */
# define DP_ALTERNATE_SCRAMBLER_RESET_ENABLE (1 << 0)
# define DP_FRAMING_CHANGE_ENABLE	    (1 << 1)
# define DP_PANEL_SELF_TEST_ENABLE	    (1 << 7)

#define DP_LINK_QUAL_LANE0_SET		    0x10b   /* DPCD >= 1.2 */
#define DP_LINK_QUAL_LANE1_SET		    0x10c
#define DP_LINK_QUAL_LANE2_SET		    0x10d
#define DP_LINK_QUAL_LANE3_SET		    0x10e
# define DP_LINK_QUAL_PATTERN_DISABLE	    0
# define DP_LINK_QUAL_PATTERN_D10_2	    1
# define DP_LINK_QUAL_PATTERN_ERROR_RATE    2
# define DP_LINK_QUAL_PATTERN_PRBS7	    3
# define DP_LINK_QUAL_PATTERN_80BIT_CUSTOM  4
# define DP_LINK_QUAL_PATTERN_CP2520_PAT_1  5
# define DP_LINK_QUAL_PATTERN_CP2520_PAT_2  6
# define DP_LINK_QUAL_PATTERN_CP2520_PAT_3  7
/* DP 2.0 UHBR10, UHBR13.5, UHBR20 */
# define DP_LINK_QUAL_PATTERN_128B132B_TPS1 0x08
# define DP_LINK_QUAL_PATTERN_128B132B_TPS2 0x10
# define DP_LINK_QUAL_PATTERN_PRSBS9        0x18
# define DP_LINK_QUAL_PATTERN_PRSBS11       0x20
# define DP_LINK_QUAL_PATTERN_PRSBS15       0x28
# define DP_LINK_QUAL_PATTERN_PRSBS23       0x30
# define DP_LINK_QUAL_PATTERN_PRSBS31       0x38
# define DP_LINK_QUAL_PATTERN_CUSTOM        0x40
# define DP_LINK_QUAL_PATTERN_SQUARE        0x48
# define DP_LINK_QUAL_PATTERN_SQUARE_PRESHOOT_DISABLED                   0x49
# define DP_LINK_QUAL_PATTERN_SQUARE_DEEMPHASIS_DISABLED                 0x4a
# define DP_LINK_QUAL_PATTERN_SQUARE_PRESHOOT_DEEMPHASIS_DISABLED        0x4b

#define DP_TRAINING_LANE0_1_SET2	    0x10f
#define DP_TRAINING_LANE2_3_SET2	    0x110
# define DP_LANE02_POST_CURSOR2_SET_MASK    (3 << 0)
# define DP_LANE02_MAX_POST_CURSOR2_REACHED (1 << 2)
# define DP_LANE13_POST_CURSOR2_SET_MASK    (3 << 4)
# define DP_LANE13_MAX_POST_CURSOR2_REACHED (1 << 6)

#define DP_MSTM_CTRL			    0x111   /* 1.2 */
# define DP_MST_EN			    (1 << 0)
# define DP_UP_REQ_EN			    (1 << 1)
# define DP_UPSTREAM_IS_SRC		    (1 << 2)

#define DP_AUDIO_DELAY0			    0x112   /* 1.2 */
#define DP_AUDIO_DELAY1			    0x113
#define DP_AUDIO_DELAY2			    0x114

#define DP_LINK_RATE_SET		    0x115   /* eDP 1.4 */
# define DP_LINK_RATE_SET_SHIFT		    0
# define DP_LINK_RATE_SET_MASK		    (7 << 0)

#define DP_RECEIVER_ALPM_CONFIG		    0x116   /* eDP 1.4 */
# define DP_ALPM_ENABLE			    (1 << 0)
# define DP_ALPM_LOCK_ERROR_IRQ_HPD_ENABLE  (1 << 1)

#define DP_SINK_DEVICE_AUX_FRAME_SYNC_CONF  0x117   /* eDP 1.4 */
# define DP_AUX_FRAME_SYNC_ENABLE	    (1 << 0)
# define DP_IRQ_HPD_ENABLE		    (1 << 1)

#define DP_UPSTREAM_DEVICE_DP_PWR_NEED	    0x118   /* 1.2 */
# define DP_PWR_NOT_NEEDED		    (1 << 0)

#define DP_FEC_CONFIGURATION		    0x120    /* 1.4 */
# define DP_FEC_READY			    (1 << 0)
# define DP_FEC_ERR_COUNT_SEL_MASK	    (7 << 1)
# define DP_FEC_ERR_COUNT_DIS		    (0 << 1)
# define DP_FEC_UNCORR_BLK_ERROR_COUNT	    (1 << 1)
# define DP_FEC_CORR_BLK_ERROR_COUNT	    (2 << 1)
# define DP_FEC_BIT_ERROR_COUNT		    (3 << 1)
# define DP_FEC_LANE_SELECT_MASK	    (3 << 4)
# define DP_FEC_LANE_0_SELECT		    (0 << 4)
# define DP_FEC_LANE_1_SELECT		    (1 << 4)
# define DP_FEC_LANE_2_SELECT		    (2 << 4)
# define DP_FEC_LANE_3_SELECT		    (3 << 4)

#define DP_SDP_ERROR_DETECTION_CONFIGURATION	0x121	/* DP 2.0 E11 */
#define DP_SDP_CRC16_128B132B_EN		BIT(0)

#define DP_AUX_FRAME_SYNC_VALUE		    0x15c   /* eDP 1.4 */
# define DP_AUX_FRAME_SYNC_VALID	    (1 << 0)

#define DP_DSC_ENABLE                       0x160   /* DP 1.4 */
# define DP_DECOMPRESSION_EN                (1 << 0)
# define DP_DSC_PASSTHROUGH_EN		    (1 << 1)
#define DP_DSC_CONFIGURATION				0x161	/* DP 2.0 */

#define DP_PSR_EN_CFG				0x170   /* XXX 1.2? */
# define DP_PSR_ENABLE				BIT(0)
# define DP_PSR_MAIN_LINK_ACTIVE		BIT(1)
# define DP_PSR_CRC_VERIFICATION		BIT(2)
# define DP_PSR_FRAME_CAPTURE			BIT(3)
# define DP_PSR_SU_REGION_SCANLINE_CAPTURE	BIT(4) /* eDP 1.4a */
# define DP_PSR_IRQ_HPD_WITH_CRC_ERRORS		BIT(5) /* eDP 1.4a */
# define DP_PSR_ENABLE_PSR2			BIT(6) /* eDP 1.4a */

#define DP_ADAPTER_CTRL			    0x1a0
# define DP_ADAPTER_CTRL_FORCE_LOAD_SENSE   (1 << 0)

#define DP_BRANCH_DEVICE_CTRL		    0x1a1
# define DP_BRANCH_DEVICE_IRQ_HPD	    (1 << 0)

#define PANEL_REPLAY_CONFIG                             0x1b0  /* DP 2.0 */
# define DP_PANEL_REPLAY_ENABLE                         (1 << 0)
# define DP_PANEL_REPLAY_UNRECOVERABLE_ERROR_EN         (1 << 3)
# define DP_PANEL_REPLAY_RFB_STORAGE_ERROR_EN           (1 << 4)
# define DP_PANEL_REPLAY_ACTIVE_FRAME_CRC_ERROR_EN      (1 << 5)
# define DP_PANEL_REPLAY_SU_ENABLE                      (1 << 6)

#define DP_PAYLOAD_ALLOCATE_SET		    0x1c0
#define DP_PAYLOAD_ALLOCATE_START_TIME_SLOT 0x1c1
#define DP_PAYLOAD_ALLOCATE_TIME_SLOT_COUNT 0x1c2

/* Link/Sink Device Status */
#define DP_SINK_COUNT			    0x200
/* prior to 1.2 bit 7 was reserved mbz */
# define DP_GET_SINK_COUNT(x)		    ((((x) & 0x80) >> 1) | ((x) & 0x3f))
# define DP_SINK_CP_READY		    (1 << 6)

#define DP_DEVICE_SERVICE_IRQ_VECTOR	    0x201
# define DP_REMOTE_CONTROL_COMMAND_PENDING  (1 << 0)
# define DP_AUTOMATED_TEST_REQUEST	    (1 << 1)
# define DP_CP_IRQ			    (1 << 2)
# define DP_MCCS_IRQ			    (1 << 3)
# define DP_DOWN_REP_MSG_RDY		    (1 << 4) /* 1.2 MST */
# define DP_UP_REQ_MSG_RDY		    (1 << 5) /* 1.2 MST */
# define DP_SINK_SPECIFIC_IRQ		    (1 << 6)

#define DP_LANE0_1_STATUS		    0x202
#define DP_LANE2_3_STATUS		    0x203
# define DP_LANE_CR_DONE		    (1 << 0)
# define DP_LANE_CHANNEL_EQ_DONE	    (1 << 1)
# define DP_LANE_SYMBOL_LOCKED		    (1 << 2)

#define DP_CHANNEL_EQ_BITS (DP_LANE_CR_DONE |		\
			    DP_LANE_CHANNEL_EQ_DONE |	\
			    DP_LANE_SYMBOL_LOCKED)

#define DP_LANE_ALIGN_STATUS_UPDATED                    0x204
#define  DP_INTERLANE_ALIGN_DONE                        (1 << 0)
#define  DP_128B132B_DPRX_EQ_INTERLANE_ALIGN_DONE       (1 << 2) /* 2.0 E11 */
#define  DP_128B132B_DPRX_CDS_INTERLANE_ALIGN_DONE      (1 << 3) /* 2.0 E11 */
#define  DP_128B132B_LT_FAILED                          (1 << 4) /* 2.0 E11 */
#define  DP_DOWNSTREAM_PORT_STATUS_CHANGED              (1 << 6)
#define  DP_LINK_STATUS_UPDATED                         (1 << 7)

#define DP_SINK_STATUS			    0x205
# define DP_RECEIVE_PORT_0_STATUS	    (1 << 0)
# define DP_RECEIVE_PORT_1_STATUS	    (1 << 1)
# define DP_STREAM_REGENERATION_STATUS      (1 << 2) /* 2.0 */
# define DP_INTRA_HOP_AUX_REPLY_INDICATION	(1 << 3) /* 2.0 */

#define DP_ADJUST_REQUEST_LANE0_1	    0x206
#define DP_ADJUST_REQUEST_LANE2_3	    0x207
# define DP_ADJUST_VOLTAGE_SWING_LANE0_MASK  0x03
# define DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT 0
# define DP_ADJUST_PRE_EMPHASIS_LANE0_MASK   0x0c
# define DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT  2
# define DP_ADJUST_VOLTAGE_SWING_LANE1_MASK  0x30
# define DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT 4
# define DP_ADJUST_PRE_EMPHASIS_LANE1_MASK   0xc0
# define DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT  6

/* DP 2.0 128b/132b Link Layer */
# define DP_ADJUST_TX_FFE_PRESET_LANE0_MASK  (0xf << 0)
# define DP_ADJUST_TX_FFE_PRESET_LANE0_SHIFT 0
# define DP_ADJUST_TX_FFE_PRESET_LANE1_MASK  (0xf << 4)
# define DP_ADJUST_TX_FFE_PRESET_LANE1_SHIFT 4

#define DP_ADJUST_REQUEST_POST_CURSOR2      0x20c
# define DP_ADJUST_POST_CURSOR2_LANE0_MASK  0x03
# define DP_ADJUST_POST_CURSOR2_LANE0_SHIFT 0
# define DP_ADJUST_POST_CURSOR2_LANE1_MASK  0x0c
# define DP_ADJUST_POST_CURSOR2_LANE1_SHIFT 2
# define DP_ADJUST_POST_CURSOR2_LANE2_MASK  0x30
# define DP_ADJUST_POST_CURSOR2_LANE2_SHIFT 4
# define DP_ADJUST_POST_CURSOR2_LANE3_MASK  0xc0
# define DP_ADJUST_POST_CURSOR2_LANE3_SHIFT 6

#define DP_TEST_REQUEST			    0x218
# define DP_TEST_LINK_TRAINING		    (1 << 0)
# define DP_TEST_LINK_VIDEO_PATTERN	    (1 << 1)
# define DP_TEST_LINK_EDID_READ		    (1 << 2)
# define DP_TEST_LINK_PHY_TEST_PATTERN	    (1 << 3) /* DPCD >= 1.1 */
# define DP_TEST_LINK_FAUX_PATTERN	    (1 << 4) /* DPCD >= 1.2 */
# define DP_TEST_LINK_AUDIO_PATTERN         (1 << 5) /* DPCD >= 1.2 */
# define DP_TEST_LINK_AUDIO_DISABLED_VIDEO  (1 << 6) /* DPCD >= 1.2 */

#define DP_TEST_LINK_RATE		    0x219
# define DP_LINK_RATE_162		    (0x6)
# define DP_LINK_RATE_27		    (0xa)

#define DP_TEST_LANE_COUNT		    0x220

#define DP_TEST_PATTERN			    0x221
# define DP_NO_TEST_PATTERN                 0x0
# define DP_COLOR_RAMP                      0x1
# define DP_BLACK_AND_WHITE_VERTICAL_LINES  0x2
# define DP_COLOR_SQUARE                    0x3

#define DP_TEST_H_TOTAL_HI                  0x222
#define DP_TEST_H_TOTAL_LO                  0x223

#define DP_TEST_V_TOTAL_HI                  0x224
#define DP_TEST_V_TOTAL_LO                  0x225

#define DP_TEST_H_START_HI                  0x226
#define DP_TEST_H_START_LO                  0x227

#define DP_TEST_V_START_HI                  0x228
#define DP_TEST_V_START_LO                  0x229

#define DP_TEST_HSYNC_HI                    0x22A
# define DP_TEST_HSYNC_POLARITY             (1 << 7)
# define DP_TEST_HSYNC_WIDTH_HI_MASK        (127 << 0)
#define DP_TEST_HSYNC_WIDTH_LO              0x22B

#define DP_TEST_VSYNC_HI                    0x22C
# define DP_TEST_VSYNC_POLARITY             (1 << 7)
# define DP_TEST_VSYNC_WIDTH_HI_MASK        (127 << 0)
#define DP_TEST_VSYNC_WIDTH_LO              0x22D

#define DP_TEST_H_WIDTH_HI                  0x22E
#define DP_TEST_H_WIDTH_LO                  0x22F

#define DP_TEST_V_HEIGHT_HI                 0x230
#define DP_TEST_V_HEIGHT_LO                 0x231

#define DP_TEST_MISC0                       0x232
# define DP_TEST_SYNC_CLOCK                 (1 << 0)
# define DP_TEST_COLOR_FORMAT_MASK          (3 << 1)
# define DP_TEST_COLOR_FORMAT_SHIFT         1
# define DP_COLOR_FORMAT_RGB                (0 << 1)
# define DP_COLOR_FORMAT_YCbCr422           (1 << 1)
# define DP_COLOR_FORMAT_YCbCr444           (2 << 1)
# define DP_TEST_DYNAMIC_RANGE_VESA         (0 << 3)
# define DP_TEST_DYNAMIC_RANGE_CEA          (1 << 3)
# define DP_TEST_YCBCR_COEFFICIENTS         (1 << 4)
# define DP_YCBCR_COEFFICIENTS_ITU601       (0 << 4)
# define DP_YCBCR_COEFFICIENTS_ITU709       (1 << 4)
# define DP_TEST_BIT_DEPTH_MASK             (7 << 5)
# define DP_TEST_BIT_DEPTH_SHIFT            5
# define DP_TEST_BIT_DEPTH_6                (0 << 5)
# define DP_TEST_BIT_DEPTH_8                (1 << 5)
# define DP_TEST_BIT_DEPTH_10               (2 << 5)
# define DP_TEST_BIT_DEPTH_12               (3 << 5)
# define DP_TEST_BIT_DEPTH_16               (4 << 5)

#define DP_TEST_MISC1                       0x233
# define DP_TEST_REFRESH_DENOMINATOR        (1 << 0)
# define DP_TEST_INTERLACED                 (1 << 1)

#define DP_TEST_REFRESH_RATE_NUMERATOR      0x234


#define DP_TEST_CRC_R_CR		    0x240
#define DP_TEST_CRC_G_Y			    0x242
#define DP_TEST_CRC_B_CB		    0x244

#define DP_TEST_SINK_MISC		    0x246
# define DP_TEST_CRC_SUPPORTED		    (1 << 5)
# define DP_TEST_COUNT_MASK		    0xf

#define DP_PHY_TEST_PATTERN                 0x248
# define DP_PHY_TEST_PATTERN_SEL_MASK       0x7
# define DP_PHY_TEST_PATTERN_NONE           0x0
# define DP_PHY_TEST_PATTERN_D10_2          0x1
# define DP_PHY_TEST_PATTERN_ERROR_COUNT    0x2
# define DP_PHY_TEST_PATTERN_PRBS7          0x3
# define DP_PHY_TEST_PATTERN_80BIT_CUSTOM   0x4
# define DP_PHY_TEST_PATTERN_CP2520         0x5

#define DP_PHY_SQUARE_PATTERN				0x249

#define DP_TEST_HBR2_SCRAMBLER_RESET        0x24A
#define DP_TEST_80BIT_CUSTOM_PATTERN_7_0    0x250
#define	DP_TEST_80BIT_CUSTOM_PATTERN_15_8   0x251
#define	DP_TEST_80BIT_CUSTOM_PATTERN_23_16  0x252
#define	DP_TEST_80BIT_CUSTOM_PATTERN_31_24  0x253
#define	DP_TEST_80BIT_CUSTOM_PATTERN_39_32  0x254
#define	DP_TEST_80BIT_CUSTOM_PATTERN_47_40  0x255
#define	DP_TEST_80BIT_CUSTOM_PATTERN_55_48  0x256
#define	DP_TEST_80BIT_CUSTOM_PATTERN_63_56  0x257
#define	DP_TEST_80BIT_CUSTOM_PATTERN_71_64  0x258
#define	DP_TEST_80BIT_CUSTOM_PATTERN_79_72  0x259

#define DP_TEST_RESPONSE		    0x260
# define DP_TEST_ACK			    (1 << 0)
# define DP_TEST_NAK			    (1 << 1)
# define DP_TEST_EDID_CHECKSUM_WRITE	    (1 << 2)

#define DP_TEST_EDID_CHECKSUM		    0x261

#define DP_TEST_SINK			    0x270
# define DP_TEST_SINK_START		    (1 << 0)
#define DP_TEST_AUDIO_MODE		    0x271
#define DP_TEST_AUDIO_PATTERN_TYPE	    0x272
#define DP_TEST_AUDIO_PERIOD_CH1	    0x273
#define DP_TEST_AUDIO_PERIOD_CH2	    0x274
#define DP_TEST_AUDIO_PERIOD_CH3	    0x275
#define DP_TEST_AUDIO_PERIOD_CH4	    0x276
#define DP_TEST_AUDIO_PERIOD_CH5	    0x277
#define DP_TEST_AUDIO_PERIOD_CH6	    0x278
#define DP_TEST_AUDIO_PERIOD_CH7	    0x279
#define DP_TEST_AUDIO_PERIOD_CH8	    0x27A

#define DP_FEC_STATUS			    0x280    /* 1.4 */
# define DP_FEC_DECODE_EN_DETECTED	    (1 << 0)
# define DP_FEC_DECODE_DIS_DETECTED	    (1 << 1)

#define DP_FEC_ERROR_COUNT_LSB		    0x0281    /* 1.4 */

#define DP_FEC_ERROR_COUNT_MSB		    0x0282    /* 1.4 */
# define DP_FEC_ERROR_COUNT_MASK	    0x7F
# define DP_FEC_ERR_COUNT_VALID		    (1 << 7)

#define DP_PAYLOAD_TABLE_UPDATE_STATUS      0x2c0   /* 1.2 MST */
# define DP_PAYLOAD_TABLE_UPDATED           (1 << 0)
# define DP_PAYLOAD_ACT_HANDLED             (1 << 1)

#define DP_VC_PAYLOAD_ID_SLOT_1             0x2c1   /* 1.2 MST */
/* up to ID_SLOT_63 at 0x2ff */

/* Source Device-specific */
#define DP_SOURCE_OUI			    0x300

/* Sink Device-specific */
#define DP_SINK_OUI			    0x400

/* Branch Device-specific */
#define DP_BRANCH_OUI			    0x500
#define DP_BRANCH_ID                        0x503
#define DP_BRANCH_REVISION_START            0x509
#define DP_BRANCH_HW_REV                    0x509
#define DP_BRANCH_SW_REV                    0x50A

/* Link/Sink Device Power Control */
#define DP_SET_POWER                        0x600
# define DP_SET_POWER_D0                    0x1
# define DP_SET_POWER_D3                    0x2
# define DP_SET_POWER_MASK                  0x3
# define DP_SET_POWER_D3_AUX_ON             0x5

/* eDP-specific */
#define DP_EDP_DPCD_REV			    0x700    /* eDP 1.2 */
# define DP_EDP_11			    0x00
# define DP_EDP_12			    0x01
# define DP_EDP_13			    0x02
# define DP_EDP_14			    0x03
# define DP_EDP_14a                         0x04    /* eDP 1.4a */
# define DP_EDP_14b                         0x05    /* eDP 1.4b */

#define DP_EDP_GENERAL_CAP_1		    0x701
# define DP_EDP_TCON_BACKLIGHT_ADJUSTMENT_CAP		(1 << 0)
# define DP_EDP_BACKLIGHT_PIN_ENABLE_CAP		(1 << 1)
# define DP_EDP_BACKLIGHT_AUX_ENABLE_CAP		(1 << 2)
# define DP_EDP_PANEL_SELF_TEST_PIN_ENABLE_CAP		(1 << 3)
# define DP_EDP_PANEL_SELF_TEST_AUX_ENABLE_CAP		(1 << 4)
# define DP_EDP_FRC_ENABLE_CAP				(1 << 5)
# define DP_EDP_COLOR_ENGINE_CAP			(1 << 6)
# define DP_EDP_SET_POWER_CAP				(1 << 7)

#define DP_EDP_BACKLIGHT_ADJUSTMENT_CAP     0x702
# define DP_EDP_BACKLIGHT_BRIGHTNESS_PWM_PIN_CAP	(1 << 0)
# define DP_EDP_BACKLIGHT_BRIGHTNESS_AUX_SET_CAP	(1 << 1)
# define DP_EDP_BACKLIGHT_BRIGHTNESS_BYTE_COUNT		(1 << 2)
# define DP_EDP_BACKLIGHT_AUX_PWM_PRODUCT_CAP		(1 << 3)
# define DP_EDP_BACKLIGHT_FREQ_PWM_PIN_PASSTHRU_CAP	(1 << 4)
# define DP_EDP_BACKLIGHT_FREQ_AUX_SET_CAP		(1 << 5)
# define DP_EDP_DYNAMIC_BACKLIGHT_CAP			(1 << 6)
# define DP_EDP_VBLANK_BACKLIGHT_UPDATE_CAP		(1 << 7)

#define DP_EDP_GENERAL_CAP_2		    0x703
# define DP_EDP_OVERDRIVE_ENGINE_ENABLED		(1 << 0)
# define DP_EDP_PANEL_LUMINANCE_CONTROL_CAPABLE		(1 << 4)

#define DP_EDP_GENERAL_CAP_3		    0x704    /* eDP 1.4 */
# define DP_EDP_X_REGION_CAP_MASK			(0xf << 0)
# define DP_EDP_X_REGION_CAP_SHIFT			0
# define DP_EDP_Y_REGION_CAP_MASK			(0xf << 4)
# define DP_EDP_Y_REGION_CAP_SHIFT			4

#define DP_EDP_DISPLAY_CONTROL_REGISTER     0x720
# define DP_EDP_BACKLIGHT_ENABLE			(1 << 0)
# define DP_EDP_BLACK_VIDEO_ENABLE			(1 << 1)
# define DP_EDP_FRC_ENABLE				(1 << 2)
# define DP_EDP_COLOR_ENGINE_ENABLE			(1 << 3)
# define DP_EDP_VBLANK_BACKLIGHT_UPDATE_ENABLE		(1 << 7)

#define DP_EDP_BACKLIGHT_MODE_SET_REGISTER  0x721
# define DP_EDP_BACKLIGHT_CONTROL_MODE_MASK		(3 << 0)
# define DP_EDP_BACKLIGHT_CONTROL_MODE_PWM		(0 << 0)
# define DP_EDP_BACKLIGHT_CONTROL_MODE_PRESET		(1 << 0)
# define DP_EDP_BACKLIGHT_CONTROL_MODE_DPCD		(2 << 0)
# define DP_EDP_BACKLIGHT_CONTROL_MODE_PRODUCT		(3 << 0)
# define DP_EDP_BACKLIGHT_FREQ_PWM_PIN_PASSTHRU_ENABLE	(1 << 2)
# define DP_EDP_BACKLIGHT_FREQ_AUX_SET_ENABLE		(1 << 3)
# define DP_EDP_DYNAMIC_BACKLIGHT_ENABLE		(1 << 4)
# define DP_EDP_REGIONAL_BACKLIGHT_ENABLE		(1 << 5)
# define DP_EDP_UPDATE_REGION_BRIGHTNESS		(1 << 6) /* eDP 1.4 */
# define DP_EDP_PANEL_LUMINANCE_CONTROL_ENABLE		(1 << 7)

#define DP_EDP_BACKLIGHT_BRIGHTNESS_MSB     0x722
#define DP_EDP_BACKLIGHT_BRIGHTNESS_LSB     0x723

#define DP_EDP_PWMGEN_BIT_COUNT             0x724
#define DP_EDP_PWMGEN_BIT_COUNT_CAP_MIN     0x725
#define DP_EDP_PWMGEN_BIT_COUNT_CAP_MAX     0x726
# define DP_EDP_PWMGEN_BIT_COUNT_MASK       (0x1f << 0)

#define DP_EDP_BACKLIGHT_CONTROL_STATUS     0x727

#define DP_EDP_BACKLIGHT_FREQ_SET           0x728
# define DP_EDP_BACKLIGHT_FREQ_BASE_KHZ     27000

#define DP_EDP_BACKLIGHT_FREQ_CAP_MIN_MSB   0x72a
#define DP_EDP_BACKLIGHT_FREQ_CAP_MIN_MID   0x72b
#define DP_EDP_BACKLIGHT_FREQ_CAP_MIN_LSB   0x72c

#define DP_EDP_BACKLIGHT_FREQ_CAP_MAX_MSB   0x72d
#define DP_EDP_BACKLIGHT_FREQ_CAP_MAX_MID   0x72e
#define DP_EDP_BACKLIGHT_FREQ_CAP_MAX_LSB   0x72f

#define DP_EDP_DBC_MINIMUM_BRIGHTNESS_SET   0x732
#define DP_EDP_DBC_MAXIMUM_BRIGHTNESS_SET   0x733
#define DP_EDP_PANEL_TARGET_LUMINANCE_VALUE 0x734

#define DP_EDP_REGIONAL_BACKLIGHT_BASE      0x740    /* eDP 1.4 */
#define DP_EDP_REGIONAL_BACKLIGHT_0	    0x741    /* eDP 1.4 */

#define DP_EDP_MSO_LINK_CAPABILITIES        0x7a4    /* eDP 1.4 */
# define DP_EDP_MSO_NUMBER_OF_LINKS_MASK    (7 << 0)
# define DP_EDP_MSO_NUMBER_OF_LINKS_SHIFT   0
# define DP_EDP_MSO_INDEPENDENT_LINK_BIT    (1 << 3)

/* Sideband MSG Buffers */
#define DP_SIDEBAND_MSG_DOWN_REQ_BASE	    0x1000   /* 1.2 MST */
#define DP_SIDEBAND_MSG_UP_REP_BASE	    0x1200   /* 1.2 MST */
#define DP_SIDEBAND_MSG_DOWN_REP_BASE	    0x1400   /* 1.2 MST */
#define DP_SIDEBAND_MSG_UP_REQ_BASE	    0x1600   /* 1.2 MST */

/* DPRX Event Status Indicator */
#define DP_SINK_COUNT_ESI                   0x2002   /* same as 0x200 */
#define DP_DEVICE_SERVICE_IRQ_VECTOR_ESI0   0x2003   /* same as 0x201 */

#define DP_DEVICE_SERVICE_IRQ_VECTOR_ESI1   0x2004   /* 1.2 */
# define DP_RX_GTC_MSTR_REQ_STATUS_CHANGE    (1 << 0)
# define DP_LOCK_ACQUISITION_REQUEST         (1 << 1)
# define DP_CEC_IRQ                          (1 << 2)

#define DP_LINK_SERVICE_IRQ_VECTOR_ESI0     0x2005   /* 1.2 */
# define RX_CAP_CHANGED                      (1 << 0)
# define LINK_STATUS_CHANGED                 (1 << 1)
# define STREAM_STATUS_CHANGED               (1 << 2)
# define HDMI_LINK_STATUS_CHANGED            (1 << 3)
# define CONNECTED_OFF_ENTRY_REQUESTED       (1 << 4)

#define DP_PSR_ERROR_STATUS                 0x2006  /* XXX 1.2? */
# define DP_PSR_LINK_CRC_ERROR              (1 << 0)
# define DP_PSR_RFB_STORAGE_ERROR           (1 << 1)
# define DP_PSR_VSC_SDP_UNCORRECTABLE_ERROR (1 << 2) /* eDP 1.4 */

#define DP_PSR_ESI                          0x2007  /* XXX 1.2? */
# define DP_PSR_CAPS_CHANGE                 (1 << 0)

#define DP_PSR_STATUS                       0x2008  /* XXX 1.2? */
# define DP_PSR_SINK_INACTIVE               0
# define DP_PSR_SINK_ACTIVE_SRC_SYNCED      1
# define DP_PSR_SINK_ACTIVE_RFB             2
# define DP_PSR_SINK_ACTIVE_SINK_SYNCED     3
# define DP_PSR_SINK_ACTIVE_RESYNC          4
# define DP_PSR_SINK_INTERNAL_ERROR         7
# define DP_PSR_SINK_STATE_MASK             0x07

#define DP_SYNCHRONIZATION_LATENCY_IN_SINK		0x2009 /* edp 1.4 */
# define DP_MAX_RESYNC_FRAME_COUNT_MASK			(0xf << 0)
# define DP_MAX_RESYNC_FRAME_COUNT_SHIFT		0
# define DP_LAST_ACTUAL_SYNCHRONIZATION_LATENCY_MASK	(0xf << 4)
# define DP_LAST_ACTUAL_SYNCHRONIZATION_LATENCY_SHIFT	4

#define DP_LAST_RECEIVED_PSR_SDP	    0x200a /* eDP 1.2 */
# define DP_PSR_STATE_BIT		    (1 << 0) /* eDP 1.2 */
# define DP_UPDATE_RFB_BIT		    (1 << 1) /* eDP 1.2 */
# define DP_CRC_VALID_BIT		    (1 << 2) /* eDP 1.2 */
# define DP_SU_VALID			    (1 << 3) /* eDP 1.4 */
# define DP_FIRST_SCAN_LINE_SU_REGION	    (1 << 4) /* eDP 1.4 */
# define DP_LAST_SCAN_LINE_SU_REGION	    (1 << 5) /* eDP 1.4 */
# define DP_Y_COORDINATE_VALID		    (1 << 6) /* eDP 1.4a */

#define DP_RECEIVER_ALPM_STATUS		    0x200b  /* eDP 1.4 */
# define DP_ALPM_LOCK_TIMEOUT_ERROR	    (1 << 0)

#define DP_LANE0_1_STATUS_ESI                  0x200c /* status same as 0x202 */
#define DP_LANE2_3_STATUS_ESI                  0x200d /* status same as 0x203 */
#define DP_LANE_ALIGN_STATUS_UPDATED_ESI       0x200e /* status same as 0x204 */
#define DP_SINK_STATUS_ESI                     0x200f /* status same as 0x205 */

#define DP_PANEL_REPLAY_ERROR_STATUS                   0x2020  /* DP 2.1*/
# define DP_PANEL_REPLAY_LINK_CRC_ERROR                (1 << 0)
# define DP_PANEL_REPLAY_RFB_STORAGE_ERROR             (1 << 1)
# define DP_PANEL_REPLAY_VSC_SDP_UNCORRECTABLE_ERROR   (1 << 2)

#define DP_SINK_DEVICE_PR_AND_FRAME_LOCK_STATUS        0x2022  /* DP 2.1 */
# define DP_SINK_DEVICE_PANEL_REPLAY_STATUS_MASK       (7 << 0)
# define DP_SINK_FRAME_LOCKED_SHIFT                    3
# define DP_SINK_FRAME_LOCKED_MASK                     (3 << 3)
# define DP_SINK_FRAME_LOCKED_STATUS_VALID_SHIFT       5
# define DP_SINK_FRAME_LOCKED_STATUS_VALID_MASK        (1 << 5)

/* Extended Receiver Capability: See DP_DPCD_REV for definitions */
#define DP_DP13_DPCD_REV                    0x2200

#define DP_DPRX_FEATURE_ENUMERATION_LIST    0x2210  /* DP 1.3 */
# define DP_GTC_CAP					(1 << 0)  /* DP 1.3 */
# define DP_SST_SPLIT_SDP_CAP				(1 << 1)  /* DP 1.4 */
# define DP_AV_SYNC_CAP					(1 << 2)  /* DP 1.3 */
# define DP_VSC_SDP_EXT_FOR_COLORIMETRY_SUPPORTED	(1 << 3)  /* DP 1.3 */
# define DP_VSC_EXT_VESA_SDP_SUPPORTED			(1 << 4)  /* DP 1.4 */
# define DP_VSC_EXT_VESA_SDP_CHAINING_SUPPORTED		(1 << 5)  /* DP 1.4 */
# define DP_VSC_EXT_CEA_SDP_SUPPORTED			(1 << 6)  /* DP 1.4 */
# define DP_VSC_EXT_CEA_SDP_CHAINING_SUPPORTED		(1 << 7)  /* DP 1.4 */

#define DP_DPRX_FEATURE_ENUMERATION_LIST_CONT_1         0x2214 /* 2.0 E11 */
# define DP_ADAPTIVE_SYNC_SDP_SUPPORTED    (1 << 0)
# define DP_AS_SDP_FIRST_HALF_LINE_OR_3840_PIXEL_CYCLE_WINDOW_NOT_SUPPORTED (1 << 1)
# define DP_VSC_EXT_SDP_FRAMEWORK_VERSION_1_SUPPORTED  (1 << 4)

#define DP_128B132B_SUPPORTED_LINK_RATES       0x2215 /* 2.0 */
# define DP_UHBR10                             (1 << 0)
# define DP_UHBR20                             (1 << 1)
# define DP_UHBR13_5                           (1 << 2)

#define DP_128B132B_TRAINING_AUX_RD_INTERVAL                    0x2216 /* 2.0 */
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_1MS_UNIT          (1 << 7)
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_MASK              0x7f
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_400_US            0x00
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_4_MS              0x01
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_8_MS              0x02
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_12_MS             0x03
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_16_MS             0x04
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_32_MS             0x05
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_64_MS             0x06

#define DP_TEST_264BIT_CUSTOM_PATTERN_7_0		0x2230
#define DP_TEST_264BIT_CUSTOM_PATTERN_263_256	0x2250

/* DSC Extended Capability Branch Total DSC Resources */
#define DP_DSC_SUPPORT_AND_DSC_DECODER_COUNT		0x2260	/* 2.0 */
# define DP_DSC_DECODER_COUNT_MASK			(0b111 << 5)
# define DP_DSC_DECODER_COUNT_SHIFT			5
#define DP_DSC_MAX_SLICE_COUNT_AND_AGGREGATION_0	0x2270	/* 2.0 */
# define DP_DSC_DECODER_0_MAXIMUM_SLICE_COUNT_MASK	(1 << 0)
# define DP_DSC_DECODER_0_AGGREGATION_SUPPORT_MASK	(0b111 << 1)
# define DP_DSC_DECODER_0_AGGREGATION_SUPPORT_SHIFT	1

/* Protocol Converter Extension */
/* HDMI CEC tunneling over AUX DP 1.3 section 5.3.3.3.1 DPCD 1.4+ */
#define DP_CEC_TUNNELING_CAPABILITY            0x3000
# define DP_CEC_TUNNELING_CAPABLE               (1 << 0)
# define DP_CEC_SNOOPING_CAPABLE                (1 << 1)
# define DP_CEC_MULTIPLE_LA_CAPABLE             (1 << 2)

#define DP_CEC_TUNNELING_CONTROL               0x3001
# define DP_CEC_TUNNELING_ENABLE                (1 << 0)
# define DP_CEC_SNOOPING_ENABLE                 (1 << 1)

#define DP_CEC_RX_MESSAGE_INFO                 0x3002
# define DP_CEC_RX_MESSAGE_LEN_MASK             (0xf << 0)
# define DP_CEC_RX_MESSAGE_LEN_SHIFT            0
# define DP_CEC_RX_MESSAGE_HPD_STATE            (1 << 4)
# define DP_CEC_RX_MESSAGE_HPD_LOST             (1 << 5)
# define DP_CEC_RX_MESSAGE_ACKED                (1 << 6)
# define DP_CEC_RX_MESSAGE_ENDED                (1 << 7)

#define DP_CEC_TX_MESSAGE_INFO                 0x3003
# define DP_CEC_TX_MESSAGE_LEN_MASK             (0xf << 0)
# define DP_CEC_TX_MESSAGE_LEN_SHIFT            0
# define DP_CEC_TX_RETRY_COUNT_MASK             (0x7 << 4)
# define DP_CEC_TX_RETRY_COUNT_SHIFT            4
# define DP_CEC_TX_MESSAGE_SEND                 (1 << 7)

#define DP_CEC_TUNNELING_IRQ_FLAGS             0x3004
# define DP_CEC_RX_MESSAGE_INFO_VALID           (1 << 0)
# define DP_CEC_RX_MESSAGE_OVERFLOW             (1 << 1)
# define DP_CEC_TX_MESSAGE_SENT                 (1 << 4)
# define DP_CEC_TX_LINE_ERROR                   (1 << 5)
# define DP_CEC_TX_ADDRESS_NACK_ERROR           (1 << 6)
# define DP_CEC_TX_DATA_NACK_ERROR              (1 << 7)

#define DP_CEC_LOGICAL_ADDRESS_MASK            0x300E /* 0x300F word */
# define DP_CEC_LOGICAL_ADDRESS_0               (1 << 0)
# define DP_CEC_LOGICAL_ADDRESS_1               (1 << 1)
# define DP_CEC_LOGICAL_ADDRESS_2               (1 << 2)
# define DP_CEC_LOGICAL_ADDRESS_3               (1 << 3)
# define DP_CEC_LOGICAL_ADDRESS_4               (1 << 4)
# define DP_CEC_LOGICAL_ADDRESS_5               (1 << 5)
# define DP_CEC_LOGICAL_ADDRESS_6               (1 << 6)
# define DP_CEC_LOGICAL_ADDRESS_7               (1 << 7)
#define DP_CEC_LOGICAL_ADDRESS_MASK_2          0x300F /* 0x300E word */
# define DP_CEC_LOGICAL_ADDRESS_8               (1 << 0)
# define DP_CEC_LOGICAL_ADDRESS_9               (1 << 1)
# define DP_CEC_LOGICAL_ADDRESS_10              (1 << 2)
# define DP_CEC_LOGICAL_ADDRESS_11              (1 << 3)
# define DP_CEC_LOGICAL_ADDRESS_12              (1 << 4)
# define DP_CEC_LOGICAL_ADDRESS_13              (1 << 5)
# define DP_CEC_LOGICAL_ADDRESS_14              (1 << 6)
# define DP_CEC_LOGICAL_ADDRESS_15              (1 << 7)

#define DP_CEC_RX_MESSAGE_BUFFER               0x3010
#define DP_CEC_TX_MESSAGE_BUFFER               0x3020
#define DP_CEC_MESSAGE_BUFFER_LENGTH             0x10

/* PCON CONFIGURE-1 FRL FOR HDMI SINK */
#define DP_PCON_HDMI_LINK_CONFIG_1             0x305A
# define DP_PCON_ENABLE_MAX_FRL_BW             (7 << 0)
# define DP_PCON_ENABLE_MAX_BW_0GBPS	       0
# define DP_PCON_ENABLE_MAX_BW_9GBPS	       1
# define DP_PCON_ENABLE_MAX_BW_18GBPS	       2
# define DP_PCON_ENABLE_MAX_BW_24GBPS	       3
# define DP_PCON_ENABLE_MAX_BW_32GBPS	       4
# define DP_PCON_ENABLE_MAX_BW_40GBPS	       5
# define DP_PCON_ENABLE_MAX_BW_48GBPS	       6
# define DP_PCON_ENABLE_SOURCE_CTL_MODE       (1 << 3)
# define DP_PCON_ENABLE_CONCURRENT_LINK       (1 << 4)
# define DP_PCON_ENABLE_SEQUENTIAL_LINK       (0 << 4)
# define DP_PCON_ENABLE_LINK_FRL_MODE         (1 << 5)
# define DP_PCON_ENABLE_HPD_READY	      (1 << 6)
# define DP_PCON_ENABLE_HDMI_LINK             (1 << 7)

/* PCON CONFIGURE-2 FRL FOR HDMI SINK */
#define DP_PCON_HDMI_LINK_CONFIG_2            0x305B
# define DP_PCON_MAX_LINK_BW_MASK             (0x3F << 0)
# define DP_PCON_FRL_BW_MASK_9GBPS            (1 << 0)
# define DP_PCON_FRL_BW_MASK_18GBPS           (1 << 1)
# define DP_PCON_FRL_BW_MASK_24GBPS           (1 << 2)
# define DP_PCON_FRL_BW_MASK_32GBPS           (1 << 3)
# define DP_PCON_FRL_BW_MASK_40GBPS           (1 << 4)
# define DP_PCON_FRL_BW_MASK_48GBPS           (1 << 5)
# define DP_PCON_FRL_LINK_TRAIN_EXTENDED      (1 << 6)
# define DP_PCON_FRL_LINK_TRAIN_NORMAL        (0 << 6)

/* PCON HDMI LINK STATUS */
#define DP_PCON_HDMI_TX_LINK_STATUS           0x303B
# define DP_PCON_HDMI_TX_LINK_ACTIVE          (1 << 0)
# define DP_PCON_FRL_READY		      (1 << 1)

/* PCON HDMI POST FRL STATUS */
#define DP_PCON_HDMI_POST_FRL_STATUS          0x3036
# define DP_PCON_HDMI_LINK_MODE               (1 << 0)
# define DP_PCON_HDMI_MODE_TMDS               0
# define DP_PCON_HDMI_MODE_FRL                1
# define DP_PCON_HDMI_FRL_TRAINED_BW          (0x3F << 1)
# define DP_PCON_FRL_TRAINED_BW_9GBPS	      (1 << 1)
# define DP_PCON_FRL_TRAINED_BW_18GBPS	      (1 << 2)
# define DP_PCON_FRL_TRAINED_BW_24GBPS	      (1 << 3)
# define DP_PCON_FRL_TRAINED_BW_32GBPS	      (1 << 4)
# define DP_PCON_FRL_TRAINED_BW_40GBPS	      (1 << 5)
# define DP_PCON_FRL_TRAINED_BW_48GBPS	      (1 << 6)

#define DP_PROTOCOL_CONVERTER_CONTROL_0		0x3050 /* DP 1.3 */
# define DP_HDMI_DVI_OUTPUT_CONFIG		(1 << 0) /* DP 1.3 */
#define DP_PROTOCOL_CONVERTER_CONTROL_1		0x3051 /* DP 1.3 */
# define DP_CONVERSION_TO_YCBCR420_ENABLE	(1 << 0) /* DP 1.3 */
# define DP_HDMI_EDID_PROCESSING_DISABLE	(1 << 1) /* DP 1.4 */
# define DP_HDMI_AUTONOMOUS_SCRAMBLING_DISABLE	(1 << 2) /* DP 1.4 */
# define DP_HDMI_FORCE_SCRAMBLING		(1 << 3) /* DP 1.4 */
#define DP_PROTOCOL_CONVERTER_CONTROL_2		0x3052 /* DP 1.3 */
# define DP_CONVERSION_TO_YCBCR422_ENABLE	(1 << 0) /* DP 1.3 */
# define DP_PCON_ENABLE_DSC_ENCODER	        (1 << 1)
# define DP_PCON_ENCODER_PPS_OVERRIDE_MASK	(0x3 << 2)
# define DP_PCON_ENC_PPS_OVERRIDE_DISABLED      0
# define DP_PCON_ENC_PPS_OVERRIDE_EN_PARAMS     1
# define DP_PCON_ENC_PPS_OVERRIDE_EN_BUFFER     2
# define DP_CONVERSION_RGB_YCBCR_MASK	       (7 << 4)
# define DP_CONVERSION_BT601_RGB_YCBCR_ENABLE  (1 << 4)
# define DP_CONVERSION_BT709_RGB_YCBCR_ENABLE  (1 << 5)
# define DP_CONVERSION_BT2020_RGB_YCBCR_ENABLE (1 << 6)

/* PCON Downstream HDMI ERROR Status per Lane */
#define DP_PCON_HDMI_ERROR_STATUS_LN0          0x3037
#define DP_PCON_HDMI_ERROR_STATUS_LN1          0x3038
#define DP_PCON_HDMI_ERROR_STATUS_LN2          0x3039
#define DP_PCON_HDMI_ERROR_STATUS_LN3          0x303A
# define DP_PCON_HDMI_ERROR_COUNT_MASK         (0x7 << 0)
# define DP_PCON_HDMI_ERROR_COUNT_THREE_PLUS   (1 << 0)
# define DP_PCON_HDMI_ERROR_COUNT_TEN_PLUS     (1 << 1)
# define DP_PCON_HDMI_ERROR_COUNT_HUNDRED_PLUS (1 << 2)

/* PCON HDMI CONFIG PPS Override Buffer
 * Valid Offsets to be added to Base : 0-127
 */
#define DP_PCON_HDMI_PPS_OVERRIDE_BASE        0x3100

/* PCON HDMI CONFIG PPS Override Parameter: Slice height
 * Offset-0 8LSBs of the Slice height.
 * Offset-1 8MSBs of the Slice height.
 */
#define DP_PCON_HDMI_PPS_OVRD_SLICE_HEIGHT    0x3180

/* PCON HDMI CONFIG PPS Override Parameter: Slice width
 * Offset-0 8LSBs of the Slice width.
 * Offset-1 8MSBs of the Slice width.
 */
#define DP_PCON_HDMI_PPS_OVRD_SLICE_WIDTH    0x3182

/* PCON HDMI CONFIG PPS Override Parameter: bits_per_pixel
 * Offset-0 8LSBs of the bits_per_pixel.
 * Offset-1 2MSBs of the bits_per_pixel.
 */
#define DP_PCON_HDMI_PPS_OVRD_BPP	     0x3184

/* HDCP 1.3 and HDCP 2.2 */
#define DP_AUX_HDCP_BKSV		0x68000
#define DP_AUX_HDCP_RI_PRIME		0x68005
#define DP_AUX_HDCP_AKSV		0x68007
#define DP_AUX_HDCP_AN			0x6800C
#define DP_AUX_HDCP_V_PRIME(h)		(0x68014 + h * 4)
#define DP_AUX_HDCP_BCAPS		0x68028
# define DP_BCAPS_REPEATER_PRESENT	BIT(1)
# define DP_BCAPS_HDCP_CAPABLE		BIT(0)
#define DP_AUX_HDCP_BSTATUS		0x68029
# define DP_BSTATUS_REAUTH_REQ		BIT(3)
# define DP_BSTATUS_LINK_FAILURE	BIT(2)
# define DP_BSTATUS_R0_PRIME_READY	BIT(1)
# define DP_BSTATUS_READY		BIT(0)
#define DP_AUX_HDCP_BINFO		0x6802A
#define DP_AUX_HDCP_KSV_FIFO		0x6802C
#define DP_AUX_HDCP_AINFO		0x6803B

/* DP HDCP2.2 parameter offsets in DPCD address space */
#define DP_HDCP_2_2_REG_RTX_OFFSET		0x69000
#define DP_HDCP_2_2_REG_TXCAPS_OFFSET		0x69008
#define DP_HDCP_2_2_REG_CERT_RX_OFFSET		0x6900B
#define DP_HDCP_2_2_REG_RRX_OFFSET		0x69215
#define DP_HDCP_2_2_REG_RX_CAPS_OFFSET		0x6921D
#define DP_HDCP_2_2_REG_EKPUB_KM_OFFSET		0x69220
#define DP_HDCP_2_2_REG_EKH_KM_WR_OFFSET	0x692A0
#define DP_HDCP_2_2_REG_M_OFFSET		0x692B0
#define DP_HDCP_2_2_REG_HPRIME_OFFSET		0x692C0
#define DP_HDCP_2_2_REG_EKH_KM_RD_OFFSET	0x692E0
#define DP_HDCP_2_2_REG_RN_OFFSET		0x692F0
#define DP_HDCP_2_2_REG_LPRIME_OFFSET		0x692F8
#define DP_HDCP_2_2_REG_EDKEY_KS_OFFSET		0x69318
#define	DP_HDCP_2_2_REG_RIV_OFFSET		0x69328
#define DP_HDCP_2_2_REG_RXINFO_OFFSET		0x69330
#define DP_HDCP_2_2_REG_SEQ_NUM_V_OFFSET	0x69332
#define DP_HDCP_2_2_REG_VPRIME_OFFSET		0x69335
#define DP_HDCP_2_2_REG_RECV_ID_LIST_OFFSET	0x69345
#define DP_HDCP_2_2_REG_V_OFFSET		0x693E0
#define DP_HDCP_2_2_REG_SEQ_NUM_M_OFFSET	0x693F0
#define DP_HDCP_2_2_REG_K_OFFSET		0x693F3
#define DP_HDCP_2_2_REG_STREAM_ID_TYPE_OFFSET	0x693F5
#define DP_HDCP_2_2_REG_MPRIME_OFFSET		0x69473
#define DP_HDCP_2_2_REG_RXSTATUS_OFFSET		0x69493
#define DP_HDCP_2_2_REG_STREAM_TYPE_OFFSET	0x69494
#define DP_HDCP_2_2_REG_DBG_OFFSET		0x69518

/* LTTPR: Link Training (LT)-tunable PHY Repeaters */
#define DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV 0xf0000 /* 1.3 */
#define DP_MAX_LINK_RATE_PHY_REPEATER			    0xf0001 /* 1.4a */
#define DP_PHY_REPEATER_CNT				    0xf0002 /* 1.3 */
#define DP_PHY_REPEATER_MODE				    0xf0003 /* 1.3 */
#define DP_MAX_LANE_COUNT_PHY_REPEATER			    0xf0004 /* 1.4a */
#define DP_Repeater_FEC_CAPABILITY			    0xf0004 /* 1.4 */
#define DP_PHY_REPEATER_EXTENDED_WAIT_TIMEOUT		    0xf0005 /* 1.4a */
#define DP_MAIN_LINK_CHANNEL_CODING_PHY_REPEATER	    0xf0006 /* 2.0 */
# define DP_PHY_REPEATER_128B132B_SUPPORTED		    (1 << 0)
/* See DP_128B132B_SUPPORTED_LINK_RATES for values */
#define DP_PHY_REPEATER_128B132B_RATES			    0xf0007 /* 2.0 */
#define DP_PHY_REPEATER_EQ_DONE                             0xf0008 /* 2.0 E11 */

enum dp_phy {
	DP_PHY_DPRX,

	DP_PHY_LTTPR1,
	DP_PHY_LTTPR2,
	DP_PHY_LTTPR3,
	DP_PHY_LTTPR4,
	DP_PHY_LTTPR5,
	DP_PHY_LTTPR6,
	DP_PHY_LTTPR7,
	DP_PHY_LTTPR8,

	DP_MAX_LTTPR_COUNT = DP_PHY_LTTPR8,
};

#define DP_PHY_LTTPR(i)					    (DP_PHY_LTTPR1 + (i))

#define __DP_LTTPR1_BASE				    0xf0010 /* 1.3 */
#define __DP_LTTPR2_BASE				    0xf0060 /* 1.3 */
#define DP_LTTPR_BASE(dp_phy) \
	(__DP_LTTPR1_BASE + (__DP_LTTPR2_BASE - __DP_LTTPR1_BASE) * \
		((dp_phy) - DP_PHY_LTTPR1))

#define DP_LTTPR_REG(dp_phy, lttpr1_reg) \
	(DP_LTTPR_BASE(dp_phy) - DP_LTTPR_BASE(DP_PHY_LTTPR1) + (lttpr1_reg))

#define DP_TRAINING_PATTERN_SET_PHY_REPEATER1		    0xf0010 /* 1.3 */
#define DP_TRAINING_PATTERN_SET_PHY_REPEATER(dp_phy) \
	DP_LTTPR_REG(dp_phy, DP_TRAINING_PATTERN_SET_PHY_REPEATER1)

#define DP_TRAINING_LANE0_SET_PHY_REPEATER1		    0xf0011 /* 1.3 */
#define DP_TRAINING_LANE0_SET_PHY_REPEATER(dp_phy) \
	DP_LTTPR_REG(dp_phy, DP_TRAINING_LANE0_SET_PHY_REPEATER1)

#define DP_TRAINING_LANE1_SET_PHY_REPEATER1		    0xf0012 /* 1.3 */
#define DP_TRAINING_LANE2_SET_PHY_REPEATER1		    0xf0013 /* 1.3 */
#define DP_TRAINING_LANE3_SET_PHY_REPEATER1		    0xf0014 /* 1.3 */
#define DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1	    0xf0020 /* 1.4a */
#define DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy)	\
	DP_LTTPR_REG(dp_phy, DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1)

#define DP_TRANSMITTER_CAPABILITY_PHY_REPEATER1		    0xf0021 /* 1.4a */
# define DP_VOLTAGE_SWING_LEVEL_3_SUPPORTED		    BIT(0)
# define DP_PRE_EMPHASIS_LEVEL_3_SUPPORTED		    BIT(1)

#define DP_128B132B_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1  0xf0022 /* 2.0 */
#define DP_128B132B_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy)	\
	DP_LTTPR_REG(dp_phy, DP_128B132B_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1)
/* see DP_128B132B_TRAINING_AUX_RD_INTERVAL for values */

#define DP_LANE0_1_STATUS_PHY_REPEATER1			    0xf0030 /* 1.3 */
#define DP_LANE0_1_STATUS_PHY_REPEATER(dp_phy) \
	DP_LTTPR_REG(dp_phy, DP_LANE0_1_STATUS_PHY_REPEATER1)

#define DP_LANE2_3_STATUS_PHY_REPEATER1			    0xf0031 /* 1.3 */

#define DP_LANE_ALIGN_STATUS_UPDATED_PHY_REPEATER1	    0xf0032 /* 1.3 */
#define DP_ADJUST_REQUEST_LANE0_1_PHY_REPEATER1		    0xf0033 /* 1.3 */
#define DP_ADJUST_REQUEST_LANE2_3_PHY_REPEATER1		    0xf0034 /* 1.3 */
#define DP_SYMBOL_ERROR_COUNT_LANE0_PHY_REPEATER1	    0xf0035 /* 1.3 */
#define DP_SYMBOL_ERROR_COUNT_LANE1_PHY_REPEATER1	    0xf0037 /* 1.3 */
#define DP_SYMBOL_ERROR_COUNT_LANE2_PHY_REPEATER1	    0xf0039 /* 1.3 */
#define DP_SYMBOL_ERROR_COUNT_LANE3_PHY_REPEATER1	    0xf003b /* 1.3 */

#define __DP_FEC1_BASE					    0xf0290 /* 1.4 */
#define __DP_FEC2_BASE					    0xf0298 /* 1.4 */
#define DP_FEC_BASE(dp_phy) \
	(__DP_FEC1_BASE + ((__DP_FEC2_BASE - __DP_FEC1_BASE) * \
			   ((dp_phy) - DP_PHY_LTTPR1)))

#define DP_FEC_REG(dp_phy, fec1_reg) \
	(DP_FEC_BASE(dp_phy) - DP_FEC_BASE(DP_PHY_LTTPR1) + fec1_reg)

#define DP_FEC_STATUS_PHY_REPEATER1			    0xf0290 /* 1.4 */
#define DP_FEC_STATUS_PHY_REPEATER(dp_phy) \
	DP_FEC_REG(dp_phy, DP_FEC_STATUS_PHY_REPEATER1)

#define DP_FEC_ERROR_COUNT_PHY_REPEATER1                    0xf0291 /* 1.4 */
#define DP_FEC_CAPABILITY_PHY_REPEATER1                     0xf0294 /* 1.4a */

#define DP_LTTPR_MAX_ADD				    0xf02ff /* 1.4 */

#define DP_DPCD_MAX_ADD					    0xfffff /* 1.4 */

/* Repeater modes */
#define DP_PHY_REPEATER_MODE_TRANSPARENT		    0x55    /* 1.3 */
#define DP_PHY_REPEATER_MODE_NON_TRANSPARENT		    0xaa    /* 1.3 */

/* DP HDCP message start offsets in DPCD address space */
#define DP_HDCP_2_2_AKE_INIT_OFFSET		DP_HDCP_2_2_REG_RTX_OFFSET
#define DP_HDCP_2_2_AKE_SEND_CERT_OFFSET	DP_HDCP_2_2_REG_CERT_RX_OFFSET
#define DP_HDCP_2_2_AKE_NO_STORED_KM_OFFSET	DP_HDCP_2_2_REG_EKPUB_KM_OFFSET
#define DP_HDCP_2_2_AKE_STORED_KM_OFFSET	DP_HDCP_2_2_REG_EKH_KM_WR_OFFSET
#define DP_HDCP_2_2_AKE_SEND_HPRIME_OFFSET	DP_HDCP_2_2_REG_HPRIME_OFFSET
#define DP_HDCP_2_2_AKE_SEND_PAIRING_INFO_OFFSET \
						DP_HDCP_2_2_REG_EKH_KM_RD_OFFSET
#define DP_HDCP_2_2_LC_INIT_OFFSET		DP_HDCP_2_2_REG_RN_OFFSET
#define DP_HDCP_2_2_LC_SEND_LPRIME_OFFSET	DP_HDCP_2_2_REG_LPRIME_OFFSET
#define DP_HDCP_2_2_SKE_SEND_EKS_OFFSET		DP_HDCP_2_2_REG_EDKEY_KS_OFFSET
#define DP_HDCP_2_2_REP_SEND_RECVID_LIST_OFFSET	DP_HDCP_2_2_REG_RXINFO_OFFSET
#define DP_HDCP_2_2_REP_SEND_ACK_OFFSET		DP_HDCP_2_2_REG_V_OFFSET
#define DP_HDCP_2_2_REP_STREAM_MANAGE_OFFSET	DP_HDCP_2_2_REG_SEQ_NUM_M_OFFSET
#define DP_HDCP_2_2_REP_STREAM_READY_OFFSET	DP_HDCP_2_2_REG_MPRIME_OFFSET

#define HDCP_2_2_DP_RXSTATUS_LEN		1
#define HDCP_2_2_DP_RXSTATUS_READY(x)		((x) & BIT(0))
#define HDCP_2_2_DP_RXSTATUS_H_PRIME(x)		((x) & BIT(1))
#define HDCP_2_2_DP_RXSTATUS_PAIRING(x)		((x) & BIT(2))
#define HDCP_2_2_DP_RXSTATUS_REAUTH_REQ(x)	((x) & BIT(3))
#define HDCP_2_2_DP_RXSTATUS_LINK_FAILED(x)	((x) & BIT(4))

/* DP 1.2 Sideband message defines */
/* peer device type - DP 1.2a Table 2-92 */
#define DP_PEER_DEVICE_NONE		0x0
#define DP_PEER_DEVICE_SOURCE_OR_SST	0x1
#define DP_PEER_DEVICE_MST_BRANCHING	0x2
#define DP_PEER_DEVICE_SST_SINK		0x3
#define DP_PEER_DEVICE_DP_LEGACY_CONV	0x4

/* DP 1.2 MST sideband request names DP 1.2a Table 2-80 */
#define DP_GET_MSG_TRANSACTION_VERSION	0x00 /* DP 1.3 */
#define DP_LINK_ADDRESS			0x01
#define DP_CONNECTION_STATUS_NOTIFY	0x02
#define DP_ENUM_PATH_RESOURCES		0x10
#define DP_ALLOCATE_PAYLOAD		0x11
#define DP_QUERY_PAYLOAD		0x12
#define DP_RESOURCE_STATUS_NOTIFY	0x13
#define DP_CLEAR_PAYLOAD_ID_TABLE	0x14
#define DP_REMOTE_DPCD_READ		0x20
#define DP_REMOTE_DPCD_WRITE		0x21
#define DP_REMOTE_I2C_READ		0x22
#define DP_REMOTE_I2C_WRITE		0x23
#define DP_POWER_UP_PHY			0x24
#define DP_POWER_DOWN_PHY		0x25
#define DP_SINK_EVENT_NOTIFY		0x30
#define DP_QUERY_STREAM_ENC_STATUS	0x38
#define  DP_QUERY_STREAM_ENC_STATUS_STATE_NO_EXIST	0
#define  DP_QUERY_STREAM_ENC_STATUS_STATE_INACTIVE	1
#define  DP_QUERY_STREAM_ENC_STATUS_STATE_ACTIVE	2

/* DP 1.2 MST sideband reply types */
#define DP_SIDEBAND_REPLY_ACK		0x00
#define DP_SIDEBAND_REPLY_NAK		0x01

/* DP 1.2 MST sideband nak reasons - table 2.84 */
#define DP_NAK_WRITE_FAILURE		0x01
#define DP_NAK_INVALID_READ		0x02
#define DP_NAK_CRC_FAILURE		0x03
#define DP_NAK_BAD_PARAM		0x04
#define DP_NAK_DEFER			0x05
#define DP_NAK_LINK_FAILURE		0x06
#define DP_NAK_NO_RESOURCES		0x07
#define DP_NAK_DPCD_FAIL		0x08
#define DP_NAK_I2C_NAK			0x09
#define DP_NAK_ALLOCATE_FAIL		0x0a

#define MODE_I2C_START	1
#define MODE_I2C_WRITE	2
#define MODE_I2C_READ	4
#define MODE_I2C_STOP	8

/* DP 1.2 MST PORTs - Section 2.5.1 v1.2a spec */
#define DP_MST_PHYSICAL_PORT_0 0
#define DP_MST_LOGICAL_PORT_0 8

#define DP_LINK_CONSTANT_N_VALUE 0x8000
#define DP_LINK_STATUS_SIZE	   6

#define DP_BRANCH_OUI_HEADER_SIZE	0xc
#define DP_RECEIVER_CAP_SIZE		0xf
#define DP_DSC_RECEIVER_CAP_SIZE        0x10 /* DSC Capabilities 0x60 through 0x6F */
#define EDP_PSR_RECEIVER_CAP_SIZE	2
#define EDP_DISPLAY_CTL_CAP_SIZE	3
#define DP_LTTPR_COMMON_CAP_SIZE	8
#define DP_LTTPR_PHY_CAP_SIZE		3

#define DP_SDP_AUDIO_TIMESTAMP		0x01
#define DP_SDP_AUDIO_STREAM		0x02
#define DP_SDP_EXTENSION		0x04 /* DP 1.1 */
#define DP_SDP_AUDIO_COPYMANAGEMENT	0x05 /* DP 1.2 */
#define DP_SDP_ISRC			0x06 /* DP 1.2 */
#define DP_SDP_VSC			0x07 /* DP 1.2 */
#define DP_SDP_CAMERA_GENERIC(i)	(0x08 + (i)) /* 0-7, DP 1.3 */
#define DP_SDP_PPS			0x10 /* DP 1.4 */
#define DP_SDP_VSC_EXT_VESA		0x20 /* DP 1.4 */
#define DP_SDP_VSC_EXT_CEA		0x21 /* DP 1.4 */
/* 0x80+ CEA-861 infoframe types */

#define DP_SDP_AUDIO_INFOFRAME_HB2	0x1b

/**
 * struct dp_sdp_header - DP secondary data packet header
 * @HB0: Secondary Data Packet ID
 * @HB1: Secondary Data Packet Type
 * @HB2: Secondary Data Packet Specific header, Byte 0
 * @HB3: Secondary Data packet Specific header, Byte 1
 */
struct dp_sdp_header {
	u8 HB0;
	u8 HB1;
	u8 HB2;
	u8 HB3;
} __packed;

#define EDP_SDP_HEADER_REVISION_MASK		0x1F
#define EDP_SDP_HEADER_VALID_PAYLOAD_BYTES	0x1F
#define DP_SDP_PPS_HEADER_PAYLOAD_BYTES_MINUS_1 0x7F

/**
 * struct dp_sdp - DP secondary data packet
 * @sdp_header: DP secondary data packet header
 * @db: DP secondaray data packet data blocks
 * VSC SDP Payload for PSR
 * db[0]: Stereo Interface
 * db[1]: 0 - PSR State; 1 - Update RFB; 2 - CRC Valid
 * db[2]: CRC value bits 7:0 of the R or Cr component
 * db[3]: CRC value bits 15:8 of the R or Cr component
 * db[4]: CRC value bits 7:0 of the G or Y component
 * db[5]: CRC value bits 15:8 of the G or Y component
 * db[6]: CRC value bits 7:0 of the B or Cb component
 * db[7]: CRC value bits 15:8 of the B or Cb component
 * db[8] - db[31]: Reserved
 * VSC SDP Payload for Pixel Encoding/Colorimetry Format
 * db[0] - db[15]: Reserved
 * db[16]: Pixel Encoding and Colorimetry Formats
 * db[17]: Dynamic Range and Component Bit Depth
 * db[18]: Content Type
 * db[19] - db[31]: Reserved
 */
struct dp_sdp {
	struct dp_sdp_header sdp_header;
	u8 db[32];
} __packed;

#define EDP_VSC_PSR_STATE_ACTIVE	(1<<0)
#define EDP_VSC_PSR_UPDATE_RFB		(1<<1)
#define EDP_VSC_PSR_CRC_VALUES_VALID	(1<<2)

/**
 * enum dp_pixelformat - drm DP Pixel encoding formats
 *
 * This enum is used to indicate DP VSC SDP Pixel encoding formats.
 * It is based on DP 1.4 spec [Table 2-117: VSC SDP Payload for DB16 through
 * DB18]
 *
 * @DP_PIXELFORMAT_RGB: RGB pixel encoding format
 * @DP_PIXELFORMAT_YUV444: YCbCr 4:4:4 pixel encoding format
 * @DP_PIXELFORMAT_YUV422: YCbCr 4:2:2 pixel encoding format
 * @DP_PIXELFORMAT_YUV420: YCbCr 4:2:0 pixel encoding format
 * @DP_PIXELFORMAT_Y_ONLY: Y Only pixel encoding format
 * @DP_PIXELFORMAT_RAW: RAW pixel encoding format
 * @DP_PIXELFORMAT_RESERVED: Reserved pixel encoding format
 */
enum dp_pixelformat {
	DP_PIXELFORMAT_RGB = 0,
	DP_PIXELFORMAT_YUV444 = 0x1,
	DP_PIXELFORMAT_YUV422 = 0x2,
	DP_PIXELFORMAT_YUV420 = 0x3,
	DP_PIXELFORMAT_Y_ONLY = 0x4,
	DP_PIXELFORMAT_RAW = 0x5,
	DP_PIXELFORMAT_RESERVED = 0x6,
};

/**
 * enum dp_colorimetry - drm DP Colorimetry formats
 *
 * This enum is used to indicate DP VSC SDP Colorimetry formats.
 * It is based on DP 1.4 spec [Table 2-117: VSC SDP Payload for DB16 through
 * DB18] and a name of enum member follows enum drm_colorimetry definition.
 *
 * @DP_COLORIMETRY_DEFAULT: sRGB (IEC 61966-2-1) or
 *                          ITU-R BT.601 colorimetry format
 * @DP_COLORIMETRY_RGB_WIDE_FIXED: RGB wide gamut fixed point colorimetry format
 * @DP_COLORIMETRY_BT709_YCC: ITU-R BT.709 colorimetry format
 * @DP_COLORIMETRY_RGB_WIDE_FLOAT: RGB wide gamut floating point
 *                                 (scRGB (IEC 61966-2-2)) colorimetry format
 * @DP_COLORIMETRY_XVYCC_601: xvYCC601 colorimetry format
 * @DP_COLORIMETRY_OPRGB: OpRGB colorimetry format
 * @DP_COLORIMETRY_XVYCC_709: xvYCC709 colorimetry format
 * @DP_COLORIMETRY_DCI_P3_RGB: DCI-P3 (SMPTE RP 431-2) colorimetry format
 * @DP_COLORIMETRY_SYCC_601: sYCC601 colorimetry format
 * @DP_COLORIMETRY_RGB_CUSTOM: RGB Custom Color Profile colorimetry format
 * @DP_COLORIMETRY_OPYCC_601: opYCC601 colorimetry format
 * @DP_COLORIMETRY_BT2020_RGB: ITU-R BT.2020 R' G' B' colorimetry format
 * @DP_COLORIMETRY_BT2020_CYCC: ITU-R BT.2020 Y'c C'bc C'rc colorimetry format
 * @DP_COLORIMETRY_BT2020_YCC: ITU-R BT.2020 Y' C'b C'r colorimetry format
 */
enum dp_colorimetry {
	DP_COLORIMETRY_DEFAULT = 0,
	DP_COLORIMETRY_RGB_WIDE_FIXED = 0x1,
	DP_COLORIMETRY_BT709_YCC = 0x1,
	DP_COLORIMETRY_RGB_WIDE_FLOAT = 0x2,
	DP_COLORIMETRY_XVYCC_601 = 0x2,
	DP_COLORIMETRY_OPRGB = 0x3,
	DP_COLORIMETRY_XVYCC_709 = 0x3,
	DP_COLORIMETRY_DCI_P3_RGB = 0x4,
	DP_COLORIMETRY_SYCC_601 = 0x4,
	DP_COLORIMETRY_RGB_CUSTOM = 0x5,
	DP_COLORIMETRY_OPYCC_601 = 0x5,
	DP_COLORIMETRY_BT2020_RGB = 0x6,
	DP_COLORIMETRY_BT2020_CYCC = 0x6,
	DP_COLORIMETRY_BT2020_YCC = 0x7,
};

/**
 * enum dp_dynamic_range - drm DP Dynamic Range
 *
 * This enum is used to indicate DP VSC SDP Dynamic Range.
 * It is based on DP 1.4 spec [Table 2-117: VSC SDP Payload for DB16 through
 * DB18]
 *
 * @DP_DYNAMIC_RANGE_VESA: VESA range
 * @DP_DYNAMIC_RANGE_CTA: CTA range
 */
enum dp_dynamic_range {
	DP_DYNAMIC_RANGE_VESA = 0,
	DP_DYNAMIC_RANGE_CTA = 1,
};

/**
 * enum dp_content_type - drm DP Content Type
 *
 * This enum is used to indicate DP VSC SDP Content Types.
 * It is based on DP 1.4 spec [Table 2-117: VSC SDP Payload for DB16 through
 * DB18]
 * CTA-861-G defines content types and expected processing by a sink device
 *
 * @DP_CONTENT_TYPE_NOT_DEFINED: Not defined type
 * @DP_CONTENT_TYPE_GRAPHICS: Graphics type
 * @DP_CONTENT_TYPE_PHOTO: Photo type
 * @DP_CONTENT_TYPE_VIDEO: Video type
 * @DP_CONTENT_TYPE_GAME: Game type
 */
enum dp_content_type {
	DP_CONTENT_TYPE_NOT_DEFINED = 0x00,
	DP_CONTENT_TYPE_GRAPHICS = 0x01,
	DP_CONTENT_TYPE_PHOTO = 0x02,
	DP_CONTENT_TYPE_VIDEO = 0x03,
	DP_CONTENT_TYPE_GAME = 0x04,
};

/* The DPCD capability helpers (Linux include/drm/display/drm_dp_helper.h). */

static inline bool
dp_tps3_supported(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x12 &&
		dpcd[DP_MAX_LANE_COUNT] & DP_TPS3_SUPPORTED;
}

static inline bool
dp_tps4_supported(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DPCD_REV] >= 0x14 &&
		dpcd[DP_MAX_DOWNSPREAD] & DP_TPS4_SUPPORTED;
}

static inline bool
dp_is_branch(const u8 dpcd[DP_RECEIVER_CAP_SIZE])
{
	return dpcd[DP_DOWNSTREAMPORT_PRESENT] & DP_DWN_STRM_PORT_PRESENT;
}

/* The training pattern symbol helper (Linux intel_dp_link_training.h). */

static inline u8 intel_dp_training_pattern_symbol(u8 pattern)
{
	return pattern & ~DP_LINK_SCRAMBLING_DISABLE;
}

/* The link training log formats (Linux intel_dp_link_training.c). */

#define TRAIN_REQ_FMT "%d/%d/%d/%d"
#define _TRAIN_REQ_VSWING_ARGS(link_status, lane) \
	(dp_get_adjust_request_voltage((link_status), (lane)) >> DP_TRAIN_VOLTAGE_SWING_SHIFT)
#define TRAIN_REQ_VSWING_ARGS(link_status) \
	_TRAIN_REQ_VSWING_ARGS(link_status, 0), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 1), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 2), \
	_TRAIN_REQ_VSWING_ARGS(link_status, 3)
#define _TRAIN_REQ_PREEMPH_ARGS(link_status, lane) \
	(dp_get_adjust_request_pre_emphasis((link_status), (lane)) >> DP_TRAIN_PRE_EMPHASIS_SHIFT)
#define TRAIN_REQ_PREEMPH_ARGS(link_status) \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 0), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 1), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 2), \
	_TRAIN_REQ_PREEMPH_ARGS(link_status, 3)
#define _TRAIN_REQ_TX_FFE_ARGS(link_status, lane) \
	dp_get_adjust_tx_ffe_preset((link_status), (lane))
#define TRAIN_REQ_TX_FFE_ARGS(link_status) \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 0), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 1), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 2), \
	_TRAIN_REQ_TX_FFE_ARGS(link_status, 3)
#define TRAIN_SET_FMT "%d%s/%d%s/%d%s/%d%s"
#define _TRAIN_SET_VSWING_ARGS(train_set) \
	((train_set) & DP_TRAIN_VOLTAGE_SWING_MASK) >> DP_TRAIN_VOLTAGE_SWING_SHIFT, \
	(train_set) & DP_TRAIN_MAX_SWING_REACHED ? "(max)" : ""
#define TRAIN_SET_VSWING_ARGS(train_set) \
	_TRAIN_SET_VSWING_ARGS((train_set)[0]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[1]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[2]), \
	_TRAIN_SET_VSWING_ARGS((train_set)[3])
#define _TRAIN_SET_PREEMPH_ARGS(train_set) \
	((train_set) & DP_TRAIN_PRE_EMPHASIS_MASK) >> DP_TRAIN_PRE_EMPHASIS_SHIFT, \
	(train_set) & DP_TRAIN_MAX_PRE_EMPHASIS_REACHED ? "(max)" : ""
#define TRAIN_SET_PREEMPH_ARGS(train_set) \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[0]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[1]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[2]), \
	_TRAIN_SET_PREEMPH_ARGS((train_set)[3])
#define _TRAIN_SET_TX_FFE_ARGS(train_set) \
	((train_set) & DP_TX_FFE_PRESET_VALUE_MASK), ""
#define TRAIN_SET_TX_FFE_ARGS(train_set) \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[0]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[1]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[2]), \
	_TRAIN_SET_TX_FFE_ARGS((train_set)[3])

/* The panel power sequencer registers (Linux intel_pps_regs.h). */

/* zedBSD: register helper macros come from dp_compat.h */

/* Panel power sequencing */
#define PPS_BASE			0x61200
#define VLV_PPS_BASE			(VLV_DISPLAY_BASE + PPS_BASE)
#define PCH_PPS_BASE			0xC7200

#define _MMIO_PPS(pps_idx, reg)		_MMIO(dev_priv->display.pps.mmio_base -	\
					      PPS_BASE + (reg) +	\
					      (pps_idx) * 0x100)

#define _PP_STATUS			0x61200
#define PP_STATUS(pps_idx)		_MMIO_PPS(pps_idx, _PP_STATUS)
#define   PP_ON				REG_BIT(31)
/*
 * Indicates that all dependencies of the panel are on:
 *
 * - PLL enabled
 * - pipe enabled
 * - LVDS/DVOB/DVOC on
 */
#define   PP_READY			REG_BIT(30)
#define   PP_SEQUENCE_MASK		REG_GENMASK(29, 28)
#define   PP_SEQUENCE_NONE		REG_FIELD_PREP(PP_SEQUENCE_MASK, 0)
#define   PP_SEQUENCE_POWER_UP		REG_FIELD_PREP(PP_SEQUENCE_MASK, 1)
#define   PP_SEQUENCE_POWER_DOWN	REG_FIELD_PREP(PP_SEQUENCE_MASK, 2)
#define   PP_CYCLE_DELAY_ACTIVE		REG_BIT(27)
#define   PP_SEQUENCE_STATE_MASK	REG_GENMASK(3, 0)
#define   PP_SEQUENCE_STATE_OFF_IDLE	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x0)
#define   PP_SEQUENCE_STATE_OFF_S0_1	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x1)
#define   PP_SEQUENCE_STATE_OFF_S0_2	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x2)
#define   PP_SEQUENCE_STATE_OFF_S0_3	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x3)
#define   PP_SEQUENCE_STATE_ON_IDLE	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x8)
#define   PP_SEQUENCE_STATE_ON_S1_1	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0x9)
#define   PP_SEQUENCE_STATE_ON_S1_2	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0xa)
#define   PP_SEQUENCE_STATE_ON_S1_3	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0xb)
#define   PP_SEQUENCE_STATE_RESET	REG_FIELD_PREP(PP_SEQUENCE_STATE_MASK, 0xf)

#define _PP_CONTROL			0x61204
#define PP_CONTROL(pps_idx)		_MMIO_PPS(pps_idx, _PP_CONTROL)
#define  PANEL_UNLOCK_MASK		REG_GENMASK(31, 16)
#define  PANEL_UNLOCK_REGS		REG_FIELD_PREP(PANEL_UNLOCK_MASK, 0xabcd)
#define  BXT_POWER_CYCLE_DELAY_MASK	REG_GENMASK(8, 4)
#define  EDP_FORCE_VDD			REG_BIT(3)
#define  EDP_BLC_ENABLE			REG_BIT(2)
#define  PANEL_POWER_RESET		REG_BIT(1)
#define  PANEL_POWER_ON			REG_BIT(0)

#define _PP_ON_DELAYS			0x61208
#define PP_ON_DELAYS(pps_idx)		_MMIO_PPS(pps_idx, _PP_ON_DELAYS)
#define  PANEL_PORT_SELECT_MASK		REG_GENMASK(31, 30)
#define  PANEL_PORT_SELECT_LVDS		REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, 0)
#define  PANEL_PORT_SELECT_DPA		REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, 1)
#define  PANEL_PORT_SELECT_DPC		REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, 2)
#define  PANEL_PORT_SELECT_DPD		REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, 3)
#define  PANEL_PORT_SELECT_VLV(port)	REG_FIELD_PREP(PANEL_PORT_SELECT_MASK, port)
#define  PANEL_POWER_UP_DELAY_MASK	REG_GENMASK(28, 16)
#define  PANEL_LIGHT_ON_DELAY_MASK	REG_GENMASK(12, 0)

#define _PP_OFF_DELAYS			0x6120C
#define PP_OFF_DELAYS(pps_idx)		_MMIO_PPS(pps_idx, _PP_OFF_DELAYS)
#define  PANEL_POWER_DOWN_DELAY_MASK	REG_GENMASK(28, 16)
#define  PANEL_LIGHT_OFF_DELAY_MASK	REG_GENMASK(12, 0)

#define _PP_DIVISOR			0x61210
#define PP_DIVISOR(pps_idx)		_MMIO_PPS(pps_idx, _PP_DIVISOR)
#define  PP_REFERENCE_DIVIDER_MASK	REG_GENMASK(31, 8)
#define  PANEL_POWER_CYCLE_DELAY_MASK	REG_GENMASK(4, 0)

/* The AUX channel registers (Linux intel_dp_aux_regs.h). */

/* zedBSD: register helper macros come from dp_compat.h */

/*
 * The aux channel provides a way to talk to the signal sink for DDC etc. Max
 * packet size supported is 20 bytes in each direction, hence the 5 fixed data
 * registers
 */

/*
 * Wrapper macro to convert from aux_ch to the index used in some of the
 * registers.
 */
#define __xe2lpd_aux_ch_idx(aux_ch)						\
	(aux_ch >= AUX_CH_USBC1 ? aux_ch : AUX_CH_USBC4 + 1 + (aux_ch) - AUX_CH_A)

#define _DPA_AUX_CH_CTL			0x64010
#define _DPB_AUX_CH_CTL			0x64110
#define _XELPDP_USBC1_AUX_CH_CTL	0x16f210
#define _XELPDP_USBC2_AUX_CH_CTL	0x16f410
#define DP_AUX_CH_CTL(aux_ch)		_MMIO_PORT(aux_ch, _DPA_AUX_CH_CTL,	\
						   _DPB_AUX_CH_CTL)
#define VLV_DP_AUX_CH_CTL(aux_ch)	_MMIO(VLV_DISPLAY_BASE + \
					      _PORT(aux_ch, _DPA_AUX_CH_CTL, _DPB_AUX_CH_CTL))
#define _XELPDP_DP_AUX_CH_CTL(aux_ch)						\
		_MMIO(_PICK_EVEN_2RANGES(aux_ch, AUX_CH_USBC1,			\
					 _DPA_AUX_CH_CTL, _DPB_AUX_CH_CTL,	\
					 _XELPDP_USBC1_AUX_CH_CTL,		\
					 _XELPDP_USBC2_AUX_CH_CTL))
#define XELPDP_DP_AUX_CH_CTL(i915__, aux_ch)					\
		(DISPLAY_VER(i915__) >= 20 ?					\
		 _XELPDP_DP_AUX_CH_CTL(__xe2lpd_aux_ch_idx(aux_ch)) :		\
		 _XELPDP_DP_AUX_CH_CTL(aux_ch))
#define   DP_AUX_CH_CTL_SEND_BUSY		REG_BIT(31)
#define   DP_AUX_CH_CTL_DONE			REG_BIT(30)
#define   DP_AUX_CH_CTL_INTERRUPT		REG_BIT(29)
#define   DP_AUX_CH_CTL_TIME_OUT_ERROR		REG_BIT(28)
#define   DP_AUX_CH_CTL_TIME_OUT_MASK		REG_GENMASK(27, 26)
#define   DP_AUX_CH_CTL_TIME_OUT_400us		REG_FIELD_PREP(DP_AUX_CH_CTL_TIME_OUT_MASK, 0)
#define   DP_AUX_CH_CTL_TIME_OUT_600us		REG_FIELD_PREP(DP_AUX_CH_CTL_TIME_OUT_MASK, 1)
#define   DP_AUX_CH_CTL_TIME_OUT_800us		REG_FIELD_PREP(DP_AUX_CH_CTL_TIME_OUT_MASK, 2)
#define   DP_AUX_CH_CTL_TIME_OUT_MAX		REG_FIELD_PREP(DP_AUX_CH_CTL_TIME_OUT_MASK, 3) /* Varies per platform */
#define   DP_AUX_CH_CTL_RECEIVE_ERROR		REG_BIT(25)
#define   DP_AUX_CH_CTL_MESSAGE_SIZE_MASK	REG_GENMASK(24, 20)
#define   DP_AUX_CH_CTL_MESSAGE_SIZE(x)		REG_FIELD_PREP(DP_AUX_CH_CTL_MESSAGE_SIZE_MASK, (x))
#define   DP_AUX_CH_CTL_PRECHARGE_2US_MASK	REG_GENMASK(19, 16) /* pre-skl */
#define   DP_AUX_CH_CTL_PRECHARGE_2US(x)	REG_FIELD_PREP(DP_AUX_CH_CTL_PRECHARGE_2US_MASK, (x))
#define   XELPDP_DP_AUX_CH_CTL_POWER_REQUEST	REG_BIT(19) /* mtl+ */
#define   XELPDP_DP_AUX_CH_CTL_POWER_STATUS	REG_BIT(18) /* mtl+ */
#define   DP_AUX_CH_CTL_AUX_AKSV_SELECT		REG_BIT(15)
#define   DP_AUX_CH_CTL_MANCHESTER_TEST		REG_BIT(14) /* pre-hsw */
#define   DP_AUX_CH_CTL_PSR_DATA_AUX_REG_SKL	REG_BIT(14) /* skl+ */
#define   DP_AUX_CH_CTL_SYNC_TEST		REG_BIT(13) /* pre-hsw */
#define   DP_AUX_CH_CTL_FS_DATA_AUX_REG_SKL	REG_BIT(13) /* skl+ */
#define   DP_AUX_CH_CTL_DEGLITCH_TEST		REG_BIT(12) /* pre-hsw */
#define   DP_AUX_CH_CTL_GTC_DATA_AUX_REG_SKL	REG_BIT(12) /* skl+ */
#define   DP_AUX_CH_CTL_PRECHARGE_TEST		REG_BIT(11) /* pre-hsw */
#define   DP_AUX_CH_CTL_TBT_IO			REG_BIT(11) /* icl+ */
#define   DP_AUX_CH_CTL_BIT_CLOCK_2X_MASK	REG_GENMASK(10, 0) /* pre-skl */
#define   DP_AUX_CH_CTL_BIT_CLOCK_2X(x)		REG_FIELD_PREP(DP_AUX_CH_CTL_BIT_CLOCK_2X_MASK, (x))
#define   DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL_MASK	REG_GENMASK(9, 5) /* skl+ */
#define   DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(c)	REG_FIELD_PREP(DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL_MASK, (c) - 1)
#define   DP_AUX_CH_CTL_SYNC_PULSE_SKL_MASK	REG_GENMASK(4, 0) /* skl+ */
#define   DP_AUX_CH_CTL_SYNC_PULSE_SKL(c)	REG_FIELD_PREP(DP_AUX_CH_CTL_SYNC_PULSE_SKL_MASK, (c) - 1)

#define _DPA_AUX_CH_DATA1		0x64014
#define _DPB_AUX_CH_DATA1		0x64114
#define _XELPDP_USBC1_AUX_CH_DATA1	0x16f214
#define _XELPDP_USBC2_AUX_CH_DATA1	0x16f414
#define DP_AUX_CH_DATA(aux_ch, i)	_MMIO(_PORT(aux_ch, _DPA_AUX_CH_DATA1,	\
						    _DPB_AUX_CH_DATA1) + (i) * 4) /* 5 registers */
#define VLV_DP_AUX_CH_DATA(aux_ch, i)	_MMIO(VLV_DISPLAY_BASE + _PORT(aux_ch, _DPA_AUX_CH_DATA1, \
								       _DPB_AUX_CH_DATA1) + (i) * 4) /* 5 registers */
#define _XELPDP_DP_AUX_CH_DATA(aux_ch, i)					\
		_MMIO(_PICK_EVEN_2RANGES(aux_ch, AUX_CH_USBC1,			\
					 _DPA_AUX_CH_DATA1, _DPB_AUX_CH_DATA1,	\
					 _XELPDP_USBC1_AUX_CH_DATA1,		\
					 _XELPDP_USBC2_AUX_CH_DATA1) + (i) * 4) /* 5 registers */
#define XELPDP_DP_AUX_CH_DATA(i915__, aux_ch, i)				\
		(DISPLAY_VER(i915__) >= 20 ?					\
		 _XELPDP_DP_AUX_CH_DATA(__xe2lpd_aux_ch_idx(aux_ch), i) :	\
		 _XELPDP_DP_AUX_CH_DATA(aux_ch, i))

/* PICA Power Well Control */
#define XE2LPD_PICA_PW_CTL			_MMIO(0x16fe04)
#define   XE2LPD_PICA_CTL_POWER_REQUEST		REG_BIT(31)
#define   XE2LPD_PICA_CTL_POWER_STATUS		REG_BIT(30)

#endif /* DRIVERS_GPU_I915_INTEL_DP_H */

/*
 * The part only the DP environment compiles: it names the environment's
 * intel_wakeref_t, struct delayed_work, struct edp_power_seq and ktime_t.
 */
#if defined(I915_DISPLAY_WORLD_DP) && !defined(DRIVERS_GPU_I915_INTEL_DP_H_DP_WORLD)
#define DRIVERS_GPU_I915_INTEL_DP_H_DP_WORLD

/* The panel power sequencer state (Linux intel_display_types.h). */

/* from intel_display_types.h */
struct intel_pps {
	int panel_power_up_delay;
	int panel_power_down_delay;
	int panel_power_cycle_delay;
	int backlight_on_delay;
	int backlight_off_delay;
	struct delayed_work panel_vdd_work;
	bool want_panel_vdd;
	bool initializing;
	unsigned long last_power_on;
	unsigned long last_backlight_off;
	ktime_t panel_power_off_time;
	intel_wakeref_t vdd_wakeref;

	union {
		/*
		 * Pipe whose power sequencer is currently locked into
		 * this port. Only relevant on VLV/CHV.
		 */
		enum pipe pps_pipe;

		/*
		 * Power sequencer index. Only relevant on BXT+.
		 */
		int pps_idx;
	};

	/*
	 * Pipe currently driving the port. Used for preventing
	 * the use of the PPS for any pipe currentrly driving
	 * external DP as that will mess things up on VLV.
	 */
	enum pipe active_pipe;
	/*
	 * Set if the sequencer may be reset due to a power transition,
	 * requiring a reinitialization. Only relevant on BXT+.
	 */
	bool pps_reset;
	struct edp_power_seq pps_delays;
	struct edp_power_seq bios_pps_delays;
};

/* The panel power sequencer entry points (Linux intel_pps.h). */

/* zedBSD: types and intel_wakeref_t come from dp_compat.h, which includes this file */

enum pipe;
struct drm_i915_private;
struct intel_connector;
struct intel_crtc_state;
struct intel_dp;
struct intel_encoder;

intel_wakeref_t intel_pps_lock(struct intel_dp *intel_dp);
intel_wakeref_t intel_pps_unlock(struct intel_dp *intel_dp, intel_wakeref_t wakeref);

#define with_intel_pps_lock(dp, wf)						\
	for ((wf) = intel_pps_lock(dp); (wf); (wf) = intel_pps_unlock((dp), (wf)))

void intel_pps_backlight_on(struct intel_dp *intel_dp);
void intel_pps_backlight_off(struct intel_dp *intel_dp);
void intel_pps_backlight_power(struct intel_connector *connector, bool enable);

bool intel_pps_vdd_on_unlocked(struct intel_dp *intel_dp);
void intel_pps_vdd_off_unlocked(struct intel_dp *intel_dp, bool sync);
void intel_pps_on_unlocked(struct intel_dp *intel_dp);
void intel_pps_off_unlocked(struct intel_dp *intel_dp);
void intel_pps_check_power_unlocked(struct intel_dp *intel_dp);

void intel_pps_vdd_on(struct intel_dp *intel_dp);
void intel_pps_on(struct intel_dp *intel_dp);
void intel_pps_off(struct intel_dp *intel_dp);
void intel_pps_vdd_off_sync(struct intel_dp *intel_dp);
bool intel_pps_have_panel_power_or_vdd(struct intel_dp *intel_dp);
void intel_pps_wait_power_cycle(struct intel_dp *intel_dp);

bool intel_pps_init(struct intel_dp *intel_dp);
void intel_pps_init_late(struct intel_dp *intel_dp);
void intel_pps_encoder_reset(struct intel_dp *intel_dp);
void intel_pps_reset_all(struct drm_i915_private *i915);

void vlv_pps_init(struct intel_encoder *encoder,
		  const struct intel_crtc_state *crtc_state);

void intel_pps_unlock_regs_wa(struct drm_i915_private *i915);
void intel_pps_setup(struct drm_i915_private *i915);

void assert_pps_unlocked(struct drm_i915_private *i915, enum pipe pipe);

/* The AUX channel entry points (Linux intel_dp_aux.h). */

/* zedBSD: types come from dp_compat.h, which includes this file */

enum aux_ch;
struct drm_i915_private;
struct intel_dp;
struct intel_encoder;

void intel_dp_aux_fini(struct intel_dp *intel_dp);
void intel_dp_aux_init(struct intel_dp *intel_dp);

enum aux_ch intel_dp_aux_ch(struct intel_encoder *encoder);

void intel_dp_aux_irq_handler(struct drm_i915_private *i915);
u32 intel_dp_aux_pack(const u8 *src, int src_bytes);
int intel_dp_aux_fw_sync_len(struct intel_dp *intel_dp);

#endif /* DRIVERS_GPU_I915_INTEL_DP_H_DP_WORLD */

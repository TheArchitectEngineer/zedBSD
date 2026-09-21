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
 * zedBSD WS031: macro definitions extracted textually from the Linux v6.8.12 reference include/drm/display/drm_dp.h
 * (sha256 306a1a47ba001c417baa3dab1f3a58c7e5de3c7a0537d26806a130603b599c5f) by tools/port_lcd_calc.py:
 * the root macros listed in tools/port_lcd_modeset.json plus every macro of that header they use.
 * The notice above is the source file's own.  Do not edit by hand.
 */
/*
 * zedBSD display rebuild: moved from src/drivers/gpu/i915/parity/lcd/lcd_mreg_drm_dp.h of the
 * pre-rebuild tree (commit 7e7ff337) without change to any definition;
 * the include guard PARITY_LCD_MREG_DRM_DP_H was renamed.  The provenance above still applies and names the generator.
 */

#ifndef DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DRM_DP_INC
#define DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DRM_DP_INC

#define DP_DPCD_REV                         0x000
# define DP_DPCD_REV_14                     0x14
#define DP_MAX_LANE_COUNT                   0x002
# define DP_TPS3_SUPPORTED		    (1 << 6) /* 1.2 */
#define DP_MAX_DOWNSPREAD                   0x003
# define DP_TPS4_SUPPORTED                  (1 << 7)
#define DP_DOWNSTREAMPORT_PRESENT           0x005
# define DP_DWN_STRM_PORT_PRESENT           (1 << 0)
#define DP_TRAINING_AUX_RD_INTERVAL             0x00e   /* XXX 1.2? */
# define DP_TRAINING_AUX_RD_MASK                0x7F    /* DP 1.3 */
#define DP_SUPPORTED_LINK_RATES		    0x010 /* eDP 1.4 */
# define DP_MAX_SUPPORTED_RATES		     8	    /* 16-bit little-endian */
# define DP_DS_PORT_HPD			    (1 << 3)
#define	DP_LINK_BW_SET		            0x100
# define DP_LINK_BW_10                      0x01    /* 2.0 128b/132b Link Layer */
# define DP_LINK_BW_13_5                    0x04    /* 2.0 128b/132b Link Layer */
# define DP_LINK_BW_20                      0x02    /* 2.0 128b/132b Link Layer */
#define DP_LANE_COUNT_SET	            0x101
# define DP_LANE_COUNT_ENHANCED_FRAME_EN    (1 << 7)
#define DP_TRAINING_PATTERN_SET	            0x102
# define DP_TRAINING_PATTERN_DISABLE	    0
# define DP_TRAINING_PATTERN_1		    1
# define DP_TRAINING_PATTERN_2		    2
# define DP_TRAINING_PATTERN_3		    3	    /* 1.2 */
# define DP_TRAINING_PATTERN_4              7       /* 1.4 */
# define DP_LINK_SCRAMBLING_DISABLE	    (1 << 5)
#define DP_TRAINING_LANE0_SET		    0x103
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
# define DP_MSA_TIMING_PAR_IGNORE_EN	    (1 << 7) /* eDP */
# define DP_SET_ANSI_8B10B		    (1 << 0)
# define DP_SET_ANSI_128B132B               (1 << 1)
#define DP_LINK_RATE_SET		    0x115   /* eDP 1.4 */
#define DP_LANE0_1_STATUS		    0x202
# define DP_LANE_CR_DONE		    (1 << 0)
# define DP_LANE_CHANNEL_EQ_DONE	    (1 << 1)
# define DP_LANE_SYMBOL_LOCKED		    (1 << 2)
#define DP_CHANNEL_EQ_BITS (DP_LANE_CR_DONE |		\
			    DP_LANE_CHANNEL_EQ_DONE |	\
			    DP_LANE_SYMBOL_LOCKED)
#define DP_LANE_ALIGN_STATUS_UPDATED                    0x204
#define  DP_INTERLANE_ALIGN_DONE                        (1 << 0)
#define DP_SINK_STATUS			    0x205
#define DP_ADJUST_REQUEST_LANE0_1	    0x206
# define DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT 0
# define DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT  2
# define DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT 4
# define DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT  6
# define DP_ADJUST_TX_FFE_PRESET_LANE0_SHIFT 0
# define DP_ADJUST_TX_FFE_PRESET_LANE1_SHIFT 4
#define DP_SOURCE_OUI			    0x300
#define DP_SET_POWER                        0x600
# define DP_SET_POWER_D0                    0x1
# define DP_SET_POWER_D3                    0x2
#define DP_128B132B_TRAINING_AUX_RD_INTERVAL                    0x2216 /* 2.0 */
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_MASK              0x7f
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_400_US            0x00
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_4_MS              0x01
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_8_MS              0x02
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_12_MS             0x03
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_16_MS             0x04
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_32_MS             0x05
# define DP_128B132B_TRAINING_AUX_RD_INTERVAL_64_MS             0x06
#define DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV 0xf0000 /* 1.3 */
#define DP_PHY_REPEATER_CNT				    0xf0002 /* 1.3 */
#define DP_PHY_REPEATER_MODE				    0xf0003 /* 1.3 */
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
#define DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1	    0xf0020 /* 1.4a */
#define DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy)	\
	DP_LTTPR_REG(dp_phy, DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1)
#define DP_TRANSMITTER_CAPABILITY_PHY_REPEATER1		    0xf0021 /* 1.4a */
# define DP_VOLTAGE_SWING_LEVEL_3_SUPPORTED		    BIT(0)
# define DP_PRE_EMPHASIS_LEVEL_3_SUPPORTED		    BIT(1)
#define DP_128B132B_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1  0xf0022 /* 2.0 */
#define DP_128B132B_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy)	\
	DP_LTTPR_REG(dp_phy, DP_128B132B_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1)
#define DP_LANE0_1_STATUS_PHY_REPEATER1			    0xf0030 /* 1.3 */
#define DP_LANE0_1_STATUS_PHY_REPEATER(dp_phy) \
	DP_LTTPR_REG(dp_phy, DP_LANE0_1_STATUS_PHY_REPEATER1)
#define DP_PHY_REPEATER_MODE_TRANSPARENT		    0x55    /* 1.3 */
#define DP_PHY_REPEATER_MODE_NON_TRANSPARENT		    0xaa    /* 1.3 */
#define DP_LINK_STATUS_SIZE	   6
#define DP_RECEIVER_CAP_SIZE		0xf
#define DP_LTTPR_COMMON_CAP_SIZE	8
#define DP_LTTPR_PHY_CAP_SIZE		3

#endif /* DRIVERS_GPU_I915_DATA_DISPLAY_MREG_DRM_DP_INC */

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2007-2008 Intel Corporation
 *   Jesse Barnes <jesse.barnes@intel.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Copyright © 2008 Intel Corporation
 * Copyright © 2012-2016 Intel Corporation
 *
 * Authors:
 *    Keith Packard <keithp@keithp.com>
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
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2019 Intel Corporation.
 */

/*
 * The Linux display types the modeset environment refers to: the EDID
 * block structures and bits of drm_edid.h, the link M/N and DPLL hardware
 * state of intel_display_types.h, intel_dpll_mgr.h and i915_reg.h, enum
 * intel_pch of soc/intel_pch.h and enum intel_dpll_id of intel_dpll_mgr.h.
 * The DRM prefix of the EDID names is dropped: DRM_EDID_* is EDID_*.
 *
 * The EDID block structures and bits (Linux include/drm/drm_edid.h).
 * zedBSD WS031: EDID block structures and bit definitions extracted textually from Linux v6.8.12
 * include/drm/drm_edid.h (sha256 e61def12761bc325265437b33e91a7c0cc99ef3f7c60a4265eadb112ff66759a) by
 * tools/port_lcd_calc.py.  The copyright / permission notice above is the source file's own.
 *
 * The link M/N and DPLL hardware state (Linux intel_display_types.h, intel_dpll_mgr.h, i915_reg.h).
 * zedBSD WS031: structures and register-field macros extracted textually from the Linux v6.8.12 i915
 * reference (display/intel_display_types.h, display/intel_dpll_mgr.h, i915_reg.h: MIT permission
 * notices, Copyright Intel Corporation; the full notices are kept in intel_link_port.c and
 * intel_dpll_port.c) by tools/port_lcd_calc.py.
 *
 * The PCH types (Linux soc/intel_pch.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/soc/intel_pch.h
 * (sha256 ff783ac9577b2e0cd7a13c9cc974f109700a816382551c06f25ac9e89968420b) by tools/port_lcd_calc.py:
 * enum intel_pch.
 * The notice above is the source file's own.
 *
 * The DPLL ids (Linux intel_dpll_mgr.h).
 * zedBSD WS031: definitions extracted textually from the Linux v6.8.12 reference drivers/gpu/drm/i915/display/intel_dpll_mgr.h
 * (sha256 baaf9883fe61287a68e697bb7c9978ca566fcbf64178c0c6c2cac57699bc5c9f) by tools/port_lcd_calc.py:
 * enum intel_dpll_id.
 * The notice above is the source file's own.
 */

#ifndef DRIVERS_GPU_I915_INTEL_REF_H
#define DRIVERS_GPU_I915_INTEL_REF_H

/* The EDID block structures and bits (Linux include/drm/drm_edid.h). */

struct est_timings {
	u8 t1;
	u8 t2;
	u8 mfg_rsvd;
} __attribute__((packed));

#define EDID_TIMING_ASPECT_SHIFT 6
#define EDID_TIMING_ASPECT_MASK  (0x3 << EDID_TIMING_ASPECT_SHIFT)
#define EDID_TIMING_VFREQ_SHIFT  0
#define EDID_TIMING_VFREQ_MASK   (0x3f << EDID_TIMING_VFREQ_SHIFT)

struct std_timing {
	u8 hsize; /* need to multiply by 8 then add 248 */
	u8 vfreq_aspect;
} __attribute__((packed));

#define EDID_PT_HSYNC_POSITIVE (1 << 1)
#define EDID_PT_VSYNC_POSITIVE (1 << 2)
#define EDID_PT_SEPARATE_SYNC  (3 << 3)
#define EDID_PT_STEREO         (1 << 5)
#define EDID_PT_INTERLACED     (1 << 7)

/* If detailed data is pixel timing */
struct detailed_pixel_timing {
	u8 hactive_lo;
	u8 hblank_lo;
	u8 hactive_hblank_hi;
	u8 vactive_lo;
	u8 vblank_lo;
	u8 vactive_vblank_hi;
	u8 hsync_offset_lo;
	u8 hsync_pulse_width_lo;
	u8 vsync_offset_pulse_width_lo;
	u8 hsync_vsync_offset_pulse_width_hi;
	u8 width_mm_lo;
	u8 height_mm_lo;
	u8 width_height_mm_hi;
	u8 hborder;
	u8 vborder;
	u8 misc;
} __attribute__((packed));

/* If it's not pixel timing, it'll be one of the below */
struct detailed_data_string {
	u8 str[13];
} __attribute__((packed));

#define EDID_RANGE_OFFSET_MIN_VFREQ (1 << 0) /* 1.4 */
#define EDID_RANGE_OFFSET_MAX_VFREQ (1 << 1) /* 1.4 */
#define EDID_RANGE_OFFSET_MIN_HFREQ (1 << 2) /* 1.4 */
#define EDID_RANGE_OFFSET_MAX_HFREQ (1 << 3) /* 1.4 */

#define EDID_DEFAULT_GTF_SUPPORT_FLAG   0x00 /* 1.3 */
#define EDID_RANGE_LIMITS_ONLY_FLAG     0x01 /* 1.4 */
#define EDID_SECONDARY_GTF_SUPPORT_FLAG 0x02 /* 1.3 */
#define EDID_CVT_SUPPORT_FLAG           0x04 /* 1.4 */

#define EDID_CVT_FLAGS_STANDARD_BLANKING (1 << 3)
#define EDID_CVT_FLAGS_REDUCED_BLANKING  (1 << 4)

struct detailed_data_monitor_range {
	u8 min_vfreq;
	u8 max_vfreq;
	u8 min_hfreq_khz;
	u8 max_hfreq_khz;
	u8 pixel_clock_mhz; /* need to multiply by 10 */
	u8 flags;
	union {
		struct {
			u8 reserved;
			u8 hfreq_start_khz; /* need to multiply by 2 */
			u8 c; /* need to divide by 2 */
			__le16 m;
			u8 k;
			u8 j; /* need to divide by 2 */
		} __attribute__((packed)) gtf2;
		struct {
			u8 version;
			u8 data1; /* high 6 bits: extra clock resolution */
			u8 data2; /* plus low 2 of above: max hactive */
			u8 supported_aspects;
			u8 flags; /* preferred aspect and blanking support */
			u8 supported_scalings;
			u8 preferred_refresh;
		} __attribute__((packed)) cvt;
	} __attribute__((packed)) formula;
} __attribute__((packed));

struct detailed_data_wpindex {
	u8 white_yx_lo; /* Lower 2 bits each */
	u8 white_x_hi;
	u8 white_y_hi;
	u8 gamma; /* need to divide by 100 then add 1 */
} __attribute__((packed));

struct detailed_data_color_point {
	u8 windex1;
	u8 wpindex1[3];
	u8 windex2;
	u8 wpindex2[3];
} __attribute__((packed));

struct cvt_timing {
	u8 code[3];
} __attribute__((packed));

struct detailed_non_pixel {
	u8 pad1;
	u8 type; /* ff=serial, fe=string, fd=monitor range, fc=monitor name
		    fb=color point data, fa=standard timing data,
		    f9=undefined, f8=mfg. reserved */
	u8 pad2;
	union {
		struct detailed_data_string str;
		struct detailed_data_monitor_range range;
		struct detailed_data_wpindex color;
		struct std_timing timings[6];
		struct cvt_timing cvt[4];
	} __attribute__((packed)) data;
} __attribute__((packed));

#define EDID_DETAIL_EST_TIMINGS 0xf7
#define EDID_DETAIL_CVT_3BYTE 0xf8
#define EDID_DETAIL_COLOR_MGMT_DATA 0xf9
#define EDID_DETAIL_STD_MODES 0xfa
#define EDID_DETAIL_MONITOR_CPDATA 0xfb
#define EDID_DETAIL_MONITOR_NAME 0xfc
#define EDID_DETAIL_MONITOR_RANGE 0xfd
#define EDID_DETAIL_MONITOR_STRING 0xfe
#define EDID_DETAIL_MONITOR_SERIAL 0xff

struct detailed_timing {
	__le16 pixel_clock; /* need to multiply by 10 KHz */
	union {
		struct detailed_pixel_timing pixel_data;
		struct detailed_non_pixel other_data;
	} __attribute__((packed)) data;
} __attribute__((packed));

#define EDID_INPUT_SERRATION_VSYNC (1 << 0)
#define EDID_INPUT_SYNC_ON_GREEN   (1 << 1)
#define EDID_INPUT_COMPOSITE_SYNC  (1 << 2)
#define EDID_INPUT_SEPARATE_SYNCS  (1 << 3)
#define EDID_INPUT_BLANK_TO_BLACK  (1 << 4)
#define EDID_INPUT_VIDEO_LEVEL     (3 << 5)
#define EDID_INPUT_DIGITAL         (1 << 7)
#define EDID_DIGITAL_DEPTH_MASK    (7 << 4) /* 1.4 */
#define EDID_DIGITAL_DEPTH_UNDEF   (0 << 4) /* 1.4 */
#define EDID_DIGITAL_DEPTH_6       (1 << 4) /* 1.4 */
#define EDID_DIGITAL_DEPTH_8       (2 << 4) /* 1.4 */
#define EDID_DIGITAL_DEPTH_10      (3 << 4) /* 1.4 */
#define EDID_DIGITAL_DEPTH_12      (4 << 4) /* 1.4 */
#define EDID_DIGITAL_DEPTH_14      (5 << 4) /* 1.4 */
#define EDID_DIGITAL_DEPTH_16      (6 << 4) /* 1.4 */
#define EDID_DIGITAL_DEPTH_RSVD    (7 << 4) /* 1.4 */
#define EDID_DIGITAL_TYPE_MASK     (7 << 0) /* 1.4 */
#define EDID_DIGITAL_TYPE_UNDEF    (0 << 0) /* 1.4 */
#define EDID_DIGITAL_TYPE_DVI      (1 << 0) /* 1.4 */
#define EDID_DIGITAL_TYPE_HDMI_A   (2 << 0) /* 1.4 */
#define EDID_DIGITAL_TYPE_HDMI_B   (3 << 0) /* 1.4 */
#define EDID_DIGITAL_TYPE_MDDI     (4 << 0) /* 1.4 */
#define EDID_DIGITAL_TYPE_DP       (5 << 0) /* 1.4 */
#define EDID_DIGITAL_DFP_1_X       (1 << 0) /* 1.3 */
#define EDID_FEATURE_DEFAULT_GTF      (1 << 0) /* 1.2 */
#define EDID_FEATURE_CONTINUOUS_FREQ  (1 << 0) /* 1.4 */
#define EDID_FEATURE_PREFERRED_TIMING (1 << 1)
#define EDID_FEATURE_STANDARD_COLOR   (1 << 2)
#define EDID_FEATURE_DISPLAY_TYPE     (3 << 3) /* 00=mono, 01=rgb, 10=non-rgb, 11=unknown */
#define EDID_FEATURE_COLOR_MASK	  (3 << 3)
#define EDID_FEATURE_RGB		  (0 << 3)
#define EDID_FEATURE_RGB_YCRCB444	  (1 << 3)
#define EDID_FEATURE_RGB_YCRCB422	  (2 << 3)
#define EDID_FEATURE_RGB_YCRCB	  (3 << 3) /* both 4:4:4 and 4:2:2 */
#define EDID_FEATURE_PM_ACTIVE_OFF    (1 << 5)
#define EDID_FEATURE_PM_SUSPEND       (1 << 6)
#define EDID_FEATURE_PM_STANDBY       (1 << 7)
#define EDID_HDMI_DC_48               (1 << 6)
#define EDID_HDMI_DC_36               (1 << 5)
#define EDID_HDMI_DC_30               (1 << 4)
#define EDID_HDMI_DC_Y444             (1 << 3)
#define EDID_DSC_10BPC			(1 << 0)
#define EDID_DSC_12BPC			(1 << 1)
#define EDID_DSC_16BPC			(1 << 2)
#define EDID_DSC_ALL_BPP			(1 << 3)
#define EDID_DSC_NATIVE_420			(1 << 6)
#define EDID_DSC_1P2			(1 << 7)
#define EDID_DSC_MAX_FRL_RATE_MASK		0xf0
#define EDID_DSC_MAX_SLICES			0xf
#define EDID_DSC_TOTAL_CHUNK_KBYTES		0x3f

struct edid {
	u8 header[8];
	/* Vendor & product info */
	u8 mfg_id[2];
	u8 prod_code[2];
	u32 serial; /* FIXME: byte order */
	u8 mfg_week;
	u8 mfg_year;
	/* EDID version */
	u8 version;
	u8 revision;
	/* Display info: */
	u8 input;
	u8 width_cm;
	u8 height_cm;
	u8 gamma;
	u8 features;
	/* Color characteristics */
	u8 red_green_lo;
	u8 blue_white_lo;
	u8 red_x;
	u8 red_y;
	u8 green_x;
	u8 green_y;
	u8 blue_x;
	u8 blue_y;
	u8 white_x;
	u8 white_y;
	/* Est. timings and mfg rsvd timings*/
	struct est_timings established_timings;
	/* Standard timings 1-8*/
	struct std_timing standard_timings[8];
	/* Detailing timings 1-4 */
	struct detailed_timing detailed_timings[4];
	/* Number of 128 byte ext. blocks */
	u8 extensions;
	/* Checksum */
	u8 checksum;
} __attribute__((packed));

/* The link M/N and DPLL hardware state (Linux intel_display_types.h, intel_dpll_mgr.h, i915_reg.h). */

struct intel_link_m_n {
	u32 tu;
	u32 data_m;
	u32 data_n;
	u32 link_m;
	u32 link_n;
};

struct intel_dpll_hw_state {
	/* i9xx, pch plls */
	u32 dpll;
	u32 dpll_md;
	u32 fp0;
	u32 fp1;

	/* hsw, bdw */
	u32 wrpll;
	u32 spll;

	/* skl */
	/*
	 * DPLL_CTRL1 has 6 bits for each each this DPLL. We store those in
	 * lower part of ctrl1 and they get shifted into position when writing
	 * the register.  This allows us to easily compare the state to share
	 * the DPLL.
	 */
	u32 ctrl1;
	/* HDMI only, 0 when used for DP */
	u32 cfgcr1, cfgcr2;

	/* icl */
	u32 cfgcr0;

	/* tgl */
	u32 div0;

	/* bxt */
	u32 ebb0, ebb4, pll0, pll1, pll2, pll3, pll6, pll8, pll9, pll10, pcsdw12;

	/*
	 * ICL uses the following, already defined:
	 * u32 cfgcr0, cfgcr1;
	 */
	u32 mg_refclkin_ctl;
	u32 mg_clktop2_coreclkctl1;
	u32 mg_clktop2_hsclkctl;
	u32 mg_pll_div0;
	u32 mg_pll_div1;
	u32 mg_pll_lf;
	u32 mg_pll_frac_lock;
	u32 mg_pll_ssc;
	u32 mg_pll_bias;
	u32 mg_pll_tdc_coldst_bias;
	u32 mg_pll_bias_mask;
	u32 mg_pll_tdc_coldst_bias_mask;
};

#define  TU_SIZE_MASK		REG_GENMASK(30, 25)
#define  TU_SIZE(x)		REG_FIELD_PREP(TU_SIZE_MASK, (x) - 1) /* default size 64 */
#define  DATA_LINK_M_N_MASK	REG_GENMASK(23, 0)
#define  DATA_LINK_N_MAX	(0x800000)
#define   DPLL_CFGCR0_DCO_FRACTION(x)	((x) << 10)
#define   DPLL_CFGCR1_QDIV_RATIO(x)	((x) << 10)
#define   DPLL_CFGCR1_QDIV_MODE(x)	((x) << 9)
#define   DPLL_CFGCR1_KDIV(x)		((x) << 6)
#define   DPLL_CFGCR1_PDIV(x)		((x) << 2)
#define   DPLL_CFGCR1_CENTRAL_FREQ_8400	(3 << 0)
#define   TGL_DPLL_CFGCR1_CFSELOVRD_NORMAL_XTAL	(0 << 0)
#define   TGL_DPLL0_DIV0_AFC_STARTUP_MASK		REG_GENMASK(27, 25)
#define   TGL_DPLL0_DIV0_AFC_STARTUP(val)		REG_FIELD_PREP(TGL_DPLL0_DIV0_AFC_STARTUP_MASK, (val))

/* The PCH types (Linux soc/intel_pch.h). */

enum intel_pch {
	PCH_NOP = -1,	/* PCH without south display */
	PCH_NONE = 0,	/* No PCH present */
	PCH_IBX,	/* Ibexpeak PCH */
	PCH_CPT,	/* Cougarpoint/Pantherpoint PCH */
	PCH_LPT,	/* Lynxpoint/Wildcatpoint PCH */
	PCH_SPT,        /* Sunrisepoint/Kaby Lake PCH */
	PCH_CNP,        /* Cannon/Comet Lake PCH */
	PCH_ICP,	/* Ice Lake/Jasper Lake PCH */
	PCH_TGP,	/* Tiger Lake/Mule Creek Canyon PCH */
	PCH_ADP,	/* Alder Lake PCH */

	/* Fake PCHs, functionality handled on the same PCI dev */
	PCH_DG1 = 1024,
	PCH_DG2,
	PCH_MTL,
	PCH_LNL,
};

/* The DPLL ids (Linux intel_dpll_mgr.h). */

enum intel_dpll_id {
	/**
	 * @DPLL_ID_PRIVATE: non-shared dpll in use
	 */
	DPLL_ID_PRIVATE = -1,

	/**
	 * @DPLL_ID_PCH_PLL_A: DPLL A in ILK, SNB and IVB
	 */
	DPLL_ID_PCH_PLL_A = 0,
	/**
	 * @DPLL_ID_PCH_PLL_B: DPLL B in ILK, SNB and IVB
	 */
	DPLL_ID_PCH_PLL_B = 1,


	/**
	 * @DPLL_ID_WRPLL1: HSW and BDW WRPLL1
	 */
	DPLL_ID_WRPLL1 = 0,
	/**
	 * @DPLL_ID_WRPLL2: HSW and BDW WRPLL2
	 */
	DPLL_ID_WRPLL2 = 1,
	/**
	 * @DPLL_ID_SPLL: HSW and BDW SPLL
	 */
	DPLL_ID_SPLL = 2,
	/**
	 * @DPLL_ID_LCPLL_810: HSW and BDW 0.81 GHz LCPLL
	 */
	DPLL_ID_LCPLL_810 = 3,
	/**
	 * @DPLL_ID_LCPLL_1350: HSW and BDW 1.35 GHz LCPLL
	 */
	DPLL_ID_LCPLL_1350 = 4,
	/**
	 * @DPLL_ID_LCPLL_2700: HSW and BDW 2.7 GHz LCPLL
	 */
	DPLL_ID_LCPLL_2700 = 5,


	/**
	 * @DPLL_ID_SKL_DPLL0: SKL and later DPLL0
	 */
	DPLL_ID_SKL_DPLL0 = 0,
	/**
	 * @DPLL_ID_SKL_DPLL1: SKL and later DPLL1
	 */
	DPLL_ID_SKL_DPLL1 = 1,
	/**
	 * @DPLL_ID_SKL_DPLL2: SKL and later DPLL2
	 */
	DPLL_ID_SKL_DPLL2 = 2,
	/**
	 * @DPLL_ID_SKL_DPLL3: SKL and later DPLL3
	 */
	DPLL_ID_SKL_DPLL3 = 3,


	/**
	 * @DPLL_ID_ICL_DPLL0: ICL/TGL combo PHY DPLL0
	 */
	DPLL_ID_ICL_DPLL0 = 0,
	/**
	 * @DPLL_ID_ICL_DPLL1: ICL/TGL combo PHY DPLL1
	 */
	DPLL_ID_ICL_DPLL1 = 1,
	/**
	 * @DPLL_ID_EHL_DPLL4: EHL combo PHY DPLL4
	 */
	DPLL_ID_EHL_DPLL4 = 2,
	/**
	 * @DPLL_ID_ICL_TBTPLL: ICL/TGL TBT PLL
	 */
	DPLL_ID_ICL_TBTPLL = 2,
	/**
	 * @DPLL_ID_ICL_MGPLL1: ICL MG PLL 1 port 1 (C),
	 *                      TGL TC PLL 1 port 1 (TC1)
	 */
	DPLL_ID_ICL_MGPLL1 = 3,
	/**
	 * @DPLL_ID_ICL_MGPLL2: ICL MG PLL 1 port 2 (D)
	 *                      TGL TC PLL 1 port 2 (TC2)
	 */
	DPLL_ID_ICL_MGPLL2 = 4,
	/**
	 * @DPLL_ID_ICL_MGPLL3: ICL MG PLL 1 port 3 (E)
	 *                      TGL TC PLL 1 port 3 (TC3)
	 */
	DPLL_ID_ICL_MGPLL3 = 5,
	/**
	 * @DPLL_ID_ICL_MGPLL4: ICL MG PLL 1 port 4 (F)
	 *                      TGL TC PLL 1 port 4 (TC4)
	 */
	DPLL_ID_ICL_MGPLL4 = 6,
	/**
	 * @DPLL_ID_TGL_MGPLL5: TGL TC PLL port 5 (TC5)
	 */
	DPLL_ID_TGL_MGPLL5 = 7,
	/**
	 * @DPLL_ID_TGL_MGPLL6: TGL TC PLL port 6 (TC6)
	 */
	DPLL_ID_TGL_MGPLL6 = 8,

	/**
	 * @DPLL_ID_DG1_DPLL0: DG1 combo PHY DPLL0
	 */
	DPLL_ID_DG1_DPLL0 = 0,
	/**
	 * @DPLL_ID_DG1_DPLL1: DG1 combo PHY DPLL1
	 */
	DPLL_ID_DG1_DPLL1 = 1,
	/**
	 * @DPLL_ID_DG1_DPLL2: DG1 combo PHY DPLL2
	 */
	DPLL_ID_DG1_DPLL2 = 2,
	/**
	 * @DPLL_ID_DG1_DPLL3: DG1 combo PHY DPLL3
	 */
	DPLL_ID_DG1_DPLL3 = 3,
};

#endif /* DRIVERS_GPU_I915_INTEL_REF_H */

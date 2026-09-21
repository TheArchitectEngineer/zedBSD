/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 */

/*
 * Copyright © 2016-2019 Intel Corporation
 * Copyright © 2006 Intel Corporation
 *
 * Authors:
 *    Eric Anholt <eric@anholt.net>
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
 */

/*
 * The VBT definitions display/vbt.h gives the DP environment: the types
 * and constants of Linux intel_bios.h (panel power sequence, backlight type,
 * MIPI sequences) and the enums and structures of the Linux display headers
 * that the VBT parser fills (port, AUX channel, PHY, PCH, DRRS, panel
 * data).
 *
 * The VBT types and constants (Linux intel_bios.h).
 * zedBSD WS031: copied from Linux v6.8.12 drivers/gpu/drm/i915/display/intel_bios.h
 * (sha256 d237a6394758f09e9612470ec14f4a214254b43d32df8d991209e3afe7a3befb) by tools/port_intel_bios.py.  Change: the <linux/types.h> include is removed.
 *
 * The enums and structures the parser fills (Linux display headers).
 * zedBSD WS031: enums and structures extracted textually from the Linux v6.8.12 i915
 * reference (MIT, Copyright Intel Corporation; see the notice in intel_bios_port.c) by
 * tools/port_intel_bios.py.
 */

#ifndef DRIVERS_GPU_I915_INTEL_VBT_H
#define DRIVERS_GPU_I915_INTEL_VBT_H

/* The VBT types and constants (Linux intel_bios.h). */

/*
 * Please use intel_vbt_defs.h for VBT private data, to hide and abstract away
 * the VBT from the rest of the driver. Add the parsed, clean data to struct
 * intel_vbt_data within struct drm_i915_private.
 */

/* zedBSD: <linux/types.h> is provided by vbt_compat.h, which includes this file */

struct drm_edid;
struct drm_i915_private;
struct intel_bios_encoder_data;
struct intel_crtc_state;
struct intel_encoder;
struct intel_panel;
enum aux_ch;
enum port;

enum intel_backlight_type {
	INTEL_BACKLIGHT_PMIC,
	INTEL_BACKLIGHT_LPSS,
	INTEL_BACKLIGHT_DISPLAY_DDI,
	INTEL_BACKLIGHT_DSI_DCS,
	INTEL_BACKLIGHT_PANEL_DRIVER_INTERFACE,
	INTEL_BACKLIGHT_VESA_EDP_AUX_INTERFACE,
};

struct edp_power_seq {
	u16 t1_t3;
	u16 t8;
	u16 t9;
	u16 t10;
	u16 t11_t12;
} __packed;

/*
 * MIPI Sequence Block definitions
 *
 * Note the VBT spec has AssertReset / DeassertReset swapped from their
 * usual naming, we use the proper names here to avoid confusion when
 * reading the code.
 */
enum mipi_seq {
	MIPI_SEQ_END = 0,
	MIPI_SEQ_DEASSERT_RESET,	/* Spec says MipiAssertResetPin */
	MIPI_SEQ_INIT_OTP,
	MIPI_SEQ_DISPLAY_ON,
	MIPI_SEQ_DISPLAY_OFF,
	MIPI_SEQ_ASSERT_RESET,		/* Spec says MipiDeassertResetPin */
	MIPI_SEQ_BACKLIGHT_ON,		/* sequence block v2+ */
	MIPI_SEQ_BACKLIGHT_OFF,		/* sequence block v2+ */
	MIPI_SEQ_TEAR_ON,		/* sequence block v2+ */
	MIPI_SEQ_TEAR_OFF,		/* sequence block v3+ */
	MIPI_SEQ_POWER_ON,		/* sequence block v3+ */
	MIPI_SEQ_POWER_OFF,		/* sequence block v3+ */
	MIPI_SEQ_MAX
};

enum mipi_seq_element {
	MIPI_SEQ_ELEM_END = 0,
	MIPI_SEQ_ELEM_SEND_PKT,
	MIPI_SEQ_ELEM_DELAY,
	MIPI_SEQ_ELEM_GPIO,
	MIPI_SEQ_ELEM_I2C,		/* sequence block v2+ */
	MIPI_SEQ_ELEM_SPI,		/* sequence block v3+ */
	MIPI_SEQ_ELEM_PMIC,		/* sequence block v3+ */
	MIPI_SEQ_ELEM_MAX
};

#define MIPI_DSI_UNDEFINED_PANEL_ID	0
#define MIPI_DSI_GENERIC_PANEL_ID	1

struct mipi_config {
	u16 panel_id;

	/* General Params */
	u32 enable_dithering:1;
	u32 rsvd1:1;
	u32 is_bridge:1;

	u32 panel_arch_type:2;
	u32 is_cmd_mode:1;

#define NON_BURST_SYNC_PULSE	0x1
#define NON_BURST_SYNC_EVENTS	0x2
#define BURST_MODE		0x3
	u32 video_transfer_mode:2;

	u32 cabc_supported:1;
#define PPS_BLC_PMIC   0
#define PPS_BLC_SOC    1
	u32 pwm_blc:1;

	/* Bit 13:10 */
#define PIXEL_FORMAT_RGB565			0x1
#define PIXEL_FORMAT_RGB666			0x2
#define PIXEL_FORMAT_RGB666_LOOSELY_PACKED	0x3
#define PIXEL_FORMAT_RGB888			0x4
	u32 videomode_color_format:4;

	/* Bit 15:14 */
#define ENABLE_ROTATION_0	0x0
#define ENABLE_ROTATION_90	0x1
#define ENABLE_ROTATION_180	0x2
#define ENABLE_ROTATION_270	0x3
	u32 rotation:2;
	u32 bta_enabled:1;
	u32 rsvd2:15;

	/* 2 byte Port Description */
#define DUAL_LINK_NOT_SUPPORTED	0
#define DUAL_LINK_FRONT_BACK	1
#define DUAL_LINK_PIXEL_ALT	2
	u16 dual_link:2;
	u16 lane_cnt:2;
	u16 pixel_overlap:3;
	u16 rgb_flip:1;
#define DL_DCS_PORT_A			0x00
#define DL_DCS_PORT_C			0x01
#define DL_DCS_PORT_A_AND_C		0x02
	u16 dl_dcs_cabc_ports:2;
	u16 dl_dcs_backlight_ports:2;
	u16 rsvd3:4;

	u16 rsvd4;

	u8 rsvd5;
	u32 target_burst_mode_freq;
	u32 dsi_ddr_clk;
	u32 bridge_ref_clk;

#define  BYTE_CLK_SEL_20MHZ		0
#define  BYTE_CLK_SEL_10MHZ		1
#define  BYTE_CLK_SEL_5MHZ		2
	u8 byte_clk_sel:2;

	u8 rsvd6:6;

	/* DPHY Flags */
	u16 dphy_param_valid:1;
	u16 eot_pkt_disabled:1;
	u16 enable_clk_stop:1;
	u16 rsvd7:13;

	u32 hs_tx_timeout;
	u32 lp_rx_timeout;
	u32 turn_around_timeout;
	u32 device_reset_timer;
	u32 master_init_timer;
	u32 dbi_bw_timer;
	u32 lp_byte_clk_val;

	/*  4 byte Dphy Params */
	u32 prepare_cnt:6;
	u32 rsvd8:2;
	u32 clk_zero_cnt:8;
	u32 trail_cnt:5;
	u32 rsvd9:3;
	u32 exit_zero_cnt:6;
	u32 rsvd10:2;

	u32 clk_lane_switch_cnt;
	u32 hl_switch_cnt;

	u32 rsvd11[6];

	/* timings based on dphy spec */
	u8 tclk_miss;
	u8 tclk_post;
	u8 rsvd12;
	u8 tclk_pre;
	u8 tclk_prepare;
	u8 tclk_settle;
	u8 tclk_term_enable;
	u8 tclk_trail;
	u16 tclk_prepare_clkzero;
	u8 rsvd13;
	u8 td_term_enable;
	u8 teot;
	u8 ths_exit;
	u8 ths_prepare;
	u16 ths_prepare_hszero;
	u8 rsvd14;
	u8 ths_settle;
	u8 ths_skip;
	u8 ths_trail;
	u8 tinit;
	u8 tlpx;
	u8 rsvd15[3];

	/* GPIOs */
	u8 panel_enable;
	u8 bl_enable;
	u8 pwm_enable;
	u8 reset_r_n;
	u8 pwr_down_r;
	u8 stdby_r_n;

} __packed;

/* all delays have a unit of 100us */
struct mipi_pps_data {
	u16 panel_on_delay;
	u16 bl_enable_delay;
	u16 bl_disable_delay;
	u16 panel_off_delay;
	u16 panel_power_cycle_delay;
} __packed;

void intel_bios_init(struct drm_i915_private *dev_priv);
void intel_bios_init_panel_early(struct drm_i915_private *dev_priv,
				 struct intel_panel *panel,
				 const struct intel_bios_encoder_data *devdata);
void intel_bios_init_panel_late(struct drm_i915_private *dev_priv,
				struct intel_panel *panel,
				const struct intel_bios_encoder_data *devdata,
				const struct drm_edid *drm_edid);
void intel_bios_fini_panel(struct intel_panel *panel);
void intel_bios_driver_remove(struct drm_i915_private *dev_priv);
bool intel_bios_is_valid_vbt(const void *buf, size_t size);
bool intel_bios_is_tv_present(struct drm_i915_private *dev_priv);
bool intel_bios_is_lvds_present(struct drm_i915_private *dev_priv, u8 *i2c_pin);
bool intel_bios_is_port_present(struct drm_i915_private *dev_priv, enum port port);
bool intel_bios_is_port_edp(struct drm_i915_private *dev_priv, enum port port);
bool intel_bios_is_dsi_present(struct drm_i915_private *dev_priv, enum port *port);
bool intel_bios_get_dsc_params(struct intel_encoder *encoder,
			       struct intel_crtc_state *crtc_state,
			       int dsc_max_bpc);
bool intel_bios_port_supports_typec_usb(struct drm_i915_private *i915, enum port port);
bool intel_bios_port_supports_tbt(struct drm_i915_private *i915, enum port port);

const struct intel_bios_encoder_data *
intel_bios_encoder_data_lookup(struct drm_i915_private *i915, enum port port);

bool intel_bios_encoder_supports_dvi(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_hdmi(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_dp(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_edp(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_typec_usb(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_tbt(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_dsi(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_supports_dp_dual_mode(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_is_lspcon(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_lane_reversal(const struct intel_bios_encoder_data *devdata);
bool intel_bios_encoder_hpd_invert(const struct intel_bios_encoder_data *devdata);
enum port intel_bios_encoder_port(const struct intel_bios_encoder_data *devdata);
enum aux_ch intel_bios_dp_aux_ch(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_boost_level(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_max_lane_count(const struct intel_bios_encoder_data *devdata);
int intel_bios_dp_max_link_rate(const struct intel_bios_encoder_data *devdata);
bool intel_bios_dp_has_shared_aux_ch(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_boost_level(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_ddc_pin(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_level_shift(const struct intel_bios_encoder_data *devdata);
int intel_bios_hdmi_max_tmds_clock(const struct intel_bios_encoder_data *devdata);

void intel_bios_for_each_encoder(struct drm_i915_private *i915,
				 void (*func)(struct drm_i915_private *i915,
					      const struct intel_bios_encoder_data *devdata));

/* The enums and structures the parser fills (Linux display headers). */

/* from intel_display_limits.h */
enum port {
	PORT_NONE = -1,

	PORT_A = 0,
	PORT_B,
	PORT_C,
	PORT_D,
	PORT_E,
	PORT_F,
	PORT_G,
	PORT_H,
	PORT_I,

	/* tgl+ */
	PORT_TC1 = PORT_D,
	PORT_TC2,
	PORT_TC3,
	PORT_TC4,
	PORT_TC5,
	PORT_TC6,

	/* XE_LPD repositions D/E offsets and bitfields */
	PORT_D_XELPD = PORT_TC5,
	PORT_E_XELPD,

	I915_MAX_PORTS
};


/* from intel_display.h */
enum aux_ch {
	AUX_CH_NONE = -1,

	AUX_CH_A,
	AUX_CH_B,
	AUX_CH_C,
	AUX_CH_D,
	AUX_CH_E, /* ICL+ */
	AUX_CH_F,
	AUX_CH_G,
	AUX_CH_H,
	AUX_CH_I,

	/* tgl+ */
	AUX_CH_USBC1 = AUX_CH_D,
	AUX_CH_USBC2,
	AUX_CH_USBC3,
	AUX_CH_USBC4,
	AUX_CH_USBC5,
	AUX_CH_USBC6,

	/* XE_LPD repositions D/E offsets and bitfields */
	AUX_CH_D_XELPD = AUX_CH_USBC5,
	AUX_CH_E_XELPD,
};


/* from intel_display.h */
enum phy {
	PHY_NONE = -1,

	PHY_A = 0,
	PHY_B,
	PHY_C,
	PHY_D,
	PHY_E,
	PHY_F,
	PHY_G,
	PHY_H,
	PHY_I,

	I915_MAX_PHYS
};


/* from ../soc/intel_pch.h */
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


/* from intel_display_types.h */
enum drrs_type {
	DRRS_TYPE_NONE,
	DRRS_TYPE_STATIC,
	DRRS_TYPE_SEAMLESS,
};


/* from intel_display_types.h */
struct intel_vbt_panel_data {
	struct drm_display_mode *lfp_lvds_vbt_mode; /* if any */
	struct drm_display_mode *sdvo_lvds_vbt_mode; /* if any */

	/* Feature bits */
	int panel_type;
	unsigned int lvds_dither:1;
	unsigned int bios_lvds_val; /* initial [PCH_]LVDS reg val in VBIOS */

	bool vrr;

	u8 seamless_drrs_min_refresh_rate;
	enum drrs_type drrs_type;

	struct {
		int max_link_rate;
		int rate;
		int lanes;
		int preemphasis;
		int vswing;
		int bpp;
		struct edp_power_seq pps;
		u8 drrs_msa_timing_delay;
		bool low_vswing;
		bool initialized;
		bool hobl;
	} edp;

	struct {
		bool enable;
		bool full_link;
		bool require_aux_wakeup;
		int idle_frames;
		int tp1_wakeup_time_us;
		int tp2_tp3_wakeup_time_us;
		int psr2_tp2_tp3_wakeup_time_us;
	} psr;

	struct {
		u16 pwm_freq_hz;
		u16 brightness_precision_bits;
		u16 hdr_dpcd_refresh_timeout;
		bool present;
		bool active_low_pwm;
		u8 min_brightness;	/* min_brightness/255 of max */
		s8 controller;		/* brightness controller number */
		enum intel_backlight_type type;
	} backlight;

	/* MIPI DSI */
	struct {
		u16 panel_id;
		struct mipi_config *config;
		struct mipi_pps_data *pps;
		u16 bl_ports;
		u16 cabc_ports;
		u8 seq_version;
		u32 size;
		u8 *data;
		const u8 *sequence[MIPI_SEQ_MAX];
		u8 *deassert_seq; /* Used by fixup_mipi_sequences() */
		enum drm_panel_orientation orientation;
	} dsi;
};


/* from intel_display_core.h */
struct intel_vbt_data {
	/* bdb version */
	u16 version;

	/* Feature bits */
	unsigned int int_tv_support:1;
	unsigned int int_crt_support:1;
	unsigned int lvds_use_ssc:1;
	unsigned int int_lvds_support:1;
	unsigned int display_clock_mode:1;
	unsigned int fdi_rx_polarity_inverted:1;
	int lvds_ssc_freq;
	enum drm_panel_orientation orientation;

	bool override_afc_startup;
	u8 override_afc_startup_val;

	int crt_ddc_pin;

	struct list_head display_devices;
	struct list_head bdb_blocks;

	struct sdvo_device_mapping {
		u8 initialized;
		u8 dvo_port;
		u8 slave_addr;
		u8 dvo_wiring;
		u8 i2c_pin;
		u8 ddc_pin;
	} sdvo_mappings[2];
};

#endif /* DRIVERS_GPU_I915_INTEL_VBT_H */

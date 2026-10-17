/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The arithmetic of the Gen9 mode set (gen9-mode.c).
 *
 * Everything here works on numbers only and never touches a register, so
 * the host tests run it as the kernel does: the CD clock of a DPLL0 and
 * CDCLK_CTL, the choice of a mode from an EDID, the DP link (rate, lanes,
 * M and N), the DPLL words for a DP rate or an HDMI clock, the DDI buffer
 * translations and signal levels, and the watermarks and DDB of the lit
 * pipes.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_MODE_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_MODE_H

#include "display-internal.h"

#include <stdint.h>

/* The reference clock of the DPLLs and of the CD clock on display 9, in kHz. */
#define I915_SKL_REF_KHZ		24000U

/* The highest TMDS clock an HDMI port of display 9 drives, in kHz. */
#define I915_SKL_HDMI_MAX_KHZ		300000U

/* The DP port clocks (link rate / 10, kHz) the external ports take: RBR, HBR, HBR2. */
#define I915_SKL_DP_RBR_KHZ		162000U
#define I915_SKL_DP_HBR_KHZ		270000U
#define I915_SKL_DP_HBR2_KHZ		540000U

/* The colour depth the mode set drives: 8 bits a channel. */
#define I915_SKL_PIPE_BPP		24U

/* What kind of port an output is. */
#define I915_SKL_OUTPUT_DP		1
#define I915_SKL_OUTPUT_HDMI		2
#define I915_SKL_OUTPUT_EDP		3

/* The part's power class, which chooses the DDI buffer tables (Linux's ULT and ULX subplatforms). */
#define I915_SKL_SKU_H			0
#define I915_SKL_SKU_ULT		1
#define I915_SKL_SKU_ULX		2

/* The most entries a DDI buffer translation table has (DP on port A has 10). */
#define I915_SKL_BUF_TRANS_MAX		10U

/*
 * One DDI buffer translation entry: the two words written to
 * DDI_BUF_TRANS_LO and _HI, and the current boost the entry asks for.
 */
struct i915_skl_buf_trans {
	uint32_t trans1;
	uint32_t trans2;
	uint8_t iboost;
};

/*
 * The DP link of one output: the port clock and the lanes, and the M and
 * N words of the transcoder.
 */
struct i915_skl_link {
	uint32_t port_khz;
	unsigned lanes;
	uint32_t data_m;
	uint32_t data_n;
	uint32_t link_m;
	uint32_t link_n;
	unsigned tu;
};

/*
 * The DPLL words of one output: the DPLL_CTRL1 field (6 bits, shifted to
 * the DPLL by the caller) and, for HDMI, the two configuration words.
 */
struct i915_skl_dpll_words {
	uint32_t ctrl1;
	uint32_t cfgcr1;
	uint32_t cfgcr2;
	uint32_t port_khz;
};

/*
 * One watermark level, or the transition one: whether it is used, its
 * blocks and lines, and the DDB it needs (0xffff: it cannot be used).
 */
struct i915_skl_wm_level {
	int enable;
	uint32_t blocks;
	uint32_t lines;
	uint32_t min_ddb;
};

/*
 * The watermarks and DDB of one lit pipe's first plane (the only plane the
 * driver shows) and of its cursor, which stays off.
 */
struct i915_skl_pipe_wm {
	/* Whether the pipe is lit; the rest is only meaningful when it is. */
	int active;

	/* What the computation takes: the mode's width, total, pixel clock (kHz) and the plane's width. */
	uint32_t hdisplay;
	uint32_t htotal;
	uint32_t pixel_khz;
	uint32_t plane_width;

	/* The pipe's DDB, and the plane's and the cursor's part of it, in blocks (end exclusive). */
	uint32_t ddb_start;
	uint32_t ddb_end;
	uint32_t plane_start;
	uint32_t plane_end;
	uint32_t cursor_start;
	uint32_t cursor_end;

	/* The plane's levels, and the line time (WM_LINETIME, 1/8 us). */
	struct i915_skl_wm_level level[8];
	uint32_t linetime;
};

/*
 * ==== Clocks and modes ====
 */

/* Reports the CD clock in kHz from LCPLL1_CTL, DPLL_CTRL1 and CDCLK_CTL, or zero when DPLL0 is off. */
uint32_t drv_i915_skl_cdclk_khz(uint32_t lcpll1_ctl, uint32_t dpll_ctrl1, uint32_t cdclk_ctl);

/* Picks the mode an output shows from its EDID (NULL for none): 0, or EINVAL when nothing fits. */
int drv_i915_skl_pick_mode(const uint8_t *edid, unsigned edid_bytes, uint32_t max_pixel_khz, struct i915_display_mode *mode);

/*
 * ==== The DP link ====
 */

/* Chooses the slowest DP link that carries the mode: 0, or ENOSPC when none does. */
int drv_i915_skl_dp_link(uint32_t sink_max_khz, unsigned sink_max_lanes, uint32_t pixel_khz, struct i915_skl_link *link);

/* Computes the M and N words of a link for a pixel clock at 24 bits a pixel. */
void drv_i915_skl_dp_m_n(struct i915_skl_link *link, uint32_t pixel_khz);

/* Reports the port clock in kHz of a DPCD MAX_LINK_RATE code, or zero for one not taken. */
uint32_t drv_i915_skl_dpcd_rate_khz(uint8_t code);

/* Reports the DPCD LINK_BW_SET code of a port clock, or zero. */
uint8_t drv_i915_skl_dpcd_rate_code(uint32_t port_khz);

/* Reports the training interval in microseconds for clock recovery (eq = 0) or channel equalisation (eq = 1). */
unsigned drv_i915_skl_dp_train_delay_us(uint8_t dpcd_rev, uint8_t rd_interval, int eq);

/*
 * ==== DPLLs ====
 */

/* Computes the DPLL words of a DP port clock: 0, or EINVAL for a rate the DPLL has no code for. */
int drv_i915_skl_dpll_dp(uint32_t port_khz, struct i915_skl_dpll_words *words);

/* Computes the DPLL words of an HDMI pixel clock (the WRPLL): 0, or EINVAL when no divider fits. */
int drv_i915_skl_dpll_hdmi(uint32_t pixel_khz, uint32_t ref_khz, struct i915_skl_dpll_words *words);

/*
 * ==== DDI buffers and signal levels ====
 */

/* Reports the part's power class from its PCI device id. */
int drv_i915_skl_sku(uint16_t device_id, int platform);

/* Reports the DDI buffer translation table of a part, a kind of output and a port; the entries' count in *count. */
const struct i915_skl_buf_trans *drv_i915_skl_buf_trans(int sku, int platform, int output, unsigned port, unsigned *count);

/* Reports the table entry for a DP lane's training set (voltage swing and pre-emphasis), or 0. */
unsigned drv_i915_skl_signal_level(uint8_t train_set);

/* Reports the highest voltage swing (0..3) a table of so many entries reaches. */
uint8_t drv_i915_skl_voltage_max(unsigned count);

/* Reports the entry an HDMI port uses (the table's default). */
unsigned drv_i915_skl_hdmi_level(int sku, int platform);

/* Turns the sink's adjust requests for the lanes into the next training set. */
uint8_t drv_i915_skl_adjust_train(const uint8_t link_status[6], unsigned lanes, uint8_t voltage_max);

/* Reports nonzero when every lane recovered the clock. */
int drv_i915_skl_dp_cr_ok(const uint8_t link_status[6], unsigned lanes);

/* Reports nonzero when every lane is equalised and the lanes are aligned. */
int drv_i915_skl_dp_eq_ok(const uint8_t link_status[6], unsigned lanes);

/*
 * ==== Watermarks and DDB ====
 */

/* Computes the DDB split and the watermarks of the lit pipes: 0, or ENOSPC when they do not fit. */
int drv_i915_skl_wm_compute(struct i915_skl_pipe_wm pipes[3], const uint16_t latency_us[8]);

/* Encodes a watermark level for PLANE_WM or CUR_WM. */
uint32_t drv_i915_skl_wm_word(const struct i915_skl_wm_level *level);

/* Encodes a DDB entry for PLANE_BUF_CFG or CUR_BUF_CFG (an empty one is 0). */
uint32_t drv_i915_skl_ddb_word(uint32_t start, uint32_t end);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_MODE_H */

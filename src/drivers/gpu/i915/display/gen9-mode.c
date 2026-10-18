/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The arithmetic of the Gen9 mode set (see gen9-mode.h): the CD clock
 * readout, the DP link choice (rates ascending, then lanes), the M and N,
 * the DPLL words, the DDI buffer tables and their choice, the training
 * adjustments, and the watermarks and the DDB.  The register fields are in
 * intel/display-regs-gen9-modeset.h.
 *
 * The watermarks are those of a linear 32-bit plane without scaling or
 * rotation, the only plane the driver shows; IPC is taken as off and the
 * transition watermark is not used (WaDisableTWM on display 9).
 */

#include "gen9-mode.h"
#include "output.h"
#include "xd-clock.h"
#include "xd-dbuf.h"
#include "xd-ddi.h"
#include "xd-dp.h"
#include "xd-platform.h"
#include <kern/kcrt.h>

#include "../device-info.h"
#include "../intel/display-regs-gen9.h"
#include "../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>
#include <stddef.h>

/* The EDID's detailed timing descriptors: the first, their size and count; and a CEA-861 extension's tag. */
#define I915_SKL_EDID_DTD_FIRST		54U
#define I915_SKL_EDID_DTD_BYTES		18U
#define I915_SKL_EDID_DTDS		4U
#define I915_SKL_EDID_CEA_TAG		0x02U

/* The training intervals of a sink without TRAINING_AUX_RD_INTERVAL (drm_dp_helper), and its unit. */
#define I915_SKL_CR_DELAY_US		100U
#define I915_SKL_EQ_DELAY_US		400U
#define I915_SKL_RD_INTERVAL_UNIT_US	4000U

/* The DPCD revision from which clock recovery always waits 100 us. */
#define I915_SKL_DPCD_REV_14		0x14U

/* A safe mode when the sink gives none: VESA DMT 1024x768 at 60 Hz. */
#define I915_SKL_SAFE_CLOCK_KHZ		65000

static int i915_skl_mode_fits(const struct i915_display_mode *mode, uint32_t max_pixel_khz);
static void i915_skl_safe_mode(struct i915_display_mode *mode);

/*
 * Reports the CD clock in kHz from LCPLL1_CTL, DPLL_CTRL1 and CDCLK_CTL,
 * or zero when DPLL0 is off or not locked.
 */
uint32_t
drv_i915_skl_cdclk_khz(
	uint32_t lcpll1_ctl,
	uint32_t dpll_ctrl1,
	uint32_t cdclk_ctl)
{
	uint32_t khz;

	/* The display 9 CD clock of the xd code. */
	khz = drv_i915_xd9_cdclk_khz(lcpll1_ctl, dpll_ctrl1, cdclk_ctl);

	/* Succeeded: the clock, or zero. */
	return khz;
}

/*
 * Picks the mode an output shows from its EDID: the first detailed timing
 * the pipe can drive (the sink's preferred one comes first), from the base
 * block and then a CEA-861 extension; without an EDID or a timing that
 * fits, 1024x768 at 60 Hz.  Returns 0, or EINVAL when even that does not
 * fit.
 */
int
drv_i915_skl_pick_mode(
	const uint8_t *edid,
	unsigned edid_bytes,
	uint32_t max_pixel_khz,
	struct i915_display_mode *mode)
{
	struct i915_display_mode candidate;
	unsigned index;
	unsigned offset;
	unsigned dtd_start;
	int fits;
	int error;

	/* The base block's four descriptors in order. */
	if (edid != NULL && edid_bytes >= I915_EDID_BLOCK_BYTES) {
		for (index = 0U; index < I915_SKL_EDID_DTDS; index++) {
			error = drv_i915_output_edid_timing(edid + I915_SKL_EDID_DTD_FIRST + index * I915_SKL_EDID_DTD_BYTES, &candidate);
			if (error != 0)
				continue;
			fits = i915_skl_mode_fits(&candidate, max_pixel_khz);
			if (fits) {
				*mode = candidate;
				return 0;
			}
		}
	}

	/* A CEA-861 extension's detailed timings, from the offset its third byte gives. */
	if (edid != NULL && edid_bytes >= 2U * I915_EDID_BLOCK_BYTES && edid[I915_EDID_BLOCK_BYTES] == I915_SKL_EDID_CEA_TAG) {
		dtd_start = edid[I915_EDID_BLOCK_BYTES + 2U];
		for (offset = dtd_start;
		     dtd_start >= 4U && offset + I915_SKL_EDID_DTD_BYTES <= I915_EDID_BLOCK_BYTES - 1U;
		     offset += I915_SKL_EDID_DTD_BYTES) {
			error = drv_i915_output_edid_timing(edid + I915_EDID_BLOCK_BYTES + offset, &candidate);
			if (error != 0)
				break;
			fits = i915_skl_mode_fits(&candidate, max_pixel_khz);
			if (fits) {
				*mode = candidate;
				return 0;
			}
		}
	}

	/* The safe mode, when the pipe can drive it. */
	i915_skl_safe_mode(&candidate);
	fits = i915_skl_mode_fits(&candidate, max_pixel_khz);
	if (!fits)
		return EINVAL;

	/* Succeeded: the safe mode. */
	*mode = candidate;
	return 0;
}

/*
 * Chooses the slowest DP link that carries the mode at 24 bits a pixel,
 * with its M and N.  Returns 0, or ENOSPC when no link the sink takes
 * carries the mode.
 */
int
drv_i915_skl_dp_link(
	uint32_t sink_max_khz,
	unsigned sink_max_lanes,
	uint32_t pixel_khz,
	struct i915_skl_link *link)
{
	struct i915_xd_dp_choice choice;
	struct i915_xd_dp_m_n m_n;
	int error;

	/* No link until one carries the mode. */
	kern_memset(link, 0, sizeof(*link));

	/* The display 9 link of the xd code. */
	error = drv_i915_xd9_dp_link(sink_max_khz, sink_max_lanes, pixel_khz, &choice, &m_n);
	if (error != 0)
		return error;

	/* Succeeded: the link and its M and N. */
	link->port_khz = choice.rate_khz;
	link->lanes = choice.lanes;
	link->data_m = m_n.data_m;
	link->data_n = m_n.data_n;
	link->link_m = m_n.link_m;
	link->link_n = m_n.link_n;
	link->tu = m_n.tu;
	return 0;
}

/*
 * Computes the M and N words of a link for a pixel clock at 24 bits a
 * pixel.
 */
void
drv_i915_skl_dp_m_n(
	struct i915_skl_link *link,
	uint32_t pixel_khz)
{
	struct i915_xd_dp_m_n m_n;

	/* The M/N values of the xd code for a single stream (no overhead: one million millionths). */
	drv_i915_xd_dp_m_n(I915_SKL_PIPE_BPP * 16U, link->lanes, pixel_khz, link->port_khz, 1000000U, &m_n);

	/* Succeeded: the words. */
	link->data_m = m_n.data_m;
	link->data_n = m_n.data_n;
	link->link_m = m_n.link_m;
	link->link_n = m_n.link_n;
	link->tu = m_n.tu;
}

/* Reports the port clock in kHz of a DPCD MAX_LINK_RATE code, or zero for one not taken. */
uint32_t
drv_i915_skl_dpcd_rate_khz(
	uint8_t code)
{
	uint32_t khz;

	/* The rate display 9 drives for the code. */
	khz = drv_i915_xd9_dp_sink_rate(code);

	/* Succeeded: the rate, or zero. */
	return khz;
}

/* Reports the DPCD LINK_BW_SET code of a port clock, or zero. */
uint8_t
drv_i915_skl_dpcd_rate_code(
	uint32_t port_khz)
{
	uint8_t code;

	/* The code of a rate display 9 drives. */
	code = drv_i915_xd9_dp_rate_code(port_khz);

	/* Succeeded: the code, or zero. */
	return code;
}

/*
 * Reports the training interval in microseconds: clock recovery waits
 * 100 us when the sink gives no interval or is DP 1.4, channel
 * equalisation 400 us without one; else the interval in 4 ms units.
 */
unsigned
drv_i915_skl_dp_train_delay_us(
	uint8_t dpcd_rev,
	uint8_t rd_interval,
	int eq)
{
	unsigned interval;

	/* The sink's interval, without the extended-format flag. */
	interval = rd_interval & I915_DPCD_TRAINING_AUX_RD_MASK;

	/* Clock recovery. */
	if (!eq) {
		if (interval == 0U || dpcd_rev >= I915_SKL_DPCD_REV_14)
			return I915_SKL_CR_DELAY_US;

		/* The sink's interval. */
		return interval * I915_SKL_RD_INTERVAL_UNIT_US;
	}

	/* Channel equalisation. */
	if (interval == 0U)
		return I915_SKL_EQ_DELAY_US;

	/* Succeeded: the sink's interval. */
	return interval * I915_SKL_RD_INTERVAL_UNIT_US;
}

/*
 * Computes the DPLL words of a DP port clock.  Returns 0, or EINVAL for
 * a rate the DPLL has no code for.
 */
int
drv_i915_skl_dpll_dp(
	uint32_t port_khz,
	struct i915_skl_dpll_words *words)
{
	struct i915_xd9_dpll_words xd;
	int error;

	/* The display 9 DPLL words of the xd code. */
	error = drv_i915_xd9_dpll_dp_words(port_khz, &xd);
	words->ctrl1 = xd.ctrl1;
	words->cfgcr1 = xd.cfgcr1;
	words->cfgcr2 = xd.cfgcr2;
	words->port_khz = xd.port_khz;
	if (error != 0)
		return error;

	/* Succeeded: the DPLL runs the link at the rate. */
	return 0;
}

/*
 * Computes the WRPLL words of an HDMI pixel clock.  Returns 0, or EINVAL
 * when no divider fits or the reference is not display 9's 24 MHz.
 */
int
drv_i915_skl_dpll_hdmi(
	uint32_t pixel_khz,
	uint32_t ref_khz,
	struct i915_skl_dpll_words *words)
{
	struct i915_xd9_dpll_words xd;
	int error;

	/* No words until a divider fits. */
	kern_memset(words, 0, sizeof(*words));

	/* Display 9's DPLLs run from 24 MHz only. */
	if (ref_khz != I915_SKL_REF_KHZ)
		return EINVAL;

	/* The display 9 WRPLL words of the xd code. */
	error = drv_i915_xd9_dpll_hdmi_words(pixel_khz, &xd);
	words->ctrl1 = xd.ctrl1;
	words->cfgcr1 = xd.cfgcr1;
	words->cfgcr2 = xd.cfgcr2;
	words->port_khz = xd.port_khz;
	if (error != 0)
		return error;

	/* Succeeded: the WRPLL makes the clock. */
	return 0;
}

/* Reports the part's power class from its PCI device id (ULT, ULX, or H and S). */
int
drv_i915_skl_sku(
	uint16_t device_id,
	int platform)
{
	struct i915_xd_choice choice;
	int error;

	UNUSED_PARAMETER(platform);

	/* The display 9 row of the id. */
	error = drv_i915_xd_platform_choose_display9(device_id, 0U, 0, 0U, &choice);
	if (error != 0)
		return I915_SKL_SKU_H;

	/* The U parts and the Y parts. */
	if (choice.platform->power_class == I915_XD_POWER_CLASS_U)
		return I915_SKL_SKU_ULT;
	if (choice.platform->power_class == I915_XD_POWER_CLASS_Y)
		return I915_SKL_SKU_ULX;

	/* Succeeded: every other part has the H and S tables. */
	return I915_SKL_SKU_H;
}

/*
 * Reports the DDI buffer translation table of a part, a kind of output
 * and a port; the entries' count in *count.
 */
const struct i915_xd9_buf_trans *
drv_i915_skl_buf_trans(
	int sku,
	int platform,
	int output,
	unsigned port,
	unsigned *count)
{
	const struct i915_xd_platform *row;
	const struct i915_xd9_buf_trans *table;
	unsigned kind;
	unsigned power_class;
	unsigned kind_of_output;

	/* Skylake's tables, or Kaby Lake's, which Coffee Lake and Comet Lake share. */
	kind = I915_XD_KIND_KBL;
	if (platform == I915_PLATFORM_SKYLAKE)
		kind = I915_XD_KIND_SKL;

	/* The power class. */
	power_class = I915_XD_POWER_CLASS_H;
	if (sku == I915_SKL_SKU_ULT) {
		power_class = I915_XD_POWER_CLASS_U;
	} else if (sku == I915_SKL_SKU_ULX) {
		power_class = I915_XD_POWER_CLASS_Y;
	}

	/* The kind of output. */
	kind_of_output = I915_XD9_OUTPUT_DP;
	if (output == I915_SKL_OUTPUT_HDMI) {
		kind_of_output = I915_XD9_OUTPUT_HDMI;
	} else if (output == I915_SKL_OUTPUT_EDP) {
		kind_of_output = I915_XD9_OUTPUT_EDP;
	}

	/* The row of the kind and the class. */
	*count = 0U;
	row = drv_i915_xd_platform_display9_class(kind, power_class);
	if (row == NULL)
		return NULL;

	/* The xd code's table of the row. */
	table = drv_i915_xd9_ddi_buf_table(row, kind_of_output, port, count);

	/* Succeeded: the table and its usable entries. */
	return table;
}

/* Reports the table entry for a DP lane's training set, or 0. */
unsigned
drv_i915_skl_signal_level(
	uint8_t train_set)
{
	unsigned row;
	int error;

	/* The row of the xd code for the set's swing and pre-emphasis. */
	error = drv_i915_xd_combo_level_row(train_set, &row);
	if (error != 0)
		return 0U;

	/* Succeeded: the entry. */
	return row;
}

/* Reports the highest voltage swing a table of so many entries reaches. */
uint8_t
drv_i915_skl_voltage_max(
	unsigned count)
{
	uint8_t swing;

	/* The highest swing of the xd code. */
	swing = drv_i915_xd_dp_swing_max(count);

	/* Succeeded: the swing. */
	return swing;
}

/* Reports the entry an HDMI port uses: the tables' default, without a VBT level shift. */
unsigned
drv_i915_skl_hdmi_level(
	int sku,
	int platform)
{
	const struct i915_xd_platform *row;
	unsigned entry;

	UNUSED_PARAMETER(sku);

	/* The platform's H and S row (every display 9 HDMI table has the same default). */
	row = drv_i915_xd_platform_display9_class(I915_XD_KIND_KBL, I915_XD_POWER_CLASS_H);
	if (platform == I915_PLATFORM_SKYLAKE)
		row = drv_i915_xd_platform_display9_class(I915_XD_KIND_SKL, I915_XD_POWER_CLASS_H);

	/* The xd code's default row. */
	entry = drv_i915_xd9_ddi_hdmi_row(row);

	/* Succeeded: the entry. */
	return entry;
}

/*
 * Turns the sink's adjust requests into the next training set
 * (intel_dp_get_lane_adjust_vswing_preemph without per-lane levels): the
 * highest swing and pre-emphasis any lane asks for, the pre-emphasis
 * capped at level 3 and the swing at what the pre-emphasis allows and the
 * table reaches, with the reached flags.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c (the training
 * adjustments).
 */
uint8_t
drv_i915_skl_adjust_train(
	const uint8_t link_status[6],
	unsigned lanes,
	uint8_t voltage_max)
{
	unsigned lane;
	uint8_t request;
	uint8_t v;
	uint8_t p;
	uint8_t lane_v;
	uint8_t lane_p;
	uint8_t v_by_p;

	/* The highest request of the lanes (ADJUST_REQUEST_LANE0_1 and _2_3, a nibble a lane). */
	v = 0U;
	p = 0U;
	for (lane = 0U; lane < lanes && lane < 4U; lane++) {
		request = link_status[4U + lane / 2U];
		request = (uint8_t)(request >> (4U * (lane & 1U)));
		lane_v = (uint8_t)(request & 0x3U);
		lane_p = (uint8_t)((request >> 2) & 0x3U);
		if (lane_v > v)
			v = lane_v;
		if (lane_p > p)
			p = lane_p;
	}

	/* The pre-emphasis at its ceiling is marked reached. */
	if (p >= 3U)
		p = (uint8_t)(3U | (I915_DPCD_TRAIN_MAX_PRE_EMPHASIS_REACHED >> I915_DPCD_TRAIN_PRE_EMPHASIS_SHIFT));

	/* The swing the pre-emphasis leaves room for (dp_voltage_max): 3 - pre-emphasis. */
	v_by_p = (uint8_t)(3U - (p & 0x3U));
	if (v > v_by_p)
		v = v_by_p;

	/* The swing at the table's ceiling is marked reached. */
	if (v >= voltage_max)
		v = (uint8_t)(voltage_max | I915_DPCD_TRAIN_MAX_SWING_REACHED);

	/* Succeeded: the pre-emphasis in 5:3 and the swing in 2:0. */
	return (uint8_t)(v | (uint8_t)(p << I915_DPCD_TRAIN_PRE_EMPHASIS_SHIFT));
}

/* Reports nonzero when every lane recovered the clock (drm_dp_clock_recovery_ok). */
int
drv_i915_skl_dp_cr_ok(
	const uint8_t link_status[6],
	unsigned lanes)
{
	unsigned lane;
	uint8_t status;

	/* Each lane's nibble of LANE0_1_STATUS and LANE2_3_STATUS. */
	for (lane = 0U; lane < lanes; lane++) {
		status = (uint8_t)(link_status[lane / 2U] >> (4U * (lane & 1U)));
		if ((status & I915_DPCD_LANE_CR_DONE) == 0U)
			return 0;
	}

	/* Succeeded: every lane has the clock. */
	return 1;
}

/* Reports nonzero when every lane is equalised and locked and the lanes are aligned (drm_dp_channel_eq_ok). */
int
drv_i915_skl_dp_eq_ok(
	const uint8_t link_status[6],
	unsigned lanes)
{
	unsigned lane;
	uint8_t status;
	uint8_t wanted;

	/* The lanes are aligned (LANE_ALIGN_STATUS_UPDATED). */
	if ((link_status[2] & I915_DPCD_INTERLANE_ALIGN_DONE) == 0U)
		return 0;

	/* Each lane recovered the clock, equalised and locked its symbols. */
	wanted = (uint8_t)(I915_DPCD_LANE_CR_DONE | I915_DPCD_LANE_CHANNEL_EQ_DONE | I915_DPCD_LANE_SYMBOL_LOCKED);
	for (lane = 0U; lane < lanes; lane++) {
		status = (uint8_t)(link_status[lane / 2U] >> (4U * (lane & 1U)));
		if ((status & wanted) != wanted)
			return 0;
	}

	/* Succeeded: the link is trained. */
	return 1;
}

/*
 * Computes the DDB split and the watermarks of the lit pipes.  Returns 0,
 * or ENOSPC when even level 0 does not fit a pipe's share; the pipes
 * reached until then keep what they were given.
 */
int
drv_i915_skl_wm_compute(
	struct i915_skl_pipe_wm pipes[3],
	const uint16_t latency_us[8])
{
	struct i915_xd9_pipe_plan plan[3];
	const struct i915_xd_platform *platform;
	unsigned pipe;
	unsigned level;
	int error;

	/* What the plan takes of each pipe. */
	kern_memset(plan, 0, sizeof(plan));
	for (pipe = 0U; pipe < 3U; pipe++) {
		plan[pipe].active = pipes[pipe].active;
		plan[pipe].hdisplay = pipes[pipe].hdisplay;
		plan[pipe].htotal = pipes[pipe].htotal;
		plan[pipe].pixel_khz = pipes[pipe].pixel_khz;
		plan[pipe].plane_width = pipes[pipe].plane_width;
	}

	/* The xd code's plan on display 9 (every display 9 part has the same display buffer). */
	platform = drv_i915_xd_platform_display9_class(I915_XD_KIND_KBL, I915_XD_POWER_CLASS_H);
	error = drv_i915_xd9_dbuf_plan(platform, latency_us, plan, 3U);

	/* What the plan gave each pipe it reached. */
	for (pipe = 0U; pipe < 3U; pipe++) {
		if (!plan[pipe].computed)
			continue;
		pipes[pipe].ddb_start = plan[pipe].pipe_ddb.start;
		pipes[pipe].ddb_end = plan[pipe].pipe_ddb.end;
		pipes[pipe].plane_start = plan[pipe].plane_ddb.start;
		pipes[pipe].plane_end = plan[pipe].plane_ddb.end;
		pipes[pipe].cursor_start = plan[pipe].cursor_ddb.start;
		pipes[pipe].cursor_end = plan[pipe].cursor_ddb.end;
		pipes[pipe].linetime = plan[pipe].linetime;
		for (level = 0U; level < 8U; level++) {
			pipes[pipe].level[level].enable = plan[pipe].wm.level[level].enable;
			pipes[pipe].level[level].blocks = plan[pipe].wm.level[level].blocks;
			pipes[pipe].level[level].lines = plan[pipe].wm.level[level].lines;
			pipes[pipe].level[level].min_ddb = plan[pipe].wm.level[level].min_ddb;
		}
	}

	/* A pipe that does not fit. */
	if (error != 0)
		return error;

	/* Succeeded: every lit pipe fits. */
	return 0;
}

/* Encodes a watermark level for PLANE_WM or CUR_WM (skl_write_wm_level). */
uint32_t
drv_i915_skl_wm_word(
	const struct i915_skl_wm_level *level)
{
	uint32_t word;

	/* The enable, the lines and the blocks. */
	word = 0U;
	if (level->enable)
		word |= I915_SKL_WM_ENABLE;
	word |= (level->lines & 0x1fffU) << I915_SKL_WM_LINES_SHIFT;
	word |= level->blocks & I915_SKL_WM_BLOCKS_MASK;

	/* Succeeded: the word. */
	return word;
}

/* Encodes a DDB entry (skl_ddb_entry_write): end - 1 in 27:16 and start in 11:0, or 0 when empty. */
uint32_t
drv_i915_skl_ddb_word(
	uint32_t start,
	uint32_t end)
{
	/* An empty entry is 0. */
	if (end == 0U || end <= start)
		return 0U;

	/* Succeeded: the entry. */
	return ((end - 1U) << I915_SKL_BUF_END_SHIFT) | start;
}

/* Reports nonzero when a mode is progressive, at most 4096x4096 and within the pixel clock. */
static int
i915_skl_mode_fits(
	const struct i915_display_mode *mode,
	uint32_t max_pixel_khz)
{
	/* A clock above the pipe's limit or no clock at all. */
	if (mode->clock_khz <= 0)
		return 0;
	if ((uint32_t)mode->clock_khz > max_pixel_khz)
		return 0;

	/* A size the plane and the buffers take. */
	if (mode->hdisplay == 0U || mode->vdisplay == 0U)
		return 0;
	if (mode->hdisplay > 4096U || mode->vdisplay > 4096U)
		return 0;

	/* Succeeded: the pipe drives it. */
	return 1;
}

/* Fills the safe mode: VESA DMT 1024x768 at 60 Hz, negative syncs. */
static void
i915_skl_safe_mode(
	struct i915_display_mode *mode)
{
	kern_memset(mode, 0, sizeof(*mode));

	/* The timing. */
	mode->clock_khz = I915_SKL_SAFE_CLOCK_KHZ;
	mode->hdisplay = 1024U;
	mode->hsync_start = 1048U;
	mode->hsync_end = 1184U;
	mode->htotal = 1344U;
	mode->vdisplay = 768U;
	mode->vsync_start = 771U;
	mode->vsync_end = 777U;
	mode->vtotal = 806U;
	mode->edid_bpc = 8;
}

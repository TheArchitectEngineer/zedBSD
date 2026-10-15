/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The arithmetic of the Gen9 mode set (WS237 stage 3, see gen9-mode.h).
 *
 * The rules are the Linux kernel 6.8.12's for display 9 (MIT): the CD
 * clock readout of skl_get_cdclk(), the DP link choice of
 * intel_dp_compute_link_config_wide() (rates ascending, then lanes), the
 * M and N of intel_link_compute_m_n(), the DPLL words of
 * skl_ddi_dp_set_dpll_hw_state() and skl_ddi_calculate_wrpll(), the
 * buffer tables of intel_ddi_buf_trans.c and their choice by
 * intel_ddi_buf_trans_init(), the training adjustments of
 * intel_dp_link_training.c, and the watermarks and DDB of
 * skl_watermark.c (skl_compute_wm_params(), skl_compute_plane_wm(),
 * skl_cursor_allocation(), skl_crtc_allocate_plane_ddb(), the pipes'
 * split by width).  The code is this file's own; the table rows and the
 * register fields are in intel/display-regs-gen9-modeset.h.
 *
 * The watermarks are those of a linear 32-bit plane without scaling or
 * rotation, the only plane the driver shows; IPC is taken as off and the
 * transition watermark is not used (WaDisableTWM on display 9).
 */

#include "gen9-mode.h"
#include "output.h"
#include <kern/kcrt.h>

#include "../device-info.h"
#include "../intel/display-regs-gen9.h"
#include "../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>
#include <stddef.h>

/* A fixed-point number with 16 fraction bits (Linux's uint_fixed_16_16_t). */
#define I915_SKL_FIXED_SHIFT		16U

/* A watermark level that cannot be used asks for this much DDB. */
#define I915_SKL_WM_REJECT		0xffffU

/* The DBUF block size and the bytes a pixel of the shown plane takes. */
#define I915_SKL_DBUF_BLOCK_BYTES	512U
#define I915_SKL_PLANE_CPP		4U

/* The cursor's width the DDB is reserved for (skl_cursor_allocation), and its least reservation with one pipe or more. */
#define I915_SKL_CURSOR_WIDTH		256U
#define I915_SKL_CURSOR_MIN_ONE		32U
#define I915_SKL_CURSOR_MIN_MANY	8U

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

/* The WRPLL's DCO deviations it accepts above and below a central frequency, in 1/10000. */
#define I915_SKL_DCO_MAX_PDEVIATION	100U
#define I915_SKL_DCO_MAX_NDEVIATION	600U

/* A safe mode when the sink gives none: VESA DMT 1024x768 at 60 Hz. */
#define I915_SKL_SAFE_CLOCK_KHZ		65000

/*
 * The DDI buffer translation tables of display 9 (intel_ddi_buf_trans.c):
 * the DP ones of Skylake and Kaby Lake by power class, the eDP low swing
 * ones, and the HDMI ones.
 */
static const struct i915_skl_buf_trans i915_skl_trans_dp[] = {
	{ 0x00002016U, 0x000000a0U, 0x0U },
	{ 0x00005012U, 0x0000009bU, 0x0U },
	{ 0x00007011U, 0x00000088U, 0x0U },
	{ 0x80009010U, 0x000000c0U, 0x1U },
	{ 0x00002016U, 0x0000009bU, 0x0U },
	{ 0x00005012U, 0x00000088U, 0x0U },
	{ 0x80007011U, 0x000000c0U, 0x1U },
	{ 0x00002016U, 0x000000dfU, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x1U }
};

/* Skylake U, DP. */
static const struct i915_skl_buf_trans i915_skl_u_trans_dp[] = {
	{ 0x0000201bU, 0x000000a2U, 0x0U },
	{ 0x00005012U, 0x00000088U, 0x0U },
	{ 0x80007011U, 0x000000cdU, 0x1U },
	{ 0x80009010U, 0x000000c0U, 0x1U },
	{ 0x0000201bU, 0x0000009dU, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x1U },
	{ 0x80007011U, 0x000000c0U, 0x1U },
	{ 0x00002016U, 0x00000088U, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x1U }
};

/* Skylake Y, DP. */
static const struct i915_skl_buf_trans i915_skl_y_trans_dp[] = {
	{ 0x00000018U, 0x000000a2U, 0x0U },
	{ 0x00005012U, 0x00000088U, 0x0U },
	{ 0x80007011U, 0x000000cdU, 0x3U },
	{ 0x80009010U, 0x000000c0U, 0x3U },
	{ 0x00000018U, 0x0000009dU, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x3U },
	{ 0x80007011U, 0x000000c0U, 0x3U },
	{ 0x00000018U, 0x00000088U, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x3U }
};

/* Kaby Lake H and S (and Coffee Lake, Comet Lake), DP. */
static const struct i915_skl_buf_trans i915_kbl_trans_dp[] = {
	{ 0x00002016U, 0x000000a0U, 0x0U },
	{ 0x00005012U, 0x0000009bU, 0x0U },
	{ 0x00007011U, 0x00000088U, 0x0U },
	{ 0x80009010U, 0x000000c0U, 0x1U },
	{ 0x00002016U, 0x0000009bU, 0x0U },
	{ 0x00005012U, 0x00000088U, 0x0U },
	{ 0x80007011U, 0x000000c0U, 0x1U },
	{ 0x00002016U, 0x00000097U, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x1U }
};

/* Kaby Lake U, DP. */
static const struct i915_skl_buf_trans i915_kbl_u_trans_dp[] = {
	{ 0x0000201bU, 0x000000a1U, 0x0U },
	{ 0x00005012U, 0x00000088U, 0x0U },
	{ 0x80007011U, 0x000000cdU, 0x3U },
	{ 0x80009010U, 0x000000c0U, 0x3U },
	{ 0x0000201bU, 0x0000009dU, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x3U },
	{ 0x80007011U, 0x000000c0U, 0x3U },
	{ 0x00002016U, 0x0000004fU, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x3U }
};

/* Kaby Lake Y, DP. */
static const struct i915_skl_buf_trans i915_kbl_y_trans_dp[] = {
	{ 0x00001017U, 0x000000a1U, 0x0U },
	{ 0x00005012U, 0x00000088U, 0x0U },
	{ 0x80007011U, 0x000000cdU, 0x3U },
	{ 0x8000800fU, 0x000000c0U, 0x3U },
	{ 0x00001017U, 0x0000009dU, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x3U },
	{ 0x80007011U, 0x000000c0U, 0x3U },
	{ 0x00001017U, 0x0000004cU, 0x0U },
	{ 0x80005012U, 0x000000c0U, 0x3U }
};

/* Skylake and Kaby Lake H and S, eDP low vswing. */
static const struct i915_skl_buf_trans i915_skl_trans_edp[] = {
	{ 0x00000018U, 0x000000a8U, 0x0U },
	{ 0x00004013U, 0x000000a9U, 0x0U },
	{ 0x00007011U, 0x000000a2U, 0x0U },
	{ 0x00009010U, 0x0000009cU, 0x0U },
	{ 0x00000018U, 0x000000a9U, 0x0U },
	{ 0x00006013U, 0x000000a2U, 0x0U },
	{ 0x00007011U, 0x000000a6U, 0x0U },
	{ 0x00000018U, 0x000000abU, 0x0U },
	{ 0x00007013U, 0x0000009fU, 0x0U },
	{ 0x00000018U, 0x000000dfU, 0x0U }
};

/* Skylake and Kaby Lake U, eDP low vswing. */
static const struct i915_skl_buf_trans i915_skl_u_trans_edp[] = {
	{ 0x00000018U, 0x000000a8U, 0x0U },
	{ 0x00004013U, 0x000000a9U, 0x0U },
	{ 0x00007011U, 0x000000a2U, 0x0U },
	{ 0x00009010U, 0x0000009cU, 0x0U },
	{ 0x00000018U, 0x000000a9U, 0x0U },
	{ 0x00006013U, 0x000000a2U, 0x0U },
	{ 0x00007011U, 0x000000a6U, 0x0U },
	{ 0x00002016U, 0x000000abU, 0x0U },
	{ 0x00005013U, 0x0000009fU, 0x0U },
	{ 0x00000018U, 0x000000dfU, 0x0U }
};

/* Skylake and Kaby Lake Y, eDP low vswing. */
static const struct i915_skl_buf_trans i915_skl_y_trans_edp[] = {
	{ 0x00000018U, 0x000000a8U, 0x0U },
	{ 0x00004013U, 0x000000abU, 0x0U },
	{ 0x00007011U, 0x000000a4U, 0x0U },
	{ 0x00009010U, 0x000000dfU, 0x0U },
	{ 0x00000018U, 0x000000aaU, 0x0U },
	{ 0x00006013U, 0x000000a4U, 0x0U },
	{ 0x00007011U, 0x0000009dU, 0x0U },
	{ 0x00000018U, 0x000000a0U, 0x0U },
	{ 0x00006012U, 0x000000dfU, 0x0U },
	{ 0x00000018U, 0x0000008aU, 0x0U }
};

/* Skylake and Kaby Lake H, S and U, HDMI (default entry 8). */
static const struct i915_skl_buf_trans i915_skl_trans_hdmi[] = {
	{ 0x00000018U, 0x000000acU, 0x0U },
	{ 0x00005012U, 0x0000009dU, 0x0U },
	{ 0x00007011U, 0x00000088U, 0x0U },
	{ 0x00000018U, 0x000000a1U, 0x0U },
	{ 0x00000018U, 0x00000098U, 0x0U },
	{ 0x00004013U, 0x00000088U, 0x0U },
	{ 0x80006012U, 0x000000cdU, 0x1U },
	{ 0x00000018U, 0x000000dfU, 0x0U },
	{ 0x80003015U, 0x000000cdU, 0x1U },
	{ 0x80003015U, 0x000000c0U, 0x1U },
	{ 0x80000018U, 0x000000c0U, 0x1U }
};

/* Skylake and Kaby Lake Y, HDMI (default entry 8). */
static const struct i915_skl_buf_trans i915_skl_y_trans_hdmi[] = {
	{ 0x00000018U, 0x000000a1U, 0x0U },
	{ 0x00005012U, 0x000000dfU, 0x0U },
	{ 0x80007011U, 0x000000cbU, 0x3U },
	{ 0x00000018U, 0x000000a4U, 0x0U },
	{ 0x00000018U, 0x0000009dU, 0x0U },
	{ 0x00004013U, 0x00000080U, 0x0U },
	{ 0x80006013U, 0x000000c0U, 0x3U },
	{ 0x00000018U, 0x0000008aU, 0x0U },
	{ 0x80003015U, 0x000000c0U, 0x3U },
	{ 0x80003015U, 0x000000c0U, 0x3U },
	{ 0x80000018U, 0x000000c0U, 0x3U }
};

/* The HDMI tables' default entry (hdmi_default_entry). */
#define I915_SKL_HDMI_DEFAULT_ENTRY	8U

/*
 * The training set each buffer table entry stands for
 * (index_to_dp_signal_levels): voltage swing in 1:0, pre-emphasis in 4:3.
 */
static const uint8_t i915_skl_signal_levels[10] = {
	0x00U, 0x08U, 0x10U, 0x18U,
	0x01U, 0x09U, 0x11U,
	0x02U, 0x0aU,
	0x03U
};

/* The ULT and ULX parts of Skylake, Kaby Lake, Coffee Lake and Comet Lake (subplatform_ult_ids, _ulx_ids). */
static const uint16_t i915_skl_ult_ids[] = {
	0x1906U, 0x1913U, 0x1916U, 0x1921U, 0x1923U, 0x1926U, 0x1927U,
	0x5906U, 0x5913U, 0x5916U, 0x5921U, 0x5926U,
	0x3ea9U, 0x3ea5U, 0x3ea6U, 0x3ea7U, 0x3ea8U,
	0x3ea1U, 0x3ea4U, 0x3ea0U, 0x3ea3U, 0x3ea2U,
	0x9b21U, 0x9baaU, 0x9bacU, 0x9b41U, 0x9bcaU, 0x9bccU
};

/* ULX parts. */
static const uint16_t i915_skl_ulx_ids[] = {
	0x190eU, 0x1915U, 0x191eU,
	0x590eU, 0x5915U, 0x591eU, 0x591cU, 0x87c0U,
	0x87caU
};

static uint32_t i915_skl_fixed_div(uint32_t value, uint32_t divisor);
static uint32_t i915_skl_fixed_mul(uint32_t value, uint32_t fixed);
static uint32_t i915_skl_fixed_round_up(uint32_t fixed);
static void i915_skl_plane_wm(uint32_t width, uint32_t htotal, uint32_t pixel_khz, const uint16_t latency_us[8], struct i915_skl_wm_level levels[8]);
static int i915_skl_mode_fits(const struct i915_display_mode *mode, uint32_t max_pixel_khz);
static void i915_skl_safe_mode(struct i915_display_mode *mode);
static unsigned i915_skl_id_in(uint16_t device_id, const uint16_t *ids, unsigned count);
static uint32_t i915_skl_reduce(uint32_t *m, uint32_t *n);
static void i915_skl_m_n(uint32_t m, uint32_t n, uint32_t constant_n, uint32_t *out_m, uint32_t *out_n);

/*
 * Reports the CD clock in kHz from LCPLL1_CTL, DPLL_CTRL1 and CDCLK_CTL
 * (skl_get_cdclk(), skl_dpll0_update()), or zero when DPLL0 is off or its
 * rate is not one the CD clock runs on.
 */
uint32_t
drv_i915_skl_cdclk_khz(
	uint32_t lcpll1_ctl,
	uint32_t dpll_ctrl1,
	uint32_t cdclk_ctl)
{
	uint32_t rate;
	uint32_t select;
	int vco_8640;

	/* DPLL0 must run and be locked. */
	if ((lcpll1_ctl & I915_SKL_DPLL_ENABLE) == 0U)
		return 0U;
	if ((lcpll1_ctl & (1U << 30)) == 0U)
		return 0U;

	/* DPLL0's link rate tells its VCO: 8640 MHz for 1080 and 2160, else 8100 MHz. */
	rate = (dpll_ctrl1 >> I915_SKL_DPLL_CTRL1_RATE_SHIFT) & I915_SKL_DPLL_CTRL1_RATE_MASK;
	vco_8640 = 0;
	if (rate == I915_SKL_LINK_RATE_1080 || rate == I915_SKL_LINK_RATE_2160)
		vco_8640 = 1;

	/* CDCLK_CTL's frequency select, bits 27:26. */
	select = (cdclk_ctl >> 26) & 3U;

	/* The frequency each select names on the VCO. */
	switch (select) {
	case 0U:
		return vco_8640 ? 432000U : 450000U;
	case 1U:
		return 540000U;
	case 2U:
		return vco_8640 ? 308571U : 337500U;
	default:
		break;
	}

	/* Succeeded: the highest select. */
	return vco_8640 ? 617143U : 675000U;
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
 * Chooses the slowest DP link that carries the mode at 24 bits a pixel:
 * the rates from RBR up to the sink's (HBR2 at most), and for each the
 * lane counts from 1 up to the sink's.  Fills the M and N too.  Returns
 * 0, or ENOSPC when no link the sink takes carries the mode.
 */
int
drv_i915_skl_dp_link(
	uint32_t sink_max_khz,
	unsigned sink_max_lanes,
	uint32_t pixel_khz,
	struct i915_skl_link *link)
{
	static const uint32_t rates[3] = { I915_SKL_DP_RBR_KHZ, I915_SKL_DP_HBR_KHZ, I915_SKL_DP_HBR2_KHZ };
	uint64_t needed;
	uint64_t available;
	unsigned rate;
	unsigned lanes;

	/* No link until one carries the mode. */
	kern_memset(link, 0, sizeof(*link));

	/* The data the mode needs in kB/s (intel_dp_link_required at 24 bpp). */
	needed = ((uint64_t)pixel_khz * I915_SKL_PIPE_BPP + 7U) / 8U;

	/* Each rate up to the sink's, and each lane count up to the sink's. */
	for (rate = 0U; rate < 3U; rate++) {
		if (rates[rate] > sink_max_khz)
			break;

		/* Each lane count up to the sink's. */
		for (lanes = 1U; lanes <= sink_max_lanes && lanes <= 4U; lanes <<= 1) {
			/* The data a link carries in kB/s (intel_dp_max_data_rate, 8b/10b): port clock times lanes. */
			available = (uint64_t)rates[rate] * lanes;
			if (needed > available)
				continue;

			/* The first link that carries it, and its M and N. */
			link->port_khz = rates[rate];
			link->lanes = lanes;
			drv_i915_skl_dp_m_n(link, pixel_khz);
			return 0;
		}
	}

	/* Succeeded in finding that nothing carries the mode. */
	return ENOSPC;
}

/*
 * Computes the M and N words of a link (intel_link_compute_m_n): data M
 * and N over a fixed N of 0x8000000, link M and N over 0x80000, each
 * halved until it fits 24 bits, and a transfer unit of 64.
 */
void
drv_i915_skl_dp_m_n(
	struct i915_skl_link *link,
	uint32_t pixel_khz)
{
	uint32_t data_m;
	uint32_t data_n;

	/* The data rate the mode needs and the one the link carries, in kB/s. */
	data_m = (uint32_t)(((uint64_t)pixel_khz * I915_SKL_PIPE_BPP + 7U) / 8U);
	data_n = link->port_khz * link->lanes;

	/* The ratios. */
	link->tu = 64U;
	i915_skl_m_n(data_m, data_n, 0x8000000U, &link->data_m, &link->data_n);
	i915_skl_m_n(pixel_khz, link->port_khz, 0x80000U, &link->link_m, &link->link_n);
}

/* Reports the port clock in kHz of a DPCD MAX_LINK_RATE code, or zero for one not taken. */
uint32_t
drv_i915_skl_dpcd_rate_khz(
	uint8_t code)
{
	/* The three rates of DP 1.2. */
	if (code == I915_DPCD_LINK_BW_1_62)
		return I915_SKL_DP_RBR_KHZ;
	if (code == I915_DPCD_LINK_BW_2_7)
		return I915_SKL_DP_HBR_KHZ;
	if (code >= I915_DPCD_LINK_BW_5_4)
		return I915_SKL_DP_HBR2_KHZ;

	/* Succeeded: a code below RBR names no rate. */
	return 0U;
}

/* Reports the DPCD LINK_BW_SET code of a port clock, or zero. */
uint8_t
drv_i915_skl_dpcd_rate_code(
	uint32_t port_khz)
{
	/* The three rates. */
	if (port_khz == I915_SKL_DP_RBR_KHZ)
		return I915_DPCD_LINK_BW_1_62;
	if (port_khz == I915_SKL_DP_HBR_KHZ)
		return I915_DPCD_LINK_BW_2_7;
	if (port_khz == I915_SKL_DP_HBR2_KHZ)
		return I915_DPCD_LINK_BW_5_4;

	/* Succeeded: no code. */
	return 0U;
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
 * Computes the DPLL words of a DP port clock: the override and the link
 * rate code of half the port clock (skl_ddi_dp_set_dpll_hw_state).
 * Returns 0, or EINVAL for a rate the DPLL has no code for.
 */
int
drv_i915_skl_dpll_dp(
	uint32_t port_khz,
	struct i915_skl_dpll_words *words)
{
	uint32_t code;

	/* No words until the rate has a code. */
	kern_memset(words, 0, sizeof(*words));

	/* The code of the link clock, half the port clock. */
	switch (port_khz / 2U) {
	case 81000U:
		code = I915_SKL_LINK_RATE_810;
		break;
	case 135000U:
		code = I915_SKL_LINK_RATE_1350;
		break;
	case 270000U:
		code = I915_SKL_LINK_RATE_2700;
		break;
	case 162000U:
		code = I915_SKL_LINK_RATE_1620;
		break;
	case 108000U:
		code = I915_SKL_LINK_RATE_1080;
		break;
	case 216000U:
		code = I915_SKL_LINK_RATE_2160;
		break;
	default:
		return EINVAL;
	}

	/* The override and the code; no configuration words for DP. */
	words->ctrl1 = 1U | (code << I915_SKL_DPLL_CTRL1_RATE_SHIFT);
	words->port_khz = port_khz;

	/* Succeeded: the DPLL runs the link at the rate. */
	return 0;
}

/*
 * Computes the WRPLL words of an HDMI pixel clock (skl_ddi_calculate_wrpll):
 * the AFE clock is five times the pixel clock; the divider whose DCO
 * frequency deviates least from one of the three central frequencies
 * within +1 % and -6 % is taken, an even one before an odd one, and split
 * into P, Q and K.  Returns 0, or EINVAL when no divider fits.
 */
int
drv_i915_skl_dpll_hdmi(
	uint32_t pixel_khz,
	uint32_t ref_khz,
	struct i915_skl_dpll_words *words)
{
	static const uint64_t central[3] = { 8400000000ULL, 9000000000ULL, 9600000000ULL };
	static const uint8_t even[] = {
		4, 6, 8, 10, 12, 14, 16, 18, 20, 24, 28, 30, 32, 36, 40, 42, 44,
		48, 52, 54, 56, 60, 64, 66, 68, 70, 72, 76, 78, 80, 84, 88, 90,
		92, 96, 98
	};
	static const uint8_t odd[] = { 3, 5, 7, 9, 15, 21, 35 };
	const uint8_t *list;
	uint64_t afe;
	uint64_t dco;
	uint64_t deviation;
	uint64_t best_deviation;
	uint64_t best_central;
	uint64_t dco_integer;
	uint64_t dco_fraction;
	unsigned list_count;
	unsigned best_p;
	unsigned set;
	unsigned c;
	unsigned i;
	unsigned p0;
	unsigned p1;
	unsigned p2;
	unsigned half;
	unsigned pdiv;
	unsigned kdiv;
	unsigned central_code;
	unsigned qdiv_mode;

	/* No words until a divider fits. */
	kern_memset(words, 0, sizeof(*words));

	/* The AFE clock in Hz. */
	afe = (uint64_t)pixel_khz * 1000U * 5U;

	/* The dividers, even then odd, against each central frequency. */
	best_deviation = ~(uint64_t)0;
	best_central = 0U;
	best_p = 0U;
	for (set = 0U; set < 2U; set++) {
		list = even;
		list_count = (unsigned)sizeof(even);
		if (set == 1U) {
			list = odd;
			list_count = (unsigned)sizeof(odd);
		}

		/* Each central frequency, until a deviation of zero. */
		for (c = 0U; c < 3U && best_deviation != 0U; c++) {
			for (i = 0U; i < list_count; i++) {
				/* The DCO frequency and its deviation from the centre, in 1/10000. */
				dco = (uint64_t)list[i] * afe;
				if (dco >= central[c]) {
					deviation = (10000U * (dco - central[c])) / central[c];
					if (deviation >= I915_SKL_DCO_MAX_PDEVIATION || deviation >= best_deviation)
						continue;
				} else {
					deviation = (10000U * (central[c] - dco)) / central[c];
					if (deviation >= I915_SKL_DCO_MAX_NDEVIATION || deviation >= best_deviation)
						continue;
				}

				/* The best divider so far. */
				best_deviation = deviation;
				best_central = central[c];
				best_p = list[i];
				if (best_deviation == 0U)
					break;
			}
		}

		/* An even divider is preferred. */
		if (set == 0U && best_p != 0U)
			break;
	}

	/* No divider keeps the DCO in range. */
	if (best_p == 0U)
		return EINVAL;

	/* P0, P1 and P2 of the divider (skl_wrpll_get_multipliers). */
	p0 = 0U;
	p1 = 0U;
	p2 = 0U;
	if ((best_p % 2U) == 0U) {
		half = best_p / 2U;
		if (half == 1U || half == 2U || half == 3U || half == 5U) {
			p0 = 2U;
			p1 = 1U;
			p2 = half;
		} else if ((half % 2U) == 0U) {
			p0 = 2U;
			p1 = half / 2U;
			p2 = 2U;
		} else if ((half % 3U) == 0U) {
			p0 = 3U;
			p1 = half / 3U;
			p2 = 2U;
		} else if ((half % 7U) == 0U) {
			p0 = 7U;
			p1 = half / 7U;
			p2 = 2U;
		}
	} else if (best_p == 3U || best_p == 9U) {
		p0 = 3U;
		p1 = 1U;
		p2 = best_p / 3U;
	} else if (best_p == 5U || best_p == 7U) {
		p0 = best_p;
		p1 = 1U;
		p2 = 1U;
	} else if (best_p == 15U) {
		p0 = 3U;
		p1 = 1U;
		p2 = 5U;
	} else if (best_p == 21U) {
		p0 = 7U;
		p1 = 1U;
		p2 = 3U;
	} else if (best_p == 35U) {
		p0 = 7U;
		p1 = 1U;
		p2 = 5U;
	}

	/* P0's and P2's codes. */
	switch (p0) {
	case 1U:
		pdiv = 0U;
		break;
	case 2U:
		pdiv = 1U;
		break;
	case 3U:
		pdiv = 2U;
		break;
	case 7U:
		pdiv = 4U;
		break;
	default:
		return EINVAL;
	}

	/* P2's code. */
	switch (p2) {
	case 5U:
		kdiv = 0U;
		break;
	case 2U:
		kdiv = 1U;
		break;
	case 3U:
		kdiv = 2U;
		break;
	case 1U:
		kdiv = 3U;
		break;
	default:
		return EINVAL;
	}

	/* The central frequency's code. */
	central_code = 3U;
	if (best_central == 9600000000ULL)
		central_code = 0U;
	else if (best_central == 9000000000ULL)
		central_code = 1U;

	/* The DCO's integer and fraction over the reference, in MHz with 15 fraction bits. */
	dco = (uint64_t)p0 * p1 * p2 * afe;
	dco_integer = dco / ((uint64_t)ref_khz * 1000U);
	dco_fraction = ((dco / (ref_khz / 1000U) - dco_integer * 1000000U) * 0x8000U) / 1000000U;

	/* The words: HDMI mode with the override, then the two configurations. */
	words->ctrl1 = 1U | (1U << I915_SKL_DPLL_CTRL1_HDMI_SHIFT);
	words->cfgcr1 = I915_SKL_CFGCR1_FREQ_ENABLE | ((uint32_t)dco_fraction << I915_SKL_CFGCR1_DCO_FRACTION_SHIFT) | (uint32_t)dco_integer;
	qdiv_mode = 1U;
	if (p1 == 1U)
		qdiv_mode = 0U;
	words->cfgcr2 = (p1 << I915_SKL_CFGCR2_QDIV_RATIO_SHIFT) | (qdiv_mode << I915_SKL_CFGCR2_QDIV_MODE_SHIFT) |
	    (kdiv << I915_SKL_CFGCR2_KDIV_SHIFT) | (pdiv << I915_SKL_CFGCR2_PDIV_SHIFT) | central_code;
	words->port_khz = pixel_khz;

	/* Succeeded: the WRPLL makes the clock. */
	return 0;
}

/* Reports the part's power class from its PCI device id (ULT, ULX, or H and S). */
int
drv_i915_skl_sku(
	uint16_t device_id,
	int platform)
{
	unsigned found;

	UNUSED_PARAMETER(platform);

	/* The ULX list, then the ULT list. */
	found = i915_skl_id_in(device_id, i915_skl_ulx_ids, (unsigned)(sizeof(i915_skl_ulx_ids) / sizeof(i915_skl_ulx_ids[0])));
	if (found)
		return I915_SKL_SKU_ULX;
	found = i915_skl_id_in(device_id, i915_skl_ult_ids, (unsigned)(sizeof(i915_skl_ult_ids) / sizeof(i915_skl_ult_ids[0])));
	if (found)
		return I915_SKL_SKU_ULT;

	/* Succeeded: every other part has the H and S tables. */
	return I915_SKL_SKU_H;
}

/*
 * Reports the DDI buffer translation table of a part, a kind of output
 * and a port (intel_ddi_buf_trans_init() and the get_buf_trans functions):
 * the count is cut to 9 for DP and eDP on a port other than A.
 */
const struct i915_skl_buf_trans *
drv_i915_skl_buf_trans(
	int sku,
	int platform,
	int output,
	unsigned port,
	unsigned *count)
{
	const struct i915_skl_buf_trans *table;
	unsigned entries;
	int kaby;

	/* Kaby Lake's DP tables also serve Coffee Lake and Comet Lake; Skylake has its own. */
	kaby = 1;
	if (platform == I915_PLATFORM_SKYLAKE)
		kaby = 0;

	/* HDMI: the Y table on ULX, the common one elsewhere. */
	if (output == I915_SKL_OUTPUT_HDMI) {
		if (sku == I915_SKL_SKU_ULX) {
			*count = (unsigned)(sizeof(i915_skl_y_trans_hdmi) / sizeof(i915_skl_y_trans_hdmi[0]));
			return i915_skl_y_trans_hdmi;
		}

		/* The common table elsewhere. */
		*count = (unsigned)(sizeof(i915_skl_trans_hdmi) / sizeof(i915_skl_trans_hdmi[0]));
		return i915_skl_trans_hdmi;
	}

	/* eDP (the low swing tables), or DP by platform and power class. */
	if (output == I915_SKL_OUTPUT_EDP) {
		table = i915_skl_trans_edp;
		entries = (unsigned)(sizeof(i915_skl_trans_edp) / sizeof(i915_skl_trans_edp[0]));
		if (sku == I915_SKL_SKU_ULX) {
			table = i915_skl_y_trans_edp;
			entries = (unsigned)(sizeof(i915_skl_y_trans_edp) / sizeof(i915_skl_y_trans_edp[0]));
		} else if (sku == I915_SKL_SKU_ULT) {
			table = i915_skl_u_trans_edp;
			entries = (unsigned)(sizeof(i915_skl_u_trans_edp) / sizeof(i915_skl_u_trans_edp[0]));
		}
	} else if (kaby) {
		table = i915_kbl_trans_dp;
		entries = (unsigned)(sizeof(i915_kbl_trans_dp) / sizeof(i915_kbl_trans_dp[0]));
		if (sku == I915_SKL_SKU_ULX) {
			table = i915_kbl_y_trans_dp;
			entries = (unsigned)(sizeof(i915_kbl_y_trans_dp) / sizeof(i915_kbl_y_trans_dp[0]));
		} else if (sku == I915_SKL_SKU_ULT) {
			table = i915_kbl_u_trans_dp;
			entries = (unsigned)(sizeof(i915_kbl_u_trans_dp) / sizeof(i915_kbl_u_trans_dp[0]));
		}
	} else {
		table = i915_skl_trans_dp;
		entries = (unsigned)(sizeof(i915_skl_trans_dp) / sizeof(i915_skl_trans_dp[0]));
		if (sku == I915_SKL_SKU_ULX) {
			table = i915_skl_y_trans_dp;
			entries = (unsigned)(sizeof(i915_skl_y_trans_dp) / sizeof(i915_skl_y_trans_dp[0]));
		} else if (sku == I915_SKL_SKU_ULT) {
			table = i915_skl_u_trans_dp;
			entries = (unsigned)(sizeof(i915_skl_u_trans_dp) / sizeof(i915_skl_u_trans_dp[0]));
		}
	}

	/* Only port A selects a tenth entry (skl_buf_trans_num_entries). */
	if (port != I915_SKL_PORT_A && entries > 9U)
		entries = 9U;

	/* Succeeded: the table and its usable entries. */
	*count = entries;
	return table;
}

/* Reports the table entry for a DP lane's training set (translate_signal_level), or 0. */
unsigned
drv_i915_skl_signal_level(
	uint8_t train_set)
{
	uint8_t levels;
	unsigned index;

	/* The swing and the pre-emphasis without the reached flags. */
	levels = (uint8_t)(train_set & (I915_DPCD_TRAIN_VOLTAGE_SWING_MASK | I915_DPCD_TRAIN_PRE_EMPHASIS_MASK));

	/* The entry that stands for them. */
	for (index = 0U; index < 10U; index++) {
		if (i915_skl_signal_levels[index] == levels)
			return index;
	}

	/* Succeeded: an unknown set takes the first entry. */
	return 0U;
}

/* Reports the highest voltage swing a table of so many entries reaches (intel_ddi_dp_voltage_max). */
uint8_t
drv_i915_skl_voltage_max(
	unsigned count)
{
	/* The last entry's swing. */
	if (count == 0U)
		count = 1U;
	if (count > 10U)
		count = 10U;

	/* Succeeded: the swing. */
	return (uint8_t)(i915_skl_signal_levels[count - 1U] & I915_DPCD_TRAIN_VOLTAGE_SWING_MASK);
}

/* Reports the entry an HDMI port uses: the tables' default, without a VBT level shift. */
unsigned
drv_i915_skl_hdmi_level(
	int sku,
	int platform)
{
	UNUSED_PARAMETER(sku);
	UNUSED_PARAMETER(platform);

	/* Succeeded: entry 8 of every display 9 HDMI table. */
	return I915_SKL_HDMI_DEFAULT_ENTRY;
}

/*
 * Turns the sink's adjust requests into the next training set
 * (intel_dp_get_lane_adjust_vswing_preemph without per-lane levels): the
 * highest swing and pre-emphasis any lane asks for, the pre-emphasis
 * capped at level 3 and the swing at what the pre-emphasis allows and the
 * table reaches, with the reached flags.
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
 * Computes the DDB split and the watermarks of the lit pipes.
 *
 * The DBUF is split between the lit pipes by their widths; each pipe
 * keeps the cursor's reservation at its end and gives the rest to the
 * first plane, at the highest watermark level whose blocks fit; levels
 * that do not fit the plane's share are turned off.  Returns 0, or
 * ENOSPC when even level 0 does not fit a pipe's share.
 */
int
drv_i915_skl_wm_compute(
	struct i915_skl_pipe_wm pipes[3],
	const uint16_t latency_us[8])
{
	struct i915_skl_wm_level cursor[8];
	uint32_t total_weight;
	uint32_t weight_before;
	uint32_t active;
	uint32_t size;
	uint32_t cursor_size;
	uint32_t plane_size;
	unsigned pipe;
	int level;
	int chosen;

	/* The lit pipes and their widths. */
	total_weight = 0U;
	active = 0U;
	for (pipe = 0U; pipe < 3U; pipe++) {
		if (!pipes[pipe].active)
			continue;
		total_weight += pipes[pipe].hdisplay;
		active++;
	}

	/* Each lit pipe: its share, the cursor's reservation, the plane's levels and its share. */
	weight_before = 0U;
	for (pipe = 0U; pipe < 3U; pipe++) {
		if (!pipes[pipe].active)
			continue;

		/* The pipe's share of the DBUF by its width (skl_ddb_entry_init of the weights). */
		pipes[pipe].ddb_start = (uint32_t)((uint64_t)I915_SKL_DBUF_BLOCKS * weight_before / total_weight);
		weight_before += pipes[pipe].hdisplay;
		pipes[pipe].ddb_end = (uint32_t)((uint64_t)I915_SKL_DBUF_BLOCKS * weight_before / total_weight);
		size = pipes[pipe].ddb_end - pipes[pipe].ddb_start;

		/* The plane's levels, and the cursor's (a 256-pixel ARGB cursor), for this pipe's timing. */
		i915_skl_plane_wm(pipes[pipe].plane_width, pipes[pipe].htotal, pipes[pipe].pixel_khz, latency_us, pipes[pipe].level);
		i915_skl_plane_wm(I915_SKL_CURSOR_WIDTH, pipes[pipe].htotal, pipes[pipe].pixel_khz, latency_us, cursor);

		/* The cursor keeps the highest level's need, at least 32 blocks alone or 8 with others. */
		cursor_size = I915_SKL_CURSOR_MIN_MANY;
		if (active == 1U)
			cursor_size = I915_SKL_CURSOR_MIN_ONE;
		for (level = 0; level < (int)I915_SKL_WM_LEVELS; level++) {
			if (cursor[level].min_ddb == I915_SKL_WM_REJECT)
				break;
			if (cursor[level].min_ddb > cursor_size)
				cursor_size = cursor[level].min_ddb;
		}

		/* A pipe whose cursor needs its whole share does not fit. */
		if (cursor_size >= size)
			return ENOSPC;
		pipes[pipe].cursor_start = pipes[pipe].ddb_end - cursor_size;
		pipes[pipe].cursor_end = pipes[pipe].ddb_end;

		/* The highest level both the plane and the cursor fit at. */
		plane_size = size - cursor_size;
		chosen = -1;
		for (level = (int)I915_SKL_WM_LEVELS - 1; level >= 0; level--) {
			if (cursor[level].min_ddb > cursor_size)
				continue;
			if (pipes[pipe].level[level].min_ddb > plane_size)
				continue;
			chosen = level;
			break;
		}

		/* A plane that fits at no level does not fit. */
		if (chosen < 0)
			return ENOSPC;

		/* The plane takes all the rest (its data rate is the pipe's only one). */
		pipes[pipe].plane_start = pipes[pipe].ddb_start;
		pipes[pipe].plane_end = pipes[pipe].ddb_start + plane_size;

		/* Levels above the chosen one stay only if their blocks fit the plane's share (skl_check_wm_level). */
		for (level = chosen + 1; level < (int)I915_SKL_WM_LEVELS; level++) {
			if (pipes[pipe].level[level].min_ddb > plane_size)
				kern_memset(&pipes[pipe].level[level], 0, sizeof(pipes[pipe].level[level]));
		}

		/* Levels the computation refused are off. */
		for (level = 0; level < (int)I915_SKL_WM_LEVELS; level++) {
			if (pipes[pipe].level[level].min_ddb == I915_SKL_WM_REJECT)
				kern_memset(&pipes[pipe].level[level], 0, sizeof(pipes[pipe].level[level]));
		}

		/* The line time in 1/8 us (skl_linetime_wm), at most 0x1ff. */
		pipes[pipe].linetime = (pipes[pipe].htotal * 1000U * 8U + pipes[pipe].pixel_khz - 1U) / pipes[pipe].pixel_khz;
		if (pipes[pipe].linetime > I915_SKL_LINETIME_MASK)
			pipes[pipe].linetime = I915_SKL_LINETIME_MASK;
	}

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

/* Divides into a 16.16 fixed-point number, rounded up (div_fixed16). */
static uint32_t
i915_skl_fixed_div(
	uint32_t value,
	uint32_t divisor)
{
	uint64_t scaled;

	/* The quotient with 16 fraction bits. */
	scaled = (uint64_t)value << I915_SKL_FIXED_SHIFT;
	scaled = (scaled + divisor - 1U) / divisor;
	if (scaled > 0xffffffffULL)
		scaled = 0xffffffffULL;

	/* Succeeded: the fixed-point quotient. */
	return (uint32_t)scaled;
}

/* Multiplies a fixed-point number by an integer (mul_u32_fixed16). */
static uint32_t
i915_skl_fixed_mul(
	uint32_t value,
	uint32_t fixed)
{
	uint64_t product;

	/* The product, clamped to 32 bits. */
	product = (uint64_t)value * fixed;
	if (product > 0xffffffffULL)
		product = 0xffffffffULL;

	/* Succeeded: the fixed-point product. */
	return (uint32_t)product;
}

/* Rounds a fixed-point number up to an integer (fixed16_to_u32_round_up). */
static uint32_t
i915_skl_fixed_round_up(
	uint32_t fixed)
{
	/* Succeeded: the integer at or above it. */
	return (uint32_t)(((uint64_t)fixed + 0xffffU) >> I915_SKL_FIXED_SHIFT);
}

/*
 * Computes the levels of a linear 32-bit plane of a width on a pipe's
 * timing (skl_compute_wm_params() and skl_compute_plane_wm() for display
 * 9, linear, unrotated, IPC off).
 */
static void
i915_skl_plane_wm(
	uint32_t width,
	uint32_t htotal,
	uint32_t pixel_khz,
	const uint16_t latency_us[8],
	struct i915_skl_wm_level levels[8])
{
	uint32_t bytes_per_line;
	uint32_t blocks_per_line;
	uint32_t linetime_us;
	uint32_t latency;
	uint32_t method1;
	uint32_t method2;
	uint32_t selected;
	uint32_t blocks;
	uint32_t lines;
	uint32_t previous_blocks;
	uint32_t line_blocks;
	uint32_t interim;
	unsigned level;

	/* The plane's line in DBUF blocks: the bytes rounded up to blocks, and one more for a linear surface. */
	bytes_per_line = width * I915_SKL_PLANE_CPP;
	blocks_per_line = ((bytes_per_line + I915_SKL_DBUF_BLOCK_BYTES - 1U) / I915_SKL_DBUF_BLOCK_BYTES + 1U) << I915_SKL_FIXED_SHIFT;

	/* The line time in microseconds, rounded up. */
	linetime_us = i915_skl_fixed_round_up(i915_skl_fixed_div(htotal * 1000U, pixel_khz));

	/* Each level from its latency. */
	previous_blocks = 0U;
	for (level = 0U; level < I915_SKL_WM_LEVELS; level++) {
		kern_memset(&levels[level], 0, sizeof(levels[level]));
		levels[level].min_ddb = I915_SKL_WM_REJECT;

		/* A level without a latency is not used. */
		latency = latency_us[level];
		if (latency == 0U)
			continue;

		/* Method 1 (bytes over the latency) and method 2 (lines over the latency). */
		method1 = i915_skl_fixed_div(latency * pixel_khz * I915_SKL_PLANE_CPP, 1000U * I915_SKL_DBUF_BLOCK_BYTES);
		interim = (latency * pixel_khz + htotal * 1000U - 1U) / (htotal * 1000U);
		method2 = i915_skl_fixed_mul(interim, blocks_per_line);

		/* The method the line's size and the latency choose. */
		if ((I915_SKL_PLANE_CPP * htotal / I915_SKL_DBUF_BLOCK_BYTES) < 1U && (bytes_per_line / I915_SKL_DBUF_BLOCK_BYTES) < 1U) {
			selected = method2;
		} else if (latency >= linetime_us) {
			selected = method1;
			if (method2 < method1)
				selected = method2;
		} else {
			selected = method1;
		}

		/* The blocks, the lines, and display 9's rules for the levels above 0. */
		blocks = i915_skl_fixed_round_up(selected) + 1U;
		lines = (selected + blocks_per_line - 1U) / blocks_per_line;
		if (level > 0U) {
			line_blocks = i915_skl_fixed_round_up(blocks_per_line);
			if (blocks < line_blocks)
				blocks = line_blocks;
			blocks++;
			if (previous_blocks > blocks)
				blocks = previous_blocks;
		} else {
			lines = 0U;
		}

		/* A level needing more lines than the register holds is not used. */
		if (lines > I915_SKL_WM_MAX_LINES)
			continue;

		/* The level, and the DDB it needs: more than its blocks. */
		levels[level].enable = 1;
		levels[level].blocks = blocks;
		levels[level].lines = lines;
		levels[level].min_ddb = blocks + 1U;
		previous_blocks = blocks;
	}
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

/* Reports nonzero when a device id is in a list. */
static unsigned
i915_skl_id_in(
	uint16_t device_id,
	const uint16_t *ids,
	unsigned count)
{
	unsigned index;

	/* Each id. */
	for (index = 0U; index < count; index++) {
		if (ids[index] == device_id)
			return 1U;
	}

	/* Succeeded: not listed. */
	return 0U;
}

/* Halves a ratio until both fit 24 bits (intel_reduce_m_n_ratio); reports the halvings. */
static uint32_t
i915_skl_reduce(
	uint32_t *m,
	uint32_t *n)
{
	uint32_t halvings;

	/* Each halving. */
	halvings = 0U;
	while (*m > 0x00ffffffU || *n > 0x00ffffffU) {
		*m >>= 1;
		*n >>= 1;
		halvings++;
	}

	/* Succeeded: the ratio fits. */
	return halvings;
}

/* Computes one M and N over a constant N (compute_m_n). */
static void
i915_skl_m_n(
	uint32_t m,
	uint32_t n,
	uint32_t constant_n,
	uint32_t *out_m,
	uint32_t *out_n)
{
	/* M scaled to the constant N, then reduced. */
	*out_n = constant_n;
	*out_m = (uint32_t)(((uint64_t)m * constant_n) / n);
	(void)i915_skl_reduce(out_m, out_n);
}

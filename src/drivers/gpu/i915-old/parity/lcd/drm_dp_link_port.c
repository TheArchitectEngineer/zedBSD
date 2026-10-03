/*
 * Copyright © 2009 Keith Packard
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
 * zedBSD WS031: generated from Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c
 * (sha256 030568524ac5db3fbd09725df196b22a430ec18fc1432dbb952298ce7c791a73) by plan/ws031/handover/tools/port_lcd_calc.py.
 * The function bodies are the reference text.  Kept / changed:
 *  - kept: dp_lttpr_common_cap, dp_lttpr_phy_cap, drm_dp_read_lttpr_regs, dp_link_status,
 *    dp_get_lane_status, drm_dp_channel_eq_ok, drm_dp_clock_recovery_ok, drm_dp_get_adjust_request_voltage,
 *    drm_dp_get_adjust_request_pre_emphasis, drm_dp_get_adjust_tx_ffe_preset, __8b10b_clock_recovery_delay_us, __8b10b_channel_eq_delay_us,
 *    __128b132b_channel_eq_delay_us, __read_delay, drm_dp_read_clock_recovery_delay, drm_dp_read_channel_eq_delay,
 *    drm_dp_dpcd_read_phy_link_status, drm_dp_lttpr_count, drm_dp_lttpr_voltage_swing_level_3_supported, drm_dp_lttpr_pre_emphasis_level_3_supported,
 *    drm_dp_read_lttpr_common_caps, drm_dp_read_lttpr_phy_caps, drm_dp_phy_name, drm_dp_link_rate_to_bw_code;
 *  - the includes are replaced by: lcd_compat.h, lcd_seq_compat.h, lcd_modeset_compat.h, lcd_dp_compat.h;
 */

#include "lcd_compat.h"
#include "lcd_seq_compat.h"
#include "lcd_modeset_compat.h"
#include "lcd_dp_compat.h"

/* zedBSD: forward declarations of the static functions of this file (generated; the keep-list is not in call order) */
static u8 dp_lttpr_common_cap(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE], int r);
static u8 dp_lttpr_phy_cap(const u8 phy_cap[DP_LTTPR_PHY_CAP_SIZE], int r);
static int drm_dp_read_lttpr_regs(struct drm_dp_aux *aux,
				  const u8 dpcd[DP_RECEIVER_CAP_SIZE], int address,
				  u8 *buf, int buf_size);
static u8 dp_link_status(const u8 link_status[DP_LINK_STATUS_SIZE], int r);
static u8 dp_get_lane_status(const u8 link_status[DP_LINK_STATUS_SIZE],
			     int lane);
static int __8b10b_clock_recovery_delay_us(const struct drm_dp_aux *aux, u8 rd_interval);
static int __8b10b_channel_eq_delay_us(const struct drm_dp_aux *aux, u8 rd_interval);
static int __128b132b_channel_eq_delay_us(const struct drm_dp_aux *aux, u8 rd_interval);
static int __read_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			enum drm_dp_phy dp_phy, bool uhbr, bool cr);

static u8 dp_lttpr_common_cap(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE], int r)
{
	return caps[r - DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV];
}

static u8 dp_lttpr_phy_cap(const u8 phy_cap[DP_LTTPR_PHY_CAP_SIZE], int r)
{
	return phy_cap[r - DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER1];
}

static int drm_dp_read_lttpr_regs(struct drm_dp_aux *aux,
				  const u8 dpcd[DP_RECEIVER_CAP_SIZE], int address,
				  u8 *buf, int buf_size)
{
	/*
	 * At least the DELL P2715Q monitor with a DPCD_REV < 0x14 returns
	 * corrupted values when reading from the 0xF0000- range with a block
	 * size bigger than 1.
	 */
	int block_size = dpcd[DP_DPCD_REV] < 0x14 ? 1 : buf_size;
	int offset;
	int ret;

	for (offset = 0; offset < buf_size; offset += block_size) {
		ret = drm_dp_dpcd_read(aux,
				       address + offset,
				       &buf[offset], block_size);
		if (ret < 0)
			return ret;

		WARN_ON(ret != block_size);
	}

	return 0;
}

/* Helpers for DP link training */
static u8 dp_link_status(const u8 link_status[DP_LINK_STATUS_SIZE], int r)
{
	return link_status[r - DP_LANE0_1_STATUS];
}

static u8 dp_get_lane_status(const u8 link_status[DP_LINK_STATUS_SIZE],
			     int lane)
{
	int i = DP_LANE0_1_STATUS + (lane >> 1);
	int s = (lane & 1) * 4;
	u8 l = dp_link_status(link_status, i);

	return (l >> s) & 0xf;
}

bool drm_dp_channel_eq_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			  int lane_count)
{
	u8 lane_align;
	u8 lane_status;
	int lane;

	lane_align = dp_link_status(link_status,
				    DP_LANE_ALIGN_STATUS_UPDATED);
	if ((lane_align & DP_INTERLANE_ALIGN_DONE) == 0)
		return false;
	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_CHANNEL_EQ_BITS) != DP_CHANNEL_EQ_BITS)
			return false;
	}
	return true;
}

bool drm_dp_clock_recovery_ok(const u8 link_status[DP_LINK_STATUS_SIZE],
			      int lane_count)
{
	int lane;
	u8 lane_status;

	for (lane = 0; lane < lane_count; lane++) {
		lane_status = dp_get_lane_status(link_status, lane);
		if ((lane_status & DP_LANE_CR_DONE) == 0)
			return false;
	}
	return true;
}

u8 drm_dp_get_adjust_request_voltage(const u8 link_status[DP_LINK_STATUS_SIZE],
				     int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_VOLTAGE_SWING_LANE1_SHIFT :
		 DP_ADJUST_VOLTAGE_SWING_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_VOLTAGE_SWING_SHIFT;
}

u8 drm_dp_get_adjust_request_pre_emphasis(const u8 link_status[DP_LINK_STATUS_SIZE],
					  int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_PRE_EMPHASIS_LANE1_SHIFT :
		 DP_ADJUST_PRE_EMPHASIS_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return ((l >> s) & 0x3) << DP_TRAIN_PRE_EMPHASIS_SHIFT;
}

/* DP 2.0 128b/132b */
u8 drm_dp_get_adjust_tx_ffe_preset(const u8 link_status[DP_LINK_STATUS_SIZE],
				   int lane)
{
	int i = DP_ADJUST_REQUEST_LANE0_1 + (lane >> 1);
	int s = ((lane & 1) ?
		 DP_ADJUST_TX_FFE_PRESET_LANE1_SHIFT :
		 DP_ADJUST_TX_FFE_PRESET_LANE0_SHIFT);
	u8 l = dp_link_status(link_status, i);

	return (l >> s) & 0xf;
}

static int __8b10b_clock_recovery_delay_us(const struct drm_dp_aux *aux, u8 rd_interval)
{
	if (rd_interval > 4)
		drm_dbg_kms(aux->drm_dev, "%s: invalid AUX interval 0x%02x (max 4)\n",
			    aux->name, rd_interval);

	if (rd_interval == 0)
		return 100;

	return rd_interval * 4 * USEC_PER_MSEC;
}

static int __8b10b_channel_eq_delay_us(const struct drm_dp_aux *aux, u8 rd_interval)
{
	if (rd_interval > 4)
		drm_dbg_kms(aux->drm_dev, "%s: invalid AUX interval 0x%02x (max 4)\n",
			    aux->name, rd_interval);

	if (rd_interval == 0)
		return 400;

	return rd_interval * 4 * USEC_PER_MSEC;
}

static int __128b132b_channel_eq_delay_us(const struct drm_dp_aux *aux, u8 rd_interval)
{
	switch (rd_interval) {
	default:
		drm_dbg_kms(aux->drm_dev, "%s: invalid AUX interval 0x%02x\n",
			    aux->name, rd_interval);
		fallthrough;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_400_US:
		return 400;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_4_MS:
		return 4000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_8_MS:
		return 8000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_12_MS:
		return 12000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_16_MS:
		return 16000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_32_MS:
		return 32000;
	case DP_128B132B_TRAINING_AUX_RD_INTERVAL_64_MS:
		return 64000;
	}
}

/*
 * The link training delays are different for:
 *
 *  - Clock recovery vs. channel equalization
 *  - DPRX vs. LTTPR
 *  - 128b/132b vs. 8b/10b
 *  - DPCD rev 1.3 vs. later
 *
 * Get the correct delay in us, reading DPCD if necessary.
 */
static int __read_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			enum drm_dp_phy dp_phy, bool uhbr, bool cr)
{
	int (*parse)(const struct drm_dp_aux *aux, u8 rd_interval);
	unsigned int offset;
	u8 rd_interval, mask;

	if (dp_phy == DP_PHY_DPRX) {
		if (uhbr) {
			if (cr)
				return 100;

			offset = DP_128B132B_TRAINING_AUX_RD_INTERVAL;
			mask = DP_128B132B_TRAINING_AUX_RD_INTERVAL_MASK;
			parse = __128b132b_channel_eq_delay_us;
		} else {
			if (cr && dpcd[DP_DPCD_REV] >= DP_DPCD_REV_14)
				return 100;

			offset = DP_TRAINING_AUX_RD_INTERVAL;
			mask = DP_TRAINING_AUX_RD_MASK;
			if (cr)
				parse = __8b10b_clock_recovery_delay_us;
			else
				parse = __8b10b_channel_eq_delay_us;
		}
	} else {
		if (uhbr) {
			offset = DP_128B132B_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy);
			mask = DP_128B132B_TRAINING_AUX_RD_INTERVAL_MASK;
			parse = __128b132b_channel_eq_delay_us;
		} else {
			if (cr)
				return 100;

			offset = DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy);
			mask = DP_TRAINING_AUX_RD_MASK;
			parse = __8b10b_channel_eq_delay_us;
		}
	}

	if (offset < DP_RECEIVER_CAP_SIZE) {
		rd_interval = dpcd[offset];
	} else {
		if (drm_dp_dpcd_readb(aux, offset, &rd_interval) != 1) {
			drm_dbg_kms(aux->drm_dev, "%s: failed rd interval read\n",
				    aux->name);
			/* arbitrary default delay */
			return 400;
		}
	}

	return parse(aux, rd_interval & mask);
}

int drm_dp_read_clock_recovery_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				     enum drm_dp_phy dp_phy, bool uhbr)
{
	return __read_delay(aux, dpcd, dp_phy, uhbr, true);
}

int drm_dp_read_channel_eq_delay(struct drm_dp_aux *aux, const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				 enum drm_dp_phy dp_phy, bool uhbr)
{
	return __read_delay(aux, dpcd, dp_phy, uhbr, false);
}

/**
 * drm_dp_dpcd_read_phy_link_status - get the link status information for a DP PHY
 * @aux: DisplayPort AUX channel
 * @dp_phy: the DP PHY to get the link status for
 * @link_status: buffer to return the status in
 *
 * Fetch the AUX DPCD registers for the DPRX or an LTTPR PHY link status. The
 * layout of the returned @link_status matches the DPCD register layout of the
 * DPRX PHY link status.
 *
 * Returns 0 if the information was read successfully or a negative error code
 * on failure.
 */
int drm_dp_dpcd_read_phy_link_status(struct drm_dp_aux *aux,
				     enum drm_dp_phy dp_phy,
				     u8 link_status[DP_LINK_STATUS_SIZE])
{
	int ret;

	if (dp_phy == DP_PHY_DPRX) {
		ret = drm_dp_dpcd_read(aux,
				       DP_LANE0_1_STATUS,
				       link_status,
				       DP_LINK_STATUS_SIZE);

		if (ret < 0)
			return ret;

		WARN_ON(ret != DP_LINK_STATUS_SIZE);

		return 0;
	}

	ret = drm_dp_dpcd_read(aux,
			       DP_LANE0_1_STATUS_PHY_REPEATER(dp_phy),
			       link_status,
			       DP_LINK_STATUS_SIZE - 1);

	if (ret < 0)
		return ret;

	WARN_ON(ret != DP_LINK_STATUS_SIZE - 1);

	/* Convert the LTTPR to the sink PHY link status layout */
	memmove(&link_status[DP_SINK_STATUS - DP_LANE0_1_STATUS + 1],
		&link_status[DP_SINK_STATUS - DP_LANE0_1_STATUS],
		DP_LINK_STATUS_SIZE - (DP_SINK_STATUS - DP_LANE0_1_STATUS) - 1);
	link_status[DP_SINK_STATUS - DP_LANE0_1_STATUS] = 0;

	return 0;
}

/**
 * drm_dp_lttpr_count - get the number of detected LTTPRs
 * @caps: LTTPR common capabilities
 *
 * Get the number of detected LTTPRs from the LTTPR common capabilities info.
 *
 * Returns:
 *   -ERANGE if more than supported number (8) of LTTPRs are detected
 *   -EINVAL if the DP_PHY_REPEATER_CNT register contains an invalid value
 *   otherwise the number of detected LTTPRs
 */
int drm_dp_lttpr_count(const u8 caps[DP_LTTPR_COMMON_CAP_SIZE])
{
	u8 count = dp_lttpr_common_cap(caps, DP_PHY_REPEATER_CNT);

	switch (hweight8(count)) {
	case 0:
		return 0;
	case 1:
		return 8 - ilog2(count);
	case 8:
		return -ERANGE;
	default:
		return -EINVAL;
	}
}

/**
 * drm_dp_lttpr_voltage_swing_level_3_supported - check for LTTPR vswing3 support
 * @caps: LTTPR PHY capabilities
 *
 * Returns true if the @caps for an LTTPR TX PHY indicate support for
 * voltage swing level 3.
 */
bool
drm_dp_lttpr_voltage_swing_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE])
{
	u8 txcap = dp_lttpr_phy_cap(caps, DP_TRANSMITTER_CAPABILITY_PHY_REPEATER1);

	return txcap & DP_VOLTAGE_SWING_LEVEL_3_SUPPORTED;
}

/**
 * drm_dp_lttpr_pre_emphasis_level_3_supported - check for LTTPR preemph3 support
 * @caps: LTTPR PHY capabilities
 *
 * Returns true if the @caps for an LTTPR TX PHY indicate support for
 * pre-emphasis level 3.
 */
bool
drm_dp_lttpr_pre_emphasis_level_3_supported(const u8 caps[DP_LTTPR_PHY_CAP_SIZE])
{
	u8 txcap = dp_lttpr_phy_cap(caps, DP_TRANSMITTER_CAPABILITY_PHY_REPEATER1);

	return txcap & DP_PRE_EMPHASIS_LEVEL_3_SUPPORTED;
}

/**
 * drm_dp_read_lttpr_common_caps - read the LTTPR common capabilities
 * @aux: DisplayPort AUX channel
 * @dpcd: DisplayPort configuration data
 * @caps: buffer to return the capability info in
 *
 * Read capabilities common to all LTTPRs.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_read_lttpr_common_caps(struct drm_dp_aux *aux,
				  const u8 dpcd[DP_RECEIVER_CAP_SIZE],
				  u8 caps[DP_LTTPR_COMMON_CAP_SIZE])
{
	return drm_dp_read_lttpr_regs(aux, dpcd,
				      DP_LT_TUNABLE_PHY_REPEATER_FIELD_DATA_STRUCTURE_REV,
				      caps, DP_LTTPR_COMMON_CAP_SIZE);
}

/**
 * drm_dp_read_lttpr_phy_caps - read the capabilities for a given LTTPR PHY
 * @aux: DisplayPort AUX channel
 * @dpcd: DisplayPort configuration data
 * @dp_phy: LTTPR PHY to read the capabilities for
 * @caps: buffer to return the capability info in
 *
 * Read the capabilities for the given LTTPR PHY.
 *
 * Returns 0 on success or a negative error code on failure.
 */
int drm_dp_read_lttpr_phy_caps(struct drm_dp_aux *aux,
			       const u8 dpcd[DP_RECEIVER_CAP_SIZE],
			       enum drm_dp_phy dp_phy,
			       u8 caps[DP_LTTPR_PHY_CAP_SIZE])
{
	return drm_dp_read_lttpr_regs(aux, dpcd,
				      DP_TRAINING_AUX_RD_INTERVAL_PHY_REPEATER(dp_phy),
				      caps, DP_LTTPR_PHY_CAP_SIZE);
}

/**
 * drm_dp_phy_name() - Get the name of the given DP PHY
 * @dp_phy: The DP PHY identifier
 *
 * Given the @dp_phy, get a user friendly name of the DP PHY, either "DPRX" or
 * "LTTPR <N>", or "<INVALID DP PHY>" on errors. The returned string is always
 * non-NULL and valid.
 *
 * Returns: Name of the DP PHY.
 */
const char *drm_dp_phy_name(enum drm_dp_phy dp_phy)
{
	static const char * const phy_names[] = {
		[DP_PHY_DPRX] = "DPRX",
		[DP_PHY_LTTPR1] = "LTTPR 1",
		[DP_PHY_LTTPR2] = "LTTPR 2",
		[DP_PHY_LTTPR3] = "LTTPR 3",
		[DP_PHY_LTTPR4] = "LTTPR 4",
		[DP_PHY_LTTPR5] = "LTTPR 5",
		[DP_PHY_LTTPR6] = "LTTPR 6",
		[DP_PHY_LTTPR7] = "LTTPR 7",
		[DP_PHY_LTTPR8] = "LTTPR 8",
	};

	if (dp_phy < 0 || dp_phy >= ARRAY_SIZE(phy_names) ||
	    WARN_ON(!phy_names[dp_phy]))
		return "<INVALID DP PHY>";

	return phy_names[dp_phy];
}

u8 drm_dp_link_rate_to_bw_code(int link_rate)
{
	switch (link_rate) {
	case 1000000:
		return DP_LINK_BW_10;
	case 1350000:
		return DP_LINK_BW_13_5;
	case 2000000:
		return DP_LINK_BW_20;
	default:
		/* Spec says link_bw = link_rate / 0.27Gbps */
		return link_rate / 27000;
	}
}


/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DisplayPort link of the display 12 and 13 code (see xd-dp.h).
 */

#include "xd-dp.h"

#include "../intel/display-dpcd-xd.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The 8b/10b rates a sink may offer by its rate code: RBR, HBR, HBR2,
 * HBR3; and those a display 11 to 13 source offers, which add the
 * intermediate eDP rates.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_set_dpcd_sink_rates' dp_rates, icl_rates).
 */
static const uint32_t i915_xd_dp_sink_table[] = {
	162000U, 270000U, 540000U, 810000U
};
static const uint32_t i915_xd_dp_source_table[] = {
	162000U, 216000U, 270000U, 324000U, 432000U, 540000U, 648000U, 810000U
};

/*
 * The rate a link falls back to when the ends share nothing, and the
 * unit of an 8b/10b rate code.
 *
 * Values referenced: Linux v6.8.12 include/drm/display/drm_dp_helper.h
 * (drm_dp_bw_code_to_link_rate), Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp.c (intel_dp_set_dpcd_sink_rates).
 */
#define I915_XD_DP_RATE_RBR		162000U
#define I915_XD_DP_CODE_UNIT		27000U

/*
 * The rate codes that name 128b/132b rates (10, 13.5 and 20 Gbit/s), and
 * where the 128b/132b rates begin.
 *
 * Values referenced: Linux v6.8.12 include/drm/display/drm_dp.h
 * (DP_LINK_BW_10, DP_LINK_BW_13_5, DP_LINK_BW_20), Linux v6.8.12
 * drivers/gpu/drm/display/drm_dp_helper.c (drm_dp_is_uhbr_rate).
 */
#define I915_XD_DP_CODE_UHBR10		0x01U
#define I915_XD_DP_CODE_UHBR20		0x02U
#define I915_XD_DP_CODE_UHBR13_5	0x04U
#define I915_XD_DP_RATE_UHBR10		1000000U
#define I915_XD_DP_RATE_UHBR13_5	1350000U
#define I915_XD_DP_RATE_UHBR20		2000000U

/*
 * The share of a link's bits that carries data: 80 % for 8b/10b, 96.71 %
 * for 128b/132b (parts per million); the bits of one symbol of each.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c
 * (drm_dp_bw_channel_coding_efficiency), Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp.c (intel_dp_link_symbol_size).
 */
#define I915_XD_DP_EFFICIENCY_8B10B	800000U
#define I915_XD_DP_EFFICIENCY_128B132B	967100U
#define I915_XD_DP_SYMBOL_8B10B		10U
#define I915_XD_DP_SYMBOL_128B132B	32U

/*
 * The M/N values: the transfer unit, the fixed N of the data and of the
 * link pair, and the width of the register fields both terms must fit.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_display.c
 * (intel_link_compute_m_n, compute_m_n, intel_reduce_m_n_ratio), Linux
 * v6.8.12 drivers/gpu/drm/i915/i915_reg.h (DATA_LINK_M_N_MASK).
 */
#define I915_XD_DP_TU			64U
#define I915_XD_DP_DATA_N		0x8000000U
#define I915_XD_DP_LINK_N		0x80000U
#define I915_XD_DP_M_N_MAX		0xffffffU

/*
 * The waits between the steps of a training: the defaults of clock
 * recovery and of channel equalization, the unit of a read interval,
 * the highest interval the standard names.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c
 * (__8b10b_clock_recovery_delay_us, __8b10b_channel_eq_delay_us,
 * __read_delay).
 */
#define I915_XD_DP_CR_WAIT_US		100U
#define I915_XD_DP_EQ_WAIT_US		400U
#define I915_XD_DP_INTERVAL_US		4000U
#define I915_XD_DP_INTERVAL_TOP		4U

/*
 * The tries of a training: clock recovery on a sink older than DPCD 1.4
 * and on a newer one, the same requests in a row, channel equalization.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_link_training_clock_recovery,
 * intel_dp_link_training_channel_equalization).
 */
#define I915_XD_DP_CR_TRIES_OLD		80U
#define I915_XD_DP_CR_TRIES		10U
#define I915_XD_DP_SAME_TRIES		5U
#define I915_XD_DP_EQ_TRIES		5U

/* The receiver a training step talks to: the sink, or repeater 1..8. */
#define I915_XD_DP_HOP_SINK		0U

/* The bytes of the link status a repeater reports (it has no sink status byte). */
#define I915_XD_DP_REPEATER_STATUS_SIZE	5U

static uint32_t i915_xd_dp_div_round_up(uint64_t numerator, uint64_t denominator);
static void i915_xd_dp_note(const struct i915_xd_dp_link *link, unsigned kind, const char *text);
static void i915_xd_dp_sleep(const struct i915_xd_dp_link *link, unsigned wait_us);
static void i915_xd_dp_ratio(uint32_t m, uint32_t n, uint32_t fixed_n, uint32_t *ret_m, uint32_t *ret_n);
static unsigned i915_xd_dp_lane_bits(const uint8_t *status, unsigned lane);
static uint8_t i915_xd_dp_request(const uint8_t *status, unsigned lane);
static int i915_xd_dp_repeater_count(const uint8_t *common);
static void i915_xd_dp_read_repeater_caps(struct i915_xd_dp_link *link, const uint8_t *caps, uint32_t address, uint8_t *buffer, unsigned size, int *error);
static unsigned i915_xd_dp_find_repeaters(struct i915_xd_dp_link *link, const uint8_t *caps);
static int i915_xd_dp_set_repeater_mode(struct i915_xd_dp_link *link, uint8_t mode);
static int i915_xd_dp_refresh_caps(struct i915_xd_dp_link *link);
static void i915_xd_dp_configure(struct i915_xd_dp_link *link);
static int i915_xd_dp_nearest(const struct i915_xd_dp_link *link, unsigned hop);
static unsigned i915_xd_dp_wait(struct i915_xd_dp_link *link, unsigned hop, int equalize);
static uint8_t i915_xd_dp_limit(const struct i915_xd_dp_link *link, unsigned hop, uint8_t swing, uint8_t preemph);
static void i915_xd_dp_follow(struct i915_xd_dp_link *link, unsigned hop, const uint8_t *status);
static int i915_xd_dp_send_pattern(struct i915_xd_dp_link *link, unsigned hop, uint8_t pattern);
static int i915_xd_dp_send_levels(struct i915_xd_dp_link *link, unsigned hop);
static int i915_xd_dp_requests_differ(const struct i915_xd_dp_link *link, const uint8_t *before, const uint8_t *now);
static int i915_xd_dp_at_top(const struct i915_xd_dp_link *link);
static int i915_xd_dp_recover_clock(struct i915_xd_dp_link *link, unsigned hop);
static uint8_t i915_xd_dp_eq_pattern(const struct i915_xd_dp_link *link, unsigned hop);
static int i915_xd_dp_equalize(struct i915_xd_dp_link *link, unsigned hop);
static void i915_xd_dp_end_hop(struct i915_xd_dp_link *link, unsigned hop);

/*
 * Converts a link rate to the code the sink's rate setting takes: the
 * code of a standard rate, otherwise the rate in units of 0.27 Gbit/s.
 *
 * Values referenced: Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c
 * (drm_dp_link_rate_to_bw_code).
 */
uint8_t
drv_i915_xd_dp_rate_code(
	uint32_t rate_khz)
{
	/* The 128b/132b rates have codes of their own. */
	switch (rate_khz) {
	case I915_XD_DP_RATE_UHBR10:
		return (uint8_t)I915_XD_DP_CODE_UHBR10;
	case I915_XD_DP_RATE_UHBR13_5:
		return (uint8_t)I915_XD_DP_CODE_UHBR13_5;
	case I915_XD_DP_RATE_UHBR20:
		return (uint8_t)I915_XD_DP_CODE_UHBR20;
	default:
		break;
	}

	/* Succeeded: an 8b/10b rate counts units of the code. */
	return (uint8_t)(rate_khz / I915_XD_DP_CODE_UNIT);
}

/*
 * Converts a rate code the sink reports to its link rate.
 *
 * Values referenced: Linux v6.8.12 include/drm/display/drm_dp_helper.h
 * (drm_dp_bw_code_to_link_rate).
 */
uint32_t
drv_i915_xd_dp_code_rate(
	uint8_t code)
{
	/* Three codes name 128b/132b rates. */
	switch (code) {
	case I915_XD_DP_CODE_UHBR10:
		return I915_XD_DP_RATE_UHBR10;
	case I915_XD_DP_CODE_UHBR13_5:
		return I915_XD_DP_RATE_UHBR13_5;
	case I915_XD_DP_CODE_UHBR20:
		return I915_XD_DP_RATE_UHBR20;
	default:
		break;
	}

	/* Succeeded: every other code counts units of 0.27 Gbit/s. */
	return (uint32_t)code * I915_XD_DP_CODE_UNIT;
}

/*
 * Lists the rates each end of a link offers and those both share.
 *
 * The source offers its table up to the lower of the platform's and the
 * VBT's limit (0: none); the sink offers the standard rates up to the
 * rate of its code and, when repeaters name one, theirs.  A sink left
 * with nothing, or ends that meet nowhere, get RBR.  Returns the
 * I915_XD_DP_RATES_ bits of what had to be made up.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_set_source_rates, intel_dp_set_dpcd_sink_rates,
 * intel_dp_set_common_rates).
 */
unsigned
drv_i915_xd_dp_rates(
	uint32_t source_max_khz,
	uint32_t vbt_max_khz,
	uint8_t sink_code,
	uint8_t repeater_code,
	struct i915_xd_dp_rates *rates)
{
	uint32_t source_limit;
	uint32_t sink_limit;
	uint32_t repeater_limit;
	unsigned index;
	unsigned source_index;
	unsigned sink_index;
	unsigned made_up;

	/* The source's limit: the platform's, and the VBT's when it is lower or the only one. */
	source_limit = source_max_khz;
	if (vbt_max_khz != 0U) {
		if (source_limit == 0U || vbt_max_khz < source_limit)
			source_limit = vbt_max_khz;
	}

	/* The source's rates up to its limit; the table rises, so the first rate above it ends the list. */
	rates->source_count = 0U;
	for (index = 0U; index < sizeof(i915_xd_dp_source_table) / sizeof(i915_xd_dp_source_table[0]); index++) {
		if (source_limit != 0U && i915_xd_dp_source_table[index] > source_limit)
			break;

		rates->source[rates->source_count] = i915_xd_dp_source_table[index];
		rates->source_count++;
	}

	/* The sink's limit: its own code's rate, lowered by the repeaters' when they name one. */
	sink_limit = drv_i915_xd_dp_code_rate(sink_code);
	repeater_limit = drv_i915_xd_dp_code_rate(repeater_code);
	if (repeater_limit != 0U && repeater_limit < sink_limit)
		sink_limit = repeater_limit;

	/* The sink's standard rates up to its limit. */
	rates->sink_count = 0U;
	for (index = 0U; index < sizeof(i915_xd_dp_sink_table) / sizeof(i915_xd_dp_sink_table[0]); index++) {
		if (i915_xd_dp_sink_table[index] > sink_limit)
			break;

		rates->sink[rates->sink_count] = i915_xd_dp_sink_table[index];
		rates->sink_count++;
	}

	/* A sink that offers nothing is given RBR. */
	made_up = 0U;
	if (rates->sink_count == 0U) {
		rates->sink[0] = I915_XD_DP_RATE_RBR;
		rates->sink_count = 1U;
		made_up |= I915_XD_DP_RATES_SINK_NONE;
	}

	/* The shared rates: both lists rise, so the list with the lower rate moves on until they meet. */
	rates->common_count = 0U;
	source_index = 0U;
	sink_index = 0U;
	while (source_index < rates->source_count && sink_index < rates->sink_count) {
		if (rates->source[source_index] == rates->sink[sink_index]) {
			rates->common[rates->common_count] = rates->source[source_index];
			rates->common_count++;
			source_index++;
			sink_index++;
		} else if (rates->source[source_index] < rates->sink[sink_index]) {
			source_index++;
		} else {
			sink_index++;
		}
	}

	/* Ends that meet nowhere share RBR. */
	if (rates->common_count == 0U) {
		rates->common[0] = I915_XD_DP_RATE_RBR;
		rates->common_count = 1U;
		made_up |= I915_XD_DP_RATES_SHARED_NONE;
	}

	/* Succeeded: what had to be made up. */
	return made_up;
}

/*
 * Reads the link rates an eDP 1.4 panel lists in its DPCD (16 bytes from
 * I915_XD_DPCD_EDP_RATES: entries of 200 kHz units, little endian, up to
 * the first zero) into rates, in kHz of the port clock.  Returns how
 * many it stored (0: the panel lists none).
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_edp_set_sink_rates).
 */
unsigned
drv_i915_xd_dp_edp_rates(
	const uint8_t *table,
	uint32_t *rates,
	unsigned room)
{
	unsigned count;
	unsigned index;
	uint32_t entry;

	/* Takes each entry up to the first zero or the room given. */
	count = 0U;
	for (index = 0U; index < I915_XD_DPCD_EDP_RATE_COUNT; index++) {
		entry = (uint32_t)table[2U * index] | ((uint32_t)table[2U * index + 1U] << 8);
		if (entry == 0U)
			break;
		if (count == room)
			break;

		/* An entry counts 200 kbit/s of a lane; the port clock counts 10 kbit/s. */
		rates[count] = entry * 200U / 10U;
		count++;
	}

	/* Succeeded: reports how many rates were listed. */
	return count;
}

/*
 * Finds the limits of the next try after a link failed to train at a rate
 * and lane count: the next lower shared rate with the same lanes, or
 * below the lowest rate (or at a rate not shared) half the lanes at the
 * highest rate.  Returns 1 with the new limits, or 0 when nothing lower
 * is left.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_get_link_train_fallback_values).
 */
int
drv_i915_xd_dp_fallback(
	const uint32_t *common,
	unsigned count,
	uint32_t rate_khz,
	unsigned lanes,
	uint32_t *max_rate_khz,
	unsigned *max_lanes)
{
	unsigned index;
	int found;

	/* Finds the failed rate among the shared ones. */
	found = -1;
	for (index = 0U; index < count; index++) {
		if (common[index] == rate_khz)
			found = (int)index;
	}

	/* A rate above the lowest falls to the next lower one. */
	if (found > 0) {
		*max_rate_khz = common[found - 1];
		*max_lanes = lanes;
		return 1;
	}

	/* The lowest rate: half the lanes at the highest rate. */
	if (lanes > 1U && count > 0U) {
		*max_rate_khz = common[count - 1U];
		*max_lanes = lanes >> 1;
		return 1;
	}

	/* Succeeded: one lane at the lowest rate leaves nothing to try. */
	return 0;
}

/*
 * Tells the data rate a mode needs (kB/s): the pixel clock times the bits
 * per pixel, in bytes, rounded up.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_link_required).
 */
uint32_t
drv_i915_xd_dp_need_kbps(
	uint32_t clock_khz,
	unsigned bpp)
{
	/* Succeeded: the bits of a second of pixels, in bytes. */
	return i915_xd_dp_div_round_up((uint64_t)clock_khz * bpp, 8U);
}

/*
 * Tells the data rate a link carries (kB/s): the bit rate of its lanes
 * times the share of data in its coding, in bytes, rounded down.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_max_data_rate).
 */
uint32_t
drv_i915_xd_dp_carry_kbps(
	uint32_t rate_khz,
	unsigned lanes)
{
	uint64_t bits;
	uint32_t efficiency;

	/* The share of data: 8b/10b below the 128b/132b rates. */
	efficiency = I915_XD_DP_EFFICIENCY_8B10B;
	if (rate_khz >= I915_XD_DP_RATE_UHBR10)
		efficiency = I915_XD_DP_EFFICIENCY_128B132B;

	/* The rate counts 10 kbit/s per lane. */
	bits = (uint64_t)rate_khz * 10U * lanes * efficiency;

	/* Succeeded: the data share of the bits, in bytes. */
	return (uint32_t)(bits / (1000000U * 8U));
}

/*
 * Tells the data rate of a stream with its allocation overhead (kB/s):
 * the pixel clock times the bits per pixel in sixteenths, scaled by the
 * overhead in parts per million, rounded up.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_effective_data_rate).
 */
uint32_t
drv_i915_xd_dp_stream_kbps(
	uint32_t clock_khz,
	unsigned bpp_x16,
	uint32_t overhead_ppm)
{
	uint64_t bits;

	/* The pixel bits of a second, scaled by the overhead. */
	bits = (uint64_t)((uint32_t)(clock_khz * bpp_x16)) * overhead_ppm;

	/* Succeeded: in bytes, without the sixteenths and the millionths. */
	return i915_xd_dp_div_round_up(bits, 1000000ULL * 16U * 8U);
}

/*
 * Tells the bits of one symbol of a link rate's coding: 10 for 8b/10b,
 * 32 for 128b/132b.
 */
unsigned
drv_i915_xd_dp_symbol_bits(
	uint32_t rate_khz)
{
	/* A 128b/132b rate. */
	if (rate_khz >= I915_XD_DP_RATE_UHBR10)
		return I915_XD_DP_SYMBOL_128B132B;

	/* Succeeded: an 8b/10b rate. */
	return I915_XD_DP_SYMBOL_8B10B;
}

/*
 * Tells the symbol clock of a link rate in kHz, rounded to the nearest.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_link_symbol_clock).
 */
uint32_t
drv_i915_xd_dp_symbol_khz(
	uint32_t rate_khz)
{
	unsigned bits;

	/* The bits of one symbol. */
	bits = drv_i915_xd_dp_symbol_bits(rate_khz);

	/* Succeeded: the rate counts 10 kbit/s; the symbols of that. */
	return (rate_khz * 10U + bits / 2U) / bits;
}

/*
 * Chooses the link that carries a mode: for each depth from max_bpc down
 * to min_bpc in steps of 2, each shared rate from the lowest (up to the
 * rate limit, 0: none), and 1, 2 then 4 lanes (up to the lane limit);
 * the first whose data rate holds the mode's.  Returns 0 with the
 * choice, or ENOSPC when no link carries the mode.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_dp.c
 * (intel_dp_compute_link_config_wide).
 */
int
drv_i915_xd_dp_choose(
	uint32_t clock_khz,
	unsigned max_bpc,
	unsigned min_bpc,
	const uint32_t *common,
	unsigned count,
	uint32_t rate_limit_khz,
	unsigned lane_limit,
	struct i915_xd_dp_choice *choice)
{
	unsigned bpc;
	unsigned index;
	unsigned lanes;
	uint32_t need;
	uint32_t carry;

	/* Tries each depth from the deepest. */
	for (bpc = max_bpc; bpc >= min_bpc && bpc != 0U; bpc -= 2U) {
		need = drv_i915_xd_dp_need_kbps(clock_khz, 3U * bpc);

		/* Tries each shared rate from the lowest, under the limit. */
		for (index = 0U; index < count; index++) {
			if (rate_limit_khz != 0U && common[index] > rate_limit_khz)
				continue;

			/* Tries 1, 2 and 4 lanes. */
			for (lanes = 1U; lanes <= lane_limit; lanes <<= 1) {
				carry = drv_i915_xd_dp_carry_kbps(common[index], lanes);
				if (need > carry)
					continue;

				/* The first link that carries the mode. */
				choice->rate_khz = common[index];
				choice->lanes = lanes;
				choice->bpp = 3U * bpc;
				choice->need_kbps = need;
				choice->carry_kbps = carry;
				return 0;
			}
		}

		/* The depth 1 would wrap below 0. */
		if (bpc < 2U)
			break;
	}

	/* No link carries the mode. */
	return ENOSPC;
}

/*
 * Computes the M/N values of a stream on a link: the data pair is the
 * stream's rate (with its overhead) over the link's payload rate with a
 * fixed N, the link pair the pixel clock over the link's symbol clock
 * with a fixed N; both are halved until they fit the register fields.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/i915/display/intel_display.c
 * (intel_link_compute_m_n, compute_m_n).
 */
void
drv_i915_xd_dp_m_n(
	unsigned bpp_x16,
	unsigned lanes,
	uint32_t clock_khz,
	uint32_t rate_khz,
	uint32_t overhead_ppm,
	struct i915_xd_dp_m_n *m_n)
{
	uint32_t stream;
	uint32_t payload;
	uint32_t symbol;

	/* The stream's rate, the link's payload rate and its symbol clock. */
	stream = drv_i915_xd_dp_stream_kbps(clock_khz, bpp_x16, overhead_ppm);
	payload = drv_i915_xd_dp_carry_kbps(rate_khz, lanes);
	symbol = drv_i915_xd_dp_symbol_khz(rate_khz);

	/* The transfer unit and the two pairs. */
	m_n->tu = I915_XD_DP_TU;
	i915_xd_dp_ratio(stream, payload, I915_XD_DP_DATA_N, &m_n->data_m, &m_n->data_n);
	i915_xd_dp_ratio(clock_khz, symbol, I915_XD_DP_LINK_N, &m_n->link_m, &m_n->link_n);
}

/*
 * Tells whether every lane in use of a link status has recovered the
 * clock.  Returns 1 or 0.
 */
int
drv_i915_xd_dp_clock_recovered(
	const uint8_t *status,
	unsigned lanes)
{
	unsigned lane;
	unsigned bits;

	/* Looks for a lane in use without the clock. */
	for (lane = 0U; lane < lanes; lane++) {
		bits = i915_xd_dp_lane_bits(status, lane);
		if ((bits & I915_XD_DPCD_LANE_CR) == 0U)
			return 0;
	}

	/* Succeeded: every lane in use has the clock. */
	return 1;
}

/*
 * Tells whether a link status shows the channel equalized: the lanes
 * aligned, and every lane in use with the clock, equalized and its
 * symbols locked.  Returns 1 or 0.
 */
int
drv_i915_xd_dp_equalized(
	const uint8_t *status,
	unsigned lanes)
{
	unsigned lane;
	unsigned bits;
	unsigned all;

	/* The lanes must be aligned with each other. */
	if ((status[I915_XD_DPCD_ALIGN - I915_XD_DPCD_LANE_STATUS] & I915_XD_DPCD_ALIGN_DONE) == 0U)
		return 0;

	/* Looks for a lane in use that lacks one of the three. */
	all = I915_XD_DPCD_LANE_CR | I915_XD_DPCD_LANE_EQ | I915_XD_DPCD_LANE_LOCKED;
	for (lane = 0U; lane < lanes; lane++) {
		bits = i915_xd_dp_lane_bits(status, lane);
		if ((bits & all) != all)
			return 0;
	}

	/* Succeeded: the channel is equalized. */
	return 1;
}

/*
 * Reads the link status of the sink (hop 0) or of repeater hop into the
 * sink's six-byte layout; a repeater's status, which has no sink status
 * byte, gets a zero one.  A short read is used as read.  Returns 0, or
 * the positive error of the read.
 */
int
drv_i915_xd_dp_read_status(
	const struct i915_xd_dp_sink *sink,
	unsigned hop,
	uint8_t *status)
{
	int moved;

	/* The sink's status is read in its own layout. */
	if (hop == I915_XD_DP_HOP_SINK) {
		moved = sink->read(sink->context, I915_XD_DPCD_LANE_STATUS, status, I915_XD_DPCD_STATUS_SIZE);
		if (moved < 0)
			return -moved;

		return 0;
	}

	/* A repeater's five bytes lack the sink status; the requests move up one byte. */
	moved = sink->read(sink->context, I915_XD_DPCD_REPEATER_STATUS(hop), status, I915_XD_DP_REPEATER_STATUS_SIZE);
	if (moved < 0)
		return -moved;
	status[5] = status[4];
	status[4] = status[3];
	status[3] = 0U;

	/* Succeeded: the status is in the sink's layout. */
	return 0;
}

/*
 * Trains a link: finds the repeaters (when asked to), reads the sink's
 * capabilities again, lets the source prepare the port, writes the link's
 * rate, lanes and coding to the sink, trains each repeater and then the
 * sink, and lets the source send the idle pattern.  Returns 0 when every
 * receiver was trained, EIO otherwise (the caller decides on a fallback).
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_start_link_train, intel_dp_init_lttpr_and_dprx_caps,
 * intel_dp_prepare_link_train, intel_dp_link_train_all_phys,
 * intel_dp_link_train_phy).
 */
int
drv_i915_xd_dp_train(
	struct i915_xd_dp_link *link)
{
	unsigned hop;
	int trained;
	int error;

	/* The repeaters and the sink's capabilities; a sink that does not answer is trained without repeaters. */
	error = i915_xd_dp_refresh_caps(link);
	if (error != 0)
		link->repeaters = 0U;

	/* The port and the sink are set up for the link. */
	i915_xd_dp_configure(link);

	/* Trains the repeaters from the highest numbered one, each left without its pattern. */
	trained = 1;
	for (hop = link->repeaters; hop > I915_XD_DP_HOP_SINK; hop--) {
		trained = i915_xd_dp_recover_clock(link, hop);
		if (trained)
			trained = i915_xd_dp_equalize(link, hop);
		i915_xd_dp_end_hop(link, hop);
		if (!trained)
			break;
	}

	/* Trains the sink once every repeater is trained. */
	if (trained) {
		trained = i915_xd_dp_recover_clock(link, I915_XD_DP_HOP_SINK);
		if (trained)
			trained = i915_xd_dp_equalize(link, I915_XD_DP_HOP_SINK);
	}

	/* Notes the outcome. */
	if (trained) {
		i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: the link is trained\n");
	} else {
		i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: the link did not train\n");
	}

	/* The source sends the idle pattern. */
	if (link->source.idle != NULL)
		link->source.idle(link->source.context);

	/* Reports a link that did not train. */
	if (!trained)
		return EIO;

	/* Succeeded: every receiver of the link is trained. */
	return 0;
}

/*
 * Ends a training: the sink and then the port leave the training
 * pattern, and the port sends the stream's symbols from now on.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_stop_link_train).
 */
void
drv_i915_xd_dp_train_end(
	struct i915_xd_dp_link *link)
{
	/* The sink, then the port. */
	i915_xd_dp_end_hop(link, I915_XD_DP_HOP_SINK);
	link->source.pattern(link->source.context, (uint8_t)I915_XD_DPCD_PATTERN_NONE);
}

/* Divides, rounding up. */
static uint32_t
i915_xd_dp_div_round_up(
	uint64_t numerator,
	uint64_t denominator)
{
	/* Succeeded: the quotient, one more when a remainder is left. */
	return (uint32_t)((numerator + denominator - 1U) / denominator);
}

/* Hands a note of the training to the caller, when it takes notes. */
static void
i915_xd_dp_note(
	const struct i915_xd_dp_link *link,
	unsigned kind,
	const char *text)
{
	/* A caller without the hook takes no notes. */
	if (link->source.note == NULL)
		return;

	/* Hands the note over. */
	link->source.note(link->source.context, kind, text);
}

/* Waits between two steps of a training: at least the wait, at most twice it. */
static void
i915_xd_dp_sleep(
	const struct i915_xd_dp_link *link,
	unsigned wait_us)
{
	/* The source's sleep. */
	link->source.sleep(link->source.context, wait_us, 2U * wait_us);
}

/*
 * Computes one M/N pair with a fixed N: M keeps the ratio m / n, and both
 * are halved until they fit the register fields.
 */
static void
i915_xd_dp_ratio(
	uint32_t m,
	uint32_t n,
	uint32_t fixed_n,
	uint32_t *ret_m,
	uint32_t *ret_n)
{
	/* M over the fixed N keeps the ratio. */
	*ret_n = fixed_n;
	*ret_m = (uint32_t)(((uint64_t)m * fixed_n) / n);

	/* Drops a bit of both terms at a time until they fit. */
	while (*ret_m > I915_XD_DP_M_N_MAX || *ret_n > I915_XD_DP_M_N_MAX) {
		*ret_m >>= 1;
		*ret_n >>= 1;
	}
}

/* Takes the status nibble of one lane: two lanes share a byte, the odd one in the high nibble. */
static unsigned
i915_xd_dp_lane_bits(
	const uint8_t *status,
	unsigned lane)
{
	uint8_t byte;

	/* The byte of the lane pair. */
	byte = status[lane >> 1];

	/* Succeeded: the lane's nibble. */
	return (unsigned)(byte >> (4U * (lane & 1U))) & 0xfU;
}

/*
 * Takes the levels one lane's receiver asks for, as a level byte: the
 * swing in the low two bits of the lane's nibble, the pre-emphasis in the
 * next two.
 */
static uint8_t
i915_xd_dp_request(
	const uint8_t *status,
	unsigned lane)
{
	uint8_t byte;
	unsigned nibble;
	uint8_t swing;
	uint8_t preemph;

	/* The lane's nibble of the request bytes. */
	byte = status[I915_XD_DPCD_REQUEST - I915_XD_DPCD_LANE_STATUS + (lane >> 1)];
	nibble = (unsigned)(byte >> (4U * (lane & 1U))) & 0xfU;

	/* The two levels at their places in a level byte. */
	swing = (uint8_t)(nibble & 0x3U);
	preemph = (uint8_t)(((nibble >> 2) & 0x3U) << I915_XD_DPCD_PREEMPH_SHIFT);

	/* Succeeded: the requested levels. */
	return (uint8_t)(swing | preemph);
}

/*
 * Decodes the repeater count of the repeaters' common capabilities: one
 * bit set counts 1 (bit 7) to 8 (bit 0) repeaters, none counts 0.
 * Returns the count, or -1 for any other value (more than one bit set).
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c
 * (drm_dp_lttpr_count).
 */
static int
i915_xd_dp_repeater_count(
	const uint8_t *common)
{
	uint8_t bits;
	int count;

	/* No bit: no repeater. */
	bits = common[I915_XD_DPCD_REPEATER_COUNT - I915_XD_DPCD_REPEATER_REV];
	if (bits == 0U)
		return 0;

	/* More than one bit says nothing usable. */
	if ((bits & (uint8_t)(bits - 1U)) != 0U)
		return -1;

	/* The bit counts from the top: bit 7 is one repeater. */
	count = 8;
	while (bits > 1U) {
		bits >>= 1;
		count--;
	}

	/* Succeeded: the count. */
	return count;
}

/*
 * Reads a run of the repeaters' registers: a byte at a time from a sink
 * older than DPCD 1.4 (at least one monitor answers wrong bytes to longer
 * reads of that range), otherwise in one read.  A short read is used as
 * read.  *error is set to the positive error of a failed read.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c
 * (drm_dp_read_lttpr_regs).
 */
static void
i915_xd_dp_read_repeater_caps(
	struct i915_xd_dp_link *link,
	const uint8_t *caps,
	uint32_t address,
	uint8_t *buffer,
	unsigned size,
	int *error)
{
	unsigned block;
	unsigned offset;
	int moved;

	/* The size of one read. */
	block = size;
	if (caps[I915_XD_DPCD_REV] < I915_XD_DPCD_REV_1_4)
		block = 1U;

	/* Reads the run block by block. */
	*error = 0;
	for (offset = 0U; offset < size; offset += block) {
		moved = link->sink.read(link->sink.context, address + offset, buffer + offset, block);
		if (moved < 0) {
			*error = -moved;
			return;
		}
	}
}

/*
 * Finds the repeaters in front of the sink and switches them to
 * non-transparent mode, so each is trained on its own; reads the
 * capabilities of each.  A link that is up keeps the mode its repeaters
 * are in (changing it would drop the link).  Returns how many repeaters
 * are to be trained: 0 for none, for transparent ones, for an unusable
 * count, or when the switch was refused.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_init_lttpr, intel_dp_init_lttpr_phys,
 * intel_dp_read_lttpr_common_caps, intel_dp_read_lttpr_phy_caps).
 */
static unsigned
i915_xd_dp_find_repeaters(
	struct i915_xd_dp_link *link,
	const uint8_t *caps)
{
	uint8_t *common;
	unsigned index;
	int count;
	int error;
	int switched;

	/* The common capabilities; unreadable ones or a revision below 1.4 are forgotten. */
	common = link->repeater_caps;
	i915_xd_dp_read_repeater_caps(link, caps, I915_XD_DPCD_REPEATER_REV, common, I915_XD_DPCD_REPEATER_COMMON_SIZE, &error);
	if (error != 0 || common[0] < I915_XD_DPCD_REPEATER_REV_1_4) {
		for (index = 0U; index < I915_XD_DPCD_REPEATER_COMMON_SIZE; index++)
			common[index] = 0U;
		return 0U;
	}

	/* No repeater: the mode is left alone (a forced transparent mode breaks at least one dock). */
	count = i915_xd_dp_repeater_count(common);
	if (count == 0)
		return 0U;

	/* A link that is up keeps its repeaters' mode: only non-transparent ones are trained. */
	if (link->active) {
		if (count < 0 ||
		    common[I915_XD_DPCD_REPEATER_MODE - I915_XD_DPCD_REPEATER_REV] == I915_XD_DPCD_REPEATER_TRANSPARENT) {
			common[I915_XD_DPCD_REPEATER_COUNT - I915_XD_DPCD_REPEATER_REV] = 0U;
			return 0U;
		}
	} else {
		/* The standard asks for transparent mode before non-transparent; an unusable count stops there. */
		(void)i915_xd_dp_set_repeater_mode(link, (uint8_t)I915_XD_DPCD_REPEATER_TRANSPARENT);
		if (count < 0)
			return 0U;

		/* Non-transparent mode; a refusal goes back to transparent mode and trains no repeater. */
		switched = i915_xd_dp_set_repeater_mode(link, (uint8_t)I915_XD_DPCD_REPEATER_OPAQUE);
		if (!switched) {
			i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: the repeaters stay transparent\n");
			(void)i915_xd_dp_set_repeater_mode(link, (uint8_t)I915_XD_DPCD_REPEATER_TRANSPARENT);
			common[I915_XD_DPCD_REPEATER_COUNT - I915_XD_DPCD_REPEATER_REV] = 0U;
			return 0U;
		}
	}

	/* The capabilities of each repeater; one that cannot be read is trained with what was there. */
	for (index = 1U; index <= (unsigned)count; index++) {
		i915_xd_dp_read_repeater_caps(link, caps, I915_XD_DPCD_REPEATER_INTERVAL(index),
			link->repeater_phy_caps[index - 1U], I915_XD_DPCD_REPEATER_CAP_SIZE, &error);
		if (error != 0)
			i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: a repeater's capabilities could not be read\n");
	}

	/* Succeeded: the repeaters to train. */
	return (unsigned)count;
}

/* Writes the repeaters' mode.  Returns 1 when the sink took it, 0 otherwise. */
static int
i915_xd_dp_set_repeater_mode(
	struct i915_xd_dp_link *link,
	uint8_t mode)
{
	int moved;

	/* The mode register. */
	moved = link->sink.write(link->sink.context, I915_XD_DPCD_REPEATER_MODE, &mode, 1U);
	if (moved != 1)
		return 0;

	/* Succeeded: the sink took the mode. */
	return 1;
}

/*
 * Looks for repeaters (when the link asks for it) on a fresh read of the
 * sink's capabilities, then reads the capabilities again into the link's
 * storage, as the standard asks after the repeaters are found.  Returns
 * 0, or EIO when the sink did not answer (the repeaters then count 0;
 * when the last read failed the repeaters' capabilities are forgotten).
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_init_lttpr_and_dprx_caps).
 */
static int
i915_xd_dp_refresh_caps(
	struct i915_xd_dp_link *link)
{
	uint8_t fresh[I915_XD_DPCD_CAPS_SIZE];
	unsigned found;
	unsigned index;
	int error;

	/* Repeaters are looked for on a sink that answers. */
	found = 0U;
	if (link->find_repeaters) {
		error = link->sink.probe(link->sink.context, I915_XD_DPCD_REPEATER_REV);
		if (error != 0)
			return EIO;
		error = link->sink.read_caps(link->sink.context, fresh);
		if (error != 0)
			return EIO;
		found = i915_xd_dp_find_repeaters(link, fresh);
	}

	/* The capabilities again, into the link's storage. */
	error = link->sink.read_caps(link->sink.context, link->caps);
	if (error != 0) {
		for (index = 0U; index < I915_XD_DPCD_REPEATER_COMMON_SIZE; index++)
			link->repeater_caps[index] = 0U;
		return EIO;
	}

	/* Succeeded: the repeaters to train. */
	link->repeaters = found;
	return 0;
}

/*
 * Sets up the port and the sink for the link: the source's preparation,
 * the rate list of an eDP 1.4 panel read again (a common eDP mux forgets
 * the rates it snooped when it powers down), the coding and the timing
 * attribute bit, then the rate and the lanes.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_prepare_link_train, intel_dp_update_downspread_ctrl,
 * intel_dp_update_link_bw_set).
 */
static void
i915_xd_dp_configure(
	struct i915_xd_dp_link *link)
{
	uint8_t table[2U * I915_XD_DPCD_EDP_RATE_COUNT];
	uint8_t bytes[2];
	uint8_t lanes;
	uint8_t code;

	/* The source prepares the port (the DDI is enabled here). */
	if (link->source.prepare != NULL)
		link->source.prepare(link->source.context);

	/* An eDP 1.4 panel set by its rate list reads the list again; the bytes are not used. */
	code = 0U;
	if (link->rate_index < 0)
		code = drv_i915_xd_dp_rate_code(link->rate_khz);
	if (code == 0U)
		(void)link->sink.read(link->sink.context, I915_XD_DPCD_EDP_RATES, table, sizeof(table));

	/* The timing attribute bit and the 8b/10b coding, before the rate. */
	bytes[0] = 0U;
	if (link->msa_ignore)
		bytes[0] = (uint8_t)I915_XD_DPCD_MSA_IGNORE;
	bytes[1] = (uint8_t)I915_XD_DPCD_CODING_8B10B;
	(void)link->sink.write(link->sink.context, I915_XD_DPCD_DOWNSPREAD, bytes, 2U);

	/* The lanes, with the enhanced framing bit. */
	lanes = (uint8_t)link->lanes;
	if (link->enhanced)
		lanes |= (uint8_t)I915_XD_DPCD_LANES_ENHANCED;

	/*
	 * The rate by its code with the lanes in one write, or by its index
	 * (an eDP 1.4 sink ignores the index once a code is set, so no code
	 * is written).
	 */
	if (code != 0U) {
		bytes[0] = code;
		bytes[1] = lanes;
		(void)link->sink.write(link->sink.context, I915_XD_DPCD_BW_SET, bytes, 2U);
	} else {
		bytes[0] = 0U;
		if (link->rate_index >= 0)
			bytes[0] = (uint8_t)link->rate_index;
		(void)link->sink.write(link->sink.context, I915_XD_DPCD_LANES_SET, &lanes, 1U);
		(void)link->sink.write(link->sink.context, I915_XD_DPCD_RATE_SET, bytes, 1U);
	}
}

/*
 * Tells whether the source drives a receiver directly: the sink when
 * there are no repeaters, otherwise the highest numbered repeater.
 * Every other receiver is driven by the repeater numbered one above it.
 */
static int
i915_xd_dp_nearest(
	const struct i915_xd_dp_link *link,
	unsigned hop)
{
	/* Without repeaters the source drives the sink. */
	if (link->repeaters == 0U)
		return 1;

	/* The highest numbered repeater. */
	if (hop == link->repeaters)
		return 1;

	/* Succeeded: a repeater drives this receiver. */
	return 0;
}

/*
 * Finds the wait between two steps of a phase on a receiver: clock
 * recovery waits the fixed 100 us on a repeater and on a DPCD 1.4 sink,
 * otherwise the sink's read interval; channel equalization waits the read
 * interval (the sink's from its capabilities, a repeater's read from it,
 * 400 us when it cannot be read).  An interval of 0 is the phase's
 * default, each step of it 4 ms.
 *
 * Procedure referenced: Linux v6.8.12 drivers/gpu/drm/display/drm_dp_helper.c
 * (__read_delay, drm_dp_read_clock_recovery_delay,
 * drm_dp_read_channel_eq_delay).
 */
static unsigned
i915_xd_dp_wait(
	struct i915_xd_dp_link *link,
	unsigned hop,
	int equalize)
{
	uint8_t interval;
	int moved;

	/* Clock recovery on a repeater or a DPCD 1.4 sink waits the fixed time. */
	if (!equalize) {
		if (hop != I915_XD_DP_HOP_SINK)
			return I915_XD_DP_CR_WAIT_US;
		if (link->caps[I915_XD_DPCD_REV] >= I915_XD_DPCD_REV_1_4)
			return I915_XD_DP_CR_WAIT_US;
	}

	/* The interval: the sink's capability, or a repeater's register. */
	if (hop == I915_XD_DP_HOP_SINK) {
		interval = link->caps[I915_XD_DPCD_READ_INTERVAL];
	} else {
		moved = link->sink.read(link->sink.context, I915_XD_DPCD_REPEATER_INTERVAL(hop), &interval, 1U);
		if (moved != 1)
			return I915_XD_DP_EQ_WAIT_US;
	}

	/* An interval beyond the named ones is noted and used. */
	interval &= (uint8_t)I915_XD_DPCD_READ_INTERVAL_MASK;
	if (interval > I915_XD_DP_INTERVAL_TOP)
		i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: the sink's read interval is out of range\n");

	/* No interval is the phase's default. */
	if (interval == 0U) {
		if (equalize)
			return I915_XD_DP_EQ_WAIT_US;
		return I915_XD_DP_CR_WAIT_US;
	}

	/* Succeeded: the interval counts 4 ms. */
	return (unsigned)interval * I915_XD_DP_INTERVAL_US;
}

/*
 * Limits the levels a receiver asks for to what its transmitter drives:
 * the pre-emphasis to the transmitter's highest (marked as the top), the
 * swing to what that pre-emphasis leaves (swing and pre-emphasis add up
 * to 3 at most) and to the transmitter's highest (marked as the top).
 * The transmitter is the source for the receiver it drives directly,
 * otherwise the repeater numbered one above (level 3 when its
 * capabilities say so, else 2).
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_get_lane_adjust_vswing_preemph, dp_voltage_max,
 * intel_dp_phy_voltage_max, intel_dp_phy_preemph_max).
 */
static uint8_t
i915_xd_dp_limit(
	const struct i915_xd_dp_link *link,
	unsigned hop,
	uint8_t swing,
	uint8_t preemph)
{
	uint8_t swing_max;
	uint8_t preemph_max;
	uint8_t room;
	uint8_t capability;
	int nearest;

	/* The transmitter's highest levels. */
	nearest = i915_xd_dp_nearest(link, hop);
	if (nearest) {
		swing_max = link->swing_max;
		preemph_max = (uint8_t)(link->preemph_max << I915_XD_DPCD_PREEMPH_SHIFT);
	} else {
		capability = link->repeater_phy_caps[hop][I915_XD_DPCD_REPEATER_TX_CAP - I915_XD_DPCD_REPEATER_INTERVAL(1U)];
		swing_max = 2U;
		if ((capability & I915_XD_DPCD_REPEATER_SWING_3) != 0U)
			swing_max = 3U;
		preemph_max = (uint8_t)(2U << I915_XD_DPCD_PREEMPH_SHIFT);
		if ((capability & I915_XD_DPCD_REPEATER_PREEMPH_3) != 0U)
			preemph_max = (uint8_t)(3U << I915_XD_DPCD_PREEMPH_SHIFT);
	}

	/* The pre-emphasis at most the transmitter's, marked when it is there. */
	if (preemph >= preemph_max)
		preemph = (uint8_t)(preemph_max | I915_XD_DPCD_PREEMPH_TOP);

	/* The swing at most what the pre-emphasis leaves. */
	room = (uint8_t)(3U - ((preemph & I915_XD_DPCD_PREEMPH) >> I915_XD_DPCD_PREEMPH_SHIFT));
	if (swing > room)
		swing = room;

	/* The swing at most the transmitter's, marked when it is there. */
	if (swing >= swing_max)
		swing = (uint8_t)(swing_max | I915_XD_DPCD_SWING_TOP);

	/* Succeeded: the lane's level byte. */
	return (uint8_t)(swing | preemph);
}

/*
 * Follows the levels a receiver asks for: each lane takes its own request
 * when its transmitter sets the lanes one by one (lanes beyond the link
 * take the last lane's), otherwise every lane the highest request of the
 * lanes in use; then the transmitter's limits.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_get_adjust_train, has_per_lane_signal_levels).
 */
static void
i915_xd_dp_follow(
	struct i915_xd_dp_link *link,
	unsigned hop,
	const uint8_t *status)
{
	uint8_t request;
	uint8_t swing;
	uint8_t preemph;
	unsigned lane;
	unsigned asked;
	int per_lane;
	int nearest;

	/* A repeater sets its lanes one by one; the source when it says so. */
	per_lane = 1;
	nearest = i915_xd_dp_nearest(link, hop);
	if (nearest)
		per_lane = link->per_lane;

	/* The highest request of the lanes in use, for a transmitter that sets all lanes alike. */
	swing = 0U;
	preemph = 0U;
	if (!per_lane) {
		for (lane = 0U; lane < link->lanes; lane++) {
			request = i915_xd_dp_request(status, lane);
			if ((request & I915_XD_DPCD_SWING) > swing)
				swing = (uint8_t)(request & I915_XD_DPCD_SWING);
			if ((request & I915_XD_DPCD_PREEMPH) > preemph)
				preemph = (uint8_t)(request & I915_XD_DPCD_PREEMPH);
		}
	}

	/* The level byte of each of the four lanes. */
	for (lane = 0U; lane < 4U; lane++) {
		if (per_lane) {
			asked = lane;
			if (asked > link->lanes - 1U)
				asked = link->lanes - 1U;
			request = i915_xd_dp_request(status, asked);
			swing = (uint8_t)(request & I915_XD_DPCD_SWING);
			preemph = (uint8_t)(request & I915_XD_DPCD_PREEMPH);
		}

		link->set[lane] = i915_xd_dp_limit(link, hop, swing, preemph);
	}
}

/*
 * Sends a training pattern: the source's port first (its levels and its
 * pattern), then the receiver's pattern register with the level byte of
 * each lane in one write.  Returns 1 when the sink took every byte, 0
 * otherwise.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_set_link_train, intel_dp_program_link_training_pattern).
 */
static int
i915_xd_dp_send_pattern(
	struct i915_xd_dp_link *link,
	unsigned hop,
	uint8_t pattern)
{
	uint8_t bytes[5];
	uint32_t address;
	unsigned lane;
	int moved;

	/* The port's pattern. */
	link->source.pattern(link->source.context, pattern);

	/* The receiver's pattern register, the lanes' levels after it. */
	address = I915_XD_DPCD_PATTERN_SET;
	if (hop != I915_XD_DP_HOP_SINK)
		address = I915_XD_DPCD_REPEATER_PATTERN(hop);
	bytes[0] = pattern;
	for (lane = 0U; lane < link->lanes; lane++)
		bytes[1U + lane] = link->set[lane];
	moved = link->sink.write(link->sink.context, address, bytes, link->lanes + 1U);
	if (moved != (int)(link->lanes + 1U))
		return 0;

	/* Succeeded: the receiver took the pattern and the levels. */
	return 1;
}

/*
 * Sends the lanes' levels: the source's port when it drives the receiver,
 * then the receiver's level registers.  Returns 1 when the sink took
 * every lane, 0 otherwise.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_update_link_train, intel_dp_set_signal_levels).
 */
static int
i915_xd_dp_send_levels(
	struct i915_xd_dp_link *link,
	unsigned hop)
{
	uint32_t address;
	int moved;
	int nearest;

	/* The port's levels, when it drives this receiver. */
	nearest = i915_xd_dp_nearest(link, hop);
	if (nearest)
		link->source.levels(link->source.context, link->set);

	/* The receiver's level registers. */
	address = I915_XD_DPCD_LEVELS_SET;
	if (hop != I915_XD_DP_HOP_SINK)
		address = I915_XD_DPCD_REPEATER_LEVELS(hop);
	moved = link->sink.write(link->sink.context, address, link->set, link->lanes);
	if (moved != (int)link->lanes)
		return 0;

	/* Succeeded: the receiver took every lane. */
	return 1;
}

/* Tells whether the requests of a lane in use changed between two statuses.  Returns 1 or 0. */
static int
i915_xd_dp_requests_differ(
	const struct i915_xd_dp_link *link,
	const uint8_t *before,
	const uint8_t *now)
{
	unsigned lane;
	uint8_t old_request;
	uint8_t new_request;

	/* Compares each lane in use. */
	for (lane = 0U; lane < link->lanes; lane++) {
		old_request = i915_xd_dp_request(before, lane);
		new_request = i915_xd_dp_request(now, lane);
		if (old_request != new_request)
			return 1;
	}

	/* Succeeded: the receiver asks for the same levels. */
	return 0;
}

/*
 * Tells whether every lane in use is at the top of the levels: its swing
 * marked as the transmitter's highest, and swing and pre-emphasis adding
 * up to 3.  Returns 1 or 0.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_link_max_vswing_reached, intel_dp_lane_max_vswing_reached).
 */
static int
i915_xd_dp_at_top(
	const struct i915_xd_dp_link *link)
{
	unsigned lane;
	unsigned levels;

	/* Looks for a lane in use below the top. */
	for (lane = 0U; lane < link->lanes; lane++) {
		if ((link->set[lane] & I915_XD_DPCD_SWING_TOP) == 0U)
			return 0;
		levels = (unsigned)(link->set[lane] & I915_XD_DPCD_SWING) +
			 ((unsigned)(link->set[lane] & I915_XD_DPCD_PREEMPH) >> I915_XD_DPCD_PREEMPH_SHIFT);
		if (levels != 3U)
			return 0;
	}

	/* Succeeded: every lane is at the top. */
	return 1;
}

/*
 * Recovers the clock on a receiver: pattern 1 without scrambling from the
 * lowest levels, then after each wait the receiver's status, following
 * its requests until every lane has the clock.  It gives up when the same
 * requests came 5 times in a row, when the lanes were already at the top,
 * after 10 tries on a DPCD 1.4 sink (80 on an older one, which the
 * standard left without a limit), or when the sink stops answering.
 * Returns 1 when the clock is recovered, 0 otherwise.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_link_training_clock_recovery, intel_dp_reset_link_train).
 */
static int
i915_xd_dp_recover_clock(
	struct i915_xd_dp_link *link,
	unsigned hop)
{
	uint8_t status[I915_XD_DPCD_STATUS_SIZE];
	uint8_t before[I915_XD_DPCD_STATUS_SIZE];
	unsigned wait_us;
	unsigned tries;
	unsigned limit;
	unsigned same;
	unsigned lane;
	int topped;
	int sent;
	int error;
	int differ;
	int nearest;
	int done;

	/* The wait between the steps. */
	wait_us = i915_xd_dp_wait(link, hop, 0);

	/* Starts from the lowest levels with pattern 1. */
	for (lane = 0U; lane < 4U; lane++)
		link->set[lane] = 0U;
	nearest = i915_xd_dp_nearest(link, hop);
	if (nearest)
		link->source.levels(link->source.context, link->set);
	sent = i915_xd_dp_send_pattern(link, hop, (uint8_t)(I915_XD_DPCD_PATTERN_1 | I915_XD_DPCD_PATTERN_NO_SCRAMBLE));
	if (!sent) {
		i915_xd_dp_note(link, I915_XD_DP_NOTE_ERROR, "xd-dp: the sink did not take pattern 1\n");
		return 0;
	}

	/* The tries a sink of its revision is given. */
	limit = I915_XD_DP_CR_TRIES_OLD;
	if (link->caps[I915_XD_DPCD_REV] >= I915_XD_DPCD_REV_1_4)
		limit = I915_XD_DP_CR_TRIES;

	/* Follows the receiver's requests until the clock is recovered. */
	for (lane = 0U; lane < I915_XD_DPCD_STATUS_SIZE; lane++)
		before[lane] = 0U;
	same = 1U;
	topped = 0;
	for (tries = 0U; tries < limit; tries++) {
		i915_xd_dp_sleep(link, wait_us);

		/* The receiver's status. */
		error = drv_i915_xd_dp_read_status(&link->sink, hop, status);
		if (error != 0) {
			i915_xd_dp_note(link, I915_XD_DP_NOTE_ERROR, "xd-dp: the link status could not be read\n");
			return 0;
		}

		/* The clock on every lane ends the phase. */
		done = drv_i915_xd_dp_clock_recovered(status, link->lanes);
		if (done)
			break;

		/* The same requests five times, or nothing higher to give: the phase has failed. */
		if (same == I915_XD_DP_SAME_TRIES) {
			i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: the same levels were asked for 5 times\n");
			return 0;
		}
		if (topped) {
			i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: the levels are at the top\n");
			return 0;
		}

		/* The requested levels. */
		i915_xd_dp_follow(link, hop, status);
		sent = i915_xd_dp_send_levels(link, hop);
		if (!sent) {
			i915_xd_dp_note(link, I915_XD_DP_NOTE_ERROR, "xd-dp: the sink did not take the levels\n");
			return 0;
		}

		/* Counts requests that did not change. */
		differ = i915_xd_dp_requests_differ(link, before, status);
		if (differ) {
			same = 1U;
		} else {
			same++;
		}
		for (lane = 0U; lane < I915_XD_DPCD_STATUS_SIZE; lane++)
			before[lane] = status[lane];

		/* Remembers lanes that cannot go higher. */
		done = i915_xd_dp_at_top(link);
		if (done)
			topped = 1;
	}

	/* A receiver that never recovered the clock. */
	if (tries == limit) {
		i915_xd_dp_note(link, I915_XD_DP_NOTE_ERROR, "xd-dp: the clock was not recovered\n");
		return 0;
	}

	/* Succeeded: every lane has the clock. */
	return 1;
}

/*
 * Picks the pattern of channel equalization: pattern 4 when both ends
 * know it (a repeater always does), else pattern 3 when both know it,
 * else pattern 2.  Patterns 2 and 3 are sent without scrambling.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_training_pattern).
 */
static uint8_t
i915_xd_dp_eq_pattern(
	const struct i915_xd_dp_link *link,
	unsigned hop)
{
	int sink_tps3;
	int sink_tps4;

	/* What the receiver knows: a repeater both, the sink what its capabilities of DPCD 1.2 and 1.4 say. */
	sink_tps3 = 1;
	sink_tps4 = 1;
	if (hop == I915_XD_DP_HOP_SINK) {
		sink_tps3 = 0;
		if (link->caps[I915_XD_DPCD_REV] >= I915_XD_DPCD_REV_1_2 &&
		    (link->caps[I915_XD_DPCD_MAX_LANES] & I915_XD_DPCD_TPS3) != 0U)
			sink_tps3 = 1;
		sink_tps4 = 0;
		if (link->caps[I915_XD_DPCD_REV] >= I915_XD_DPCD_REV_1_4 &&
		    (link->caps[I915_XD_DPCD_MAX_DOWNSPREAD] & I915_XD_DPCD_TPS4) != 0U)
			sink_tps4 = 1;
	}

	/* Pattern 4, scrambled. */
	if (link->tps4 && sink_tps4)
		return (uint8_t)I915_XD_DPCD_PATTERN_4;

	/* Pattern 3. */
	if (link->tps3 && sink_tps3)
		return (uint8_t)(I915_XD_DPCD_PATTERN_3 | I915_XD_DPCD_PATTERN_NO_SCRAMBLE);

	/* Succeeded: pattern 2, which every receiver knows. */
	return (uint8_t)(I915_XD_DPCD_PATTERN_2 | I915_XD_DPCD_PATTERN_NO_SCRAMBLE);
}

/*
 * Equalizes the channel on a receiver: the equalization pattern with the
 * levels clock recovery left, then after each wait the receiver's status,
 * following its requests until the channel is equalized, at most 5
 * times.  A lane that loses the clock, or a sink that stops answering,
 * ends it.  Returns 1 when the channel is equalized, 0 otherwise.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp_link_training.c
 * (intel_dp_link_training_channel_equalization).
 */
static int
i915_xd_dp_equalize(
	struct i915_xd_dp_link *link,
	unsigned hop)
{
	uint8_t status[I915_XD_DPCD_STATUS_SIZE];
	unsigned wait_us;
	unsigned tries;
	uint8_t pattern;
	int sent;
	int error;
	int done;

	/* The wait between the steps, then the pattern. */
	wait_us = i915_xd_dp_wait(link, hop, 1);
	pattern = i915_xd_dp_eq_pattern(link, hop);
	sent = i915_xd_dp_send_pattern(link, hop, pattern);
	if (!sent) {
		i915_xd_dp_note(link, I915_XD_DP_NOTE_ERROR, "xd-dp: the sink did not take the equalization pattern\n");
		return 0;
	}

	/* Follows the receiver's requests until the channel is equalized. */
	for (tries = 0U; tries < I915_XD_DP_EQ_TRIES; tries++) {
		i915_xd_dp_sleep(link, wait_us);

		/* The receiver's status. */
		error = drv_i915_xd_dp_read_status(&link->sink, hop, status);
		if (error != 0) {
			i915_xd_dp_note(link, I915_XD_DP_NOTE_ERROR, "xd-dp: the link status could not be read\n");
			return 0;
		}

		/* A lane that lost the clock ends the phase. */
		done = drv_i915_xd_dp_clock_recovered(status, link->lanes);
		if (!done) {
			i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: a lane lost the clock during equalization\n");
			return 0;
		}

		/* An equalized channel ends the training of the receiver. */
		done = drv_i915_xd_dp_equalized(status, link->lanes);
		if (done)
			return 1;

		/* The requested levels. */
		i915_xd_dp_follow(link, hop, status);
		sent = i915_xd_dp_send_levels(link, hop);
		if (!sent) {
			i915_xd_dp_note(link, I915_XD_DP_NOTE_ERROR, "xd-dp: the sink did not take the levels\n");
			return 0;
		}
	}

	/* Five tries without an equalized channel. */
	i915_xd_dp_note(link, I915_XD_DP_NOTE_DEBUG, "xd-dp: the channel was not equalized\n");
	return 0;
}

/* Takes a receiver out of the training pattern. */
static void
i915_xd_dp_end_hop(
	struct i915_xd_dp_link *link,
	unsigned hop)
{
	uint8_t none;
	uint32_t address;

	/* The receiver's pattern register. */
	none = (uint8_t)I915_XD_DPCD_PATTERN_NONE;
	address = I915_XD_DPCD_PATTERN_SET;
	if (hop != I915_XD_DP_HOP_SINK)
		address = I915_XD_DPCD_REPEATER_PATTERN(hop);
	(void)link->sink.write(link->sink.context, address, &none, 1U);
}

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP link of a Gen9 DDI port (WS237 stage 3, see gen9-link.h).
 *
 * The order is the Linux kernel 6.8.12's for display 9 (MIT):
 * hsw_prepare_dp_ddi_buffers() and hsw_set_signal_levels() with
 * skl_ddi_set_iboost() for the buffers; intel_ddi_prepare_link_retrain()
 * for the transport; intel_dp_prepare_link_train() (DOWNSPREAD_CTRL,
 * LINK_BW_SET), intel_dp_link_training_clock_recovery() and
 * intel_dp_link_training_channel_equalization() for the training, with
 * TPS3 when the sink takes it; intel_ddi_set_idle_link_train() and
 * intel_dp_stop_link_train() after it.  LTTPRs are left in their
 * transparent mode.  The code is this file's own.
 */

#include "gen9-link.h"
#include "gen9-ddc.h"
#include <kern/kcrt.h>

#include "../intel/display-regs-gen9.h"
#include "../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>

/* How long the buffer takes to leave idle on display 9 (> 518 us), and to become idle. */
#define I915_SKL_BUF_ACTIVE_US		600U
#define I915_SKL_BUF_IDLE_US		8U

/* How long the idle patterns may take to go out. */
#define I915_SKL_IDLE_DONE_US		1000U

/* The DP_TP_STATUS bit of the idle patterns' end. */
#define I915_SKL_DP_TP_STATUS_IDLE_DONE	(1U << 25)

/* The clock recovery tries: 10 for DP 1.4 sinks, 80 for older ones; five at one swing. */
#define I915_SKL_CR_TRIES_14		10U
#define I915_SKL_CR_TRIES_OLD		80U
#define I915_SKL_CR_SAME_TRIES		5U

/* The channel equalisation tries. */
#define I915_SKL_EQ_TRIES		5U

/* How many times the sink's power state is written while it wakes. */
#define I915_SKL_POWER_TRIES		3U

/* The DPCD revision from which a sink has a power state, and DP 1.4's. */
#define I915_SKL_DPCD_REV_11		0x11U
#define I915_SKL_DPCD_REV_14		0x14U

static void i915_skl_set_pattern(const struct i915_skl_io *io, unsigned port, uint32_t pattern);
static int i915_skl_dpcd_pattern(const struct i915_skl_io *io, struct i915_skl_dp_port *dp, uint8_t pattern, unsigned lanes);
static int i915_skl_dpcd_lanes(const struct i915_skl_io *io, struct i915_skl_dp_port *dp, unsigned lanes);
static int i915_skl_clock_recovery(const struct i915_skl_io *io, struct i915_skl_dp_port *dp, unsigned lanes);
static int i915_skl_channel_eq(const struct i915_skl_io *io, struct i915_skl_dp_port *dp, unsigned lanes);
static int i915_skl_max_vswing_reached(uint8_t train_set);
static int i915_skl_adjust_changed(const uint8_t *before, const uint8_t *after, unsigned lanes);

/* Writes the buffer translations of a port's table (hsw_prepare_dp_ddi_buffers). */
void
drv_i915_skl_ddi_buffers(
	const struct i915_skl_io *io,
	const struct i915_skl_dp_port *dp)
{
	uint32_t base;
	unsigned index;

	base = I915_SKL_DDI_BUF_TRANS_A + dp->port * I915_SKL_DDI_BUF_TRANS_STRIDE;

	/* Each entry's two words. */
	for (index = 0U; index < dp->trans_count; index++) {
		drv_i915_skl_write(io, base + index * 8U, dp->trans[index].trans1);
		drv_i915_skl_write(io, base + index * 8U + 4U, dp->trans[index].trans2);
	}
}

/*
 * Selects a table entry on the port (hsw_set_signal_levels): the entry's
 * current boost into DISPIO_CR_TX_BMU_CR0 (a balance leg, or the leg off),
 * and the swing select of the buffer control.
 */
void
drv_i915_skl_ddi_levels(
	const struct i915_skl_io *io,
	struct i915_skl_dp_port *dp,
	unsigned level)
{
	uint32_t boost;
	uint32_t buf_ctl;

	/* An entry past the table is the last one. */
	if (level >= dp->trans_count)
		level = dp->trans_count - 1U;

	/* The entry's boost: 1, 3 or 7 sets the leg, 0 turns it off (_skl_ddi_set_iboost). */
	boost = dp->trans[level].iboost;
	if (boost == 0U) {
		(void)drv_i915_skl_rmw(io, I915_SKL_DISPIO_CR_TX_BMU_CR0, I915_SKL_BALANCE_LEG_MASK(dp->port), I915_SKL_BALANCE_LEG_DISABLE(dp->port));
	} else if (boost == 1U || boost == 3U || boost == 7U) {
		(void)drv_i915_skl_rmw(io, I915_SKL_DISPIO_CR_TX_BMU_CR0,
		    I915_SKL_BALANCE_LEG_MASK(dp->port) | I915_SKL_BALANCE_LEG_DISABLE(dp->port),
		    boost << I915_SKL_BALANCE_LEG_SHIFT(dp->port));
	}

	/* The swing select of the buffer control. */
	dp->buf_ctl &= ~I915_SKL_DDI_BUF_EMP_MASK;
	dp->buf_ctl |= (uint32_t)level << I915_SKL_DDI_BUF_TRANS_SELECT_SHIFT;
	buf_ctl = I915_SKL_DDI_BUF_CTL_A + dp->port * I915_SKL_PORT_STRIDE;
	drv_i915_skl_write(io, buf_ctl, dp->buf_ctl);
	(void)drv_i915_skl_read(io, buf_ctl);
}

/*
 * Trains the link at a rate and lane count and leaves it sending normal
 * pixels.  The buffer control word must hold the port's width and saved
 * bits; the transport and the buffer are started here.  Returns 0, or EIO
 * when the training failed (the port is left training, for the caller to
 * stop or try again).
 */
int
drv_i915_skl_dp_train(
	const struct i915_skl_io *io,
	struct i915_skl_dp_port *dp,
	const struct i915_skl_link *link)
{
	uint8_t config[2];
	uint32_t tp_ctl;
	uint32_t tp_status;
	uint32_t buf_ctl;
	uint32_t value;
	int enhanced;
	int error;

	tp_ctl = I915_SKL_DP_TP_CTL_A + dp->port * I915_SKL_PORT_STRIDE;
	tp_status = I915_SKL_DP_TP_STATUS_A + dp->port * I915_SKL_PORT_STRIDE;
	buf_ctl = I915_SKL_DDI_BUF_CTL_A + dp->port * I915_SKL_PORT_STRIDE;

	/* Whether the sink takes enhanced framing. */
	enhanced = 0;
	if ((dp->dpcd[I915_DPCD_MAX_LANE_COUNT] & I915_DPCD_ENHANCED_FRAME_CAP) != 0U)
		enhanced = 1;

	/* A running transport stops first, with the buffer, which then goes idle (intel_ddi_prepare_link_retrain). */
	value = drv_i915_skl_read(io, tp_ctl);
	if ((value & I915_SKL_DP_TP_ENABLE) != 0U)
		drv_i915_skl_ddi_buffer_off(io, dp->port);

	/* The transport sends pattern 1 in single-stream mode. */
	value = I915_SKL_DP_TP_ENABLE | I915_SKL_DP_TP_TRAIN_PAT1;
	if (enhanced)
		value |= I915_SKL_DP_TP_ENHANCED_FRAME;
	drv_i915_skl_write(io, tp_ctl, value);
	(void)drv_i915_skl_read(io, tp_ctl);

	/* The buffer on, and the time it takes to leave idle. */
	dp->buf_ctl |= I915_SKL_DDI_BUF_ENABLE;
	drv_i915_skl_write(io, buf_ctl, dp->buf_ctl);
	(void)drv_i915_skl_read(io, buf_ctl);
	io->udelay(io->context, I915_SKL_BUF_ACTIVE_US);

	/* The sink's link: no downspread, 8b/10b, then the rate and the lanes. */
	config[0] = 0U;
	config[1] = I915_DPCD_SET_ANSI_8B10B;
	error = drv_i915_skl_dpcd_write(io, dp->port, I915_DPCD_DOWNSPREAD_CTRL, config, 2U);
	if (error != 0)
		return EIO;
	config[0] = drv_i915_skl_dpcd_rate_code(link->port_khz);
	config[1] = (uint8_t)link->lanes;
	if (enhanced)
		config[1] |= I915_DPCD_LANE_COUNT_ENHANCED_FRAME_EN;
	error = drv_i915_skl_dpcd_write(io, dp->port, I915_DPCD_LINK_BW_SET, config, 2U);
	if (error != 0)
		return EIO;

	/* Clock recovery, then channel equalisation. */
	error = i915_skl_clock_recovery(io, dp, link->lanes);
	if (error == 0)
		error = i915_skl_channel_eq(io, dp, link->lanes);

	/* The idle patterns after the training (intel_ddi_set_idle_link_train); port A does not wait for them. */
	(void)drv_i915_skl_rmw(io, tp_ctl, I915_SKL_DP_TP_TRAIN_MASK, I915_SKL_DP_TP_TRAIN_IDLE);
	if (dp->port != I915_SKL_PORT_A)
		(void)drv_i915_skl_wait(io, tp_status, I915_SKL_DP_TP_STATUS_IDLE_DONE, I915_SKL_DP_TP_STATUS_IDLE_DONE, I915_SKL_IDLE_DONE_US, NULL);

	/* Reports a failed training. */
	if (error != 0)
		return EIO;

	/* The training ends: the sink's pattern off, the transport normal (intel_dp_stop_link_train). */
	config[0] = I915_DPCD_TRAINING_PATTERN_DISABLE;
	(void)drv_i915_skl_dpcd_write(io, dp->port, I915_DPCD_TRAINING_PATTERN_SET, config, 1U);
	i915_skl_set_pattern(io, dp->port, I915_SKL_DP_TP_TRAIN_NORMAL);

	/* Succeeded: the link carries pixels. */
	return 0;
}

/* Stops the port's buffer and transport, waiting for the buffer to go idle (disable_ddi_buf). */
void
drv_i915_skl_ddi_buffer_off(
	const struct i915_skl_io *io,
	unsigned port)
{
	uint32_t buf_ctl;
	uint32_t value;
	int wait;

	buf_ctl = I915_SKL_DDI_BUF_CTL_A + port * I915_SKL_PORT_STRIDE;

	/* The buffer off. */
	wait = 0;
	value = drv_i915_skl_read(io, buf_ctl);
	if ((value & I915_SKL_DDI_BUF_ENABLE) != 0U) {
		drv_i915_skl_write(io, buf_ctl, value & ~I915_SKL_DDI_BUF_ENABLE);
		wait = 1;
	}

	/* The transport off. */
	(void)drv_i915_skl_rmw(io, I915_SKL_DP_TP_CTL_A + port * I915_SKL_PORT_STRIDE, I915_SKL_DP_TP_ENABLE, 0U);
	(void)drv_i915_skl_read(io, I915_SKL_DP_TP_CTL_A + port * I915_SKL_PORT_STRIDE);

	/* The buffer becomes idle (intel_wait_ddi_buf_idle). */
	if (wait)
		(void)drv_i915_skl_wait(io, buf_ctl, I915_SKL_DDI_BUF_IS_IDLE, I915_SKL_DDI_BUF_IS_IDLE, I915_SKL_BUF_IDLE_US, NULL);
}

/*
 * Sets the sink's power state (intel_dp_set_power): D0 is written up to
 * three times a millisecond apart while the sink wakes.  A sink older than
 * DPCD 1.1 has none.  Returns 0, or an errno.
 */
int
drv_i915_skl_dp_sink_power(
	const struct i915_skl_io *io,
	const struct i915_skl_dp_port *dp,
	uint8_t state)
{
	unsigned tries;
	int error;

	/* An old sink has no power state. */
	if (dp->dpcd[I915_DPCD_REV] < I915_SKL_DPCD_REV_11)
		return 0;

	/* The write, again while the sink wakes. */
	error = EIO;
	for (tries = 0U; tries < I915_SKL_POWER_TRIES; tries++) {
		error = drv_i915_skl_dpcd_write(io, dp->port, I915_DPCD_SET_POWER, &state, 1U);
		if (error == 0)
			break;
		io->msleep(io->context, 1U);
	}

	/* Reports a sink that did not take it. */
	if (error != 0)
		return error;

	/* Succeeded: the sink is in the state. */
	return 0;
}

/* Sets the transport's training pattern field (intel_ddi_set_link_train). */
static void
i915_skl_set_pattern(
	const struct i915_skl_io *io,
	unsigned port,
	uint32_t pattern)
{
	/* The field, the rest kept. */
	(void)drv_i915_skl_rmw(io, I915_SKL_DP_TP_CTL_A + port * I915_SKL_PORT_STRIDE, I915_SKL_DP_TP_TRAIN_MASK, pattern);
}

/* Writes the pattern and the lanes' training sets to the sink in one go (intel_dp_set_link_train). */
static int
i915_skl_dpcd_pattern(
	const struct i915_skl_io *io,
	struct i915_skl_dp_port *dp,
	uint8_t pattern,
	unsigned lanes)
{
	uint8_t bytes[5];
	unsigned lane;
	int error;

	/* The pattern, then one set a lane. */
	bytes[0] = pattern;
	for (lane = 0U; lane < lanes; lane++)
		bytes[1U + lane] = dp->train_set;
	error = drv_i915_skl_dpcd_write(io, dp->port, I915_DPCD_TRAINING_PATTERN_SET, bytes, lanes + 1U);
	if (error != 0)
		return error;

	/* Succeeded: the sink trains on the pattern. */
	return 0;
}

/* Writes the lanes' training sets to the sink (intel_dp_update_link_train). */
static int
i915_skl_dpcd_lanes(
	const struct i915_skl_io *io,
	struct i915_skl_dp_port *dp,
	unsigned lanes)
{
	uint8_t bytes[4];
	unsigned lane;
	int error;

	/* One set a lane. */
	for (lane = 0U; lane < lanes; lane++)
		bytes[lane] = dp->train_set;
	error = drv_i915_skl_dpcd_write(io, dp->port, I915_DPCD_TRAINING_LANE0_SET, bytes, lanes);
	if (error != 0)
		return error;

	/* Succeeded: the sink has the sets. */
	return 0;
}

/*
 * Recovers the clock on pattern 1 with scrambling off, raising the swing
 * and pre-emphasis as the sink asks; gives up after five tries at one
 * request, at the highest swing, or after the try limit.  Returns 0, or
 * EIO.
 */
static int
i915_skl_clock_recovery(
	const struct i915_skl_io *io,
	struct i915_skl_dp_port *dp,
	unsigned lanes)
{
	uint8_t before[6];
	unsigned delay;
	unsigned max_tries;
	unsigned same_tries;
	unsigned changed;
	int max_vswing;
	int ok;
	int error;

	/* The interval and the limit the sink's revision gives. */
	delay = drv_i915_skl_dp_train_delay_us(dp->dpcd[I915_DPCD_REV], dp->dpcd[I915_DPCD_TRAINING_AUX_RD_INTERVAL], 0);
	max_tries = I915_SKL_CR_TRIES_OLD;
	if (dp->dpcd[I915_DPCD_REV] >= I915_SKL_DPCD_REV_14)
		max_tries = I915_SKL_CR_TRIES_14;

	/* The lowest levels and pattern 1 on both ends (intel_dp_reset_link_train). */
	dp->train_set = 0U;
	drv_i915_skl_ddi_levels(io, dp, 0U);
	i915_skl_set_pattern(io, dp->port, I915_SKL_DP_TP_TRAIN_PAT1);
	error = i915_skl_dpcd_pattern(io, dp, (uint8_t)(I915_DPCD_TRAINING_PATTERN_1 | I915_DPCD_LINK_SCRAMBLING_DISABLE), lanes);
	if (error != 0)
		return EIO;

	/* Each try: wait, read the status, adjust. */
	kern_memset(before, 0, sizeof(before));
	same_tries = 1U;
	max_vswing = 0;
	for (dp->cr_tries = 0U; dp->cr_tries < max_tries; dp->cr_tries++) {
		io->udelay(io->context, delay);

		/* The lanes' status and the sink's requests. */
		error = drv_i915_skl_dpcd_read(io, dp->port, I915_DPCD_LANE0_1_STATUS, dp->link_status, I915_DPCD_LINK_STATUS_SIZE);
		if (error != 0)
			return EIO;

		/* Every lane has the clock. */
		ok = drv_i915_skl_dp_cr_ok(dp->link_status, lanes);
		if (ok)
			return 0;

		/* Five tries at one request, or the highest swing already tried, end it. */
		if (same_tries == I915_SKL_CR_SAME_TRIES || max_vswing)
			return EIO;

		/* The levels the sink asks for, on both ends. */
		dp->train_set = drv_i915_skl_adjust_train(dp->link_status, lanes, drv_i915_skl_voltage_max(dp->trans_count));
		drv_i915_skl_ddi_levels(io, dp, drv_i915_skl_signal_level(dp->train_set));
		error = i915_skl_dpcd_lanes(io, dp, lanes);
		if (error != 0)
			return EIO;

		/* An unchanged request counts towards the five. */
		changed = (unsigned)i915_skl_adjust_changed(before, dp->link_status, lanes);
		if (changed) {
			same_tries = 1U;
		} else {
			same_tries++;
		}
		kern_memcpy(before, dp->link_status, sizeof(before));

		/* The highest swing is the last try. */
		max_vswing = i915_skl_max_vswing_reached(dp->train_set);
	}

	/* Succeeded in learning that the clock did not come. */
	return EIO;
}

/*
 * Equalises the channel on pattern 3 (or 2 for a sink without TPS3) with
 * scrambling off, adjusting as the sink asks, at most five times.  Returns
 * 0, or EIO.
 */
static int
i915_skl_channel_eq(
	const struct i915_skl_io *io,
	struct i915_skl_dp_port *dp,
	unsigned lanes)
{
	unsigned delay;
	uint32_t source_pattern;
	uint8_t sink_pattern;
	int ok;
	int error;

	/* The interval, and TPS3 when the sink takes it (display 9 always does). */
	delay = drv_i915_skl_dp_train_delay_us(dp->dpcd[I915_DPCD_REV], dp->dpcd[I915_DPCD_TRAINING_AUX_RD_INTERVAL], 1);
	source_pattern = I915_SKL_DP_TP_TRAIN_PAT2;
	sink_pattern = I915_DPCD_TRAINING_PATTERN_2;
	if ((dp->dpcd[I915_DPCD_MAX_LANE_COUNT] & I915_DPCD_TPS3_SUPPORTED) != 0U) {
		source_pattern = I915_SKL_DP_TP_TRAIN_PAT3;
		sink_pattern = I915_DPCD_TRAINING_PATTERN_3;
	}

	/* The pattern on both ends. */
	i915_skl_set_pattern(io, dp->port, source_pattern);
	error = i915_skl_dpcd_pattern(io, dp, (uint8_t)(sink_pattern | I915_DPCD_LINK_SCRAMBLING_DISABLE), lanes);
	if (error != 0)
		return EIO;

	/* Each try: wait, read, check, adjust. */
	for (dp->eq_tries = 0U; dp->eq_tries < I915_SKL_EQ_TRIES; dp->eq_tries++) {
		io->udelay(io->context, delay);

		/* The lanes' status. */
		error = drv_i915_skl_dpcd_read(io, dp->port, I915_DPCD_LANE0_1_STATUS, dp->link_status, I915_DPCD_LINK_STATUS_SIZE);
		if (error != 0)
			return EIO;

		/* A lost clock ends it. */
		ok = drv_i915_skl_dp_cr_ok(dp->link_status, lanes);
		if (!ok)
			return EIO;

		/* An equalised, aligned channel is done. */
		ok = drv_i915_skl_dp_eq_ok(dp->link_status, lanes);
		if (ok)
			return 0;

		/* The levels the sink asks for, on both ends. */
		dp->train_set = drv_i915_skl_adjust_train(dp->link_status, lanes, drv_i915_skl_voltage_max(dp->trans_count));
		drv_i915_skl_ddi_levels(io, dp, drv_i915_skl_signal_level(dp->train_set));
		error = i915_skl_dpcd_lanes(io, dp, lanes);
		if (error != 0)
			return EIO;
	}

	/* Succeeded in learning the channel did not equalise. */
	return EIO;
}

/* Reports nonzero when the training set is at the highest swing with the pre-emphasis (swing + pre-emphasis = 3). */
static int
i915_skl_max_vswing_reached(
	uint8_t train_set)
{
	unsigned v;
	unsigned p;

	/* The swing's reached flag is needed. */
	if ((train_set & I915_DPCD_TRAIN_MAX_SWING_REACHED) == 0U)
		return 0;

	/* And the two levels add to three. */
	v = train_set & I915_DPCD_TRAIN_VOLTAGE_SWING_MASK;
	p = (train_set & I915_DPCD_TRAIN_PRE_EMPHASIS_MASK) >> I915_DPCD_TRAIN_PRE_EMPHASIS_SHIFT;
	if (v + p != 3U)
		return 0;

	/* Succeeded: nothing higher can be tried. */
	return 1;
}

/* Reports nonzero when the sink's adjust requests changed between two statuses for the used lanes. */
static int
i915_skl_adjust_changed(
	const uint8_t *before,
	const uint8_t *after,
	unsigned lanes)
{
	unsigned lane;
	uint8_t old_request;
	uint8_t new_request;

	/* Each lane's nibble of the adjust requests. */
	for (lane = 0U; lane < lanes; lane++) {
		old_request = (uint8_t)((before[4U + lane / 2U] >> (4U * (lane & 1U))) & 0xfU);
		new_request = (uint8_t)((after[4U + lane / 2U] >> (4U * (lane & 1U))) & 0xfU);
		if (old_request != new_request)
			return 1;
	}

	/* Succeeded: the same requests. */
	return 0;
}

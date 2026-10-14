/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP link of a Gen9 DDI port (gen9-link.c, WS237 stage 3): the
 * buffer translations and signal levels, the transport, and the link
 * training (clock recovery, then channel equalisation) with the sink over
 * the AUX channel.
 */

#ifndef DRIVERS_GPU_I915_DISPLAY_GEN9_LINK_H
#define DRIVERS_GPU_I915_DISPLAY_GEN9_LINK_H

#include "gen9-io.h"
#include "gen9-mode.h"

#include <stdint.h>

/*
 * One DP port being trained: what the sink said, the table its buffers
 * use, and the DDI_BUF_CTL word kept between the steps.
 */
struct i915_skl_dp_port {
	/* The port, its receiver capabilities (DPCD 0x000..0x00e) and whether it is the panel. */
	unsigned port;
	uint8_t dpcd[15];
	int edp;

	/* The buffer table and its usable entries. */
	const struct i915_skl_buf_trans *trans;
	unsigned trans_count;

	/* The DDI_BUF_CTL word built for the link, and the lane training set. */
	uint32_t buf_ctl;
	uint8_t train_set;

	/* What the training ended with, for the log. */
	uint8_t link_status[6];
	unsigned cr_tries;
	unsigned eq_tries;
};

/* Writes the buffer translations and the boost of a port's table. */
void drv_i915_skl_ddi_buffers(const struct i915_skl_io *io, const struct i915_skl_dp_port *dp);

/* Selects a table entry on the port: the buffer control's swing select and the entry's boost. */
void drv_i915_skl_ddi_levels(const struct i915_skl_io *io, struct i915_skl_dp_port *dp, unsigned level);

/* Trains the link at a rate and lane count, leaving it sending normal pixels: 0, or EIO. */
int drv_i915_skl_dp_train(const struct i915_skl_io *io, struct i915_skl_dp_port *dp, const struct i915_skl_link *link);

/* Stops the port's buffer and transport, waiting for the buffer to go idle. */
void drv_i915_skl_ddi_buffer_off(const struct i915_skl_io *io, unsigned port);

/* Sets the sink's power state (DPCD SET_POWER, D0 or D3): 0, or an errno. */
int drv_i915_skl_dp_sink_power(const struct i915_skl_io *io, const struct i915_skl_dp_port *dp, uint8_t state);

#endif /* DRIVERS_GPU_I915_DISPLAY_GEN9_LINK_H */

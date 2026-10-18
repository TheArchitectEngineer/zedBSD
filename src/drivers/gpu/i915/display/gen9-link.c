/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The DP link of a Gen9 DDI port (see gen9-link.h), through the xd code's
 * display 9 DDI and its link training (xd-ddi.c, xd-dp.c).
 */

#include "gen9-link.h"
#include "xd-aux.h"
#include "xd-ddi.h"
#include "xd-io.h"
#include <kern/kcrt.h>

#include "../intel/display-dpcd-xd.h"

#include <uapi/errno.h>

static int i915_skl_xd_port(const struct i915_skl_io *io, const struct i915_skl_dp_port *dp, struct i915_xd9_dp_port *port);

/*
 * Writes the buffer translations of a port's table.
 */
void
drv_i915_skl_ddi_buffers(
	const struct i915_skl_io *io,
	const struct i915_skl_dp_port *dp)
{
	/* The xd code's display 9 rows. */
	drv_i915_xd9_ddi_buffers(drv_i915_skl_xd_io(io), dp->port, dp->trans, dp->trans_count);
}

/*
 * Selects a table entry on the port: its boost and the buffer control's
 * row.
 */
void
drv_i915_skl_ddi_levels(
	const struct i915_skl_io *io,
	struct i915_skl_dp_port *dp,
	unsigned level)
{
	/* The xd code's display 9 row. */
	drv_i915_xd9_ddi_levels(drv_i915_skl_xd_io(io), dp->port, dp->trans, dp->trans_count, level, &dp->buf_ctl);
}

/*
 * Trains the link at a rate and lane count and leaves it sending normal
 * pixels.  Returns 0, or EIO when the training failed (the port is left
 * training, for the caller to stop or try again).
 */
int
drv_i915_skl_dp_train(
	const struct i915_skl_io *io,
	struct i915_skl_dp_port *dp,
	const struct i915_skl_link *link)
{
	struct i915_xd9_dp_port port;
	int error;

	/* The port in the xd code's form. */
	error = i915_skl_xd_port(io, dp, &port);
	if (error != 0)
		return EIO;

	/* The xd code's training. */
	error = drv_i915_xd9_dp_train(&port, link->port_khz, link->lanes);
	dp->buf_ctl = port.buf_word;
	dp->train_set = port.set;
	kern_memcpy(dp->dpcd, port.caps, sizeof(dp->dpcd));
	if (error != 0)
		return EIO;

	/* Succeeded: the link carries pixels. */
	return 0;
}

/* Stops the port's buffer and transport, waiting for the buffer to go idle. */
void
drv_i915_skl_ddi_buffer_off(
	const struct i915_skl_io *io,
	unsigned port)
{
	/* The xd code's display 9 DDI. */
	drv_i915_xd9_ddi_buffer_off(drv_i915_skl_xd_io(io), port);
}

/*
 * Sets the sink's power state.  Returns 0, or an errno.
 */
int
drv_i915_skl_dp_sink_power(
	const struct i915_skl_io *io,
	const struct i915_skl_dp_port *dp,
	uint8_t state)
{
	struct i915_xd9_dp_port port;
	int error;

	/* The port in the xd code's form. */
	error = i915_skl_xd_port(io, dp, &port);
	if (error != 0)
		return error;

	/* The xd code's sink power. */
	error = drv_i915_xd9_dp_sink_power(&port, state);
	if (error != 0)
		return error;

	/* Succeeded: the sink is in the state. */
	return 0;
}

/* Fills the xd code's display 9 port from the node's: 0, or the error of its AUX channel. */
static int
i915_skl_xd_port(
	const struct i915_skl_io *io,
	const struct i915_skl_dp_port *dp,
	struct i915_xd9_dp_port *port)
{
	int error;

	/* Nothing kept from before. */
	kern_memset(port, 0, sizeof(*port));

	/* The DDI's AUX channel. */
	error = drv_i915_xd_aux_init(&port->aux, dp->port);
	if (error != 0)
		return error;

	/* Succeeded: the registers, the DDI, its table, the buffer's word and the sink's capabilities. */
	port->io = drv_i915_skl_xd_io(io);
	port->ddi = dp->port;
	port->table = dp->trans;
	port->rows = dp->trans_count;
	port->buf_word = dp->buf_ctl;
	port->set = dp->train_set;
	kern_memcpy(port->caps, dp->dpcd, sizeof(port->caps));
	return 0;
}

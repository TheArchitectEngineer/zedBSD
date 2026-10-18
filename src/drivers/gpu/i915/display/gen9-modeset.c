/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register sequences of the Gen9 mode set (see gen9-modeset.h): power
 * well 2 and the DDI IO wells, the DPLLs, the pipe, transcoder and DDI of
 * an output and the way back, the plane and its watermarks, and the
 * hotplug sense.
 *
 * Not done: the pipe's gamma and CSC (the plane bypasses them), audio and
 * infoframes on HDMI, HDMI 2.0 scrambling (the clock stays at most
 * 300 MHz), DP MST, and a CD clock change (a mode above the CD clock is
 * not chosen).
 */

#include "gen9-modeset.h"
#include "gen9-ddc.h"
#include "xd-aux.h"
#include "xd-clock.h"
#include "xd-dbuf.h"
#include "xd-ddi.h"
#include "xd-hpd.h"
#include "xd-io.h"
#include "xd-output.h"
#include "xd-pipe.h"
#include "xd-platform.h"
#include "xd-power.h"
#include <kern/kcrt.h>

#include <uapi/errno.h>

static const struct i915_xd_platform *i915_skl_modeset_platform(void);
static int i915_skl_xd_output(const struct i915_skl_io *io, const struct i915_skl_output_config *config, const struct i915_skl_dp_port *dp, struct i915_xd9_output *out, struct i915_xd9_dp_port *port);

/*
 * Turns power well 2 on; a well already up is left alone.  Returns 0, or
 * ETIMEDOUT.
 */
int
drv_i915_skl_pw2_on(
	const struct i915_skl_io *io)
{
	const struct i915_xd_platform *platform;
	int error;

	/* The xd code's display 9 power well 2 (every display 9 part has the same wells). */
	platform = drv_i915_xd_platform_display9_class(I915_XD_KIND_KBL, I915_XD_POWER_CLASS_H);
	error = drv_i915_xd9_power_pw2_on(drv_i915_skl_xd_io(io), platform);
	if (error != 0)
		return error;

	/* Succeeded: pipes B and C and the AUX channels B to D have power. */
	return 0;
}

/*
 * Turns a DDI port's IO power well on or off: 0, or ETIMEDOUT.
 */
int
drv_i915_skl_ddi_io_power(
	const struct i915_skl_io *io,
	unsigned port,
	int on)
{
	const struct i915_xd_platform *platform;
	int error;

	/* The xd code's display 9 DDI IO well. */
	platform = drv_i915_xd_platform_display9_class(I915_XD_KIND_KBL, I915_XD_POWER_CLASS_H);
	error = drv_i915_xd9_power_ddi_io(drv_i915_skl_xd_io(io), platform, port, on);
	if (error != 0)
		return error;

	/* Succeeded: the request is as asked. */
	return 0;
}

/*
 * Starts a DPLL with its words.  Returns 0, EINVAL for DPLL0 or no DPLL,
 * or ETIMEDOUT (the DPLL is left enabled for the caller to stop).
 */
int
drv_i915_skl_dpll_on(
	const struct i915_skl_io *io,
	unsigned id,
	const struct i915_skl_dpll_words *words)
{
	const struct i915_xd_platform *platform;
	struct i915_xd9_dpll_words xd;
	int error;

	/* The words in the xd code's form. */
	xd.ctrl1 = words->ctrl1;
	xd.cfgcr1 = words->cfgcr1;
	xd.cfgcr2 = words->cfgcr2;
	xd.port_khz = words->port_khz;

	/* The xd code's display 9 DPLL. */
	platform = drv_i915_xd_platform_display9_class(I915_XD_KIND_KBL, I915_XD_POWER_CLASS_H);
	error = drv_i915_xd9_dpll_enable(drv_i915_skl_xd_io(io), platform, id, &xd);
	if (error != 0)
		return error;

	/* Succeeded: the DPLL runs. */
	return 0;
}

/*
 * Stops a DPLL other than DPLL0.
 */
void
drv_i915_skl_dpll_off(
	const struct i915_skl_io *io,
	unsigned id)
{
	const struct i915_xd_platform *platform;

	/* The xd code's display 9 DPLL. */
	platform = drv_i915_xd_platform_display9_class(I915_XD_KIND_KBL, I915_XD_POWER_CLASS_H);
	drv_i915_xd9_dpll_disable(drv_i915_skl_xd_io(io), platform, id);
}

/*
 * Lights an external output on its pipe through the xd code's display 9
 * output.  Returns 0, or an errno with what was started stopped again.
 */
int
drv_i915_skl_output_on(
	const struct i915_skl_io *io,
	const struct i915_skl_output_config *config,
	struct i915_skl_dp_port *dp)
{
	struct i915_xd9_output out;
	struct i915_xd9_dp_port port;
	int error;

	/* The output and its port in the xd code's form. */
	error = i915_skl_xd_output(io, config, dp, &out, &port);
	if (error != 0)
		return error;

	/* The xd code's display 9 output; a DisplayPort port keeps what the training ended with. */
	error = drv_i915_xd9_output_on(drv_i915_skl_xd_io(io), i915_skl_modeset_platform(), &out, &port);
	if (!out.hdmi) {
		dp->buf_ctl = port.buf_word;
		dp->train_set = port.set;
		kern_memcpy(dp->dpcd, port.caps, sizeof(dp->dpcd));
	}
	if (error != 0)
		return error;

	/* Succeeded: the output shows the surface. */
	return 0;
}

/*
 * Darkens an external output through the xd code's display 9 output.
 */
void
drv_i915_skl_output_off(
	const struct i915_skl_io *io,
	const struct i915_skl_output_config *config,
	struct i915_skl_dp_port *dp)
{
	struct i915_xd9_output out;
	struct i915_xd9_dp_port port;
	int error;

	/* The output in the xd code's form; a DisplayPort port without its AUX channel is darkened without the sink. */
	error = i915_skl_xd_output(io, config, dp, &out, &port);
	if (out.hdmi || dp == NULL || error != 0) {
		drv_i915_xd9_output_off(drv_i915_skl_xd_io(io), i915_skl_modeset_platform(), &out, NULL);
		return;
	}

	/* Succeeded: the xd code's display 9 output with its sink. */
	drv_i915_xd9_output_off(drv_i915_skl_xd_io(io), i915_skl_modeset_platform(), &out, &port);
}

/* Writes a pipe's first plane's and cursor's watermarks and DDB and the line time through the xd code's display 9 buffer. */
void
drv_i915_skl_write_pipe_wm(
	const struct i915_skl_io *io,
	unsigned pipe,
	const struct i915_skl_pipe_wm *wm)
{
	struct i915_xd9_pipe_plan plan;

	/* The pipe in the xd code's form, written. */
	drv_i915_skl_xd_plan(wm, &plan);
	drv_i915_xd9_wm_write_pipe(drv_i915_skl_xd_io(io), pipe, &plan);
}

/* Fills the xd code's display 9 plan of a pipe from the node's watermarks and DDB. */
void
drv_i915_skl_xd_plan(
	const struct i915_skl_pipe_wm *wm,
	struct i915_xd9_pipe_plan *plan)
{
	unsigned level;

	/* Succeeded: whether it is lit, its ranges, its levels and its line time. */
	kern_memset(plan, 0, sizeof(*plan));
	plan->active = wm->active;
	plan->computed = wm->active;
	plan->pipe_ddb.start = (uint16_t)wm->ddb_start;
	plan->pipe_ddb.end = (uint16_t)wm->ddb_end;
	plan->plane_ddb.start = (uint16_t)wm->plane_start;
	plan->plane_ddb.end = (uint16_t)wm->plane_end;
	plan->cursor_ddb.start = (uint16_t)wm->cursor_start;
	plan->cursor_ddb.end = (uint16_t)wm->cursor_end;
	for (level = 0U; level < I915_XD_WM_LEVELS; level++) {
		plan->wm.level[level].enable = (uint8_t)(wm->level[level].enable != 0);
		plan->wm.level[level].blocks = (uint16_t)wm->level[level].blocks;
		plan->wm.level[level].lines = (uint16_t)wm->level[level].lines;
		plan->wm.level[level].min_ddb = (uint16_t)wm->level[level].min_ddb;
	}
	plan->linetime = wm->linetime;
}

/* Shows a surface on a pipe's first plane through the xd code's display 9 plane. */
void
drv_i915_skl_plane_on(
	const struct i915_skl_io *io,
	unsigned pipe,
	uint32_t surf,
	uint32_t stride_units,
	uint32_t width,
	uint32_t height,
	int rgbx)
{
	/* The xd code's display 9 plane. */
	drv_i915_xd9_plane_on(drv_i915_skl_xd_io(io), pipe, surf, stride_units, width, height, rgbx);
}

/* Turns a pipe's first plane off through the xd code's display 9 plane. */
void
drv_i915_skl_plane_off(
	const struct i915_skl_io *io,
	unsigned pipe)
{
	/* The xd code's display 9 plane. */
	drv_i915_xd9_plane_off(drv_i915_skl_xd_io(io), pipe);
}

/* Waits for the next vertical blank of a running pipe: 0, or ETIMEDOUT. */
int
drv_i915_skl_wait_vblank(
	const struct i915_skl_io *io,
	unsigned pipe)
{
	int error;

	/* The xd code's display 9 pipe. */
	error = drv_i915_xd9_pipe_wait_vblank(drv_i915_skl_xd_io(io), pipe);
	if (error != 0)
		return error;

	/* Succeeded: a vertical blank came. */
	return 0;
}

/* Turns on the PCH's hotplug sense of ports B to D through the xd code's display 9 hotplug. */
void
drv_i915_skl_hotplug_enable(
	const struct i915_skl_io *io)
{
	/* The xd code's display 9 hotplug. */
	drv_i915_xd9_hpd_enable(drv_i915_skl_xd_io(io));
}

/* Reports the ports the board has: bit 1 port B, bit 2 C, bit 3 D. */
unsigned
drv_i915_skl_ports_present(
	const struct i915_skl_io *io)
{
	/* The xd code's display 9 hotplug. */
	return drv_i915_xd9_hpd_present(drv_i915_skl_xd_io(io));
}

/* Reports the ports a sink is attached to now. */
unsigned
drv_i915_skl_ports_connected(
	const struct i915_skl_io *io)
{
	/* The xd code's display 9 hotplug. */
	return drv_i915_xd9_hpd_connected(drv_i915_skl_xd_io(io));
}

/* Reports the display 9 platform the mode set's registers follow. */
static const struct i915_xd_platform *
i915_skl_modeset_platform(void)
{
	/* Succeeded: the rows every display 9 part shares here. */
	return drv_i915_xd_platform_display9_class(I915_XD_KIND_KBL, I915_XD_POWER_CLASS_H);
}

/*
 * Fills the xd code's display 9 output from the node's, and for
 * DisplayPort its port (dp NULL leaves it empty).  Returns 0, or the error
 * of the port's AUX channel (the output is filled even then).
 */
static int
i915_skl_xd_output(
	const struct i915_skl_io *io,
	const struct i915_skl_output_config *config,
	const struct i915_skl_dp_port *dp,
	struct i915_xd9_output *out,
	struct i915_xd9_dp_port *port)
{
	const struct i915_xd9_buf_trans *table;
	unsigned count;
	unsigned level;
	int error;

	/* The output: its kind, DDI, pipe, DPLL, mode, link and first surface. */
	kern_memset(out, 0, sizeof(*out));
	out->hdmi = config->type == I915_SKL_OUTPUT_HDMI;
	out->ddi = config->port;
	out->pipe = config->pipe;
	out->dpll = config->dpll;
	out->dpll_words.ctrl1 = config->dpll_words.ctrl1;
	out->dpll_words.cfgcr1 = config->dpll_words.cfgcr1;
	out->dpll_words.cfgcr2 = config->dpll_words.cfgcr2;
	out->dpll_words.port_khz = config->dpll_words.port_khz;
	out->mode = config->mode;
	out->rate_khz = config->link.port_khz;
	out->lanes = config->link.lanes;
	out->m_n.tu = config->link.tu;
	out->m_n.data_m = config->link.data_m;
	out->m_n.data_n = config->link.data_n;
	out->m_n.link_m = config->link.link_m;
	out->m_n.link_n = config->link.link_n;
	out->surf = config->surf;
	out->stride_units = config->stride_units;
	out->rgbx = config->rgbx;

	/* HDMI: the row of the part's table its level names. */
	kern_memset(port, 0, sizeof(*port));
	if (out->hdmi) {
		table = drv_i915_skl_buf_trans(config->sku, config->platform, I915_SKL_OUTPUT_HDMI, config->port, &count);
		level = drv_i915_skl_hdmi_level(config->sku, config->platform);
		if (level >= count)
			level = count - 1U;
		out->hdmi_row = &table[level];
		return 0;
	}

	/* No DisplayPort port to fill. */
	if (dp == NULL)
		return 0;

	/* The DDI's AUX channel. */
	error = drv_i915_xd_aux_init(&port->aux, dp->port);
	if (error != 0)
		return error;

	/* Succeeded: the registers, the DDI, its table, the training set and the sink's capabilities. */
	port->io = drv_i915_skl_xd_io(io);
	port->ddi = dp->port;
	port->table = dp->trans;
	port->rows = dp->trans_count;
	port->buf_word = dp->buf_ctl;
	port->set = dp->train_set;
	kern_memcpy(port->caps, dp->dpcd, sizeof(port->caps));
	return 0;
}

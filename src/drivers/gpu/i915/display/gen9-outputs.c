/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The external outputs of a Gen9 display (see gen9-outputs.h).
 *
 * A port's sink is found by the south display's live hotplug state
 * (SDEISR), then the DPCD over AUX for a DP sink, else the EDID over the
 * GMBUS for an HDMI one.  Lighting an output moves the DDB: the pipes
 * already lit are given their new, smaller allocations first, each taking
 * effect at its vertical blank, and only then is the new pipe's allocation
 * written; darkening frees the pipe's allocation first and grows the others
 * after it, in the opposite order.  A plane's double-buffered watermark and
 * DDB registers are armed by writing its surface register before the
 * plane's update.
 *
 * Not done: the CD clock is never changed (a mode above it is not chosen),
 * MST, and a panel on an external port.
 */

#include "gen9-outputs.h"
#include "gen9-ddc.h"
#include "xd-dbuf.h"
#include "xd-io.h"
#include <kern/kcrt.h>
#include <kern/klog.h>

#include "../device-info.h"
#include "../intel/display-regs-gen9.h"
#include "../intel/display-regs-gen9-modeset.h"

#include <uapi/errno.h>

/* The memory read latency PCODE leaves out on display 9 (WaWmMemoryReadLatency), in microseconds. */
#define I915_SKL_READ_LATENCY_US	2

/* The latency IPC adds on Kaby Lake and later (skl_wm_latency), in microseconds. */
#define I915_SKL_IPC_LATENCY_US		4U

/* The bits of the latency levels in each PCODE word. */
#define I915_SKL_LATENCY_BITS		8U
#define I915_SKL_LATENCY_MASK		0xffU

/* The pixel clocks (kHz) an HDMI port drives at most. */
#define I915_SKL_OUTPUT_HDMI_KHZ	300000U

/* The bytes a pixel takes on the link: 24 bits. */
#define I915_SKL_OUTPUT_LINK_BYTES	3U

/* The EDID's header. */
static const uint8_t i915_skl_edid_header[8] = { 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00 };

static int i915_skl_outputs_probe(struct i915_skl_outputs *outputs, struct i915_skl_output *output);
static void i915_skl_outputs_edid(struct i915_skl_outputs *outputs, struct i915_skl_output *output);
static void i915_skl_outputs_mode(struct i915_skl_outputs *outputs, struct i915_skl_output *output);
static int i915_skl_outputs_config(struct i915_skl_outputs *outputs, struct i915_skl_output *output);
static void i915_skl_outputs_pipes(const struct i915_skl_outputs *outputs, const struct i915_skl_output *with, struct i915_skl_pipe_wm pipes[3]);
static void i915_skl_outputs_rewrite(struct i915_skl_outputs *outputs, const struct i915_skl_pipe_wm pipes[3], int ascending, unsigned except);

/*
 * Decodes and adjusts the memory latencies PCODE answered (see
 * gen9-outputs.h).
 */
void
drv_i915_skl_wm_latency(
	uint32_t low,
	uint32_t high,
	int ipc,
	uint16_t latency[8])
{
	unsigned level;
	unsigned levels;
	uint32_t word;

	/* Four levels in each word, level 0 lowest. */
	for (level = 0; level < 8U; level++) {
		word = low;
		if (level >= 4U)
			word = high;
		latency[level] = (uint16_t)((word >> ((level % 4U) * I915_SKL_LATENCY_BITS)) & I915_SKL_LATENCY_MASK);
	}

	/*
	 * A level PCODE answered with 0 us cannot be used, and neither can any
	 * level above it: the levels from there up are cleared and no longer
	 * count.
	 *
	 * Procedure referenced: Linux v6.8.12
	 * drivers/gpu/drm/i915/display/skl_watermark.c (adjust_wm_latency).
	 */
	levels = 8U;
	for (level = 1U; level < 8U; level++) {
		/* A level with a latency is usable; the search goes on above it. */
		if (latency[level] != 0U)
			continue;

		/* The first unusable level ends the usable ones. */
		levels = level;
		break;
	}

	/* Clears every level from the first unusable one up. */
	for (level = levels; level < 8U; level++)
		latency[level] = 0U;

	/*
	 * When level 0 came back as 0 us, PCODE left the memory read latency
	 * out of every usable level, so it is added to each of them.
	 */
	if (latency[0] == 0U) {
		for (level = 0U; level < levels; level++)
			latency[level] = (uint16_t)(latency[level] + I915_SKL_READ_LATENCY_US);
	}

	/* Level 0 takes 1 us more, because the DIMMs' size is not known (it may be a 16 GB one). */
	latency[0] = (uint16_t)(latency[0] + 1U);

	/* IPC lengthens every level that is used. */
	if (!ipc)
		return;
	for (level = 0; level < 8U; level++) {
		if (latency[level] != 0U)
			latency[level] = (uint16_t)(latency[level] + I915_SKL_IPC_LATENCY_US);
	}
}

/* Sets the outputs up beside the panel and turns on the ports' hotplug sense. */
void
drv_i915_skl_outputs_init(
	struct i915_skl_outputs *outputs,
	const struct i915_skl_io *io,
	const struct i915_skl_panel *panel,
	int sku,
	int platform,
	uint32_t cdclk_khz,
	const uint16_t latency[8])
{
	unsigned index;
	unsigned present;

	/* What the outputs share the display with. */
	kern_memset(outputs, 0, sizeof(*outputs));
	outputs->io = io;
	outputs->panel = *panel;
	outputs->sku = sku;
	outputs->platform = platform;
	outputs->cdclk_khz = cdclk_khz;
	kern_memcpy(outputs->latency, latency, sizeof(outputs->latency));

	/* Each external port. */
	for (index = 0; index < I915_SKL_OUTPUTS; index++)
		outputs->output[index].port = index + I915_SKL_PORT_B;

	/* The ports the board has, but the panel's, and their hotplug sense. */
	present = drv_i915_skl_ports_present(io);
	outputs->present = present & ~(1U << panel->port);
	drv_i915_skl_hotplug_enable(io);
}

/*
 * Reads which ports have a sink and probes those that changed (see
 * gen9-outputs.h).
 */
unsigned
drv_i915_skl_outputs_poll(
	struct i915_skl_outputs *outputs)
{
	struct i915_skl_output *output;
	unsigned connected;
	unsigned changed;
	unsigned bit;
	unsigned index;
	int error;

	/* The live state of the ports the board has. */
	connected = drv_i915_skl_ports_connected(outputs->io) & outputs->present;
	changed = connected ^ outputs->connected;

	/* Each port whose sink came or went. */
	for (index = 0; index < I915_SKL_OUTPUTS; index++) {
		output = &outputs->output[index];
		bit = 1U << output->port;
		if ((changed & bit) == 0U)
			continue;

		/* A sink that went. */
		if ((connected & bit) == 0U) {
			output->connected = 0;
			output->type = 0;
			continue;
		}

		/* A sink that came: what it is. */
		error = i915_skl_outputs_probe(outputs, output);
		if (error != 0) {
			kern_logf("i915: gen9 outputs: port %c has a sink that could not be probed: %d\n", 'A' + (int)output->port, error);
			connected &= ~bit;
			changed &= ~bit;
			continue;
		}

		/* A new attachment. */
		outputs->generation++;
		output->generation = outputs->generation;
		output->connected = 1;
	}

	/* Succeeded: the ports that changed. */
	outputs->connected = connected;
	return changed;
}

/*
 * Lights an output on a free pipe and DPLL: the pipes already lit get
 * their new allocations in ascending order, then the new pipe's
 * watermarks, then the mode set.  Returns 0, ENOENT for a port without a
 * sink, EBUSY for one lit already, ENOSPC when no pipe, DPLL or buffer
 * space is left, or the mode set's error (with the allocations given
 * back).
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display.c (skl_commit_modeset_enables)
 * and skl_watermark.c (skl_write_plane_wm).
 */
int
drv_i915_skl_outputs_light(
	struct i915_skl_outputs *outputs,
	unsigned index,
	uint32_t surf,
	uint32_t stride_units,
	int rgbx)
{
	struct i915_skl_pipe_wm pipes[3];
	struct i915_skl_pipe_wm empty;
	struct i915_skl_output *output;
	int error;

	/* The output the index names. */
	output = &outputs->output[index];

	/* A sink that is there, and not lit yet. */
	if (!output->connected)
		return ENOENT;
	if (output->lit)
		return EBUSY;

	/* The pipe, the DPLL, the link and the clocks. */
	error = i915_skl_outputs_config(outputs, output);
	if (error != 0)
		return error;
	output->config.surf = surf;
	output->config.stride_units = stride_units;
	output->config.rgbx = rgbx;

	/* The buffer split with the new pipe in it. */
	i915_skl_outputs_pipes(outputs, output, pipes);
	error = drv_i915_skl_wm_compute(pipes, outputs->latency);
	if (error != 0)
		return error;

	/* The lit pipes shrink first, each at its vertical blank, then the new pipe's watermarks and the mode set. */
	i915_skl_outputs_rewrite(outputs, pipes, 1, output->config.pipe);
	drv_i915_skl_write_pipe_wm(outputs->io, output->config.pipe, &pipes[output->config.pipe]);
	error = drv_i915_skl_output_on(outputs->io, &output->config, &output->dp);
	if (error == 0) {
		output->lit = 1;
		kern_logf("i915: gen9 outputs: port %c lit on pipe %c, DPLL%u, %ux%u at %u kHz\n",
		    'A' + (int)output->port,
		    'A' + (int)output->config.pipe,
		    output->config.dpll,
		    (unsigned)output->mode.hdisplay,
		    (unsigned)output->mode.vdisplay,
		    (unsigned)output->mode.clock_khz);
		return 0;
	}

	/* The mode set failed: the new pipe's allocation goes, and the others grow back. */
	kern_memset(&empty, 0, sizeof(empty));
	drv_i915_skl_write_pipe_wm(outputs->io, output->config.pipe, &empty);
	i915_skl_outputs_pipes(outputs, NULL, pipes);
	(void)drv_i915_skl_wm_compute(pipes, outputs->latency);
	i915_skl_outputs_rewrite(outputs, pipes, 0, output->config.pipe);
	kern_logf("i915: gen9 outputs: port %c could not be lit: %d\n", 'A' + (int)output->port, error);

	/* Succeeded in learning the output stays dark. */
	return error;
}

/*
 * Darkens a lit output: the mode set's way back, the pipe's allocation
 * freed, then the other pipes grow in descending order.
 */
void
drv_i915_skl_outputs_darken(
	struct i915_skl_outputs *outputs,
	unsigned index)
{
	struct i915_skl_pipe_wm pipes[3];
	struct i915_skl_pipe_wm empty;
	struct i915_skl_output *output;
	unsigned pipe;

	/* The output the index names. */
	output = &outputs->output[index];

	/* Nothing to darken. */
	if (!output->lit)
		return;

	/* The output off, and its pipe's allocation freed (its plane is off: the surface write arms it at once). */
	drv_i915_skl_output_off(outputs->io, &output->config, &output->dp);
	output->lit = 0;
	pipe = output->config.pipe;
	kern_memset(&empty, 0, sizeof(empty));
	drv_i915_skl_write_pipe_wm(outputs->io, pipe, &empty);
	drv_i915_skl_write(outputs->io, I915_SKL_PLANE_SURF_1_A + pipe * I915_SKL_PIPE_STRIDE, 0U);

	/* The others grow into it. */
	i915_skl_outputs_pipes(outputs, NULL, pipes);
	(void)drv_i915_skl_wm_compute(pipes, outputs->latency);
	i915_skl_outputs_rewrite(outputs, pipes, 0, pipe);

	/* Succeeded: the output is dark. */
	kern_logf("i915: gen9 outputs: port %c dark\n", 'A' + (int)output->port);
}

/* Points a lit output's plane at a surface, which takes effect at its next vertical blank. */
void
drv_i915_skl_outputs_flip(
	struct i915_skl_outputs *outputs,
	unsigned index,
	uint32_t surf)
{
	uint32_t surf_reg;

	/* The plane's surface register on the output's pipe. */
	surf_reg = I915_SKL_PLANE_SURF_1_A + outputs->output[index].config.pipe * I915_SKL_PIPE_STRIDE;

	/* The surface, which arms the plane. */
	drv_i915_skl_write(outputs->io, surf_reg, surf);
	(void)drv_i915_skl_read(outputs->io, surf_reg);
}

/* Reports the surface a lit output's plane scans out now. */
uint32_t
drv_i915_skl_outputs_live(
	struct i915_skl_outputs *outputs,
	unsigned index)
{
	uint32_t live;

	/* The live surface, without the low bits. */
	live = drv_i915_skl_read(outputs->io, I915_SKL_PLANE_SURFLIVE_1_A + outputs->output[index].config.pipe * I915_SKL_PIPE_STRIDE);

	/* Succeeded: the surface. */
	return live & I915_SKL_PLANE_SURF_ADDR_MASK;
}

/* Reports an output's mode's refresh in millihertz, or 0 without a mode. */
uint32_t
drv_i915_skl_output_refresh(
	const struct i915_skl_output *output)
{
	uint64_t frame;
	uint64_t millihz;

	/* A mode with a frame. */
	frame = (uint64_t)output->mode.htotal * output->mode.vtotal;
	if (frame == 0U || output->mode.clock_khz <= 0)
		return 0U;

	/* Succeeded: pixels a second over pixels a frame, rounded. */
	millihz = ((uint64_t)output->mode.clock_khz * 1000000U + frame / 2U) / frame;
	return (uint32_t)millihz;
}

/*
 * Finds what a port's sink is: a DP sink answers its DPCD over AUX; else
 * an HDMI one, whose EDID comes over the GMBUS.  Power well 2 (the AUX
 * channels and the GMBUS of ports B to D) goes up first and stays up.
 * Returns 0, or the power well's error.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_dp.c (intel_dp_detect), g4x_dp.c
 * (ibx_digital_port_connected) and intel_hdmi.c (intel_hdmi_detect).
 */
static int
i915_skl_outputs_probe(
	struct i915_skl_outputs *outputs,
	struct i915_skl_output *output)
{
	unsigned port;
	unsigned lanes;
	int error;

	/* Nothing from an attachment before. */
	port = output->port;
	kern_memset(output, 0, sizeof(*output));
	output->port = port;

	/* The AUX channels and the GMBUS. */
	error = drv_i915_skl_pw2_on(outputs->io);
	if (error != 0)
		return error;

	/* A DP sink: its receiver capabilities. */
	error = drv_i915_skl_dpcd_read(outputs->io, port, 0U, output->dp.dpcd, sizeof(output->dp.dpcd));
	if (error == 0 && output->dp.dpcd[I915_DPCD_REV] != 0U) {
		output->type = I915_SKL_OUTPUT_DP;
		output->dp.port = port;
		output->sink_max_khz = drv_i915_skl_dpcd_rate_khz(output->dp.dpcd[I915_DPCD_MAX_LINK_RATE]);
		if (output->sink_max_khz == 0U)
			output->sink_max_khz = I915_SKL_DP_RBR_KHZ;
		lanes = output->dp.dpcd[I915_DPCD_MAX_LANE_COUNT] & I915_DPCD_LANE_COUNT_MASK;
		if (lanes == 0U || lanes > 4U)
			lanes = 4U;
		output->sink_lanes = lanes;
	} else {
		output->type = I915_SKL_OUTPUT_HDMI;
	}

	/* Its EDID, and the mode. */
	i915_skl_outputs_edid(outputs, output);
	i915_skl_outputs_mode(outputs, output);
	kern_logf("i915: gen9 outputs: port %c: %s sink, EDID %u bytes, %ux%u at %u kHz\n",
	    'A' + (int)port,
	    (output->type == I915_SKL_OUTPUT_DP) ? "DP" : "HDMI",
	    output->edid_bytes,
	    (unsigned)output->mode.hdisplay,
	    (unsigned)output->mode.vdisplay,
	    (unsigned)output->mode.clock_khz);

	/* Succeeded: the sink is known. */
	return 0;
}

/*
 * Reads a sink's EDID: the base block, then the first extension when the
 * base names one.  A block that cannot be read, or a base without the
 * header, leaves no EDID.
 */
static void
i915_skl_outputs_edid(
	struct i915_skl_outputs *outputs,
	struct i915_skl_output *output)
{
	unsigned blocks;
	unsigned pin;
	int differs;
	int error;

	/* The base block, over AUX or the GMBUS. */
	pin = drv_i915_skl_gmbus_pin(output->port);
	if (output->type == I915_SKL_OUTPUT_DP)
		error = drv_i915_skl_aux_edid(outputs->io, output->port, output->edid, 1U);
	else
		error = drv_i915_skl_gmbus_edid(outputs->io, pin, output->edid, 1U);
	if (error != 0)
		return;
	differs = kern_memcmp(output->edid, i915_skl_edid_header, sizeof(i915_skl_edid_header));
	if (differs != 0)
		return;
	output->edid_bytes = I915_EDID_BLOCK_BYTES;

	/* The first extension, when the base names one. */
	blocks = 1U + output->edid[126];
	if (blocks < 2U)
		return;
	if (output->type == I915_SKL_OUTPUT_DP)
		error = drv_i915_skl_aux_edid(outputs->io, output->port, output->edid, 2U);
	else
		error = drv_i915_skl_gmbus_edid(outputs->io, pin, output->edid, 2U);
	if (error != 0)
		return;

	/* Succeeded: both blocks. */
	output->edid_bytes = 2U * I915_EDID_BLOCK_BYTES;
}

/*
 * Chooses the mode an output shows: the first timing of its EDID the CD
 * clock, the port and the link carry and the display window holds, else
 * 1024x768.
 */
static void
i915_skl_outputs_mode(
	struct i915_skl_outputs *outputs,
	struct i915_skl_output *output)
{
	const uint8_t *edid;
	uint32_t limit;
	uint32_t link;
	int error;

	/* The CD clock, and the port's or the link's limit. */
	limit = outputs->cdclk_khz;
	if (output->type == I915_SKL_OUTPUT_HDMI && limit > I915_SKL_OUTPUT_HDMI_KHZ)
		limit = I915_SKL_OUTPUT_HDMI_KHZ;
	if (output->type == I915_SKL_OUTPUT_DP) {
		link = output->sink_max_khz * output->sink_lanes / I915_SKL_OUTPUT_LINK_BYTES;
		if (limit > link)
			limit = link;
	}

	/* The EDID's first timing that fits. */
	edid = NULL;
	if (output->edid_bytes != 0U)
		edid = output->edid;
	error = drv_i915_skl_pick_mode(edid, output->edid_bytes, limit, &output->mode);
	if (error != 0)
		kern_memset(&output->mode, 0, sizeof(output->mode));

	/* Larger than the display window holds: the safe mode. */
	if (output->mode.hdisplay > I915_SKL_OUTPUT_MAX_WIDTH || output->mode.vdisplay > I915_SKL_OUTPUT_MAX_HEIGHT) {
		error = drv_i915_skl_pick_mode(NULL, 0U, limit, &output->mode);
		if (error != 0)
			kern_memset(&output->mode, 0, sizeof(output->mode));
	}
}

/*
 * Works out how an output is lit: a pipe neither the panel nor another
 * lit output uses (its transcoder has the same number), a DPLL from 1 to
 * 3 neither uses, the DP link or the HDMI clock, and the DPLL's words.
 * Returns 0, or ENOSPC.
 */
static int
i915_skl_outputs_config(
	struct i915_skl_outputs *outputs,
	struct i915_skl_output *output)
{
	struct i915_skl_output_config *config;
	unsigned pipes_used;
	unsigned dplls_used;
	unsigned index;
	unsigned pipe;
	unsigned dpll;
	unsigned count;
	int error;

	/* What the panel and the lit outputs use. */
	pipes_used = 1U << outputs->panel.pipe;
	dplls_used = (1U << 0) | (1U << outputs->panel.dpll);
	for (index = 0; index < I915_SKL_OUTPUTS; index++) {
		if (!outputs->output[index].lit)
			continue;
		pipes_used |= 1U << outputs->output[index].config.pipe;
		dplls_used |= 1U << outputs->output[index].config.dpll;
	}

	/* The first free pipe and DPLL. */
	for (pipe = 0; pipe < I915_SKL_PIPES; pipe++) {
		if ((pipes_used & (1U << pipe)) == 0U)
			break;
	}

	/* DPLL0 is the CD clock's. */
	for (dpll = 1; dpll <= 3U; dpll++) {
		if ((dplls_used & (1U << dpll)) == 0U)
			break;
	}

	/* None left, or no mode. */
	if (pipe == I915_SKL_PIPES || dpll > 3U || output->mode.clock_khz <= 0)
		return ENOSPC;

	/* The pipe and DPLL, the mode, the part. */
	config = &output->config;
	kern_memset(config, 0, sizeof(*config));
	config->type = output->type;
	config->port = output->port;
	config->pipe = pipe;
	config->dpll = dpll;
	config->mode = output->mode;
	config->sku = outputs->sku;
	config->platform = outputs->platform;

	/* HDMI: the WRPLL at the pixel clock. */
	if (output->type == I915_SKL_OUTPUT_HDMI) {
		error = drv_i915_skl_dpll_hdmi((uint32_t)output->mode.clock_khz, I915_SKL_REF_KHZ, &config->dpll_words);
		if (error != 0)
			return ENOSPC;
		return 0;
	}

	/* DP: the link, its rate's DPLL code and the port's buffer table. */
	error = drv_i915_skl_dp_link(output->sink_max_khz, output->sink_lanes, (uint32_t)output->mode.clock_khz, &config->link);
	if (error != 0)
		return ENOSPC;
	error = drv_i915_skl_dpll_dp(config->link.port_khz, &config->dpll_words);
	if (error != 0)
		return ENOSPC;
	output->dp.edp = 0;
	output->dp.trans = drv_i915_skl_buf_trans(outputs->sku, outputs->platform, I915_SKL_OUTPUT_DP, output->port, &count);
	output->dp.trans_count = count;

	/* Succeeded: the output can be lit. */
	return 0;
}

/*
 * Fills the pipes the buffer is split between: the panel's, the lit
 * outputs', and the one about to be lit (with, or NULL).
 */
static void
i915_skl_outputs_pipes(
	const struct i915_skl_outputs *outputs,
	const struct i915_skl_output *with,
	struct i915_skl_pipe_wm pipes[3])
{
	const struct i915_skl_output *output;
	struct i915_skl_pipe_wm *wm;
	unsigned index;

	/* No pipe lit until it is filled in. */
	kern_memset(pipes, 0, 3U * sizeof(pipes[0]));

	/* The panel. */
	wm = &pipes[outputs->panel.pipe];
	wm->active = 1;
	wm->hdisplay = outputs->panel.hdisplay;
	wm->htotal = outputs->panel.htotal;
	wm->pixel_khz = outputs->panel.pixel_khz;
	wm->plane_width = outputs->panel.hdisplay;

	/* Each output lit or being lit. */
	for (index = 0; index < I915_SKL_OUTPUTS; index++) {
		output = &outputs->output[index];
		if (!output->lit && output != with)
			continue;
		wm = &pipes[output->config.pipe];
		wm->active = 1;
		wm->hdisplay = output->mode.hdisplay;
		wm->htotal = output->mode.htotal;
		wm->pixel_khz = (uint32_t)output->mode.clock_khz;
		wm->plane_width = output->mode.hdisplay;
	}
}

/*
 * Gives each lit pipe but one its allocation through the xd code's
 * display 9 buffer, in ascending or descending order of pipes.
 */
static void
i915_skl_outputs_rewrite(
	struct i915_skl_outputs *outputs,
	const struct i915_skl_pipe_wm pipes[3],
	int ascending,
	unsigned except)
{
	struct i915_xd9_pipe_plan plans[I915_SKL_PIPES];
	unsigned pipe;

	/* Succeeded: the pipes in the xd code's form, moved. */
	for (pipe = 0U; pipe < I915_SKL_PIPES; pipe++)
		drv_i915_skl_xd_plan(&pipes[pipe], &plans[pipe]);
	drv_i915_xd9_wm_arm_pipes(drv_i915_skl_xd_io(outputs->io), plans, I915_SKL_PIPES, ascending, except);
}

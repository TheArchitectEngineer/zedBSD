/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display the firmware left running, read before the driver's first
 * display write (see xd-takeover.h).
 */

#include "xd-takeover.h"
#include "xd-io.h"
#include "xd-platform.h"
#include "xd-power.h"
#include "display-internal.h"

#include "../intel/display-regs-xd.h"
#include "../intel/display-regs-xd-south.h"

#include <kern/klog.h>
#include <kern/kcrt.h>

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * The GPU's VT-d unit base (GFXVTBAR) as the MCHBAR mirror in the
 * display's register space shows it: bit 0 enables the unit, the page
 * above it is the unit's registers.
 *
 * Values referenced: Intel 12th Generation Intel Core Processors
 * Datasheet, Volume 2 (MCHBAR GFXVTBAR).
 */
#define I915_XD_FW_GFXVTBAR		0x145400U
#define I915_XD_FW_GFXVTBAR_ENABLE	0x1ULL
#define I915_XD_FW_GFXVTBAR_BASE	(~0xfffULL)

/*
 * The VT-d unit's global status bits the decision reads: translation
 * enabled, and interrupt remapping enabled; and the protected memory
 * enable and its status.
 *
 * Values referenced: Intel Virtualization Technology for Directed I/O
 * Architecture Specification (GSTS_REG TES and IRES, PMEN_REG EPM and
 * PRS).
 */
#define I915_XD_FW_VTD_TRANSLATES	0x80000000U
#define I915_XD_FW_VTD_REMAPS		0x02000000U
#define I915_XD_FW_VTD_PROTECTS		0x80000001U

/* A register read that is all ones: nothing decoded the access. */
#define I915_XD_FW_ALL_ONES		0xffffffffU

/* Where the DDI select and the mode sit in the transcoder's DDI function word. */
#define I915_XD_FW_DDI_SELECT_SHIFT	27U
#define I915_XD_FW_DDI_MODE_SHIFT	24U

/* The size of a GGTT page. */
#define I915_XD_FW_PAGE_SHIFT		12U
#define I915_XD_FW_PAGE_SIZE		4096U

static int i915_xd_fw_pipe_powered(struct i915_xd_io *io, const struct i915_xd_platform *platform, unsigned pipe);
static void i915_xd_fw_read_pipe(struct i915_xd_io *io, unsigned pipe, struct i915_xd_firmware *firmware);
static void i915_xd_fw_read_pipe_a_domain(struct i915_xd_io *io, const struct i915_xd_platform *platform, struct i915_xd_firmware *firmware);
static void i915_xd_fw_read_fb(const struct i915_xd_fw_env *env, struct i915_xd_firmware *firmware);
static void i915_xd_fw_read_vtd(struct i915_xd_io *io, const struct i915_xd_fw_env *env, struct i915_xd_firmware *firmware);
static void i915_xd_fw_vtd_walls(struct i915_xd_firmware *firmware);
static void i915_xd_fw_choose_stop(struct i915_xd_firmware *firmware);

/*
 * Reads what the firmware left on the display into a record, writing
 * nothing, and decides from it (drv_i915_xd_firmware_decide).  opregion
 * is the OpRegion's address (ASLS, 0 for none), which is only recorded.
 * A device without a description is recorded as one: the decision stops.
 * Returns 0, or ENODEV for a device without a description.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_modeset_setup.c
 * (intel_modeset_readout_hw_state, which reads a pipe only when its power
 * domain is on).
 */
int
drv_i915_xd_firmware_read(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	const struct i915_xd_fw_env *env,
	uint32_t opregion,
	struct i915_xd_firmware *firmware)
{
	unsigned pipe;
	unsigned pipes;
	int powered;

	/* Starts from an empty record with what the environment says. */
	kern_memset(firmware, 0, sizeof(*firmware));
	firmware->output_ddi = -1;
	firmware->hypervisor = env->hypervisor;
	firmware->ggtt_pages = env->ggtt_pages;
	firmware->driver_first_page = env->driver_first_page;
	firmware->opregion = opregion;

	/* The framebuffer and the VT-d unit are read whatever the display is. */
	i915_xd_fw_read_fb(env, firmware);
	i915_xd_fw_read_vtd(io, env, firmware);

	/* A device without a description: nothing on the display is known. */
	if (platform == NULL) {
		firmware->walls |= I915_XD_FW_WALL_UNDESCRIBED;
		drv_i915_xd_firmware_decide(firmware);
		return ENODEV;
	}

	/* The main well control: its states gate every pipe; all ones is not a register value. */
	firmware->well_control = drv_i915_xd_io_read(io, I915_XD_PWR_WELL_CTL);

	/* Reads every pipe whose wells are on, and marks the rest. */
	pipes = platform->pipes;
	if (pipes > I915_XD_FW_PIPES)
		pipes = I915_XD_FW_PIPES;
	for (pipe = 0U; pipe < I915_XD_FW_PIPES; pipe++) {
		/* A pipe the platform does not have reads as off. */
		if (pipe >= pipes) {
			firmware->pipe[pipe].state = I915_XD_FW_PIPE_OFF;
			firmware->unread_pipes |= 1U << pipe;
			continue;
		}

		/* A well state that is not a register value is a garbled pipe. */
		if (firmware->well_control == I915_XD_FW_ALL_ONES) {
			firmware->pipe[pipe].state = I915_XD_FW_PIPE_GARBLED;
			firmware->unread_pipes |= 1U << pipe;
			continue;
		}

		/* A pipe whose wells are off is not read. */
		powered = i915_xd_fw_pipe_powered(io, platform, pipe);
		if (!powered) {
			firmware->pipe[pipe].state = I915_XD_FW_PIPE_OFF;
			firmware->unread_pipes |= 1U << pipe;
			continue;
		}

		i915_xd_fw_read_pipe(io, pipe, firmware);
	}

	/* Pipe A's domain carries the panel's parts: read only when pipe A reads. */
	if (firmware->pipe[0].state == I915_XD_FW_PIPE_DARK || firmware->pipe[0].state == I915_XD_FW_PIPE_LIT)
		i915_xd_fw_read_pipe_a_domain(io, platform, firmware);

	/* The VGA plane, whose well is the always-on one. */
	firmware->vga_control = drv_i915_xd_io_read(io, I915_XD_VGA_CONTROL);

	/* Decides from the record. */
	drv_i915_xd_firmware_decide(firmware);

	/* Succeeded: the record and its decision. */
	return 0;
}

/*
 * Decides from a record alone whether the driver may use the display.
 *
 * Every wall is recorded, so one start names every wall it can see; the
 * first in order is the one that stops: a device without a description,
 * a pipe that did not read, a firmware scanout in the GGTT pages the
 * driver writes, a VT-d unit that does not read, one that translates or
 * protects memory.  A lit pipe is no stop: the driver takes the
 * firmware's output over before its first write.
 */
void
drv_i915_xd_firmware_decide(
	struct i915_xd_firmware *firmware)
{
	uint32_t last_page;
	unsigned pipe;

	/* Keeps only the wall the readout alone knows, and the OpRegion's record. */
	firmware->walls &= I915_XD_FW_WALL_UNDESCRIBED;
	if (firmware->opregion != 0U)
		firmware->walls |= I915_XD_FW_WALL_OPREGION;

	/* A lit pipe, and a pipe that did not read. */
	firmware->lit_pipes = 0U;
	for (pipe = 0U; pipe < I915_XD_FW_PIPES; pipe++) {
		if (firmware->pipe[pipe].state == I915_XD_FW_PIPE_LIT)
			firmware->lit_pipes |= 1U << pipe;
		if (firmware->pipe[pipe].state == I915_XD_FW_PIPE_GARBLED)
			firmware->walls |= I915_XD_FW_WALL_GARBLED;
	}
	if (firmware->lit_pipes != 0U)
		firmware->walls |= I915_XD_FW_WALL_LIT;

	/* The framebuffer may not reach the pages the driver writes. */
	if (firmware->fb_in_aperture && firmware->fb_first_page + firmware->fb_pages > firmware->driver_first_page)
		firmware->walls |= I915_XD_FW_WALL_OVERLAP;

	/* Nor may the surface of a lit pipe. */
	for (pipe = 0U; pipe < I915_XD_FW_PIPES; pipe++) {
		if ((firmware->lit_pipes & (1U << pipe)) == 0U)
			continue;

		last_page = firmware->pipe[pipe].plane_surface >> I915_XD_FW_PAGE_SHIFT;
		if (last_page + 1U > firmware->driver_first_page)
			firmware->walls |= I915_XD_FW_WALL_OVERLAP;
	}

	/* The VT-d unit of a machine that owns it. */
	i915_xd_fw_vtd_walls(firmware);

	/* Takes the first stop, or uses the display (taking a lit one over). */
	i915_xd_fw_choose_stop(firmware);
}

/*
 * Names the output the firmware lit, from the pipes the readout read and
 * the ports of the platform's description.
 *
 * The output is the lowest lit pipe whose transcoder drives a DDI (a
 * firmware that clones onto two outputs gives the lowest pipe).
 * DisplayPort SST on the panel's DDI (panel_ddi, -1 for none) is the
 * panel; HDMI or DVI on a combo port is the HDMI output; DisplayPort SST
 * on a Type-C port is the Type-C DisplayPort; anything else (a DDI the
 * description does not have or drives with no PHY, DisplayPort MST, HDMI
 * on a Type-C port, DisplayPort SST on a combo port that is not the
 * panel's) is an output the driver does not light.
 */
void
drv_i915_xd_firmware_classify(
	const struct i915_xd_platform *platform,
	int panel_ddi,
	struct i915_xd_firmware *firmware)
{
	const struct i915_xd_port_desc *port;
	uint32_t function;
	unsigned select;
	unsigned mode;
	unsigned pipe;

	/* Nothing lit until a pipe says so. */
	firmware->output = I915_XD_FW_OUTPUT_NONE;
	firmware->output_pipe = 0U;
	firmware->output_ddi = -1;
	firmware->output_mode = 0U;

	/* The lowest lit pipe that drives a DDI. */
	for (pipe = 0U; pipe < I915_XD_FW_PIPES; pipe++) {
		/* A pipe the firmware did not light. */
		if ((firmware->lit_pipes & (1U << pipe)) == 0U)
			continue;

		/* A DDI function that is off or drives no DDI is not an output. */
		function = firmware->pipe[pipe].ddi_function;
		select = (function & I915_XD_DDI_FUNC_SELECT) >> I915_XD_FW_DDI_SELECT_SHIFT;
		if ((function & I915_XD_DDI_FUNC_ENABLE) == 0U)
			continue;
		if (select == 0U)
			continue;

		/* The output: its pipe, DDI and mode. */
		mode = (function & I915_XD_DDI_FUNC_MODE) >> I915_XD_FW_DDI_MODE_SHIFT;
		firmware->output_pipe = pipe;
		firmware->output_ddi = (int)select - 1;
		firmware->output_mode = mode;
		firmware->output = I915_XD_FW_OUTPUT_OTHER;
		break;
	}

	/* No lit output, or no description to read the port in. */
	if (firmware->output == I915_XD_FW_OUTPUT_NONE)
		return;
	if (platform == NULL)
		return;

	/* A DDI the description does not have, or drives with no PHY, is not lit by the driver. */
	port = drv_i915_xd_platform_port(platform, (unsigned)firmware->output_ddi);
	if (port == NULL)
		return;
	if (port->phy_kind == I915_XD_PHY_NONE)
		return;

	/* The panel: DisplayPort SST on the panel's DDI. */
	if (firmware->output_ddi == panel_ddi && firmware->output_mode == I915_XD_DDI_FUNC_MODE_DP_SST) {
		firmware->output = I915_XD_FW_OUTPUT_PANEL;
		return;
	}

	/* HDMI or DVI on a combo port. */
	if (port->phy_kind == I915_XD_PHY_COMBO &&
	    (firmware->output_mode == I915_XD_DDI_FUNC_MODE_HDMI || firmware->output_mode == I915_XD_DDI_FUNC_MODE_DVI)) {
		firmware->output = I915_XD_FW_OUTPUT_HDMI;
		return;
	}

	/* DisplayPort SST on a Type-C port. */
	if (port->phy_kind == I915_XD_PHY_DEKEL && firmware->output_mode == I915_XD_DDI_FUNC_MODE_DP_SST)
		firmware->output = I915_XD_FW_OUTPUT_DP_TC;
}

/*
 * Names a kind of firmware output for the log.
 */
const char *
drv_i915_xd_firmware_output_name(
	unsigned output)
{
	/* One name per kind. */
	switch (output) {
	case I915_XD_FW_OUTPUT_NONE:
		return "none";
	case I915_XD_FW_OUTPUT_PANEL:
		return "panel";
	case I915_XD_FW_OUTPUT_HDMI:
		return "HDMI";
	case I915_XD_FW_OUTPUT_DP_TC:
		return "Type-C DP";
	default:
		break;
	}

	/* Succeeded: an output the driver does not light. */
	return "other";
}

/*
 * Logs the record and its decision in two lines: the firmware's display
 * (the lit and unread pipes, the output, the framebuffer's pages, the
 * VT-d unit) and the decision with its walls.
 */
void
drv_i915_xd_firmware_log(
	const struct i915_xd_firmware *firmware)
{
	/* What the firmware left. */
	kern_logf("i915: xd-takeover: lit pipes 0x%x, unread 0x%x, output %s (pipe %c, DDI %d, mode %u), framebuffer %s pages %u..%u, VT-d %s (status 0x%08x, protect 0x%08x)\n",
	    firmware->lit_pipes,
	    firmware->unread_pipes,
	    drv_i915_xd_firmware_output_name(firmware->output),
	    'A' + (int)firmware->output_pipe,
	    firmware->output_ddi,
	    firmware->output_mode,
	    firmware->fb_in_aperture ? "in the aperture" : "outside the aperture",
	    firmware->fb_first_page,
	    firmware->fb_first_page + firmware->fb_pages,
	    firmware->vtd_enabled ? "on" : "off",
	    firmware->vtd_status,
	    firmware->vtd_protect);

	/* The decision. */
	kern_logf("i915: xd-takeover: %s, take over %d, walls 0x%x, stop 0x%x: %s\n",
	    firmware->use ? "use" : "keep the firmware's picture",
	    firmware->take_over,
	    firmware->walls,
	    firmware->stop,
	    firmware->reason);
}

/* Tells whether every well of a pipe and its transcoder is on. */
static int
i915_xd_fw_pipe_powered(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	unsigned pipe)
{
	uint64_t wells;
	unsigned well;
	int powered;

	/* The wells of the pipe and of its transcoder. */
	wells = drv_i915_xd_power_wells_of(platform, I915_XD_POWERS_PIPE(pipe) | I915_XD_POWERS_TRANS(pipe));

	/* Each must be on. */
	for (well = 0U; well < platform->nwells; well++) {
		if ((wells & (1ULL << well)) == 0ULL)
			continue;

		powered = drv_i915_xd_power_well_powered(io, platform, well);
		if (!powered)
			return 0;
	}

	/* Succeeded: the pipe's registers read. */
	return 1;
}

/* Reads one powered pipe: its transcoder, DDI function, source size and primary plane. */
static void
i915_xd_fw_read_pipe(
	struct i915_xd_io *io,
	unsigned pipe,
	struct i915_xd_firmware *firmware)
{
	struct i915_xd_fw_pipe *record;

	/* The transcoder, its DDI function and the pipe's source size. */
	record = &firmware->pipe[pipe];
	record->trans_conf = drv_i915_xd_io_read(io, I915_XD_TRANS_CONF(pipe));
	record->ddi_function = drv_i915_xd_io_read(io, I915_XD_TRANS_DDI_FUNC_CTL(pipe));
	record->source_size = drv_i915_xd_io_read(io, I915_XD_PIPE_SRCSZ(pipe));

	/* The primary plane. */
	record->plane_ctl = drv_i915_xd_io_read(io, I915_XD_PLANE_CTL(pipe));
	record->plane_stride = drv_i915_xd_io_read(io, I915_XD_PLANE_STRIDE(pipe));
	record->plane_size = drv_i915_xd_io_read(io, I915_XD_PLANE_SIZE(pipe));
	record->plane_surface = drv_i915_xd_io_read(io, I915_XD_PLANE_SURF(pipe));

	/* A transcoder or plane word of all ones is not a register value. */
	if (record->trans_conf == I915_XD_FW_ALL_ONES || record->plane_ctl == I915_XD_FW_ALL_ONES) {
		record->state = I915_XD_FW_PIPE_GARBLED;
		return;
	}

	/* An enabled transcoder or plane makes the pipe lit. */
	record->state = I915_XD_FW_PIPE_DARK;
	if ((record->trans_conf & I915_XD_TRANS_CONF_ENABLE) != 0U || (record->plane_ctl & I915_XD_PLANE_CTL_ENABLE) != 0U)
		record->state = I915_XD_FW_PIPE_LIT;
}

/* Reads pipe A's domain: the first two combo PLLs, the DDI A buffer, the panel, its PWM and the clock map. */
static void
i915_xd_fw_read_pipe_a_domain(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	struct i915_xd_firmware *firmware)
{
	unsigned index;
	unsigned found;

	/* The enables of the first two combo PLLs. */
	found = 0U;
	for (index = 0U; index < platform->nplls; index++) {
		if (found == 2U)
			break;
		if (platform->plls[index].kind != I915_XD_PLL_COMBO)
			continue;

		firmware->pll_enable[found] = drv_i915_xd_io_read(io, platform->plls[index].enable);
		found++;
	}

	/* The DDI A buffer, the panel's power sequencer and its backlight PWM. */
	firmware->ddi_a_buffer = drv_i915_xd_io_read(io, I915_XD_DDI_BUF_CTL(0U));
	firmware->panel_status = drv_i915_xd_io_read(io, I915_XD_PP_STATUS(0U));
	firmware->panel_control = drv_i915_xd_io_read(io, I915_XD_PP_CONTROL(0U));
	firmware->pwm_control = drv_i915_xd_io_read(io, I915_XD_SBLC_PWM_CTL(0U));
	firmware->pwm_duty = drv_i915_xd_io_read(io, I915_XD_SBLC_PWM_DUTY(0U));

	/* The DDI clock map. */
	firmware->clock_map = drv_i915_xd_io_read(io, I915_XD_DPCLKA_CFGCR0);
}

/* Records the firmware's framebuffer and the GGTT pages it takes in the aperture. */
static void
i915_xd_fw_read_fb(
	const struct i915_xd_fw_env *env,
	struct i915_xd_firmware *firmware)
{
	uint64_t aperture_end;

	/* No framebuffer: nothing to record. */
	if (!env->fb_present)
		return;
	if (env->fb_size == 0U)
		return;

	/* The framebuffer. */
	firmware->fb_present = 1;
	firmware->fb_base = env->fb_base;
	firmware->fb_size = env->fb_size;

	/* One inside the aperture takes GGTT pages. */
	aperture_end = env->aperture_base + env->aperture_size;
	if (env->fb_base < env->aperture_base)
		return;
	if (env->fb_base + env->fb_size > aperture_end)
		return;
	firmware->fb_in_aperture = 1;
	firmware->fb_first_page = (uint32_t)((env->fb_base - env->aperture_base) >> I915_XD_FW_PAGE_SHIFT);
	firmware->fb_pages = (uint32_t)((env->fb_size + I915_XD_FW_PAGE_SIZE - 1U) >> I915_XD_FW_PAGE_SHIFT);
}

/* Reads the GPU's VT-d unit through the MCHBAR mirror and the environment's reader. */
static void
i915_xd_fw_read_vtd(
	struct i915_xd_io *io,
	const struct i915_xd_fw_env *env,
	struct i915_xd_firmware *firmware)
{
	uint32_t low;
	uint32_t high;
	uint64_t base;
	int error;

	/* The unit's base, low half first. */
	low = drv_i915_xd_io_read(io, I915_XD_FW_GFXVTBAR);
	high = drv_i915_xd_io_read(io, I915_XD_FW_GFXVTBAR + 4U);
	firmware->vtd_bar = (uint64_t)low | (uint64_t)high << 32;

	/* A disabled unit, or one at address 0, is not read. */
	firmware->vtd_enabled = (int)(firmware->vtd_bar & I915_XD_FW_GFXVTBAR_ENABLE);
	base = firmware->vtd_bar & I915_XD_FW_GFXVTBAR_BASE;
	if (!firmware->vtd_enabled)
		return;
	if (base == 0U)
		return;
	if (env->vtd_read == NULL)
		return;

	/* The version, the global status and the protected memory enable. */
	error = env->vtd_read(env->context, base, &firmware->vtd_version, &firmware->vtd_status, &firmware->vtd_protect);
	if (error != 0)
		return;

	/* A real unit reports a version that is neither zero nor all ones (a guest may see nothing there). */
	if (firmware->vtd_version != 0U && firmware->vtd_version != I915_XD_FW_ALL_ONES)
		firmware->vtd_readable = 1;
}

/* Records the walls of a VT-d unit the machine owns (a guest's unit is the host's). */
static void
i915_xd_fw_vtd_walls(
	struct i915_xd_firmware *firmware)
{
	/* A guest, or no unit: nothing to record. */
	if (firmware->hypervisor)
		return;
	if (!firmware->vtd_enabled)
		return;

	/* A unit that does not read. */
	if (!firmware->vtd_readable) {
		firmware->walls |= I915_XD_FW_WALL_VTD_HIDDEN;
		return;
	}

	/* Translation, a protected memory region, interrupt remapping. */
	if ((firmware->vtd_status & I915_XD_FW_VTD_TRANSLATES) != 0U)
		firmware->walls |= I915_XD_FW_WALL_VTD_TRANSLATES;
	if ((firmware->vtd_protect & I915_XD_FW_VTD_PROTECTS) != 0U)
		firmware->walls |= I915_XD_FW_WALL_VTD_PROTECTS;
	if ((firmware->vtd_status & I915_XD_FW_VTD_REMAPS) != 0U)
		firmware->walls |= I915_XD_FW_WALL_VTD_REMAPS;
}

/* Takes the first stop in order, or uses the display (and takes a lit one over). */
static void
i915_xd_fw_choose_stop(
	struct i915_xd_firmware *firmware)
{
	unsigned vtd_dma;

	/* The order of the stops. */
	firmware->use = 0;
	firmware->take_over = 0;
	firmware->stop = 0U;
	vtd_dma = firmware->walls & (I915_XD_FW_WALL_VTD_TRANSLATES | I915_XD_FW_WALL_VTD_PROTECTS);
	if ((firmware->walls & I915_XD_FW_WALL_UNDESCRIBED) != 0U) {
		firmware->stop = I915_XD_FW_WALL_UNDESCRIBED;
		firmware->reason = "the device has no description: the display is not known";
	} else if ((firmware->walls & I915_XD_FW_WALL_GARBLED) != 0U) {
		firmware->stop = I915_XD_FW_WALL_GARBLED;
		firmware->reason = "a pipe did not read as register values";
	} else if ((firmware->walls & I915_XD_FW_WALL_OVERLAP) != 0U) {
		firmware->stop = I915_XD_FW_WALL_OVERLAP;
		firmware->reason = "the firmware's scanout overlaps the GGTT pages the driver writes";
	} else if ((firmware->walls & I915_XD_FW_WALL_VTD_HIDDEN) != 0U) {
		firmware->stop = I915_XD_FW_WALL_VTD_HIDDEN;
		firmware->reason = "the GPU's VT-d unit is on but does not read";
	} else if (vtd_dma != 0U) {
		firmware->stop = vtd_dma;
		firmware->reason = "the GPU's VT-d unit translates or protects memory, and there is no IOMMU driver";
	} else if (firmware->hypervisor) {
		firmware->use = 1;
		firmware->reason = "no wall (a guest: the host owns the VT-d unit)";
	} else {
		firmware->use = 1;
		firmware->reason = "no wall (DMA is not translated)";
	}

	/* A lit display is taken over before the first write. */
	if (firmware->use && (firmware->walls & I915_XD_FW_WALL_LIT) != 0U) {
		firmware->take_over = 1;
		firmware->reason = "a pipe is lit: the driver takes the firmware's output over before its first write";
	}
}

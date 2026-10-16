/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The power wells of the display 12 and 13 code (see xd-power.h).
 */

#include "xd-io.h"
#include "xd-platform.h"
#include "xd-power.h"

#include "../intel/display-regs-xd.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>

/*
 * How long the hardware may take: a well's state after its request when
 * the table gives no time, a power gate's fuses, and a well's state
 * after its request is withdrawn.
 *
 * Values referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (hsw_wait_for_power_well_enable, gen9_wait_for_power_well_fuses,
 * hsw_wait_for_power_well_disable).
 */
#define I915_XD_POWER_ENABLE_US		1000U
#define I915_XD_POWER_FUSE_US		1000U
#define I915_XD_POWER_DISABLE_US	1000U

/* The power gate whose fuses come before power well 1, and power well 1's own. */
#define I915_XD_POWER_GATE_0		0U
#define I915_XD_POWER_GATE_1		1U

/* The distance from a well control to its BIOS copy, the first of the four. */
#define I915_XD_POWER_BIOS_OFFSET	4U

static const struct i915_xd_well_desc *i915_xd_power_desc(const struct i915_xd_platform *platform, unsigned well);
static uint32_t i915_xd_power_control(unsigned control);
static int i915_xd_power_tc_port(const struct i915_xd_platform *platform, const struct i915_xd_well_desc *desc, unsigned *aux);
static int i915_xd_power_wait_fuses(struct i915_xd_io *io, unsigned gate, struct i915_xd_well_report *report);

/*
 * Finds the table entry of a well given by its control and its index
 * there.  Returns the entry, or -1 when the platform has no such well.
 */
int
drv_i915_xd_power_find(
	const struct i915_xd_platform *platform,
	unsigned control,
	unsigned index)
{
	const struct i915_xd_well_desc *desc;
	unsigned well;

	/* Looks at each well of the table. */
	for (well = 0U; well < platform->nwells; well++) {
		desc = &platform->wells[well];
		if (desc->control != control)
			continue;
		if (desc->index != index)
			continue;

		/* Succeeded: the well's entry. */
		return (int)well;
	}

	/* The platform has no such well. */
	return -1;
}

/*
 * Brings up a well that has a request and a state bit.
 *
 * Power well 1 waits for the fuses of power gate 0 first (and on the
 * platforms that need it sets the FLR source chicken bit); a Type-C AUX
 * well routes its channel through the Thunderbolt IO or the PHY's own;
 * the request is set and the state waited for (a state that does not
 * come is noted in the report and the enable goes on, as a Thunderbolt
 * AUX well with its tunnel down never reports); the DisplayPort AUX well
 * of a Type-C port waits for its PHY's microcontroller; a well with fuses
 * waits for them (late fuses are noted); then the owner rewrites the VGA
 * registers and restores the pipe interrupts inside the well.  Returns 0,
 * EINVAL for a well that is not switched by a request, or EIO when the
 * time base failed (the caller stops).
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (hsw_power_well_enable, icl_tc_phy_aux_power_well_enable,
 * hsw_wait_for_power_well_enable, hsw_power_well_post_enable).
 */
int
drv_i915_xd_power_well_enable(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	unsigned well,
	const struct i915_xd_power_hooks *hooks,
	struct i915_xd_well_report *report)
{
	const struct i915_xd_well_desc *desc;
	uint32_t control;
	uint32_t state;
	unsigned timeout_us;
	unsigned aux;
	int tc_port;
	int error;

	/* Starts with nothing to report. */
	report->state_late = 0;
	report->fuses_late = 0U;

	/* Refuses a well the table does not switch by a request. */
	desc = i915_xd_power_desc(platform, well);
	if (desc == NULL)
		return EINVAL;
	control = i915_xd_power_control(desc->control);
	if (control == 0U)
		return EINVAL;

	/* Power well 1 needs the FLR source chicken bit where the platform asks for it, and power gate 0's fuses. */
	if (desc->fuse_gate == I915_XD_POWER_GATE_1) {
		if ((platform->wa & I915_XD_WA_PW1_FLR_SOURCE) != 0U)
			(void)drv_i915_xd_io_rmw(io, I915_XD_CHICKEN_DCPR_1, 0U, I915_XD_DCPR_1_DISABLE_FLR_SRC);
		error = i915_xd_power_wait_fuses(io, I915_XD_POWER_GATE_0, report);
		if (error != 0)
			return error;
	}

	/* A Type-C AUX well routes its channel through the Thunderbolt IO or the PHY's own. */
	tc_port = i915_xd_power_tc_port(platform, desc, &aux);
	if (tc_port >= 0) {
		if ((desc->flags & I915_XD_WELL_TBT_AUX) != 0U) {
			(void)drv_i915_xd_io_rmw(io, I915_XD_DDI_AUX_CTL(aux), 0U, I915_XD_AUX_CTL_TBT_IO);
		} else {
			(void)drv_i915_xd_io_rmw(io, I915_XD_DDI_AUX_CTL(aux), I915_XD_AUX_CTL_TBT_IO, 0U);
		}
	}

	/* Requests the well. */
	(void)drv_i915_xd_io_rmw(io, control, 0U, I915_XD_PWR_WELL_REQUEST(desc->index));

	/* Waits for the state; a state that does not come is noted and the enable goes on. */
	timeout_us = (unsigned)desc->enable_timeout_ms * 1000U;
	if (timeout_us == 0U)
		timeout_us = I915_XD_POWER_ENABLE_US;
	state = I915_XD_PWR_WELL_STATE(desc->index);
	error = drv_i915_xd_io_poll(io, control, state, state, timeout_us, NULL);
	if (error == EIO)
		return EIO;
	if (error != 0)
		report->state_late = 1;

	/* The DisplayPort AUX well of a Type-C port waits for the PHY's microcontroller. */
	if (tc_port >= 0 &&
	    (desc->flags & I915_XD_WELL_TBT_AUX) == 0U &&
	    hooks != NULL &&
	    hooks->tc_health != NULL) {
		error = hooks->tc_health(hooks->context, (unsigned)tc_port);
		if (error == EIO)
			return EIO;
	}

	/* A well with fuses waits for its power gate's. */
	if (desc->fuse_gate != I915_XD_WELL_NO_FUSE) {
		error = i915_xd_power_wait_fuses(io, desc->fuse_gate, report);
		if (error != 0)
			return error;
	}

	/* The VGA registers inside the well are rewritten. */
	if ((desc->flags & I915_XD_WELL_VGA) != 0U &&
	    hooks != NULL &&
	    hooks->vga_reset != NULL)
		hooks->vga_reset(hooks->context);

	/* The pipe interrupts inside the well are restored. */
	if (desc->irq_pipes != 0U &&
	    hooks != NULL &&
	    hooks->pipes_up != NULL)
		hooks->pipes_up(hooks->context, desc->irq_pipes);

	/* Succeeded: the well is requested and up. */
	return 0;
}

/*
 * Takes down a well that has a request and a state bit.
 *
 * The owner drains the pipe interrupts inside the well first (a failed
 * drain keeps the well on); then the driver's request is withdrawn, and
 * when the BIOS does not request the well too its state is waited for (a
 * state that stays is noted).  Returns 0, EINVAL for a well that is not
 * switched by a request, EBUSY when the drain failed, or EIO when the
 * time base failed.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (hsw_power_well_disable, hsw_power_well_pre_disable,
 * hsw_wait_for_power_well_disable).
 */
int
drv_i915_xd_power_well_disable(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	unsigned well,
	const struct i915_xd_power_hooks *hooks,
	struct i915_xd_well_report *report)
{
	const struct i915_xd_well_desc *desc;
	uint32_t control;
	uint32_t request;
	uint32_t bios;
	int drained;
	int error;

	/* Starts with nothing to report. */
	report->state_late = 0;
	report->fuses_late = 0U;

	/* Refuses a well the table does not switch by a request. */
	desc = i915_xd_power_desc(platform, well);
	if (desc == NULL)
		return EINVAL;
	control = i915_xd_power_control(desc->control);
	if (control == 0U)
		return EINVAL;

	/* The owner drains the pipe interrupts inside the well; a failed drain keeps it on. */
	if (desc->irq_pipes != 0U &&
	    hooks != NULL &&
	    hooks->pipes_down != NULL) {
		drained = hooks->pipes_down(hooks->context, desc->irq_pipes);
		if (drained != 0)
			return EBUSY;
	}

	/* Withdraws the driver's request. */
	request = I915_XD_PWR_WELL_REQUEST(desc->index);
	(void)drv_i915_xd_io_rmw(io, control, request, 0U);

	/* While the BIOS still requests the well its state stays, as expected. */
	bios = drv_i915_xd_io_read(io, control - I915_XD_POWER_BIOS_OFFSET);
	if ((bios & request) != 0U)
		return 0;

	/* Waits for the state to clear; a state that stays is noted. */
	error = drv_i915_xd_io_poll(io, control, I915_XD_PWR_WELL_STATE(desc->index), 0U, I915_XD_POWER_DISABLE_US, NULL);
	if (error == EIO)
		return EIO;
	if (error != 0)
		report->state_late = 1;

	/* Succeeded: the driver's request is gone. */
	return 0;
}

/*
 * Tells whether the driver owns a well that is on: its driver request
 * and its state are both set.  A well not switched by a request reads
 * as off.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (hsw_power_well_enabled).
 */
int
drv_i915_xd_power_well_owned(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	unsigned well)
{
	const struct i915_xd_well_desc *desc;
	uint32_t control;
	uint32_t bits;
	uint32_t value;

	/* A well the table does not switch by a request is not owned. */
	desc = i915_xd_power_desc(platform, well);
	if (desc == NULL)
		return 0;
	control = i915_xd_power_control(desc->control);
	if (control == 0U)
		return 0;

	/* Both the driver's request and the state. */
	bits = I915_XD_PWR_WELL_REQUEST(desc->index) | I915_XD_PWR_WELL_STATE(desc->index);
	value = drv_i915_xd_io_read(io, control);
	if ((value & bits) != bits)
		return 0;

	/* Succeeded: the driver owns the well and it is on. */
	return 1;
}

/*
 * Takes a well's request over from the BIOS: when the BIOS requests the
 * well, the driver's request is set first (when it is not set already)
 * and the BIOS's cleared after it, so the well never goes down between.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (hsw_power_well_sync_hw).
 */
void
drv_i915_xd_power_well_adopt(
	struct i915_xd_io *io,
	const struct i915_xd_platform *platform,
	unsigned well)
{
	const struct i915_xd_well_desc *desc;
	uint32_t control;
	uint32_t request;
	uint32_t bios;
	uint32_t driver;

	/* A well the table does not switch by a request has nothing to adopt. */
	desc = i915_xd_power_desc(platform, well);
	if (desc == NULL)
		return;
	control = i915_xd_power_control(desc->control);
	if (control == 0U)
		return;

	/* Nothing to take over when the BIOS does not request the well. */
	request = I915_XD_PWR_WELL_REQUEST(desc->index);
	bios = drv_i915_xd_io_read(io, control - I915_XD_POWER_BIOS_OFFSET);
	if ((bios & request) == 0U)
		return;

	/* The driver's request first, then the BIOS's withdrawn. */
	driver = drv_i915_xd_io_read(io, control);
	if ((driver & request) == 0U)
		drv_i915_xd_io_write(io, control, driver | request);
	drv_i915_xd_io_write(io, control - I915_XD_POWER_BIOS_OFFSET, bios & ~request);
}

/* Returns a well's entry in the platform's table, or NULL past its end. */
static const struct i915_xd_well_desc *
i915_xd_power_desc(
	const struct i915_xd_platform *platform,
	unsigned well)
{
	/* An entry past the table has no description. */
	if (well >= platform->nwells)
		return NULL;

	/* Succeeded: the entry. */
	return &platform->wells[well];
}

/* Returns the driver's request register of a kind of control, or 0 for a well without one. */
static uint32_t
i915_xd_power_control(
	unsigned control)
{
	/* Names the register of the control. */
	switch (control) {
	case I915_XD_WELL_CONTROL_MAIN:
		return I915_XD_PWR_WELL_CTL;
	case I915_XD_WELL_CONTROL_AUX:
		return I915_XD_PWR_WELL_CTL_AUX;
	case I915_XD_WELL_CONTROL_DDI:
		return I915_XD_PWR_WELL_CTL_DDI;
	default:
		break;
	}

	/* DC off and the mailbox wells have no request bit. */
	return 0U;
}

/*
 * Returns the Type-C port (0 for the first) whose AUX well this is, and
 * in *aux that port's AUX channel; -1 for any other well.  A Thunderbolt
 * AUX well serves the port of its number, a DisplayPort AUX well the
 * port whose AUX channel it powers when that port's PHY is a Dekel PHY.
 */
static int
i915_xd_power_tc_port(
	const struct i915_xd_platform *platform,
	const struct i915_xd_well_desc *desc,
	unsigned *aux)
{
	const struct i915_xd_port_desc *port;
	unsigned tc;
	unsigned index;

	/* Only an AUX well can be a Type-C port's. */
	if (desc->control != I915_XD_WELL_CONTROL_AUX)
		return -1;

	/* The Type-C number a Thunderbolt AUX well serves (1 for the first). */
	tc = 0U;
	if ((desc->flags & I915_XD_WELL_TBT_AUX) != 0U)
		tc = desc->index - I915_XD_WELL_AUX_TBT(0U) + 1U;

	/* Finds the port: by its Type-C number, or by the AUX channel the well powers. */
	for (index = 0U; index < platform->nports; index++) {
		port = &platform->ports[index];
		if (port->phy_kind != I915_XD_PHY_DEKEL || port->tc == 0U)
			continue;
		if (tc != 0U && port->tc != tc)
			continue;
		if (tc == 0U && port->aux != desc->index)
			continue;

		/* Succeeded: the port's Type-C number and its AUX channel. */
		*aux = port->aux;
		return (int)port->tc - 1;
	}

	/* A combo PHY's AUX well, or a Thunderbolt well of a port the platform does not have. */
	return -1;
}

/*
 * Waits for a power gate's fuses to be distributed.  Late fuses are
 * noted in the report and the caller goes on.  Returns 0, or EIO when
 * the time base failed.
 *
 * Procedure referenced: Linux v6.8.12
 * drivers/gpu/drm/i915/display/intel_display_power_well.c
 * (gen9_wait_for_power_well_fuses).
 */
static int
i915_xd_power_wait_fuses(
	struct i915_xd_io *io,
	unsigned gate,
	struct i915_xd_well_report *report)
{
	int error;

	/* Waits for the gate's bit. */
	error = drv_i915_xd_io_poll(io, I915_XD_FUSE_STATUS, I915_XD_FUSE_STATUS_PG(gate), I915_XD_FUSE_STATUS_PG(gate), I915_XD_POWER_FUSE_US, NULL);
	if (error == EIO)
		return EIO;
	if (error != 0)
		report->fuses_late |= 1U << gate;

	/* Succeeded: the wait is over. */
	return 0;
}

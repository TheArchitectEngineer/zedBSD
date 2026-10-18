/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel's backlight of a Gen9 node (see gen9-backlight.h).
 *
 * The PWM is the south display's; the xd code reads and drives it in
 * either of its layouts (xd-panel.c).  The firmware's period, enable and
 * polarity are kept; the backlight device reads and writes the duty only,
 * and refuses (EBUSY) while the PWM does not run.  A brightness of 0 % is
 * the PWM's floor, not darkness.
 *
 * XXX: one device; a panel whose backlight the VBT puts on the DPCD (eDP
 * AUX backlight) or on the utility pin is not driven.
 */

#include "internal.h"
#include "display.h"
#include "gen9-backlight.h"
#include "gen9-display.h"
#include "xd-io.h"
#include "xd-panel.h"
#include "xd-platform.h"
#include <kern/kcrt.h>

#include "../i915.h"

#include <kern/backlight.h>
#include <kern/klog.h>

#include <uapi/backlight.h>
#include <uapi/errno.h>
#include <stddef.h>

/*
 * The panel's backlight of the node.
 *
 * Filled by the start before the device is published and emptied by the
 * stop after it is withdrawn; the class calls the operations one at a
 * time in between, and they only read the fields.
 */
struct i915_gen9_backlight {
	/* The device's display registers, and whether the PWM has the packed layout of the Sunrise Point family. */
	struct i915_xd_io io;
	int packed;

	/* The published device; NULL while none is. */
	struct kern_backlight *published;
};

/*
 * The panel's backlight of the one device.
 *
 * Zero means no backlight device: the start fills it, the stop empties
 * it.
 */
static struct i915_gen9_backlight i915_gen9_backlight_state;

/*
 * The register window the sleep's light off and back on use, the PWM
 * control a sleep turned off, and whether one did.  Written by the suspend
 * and the resume, which the one thread of S0 idle runs one after the
 * other.
 */
static struct i915_xd_io i915_gen9_backlight_sleep_io;
static uint32_t i915_gen9_backlight_slept_control;
static int i915_gen9_backlight_slept;

static int i915_gen9_backlight_get(void *context, uint32_t *percent);
static int i915_gen9_backlight_set(void *context, uint32_t percent);

/* The provider's operations. */
static const struct kern_backlight_ops i915_gen9_backlight_ops = {
	.get = i915_gen9_backlight_get,
	.set = i915_gen9_backlight_set
};

static int i915_gen9_backlight_layout(struct i915_device *device, int *packed);
static void i915_gen9_backlight_read(struct i915_gen9_backlight *backlight, struct i915_skl_pwm_registers *registers);
static void i915_gen9_backlight_from_xd(const struct i915_xd9_pwm *xd, struct i915_skl_pwm *pwm);
static void i915_gen9_backlight_to_xd(const struct i915_skl_pwm *pwm, struct i915_xd9_pwm *xd);

/*
 * Decodes the PWM's registers.
 *
 * Returns 0 with the PWM, or EINVAL when the period is zero (the firmware
 * did not program it, so no duty means anything).
 */
int
drv_i915_skl_pwm_decode(
	const struct i915_skl_pwm_registers *registers,
	struct i915_skl_pwm *pwm)
{
	struct i915_xd9_pwm xd;
	int error;

	/* The xd code's decode in the layout. */
	error = drv_i915_xd9_pwm_decode(registers->control, registers->period, registers->duty, !registers->cnp, &xd);
	i915_gen9_backlight_from_xd(&xd, pwm);
	if (error != 0)
		return error;

	/* Succeeded: the PWM can be read and changed. */
	return 0;
}

/*
 * Reports the duty a brightness maps to.
 */
uint32_t
drv_i915_skl_pwm_duty(
	const struct i915_skl_pwm *pwm,
	uint32_t percent)
{
	struct i915_xd9_pwm xd;
	uint32_t duty;

	/* The xd code's mapping. */
	i915_gen9_backlight_to_xd(pwm, &xd);
	duty = drv_i915_xd9_pwm_duty(&xd, percent);

	/* Succeeded: the duty. */
	return duty;
}

/*
 * Reports the brightness a duty maps to.
 */
uint32_t
drv_i915_skl_pwm_percent(
	const struct i915_skl_pwm *pwm,
	uint32_t duty)
{
	struct i915_xd9_pwm xd;
	uint32_t percent;

	/* The xd code's mapping. */
	i915_gen9_backlight_to_xd(pwm, &xd);
	percent = drv_i915_xd9_pwm_percent(&xd, duty);

	/* Succeeded: the brightness. */
	return percent;
}

/*
 * Offers the panel's backlight when the step of i915.gen9.display= asks
 * for it, the firmware lit the eDP panel and its PWM runs.
 *
 * Every outcome is logged; the node works without the device.
 */
void
drv_i915_gen9_backlight_start(
	struct i915_device *device)
{
	struct i915_gen9_backlight *backlight;
	struct i915_skl_pwm_registers registers;
	struct i915_skl_pwm pwm;
	int level;
	int lit;
	int error;

	/* The backlight of the one device. */
	backlight = &i915_gen9_backlight_state;

	/* Only a Gen9 node, once. */
	if (device->gt.graphics_ver != 9U)
		return;
	if (backlight->published != NULL)
		return;

	/* The step the node takes (the boot line's, lowered by a step that failed) must take the backlight. */
	level = drv_i915_gen9_display_step();
	if (level < I915_GEN9_DISPLAY_BACKLIGHT)
		return;

	/* The panel must be lit for its PWM to mean anything. */
	drv_i915_xd_io_init(&backlight->io, &device->gt.mmio, NULL);
	lit = drv_i915_xd9_panel_lit(&backlight->io);
	if (!lit) {
		kern_logf("i915: backlight: the eDP panel is not lit; no backlight device\n");
		return;
	}

	/* Which layout the south display's PWM has. */
	error = i915_gen9_backlight_layout(device, &backlight->packed);
	if (error != 0) {
		kern_logf("i915: backlight: the south display is not one whose PWM is known; no backlight device\n");
		kern_memset(backlight, 0, sizeof(*backlight));
		return;
	}

	/* What the firmware set the PWM to. */
	i915_gen9_backlight_read(backlight, &registers);
	error = drv_i915_skl_pwm_decode(&registers, &pwm);
	kern_logf("i915: backlight: %s PWM control 0x%08x period 0x%08x duty 0x%08x: %s, max %u min %u duty %u (%u %%)%s\n",
	    backlight->packed ? "SPT" : "CNP",
	    registers.control,
	    registers.period,
	    registers.duty,
	    pwm.enabled ? "on" : "off",
	    pwm.max,
	    pwm.min,
	    pwm.duty,
	    drv_i915_skl_pwm_percent(&pwm, pwm.duty),
	    pwm.active_low ? ", active low" : "");

	/* A PWM without a period, or one that does not run, does not drive this panel's light. */
	if (error != 0 || !pwm.enabled) {
		kern_logf("i915: backlight: the PWM does not drive the panel (the light may be the DPCD's or the EC's); no backlight device\n");
		kern_memset(backlight, 0, sizeof(*backlight));
		return;
	}

	/* Publishes the device the Settings app's brightness reaches. */
	error = kern_backlight_register("i915", BACKLIGHT_TYPE_PANEL, &i915_gen9_backlight_ops, backlight, &backlight->published);
	if (error != 0) {
		kern_logf("i915: backlight: the device could not be registered rc=%d\n", error);
		kern_memset(backlight, 0, sizeof(*backlight));
		return;
	}

	/* Succeeded: says the device is there. */
	kern_logf("i915: backlight: the panel's light is a backlight device\n");
}

/*
 * Withdraws the backlight device; no operation runs after this returns.
 */
void
drv_i915_gen9_backlight_stop(
	struct i915_device *device)
{
	struct i915_gen9_backlight *backlight;

	UNUSED_PARAMETER(device);

	/* The backlight of the one device. */
	backlight = &i915_gen9_backlight_state;

	/* Nothing was published. */
	if (backlight->published == NULL)
		return;

	/* The class runs no operation after the withdrawal. */
	kern_backlight_unregister(backlight->published);
	kern_memset(backlight, 0, sizeof(*backlight));
}

/*
 * Turns the panel's light off for a sleep when the eDP panel is lit and
 * the PWM runs; the control word is kept for the resume.  Works whatever
 * the step of i915.gen9.display, without the backlight device.
 */
void
drv_i915_gen9_backlight_suspend(
	struct i915_device *device)
{
	int lit;
	int paused;

	/* Nothing to give back unless this turns the light off. */
	i915_gen9_backlight_slept = 0;
	drv_i915_xd_io_init(&i915_gen9_backlight_sleep_io, &device->gt.mmio, NULL);

	/* A lit panel. */
	lit = drv_i915_xd9_panel_lit(&i915_gen9_backlight_sleep_io);
	if (!lit)
		return;

	/* Its PWM stopped, when it runs. */
	paused = drv_i915_xd9_pwm_pause(&i915_gen9_backlight_sleep_io, &i915_gen9_backlight_slept_control);
	if (!paused)
		return;

	/* Succeeded: the light is off and its control kept. */
	i915_gen9_backlight_slept = 1;
	kern_logf("i915: backlight: the panel's light is off for the sleep\n");
}

/* Turns the panel's light back on after a sleep that turned it off. */
void
drv_i915_gen9_backlight_resume(
	struct i915_device *device)
{
	/* Nothing was turned off. */
	if (!i915_gen9_backlight_slept)
		return;

	/* Succeeded: the control as it was. */
	i915_gen9_backlight_slept = 0;
	drv_i915_xd_io_init(&i915_gen9_backlight_sleep_io, &device->gt.mmio, NULL);
	drv_i915_xd9_pwm_resume(&i915_gen9_backlight_sleep_io, i915_gen9_backlight_slept_control);
	kern_logf("i915: backlight: the panel's light is on again\n");
}

/*
 * Reads the panel's brightness (the backlight's get operation).
 */
static int
i915_gen9_backlight_get(
	void *context,
	uint32_t *percent)
{
	struct i915_gen9_backlight *backlight;
	struct i915_skl_pwm_registers registers;
	struct i915_skl_pwm pwm;
	int error;

	/* The backlight the device was registered with. */
	backlight = context;

	/* Reads and decodes the PWM. */
	i915_gen9_backlight_read(backlight, &registers);
	error = drv_i915_skl_pwm_decode(&registers, &pwm);
	if (error != 0)
		return EBUSY;

	/* A stopped PWM (the panel off) cannot be read as a brightness. */
	if (!pwm.enabled)
		return EBUSY;

	/* Succeeded: the duty as a brightness. */
	*percent = drv_i915_skl_pwm_percent(&pwm, pwm.duty);
	return 0;
}

/*
 * Sets the panel's brightness (the backlight's set operation).
 */
static int
i915_gen9_backlight_set(
	void *context,
	uint32_t percent)
{
	struct i915_gen9_backlight *backlight;
	struct i915_skl_pwm_registers registers;
	struct i915_skl_pwm pwm;
	uint32_t duty;
	int error;

	/* The backlight the device was registered with. */
	backlight = context;

	/* Reads and decodes the PWM. */
	i915_gen9_backlight_read(backlight, &registers);
	error = drv_i915_skl_pwm_decode(&registers, &pwm);
	if (error != 0)
		return EBUSY;

	/* A stopped PWM (the panel off) is not changed. */
	if (!pwm.enabled)
		return EBUSY;

	/* Writes the duty in the PWM's layout. */
	duty = drv_i915_skl_pwm_duty(&pwm, percent);
	drv_i915_xd9_pwm_set(&backlight->io, backlight->packed, registers.period, duty);

	/* Succeeded: the panel shows the new brightness. */
	return 0;
}

/*
 * Tells the PWM's layout from the south display: the xd code's choice of
 * the part (the PCH the bridges name, or the platform's own when none is
 * found).  Returns 0, or ENODEV for a south display of neither family.
 */
static int
i915_gen9_backlight_layout(
	struct i915_device *device,
	int *packed)
{
	struct i915_xd_choice choice;
	struct i915_pch_state pch;
	int present;
	int error;

	/* Finds the PCH from the ISA bridges. */
	kern_memset(&pch, 0, sizeof(pch));
	drv_i915_detect_pch(device->display, &pch, 9, 0, 1, 1);
	present = 0;
	if (pch.type != I915_PCH_NONE)
		present = 1;

	/* The display 9 part and its south display. */
	error = drv_i915_xd_platform_choose_display9(device->product, device->revision, present, pch.bridge_device, &choice);
	if (error != 0)
		return ENODEV;

	/* An assumed PCH is said so. */
	if (choice.pch_assumed)
		kern_logf("i915: backlight: no PCH found; assuming the platform's\n");

	/* The Sunrise Point family packs the period and the duty. */
	*packed = 0;
	if (choice.pch_kind == I915_PCH_SPT)
		*packed = 1;

	/* Succeeded: the layout is known. */
	return 0;
}

/* Reads the PWM's registers in the layout. */
static void
i915_gen9_backlight_read(
	struct i915_gen9_backlight *backlight,
	struct i915_skl_pwm_registers *registers)
{
	/* The layout. */
	kern_memset(registers, 0, sizeof(*registers));
	registers->cnp = !backlight->packed;

	/* Succeeded: the xd code's read. */
	drv_i915_xd9_pwm_read(&backlight->io, backlight->packed, &registers->control, &registers->period, &registers->duty);
}

/* Copies the xd code's PWM into the node's form. */
static void
i915_gen9_backlight_from_xd(
	const struct i915_xd9_pwm *xd,
	struct i915_skl_pwm *pwm)
{
	/* Succeeded: field by field. */
	pwm->enabled = xd->running;
	pwm->active_low = xd->active_low;
	pwm->max = xd->period;
	pwm->min = xd->floor;
	pwm->duty = xd->duty;
}

/* Copies the node's PWM into the xd code's form. */
static void
i915_gen9_backlight_to_xd(
	const struct i915_skl_pwm *pwm,
	struct i915_xd9_pwm *xd)
{
	/* Succeeded: field by field. */
	xd->running = pwm->enabled;
	xd->active_low = pwm->active_low;
	xd->period = pwm->max;
	xd->floor = pwm->min;
	xd->duty = pwm->duty;
}

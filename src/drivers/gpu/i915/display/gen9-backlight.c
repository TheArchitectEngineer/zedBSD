/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The panel's backlight of a Gen9 node (WS237 stage 2, see
 * gen9-backlight.h).
 *
 * The PWM is the PCH's.  A Sunrise Point or Kaby Point PCH (Skylake, Kaby
 * Lake) keeps the period and the duty in one register; a Cannon Point or
 * Comet Point PCH (Coffee Lake, Comet Lake) has a register for each
 * (Linux 6.8.12 intel_backlight.c: lpt_get_backlight, lpt_set_backlight,
 * lpt_setup_backlight, cnp_setup_backlight, bxt_get_backlight,
 * bxt_set_backlight; the register values are in
 * intel/display-regs-gen9.h).  The PCH is told from the ISA bridge, or
 * from the platform in a virtual machine that hides it (Linux's
 * intel_virt_detect_pch()).
 *
 * The firmware's period, enable and polarity are kept; the backlight
 * device reads and writes the duty only, and refuses (EBUSY) while the
 * PWM does not run.  A brightness of 0 % is the lowest duty
 * (I915_GEN9_BACKLIGHT_MIN_255 of 255ths of the period), not darkness.
 *
 * XXX: one device; a panel whose backlight the VBT puts on the DPCD (eDP
 * AUX backlight) or on the utility pin is not driven.
 */

#include "internal.h"
#include "display.h"
#include "gen9-backlight.h"
#include "gen9-display.h"
#include <kern/kcrt.h>

#include "../i915.h"
#include "../device-info.h"
#include "../mmio.h"
#include "../intel/display-regs-gen9.h"

#include <kern/backlight.h>
#include <kern/klog.h>

#include <uapi/backlight.h>
#include <uapi/errno.h>
#include <stddef.h>

/* The most a brightness may be (a percentage). */
#define I915_GEN9_BACKLIGHT_PERCENT_MAX	100U

/* The scale of the minimum's coefficient. */
#define I915_GEN9_BACKLIGHT_SCALE_255	255U

/*
 * The panel's backlight of the node.
 *
 * Filled by the start before the device is published and emptied by the
 * stop after it is withdrawn; the class calls the operations one at a
 * time in between, and they only read the fields.
 */
struct i915_gen9_backlight {
	/* The device's registers, and whether the PCH is a Cannon Point one. */
	struct i915_mmio *mmio;
	int cnp;

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

static int i915_gen9_backlight_get(void *context, uint32_t *percent);
static int i915_gen9_backlight_set(void *context, uint32_t percent);

/* The provider's operations. */
static const struct kern_backlight_ops i915_gen9_backlight_ops = {
	.get = i915_gen9_backlight_get,
	.set = i915_gen9_backlight_set
};

static int i915_gen9_backlight_pch_cnp(struct i915_device *device, int *cnp);
static void i915_gen9_backlight_read(const struct i915_gen9_backlight *backlight, struct i915_skl_pwm_registers *registers);
static int i915_gen9_backlight_panel_lit(struct i915_mmio *mmio);

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
	kern_memset(pwm, 0, sizeof(*pwm));

	/* The enable and the polarity have the same bits on both PCHs. */
	if ((registers->control & I915_SKL_BLM_PCH_PWM_ENABLE) != 0U)
		pwm->enabled = 1;
	if ((registers->control & I915_SKL_BLM_PCH_POLARITY) != 0U)
		pwm->active_low = 1;

	/* The period and the duty: one register each, or the two halves of CTL2. */
	if (registers->cnp) {
		pwm->max = registers->period;
		pwm->duty = registers->duty;
	} else {
		pwm->max = registers->period >> I915_SKL_BLC_PWM_PERIOD_SHIFT;
		pwm->duty = registers->period & I915_SKL_BLC_PWM_DUTY_MASK;
	}

	/* A period of zero is a PWM nobody set up. */
	if (pwm->max == 0U)
		return EINVAL;

	/* The lowest duty a brightness maps to. */
	pwm->min = (uint32_t)(((uint64_t)pwm->max * I915_GEN9_BACKLIGHT_MIN_255 + I915_GEN9_BACKLIGHT_SCALE_255 - 1U) / I915_GEN9_BACKLIGHT_SCALE_255);

	/* Succeeded: the PWM can be read and changed. */
	return 0;
}

/*
 * Reports the duty a brightness maps to: the lowest duty at 0 %, the
 * period at 100 %, in a straight line between, rounded to the nearest.
 */
uint32_t
drv_i915_skl_pwm_duty(
	const struct i915_skl_pwm *pwm,
	uint32_t percent)
{
	uint64_t span;
	uint64_t step;

	/* A brightness above full is full. */
	if (percent > I915_GEN9_BACKLIGHT_PERCENT_MAX)
		percent = I915_GEN9_BACKLIGHT_PERCENT_MAX;

	/* The part of the span above the lowest duty. */
	span = (uint64_t)(pwm->max - pwm->min);
	step = (span * percent + I915_GEN9_BACKLIGHT_PERCENT_MAX / 2U) / I915_GEN9_BACKLIGHT_PERCENT_MAX;

	/* Succeeded: the duty. */
	return pwm->min + (uint32_t)step;
}

/*
 * Reports the brightness a duty maps to, the inverse of
 * drv_i915_skl_pwm_duty(): 0 % at or below the lowest duty, 100 % at or
 * above the period, rounded to the nearest.
 */
uint32_t
drv_i915_skl_pwm_percent(
	const struct i915_skl_pwm *pwm,
	uint32_t duty)
{
	uint64_t span;
	uint64_t above;

	/* At or below the lowest duty is 0 %; at or above the period is 100 %. */
	if (duty <= pwm->min)
		return 0U;
	if (duty >= pwm->max)
		return I915_GEN9_BACKLIGHT_PERCENT_MAX;

	/* The part of the span above the lowest duty, as a percentage. */
	span = (uint64_t)(pwm->max - pwm->min);
	above = (uint64_t)(duty - pwm->min);

	/* Succeeded: the brightness. */
	return (uint32_t)((above * I915_GEN9_BACKLIGHT_PERCENT_MAX + span / 2U) / span);
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

	backlight = &i915_gen9_backlight_state;

	/* Only a Gen9 node, once. */
	if (device->gt.graphics_ver != 9U)
		return;
	if (backlight->published != NULL)
		return;

	/* The step the boot line asks for must take the backlight. */
	level = drv_i915_gen9_display_level();
	if (level < I915_GEN9_DISPLAY_BACKLIGHT)
		return;

	/* The panel must be lit for its PWM to mean anything. */
	lit = i915_gen9_backlight_panel_lit(&device->gt.mmio);
	if (!lit) {
		kern_logf("i915: gen9 backlight: the eDP panel is not lit; no backlight device\n");
		return;
	}

	/* Which register layout the PCH has. */
	backlight->mmio = &device->gt.mmio;
	error = i915_gen9_backlight_pch_cnp(device, &backlight->cnp);
	if (error != 0) {
		kern_logf("i915: gen9 backlight: the PCH is not one this driver knows the PWM of; no backlight device\n");
		kern_memset(backlight, 0, sizeof(*backlight));
		return;
	}

	/* What the firmware set the PWM to. */
	i915_gen9_backlight_read(backlight, &registers);
	error = drv_i915_skl_pwm_decode(&registers, &pwm);
	kern_logf("i915: gen9 backlight: %s PWM control 0x%08x period 0x%08x duty 0x%08x: %s, max %u min %u duty %u (%u %%)%s\n",
	    backlight->cnp ? "CNP" : "SPT",
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
		kern_logf("i915: gen9 backlight: the PWM does not drive the panel (the light may be the DPCD's or the EC's); no backlight device\n");
		kern_memset(backlight, 0, sizeof(*backlight));
		return;
	}

	/* Publishes the device the Settings app's brightness reaches. */
	error = kern_backlight_register("i915", BACKLIGHT_TYPE_PANEL, &i915_gen9_backlight_ops, backlight, &backlight->published);
	if (error != 0) {
		kern_logf("i915: gen9 backlight: the backlight device could not be registered: %d\n", error);
		kern_memset(backlight, 0, sizeof(*backlight));
		return;
	}

	kern_logf("i915: gen9 backlight: the panel's brightness is offered as a backlight device\n");
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

	backlight = &i915_gen9_backlight_state;

	/* Nothing was published. */
	if (backlight->published == NULL)
		return;

	/* The class runs no operation after the withdrawal. */
	kern_backlight_unregister(backlight->published);
	kern_memset(backlight, 0, sizeof(*backlight));
}

/* Reads the panel's brightness (the backlight's get operation). */
static int
i915_gen9_backlight_get(
	void *context,
	uint32_t *percent)
{
	struct i915_gen9_backlight *backlight;
	struct i915_skl_pwm_registers registers;
	struct i915_skl_pwm pwm;
	int error;

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

/* Sets the panel's brightness (the backlight's set operation). */
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

	backlight = context;

	/* Reads and decodes the PWM. */
	i915_gen9_backlight_read(backlight, &registers);
	error = drv_i915_skl_pwm_decode(&registers, &pwm);
	if (error != 0)
		return EBUSY;

	/* A stopped PWM (the panel off) is not changed. */
	if (!pwm.enabled)
		return EBUSY;

	/* Writes the duty: its own register, or CTL2's low half with the period kept. */
	duty = drv_i915_skl_pwm_duty(&pwm, percent);
	if (backlight->cnp) {
		drv_i915_raw_write32(backlight->mmio, I915_CNP_BLC_PWM_DUTY1, duty);
	} else {
		drv_i915_raw_write32(backlight->mmio, I915_SKL_BLC_PWM_PCH_CTL2, (registers.period & ~I915_SKL_BLC_PWM_DUTY_MASK) | duty);
	}

	/* Succeeded: the panel shows the new brightness. */
	return 0;
}

/*
 * Tells the PWM's register layout from the PCH: Cannon Point and Comet
 * Point have the Broxton one, Sunrise Point and Kaby Point the LPT one.
 * Without a PCH the bridge names (a virtual machine), the platform
 * decides.  Returns 0, or ENODEV for another PCH.
 */
static int
i915_gen9_backlight_pch_cnp(
	struct i915_device *device,
	int *cnp)
{
	struct i915_pch_state pch;
	int platform;

	/* Finds the PCH from the ISA bridges. */
	kern_memset(&pch, 0, sizeof(pch));
	drv_i915_detect_pch(device->display, &pch, 9, 0, 1, 1);

	/* A PCH the bridges name decides. */
	if (pch.type == I915_PCH_SPT) {
		*cnp = 0;
		return 0;
	}
	if (pch.type == I915_PCH_CNP) {
		*cnp = 1;
		return 0;
	}

	/* Another PCH has neither layout. */
	if (pch.type != I915_PCH_NONE)
		return ENODEV;

	/* Without one, Coffee Lake and Comet Lake pair with Cannon Point, Skylake and Kaby Lake with Sunrise Point. */
	platform = device->gt.info.platform;
	kern_logf("i915: gen9 backlight: no PCH found; assuming the platform's (%d)\n", platform);
	if (platform == I915_PLATFORM_COFFEELAKE || platform == I915_PLATFORM_COMETLAKE) {
		*cnp = 1;
	} else {
		*cnp = 0;
	}

	/* Succeeded: the layout is known. */
	return 0;
}

/* Reads the PWM's registers in the PCH's layout. */
static void
i915_gen9_backlight_read(
	const struct i915_gen9_backlight *backlight,
	struct i915_skl_pwm_registers *registers)
{
	kern_memset(registers, 0, sizeof(*registers));
	registers->cnp = backlight->cnp;

	/* The control, the period, and on Cannon Point the duty. */
	if (backlight->cnp) {
		registers->control = drv_i915_raw_read32(backlight->mmio, I915_CNP_BLC_PWM_CTL1);
		registers->period = drv_i915_raw_read32(backlight->mmio, I915_CNP_BLC_PWM_FREQ1);
		registers->duty = drv_i915_raw_read32(backlight->mmio, I915_CNP_BLC_PWM_DUTY1);
	} else {
		registers->control = drv_i915_raw_read32(backlight->mmio, I915_SKL_BLC_PWM_PCH_CTL1);
		registers->period = drv_i915_raw_read32(backlight->mmio, I915_SKL_BLC_PWM_PCH_CTL2);
	}
}

/* Reports nonzero when the eDP transcoder runs: the panel is lit. */
static int
i915_gen9_backlight_panel_lit(
	struct i915_mmio *mmio)
{
	uint32_t power;
	uint32_t ddi_func;

	/* The eDP transcoder lives in power well 1. */
	power = drv_i915_raw_read32(mmio, I915_SKL_PWR_WELL_CTL2);
	if ((power & I915_SKL_PW_1_STATE) == 0U)
		return 0;

	/* Its DDI function is on while it drives the panel. */
	ddi_func = drv_i915_raw_read32(mmio, I915_SKL_TRANS_DDI_FUNC_CTL_EDP);
	if ((ddi_func & I915_SKL_TRANS_DDI_FUNC_ENABLE) == 0U)
		return 0;

	/* Succeeded: the panel is lit. */
	return 1;
}

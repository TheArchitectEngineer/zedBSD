/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * What binds the display's Type-C ports to the device (see tc-kern.h).
 *
 * The Type-C core (tc.c) asks for registers, power, a lock per port, a
 * delay and the log through struct i915_tc_env; this file answers with the
 * display's MMIO, its power domains, one mutex per port, the driver's busy
 * delay and the kernel log.  The start declares the Type-C ports the
 * display probe made encoders for, with the VBT's legacy flag and AUX
 * channel, and reads how the firmware left them.
 */

#include "tc-kern.h"
#include "power.h"
#include "vbt-parse.h"

#include "../mmio.h"
#include "../sync.h"

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/lock.h>

#include <drivers/typec/typec.h>

#include <stdarg.h>

/* The display version whose Type-C ports the core knows (Alder Lake-P). */
#define I915_TC_KERN_DISPLAY_VER	13u

/* The longest line the core logs. */
#define I915_TC_KERN_LOG_LINE		256u

/*
 * The USB Type-C connector layer, which the ports' DisplayPort state is
 * reported to.
 *
 * It is linked only with CONFIG_DRIVER_ACPI and CONFIG_DRIVER_TYPEC (the
 * UCSI driver); without it the symbol is weak and NULL, and a report is
 * not made.
 */
extern int drv_typec_display_report(unsigned port, const struct drv_typec_dp_state *state) __attribute__((weak));

static uint32_t tc_kern_read32(void *ctx, uint32_t reg);
static void tc_kern_write32(void *ctx, uint32_t reg, uint32_t value);
static enum i915_power_domain tc_kern_domain(struct i915_tc_kern *k, enum i915_tc_power power, unsigned port);
static int tc_kern_power_get(void *ctx, enum i915_tc_power power, unsigned port);
static void tc_kern_power_put(void *ctx, enum i915_tc_power power, unsigned port);
static void tc_kern_lock(void *ctx, unsigned port);
static void tc_kern_unlock(void *ctx, unsigned port);
static int tc_kern_delay_us(void *ctx, unsigned us);
static void tc_kern_log(void *ctx, const char *format, ...) __attribute__((format(printf, 2, 3)));
static void tc_kern_declare_ports(struct i915_display *display, struct i915_tc_kern *k);

/*
 * Binds the display's Type-C ports and reads how the firmware left them.
 *
 * Runs once, after the display probe made its encoders and before the
 * hotplug path starts.  A display version other than 13 has no Type-C
 * ports here.
 */
void
drv_i915_tc_kern_start(
	struct i915_display *display,
	struct i915_mmio *mmio,
	unsigned display_ver)
{
	struct i915_tc_kern *k;
	struct i915_tc_env env;
	unsigned port;

	/* The display's binding of its Type-C ports. */
	k = &display->tck;

	/* Only display version 13's Type-C ports are known. */
	if (display_ver != I915_TC_KERN_DISPLAY_VER) {
		kern_logf("i915: TC: display version %u: the Type-C ports are not driven\n", display_ver);
		return;
	}

	/* What the ports use: the display's registers and power, and a lock each. */
	k->mmio = mmio;
	k->pd = &display->power_domains;
	k->pwc = &display->pwc;
	k->display_ver = display_ver;
	k->power_failures = 0u;
	for (port = 0u; port < I915_TC_PORTS; port++) {
		(void)mutex_init(&k->locks[port], LOCK_RANK_DEVICE, "i915 tc port");
		k->aux_ch[port] = I915_AUX_CH_USBC1 + (int)port;
	}

	/* The environment the core asks through. */
	kern_memset(&env, 0, sizeof(env));
	env.ctx = k;
	env.read32 = tc_kern_read32;
	env.write32 = tc_kern_write32;
	env.power_get = tc_kern_power_get;
	env.power_put = tc_kern_power_put;
	env.lock = tc_kern_lock;
	env.unlock = tc_kern_unlock;
	env.delay_us = tc_kern_delay_us;
	env.log = tc_kern_log;
	drv_i915_tc_init(&k->tc, &env, display_ver);

	/* The ports are bound from here on. */
	k->live = 1;

	/* Declares the ports the VBT names and reads how the firmware left them. */
	tc_kern_declare_ports(display, k);
	drv_i915_tc_readout(&k->tc);

	/* Tells the Type-C layer what each declared port has of DisplayPort after the readout. */
	for (port = 0u; port < I915_TC_PORTS; port++) {
		if (k->tc.port[port].present)
			drv_i915_tc_kern_report(&k->tc, port);
	}
}

/*
 * Returns the display's Type-C ports, or NULL when they are not bound.
 */
struct i915_tc *
drv_i915_tc_kern_ports(
	struct i915_display *display)
{
	/* A display without bound ports has none to give. */
	if (!display->tck.live)
		return NULL;

	/* Succeeded: the ports. */
	return &display->tck.tc;
}

/*
 * Tells which Type-C port a DDI port is: 0 for TC1, or -1 for a port that
 * is not a Type-C port of display version 13.
 */
int
drv_i915_tc_kern_port_of(
	int port)
{
	/* TC1 to TC4 are DDI ports D to G. */
	if (port < I915_PORT_TC1)
		return -1;
	if (port >= I915_PORT_TC1 + (int)I915_TC_PORTS)
		return -1;

	/* Succeeded: the Type-C port's number. */
	return port - I915_PORT_TC1;
}

/*
 * Gives an output's link to a Type-C port back (the panel run's
 * tc_put_link hook, intel_tc_port_put_link()).
 *
 * The run's context is its struct i915_lcd_kernel; a run whose display has
 * no bound ports gives nothing back.
 */
void
drv_i915_lcd_tc_put_link(
	void *ctx,
	int tc_port)
{
	struct i915_lcd_kernel *kernel;

	/* Resolves the run the hook belongs to. */
	kernel = ctx;

	/* A display without bound ports, or a port it does not have, has no link. */
	if (kernel->d == NULL || kernel->d->tc == NULL) {
		kern_logf("i915: TC: put_link TC%d without bound Type-C ports: nothing given back\n", tc_port + 1);
		return;
	}

	/* A number outside the four ports names none. */
	if (tc_port < 0 || tc_port >= (int)I915_TC_PORTS)
		return;

	/* Gives the link back; the last one gives the PHY back at once. */
	drv_i915_tc_put_link(kernel->d->tc, (unsigned)tc_port);
}

/*
 * Tells the USB Type-C connector layer what a Type-C port has of
 * DisplayPort: the DP-alt hot plug detect, the pin assignment and the
 * lanes (the plug's orientation is not known to the display).
 *
 * Called from the display's start and its hotplug work, never from the
 * interrupt, and with the port's lock not held (the sample takes it).  A
 * kernel without the Type-C layer reports nothing.
 */
void
drv_i915_tc_kern_report(
	struct i915_tc *tc,
	unsigned port)
{
	struct i915_tc_dp_sample sample;
	struct drv_typec_dp_state state;
	int error;

	/* A kernel without the Type-C layer has no one to tell. */
	if (drv_typec_display_report == NULL)
		return;

	/* Reads the port. */
	drv_i915_tc_dp_sample(tc, port, &sample);

	/* The layer's form: the FIA numbers the pin assignments as the layer does (3 is C). */
	kern_memset(&state, 0, sizeof(state));
	state.known = true;
	if (sample.hpd)
		state.hpd = true;
	state.pin = DRV_TYPEC_DP_PIN_NONE;
	if (sample.pin <= (unsigned)DRV_TYPEC_DP_PIN_F)
		state.pin = (enum drv_typec_dp_pin)sample.pin;
	state.lanes = (unsigned)sample.lanes;
	state.orientation = DRV_TYPEC_ORIENTATION_UNKNOWN;

	/* Tells the layer; a refusal is only logged. */
	error = drv_typec_display_report(port, &state);
	if (error != 0)
		kern_logf("i915: TC%u: the Type-C layer refused the DisplayPort report (error %d)\n", port + 1u, error);
}

/* Reads a display register for the core. */
static uint32_t
tc_kern_read32(
	void *ctx,
	uint32_t reg)
{
	struct i915_tc_kern *k;
	uint32_t value;

	/* Reads the display's register directly, as the display does. */
	k = ctx;
	value = drv_i915_raw_read32(k->mmio, reg);

	/* Succeeded: the value. */
	return value;
}

/* Writes a display register for the core. */
static void
tc_kern_write32(
	void *ctx,
	uint32_t reg,
	uint32_t value)
{
	struct i915_tc_kern *k;

	/* Writes the display's register directly, as the display does. */
	k = ctx;
	drv_i915_raw_write32(k->mmio, reg, value);
}

/* Names the power domain of a port's power. */
static enum i915_power_domain
tc_kern_domain(
	struct i915_tc_kern *k,
	enum i915_tc_power power,
	unsigned port)
{
	enum i915_power_domain domain;

	/* Picks the domain of the power. */
	switch (power) {
	case I915_TC_POWER_PORT:
		/* The port's DDI lanes. */
		return (enum i915_power_domain)(I915_PW_DOMAIN_PORT_DDI_LANES_TC1 + port);
	case I915_TC_POWER_COLD:
		/* The AUX_USBC domain of the port's AUX channel, which blocks TC cold. */
		domain = drv_i915_aux_legacy_power_domain(k->display_ver, k->aux_ch[port]);
		return domain;
	default:
		break;
	}

	/* Succeeded: the display core. */
	return I915_PW_DOMAIN_DISPLAY_CORE;
}

/* Takes a port's power for the core: 0, or the power domains' error. */
static int
tc_kern_power_get(
	void *ctx,
	enum i915_tc_power power,
	unsigned port)
{
	struct i915_tc_kern *k;
	enum i915_power_domain domain;
	int error;

	/* Names the domain of the power. */
	k = ctx;
	domain = tc_kern_domain(k, power, port);

	/* Takes a reference on the domain. */
	error = drv_i915_display_power_get(k->pd, domain, k->pwc);
	if (error != 0) {
		k->power_failures++;
		kern_logf("i915: TC%u: power domain %d did not come (error %d)\n", port + 1u, (int)domain, error);
		return error;
	}

	/* Succeeded: the power is on. */
	return 0;
}

/* Gives a port's power back for the core. */
static void
tc_kern_power_put(
	void *ctx,
	enum i915_tc_power power,
	unsigned port)
{
	struct i915_tc_kern *k;
	enum i915_power_domain domain;

	/* Names the domain of the power. */
	k = ctx;
	domain = tc_kern_domain(k, power, port);

	/* Gives the reference back. */
	drv_i915_display_power_put(k->pd, domain, k->pwc);
}

/* Takes a port's lock for the core. */
static void
tc_kern_lock(
	void *ctx,
	unsigned port)
{
	struct i915_tc_kern *k;

	/* Takes the port's mutex. */
	k = ctx;
	mutex_lock(&k->locks[port]);
}

/* Releases a port's lock for the core. */
static void
tc_kern_unlock(
	void *ctx,
	unsigned port)
{
	struct i915_tc_kern *k;

	/* Releases the port's mutex. */
	k = ctx;
	mutex_unlock(&k->locks[port]);
}

/* Busy-waits for the core: 0, or EIO when the time base failed. */
static int
tc_kern_delay_us(
	void *ctx,
	unsigned us)
{
	int delay_error;

	UNUSED_PARAMETER(ctx);

	/* Waits with the driver's delay. */
	delay_error = drv_i915_udelay(us);
	if (delay_error != 0)
		return delay_error;

	/* Succeeded: the time has passed. */
	return 0;
}

/* Writes a line of the core's log to the kernel log. */
static void
tc_kern_log(
	void *ctx,
	const char *format,
	...)
{
	char line[I915_TC_KERN_LOG_LINE];
	va_list arguments;

	UNUSED_PARAMETER(ctx);

	/* Formats the line and writes it. */
	va_start(arguments, format);
	(void)kern_vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	kern_logf("%s", line);
}

/*
 * Declares the Type-C ports the display probe made encoders for, with the
 * VBT's legacy flag (a port that supports neither USB-C nor Thunderbolt has
 * a connector wired to it) and its AUX channel.
 */
static void
tc_kern_declare_ports(
	struct i915_display *display,
	struct i915_tc_kern *k)
{
	const struct i915_encoder *encoder;
	const struct i915_vbt_encoder *child;
	unsigned index;
	int tc_port;
	int legacy;

	/* Declares the port of each Type-C encoder. */
	for (index = 0u; index < display->nogem.num_encoders; index++) {
		/* Skips an encoder that is not a Type-C port's. */
		encoder = &display->nogem.encoders[index];
		if (!encoder->is_tc)
			continue;

		/* Skips a Type-C encoder outside TC1 to TC4. */
		tc_port = drv_i915_tc_kern_port_of(encoder->port);
		if (tc_port < 0)
			continue;

		/* Without the VBT's word the port is a USB-C receptacle on its own AUX channel. */
		legacy = 0;
		child = NULL;
		if (display->vbt_state.parsed_live)
			child = drv_i915_vbt_encoder_for_port(&display->vbt_state.parsed, encoder->port);

		/* The VBT's child says whether the port has a receptacle, and its AUX channel. */
		if (child != NULL) {
			/* A port with neither a USB-C nor a Thunderbolt receptacle has a connector wired to it. */
			if (!child->supports_typec_usb && !child->supports_tbt)
				legacy = 1;

			/* The VBT's AUX channel, when it names one. */
			if (child->aux_ch >= 0)
				k->aux_ch[tc_port] = child->aux_ch;
		}

		/* Declares the port. */
		drv_i915_tc_declare(&k->tc, (unsigned)tc_port, legacy);
		kern_logf("i915: TC%d: declared (DDI %c, legacy %d, AUX channel %d)\n", tc_port + 1, (char)('A' + encoder->port), legacy, k->aux_ch[tc_port]);
	}
}

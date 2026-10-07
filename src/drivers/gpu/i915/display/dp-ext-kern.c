/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The display's external DP ports (see dp-ext-kern.h).
 *
 * Each Type-C port the display declares gets the Linux objects the AUX
 * text of aux.c and the DPCD and I2C-over-AUX helpers of dp-sink.c work
 * on -- a digital port with its DP output, AUX channel and Type-C port,
 * and a connector -- on a device of their own whose environment is the
 * eDP device's kernel backend (its MMIO, power domains, mutexes and
 * threads) with bookkeeping of its own.  The eDP device's backend is
 * started here when no eDP started it.
 *
 * A probe holds a link on the Type-C port for its whole length, so the
 * port's PHY stays with the display between the AUX messages, and gives
 * the link back at the end: the port's last link gives the PHY back at
 * once, so a port the display does not drive is released as soon as it
 * has been looked at (the firmware updates the other Type-C ports' hot
 * plug only after that).  The sink's probe itself is dp-ext.c.
 */

#include "dp-internal.h"
#include "dp-ext-kern.h"
#include "dp-sink.h"
#include "tc-kern.h"
#include "vbt-parse.h"

#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/lock.h>

#include <stdarg.h>

/* The longest line the probe logs. */
#define I915_DP_EXT_KERN_LOG_LINE	256u

/* The highest link rate of a display version 13 Type-C port (HBR3, the Linux intel_dp_set_source_rates()). */
#define I915_DP_EXT_KERN_MAX_RATE	810000

/* The lanes a Type-C port's DDI drives. */
#define I915_DP_EXT_KERN_MAX_LANES	4

/* The lanes a probe asks the Type-C port for: AUX needs the PHY, not a lane count. */
#define I915_DP_EXT_KERN_PROBE_LANES	1

static void i915_dp_ext_bind_device(struct i915_dp_ext_world *ext, struct i915_display *display);
static void i915_dp_ext_bind_port(struct i915_dp_ext_world *ext, struct i915_display *display, unsigned tc_port);
static void i915_dp_ext_probe_locked(struct i915_dp_ext_world *ext, struct i915_dp_ext_port *p);
static long i915_dp_ext_dpcd_read(void *ctx, unsigned offset, uint8_t *buffer, size_t size);
static long i915_dp_ext_dpcd_write(void *ctx, unsigned offset, const uint8_t *buffer, size_t size);
static int i915_dp_ext_dpcd_probe(void *ctx, unsigned offset);
static int i915_dp_ext_read_caps(void *ctx, uint8_t dpcd[I915_DP_EXT_DPCD_SIZE]);
static int i915_dp_ext_ddc_probe(void *ctx);
static int i915_dp_ext_edid_read(void *ctx, uint8_t *buffer, unsigned max_blocks, unsigned *extensions);
static void i915_dp_ext_log(void *ctx, const char *format, ...) __attribute__((format(printf, 2, 3)));

/*
 * Binds the display's Type-C ports as external DP ports.
 *
 * It runs once, after the Type-C ports are declared and read out and
 * before the hotplug path starts; a display without bound Type-C ports
 * has no external DP port.  Nothing is probed here.
 */
void
drv_i915_dp_ext_start(
	struct i915_display *display)
{
	struct i915_dp_world *world;
	struct i915_dp_ext_world *ext;
	struct i915_tc *tc;
	unsigned tc_port;
	unsigned bound;
	int error;

	/* A display without its DP world has nothing to bind. */
	world = display->dp_world;
	if (world == NULL)
		return;

	/* Without bound Type-C ports there is no external DP port. */
	ext = &world->ext;
	tc = drv_i915_tc_kern_ports(display);
	if (tc == NULL) {
		kern_logf("i915: DP-ext: no Type-C ports: no external DP port\n");
		return;
	}

	/* Starts the eDP device's backend unless the eDP did. */
	error = drv_i915_edp_device_start(&display->edp_dev);
	if (error != 0) {
		kern_logf("i915: DP-ext: the display's AUX backend could not start (rc %d): no external DP port\n", error);
		return;
	}

	/* The external ports' environment is the eDP device's backend, with bookkeeping of its own. */
	drv_i915_dp_kernel_bind(&display->edp_dev.k, &ext->env);
	i915_dp_ext_bind_device(ext, display);
	ext->tc = tc;

	/* Binds each declared Type-C port. */
	bound = 0u;
	for (tc_port = 0u; tc_port < I915_TC_PORTS; tc_port++) {
		/* A port the VBT does not declare is never touched. */
		if (!tc->port[tc_port].present)
			continue;

		/* Binds the port and counts it. */
		i915_dp_ext_bind_port(ext, display, tc_port);
		bound++;
	}

	/* The ports can be probed from here on. */
	ext->live = 1;
	kern_logf("i915: DP-ext: %u external DP port(s) bound\n", bound);
}

/*
 * Probes the sink of an external DP port (the Type-C port of a DDI port)
 * and copies its EDID.
 *
 * The caller has found the port's live status connected.  Returns what
 * the probe concluded; unknown when the external ports are not bound or
 * the port is not a Type-C port, disconnected for a port the VBT does not
 * declare.  *edid_bytes receives the EDID bytes copied (whole blocks, 0
 * without an EDID).
 */
enum i915_dp_ext_status
drv_i915_dp_ext_probe(
	struct i915_display *display,
	int port,
	uint8_t *edid,
	size_t edid_size,
	unsigned *edid_bytes)
{
	struct i915_dp_world *world;
	struct i915_dp_ext_world *ext;
	struct i915_dp_ext_port *p;
	enum i915_dp_ext_status status;
	unsigned bytes;
	int tc_port;

	/* Nothing is copied unless the probe reads an EDID. */
	*edid_bytes = 0u;

	/* Without bound external ports the sink cannot be asked. */
	world = display->dp_world;
	if (world == NULL)
		return I915_DP_EXT_UNKNOWN;
	ext = &world->ext;
	if (!ext->live)
		return I915_DP_EXT_UNKNOWN;

	/* Only a Type-C port is an external DP port here. */
	tc_port = drv_i915_tc_kern_port_of(port);
	if (tc_port < 0)
		return I915_DP_EXT_UNKNOWN;

	/* A port the VBT does not declare has no sink. */
	p = &ext->port[tc_port];
	if (!p->declared)
		return I915_DP_EXT_DISCONNECTED;

	/* Probes under the port's lock, unless the ports stopped meanwhile, and copies the EDID's whole blocks. */
	mutex_lock(&p->lock);

	if (!ext->live) {
		mutex_unlock(&p->lock);
		return I915_DP_EXT_UNKNOWN;
	}

	/* Probes the sink and takes its conclusion. */
	i915_dp_ext_probe_locked(ext, p);
	status = p->sink.status;

	/* Copies as many of the EDID's whole blocks as the caller has room for. */
	bytes = p->sink.edid_blocks * I915_DP_EXT_EDID_BLOCK_SIZE;
	if (bytes > edid_size)
		bytes = (unsigned)(edid_size / I915_DP_EXT_EDID_BLOCK_SIZE) * I915_DP_EXT_EDID_BLOCK_SIZE;

	/* A probe without an EDID copies nothing. */
	if (bytes != 0u)
		kern_memcpy(edid, p->sink.edid, bytes);

	mutex_unlock(&p->lock);

	/* Succeeded: reports the conclusion and the EDID copied. */
	*edid_bytes = bytes;
	return status;
}

/*
 * Handles a short pulse (IRQ_HPD) of an external DP port (the DP part of
 * the Linux intel_dp_hpd_pulse() for a short pulse).
 *
 * The port is held for the AUX messages as for a probe, and its sink is
 * checked against what the last probe found (dp-ext.c).  Returns 1 when
 * the pulse is handled, 0 when the port must be detected again (the
 * hotplug work then probes it); a port that cannot be asked returns 0.
 */
int
drv_i915_dp_ext_pulse(
	struct i915_display *display,
	int port)
{
	struct i915_dp_world *world;
	struct i915_dp_ext_world *ext;
	struct i915_dp_ext_port *p;
	unsigned tc_port;
	int handled;
	int index;

	/* Without bound external ports the pulse goes to a detection. */
	world = display->dp_world;
	if (world == NULL)
		return 0;
	ext = &world->ext;
	if (!ext->live)
		return 0;

	/* Only a declared Type-C port has a sink to check. */
	index = drv_i915_tc_kern_port_of(port);
	if (index < 0)
		return 0;
	p = &ext->port[index];
	if (!p->declared)
		return 0;

	/* Checks the sink with the port held for the messages, unless the ports stopped meanwhile. */
	tc_port = (unsigned)index;
	mutex_lock(&p->lock);

	if (!ext->live) {
		mutex_unlock(&p->lock);
		return 0;
	}

	/* Holds the port, checks the sink, and gives the port back (its PHY at once when no output holds it). */
	drv_i915_tc_get_link(ext->tc, tc_port, I915_DP_EXT_KERN_PROBE_LANES);
	handled = drv_i915_dp_ext_short_pulse(&p->env, &p->sink);
	drv_i915_tc_put_link(ext->tc, tc_port);
	kern_logf("i915: DP-ext %s: short pulse %s\n", p->name, handled ? "handled" : "asks for a detection");

	mutex_unlock(&p->lock);

	/* Reports a sink that changed. */
	if (!handled)
		return 0;

	/* Succeeded: the pulse is handled. */
	return 1;
}

/*
 * Stops the external DP ports before the eDP device's backend goes: no
 * probe starts after this, and one running is waited for.
 */
void
drv_i915_dp_ext_stop(
	struct i915_dp_world *world)
{
	struct i915_dp_ext_world *ext;
	struct i915_dp_ext_port *p;
	unsigned tc_port;

	/* Nothing to stop without started external ports. */
	if (world == NULL)
		return;
	ext = &world->ext;
	if (!ext->live)
		return;

	/* A probe that starts from here on finds the ports stopped. */
	ext->live = 0;

	/* Waits out a probe that is running, port by port. */
	for (tc_port = 0u; tc_port < I915_TC_PORTS; tc_port++) {
		p = &ext->port[tc_port];

		/* A port never bound has no lock. */
		if (!p->declared)
			continue;

		/* Takes the port's lock, which a running probe holds, and logs the port's tally. */
		mutex_lock(&p->lock);

		kern_logf("i915: DP-ext %s: stopped after %u probe(s), last %s\n", p->name, p->probes, drv_i915_dp_ext_status_name(p->sink.status));

		mutex_unlock(&p->lock);
	}
}

/*
 * Builds the device the Linux AUX text sees for the external ports: the
 * environment, the PPS mutex (which the AUX transfer takes for every port,
 * as Linux does) and the raw clock.
 */
static void
i915_dp_ext_bind_device(
	struct i915_dp_ext_world *ext,
	struct i915_display *display)
{
	/* The environment, the PCH split's PPS registers and the PPS mutex on the backend's PPS lock. */
	ext->i915.dp_env = &ext->env;
	ext->i915.display.pps.mmio_base = PCH_PPS_BASE;
	i915_dp_mutex_init(&ext->i915.display.pps.mutex);
	ext->i915.display.pps.mutex.env = &ext->env;
	ext->i915.display.pps.mutex.id = I915_DP_LOCK_PPS;

	/* The raw clock the eDP was given; the AUX clock of display version 13 does not use it. */
	ext->i915.display_runtime.rawclk_freq = display->edp_dev.cfg.rawclk_khz;
}

/*
 * Binds one declared Type-C port as an external DP port: its digital port
 * on the port's AUX channel, its DP output and connector, what the source
 * offers there (the VBT's limits), the probe's environment and the lock.
 */
static void
i915_dp_ext_bind_port(
	struct i915_dp_ext_world *ext,
	struct i915_display *display,
	unsigned tc_port)
{
	struct i915_dp_ext_port *p;
	struct intel_dp *intel_dp;
	const struct i915_vbt_encoder *child;
	int port;

	/* The port, its DDI port, and the VBT's child of that port. */
	p = &ext->port[tc_port];
	port = I915_PORT_TC1 + (int)tc_port;
	child = NULL;
	if (display->vbt_state.parsed_live)
		child = drv_i915_vbt_encoder_for_port(&display->vbt_state.parsed, port);

	/* The names in the log. */
	(void)kern_snprintf(p->name, sizeof(p->name), "TC%u", tc_port + 1u);
	(void)kern_snprintf(p->aux_name, sizeof(p->aux_name), "AUX USBC%u/DDI TC%u", tc_port + 1u, tc_port + 1u);

	/* The digital port: its device, encoder, DDI port, AUX channel and Type-C port. */
	p->dig_port.i915 = &ext->i915;
	p->dig_port.base.base.dev = &ext->i915.drm;
	p->dig_port.base.base.name = p->name;
	p->dig_port.base.port = (enum port)port;
	p->dig_port.aux_ch = (enum aux_ch)display->tck.aux_ch[tc_port];
	p->dig_port.tc = ext->tc;
	p->dig_port.tc_port = tc_port;
	p->dig_port.tc_mode = I915_TC_MODE_NONE;

	/* The DP output: an external DP driving the connector over the port's AUX channel. */
	intel_dp = &p->dig_port.dp;
	intel_dp->is_edp = false;
	intel_dp->attached_connector = &p->connector;
	intel_dp->aux.name = p->aux_name;
	intel_dp->aux.drm_dev = &ext->i915.drm;

	/* The connector has no panel and no backlight controller. */
	p->connector.panel.vbt.backlight.controller = -1;

	/* Binds the AUX channel to the hardware, and its mutex to the backend's AUX lock. */
	drv_i915_dp_aux_init(intel_dp);
	intel_dp->aux.hw_mutex.env = &ext->env;
	intel_dp->aux.hw_mutex.id = I915_DP_LOCK_AUX;

	/* What the source offers: HBR3 and 4 lanes, within the VBT's limits; the FIA's lanes are read at each probe. */
	p->source.max_rate = I915_DP_EXT_KERN_MAX_RATE;
	p->source.max_lanes = I915_DP_EXT_KERN_MAX_LANES;
	p->source.vbt_max_rate = 0;
	p->source.vbt_max_lanes = 0;
	p->source.fia_lanes = 0;
	if (child != NULL) {
		p->source.vbt_max_rate = child->dp_max_link_rate;
		p->source.vbt_max_lanes = child->dp_max_lane_count;
	}

	/* The probe's access to the AUX channel. */
	p->env.ctx = p;
	p->env.dpcd_read = i915_dp_ext_dpcd_read;
	p->env.dpcd_write = i915_dp_ext_dpcd_write;
	p->env.dpcd_probe = i915_dp_ext_dpcd_probe;
	p->env.read_caps = i915_dp_ext_read_caps;
	p->env.ddc_probe = i915_dp_ext_ddc_probe;
	p->env.edid_read = i915_dp_ext_edid_read;
	p->env.log = i915_dp_ext_log;

	/* The lock, and a sink not yet probed. */
	(void)mutex_init(&p->lock, LOCK_RANK_DEVICE, "i915 dp-ext port");
	drv_i915_dp_ext_forget(&p->sink);
	p->probes = 0u;
	p->declared = 1;

	/* Logs the binding. */
	kern_logf("i915: DP-ext %s: bound (DDI %c, AUX channel %d, VBT max rate %d lanes %d)\n",
		  p->name,
		  (char)('A' + port),
		  (int)p->dig_port.aux_ch,
		  p->source.vbt_max_rate,
		  p->source.vbt_max_lanes);
}

/*
 * Probes one port's sink with the port's lock held: takes a link on the
 * Type-C port for the probe, reads the lanes the FIA assigned, probes a
 * port held in DP-alt or legacy mode, gives the link back (the PHY goes
 * back at once when no output holds the port) and logs what it found.
 */
static void
i915_dp_ext_probe_locked(
	struct i915_dp_ext_world *ext,
	struct i915_dp_ext_port *p)
{
	enum i915_tc_mode mode;
	enum i915_tc_mode after;
	unsigned tc_port;

	/* Counts the probe. */
	tc_port = p->dig_port.tc_port;
	p->probes++;

	/* Holds the port for the whole probe: its PHY stays with the display between the AUX messages. */
	drv_i915_tc_get_link(ext->tc, tc_port, I915_DP_EXT_KERN_PROBE_LANES);

	/* The mode the link fixed, and the lanes the FIA gave DisplayPort (read with the port held). */
	mode = drv_i915_tc_lock(ext->tc, tc_port, I915_DP_EXT_KERN_PROBE_LANES);

	p->source.fia_lanes = drv_i915_tc_max_lanes(ext->tc, tc_port);

	drv_i915_tc_unlock(ext->tc, tc_port);

	/* Only a port the display holds for DP has a sink to probe; Thunderbolt is outside the driver's scope. */
	if (mode != I915_TC_MODE_DP_ALT && mode != I915_TC_MODE_LEGACY) {
		drv_i915_dp_ext_forget(&p->sink);
		p->sink.step = I915_DP_EXT_STEP_PHY;
		kern_logf("i915: DP-ext %s: held in %s mode, not DP: no sink probed\n", p->name, drv_i915_tc_mode_name(mode));
	} else {
		/* Counts this probe's I2C replies afresh, as Linux does before the EDID. */
		p->dig_port.dp.aux.i2c_nack_count = 0u;
		p->dig_port.dp.aux.i2c_defer_count = 0u;
		drv_i915_dp_ext_detect(&p->env, &p->source, &p->sink);
	}

	/* Gives the link back: the port's last link gives the PHY back at once. */
	drv_i915_tc_put_link(ext->tc, tc_port);
	after = drv_i915_tc_mode(ext->tc, tc_port);

	/* Logs what the probe found and how the port was left. */
	drv_i915_dp_ext_log(&p->env, &p->sink, p->name);
	kern_logf("i915: DP-ext %s: probe %u held the port in %s mode (FIA lanes %d), %s after it | I2C nacks %u defers %u\n",
		  p->name,
		  p->probes,
		  drv_i915_tc_mode_name(mode),
		  p->source.fia_lanes,
		  drv_i915_tc_mode_name(after),
		  p->dig_port.dp.aux.i2c_nack_count,
		  p->dig_port.dp.aux.i2c_defer_count);
}

/* Reads DPCD bytes over the port's AUX channel for the probe. */
static long
i915_dp_ext_dpcd_read(
	void *ctx,
	unsigned offset,
	uint8_t *buffer,
	size_t size)
{
	struct i915_dp_ext_port *p;
	long transferred;

	/* Reads over the port's channel. */
	p = ctx;
	transferred = drv_i915_drm_dp_dpcd_read(&p->dig_port.dp.aux, offset, buffer, size);
	if (transferred < 0)
		return transferred;

	/* Succeeded: reports the bytes read. */
	return transferred;
}

/* Writes DPCD bytes over the port's AUX channel for the probe. */
static long
i915_dp_ext_dpcd_write(
	void *ctx,
	unsigned offset,
	const uint8_t *buffer,
	size_t size)
{
	struct i915_dp_ext_port *p;
	long transferred;

	/* Writes over the port's channel. */
	p = ctx;
	transferred = drv_i915_drm_dp_dpcd_write(&p->dig_port.dp.aux, offset, buffer, size);
	if (transferred < 0)
		return transferred;

	/* Succeeded: reports the bytes written. */
	return transferred;
}

/* Wakes the port's sink with a throw-away read. */
static int
i915_dp_ext_dpcd_probe(
	void *ctx,
	unsigned offset)
{
	struct i915_dp_ext_port *p;
	int error;

	/* Reads one byte over the port's channel. */
	p = ctx;
	error = drv_i915_drm_dp_dpcd_probe(&p->dig_port.dp.aux, offset);
	if (error != 0)
		return error;

	/* Succeeded: the sink answered. */
	return 0;
}

/* Reads the port's receiver capabilities, the extended ones where the sink has them. */
static int
i915_dp_ext_read_caps(
	void *ctx,
	uint8_t dpcd[I915_DP_EXT_DPCD_SIZE])
{
	struct i915_dp_ext_port *p;
	int error;

	/* Reads over the port's channel. */
	p = ctx;
	error = drv_i915_drm_dp_read_dpcd_caps(&p->dig_port.dp.aux, dpcd);
	if (error != 0)
		return error;

	/* Succeeded: the capabilities are in dpcd. */
	return 0;
}

/* Asks whether a device answers at the EDID address behind the port's channel. */
static int
i915_dp_ext_ddc_probe(
	void *ctx)
{
	struct i915_dp_ext_port *p;
	bool answered;

	/* Asks over the channel's I2C-over-AUX adapter. */
	p = ctx;
	answered = drv_i915_drm_probe_ddc(&p->dig_port.dp.aux.ddc);
	if (!answered)
		return 0;

	/* Succeeded: a device answered. */
	return 1;
}

/* Reads the EDID behind the port's channel: the valid blocks, or a negative Linux errno. */
static int
i915_dp_ext_edid_read(
	void *ctx,
	uint8_t *buffer,
	unsigned max_blocks,
	unsigned *extensions)
{
	struct i915_dp_ext_port *p;
	int blocks;

	/* Reads over the channel's I2C-over-AUX adapter. */
	p = ctx;
	blocks = drv_i915_drm_edid_read(&p->dig_port.dp.aux.ddc, buffer, max_blocks, extensions);
	if (blocks < 0)
		return blocks;

	/* Succeeded: reports the valid blocks. */
	return blocks;
}

/* Writes a line of the probe's log to the kernel log. */
static void
i915_dp_ext_log(
	void *ctx,
	const char *format,
	...)
{
	char line[I915_DP_EXT_KERN_LOG_LINE];
	va_list arguments;

	UNUSED_PARAMETER(ctx);

	/* Formats the line and writes it. */
	va_start(arguments, format);
	(void)kern_vsnprintf(line, sizeof(line), format, arguments);
	va_end(arguments);
	kern_logf("%s", line);
}

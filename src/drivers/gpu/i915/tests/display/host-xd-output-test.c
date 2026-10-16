/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of a screen's output (display/xd-output.c) on the
 * register model: the swing table rows of the sink's level bytes; the
 * light of the 5330's panel on pipe A (the power uses in order, the
 * display buffer, the backlight's setup from the VBT, the pipe's DMC,
 * the AUX power before the PLL, the PLL's words, the port and its link
 * trained against a model sink, the transcoder's words, the start with
 * the backlight); the dark that gives everything back in reverse; the
 * backlight's levels (the user's range, the firmware's request, on and
 * off, a panel the other way round); and the faults: a PLL that does not
 * lock, a link that does not train, a pipe found running.
 */

#include "host-test.h"
#include "xd-fake-regs.h"

#include "../../display/xd-ddi.h"
#include "../../display/xd-dmc.h"
#include "../../display/xd-io.h"
#include "../../display/xd-output.h"
#include "../../display/xd-platform.h"
#include "../../display/xd-screen.h"
#include "../../intel/display-dpcd-xd.h"
#include "../../intel/display-regs-xd.h"
#include "../../intel/display-regs-xd-south.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The 5330's Alder Lake-P. */
#define I915_XD_OT_TEST_ADLP		0x46a6U
#define I915_XD_OT_TEST_ADLP_REVID	0x0cU

/* The events the log keeps: the environment's calls and the writes of the registers the test watches. */
#define I915_XD_OT_GET			1U
#define I915_XD_OT_PUT			2U
#define I915_XD_OT_PUT_LATER		3U
#define I915_XD_OT_PANEL		4U
#define I915_XD_OT_SLICES		5U
#define I915_XD_OT_VBLANK_OFF		6U
#define I915_XD_OT_LINK_FAILED		7U
#define I915_XD_OT_OBSERVE		8U
#define I915_XD_OT_DPCD_WRITE		9U
#define I915_XD_OT_WRITE		10U

/* The most events the log keeps. */
#define I915_XD_OT_LOG			512U

/* One event: its kind and two values (the use and the cookie, the register and the value, the address and its first byte). */
struct i915_xd_ot_event {
	unsigned kind;
	uint32_t a;
	uint32_t b;
};

/*
 * The model of the panel's sink: the pattern and levels it was given,
 * the lanes, whether it never recovers its clock, and its power state.
 */
struct i915_xd_ot_sink {
	uint8_t caps[I915_XD_DPCD_CAPS_SIZE];
	uint8_t pattern;
	uint8_t set[4];
	unsigned lanes;
	int cr_never;
	uint8_t power;
	uint8_t oui[3];
};

/*
 * The log of a run: its events in order, the errors the output noted
 * (and the last one's text), and the locks it holds.
 */
struct i915_xd_ot_run {
	struct i915_xd_ot_event log[I915_XD_OT_LOG];
	unsigned count;
	unsigned errors;
	const char *last_error;
	int locks;
	int cookie_next;
	int failing_get;
};

/* The window, the run's log and the sink the checks share; each group starts them afresh. */
static struct i915_xd_io i915_xd_ot_io;
static struct i915_xd_ot_run i915_xd_ot_log;
static struct i915_xd_ot_sink i915_xd_ot_sink;

static const struct i915_xd_platform *i915_xd_ot_setup(void);
static void i915_xd_ot_env(struct i915_xd_output_env *env, const struct i915_xd_platform *platform);
static void i915_xd_ot_panel_output(struct i915_xd_output *out);
static void i915_xd_ot_record(unsigned kind, uint32_t a, uint32_t b);
static int i915_xd_ot_find(unsigned kind, uint32_t a, unsigned from);
static int i915_xd_ot_find_value(unsigned kind, uint32_t a, uint32_t b, unsigned from);
static void i915_xd_ot_watch(uint32_t offset, uint32_t value, void *context);
static void i915_xd_ot_buffer(uint32_t offset, uint32_t value, void *context);
static int i915_xd_ot_power_get(void *context, unsigned use, unsigned index);
static void i915_xd_ot_power_put(void *context, unsigned use, unsigned index, int cookie);
static void i915_xd_ot_power_put_later(void *context, unsigned use, unsigned index, int cookie, int delay_ms);
static int i915_xd_ot_panel(void *context, unsigned step);
static void i915_xd_ot_slices(void *context, uint8_t slices);
static void i915_xd_ot_lock(void *context, unsigned lock, int taken);
static void i915_xd_ot_vblank_off(void *context);
static void i915_xd_ot_link_failed(void *context);
static void i915_xd_ot_observe(void *context, unsigned point);
static void i915_xd_ot_error(void *context, const char *text);
static int i915_xd_ot_sink_read(void *context, uint32_t address, uint8_t *buffer, unsigned size);
static int i915_xd_ot_sink_write(void *context, uint32_t address, const uint8_t *buffer, unsigned size);
static int i915_xd_ot_sink_probe(void *context, uint32_t address);
static int i915_xd_ot_sink_caps(void *context, uint8_t *caps);
static int i915_xd_ot_light(struct i915_xd_output *out, const struct i915_xd_output_env *env);
static void i915_xd_ot_dark(struct i915_xd_output *out, const struct i915_xd_output_env *env);
static void i915_xd_ot_test_rows(void);
static void i915_xd_ot_test_light(void);
static void i915_xd_ot_test_dark(void);
static void i915_xd_ot_test_backlight(void);
static void i915_xd_ot_test_faults(void);

/*
 * Runs every check of the output and reports.
 */
int
main(void)
{
	int failures;

	/* Runs each group of checks. */
	i915_xd_ot_test_rows();
	i915_xd_ot_test_light();
	i915_xd_ot_test_dark();
	i915_xd_ot_test_backlight();
	i915_xd_ot_test_faults();

	/* Reports the result. */
	failures = i915_host_report("host_xd_output_test");
	if (failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Starts a fresh model, log and sink, with the hardware's answers the
 * output waits for: the PLL's power and lock follow their requests, the
 * DDI buffer leaves idle while enabled, the transcoder's state follows
 * its enable, the idle patterns are sent; returns the 5330's
 * description.
 */
static const struct i915_xd_platform *
i915_xd_ot_setup(void)
{
	static const uint32_t watched[] = {
		0x46010U, 0x164280U, 0x164284U, 0x164288U, 0x46140U, 0x60400U, 0x60410U, 0x6001cU, 0x70030U,
		0x60030U, 0x70008U, 0x70034U, 0x45270U, 0x7003cU, 0x45250U, 0x60200U, 0x68180U, 0x68280U,
		0xc8250U, 0xc8254U, 0xc8258U, 0x4438cU, 0x78000U, 0x60540U,
	};
	struct i915_xd_io_hooks hooks;
	struct i915_xd_choice choice;
	unsigned index;
	int error;

	/* A fresh model and a window that reaches it. */
	i915_xd_fake_reset();
	i915_xd_fake_hooks(&hooks);
	drv_i915_xd_io_init(&i915_xd_ot_io, NULL, NULL);
	drv_i915_xd_io_set_hooks(&i915_xd_ot_io, &hooks);
	memset(&i915_xd_ot_log, 0, sizeof(i915_xd_ot_log));
	i915_xd_ot_log.cookie_next = 1;

	/* The panel's sink: DPCD 1.1, HBR, two lanes, enhanced framing, asleep. */
	memset(&i915_xd_ot_sink, 0, sizeof(i915_xd_ot_sink));
	i915_xd_ot_sink.caps[0] = 0x11U;
	i915_xd_ot_sink.caps[1] = 0x0aU;
	i915_xd_ot_sink.caps[2] = 0x82U;
	i915_xd_ot_sink.power = I915_XD_DPCD_POWER_D3;

	/* The hardware's answers. */
	i915_xd_fake_rule_mirror(0x46010U, 0x08000000U, 0x46010U, -1, 0U);
	i915_xd_fake_rule_mirror(0x46010U, 0x80000000U, 0x46010U, -1, 0U);
	i915_xd_fake_rule_mirror(0x70008U, 0x80000000U, 0x70008U, -1, 0U);
	i915_xd_fake_rule_call(I915_XD_DDI_BUF_CTL(0U), i915_xd_ot_buffer, NULL);
	i915_xd_fake_set(I915_XD_DDI_BUF_CTL(0U), I915_XD_DDI_BUF_IDLE);
	i915_xd_fake_set(0x60544U, 0x02000000U);

	/* The registers whose writes the log keeps. */
	for (index = 0U; index < sizeof(watched) / sizeof(watched[0]); index++)
		i915_xd_fake_rule_call(watched[index], i915_xd_ot_watch, NULL);

	/* The description. */
	error = drv_i915_xd_platform_choose(I915_XD_OT_TEST_ADLP, I915_XD_OT_TEST_ADLP_REVID, 0, 0U, &choice);
	if (error != 0)
		return NULL;

	/* Succeeded: the description. */
	return choice.platform;
}

/* Fills the environment the output runs in: the model's window and the test's hooks. */
static void
i915_xd_ot_env(
	struct i915_xd_output_env *env,
	const struct i915_xd_platform *platform)
{
	/* The window, the platform and every hook. */
	memset(env, 0, sizeof(*env));
	env->io = &i915_xd_ot_io;
	env->platform = platform;
	env->power_get = i915_xd_ot_power_get;
	env->power_put = i915_xd_ot_power_put;
	env->power_put_later = i915_xd_ot_power_put_later;
	env->panel = i915_xd_ot_panel;
	env->sink.read = i915_xd_ot_sink_read;
	env->sink.write = i915_xd_ot_sink_write;
	env->sink.probe = i915_xd_ot_sink_probe;
	env->sink.read_caps = i915_xd_ot_sink_caps;
	env->sink.context = &i915_xd_ot_sink;
	env->dbuf_slices = i915_xd_ot_slices;
	env->lock = i915_xd_ot_lock;
	env->vblank_off = i915_xd_ot_vblank_off;
	env->link_failed = i915_xd_ot_link_failed;
	env->observe = i915_xd_ot_observe;
	env->error = i915_xd_ot_error;
	env->context = &i915_xd_ot_log;
}

/*
 * Describes the 5330's panel: DDI A, AUX A, pipe and transcoder A,
 * 1920x1080 (positive horizontal sync) at 18 bpp with dither on HBR x2, DPLL 0 with its words, the
 * pipe's DMC program loaded, the VBT's 200 Hz PWM with a lowest
 * brightness of 6, the display buffer going from slice 1 to all four
 * joined.
 */
static void
i915_xd_ot_panel_output(
	struct i915_xd_output *out)
{
	/* The port, the pipe and the mode. */
	memset(out, 0, sizeof(*out));
	out->kind = I915_XD_SCREEN_PANEL;
	out->timing.hdisplay = 1920U;
	out->timing.hblank_start = 1920U;
	out->timing.hblank_end = 2080U;
	out->timing.hsync_start = 1936U;
	out->timing.hsync_end = 1952U;
	out->timing.htotal = 2080U;
	out->timing.vdisplay = 1080U;
	out->timing.vblank_start = 1080U;
	out->timing.vblank_end = 1128U;
	out->timing.vsync_start = 1083U;
	out->timing.vsync_end = 1097U;
	out->timing.vtotal = 1128U;
	out->hsync_high = 1;
	out->src_width = 1920U;
	out->src_height = 1080U;
	out->bpp = 18U;
	out->dither = 1;
	out->frame_start_delay = 1U;
	out->pixel_multiplier = 1U;

	/* The link. */
	out->rate_khz = 270000U;
	out->lanes = 2U;
	out->enhanced = 1;
	out->rate_index = -1;
	drv_i915_xd_dp_m_n(18U * 16U, 2U, 140800U, 270000U, 1000000U, &out->m_n);

	/* The PLL, the colours, the line time and the DMC programs. */
	out->pll = 0U;
	out->pll_words.cfgcr0 = 0x00e001a5U;
	out->pll_words.cfgcr1 = 0x00000088U;
	out->gamma_mode = 0U;
	out->csc_mode = 0U;
	out->linetime = 0x78U;
	out->dmc_programs = 1U << I915_XD_DMC_PIPE(0U);

	/* The sink's capabilities. */
	memcpy(out->caps, i915_xd_ot_sink.caps, sizeof(out->caps));

	/* The display buffer of a light from nothing. */
	out->dbuf.slices_before = 0x1U;
	out->dbuf.slices_after = 0xfU;
	out->dbuf.joined_before = 0U;
	out->dbuf.joined_after = 1U;
	out->dbuf.pipes_before = 0U;
	out->dbuf.pipes_after = 0x1U;

	/* The VBT's backlight and the south display's. */
	out->backlight.vbt_present = 1;
	out->backlight.vbt_controller = 0U;
	out->backlight.vbt_hz = 200U;
	out->backlight.vbt_min = 6U;
	out->backlight.rawclk_khz = 19200U;
	out->backlight.controllers = 2U;
	out->backlight.muxed = 1;
}

/* Adds an event to the run's log. */
static void
i915_xd_ot_record(
	unsigned kind,
	uint32_t a,
	uint32_t b)
{
	struct i915_xd_ot_event *event;

	/* A full log keeps its first events. */
	if (i915_xd_ot_log.count == I915_XD_OT_LOG)
		return;

	/* The next event. */
	event = &i915_xd_ot_log.log[i915_xd_ot_log.count];
	event->kind = kind;
	event->a = a;
	event->b = b;
	i915_xd_ot_log.count++;
}

/* Finds the first event of a kind and first value from a position; -1 when none. */
static int
i915_xd_ot_find(
	unsigned kind,
	uint32_t a,
	unsigned from)
{
	unsigned index;

	/* The events in order. */
	for (index = from; index < i915_xd_ot_log.count; index++) {
		if (i915_xd_ot_log.log[index].kind == kind && i915_xd_ot_log.log[index].a == a)
			return (int)index;
	}

	/* None. */
	return -1;
}

/* Finds the first event of a kind and both values from a position; -1 when none. */
static int
i915_xd_ot_find_value(
	unsigned kind,
	uint32_t a,
	uint32_t b,
	unsigned from)
{
	unsigned index;

	/* The events in order. */
	for (index = from; index < i915_xd_ot_log.count; index++) {
		if (i915_xd_ot_log.log[index].kind == kind &&
		    i915_xd_ot_log.log[index].a == a &&
		    i915_xd_ot_log.log[index].b == b)
			return (int)index;
	}

	/* None. */
	return -1;
}

/* Logs a write of a watched register. */
static void
i915_xd_ot_watch(
	uint32_t offset,
	uint32_t value,
	void *context)
{
	(void)context;

	/* The register and its value. */
	i915_xd_ot_record(I915_XD_OT_WRITE, offset, value);
}

/* Answers a write of DDI A's buffer: idle while it is not enabled. */
static void
i915_xd_ot_buffer(
	uint32_t offset,
	uint32_t value,
	void *context)
{
	(void)context;

	/* The idle bit follows the enable. */
	if ((value & I915_XD_DDI_BUF_ENABLE) != 0U) {
		i915_xd_fake_set(offset, value & ~I915_XD_DDI_BUF_IDLE);
	} else {
		i915_xd_fake_set(offset, value | I915_XD_DDI_BUF_IDLE);
	}
	i915_xd_ot_record(I915_XD_OT_WRITE, offset, value);
}

/* Takes a reference: a new cookie, or 0 when the test makes the get fail. */
static int
i915_xd_ot_power_get(
	void *context,
	unsigned use,
	unsigned index)
{
	struct i915_xd_ot_run *run;
	int cookie;

	(void)index;

	/* The test's refusal. */
	run = context;
	if (run->failing_get)
		return 0;

	/* Succeeded: the next cookie. */
	cookie = run->cookie_next;
	run->cookie_next++;
	i915_xd_ot_record(I915_XD_OT_GET, use, (uint32_t)cookie);
	return cookie;
}

/* Gives a reference back. */
static void
i915_xd_ot_power_put(
	void *context,
	unsigned use,
	unsigned index,
	int cookie)
{
	(void)context;
	(void)index;

	/* The use and its cookie. */
	i915_xd_ot_record(I915_XD_OT_PUT, use, (uint32_t)cookie);
}

/* Gives a reference back later; the delay is kept in place of the cookie. */
static void
i915_xd_ot_power_put_later(
	void *context,
	unsigned use,
	unsigned index,
	int cookie,
	int delay_ms)
{
	(void)context;
	(void)index;
	(void)cookie;

	/* The use and the delay. */
	i915_xd_ot_record(I915_XD_OT_PUT_LATER, use, (uint32_t)delay_ms);
}

/* Takes a step of the panel's power sequencer. */
static int
i915_xd_ot_panel(
	void *context,
	unsigned step)
{
	(void)context;

	/* The step. */
	i915_xd_ot_record(I915_XD_OT_PANEL, step, 0U);

	/* Succeeded: the step ran. */
	return 0;
}

/* Takes a request for display buffer slices. */
static void
i915_xd_ot_slices(
	void *context,
	uint8_t slices)
{
	(void)context;

	/* The slices. */
	i915_xd_ot_record(I915_XD_OT_SLICES, slices, 0U);
}

/* Counts the locks the output holds. */
static void
i915_xd_ot_lock(
	void *context,
	unsigned lock,
	int taken)
{
	struct i915_xd_ot_run *run;

	(void)lock;

	/* One more or one less. */
	run = context;
	if (taken) {
		run->locks++;
	} else {
		run->locks--;
	}
}

/* Notes the settling of the pipe's event. */
static void
i915_xd_ot_vblank_off(
	void *context)
{
	(void)context;

	/* The event. */
	i915_xd_ot_record(I915_XD_OT_VBLANK_OFF, 0U, 0U);
}

/* Notes a link that did not train. */
static void
i915_xd_ot_link_failed(
	void *context)
{
	(void)context;

	/* The event. */
	i915_xd_ot_record(I915_XD_OT_LINK_FAILED, 0U, 0U);
}

/* Notes a point of the run. */
static void
i915_xd_ot_observe(
	void *context,
	unsigned point)
{
	(void)context;

	/* The point. */
	i915_xd_ot_record(I915_XD_OT_OBSERVE, point, 0U);
}

/* Counts an error and keeps its text. */
static void
i915_xd_ot_error(
	void *context,
	const char *text)
{
	struct i915_xd_ot_run *run;

	/* The error. */
	run = context;
	run->errors++;
	run->last_error = text;
}

/*
 * Answers a DPCD read of the sink: the link status reports the clock
 * recovered at every level (never with cr_never) and the channel
 * equalized outside pattern 1, with no level requests.
 */
static int
i915_xd_ot_sink_read(
	void *context,
	uint32_t address,
	uint8_t *buffer,
	unsigned size)
{
	struct i915_xd_ot_sink *sink;
	uint8_t status[I915_XD_DPCD_STATUS_SIZE];
	unsigned lane;
	uint8_t nibble;

	/* Everything else reads as zeros. */
	sink = context;
	memset(buffer, 0, size);
	if (address != I915_XD_DPCD_LANE_STATUS)
		return (int)size;

	/* The status of each lane. */
	memset(status, 0, sizeof(status));
	for (lane = 0U; lane < sink->lanes && lane < 4U; lane++) {
		nibble = 0U;
		if (!sink->cr_never)
			nibble |= I915_XD_DPCD_LANE_CR;
		if (!sink->cr_never && (sink->pattern & 0x0fU) != I915_XD_DPCD_PATTERN_1)
			nibble |= I915_XD_DPCD_LANE_EQ | I915_XD_DPCD_LANE_LOCKED;
		status[lane >> 1] |= (uint8_t)(nibble << (4U * (lane & 1U)));
	}
	status[2] = I915_XD_DPCD_ALIGN_DONE;
	memcpy(buffer, status, size < sizeof(status) ? size : sizeof(status));

	/* Succeeded: the status is read. */
	return (int)size;
}

/* Takes a DPCD write of the sink: its power, the source's identifier, the pattern and levels, the lanes. */
static int
i915_xd_ot_sink_write(
	void *context,
	uint32_t address,
	const uint8_t *buffer,
	unsigned size)
{
	struct i915_xd_ot_sink *sink;
	unsigned lane;

	/* Every write is logged by its first byte. */
	sink = context;
	i915_xd_ot_record(I915_XD_OT_DPCD_WRITE, address, buffer[0]);

	/* The power state and the source's identifier. */
	if (address == I915_XD_DPCD_SET_POWER)
		sink->power = buffer[0];
	if (address == 0x300U && size == 3U)
		memcpy(sink->oui, buffer, 3U);

	/* The pattern with the levels after it, and the levels alone. */
	if (address == I915_XD_DPCD_PATTERN_SET) {
		sink->pattern = buffer[0];
		for (lane = 0U; lane + 1U < size && lane < 4U; lane++)
			sink->set[lane] = buffer[1U + lane];
	}
	if (address == I915_XD_DPCD_LEVELS_SET) {
		for (lane = 0U; lane < size && lane < 4U; lane++)
			sink->set[lane] = buffer[lane];
	}

	/* The rate and the lanes. */
	if (address == I915_XD_DPCD_BW_SET && size == 2U)
		sink->lanes = buffer[1] & 0x1fU;
	if (address == I915_XD_DPCD_LANES_SET)
		sink->lanes = buffer[0] & 0x1fU;

	/* Succeeded: the bytes are taken. */
	return (int)size;
}

/* Answers that the sink is awake. */
static int
i915_xd_ot_sink_probe(
	void *context,
	uint32_t address)
{
	(void)context;
	(void)address;

	/* Succeeded: the sink answers. */
	return 0;
}

/* Answers the sink's capabilities. */
static int
i915_xd_ot_sink_caps(
	void *context,
	uint8_t *caps)
{
	struct i915_xd_ot_sink *sink;

	/* The model's. */
	sink = context;
	memcpy(caps, sink->caps, I915_XD_DPCD_CAPS_SIZE);

	/* Succeeded: the capabilities are read. */
	return 0;
}

/* Runs a light's steps of the output as the screen orders them; the first failed step's error, or 0. */
static int
i915_xd_ot_light(
	struct i915_xd_output *out,
	const struct i915_xd_output_env *env)
{
	int first;
	int error;

	/* Each step, the first failure kept. */
	first = 0;
	error = drv_i915_xd_output_dc_hold(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_power(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_dbuf(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_backlight_setup(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_dmc(out, env, 1);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_link_power(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_pll(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_port(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_pipe(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_start(out, env);
	if (error != 0 && first == 0)
		first = error;
	error = drv_i915_xd_output_settle(out, env);
	if (error != 0 && first == 0)
		first = error;
	drv_i915_xd_output_dc_allow(out, env);

	/* Reports the first failure, or none. */
	return first;
}

/* Runs a dark's steps of the output as the screen orders them, for the display buffer going back to slice 1. */
static void
i915_xd_ot_dark(
	struct i915_xd_output *out,
	const struct i915_xd_output_env *env)
{
	/* The buffer of a dark that leaves nothing running. */
	out->dbuf.slices_before = 0xfU;
	out->dbuf.slices_after = 0x1U;
	out->dbuf.joined_before = 1U;
	out->dbuf.joined_after = 0U;
	out->dbuf.pipes_before = 0x1U;
	out->dbuf.pipes_after = 0U;

	/* Each step. */
	(void)drv_i915_xd_output_dc_hold(out, env);
	(void)drv_i915_xd_output_dark_power(out, env);
	(void)drv_i915_xd_output_stop(out, env);
	(void)drv_i915_xd_output_port_off(out, env);
	(void)drv_i915_xd_output_pll_off(out, env);
	(void)drv_i915_xd_output_link_power_off(out, env);
	(void)drv_i915_xd_output_dmc(out, env, 0);
	(void)drv_i915_xd_output_dark_settle(out, env);
	drv_i915_xd_output_dc_allow(out, env);
}

/* Checks the swing table rows of the sink's level bytes and the highest swing of a table. */
static void
i915_xd_ot_test_rows(void)
{
	static const uint8_t levels[10] = { 0x00U, 0x08U, 0x10U, 0x18U, 0x01U, 0x09U, 0x11U, 0x02U, 0x0aU, 0x03U };
	struct i915_xd_combo_swing_table table;
	unsigned index;
	unsigned row;
	int ordered;
	int error;

	/* Each pair a sink may ask for takes its row in order. */
	ordered = 1;
	for (index = 0U; index < 10U; index++) {
		error = drv_i915_xd_combo_level_row(levels[index], &row);
		if (error != 0 || row != index)
			ordered = 0;
	}
	i915_host_check(ordered, "output: the ten swing and pre-emphasis pairs take rows 0 to 9 in order");

	/* A pair beyond the tables, and the bits a level byte carries besides. */
	error = drv_i915_xd_combo_level_row(0x1aU, &row);
	i915_host_check(error == EINVAL && row == 0U, "output: swing 2 with pre-emphasis 3 has no row (row 0)");
	error = drv_i915_xd_combo_level_row(0x0aU | I915_XD_DPCD_SWING_TOP | I915_XD_DPCD_PREEMPH_TOP, &row);
	i915_host_check(error == 0 && row == 8U, "output: the highest-level flags of a level byte do not move its row");

	/* The highest swing a table reaches: that of its last row. */
	memset(&table, 0, sizeof(table));
	table.count = 10U;
	i915_host_check(drv_i915_xd_combo_swing_max(&table) == 3U, "output: a table of ten rows reaches swing 3");
	table.count = 9U;
	i915_host_check(drv_i915_xd_combo_swing_max(&table) == 2U, "output: a table of nine rows reaches swing 2");
	table.count = 0U;
	i915_host_check(drv_i915_xd_combo_swing_max(&table) == 0U, "output: an empty table is taken as its first row");
}

/* Checks the light of the 5330's panel. */
static void
i915_xd_ot_test_light(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_output_env env;
	struct i915_xd_output out;
	int dc;
	int core;
	int pipe;
	int trans;
	int lanes;
	int aux;
	int pll;
	int map;
	int io;
	int panel_on;
	int wake;
	int start;
	int pwm;
	int backlight;
	int error;

	/* A light of the panel. */
	platform = i915_xd_ot_setup();
	i915_host_check(platform != NULL, "output: the 5330's description");
	if (platform == NULL)
		return;
	i915_xd_ot_env(&env, platform);
	i915_xd_ot_panel_output(&out);
	error = i915_xd_ot_light(&out, &env);
	i915_host_check(error == 0 && i915_xd_ot_log.errors == 0U && i915_xd_ot_log.locks == 0,
			"output: the panel's light runs through, without errors, every lock given back");

	/* The power uses: DC states off first, then the core, the pipe, the transcoder and the lanes. */
	dc = i915_xd_ot_find(I915_XD_OT_GET, I915_XD_OUTPUT_POWER_DC_OFF, 0U);
	core = i915_xd_ot_find(I915_XD_OT_GET, I915_XD_OUTPUT_POWER_CORE, 0U);
	pipe = i915_xd_ot_find(I915_XD_OT_GET, I915_XD_OUTPUT_POWER_PIPE, 0U);
	trans = i915_xd_ot_find(I915_XD_OT_GET, I915_XD_OUTPUT_POWER_TRANS, 0U);
	lanes = i915_xd_ot_find(I915_XD_OT_GET, I915_XD_OUTPUT_POWER_LANES, 0U);
	i915_host_check(dc == 0 && core > dc && pipe > core && trans > pipe && lanes > trans,
			"output: DC states held off, then the display core, the pipe, the transcoder and the lanes");

	/* The display buffer: the bus joined and the slices of both layouts, then the pipe's credits. */
	i915_host_check(i915_xd_ot_find(I915_XD_OT_SLICES, 0xfU, 0U) > lanes &&
			(i915_xd_fake_get(0x4438cU) & 0x80000000U) != 0U &&
			i915_xd_ot_find(I915_XD_OT_WRITE, 0x7003cU, 0U) > i915_xd_ot_find(I915_XD_OT_SLICES, 0xfU, 0U),
			"output: the bus joined and slices 1 to 4 asked for, then the pipe's data box credits");

	/* The backlight from the VBT: 19.2 MHz over 200 Hz, the lowest duty of 6/255, the level at the highest. */
	i915_host_check(out.backlight.present && out.backlight.max == 96000U && out.backlight.min == 2259U &&
			out.backlight.level == 96000U && out.backlight.enabled,
			"output: the backlight's range from the VBT (96000, lowest 2259), the level at the highest, on");

	/* The pipe's DMC, the AUX power before the PLL, the PLL's words, power and lock. */
	aux = i915_xd_ot_find(I915_XD_OT_GET, I915_XD_OUTPUT_POWER_AUX, 0U);
	pll = i915_xd_ot_find_value(I915_XD_OT_WRITE, 0x46010U, 0x88000000U, 0U);
	if (pll < 0)
		pll = i915_xd_ot_find(I915_XD_OT_WRITE, 0x164284U, 0U);
	i915_host_check((i915_xd_fake_get(0x45250U) & 1U) != 0U && aux > lanes && pll > aux,
			"output: the pipe's DMC switched on, the AUX power taken before the PLL");
	i915_host_check(i915_xd_fake_get(0x164284U) == 0x00e001a5U && i915_xd_fake_get(0x164288U) == 0x00000088U &&
			(i915_xd_fake_get(0x46010U) & 0xcc000000U) == 0xcc000000U && out.pll_on && out.pll_pipes == 1U,
			"output: DPLL 0 with the plan's words, powered and locked, for pipe A");

	/* The port: the panel's power before the clock map and the IO power, the clock select, the function. */
	panel_on = i915_xd_ot_find(I915_XD_OT_PANEL, I915_XD_OUTPUT_PANEL_ON, 0U);
	map = i915_xd_ot_find(I915_XD_OT_WRITE, 0x164280U, 0U);
	io = i915_xd_ot_find(I915_XD_OT_GET, I915_XD_OUTPUT_POWER_DDI_IO, 0U);
	i915_host_check(panel_on > pll && map > panel_on && io > map,
			"output: the panel's power, then DDI A's clock from DPLL 0, then the port's IO power");
	i915_host_check((i915_xd_fake_get(0x164280U) & 0x403U) == 0U && i915_xd_fake_get(0x46140U) == 0x10000000U,
			"output: DDI A's clock ungated on DPLL 0, transcoder A on PHY A's clock");
	i915_host_check(i915_xd_fake_get(0x60400U) == 0x8a210002U,
			"output: transcoder A's function: enabled, DDI A, DP SST, 6 bpc, positive horizontal sync, two lanes");

	/* The sink: the source's identifier, then woken, before the training. */
	wake = i915_xd_ot_find_value(I915_XD_OT_DPCD_WRITE, I915_XD_DPCD_SET_POWER, I915_XD_DPCD_POWER_D0, 0U);
	i915_host_check(i915_xd_ot_find(I915_XD_OT_DPCD_WRITE, 0x300U, 0U) > io && wake > i915_xd_ot_find(I915_XD_OT_DPCD_WRITE, 0x300U, 0U) &&
			i915_xd_ot_sink.oui[1] == 0xaaU && i915_xd_ot_find(I915_XD_OT_DPCD_WRITE, I915_XD_DPCD_PATTERN_SET, 0U) > wake,
			"output: the source's identifier written, the sink woken, then the training");

	/* The link trained and carrying the stream. */
	i915_host_check(out.trained && i915_xd_ot_sink.lanes == 2U && (i915_xd_ot_sink.pattern & 0x0fU) == 0U &&
			(i915_xd_fake_get(I915_XD_DDI_BUF_CTL(0U)) & 0x80000080U) == 0x80000000U &&
			(i915_xd_fake_get(0x60540U) & 0x80000700U) == 0x80000300U,
			"output: the link trained on two lanes, the sink out of the pattern, the buffer on, the transport normal");

	/* The pipe's words: the source, the depth with dither, the stream's attributes, the M/N. */
	i915_host_check(i915_xd_fake_get(0x6001cU) == ((1919U << 16) | 1079U) &&
			(i915_xd_fake_get(0x70030U) & 0xf0U) == 0x50U &&
			i915_xd_ot_find(I915_XD_OT_WRITE, 0x60410U, 0U) >= 0 &&
			i915_xd_fake_get(0x60030U) == ((out.m_n.tu - 1U) << 25 | out.m_n.data_m),
			"output: the pipe's source 1920x1080, 6 bpc dithered, the stream's attributes, the data M/N");

	/* The output: the transcoder running, then the PWM, then the sequencer's backlight enable. */
	start = i915_xd_ot_find_value(I915_XD_OT_WRITE, 0x70008U, 0x80000000U, 0U);
	pwm = i915_xd_ot_find_value(I915_XD_OT_WRITE, 0xc8250U, 0x80000000U, 0U);
	backlight = i915_xd_ot_find(I915_XD_OT_PANEL, I915_XD_OUTPUT_PANEL_BACKLIGHT_ON, 0U);
	i915_host_check(out.lit && (i915_xd_fake_get(0x70008U) & 0xc0000000U) == 0xc0000000U &&
			start > wake && pwm > start && backlight > pwm,
			"output: the transcoder runs, then the PWM starts, then the sequencer's backlight enable");
	i915_host_check(i915_xd_fake_get(0xc8254U) == 96000U && i915_xd_fake_get(0xc8258U) == 96000U,
			"output: the PWM at the VBT's period and the highest duty");

	/* The settle: the new slices alone, DC states back 17 ms later, the uses kept. */
	i915_host_check(i915_xd_ot_find(I915_XD_OT_SLICES, 0xfU, (unsigned)backlight) > backlight &&
			i915_xd_ot_find_value(I915_XD_OT_PUT_LATER, I915_XD_OUTPUT_POWER_DC_OFF, 17U, 0U) >= 0 &&
			i915_xd_ot_find(I915_XD_OT_PUT, I915_XD_OUTPUT_POWER_PIPE, 0U) < 0 &&
			out.held == ((1U << I915_XD_OUTPUT_POWER_CORE) | (1U << I915_XD_OUTPUT_POWER_PIPE) |
				     (1U << I915_XD_OUTPUT_POWER_TRANS) | (1U << I915_XD_OUTPUT_POWER_LANES) |
				     (1U << I915_XD_OUTPUT_POWER_DDI_IO) | (1U << I915_XD_OUTPUT_POWER_AUX)),
			"output: the settle keeps the new slices, lets DC states back after 17 ms, keeps the pipe's power");
}

/* Checks the dark of the panel after its light. */
static void
i915_xd_ot_test_dark(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_output_env env;
	struct i915_xd_output out;
	unsigned from;
	int backlight;
	int pwm;
	int vblank;
	int stop;
	int sleep;
	int vdd;
	int off;
	int io;
	int aux;
	int put;

	/* A light, then a dark, of the panel. */
	platform = i915_xd_ot_setup();
	if (platform == NULL)
		return;
	i915_xd_ot_env(&env, platform);
	i915_xd_ot_panel_output(&out);
	(void)i915_xd_ot_light(&out, &env);
	from = i915_xd_ot_log.count;
	i915_xd_ot_dark(&out, &env);
	i915_host_check(i915_xd_ot_log.errors == 0U && i915_xd_ot_log.locks == 0, "output: the panel's dark runs without errors, every lock given back");

	/* The backlight: the sequencer's enable, then the PWM at duty 0 and stopped. */
	backlight = i915_xd_ot_find(I915_XD_OT_PANEL, I915_XD_OUTPUT_PANEL_BACKLIGHT_OFF, from);
	pwm = i915_xd_ot_find_value(I915_XD_OT_WRITE, 0xc8258U, 0U, from);
	i915_host_check(backlight > (int)from && pwm > backlight &&
			(i915_xd_fake_get(0xc8250U) & 0x80000000U) == 0U && !out.backlight.enabled && out.backlight.level == 96000U,
			"output: the sequencer's backlight enable off, then the PWM at duty 0 and stopped, the level kept");

	/* The transcoder: the pipe's event settled first, then stopped and its function cleared, the scalers off. */
	vblank = i915_xd_ot_find(I915_XD_OT_VBLANK_OFF, 0U, from);
	stop = i915_xd_ot_find_value(I915_XD_OT_WRITE, 0x70008U, 0x40000000U, from);
	if (stop < 0)
		stop = i915_xd_ot_find_value(I915_XD_OT_WRITE, 0x70008U, 0x00000000U, from);
	i915_host_check(vblank > pwm && stop > vblank && (i915_xd_fake_get(0x70008U) & 0xc0000000U) == 0U &&
			(i915_xd_fake_get(0x60400U) & 0x80000000U) == 0U &&
			i915_xd_ot_find_value(I915_XD_OT_WRITE, 0x68180U, 0U, from) > stop &&
			i915_xd_ot_find_value(I915_XD_OT_WRITE, 0x68280U, 0U, from) > stop,
			"output: the pipe's event settled, the transcoder stopped, its function off, both scalers off");

	/* The sink to sleep, then the port: buffer off and idle, no clock to the transcoder. */
	sleep = i915_xd_ot_find_value(I915_XD_OT_DPCD_WRITE, I915_XD_DPCD_SET_POWER, I915_XD_DPCD_POWER_D3, from);
	i915_host_check(sleep > stop && i915_xd_ot_sink.power == I915_XD_DPCD_POWER_D3 &&
			(i915_xd_fake_get(I915_XD_DDI_BUF_CTL(0U)) & 0x80000080U) == 0x80U &&
			i915_xd_fake_get(0x46140U) == 0U &&
			i915_xd_ot_find_value(I915_XD_OT_WRITE, I915_XD_DDI_BUF_CTL(0U), 0x00000002U, from) > sleep,
			"output: the sink put to sleep, then the DDI buffer off and idle, no clock to the transcoder");

	/* The panel's power with VDD held, then the port's IO power, then the clock gated. */
	vdd = i915_xd_ot_find(I915_XD_OT_PANEL, I915_XD_OUTPUT_PANEL_VDD_ON, from);
	off = i915_xd_ot_find(I915_XD_OT_PANEL, I915_XD_OUTPUT_PANEL_OFF, from);
	io = i915_xd_ot_find(I915_XD_OT_PUT, I915_XD_OUTPUT_POWER_DDI_IO, from);
	i915_host_check(vdd > sleep && off > vdd && io > off &&
			i915_xd_ot_find(I915_XD_OT_WRITE, 0x164280U, (unsigned)io) > io &&
			(i915_xd_fake_get(0x164280U) & 0x400U) == 0x400U,
			"output: VDD held, the panel off, the port's IO power given back, then DDI A's clock gated");

	/* The PLL off, then the AUX power, then the pipe's DMC. */
	aux = i915_xd_ot_find(I915_XD_OT_PUT, I915_XD_OUTPUT_POWER_AUX, from);
	i915_host_check((i915_xd_fake_get(0x46010U) & 0xcc000000U) == 0U && !out.pll_on && out.pll_pipes == 0U &&
			aux > io && (i915_xd_fake_get(0x45250U) & 1U) == 0U && !out.lit,
			"output: DPLL 0 off, the AUX power given back, the pipe's DMC off, the pipe no longer runs");

	/* The display buffer back to slice 1 split, the pipe's power given back, DC states let back. */
	put = i915_xd_ot_find(I915_XD_OT_PUT, I915_XD_OUTPUT_POWER_LANES, from);
	i915_host_check((i915_xd_fake_get(0x4438cU) & 0x80000000U) == 0U &&
			i915_xd_ot_find(I915_XD_OT_SLICES, 0xfU, from) > aux &&
			i915_xd_ot_find(I915_XD_OT_SLICES, 0x1U, from) > i915_xd_ot_find(I915_XD_OT_SLICES, 0xfU, from) &&
			put > i915_xd_ot_find(I915_XD_OT_SLICES, 0x1U, from) &&
			i915_xd_ot_find(I915_XD_OT_PUT, I915_XD_OUTPUT_POWER_CORE, from) > i915_xd_ot_find(I915_XD_OT_SLICES, 0x1U, from) &&
			i915_xd_ot_find_value(I915_XD_OT_PUT_LATER, I915_XD_OUTPUT_POWER_DC_OFF, 17U, from) > put &&
			out.held == 0U,
			"output: the bus split, slices 1 to 4 then slice 1 alone, the pipe's power given back, DC states back later: nothing held");
}

/* Checks the backlight's levels. */
static void
i915_xd_ot_test_backlight(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_output_env env;
	struct i915_xd_output out;
	int error;

	/* A panel whose firmware left the PWM running at its own period and a duty. */
	platform = i915_xd_ot_setup();
	if (platform == NULL)
		return;
	i915_xd_ot_env(&env, platform);
	i915_xd_ot_panel_output(&out);
	i915_xd_fake_set(0xc8250U, 0x80000000U);
	i915_xd_fake_set(0xc8254U, 50000U);
	i915_xd_fake_set(0xc8258U, 30000U);
	error = drv_i915_xd_output_backlight_setup(&out, &env);
	i915_host_check(error == 0 && out.backlight.max == 50000U && out.backlight.level == 30000U && out.backlight.enabled,
			"output: the firmware's period is the range, its duty the level, the PWM found on");

	/* The user's half lies half way from the lowest level, rounded to the closest. */
	drv_i915_xd_output_brightness(&out, &env, 50U, 100U);
	i915_host_check(out.backlight.level == out.backlight.min + (50000U - out.backlight.min + 1U) / 2U &&
			i915_xd_fake_get(0xc8258U) == out.backlight.level,
			"output: the user's half lies half way from the lowest level, shown at once");

	/* The user's 0 is the lowest level, never 0; the user's level comes back from it. */
	drv_i915_xd_output_brightness(&out, &env, 0U, 100U);
	i915_host_check(out.backlight.level == out.backlight.min && out.backlight.level != 0U &&
			drv_i915_xd_output_user_level(&out, 100U) == 0U,
			"output: the user's 0 is the lowest level (not 0), and reads back as 0");

	/* The firmware's request on the whole range, raised to the lowest level. */
	drv_i915_xd_output_brightness_acpi(&out, &env, 1U, 255U);
	i915_host_check(out.backlight.level == out.backlight.min, "output: a firmware request below the lowest level is raised to it");
	drv_i915_xd_output_brightness_acpi(&out, &env, 255U, 255U);
	i915_host_check(out.backlight.level == 50000U, "output: the firmware's highest is the period");

	/* Off keeps the level; on from the lowest comes back at the highest. */
	drv_i915_xd_output_brightness(&out, &env, 0U, 100U);
	drv_i915_xd_output_backlight_off(&out, &env);
	i915_host_check(!out.backlight.enabled && out.backlight.level == out.backlight.min &&
			(i915_xd_fake_get(0xc8250U) & 0x80000000U) == 0U,
			"output: off stops the PWM and keeps the level");
	drv_i915_xd_output_backlight_on(&out, &env);
	i915_host_check(out.backlight.enabled && out.backlight.level == 50000U && i915_xd_fake_get(0xc8258U) == 50000U,
			"output: on from the lowest level comes back at the highest");

	/* A level set while off is shown at the next on. */
	drv_i915_xd_output_backlight_off(&out, &env);
	drv_i915_xd_output_brightness(&out, &env, 100U, 100U);
	i915_host_check(i915_xd_fake_get(0xc8258U) == 0U, "output: a level set while off is not shown");
	drv_i915_xd_output_backlight_on(&out, &env);
	i915_host_check(i915_xd_fake_get(0xc8258U) == 50000U, "output: ... and is shown at the next on");

	/* A panel the other way round takes the range read backwards. */
	out.backlight.inverted = 1;
	drv_i915_xd_output_brightness(&out, &env, 100U, 100U);
	i915_host_check(i915_xd_fake_get(0xc8258U) == out.backlight.min, "output: an inverted panel's highest level is the lowest duty");

	/* A panel without a VBT backlight has none. */
	i915_xd_ot_panel_output(&out);
	out.backlight.vbt_present = 0;
	error = drv_i915_xd_output_backlight_setup(&out, &env);
	i915_host_check(error == 0 && !out.backlight.present, "output: a panel the VBT gives no backlight has none");

	/* No period from the firmware nor the VBT: no backlight can be driven. */
	i915_xd_ot_panel_output(&out);
	out.backlight.rawclk_khz = 0U;
	i915_xd_fake_set(0xc8254U, 0U);
	error = drv_i915_xd_output_backlight_setup(&out, &env);
	i915_host_check(error == ENODEV && !out.backlight.present && i915_xd_ot_log.locks == 0,
			"output: no period from the firmware or the VBT is refused, the lock given back");
}

/* Checks the faults of a light. */
static void
i915_xd_ot_test_faults(void)
{
	struct i915_xd_io_hooks hooks;
	const struct i915_xd_platform *platform;
	struct i915_xd_output_env env;
	struct i915_xd_output out;
	int error;

	/* A PLL that does not lock: the step reports it, the PLL counts as on (the dark turns it off). */
	platform = i915_xd_ot_setup();
	if (platform == NULL)
		return;
	i915_xd_fake_reset();
	i915_xd_fake_hooks(&hooks);
	drv_i915_xd_io_set_hooks(&i915_xd_ot_io, &hooks);
	i915_xd_fake_rule_mirror(0x46010U, 0x08000000U, 0x46010U, -1, 0U);
	i915_xd_ot_env(&env, platform);
	i915_xd_ot_panel_output(&out);
	(void)drv_i915_xd_output_link_power(&out, &env);
	error = drv_i915_xd_output_pll(&out, &env);
	i915_host_check(error == EIO && i915_xd_ot_log.errors == 1U && strstr(i915_xd_ot_log.last_error, "not locked") != NULL &&
			out.pll_on && i915_xd_ot_log.locks == 0,
			"output: a PLL that does not lock is reported, counts as on, the lock given back");
	error = drv_i915_xd_output_pll_off(&out, &env);
	i915_host_check(error == 0 && !out.pll_on && out.pll_pipes == 0U, "output: ... and the dark turns it off");

	/* A link that does not train: the caller hears of it, the link still ends its training. */
	platform = i915_xd_ot_setup();
	if (platform == NULL)
		return;
	i915_xd_ot_env(&env, platform);
	i915_xd_ot_panel_output(&out);
	i915_xd_ot_sink.cr_never = 1;
	(void)i915_xd_ot_light(&out, &env);
	i915_host_check(i915_xd_ot_find(I915_XD_OT_LINK_FAILED, 0U, 0U) >= 0 && out.trained && out.lit,
			"output: a link that does not train is the caller's to lower; the pipe still lights (black)");

	/* A pipe found running is not lit again. */
	error = drv_i915_xd_output_dmc(&out, &env, 1);
	i915_host_check(error == EBUSY, "output: a pipe that runs already is refused");

	/* A failed reference fails its step. */
	platform = i915_xd_ot_setup();
	if (platform == NULL)
		return;
	i915_xd_ot_env(&env, platform);
	i915_xd_ot_panel_output(&out);
	i915_xd_ot_log.failing_get = 1;
	error = drv_i915_xd_output_power(&out, &env);
	i915_host_check(error == EIO && out.held == 0U && i915_xd_ot_log.errors == 1U,
			"output: a power reference that could not be taken fails the step, nothing held");

	/* A pipe without a DMC program has nothing to switch. */
	platform = i915_xd_ot_setup();
	if (platform == NULL)
		return;
	i915_xd_ot_env(&env, platform);
	i915_xd_ot_panel_output(&out);
	out.dmc_programs = 0U;
	(void)drv_i915_xd_output_dmc(&out, &env, 1);
	i915_host_check(i915_xd_ot_find(I915_XD_OT_WRITE, 0x45250U, 0U) < 0, "output: a pipe without a DMC program has nothing switched");
}

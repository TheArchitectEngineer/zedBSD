/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the firmware's display (display/xd-takeover.c): the
 * decision on each start situation (an idle start, the 5330's firmware
 * picture on pipe A from GGTT page 0, a scanout in the driver's GGTT
 * pages, the VT-d unit's walls, a guest, a garbled pipe, a device
 * without a description), the readout of an Alder Lake-P on the
 * register model (which writes nothing), the output's kind by the
 * platform's ports, and the framebuffer's GGTT range; the takeover's
 * stop of the 5330's panel on pipe A (its order, the caller's hooks, the
 * PLL), of an HDMI pipe, of two pipes on one PLL, and the VGA plane's.
 */

#include "host-test.h"
#include "xd-fake-regs.h"

#include "../../display/xd-io.h"
#include "../../display/xd-platform.h"
#include "../../display/xd-power.h"
#include "../../display/xd-takeover.h"
#include "../../display/xd.h"
#include "../../intel/display-dpcd-xd.h"
#include "../../intel/display-regs-xd.h"
#include "../../intel/display-regs-xd-south.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The 5330's Alder Lake-P. */
#define I915_XD_TO_TEST_ADLP		0x46a6U
#define I915_XD_TO_TEST_ADLP_REVID	0x0cU

/* The 8 MiB GGTT and the first page the driver writes at its top. */
#define I915_XD_TO_TEST_GGTT_PAGES	1048576U
#define I915_XD_TO_TEST_DRIVER_FIRST	(1048576U - 256U - 8192U)

/* The MCHBAR mirror's GFXVTBAR, and a unit's address. */
#define I915_XD_TO_TEST_GFXVTBAR	0x145400U
#define I915_XD_TO_TEST_VTD_BASE	0xfed90000ULL

/* A transcoder's data island control. */
#define I915_XD_FW_TEST_DIP_CTL(trans)	(0x60200U + 0x1000U * (trans))

/* The DDI function words: DP SST on DDI A, HDMI and DVI on DDI B, DP SST and HDMI on TC1, DP MST on DDI A. */
#define I915_XD_TO_TEST_FUNC_EDP	0x8a000000U
#define I915_XD_TO_TEST_FUNC_HDMI_B	0x90000000U
#define I915_XD_TO_TEST_FUNC_DVI_B	0x91000000U
#define I915_XD_TO_TEST_FUNC_DP_TC1	0xa2000000U
#define I915_XD_TO_TEST_FUNC_HDMI_TC1	0xa0000000U
#define I915_XD_TO_TEST_FUNC_MST_A	0x8b000000U
#define I915_XD_TO_TEST_FUNC_DDI_C	0x9a000000U

/*
 * The VT-d unit the environment's reader shows: its words, and whether it
 * can be mapped.
 */
struct i915_xd_to_test_vtd {
	uint32_t version;
	uint32_t status;
	uint32_t protect;
	int unmappable;
	unsigned reads;
};

/* The caller's steps the stop checks see. */
#define I915_XD_TO_TEST_BACKLIGHT	1U
#define I915_XD_TO_TEST_SINK_BEFORE	2U
#define I915_XD_TO_TEST_SINK_AFTER	3U
#define I915_XD_TO_TEST_PANEL		4U
#define I915_XD_TO_TEST_PORT		5U
#define I915_XD_TO_TEST_LINK		6U

/*
 * The caller's side of a stop: the hooks it saw in order, the register
 * writes made before each, and how many times the screen-off ran.
 */
struct i915_xd_to_test_run {
	unsigned steps[8];
	unsigned writes_before[8];
	unsigned count;
	unsigned screen_offs;
	int screen_refused;
	uint32_t frame;
};

/* The window the readout checks use. */
static struct i915_xd_io i915_xd_to_test_io;

/* The unit the environment's reader shows. */
static struct i915_xd_to_test_vtd i915_xd_to_test_unit;

/* The caller's side of the stop being checked. */
static struct i915_xd_to_test_run i915_xd_to_test_run;

static void i915_xd_to_test_base(struct i915_xd_firmware *firmware);
static int i915_xd_to_test_reason_has(const struct i915_xd_firmware *firmware, const char *word);
static int i915_xd_to_test_vtd_read(void *context, uint64_t base, uint32_t *version, uint32_t *status, uint32_t *protect);
static const struct i915_xd_platform *i915_xd_to_test_setup(void);
static void i915_xd_to_test_power_pipe(const struct i915_xd_platform *platform, unsigned pipe);
static void i915_xd_to_test_env(struct i915_xd_fw_env *env);
static void i915_xd_to_test_decisions(void);
static void i915_xd_to_test_readout(void);
static void i915_xd_to_test_classify(void);
static void i915_xd_to_test_range(void);
static void i915_xd_to_test_note(unsigned step);
static void i915_xd_to_test_backlight(void *context, unsigned pipe);
static void i915_xd_to_test_sink_before(void *context, unsigned pipe);
static void i915_xd_to_test_sink_after(void *context, unsigned pipe);
static void i915_xd_to_test_panel(void *context, unsigned pipe);
static void i915_xd_to_test_port(void *context, unsigned pipe);
static void i915_xd_to_test_link(void *context, unsigned pipe);
static void i915_xd_to_test_hooks(struct i915_xd_takeover_hooks *hooks);
static void i915_xd_to_test_buffer_rule(uint32_t offset, uint32_t value, void *context);
static uint32_t i915_xd_to_test_frame_rule(uint32_t offset, uint32_t value, void *context);
static int i915_xd_to_test_screen_off(void *context);
static void i915_xd_to_test_light(unsigned pipe, uint32_t function);
static unsigned i915_xd_to_test_step_at(unsigned step);
static void i915_xd_to_test_stop_panel(void);
static void i915_xd_to_test_stop_hdmi(void);
static void i915_xd_to_test_stop_shared(void);
static void i915_xd_to_test_vga(void);
static void i915_xd_to_test_sanitize(void);
static void i915_xd_to_test_sanitize_unknown(void);
static void i915_xd_to_test_pps_status(uint32_t offset, uint32_t value, void *context);
static void i915_xd_to_test_takeover(void);

/*
 * Runs every check of the firmware's display and reports.
 */
int
main(void)
{
	int failures;

	/* Each group of checks. */
	i915_xd_to_test_decisions();
	i915_xd_to_test_readout();
	i915_xd_to_test_classify();
	i915_xd_to_test_range();
	i915_xd_to_test_stop_panel();
	i915_xd_to_test_stop_hdmi();
	i915_xd_to_test_stop_shared();
	i915_xd_to_test_vga();
	i915_xd_to_test_sanitize();
	i915_xd_to_test_sanitize_unknown();
	i915_xd_to_test_takeover();

	/* Reports the result. */
	failures = i915_host_report("host_xd_takeover_test");
	if (failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Fills a record with an idle native start: pipe A dark, B..D off, the VT-d unit readable and idle. */
static void
i915_xd_to_test_base(
	struct i915_xd_firmware *firmware)
{
	/* Starts from an empty record. */
	memset(firmware, 0, sizeof(*firmware));
	firmware->output_ddi = -1;

	/* The GGTT the driver writes at its top. */
	firmware->ggtt_pages = I915_XD_TO_TEST_GGTT_PAGES;
	firmware->driver_first_page = I915_XD_TO_TEST_DRIVER_FIRST;

	/* Pipe A reads and is dark; B..D are off. */
	firmware->pipe[0].state = I915_XD_FW_PIPE_DARK;
	firmware->unread_pipes = 0xeU;

	/* The GPU's VT-d unit is on, reads and is idle. */
	firmware->vtd_enabled = 1;
	firmware->vtd_readable = 1;
	firmware->vtd_version = 0x10U;
}

/* Tells whether the decision's reason names a word. */
static int
i915_xd_to_test_reason_has(
	const struct i915_xd_firmware *firmware,
	const char *word)
{
	const char *found;

	/* A decision without a reason names nothing. */
	if (firmware->reason == NULL)
		return 0;

	/* Looks for the word. */
	found = strstr(firmware->reason, word);
	if (found == NULL)
		return 0;

	/* Succeeded: the reason names the word. */
	return 1;
}

/* The environment's reader of the VT-d unit: the test's words, or EIO for an unmappable unit. */
static int
i915_xd_to_test_vtd_read(
	void *context,
	uint64_t base,
	uint32_t *version,
	uint32_t *status,
	uint32_t *protect)
{
	struct i915_xd_to_test_vtd *unit;

	/* Counts the read; an unmappable unit gives nothing. */
	unit = context;
	unit->reads++;
	if (base != I915_XD_TO_TEST_VTD_BASE)
		return EIO;
	if (unit->unmappable)
		return EIO;

	/* The three words. */
	*version = unit->version;
	*status = unit->status;
	*protect = unit->protect;

	/* Succeeded: the words are read. */
	return 0;
}

/* Starts a fresh model and a window on it; returns the 5330's description. */
static const struct i915_xd_platform *
i915_xd_to_test_setup(void)
{
	struct i915_xd_io_hooks hooks;
	struct i915_xd_choice choice;
	int error;

	/* A fresh model and a window that reaches it. */
	i915_xd_fake_reset();
	i915_xd_fake_hooks(&hooks);
	drv_i915_xd_io_init(&i915_xd_to_test_io, NULL, NULL);
	drv_i915_xd_io_set_hooks(&i915_xd_to_test_io, &hooks);
	memset(&i915_xd_to_test_unit, 0, sizeof(i915_xd_to_test_unit));

	/* The description. */
	error = drv_i915_xd_platform_choose(I915_XD_TO_TEST_ADLP, I915_XD_TO_TEST_ADLP_REVID, 0, 0U, &choice);
	if (error != 0)
		return NULL;

	/* Succeeded: the description. */
	return choice.platform;
}

/* Sets the state bits of every well that powers a pipe and its transcoder. */
static void
i915_xd_to_test_power_pipe(
	const struct i915_xd_platform *platform,
	unsigned pipe)
{
	const struct i915_xd_well_desc *desc;
	uint64_t wells;
	uint32_t control;
	unsigned well;

	/* The wells of the pipe and its transcoder. */
	wells = drv_i915_xd_power_wells_of(platform, I915_XD_POWERS_PIPE(pipe) | I915_XD_POWERS_TRANS(pipe));

	/* Each one's state in its control. */
	for (well = 0U; well < platform->nwells; well++) {
		if ((wells & (1ULL << well)) == 0ULL)
			continue;

		desc = &platform->wells[well];
		control = 0U;
		if (desc->control == I915_XD_WELL_CONTROL_MAIN)
			control = I915_XD_PWR_WELL_CTL;
		if (desc->control == I915_XD_WELL_CONTROL_AUX)
			control = I915_XD_PWR_WELL_CTL_AUX;
		if (desc->control == I915_XD_WELL_CONTROL_DDI)
			control = I915_XD_PWR_WELL_CTL_DDI;
		if (control == 0U)
			continue;
		i915_xd_fake_set(control, i915_xd_fake_get(control) | I915_XD_PWR_WELL_STATE(desc->index));
	}
}

/* The 5330's environment: native, the firmware's framebuffer at the aperture's first page, the test's unit. */
static void
i915_xd_to_test_env(
	struct i915_xd_fw_env *env)
{
	/* The machine and the GGTT the driver writes. */
	memset(env, 0, sizeof(*env));
	env->hypervisor = 0;
	env->aperture_base = 0x4000000000ULL;
	env->aperture_size = 0x10000000ULL;
	env->ggtt_pages = I915_XD_TO_TEST_GGTT_PAGES;
	env->driver_first_page = I915_XD_TO_TEST_DRIVER_FIRST;

	/* The firmware's 1920x1080 framebuffer from the aperture's first page. */
	env->fb_present = 1;
	env->fb_base = 0x4000000000ULL;
	env->fb_size = 1920ULL * 1080ULL * 4ULL;

	/* The unit the test shows. */
	env->vtd_read = i915_xd_to_test_vtd_read;
	env->context = &i915_xd_to_test_unit;
}

/* The decision on each start situation, from the record alone. */
static void
i915_xd_to_test_decisions(void)
{
	struct i915_xd_firmware firmware;
	int named;

	/* An idle native start. */
	i915_xd_to_test_base(&firmware);
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use != 0 && firmware.take_over == 0, "idle native: pipe A dark, B..D off, the VT-d unit idle -> use");

	/* The 5330's start: the firmware lit pipe A from GGTT page 0. */
	i915_xd_to_test_base(&firmware);
	firmware.pipe[0].state = I915_XD_FW_PIPE_LIT;
	firmware.pipe[0].trans_conf = 0x80000000U;
	firmware.pipe[0].plane_ctl = 0x84000000U;
	firmware.pipe[0].plane_surface = 0U;
	firmware.fb_present = 1;
	firmware.fb_in_aperture = 1;
	firmware.fb_first_page = 0U;
	firmware.fb_pages = 2025U;
	drv_i915_xd_firmware_decide(&firmware);
	named = i915_xd_to_test_reason_has(&firmware, "lit");
	i915_host_check(firmware.use != 0 && firmware.take_over != 0 && named != 0 && (firmware.walls & I915_XD_FW_WALL_OVERLAP) == 0U,
			"firmware picture: use and take over first, the reason names the lit pipe, no GGTT overlap");

	/* A firmware scanout inside the driver's GGTT pages. */
	i915_xd_to_test_base(&firmware);
	firmware.fb_present = 1;
	firmware.fb_in_aperture = 1;
	firmware.fb_first_page = I915_XD_TO_TEST_DRIVER_FIRST - 10U;
	firmware.fb_pages = 2025U;
	drv_i915_xd_firmware_decide(&firmware);
	named = i915_xd_to_test_reason_has(&firmware, "overlap");
	i915_host_check(firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_OVERLAP && named != 0, "overlap: a scanout in the driver's GGTT pages -> stop");

	/* A lit pipe whose surface is in the driver's pages. */
	i915_xd_to_test_base(&firmware);
	firmware.pipe[1].state = I915_XD_FW_PIPE_LIT;
	firmware.pipe[1].plane_surface = (I915_XD_TO_TEST_DRIVER_FIRST + 4U) << 12;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_OVERLAP, "overlap: a lit pipe's surface in the driver's GGTT pages -> stop");

	/* The VT-d unit translates. */
	i915_xd_to_test_base(&firmware);
	firmware.vtd_status = 0x80000000U;
	drv_i915_xd_firmware_decide(&firmware);
	named = i915_xd_to_test_reason_has(&firmware, "VT-d");
	i915_host_check(firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_VTD_TRANSLATES && named != 0, "translation: the VT-d unit translates -> stop");

	/* A protected memory region. */
	i915_xd_to_test_base(&firmware);
	firmware.vtd_protect = 0x80000001U;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_VTD_PROTECTS, "protected memory: a region is enabled -> stop");

	/* A unit that is on but does not read. */
	i915_xd_to_test_base(&firmware);
	firmware.vtd_readable = 0;
	firmware.vtd_version = 0U;
	drv_i915_xd_firmware_decide(&firmware);
	named = i915_xd_to_test_reason_has(&firmware, "does not read");
	i915_host_check(firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_VTD_HIDDEN && named != 0, "hidden unit: on but unreadable -> stop");

	/* A guest reads nothing at the host unit's address. */
	i915_xd_to_test_base(&firmware);
	firmware.hypervisor = 1;
	firmware.vtd_readable = 0;
	firmware.vtd_version = 0U;
	drv_i915_xd_firmware_decide(&firmware);
	named = i915_xd_to_test_reason_has(&firmware, "guest");
	i915_host_check(firmware.use != 0 && named != 0 && firmware.walls == 0U, "guest: the host owns the unit -> use");

	/* No unit. */
	i915_xd_to_test_base(&firmware);
	firmware.vtd_enabled = 0;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use != 0, "no unit: DMA is not translated -> use");

	/* The 5330's real picture: lit, an OpRegion, the root table pointer status alone. */
	i915_xd_to_test_base(&firmware);
	firmware.opregion = 0x614e5018U;
	firmware.pipe[0].state = I915_XD_FW_PIPE_LIT;
	firmware.vtd_status = 0x40000000U;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use != 0 && firmware.take_over != 0 && firmware.stop == 0U && firmware.walls == (I915_XD_FW_WALL_LIT | I915_XD_FW_WALL_OPREGION),
			"5330: a lit pipe is taken over, not a stop; the OpRegion is recorded; the root table pointer alone is no wall");

	/* A lit pipe and translation together. */
	i915_xd_to_test_base(&firmware);
	firmware.pipe[0].state = I915_XD_FW_PIPE_LIT;
	firmware.vtd_status = 0x80000000U;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use == 0 && firmware.take_over == 0 && firmware.stop == I915_XD_FW_WALL_VTD_TRANSLATES && (firmware.walls & I915_XD_FW_WALL_LIT) != 0U,
			"two walls: the translation stops, the lit pipe is recorded too");

	/* Interrupt remapping alone. */
	i915_xd_to_test_base(&firmware);
	firmware.vtd_status = 0x02000000U;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use != 0 && (firmware.walls & I915_XD_FW_WALL_VTD_REMAPS) != 0U && firmware.stop == 0U, "remapping: recorded for the MSI path, no stop");

	/* A garbled pipe. */
	i915_xd_to_test_base(&firmware);
	firmware.pipe[1].state = I915_XD_FW_PIPE_GARBLED;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_GARBLED, "garbled: a pipe that did not read is not taken as dark -> stop");

	/* A pipe whose wells are off. */
	i915_xd_to_test_base(&firmware);
	firmware.pipe[1].state = I915_XD_FW_PIPE_OFF;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use != 0 && firmware.walls == 0U, "off: a pipe whose wells are off is dark -> no wall");

	/* A device without a description. */
	i915_xd_to_test_base(&firmware);
	firmware.walls = I915_XD_FW_WALL_UNDESCRIBED;
	drv_i915_xd_firmware_decide(&firmware);
	i915_host_check(firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_UNDESCRIBED, "undescribed: a device without a description -> keep the firmware's picture");
}

/* The readout of an Alder Lake-P with the firmware's picture on pipe A: what it reads, and that it writes nothing. */
static void
i915_xd_to_test_readout(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_firmware firmware;
	struct i915_xd_fw_env env;
	int error;

	/* The 5330's firmware picture: pipe A's wells on, the transcoder, DDI A and the plane enabled. */
	platform = i915_xd_to_test_setup();
	i915_host_check(platform != NULL, "readout: the 5330's description");
	if (platform == NULL)
		return;
	i915_xd_to_test_power_pipe(platform, 0U);
	i915_xd_fake_set(I915_XD_TRANS_CONF(0U), 0xc0000000U);
	i915_xd_fake_set(I915_XD_TRANS_DDI_FUNC_CTL(0U), I915_XD_TO_TEST_FUNC_EDP);
	i915_xd_fake_set(I915_XD_PIPE_SRCSZ(0U), 0x077f0437U);
	i915_xd_fake_set(I915_XD_PLANE_CTL(0U), 0x84000000U);
	i915_xd_fake_set(I915_XD_PLANE_SURF(0U), 0U);
	i915_xd_fake_set(I915_XD_PP_STATUS(0U), 0x80000008U);
	i915_xd_fake_set(I915_XD_SBLC_PWM_CTL(0U), 0x80000000U);
	i915_xd_fake_set(I915_XD_TO_TEST_GFXVTBAR, (uint32_t)I915_XD_TO_TEST_VTD_BASE | 1U);
	i915_xd_to_test_unit.version = 0x10U;
	i915_xd_to_test_unit.status = 0x40000000U;
	i915_xd_to_test_env(&env);

	/* Reads and decides. */
	error = drv_i915_xd_firmware_read(&i915_xd_to_test_io, platform, &env, 0x614e5018U, &firmware);
	i915_host_check(error == 0 && i915_xd_fake_writes() == 0U, "readout: reads the 5330's firmware display and writes nothing");
	i915_host_check(firmware.pipe[0].state == I915_XD_FW_PIPE_LIT && firmware.lit_pipes == 1U && firmware.unread_pipes == 0xeU,
			"readout: pipe A lit, B..D off (their wells are off)");
	i915_host_check(firmware.pipe[0].source_size == 0x077f0437U && firmware.pipe[0].ddi_function == I915_XD_TO_TEST_FUNC_EDP,
			"readout: pipe A's source size and DDI function");
	i915_host_check(firmware.panel_status == 0x80000008U && firmware.pwm_control == 0x80000000U, "readout: pipe A's domain: the panel on, the PWM enabled");
	i915_host_check(firmware.vtd_enabled && firmware.vtd_readable && i915_xd_to_test_unit.reads == 1U && firmware.vtd_status == 0x40000000U,
			"readout: the VT-d unit through the MCHBAR mirror and the environment's reader");
	i915_host_check(firmware.use != 0 && firmware.take_over != 0 && firmware.fb_in_aperture && firmware.fb_first_page == 0U && firmware.fb_pages == 2025U,
			"readout: use and take over; the framebuffer takes GGTT pages 0..2025");

	/* A main well control that reads all ones garbles every pipe. */
	platform = i915_xd_to_test_setup();
	i915_xd_fake_set(I915_XD_PWR_WELL_CTL, 0xffffffffU);
	i915_xd_to_test_env(&env);
	(void)drv_i915_xd_firmware_read(&i915_xd_to_test_io, platform, &env, 0U, &firmware);
	i915_host_check(firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_GARBLED && firmware.unread_pipes == 0xfU, "readout: all ones in the well control -> garbled, stop");

	/* A transcoder of all ones on a powered pipe. */
	platform = i915_xd_to_test_setup();
	i915_xd_to_test_power_pipe(platform, 0U);
	i915_xd_fake_set(I915_XD_TRANS_CONF(0U), 0xffffffffU);
	i915_xd_to_test_env(&env);
	(void)drv_i915_xd_firmware_read(&i915_xd_to_test_io, platform, &env, 0U, &firmware);
	i915_host_check(firmware.pipe[0].state == I915_XD_FW_PIPE_GARBLED && firmware.use == 0, "readout: a transcoder of all ones is garbled");

	/* An unmappable unit and a unit that reads zeros. */
	platform = i915_xd_to_test_setup();
	i915_xd_fake_set(I915_XD_TO_TEST_GFXVTBAR, (uint32_t)I915_XD_TO_TEST_VTD_BASE | 1U);
	i915_xd_to_test_unit.unmappable = 1;
	i915_xd_to_test_env(&env);
	(void)drv_i915_xd_firmware_read(&i915_xd_to_test_io, platform, &env, 0U, &firmware);
	i915_host_check(firmware.vtd_readable == 0 && firmware.stop == I915_XD_FW_WALL_VTD_HIDDEN, "readout: an unmappable unit is hidden -> stop");

	/* A device without a description: the framebuffer and the unit are read, the display is not. */
	platform = i915_xd_to_test_setup();
	i915_xd_to_test_env(&env);
	error = drv_i915_xd_firmware_read(&i915_xd_to_test_io, NULL, &env, 0U, &firmware);
	i915_host_check(error == ENODEV && firmware.use == 0 && firmware.stop == I915_XD_FW_WALL_UNDESCRIBED && firmware.fb_present, "readout: no description -> ENODEV, stop");
	(void)platform;
}

/* The output's kind by the ports of the 5330's description. */
static void
i915_xd_to_test_classify(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_firmware firmware;

	platform = i915_xd_to_test_setup();
	if (platform == NULL)
		return;

	/* Nothing lit. */
	i915_xd_to_test_base(&firmware);
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_NONE && firmware.output_ddi == -1, "classify: nothing lit -> none");

	/* DP SST on DDI A, the panel's. */
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 1U;
	firmware.pipe[0].ddi_function = I915_XD_TO_TEST_FUNC_EDP;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_PANEL && firmware.output_pipe == 0U && firmware.output_ddi == 0, "classify: DP SST on DDI A -> the panel, pipe A");

	/* HDMI and DVI on DDI B, pipe B. */
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 2U;
	firmware.pipe[1].ddi_function = I915_XD_TO_TEST_FUNC_HDMI_B;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_HDMI && firmware.output_pipe == 1U && firmware.output_ddi == 1, "classify: HDMI on DDI B -> HDMI, pipe B");
	firmware.pipe[1].ddi_function = I915_XD_TO_TEST_FUNC_DVI_B;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_HDMI && firmware.output_mode == I915_XD_DDI_FUNC_MODE_DVI, "classify: DVI on DDI B -> HDMI");

	/* DP SST on TC1, and HDMI on TC1. */
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 1U;
	firmware.pipe[0].ddi_function = I915_XD_TO_TEST_FUNC_DP_TC1;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_DP_TC && firmware.output_ddi == 3, "classify: DP SST on TC1 -> Type-C DP");
	firmware.pipe[0].ddi_function = I915_XD_TO_TEST_FUNC_HDMI_TC1;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_OTHER, "classify: HDMI on TC1 -> other");

	/* DP MST on DDI A, DDI C (not on Alder Lake-P), DP SST on DDI B (not the panel's). */
	firmware.pipe[0].ddi_function = I915_XD_TO_TEST_FUNC_MST_A;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_OTHER, "classify: DP MST on DDI A -> other");
	firmware.pipe[0].ddi_function = I915_XD_TO_TEST_FUNC_DDI_C;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_OTHER && firmware.output_ddi == 2, "classify: DDI C, which Alder Lake-P does not have -> other");
	firmware.pipe[0].ddi_function = 0x92000000U;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_DP_COMBO, "classify: DP SST on DDI B, not the panel's -> combo DP");

	/* A lit pipe with its DDI function off is passed over for the next. */
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 3U;
	firmware.pipe[0].ddi_function = 0U;
	firmware.pipe[1].ddi_function = I915_XD_TO_TEST_FUNC_HDMI_B;
	drv_i915_xd_firmware_classify(platform, 0, &firmware);
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_HDMI && firmware.output_pipe == 1U, "classify: a lit pipe without a DDI is passed over");
}

/* The framebuffer's GGTT range inside and outside the aperture. */
static void
i915_xd_to_test_range(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_firmware firmware;
	struct i915_xd_fw_env env;

	/* A framebuffer in the middle of the aperture, one byte over a page. */
	platform = i915_xd_to_test_setup();
	i915_xd_to_test_env(&env);
	env.fb_base = env.aperture_base + 0x100000ULL;
	env.fb_size = 4097U;
	(void)drv_i915_xd_firmware_read(&i915_xd_to_test_io, platform, &env, 0U, &firmware);
	i915_host_check(firmware.fb_in_aperture && firmware.fb_first_page == 256U && firmware.fb_pages == 2U, "range: pages 256..258 for 4097 bytes at 1 MiB");

	/* One that runs past the aperture's end takes no GGTT pages. */
	platform = i915_xd_to_test_setup();
	i915_xd_to_test_env(&env);
	env.fb_base = env.aperture_base + env.aperture_size - 4096U;
	env.fb_size = 8192U;
	(void)drv_i915_xd_firmware_read(&i915_xd_to_test_io, platform, &env, 0U, &firmware);
	i915_host_check(firmware.fb_present && !firmware.fb_in_aperture && firmware.use != 0, "range: a framebuffer past the aperture's end is outside it");

	/* No framebuffer. */
	platform = i915_xd_to_test_setup();
	i915_xd_to_test_env(&env);
	env.fb_present = 0;
	(void)drv_i915_xd_firmware_read(&i915_xd_to_test_io, platform, &env, 0U, &firmware);
	i915_host_check(!firmware.fb_present && !firmware.fb_in_aperture, "range: no framebuffer");
}

/* Notes one of the caller's steps, with the writes made so far. */
static void
i915_xd_to_test_note(
	unsigned step)
{
	/* A full list keeps its first steps. */
	if (i915_xd_to_test_run.count == 8U)
		return;

	/* The step. */
	i915_xd_to_test_run.steps[i915_xd_to_test_run.count] = step;
	i915_xd_to_test_run.writes_before[i915_xd_to_test_run.count] = i915_xd_fake_writes();
	i915_xd_to_test_run.count++;
}

/* The panel's backlight off. */
static void
i915_xd_to_test_backlight(
	void *context,
	unsigned pipe)
{
	/* Notes the step. */
	(void)context;
	(void)pipe;
	i915_xd_to_test_note(I915_XD_TO_TEST_BACKLIGHT);
}

/* The HDMI sink before the transcoder stops. */
static void
i915_xd_to_test_sink_before(
	void *context,
	unsigned pipe)
{
	/* Notes the step. */
	(void)context;
	(void)pipe;
	i915_xd_to_test_note(I915_XD_TO_TEST_SINK_BEFORE);
}

/* The DisplayPort sink after the transcoder stopped. */
static void
i915_xd_to_test_sink_after(
	void *context,
	unsigned pipe)
{
	/* Notes the step. */
	(void)context;
	(void)pipe;
	i915_xd_to_test_note(I915_XD_TO_TEST_SINK_AFTER);
}

/* The panel's power off. */
static void
i915_xd_to_test_panel(
	void *context,
	unsigned pipe)
{
	/* Notes the step. */
	(void)context;
	(void)pipe;
	i915_xd_to_test_note(I915_XD_TO_TEST_PANEL);
}

/* The port's references. */
static void
i915_xd_to_test_port(
	void *context,
	unsigned pipe)
{
	/* Notes the step. */
	(void)context;
	(void)pipe;
	i915_xd_to_test_note(I915_XD_TO_TEST_PORT);
}

/* The link's references. */
static void
i915_xd_to_test_link(
	void *context,
	unsigned pipe)
{
	/* Notes the step. */
	(void)context;
	(void)pipe;
	i915_xd_to_test_note(I915_XD_TO_TEST_LINK);
}

/* Fills the caller's hooks. */
static void
i915_xd_to_test_hooks(
	struct i915_xd_takeover_hooks *hooks)
{
	/* Every hook notes its step. */
	memset(hooks, 0, sizeof(*hooks));
	hooks->backlight_off = i915_xd_to_test_backlight;
	hooks->sink_before = i915_xd_to_test_sink_before;
	hooks->sink_after = i915_xd_to_test_sink_after;
	hooks->panel_off = i915_xd_to_test_panel;
	hooks->port_release = i915_xd_to_test_port;
	hooks->link_release = i915_xd_to_test_link;
	hooks->context = NULL;
}

/* A DDI buffer goes idle when its enable is cleared, and leaves idle when it is set. */
static void
i915_xd_to_test_buffer_rule(
	uint32_t offset,
	uint32_t value,
	void *context)
{
	/* The idle bit follows the enable. */
	(void)context;
	if ((value & I915_XD_DDI_BUF_ENABLE) == 0U) {
		i915_xd_fake_set(offset, value | I915_XD_DDI_BUF_IDLE);
	} else {
		i915_xd_fake_set(offset, value & ~I915_XD_DDI_BUF_IDLE);
	}
}

/* A frame counter that moves at each read. */
static uint32_t
i915_xd_to_test_frame_rule(
	uint32_t offset,
	uint32_t value,
	void *context)
{
	/* One frame per read. */
	(void)offset;
	(void)value;
	(void)context;
	i915_xd_to_test_run.frame++;

	/* Succeeded: the count. */
	return i915_xd_to_test_run.frame;
}

/* The legacy screen off: counted, or refused when the run asks. */
static int
i915_xd_to_test_screen_off(
	void *context)
{
	/* Counts the call. */
	(void)context;
	i915_xd_to_test_run.screen_offs++;
	if (i915_xd_to_test_run.screen_refused)
		return EIO;

	/* Succeeded: the screen is off. */
	return 0;
}

/* Lights a pipe on the model: its transcoder, function, plane and the rules a stop meets. */
static void
i915_xd_to_test_light(
	unsigned pipe,
	uint32_t function)
{
	uint32_t ddi;

	/* The transcoder, whose state follows its enable, the function and the plane. */
	i915_xd_fake_rule_mirror(I915_XD_TRANS_CONF(pipe), I915_XD_TRANS_CONF_ENABLE, I915_XD_TRANS_CONF(pipe), -1, 0U);
	i915_xd_fake_set(I915_XD_TRANS_CONF(pipe), I915_XD_TRANS_CONF_ENABLE | I915_XD_TRANS_CONF_STATE);
	i915_xd_fake_set(I915_XD_TRANS_DDI_FUNC_CTL(pipe), function);
	i915_xd_fake_set(I915_XD_PLANE_CTL(pipe), I915_XD_PLANE_CTL_ENABLE);
	i915_xd_fake_rule_read(I915_XD_PIPE_FRMCNT(pipe), i915_xd_to_test_frame_rule, NULL);

	/* The DDI buffer on, going idle when it is turned off. */
	ddi = ((function & I915_XD_DDI_FUNC_SELECT) >> 27) - 1U;
	i915_xd_fake_set(I915_XD_DDI_BUF_CTL(ddi), I915_XD_DDI_BUF_ENABLE);
	i915_xd_fake_rule_call(I915_XD_DDI_BUF_CTL(ddi), i915_xd_to_test_buffer_rule, NULL);

	/* DPLL0 powered and locked, its state following its requests. */
	i915_xd_fake_rule_mirror(I915_XD_DPLL0_ENABLE, I915_XD_PLL_POWER_ENABLE, I915_XD_DPLL0_ENABLE, -1, 0U);
	i915_xd_fake_rule_mirror(I915_XD_DPLL0_ENABLE, I915_XD_PLL_ENABLE, I915_XD_DPLL0_ENABLE, -1, 0U);
	i915_xd_fake_set(I915_XD_DPLL0_ENABLE, I915_XD_PLL_ENABLE | I915_XD_PLL_LOCK | I915_XD_PLL_POWER_ENABLE | I915_XD_PLL_POWER_STATE);
}

/* Returns the index of a step in the run, or 8 when it was not seen. */
static unsigned
i915_xd_to_test_step_at(
	unsigned step)
{
	unsigned index;

	/* The first time the step was seen. */
	for (index = 0U; index < i915_xd_to_test_run.count; index++) {
		if (i915_xd_to_test_run.steps[index] == step)
			return index;
	}

	/* Succeeded: not seen. */
	return 8U;
}

/* The 5330's panel on pipe A: the order, the hooks between the writes, the PLL off. */
static void
i915_xd_to_test_stop_panel(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_takeover_report report;
	struct i915_xd_takeover_hooks hooks;
	struct i915_xd_firmware firmware;
	int error;

	/* The firmware's panel on pipe A, DDI A, DPLL0. */
	platform = i915_xd_to_test_setup();
	if (platform == NULL)
		return;
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	i915_xd_to_test_light(0U, I915_XD_TO_TEST_FUNC_EDP);
	i915_xd_fake_set(I915_XD_FW_TEST_DIP_CTL(0U), 0x11111111U);
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 1U;
	firmware.pipe[0].state = I915_XD_FW_PIPE_LIT;
	firmware.panel_ddi = 0;
	i915_xd_to_test_hooks(&hooks);

	/* Stops it. */
	error = drv_i915_xd_takeover_stop(&i915_xd_to_test_io, platform, NULL, &firmware, 0U, &hooks, &report);
	i915_host_check(error == 0 && report.ddi == 0 && report.mode == I915_XD_DDI_FUNC_MODE_DP_SST && report.pll == 0 && report.late == 0U,
			"stop panel: DDI A in DP SST, DPLL0 off, no wait ran out");
	i915_host_check(i915_xd_fake_get(I915_XD_PLANE_CTL(0U)) == 0U && i915_xd_fake_get(I915_XD_TRANS_CONF(0U)) == 0U,
			"stop panel: the plane and the transcoder off");
	i915_host_check((i915_xd_fake_get(I915_XD_TRANS_DDI_FUNC_CTL(0U)) & I915_XD_DDI_FUNC_ENABLE) == 0U && i915_xd_fake_get(I915_XD_FW_TEST_DIP_CTL(0U)) == 0x00000000U,
			"stop panel: the DDI function off, every data island off (the picture parameters too)");
	i915_host_check((i915_xd_fake_get(I915_XD_DDI_BUF_CTL(0U)) & I915_XD_DDI_BUF_ENABLE) == 0U && (i915_xd_fake_get(I915_XD_DPCLKA_CFGCR0) & I915_XD_DPCLKA_DDI_OFF_A) != 0U,
			"stop panel: the DDI buffer off and DDI A's clock gated");
	i915_host_check((i915_xd_fake_get(I915_XD_DPLL0_ENABLE) & (I915_XD_PLL_ENABLE | I915_XD_PLL_POWER_ENABLE)) == 0U, "stop panel: DPLL0 off and unpowered");
	i915_host_check(i915_xd_to_test_run.count == 5U &&
			i915_xd_to_test_run.steps[0] == I915_XD_TO_TEST_BACKLIGHT &&
			i915_xd_to_test_run.steps[1] == I915_XD_TO_TEST_SINK_AFTER &&
			i915_xd_to_test_run.steps[2] == I915_XD_TO_TEST_PANEL &&
			i915_xd_to_test_run.steps[3] == I915_XD_TO_TEST_PORT &&
			i915_xd_to_test_run.steps[4] == I915_XD_TO_TEST_LINK,
			"stop panel: the hooks in order: backlight, sink, panel, port, link (no HDMI sink)");
	i915_host_check(i915_xd_to_test_run.writes_before[0] == 3U && i915_xd_to_test_run.writes_before[1] > i915_xd_to_test_run.writes_before[0] + 8U,
			"stop panel: the backlight after the plane's three writes, the sink after the transcoder, function, scalers and islands");
	i915_host_check(firmware.lit_pipes == 0U && firmware.pipe[0].state == I915_XD_FW_PIPE_DARK, "stop panel: the record says pipe A is dark");

	/* A pipe that is not lit is refused. */
	error = drv_i915_xd_takeover_stop(&i915_xd_to_test_io, platform, NULL, &firmware, 0U, &hooks, &report);
	i915_host_check(error == EINVAL, "stop panel: a dark pipe is refused");
}

/* HDMI on DDI B, pipe B: the sink before, no panel steps, the picture parameters kept. */
static void
i915_xd_to_test_stop_hdmi(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_takeover_report report;
	struct i915_xd_takeover_hooks hooks;
	struct i915_xd_firmware firmware;
	int error;

	/* HDMI on DDI B. */
	platform = i915_xd_to_test_setup();
	if (platform == NULL)
		return;
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	i915_xd_to_test_light(1U, I915_XD_TO_TEST_FUNC_HDMI_B);
	i915_xd_fake_set(I915_XD_FW_TEST_DIP_CTL(1U), 0x11111111U);
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 2U;
	firmware.pipe[1].state = I915_XD_FW_PIPE_LIT;
	firmware.panel_ddi = 0;
	i915_xd_to_test_hooks(&hooks);

	/* Stops it. */
	error = drv_i915_xd_takeover_stop(&i915_xd_to_test_io, platform, NULL, &firmware, 1U, &hooks, &report);
	i915_host_check(error == 0 && report.ddi == 1 && report.mode == I915_XD_DDI_FUNC_MODE_HDMI && report.pll == 0, "stop HDMI: DDI B, DPLL0 off");
	i915_host_check(i915_xd_to_test_run.count == 3U &&
			i915_xd_to_test_step_at(I915_XD_TO_TEST_SINK_BEFORE) == 0U &&
			i915_xd_to_test_step_at(I915_XD_TO_TEST_BACKLIGHT) == 8U &&
			i915_xd_to_test_step_at(I915_XD_TO_TEST_PANEL) == 8U &&
			i915_xd_to_test_step_at(I915_XD_TO_TEST_SINK_AFTER) == 8U,
			"stop HDMI: the sink before the transcoder, no backlight, panel or DisplayPort sink");
	i915_host_check(i915_xd_fake_get(I915_XD_FW_TEST_DIP_CTL(1U)) == 0x01000000U && (i915_xd_fake_get(I915_XD_DPCLKA_CFGCR0) & I915_XD_DPCLKA_DDI_OFF_B) != 0U,
			"stop HDMI: the infoframes off (the picture parameters left), DDI B's clock gated");
}

/* Two lit pipes on DPLL0: the first stop keeps the PLL, the second turns it off. */
static void
i915_xd_to_test_stop_shared(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_takeover_report report;
	struct i915_xd_takeover_hooks hooks;
	struct i915_xd_firmware firmware;
	uint32_t pll;

	/* The panel on pipe A and HDMI on pipe B, both on DPLL0. */
	platform = i915_xd_to_test_setup();
	if (platform == NULL)
		return;
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	i915_xd_to_test_light(0U, I915_XD_TO_TEST_FUNC_EDP);
	i915_xd_to_test_light(1U, I915_XD_TO_TEST_FUNC_HDMI_B);
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 3U;
	firmware.pipe[0].state = I915_XD_FW_PIPE_LIT;
	firmware.pipe[1].state = I915_XD_FW_PIPE_LIT;
	firmware.panel_ddi = 0;
	i915_xd_to_test_hooks(&hooks);

	/* Pipe A first: DPLL0 stays. */
	(void)drv_i915_xd_takeover_stop(&i915_xd_to_test_io, platform, NULL, &firmware, 0U, &hooks, &report);
	pll = i915_xd_fake_get(I915_XD_DPLL0_ENABLE);
	i915_host_check(report.pll == -1 && (pll & I915_XD_PLL_ENABLE) != 0U, "stop shared: pipe B still runs on DPLL0, so it stays on");

	/* Then pipe B: DPLL0 goes. */
	(void)drv_i915_xd_takeover_stop(&i915_xd_to_test_io, platform, NULL, &firmware, 1U, &hooks, &report);
	pll = i915_xd_fake_get(I915_XD_DPLL0_ENABLE);
	i915_host_check(report.pll == 0 && (pll & I915_XD_PLL_ENABLE) == 0U && firmware.lit_pipes == 0U, "stop shared: the last user turns DPLL0 off");
}

/* The VGA plane: off already, turned off, and the legacy I/O refused. */
static void
i915_xd_to_test_vga(void)
{
	unsigned writes;
	int result;

	/* Off already: nothing written, the sequencer not touched. */
	(void)i915_xd_to_test_setup();
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	i915_xd_fake_set(I915_XD_VGA_CONTROL, I915_XD_VGA_CONTROL_DISABLE);
	result = drv_i915_xd_vga_off(&i915_xd_to_test_io, i915_xd_to_test_screen_off, NULL);
	i915_host_check(result == 1 && i915_xd_fake_writes() == 0U && i915_xd_to_test_run.screen_offs == 0U, "vga: off already -> 1, nothing written");

	/* On: the screen off, then the plane's disable. */
	(void)i915_xd_to_test_setup();
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	result = drv_i915_xd_vga_off(&i915_xd_to_test_io, i915_xd_to_test_screen_off, NULL);
	writes = i915_xd_fake_writes();
	i915_host_check(result == 0 && i915_xd_to_test_run.screen_offs == 1U && writes == 1U && i915_xd_fake_get(I915_XD_VGA_CONTROL) == I915_XD_VGA_CONTROL_DISABLE,
			"vga: the screen off through the sequencer, then the plane disabled");

	/* The legacy I/O refused: the plane is left. */
	(void)i915_xd_to_test_setup();
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	i915_xd_to_test_run.screen_refused = 1;
	result = drv_i915_xd_vga_off(&i915_xd_to_test_io, i915_xd_to_test_screen_off, NULL);
	i915_host_check(result == EIO && i915_xd_fake_writes() == 0U, "vga: the legacy I/O refused -> EIO, the plane left");
}

/*
 * The sanitize of a start with the panel lit on pipe A: every other
 * port's clock gated (the model starts with every clock running), DPLL1
 * off, DPLL0 kept, the compressor off, the lit pipe left.
 */
static void
i915_xd_to_test_sanitize(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_sanitize_report report;
	struct i915_xd_firmware firmware;
	unsigned others;
	unsigned index;

	/* The panel on pipe A, DDI A mapped to DPLL0; DPLL1 left on; the compressor on. */
	platform = i915_xd_to_test_setup();
	if (platform == NULL)
		return;
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	i915_xd_to_test_light(0U, I915_XD_TO_TEST_FUNC_EDP);
	i915_xd_fake_rule_mirror(I915_XD_DPLL1_ENABLE, I915_XD_PLL_POWER_ENABLE, I915_XD_DPLL1_ENABLE, -1, 0U);
	i915_xd_fake_rule_mirror(I915_XD_DPLL1_ENABLE, I915_XD_PLL_ENABLE, I915_XD_DPLL1_ENABLE, -1, 0U);
	i915_xd_fake_set(I915_XD_DPLL1_ENABLE, I915_XD_PLL_ENABLE | I915_XD_PLL_LOCK | I915_XD_PLL_POWER_ENABLE | I915_XD_PLL_POWER_STATE);
	i915_xd_fake_set(I915_XD_DPFC_CONTROL_A, I915_XD_DPFC_CONTROL_ENABLE | 0x10U);
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 1U;
	firmware.pipe[0].state = I915_XD_FW_PIPE_LIT;

	/* Sanitizes. */
	drv_i915_xd_firmware_sanitize(&i915_xd_to_test_io, platform, NULL, &firmware, &report);
	others = 0U;
	for (index = 0U; index < platform->nports; index++) {
		if (platform->ports[index].ddi != 0U)
			others |= 1U << platform->ports[index].ddi;
	}
	i915_host_check(report.clocks_gated == others && (i915_xd_fake_get(I915_XD_DPCLKA_CFGCR0) & I915_XD_DPCLKA_DDI_OFF_A) == 0U &&
			(i915_xd_fake_get(I915_XD_DPCLKA_CFGCR0) & I915_XD_DPCLKA_DDI_OFF_B) != 0U && (i915_xd_fake_get(I915_XD_DPCLKA_CFGCR0) & I915_XD_DPCLKA_TC_OFF(0U)) != 0U,
			"sanitize: every port but the lit DDI A has its clock gated");
	i915_host_check(report.plls_off == 2U && report.plls_kept == 0 && (i915_xd_fake_get(I915_XD_DPLL1_ENABLE) & I915_XD_PLL_ENABLE) == 0U &&
			(i915_xd_fake_get(I915_XD_DPLL0_ENABLE) & I915_XD_PLL_ENABLE) != 0U,
			"sanitize: DPLL1 off, the lit pipe's DPLL0 kept");
	i915_host_check(report.fbc_off == 1 && i915_xd_fake_get(I915_XD_DPFC_CONTROL_A) == 0x10U, "sanitize: the compressor off, its other bits kept");
	i915_host_check(report.dmc_pipes == 0U && firmware.lit_pipes == 1U && (i915_xd_fake_get(I915_XD_TRANS_CONF(0U)) & I915_XD_TRANS_CONF_ENABLE) != 0U,
			"sanitize: no DMC loaded, the lit pipe left running");

	/* A second sanitize finds nothing to do. */
	drv_i915_xd_firmware_sanitize(&i915_xd_to_test_io, platform, NULL, &firmware, &report);
	i915_host_check(report.clocks_gated == 0U && report.plls_off == 0U && report.fbc_off == 0, "sanitize: again, nothing to change");
}

/* A lit Type-C port whose PLL select is unknown: no PLL is turned off. */
static void
i915_xd_to_test_sanitize_unknown(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_sanitize_report report;
	struct i915_xd_firmware firmware;

	/* DP on TC1 with no clock select; DPLL0 on. */
	platform = i915_xd_to_test_setup();
	if (platform == NULL)
		return;
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	i915_xd_to_test_light(0U, I915_XD_TO_TEST_FUNC_DP_TC1);
	i915_xd_to_test_base(&firmware);
	firmware.lit_pipes = 1U;
	firmware.pipe[0].state = I915_XD_FW_PIPE_LIT;

	/* Sanitizes. */
	drv_i915_xd_firmware_sanitize(&i915_xd_to_test_io, platform, NULL, &firmware, &report);
	i915_host_check(report.plls_kept == 1 && report.plls_off == 0U && (i915_xd_fake_get(I915_XD_DPLL0_ENABLE) & I915_XD_PLL_ENABLE) != 0U,
			"sanitize unknown: a lit port's PLL unknown, every PLL kept");
	i915_host_check((report.clocks_gated & (1U << 3U)) == 0U && (report.clocks_gated & 1U) != 0U,
			"sanitize unknown: TC1's clock kept, DDI A's gated");
}

/* The sequencer's status follows its power request at once. */
static void
i915_xd_to_test_pps_status(
	uint32_t offset,
	uint32_t value,
	void *context)
{
	/* On and idle with the power, off and idle without. */
	(void)offset;
	(void)context;
	if ((value & I915_XD_PP_CONTROL_POWER_ON) != 0U) {
		i915_xd_fake_set(I915_XD_PP_STATUS(0U), I915_XD_PP_STATUS_ON | I915_XD_PP_STATUS_STATE_ON_IDLE);
	} else {
		i915_xd_fake_set(I915_XD_PP_STATUS(0U), I915_XD_PP_STATUS_STATE_OFF_IDLE);
	}
}

/*
 * The takeover of the 5330's lit panel through the display: the
 * backlight off (the sequencer's enable, then the PWM at duty 0), the
 * sink to D3, the panel's power off, the pipe dark.
 */
static void
i915_xd_to_test_takeover(void)
{
	static struct i915_xd xd;
	static struct i915_xd_fake_aux sink;
	struct i915_xd_takeover_outcome outcome;
	struct i915_xd_io_hooks hooks;
	int error;

	/* The display: a window on the model and the 5330's description; no VBT, no DMC. */
	i915_xd_fake_reset();
	memset(&xd, 0, sizeof(xd));
	i915_xd_fake_hooks(&hooks);
	drv_i915_xd_io_init(&xd.io, NULL, NULL);
	drv_i915_xd_io_set_hooks(&xd.io, &hooks);
	error = drv_i915_xd_platform_choose(I915_XD_TO_TEST_ADLP, I915_XD_TO_TEST_ADLP_REVID, 0, 0U, &xd.choice);
	i915_host_check(error == 0, "takeover: the 5330's description");
	if (error != 0)
		return;
	xd.chosen = 1;

	/* The panel lit on pipe A: powered with its backlight, the PWM running, the sink behind AUX A. */
	memset(&i915_xd_to_test_run, 0, sizeof(i915_xd_to_test_run));
	i915_xd_to_test_light(0U, I915_XD_TO_TEST_FUNC_EDP);
	i915_xd_fake_set(I915_XD_PP_CONTROL(0U), I915_XD_PP_CONTROL_UNLOCK | I915_XD_PP_CONTROL_POWER_ON | I915_XD_PP_CONTROL_RESET | I915_XD_PP_CONTROL_BACKLIGHT);
	i915_xd_fake_set(I915_XD_PP_STATUS(0U), I915_XD_PP_STATUS_ON | I915_XD_PP_STATUS_STATE_ON_IDLE);
	i915_xd_fake_rule_call(I915_XD_PP_CONTROL(0U), i915_xd_to_test_pps_status, NULL);
	i915_xd_fake_set(I915_XD_SBLC_PWM_CTL(0U), I915_XD_SBLC_PWM_ENABLE);
	i915_xd_fake_set(I915_XD_SBLC_PWM_FREQ(0U), 96000U);
	i915_xd_fake_set(I915_XD_SBLC_PWM_DUTY(0U), 48000U);
	memset(&sink, 0, sizeof(sink));
	sink.dpcd[I915_XD_DPCD_SET_POWER] = I915_XD_DPCD_POWER_D0;
	i915_xd_fake_aux_attach(&sink, 0U);
	i915_xd_to_test_base(&xd.firmware);
	xd.firmware.lit_pipes = 1U;
	xd.firmware.pipe[0].state = I915_XD_FW_PIPE_LIT;
	xd.firmware.pipe[0].ddi_function = I915_XD_TO_TEST_FUNC_EDP;
	xd.firmware.panel_ddi = 0;

	/* Takes it over. */
	error = drv_i915_xd_takeover(&xd, &outcome);
	i915_host_check(error == 0 && outcome.lit == 1U && outcome.stopped == 1U && outcome.still_lit == 0U && xd.firmware.lit_pipes == 0U,
			"takeover: pipe A found lit and stopped, nothing left lit");
	i915_host_check((i915_xd_fake_get(I915_XD_SBLC_PWM_CTL(0U)) & I915_XD_SBLC_PWM_ENABLE) == 0U && i915_xd_fake_get(I915_XD_SBLC_PWM_DUTY(0U)) == 0U,
			"takeover: the PWM stopped at duty 0");
	i915_host_check(sink.dpcd[I915_XD_DPCD_SET_POWER] == I915_XD_DPCD_POWER_D3, "takeover: the sink put to sleep (D3) over AUX A");
	i915_host_check((i915_xd_fake_get(I915_XD_PP_CONTROL(0U)) & (I915_XD_PP_CONTROL_POWER_ON | I915_XD_PP_CONTROL_BACKLIGHT | I915_XD_PP_CONTROL_FORCE_VDD | I915_XD_PP_CONTROL_RESET)) == 0U &&
			(i915_xd_fake_get(I915_XD_PP_STATUS(0U)) & I915_XD_PP_STATUS_ON) == 0U,
			"takeover: the panel's power, VDD, reset and backlight enable off, the panel off");
	i915_host_check((i915_xd_fake_get(I915_XD_TRANS_CONF(0U)) & I915_XD_TRANS_CONF_ENABLE) == 0U && (i915_xd_fake_get(I915_XD_DPLL0_ENABLE) & I915_XD_PLL_ENABLE) == 0U,
			"takeover: the transcoder and DPLL0 off");

	/* Nothing lit: nothing to do. */
	error = drv_i915_xd_takeover(&xd, &outcome);
	i915_host_check(error == 0 && outcome.lit == 0U && outcome.stopped == 0U, "takeover: again, nothing lit and nothing done");

	/* A device without a description cannot stop a lit pipe. */
	xd.chosen = 0;
	xd.firmware.lit_pipes = 1U;
	error = drv_i915_xd_takeover(&xd, &outcome);
	i915_host_check(error == ENODEV && outcome.still_lit == 1U, "takeover: no description, the lit pipe left (ENODEV)");
}

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
 * platform's ports, and the framebuffer's GGTT range.
 */

#include "host-test.h"
#include "xd-fake-regs.h"

#include "../../display/xd-io.h"
#include "../../display/xd-platform.h"
#include "../../display/xd-power.h"
#include "../../display/xd-takeover.h"
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

/* The window the readout checks use. */
static struct i915_xd_io i915_xd_to_test_io;

/* The unit the environment's reader shows. */
static struct i915_xd_to_test_vtd i915_xd_to_test_unit;

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
	i915_host_check(firmware.output == I915_XD_FW_OUTPUT_OTHER, "classify: DP SST on DDI B, not the panel's -> other");

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

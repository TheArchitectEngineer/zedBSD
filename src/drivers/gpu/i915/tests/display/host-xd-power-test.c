/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the xd power wells (display/xd-power.c) on the register
 * model (xd-fake-regs.c): the wells of the Alder Lake-P description
 * against the Latitude 5330's own list and the Linux table's facts; the
 * exact writes of power well 1 (power gate 0 before, power gate 1 after),
 * a pipe well brought up without power well 2, a Type-C AUX well's
 * routing and its PHY health hook, a Thunderbolt AUX well that does not
 * report; a drain that fails and keeps the well on; a disable while the
 * BIOS still requests the well; and the take-over of a BIOS request.
 */

#include "host-test.h"
#include "xd-fake-regs.h"

#include "../../display/xd-io.h"
#include "../../display/xd-platform.h"
#include "../../display/xd-power.h"
#include "../../intel/display-regs-xd.h"

#include <uapi/errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The Latitude 5330's GPU (Alder Lake-P) and its revision. */
#define I915_XD_POWER_TEST_DEVICE	0x46a6U
#define I915_XD_POWER_TEST_REVID	0x0cU

/*
 * What the owner's hooks saw: the pipe masks restored and drained, the
 * VGA rewrites, the Type-C ports whose PHY health was waited for, and
 * whether the next drain fails.
 */
struct i915_xd_power_test_seen {
	unsigned pipes_up;
	unsigned pipes_down;
	unsigned vga;
	unsigned health_calls;
	unsigned health_port;
	int drain_fails;
};

/* The window the checks share, reaching a fresh model each time. */
static struct i915_xd_io i915_xd_power_test_io;

/*
 * The power wells of the Latitude 5330 as its Linux debugfs lists them
 * (i915_power_domain_info of the reference capture), the always-on
 * well left out: the description must have the same wells in the same
 * order.
 */
static const char *const i915_xd_power_test_5330[] = {
	"PW_1", "DC_off", "PW_2", "PW_A", "PW_B", "PW_C", "PW_D",
	"DDI_IO_A", "DDI_IO_B", "DDI_IO_C", "DDI_IO_D", "DDI_IO_E",
	"DDI_IO_TC1", "DDI_IO_TC2", "DDI_IO_TC3", "DDI_IO_TC4",
	"AUX_A", "AUX_B", "AUX_C", "AUX_D", "AUX_E",
	"AUX_USBC1", "AUX_USBC2", "AUX_USBC3", "AUX_USBC4",
	"AUX_TBT1", "AUX_TBT2", "AUX_TBT3", "AUX_TBT4"
};

static const struct i915_xd_platform *i915_xd_power_test_setup(void);
static void i915_xd_power_test_bare(void);
static unsigned i915_xd_power_test_writes(uint32_t *offsets, uint32_t *values, unsigned max);
static void i915_xd_power_test_hooks(struct i915_xd_power_hooks *hooks, struct i915_xd_power_test_seen *seen);
static void i915_xd_power_test_pipes_up(void *context, unsigned pipe_mask);
static int i915_xd_power_test_pipes_down(void *context, unsigned pipe_mask);
static void i915_xd_power_test_vga(void *context);
static int i915_xd_power_test_health(void *context, unsigned tc_port);
static int i915_xd_power_test_well(const struct i915_xd_platform *platform, const char *name);
static void i915_xd_power_test_table(void);
static void i915_xd_power_test_main_wells(void);
static void i915_xd_power_test_aux_wells(void);
static void i915_xd_power_test_disable(void);

/*
 * Runs every check of the xd power wells and reports.
 */
int
main(void)
{
	int failures;

	/* Runs each group of checks. */
	i915_xd_power_test_table();
	i915_xd_power_test_main_wells();
	i915_xd_power_test_aux_wells();
	i915_xd_power_test_disable();

	/* Reports the result. */
	failures = i915_host_report("host_xd_power_test");
	if (failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Starts a fresh model in which every well's state follows its driver
 * request at once and every power gate's fuses are distributed, and
 * returns the Alder Lake-P description.
 */
static const struct i915_xd_platform *
i915_xd_power_test_setup(void)
{
	struct i915_xd_io_hooks hooks;
	struct i915_xd_choice choice;
	int error;

	/* A fresh model and a window that reaches it. */
	i915_xd_fake_reset();
	i915_xd_fake_hooks(&hooks);
	drv_i915_xd_io_init(&i915_xd_power_test_io, NULL, NULL);
	drv_i915_xd_io_set_hooks(&i915_xd_power_test_io, &hooks);

	/* The states of the three controls follow their requests; the fuses are in. */
	i915_xd_fake_rule_mirror(I915_XD_PWR_WELL_CTL, 0xaaaaaaaaU, I915_XD_PWR_WELL_CTL, -1, 0U);
	i915_xd_fake_rule_mirror(I915_XD_PWR_WELL_CTL_AUX, 0xaaaaaaaaU, I915_XD_PWR_WELL_CTL_AUX, -1, 0U);
	i915_xd_fake_rule_mirror(I915_XD_PWR_WELL_CTL_DDI, 0xaaaaaaaaU, I915_XD_PWR_WELL_CTL_DDI, -1, 0U);
	i915_xd_fake_set(I915_XD_FUSE_STATUS, 0xffffffffU);

	/* The description of the Latitude 5330's GPU. */
	error = drv_i915_xd_platform_choose(I915_XD_POWER_TEST_DEVICE, I915_XD_POWER_TEST_REVID, 0, 0U, &choice);
	if (error != 0)
		return NULL;

	/* Succeeded: the description. */
	return choice.platform;
}

/* Starts a fresh model whose wells never report their state. */
static void
i915_xd_power_test_bare(void)
{
	struct i915_xd_io_hooks hooks;

	/* A fresh model without rules, and the window on it. */
	i915_xd_fake_reset();
	i915_xd_fake_hooks(&hooks);
	drv_i915_xd_io_init(&i915_xd_power_test_io, NULL, NULL);
	drv_i915_xd_io_set_hooks(&i915_xd_power_test_io, &hooks);
}

/* Copies the writes of the window, oldest first; returns how many. */
static unsigned
i915_xd_power_test_writes(
	uint32_t *offsets,
	uint32_t *values,
	unsigned max)
{
	struct i915_xd_io_record records[I915_XD_IO_RECORDS];
	unsigned count;
	unsigned index;
	unsigned kept;

	/* Takes the record and keeps the writes. */
	count = drv_i915_xd_io_records(&i915_xd_power_test_io, records, I915_XD_IO_RECORDS);
	kept = 0U;
	for (index = 0U; index < count && kept < max; index++) {
		if (records[index].kind != I915_XD_IO_RECORD_WRITE)
			continue;

		offsets[kept] = records[index].offset;
		values[kept] = records[index].value;
		kept++;
	}

	/* Succeeded: the writes copied. */
	return kept;
}

/* Fills the hooks with the test's recorders. */
static void
i915_xd_power_test_hooks(
	struct i915_xd_power_hooks *hooks,
	struct i915_xd_power_test_seen *seen)
{
	/* Nothing seen yet. */
	memset(seen, 0, sizeof(*seen));

	/* The recorders. */
	hooks->pipes_up = i915_xd_power_test_pipes_up;
	hooks->pipes_down = i915_xd_power_test_pipes_down;
	hooks->vga_reset = i915_xd_power_test_vga;
	hooks->tc_health = i915_xd_power_test_health;
	hooks->context = seen;
}

/* Records the pipes whose interrupts are restored. */
static void
i915_xd_power_test_pipes_up(
	void *context,
	unsigned pipe_mask)
{
	struct i915_xd_power_test_seen *seen;

	/* Adds the pipes. */
	seen = context;
	seen->pipes_up |= pipe_mask;
}

/* Records the pipes drained, and fails the drain when asked to. */
static int
i915_xd_power_test_pipes_down(
	void *context,
	unsigned pipe_mask)
{
	struct i915_xd_power_test_seen *seen;

	/* Adds the pipes. */
	seen = context;
	seen->pipes_down |= pipe_mask;

	/* A drain the test made fail. */
	if (seen->drain_fails != 0)
		return 1;

	/* Succeeded: drained. */
	return 0;
}

/* Counts the VGA rewrites. */
static void
i915_xd_power_test_vga(
	void *context)
{
	struct i915_xd_power_test_seen *seen;

	/* Counts one. */
	seen = context;
	seen->vga++;
}

/* Records a wait for a Type-C PHY's health. */
static int
i915_xd_power_test_health(
	void *context,
	unsigned tc_port)
{
	struct i915_xd_power_test_seen *seen;

	/* Records the port. */
	seen = context;
	seen->health_calls++;
	seen->health_port = tc_port;

	/* Succeeded: the PHY is healthy. */
	return 0;
}

/* Returns the table entry of a well by its name, or -1. */
static int
i915_xd_power_test_well(
	const struct i915_xd_platform *platform,
	const char *name)
{
	unsigned well;

	/* Looks for the name. */
	for (well = 0U; well < platform->nwells; well++) {
		if (strcmp(platform->wells[well].name, name) == 0)
			return (int)well;
	}

	/* No such well. */
	return -1;
}

/*
 * The Alder Lake-P wells: the 5330's list in order, and the facts of the
 * main wells as the Linux table has them (control index, power gate,
 * VGA, interrupt pipes).
 */
static void
i915_xd_power_test_table(void)
{
	const struct i915_xd_platform *platform;
	const struct i915_xd_well_desc *desc;
	unsigned count;
	unsigned well;
	unsigned same;
	int found;

	/* The wells, in the 5330's order. */
	platform = i915_xd_power_test_setup();
	i915_host_check(platform != NULL, "power: the Latitude 5330's GPU has a description");
	if (platform == NULL)
		return;
	count = sizeof(i915_xd_power_test_5330) / sizeof(i915_xd_power_test_5330[0]);
	same = 0U;
	for (well = 0U; well < count && well < platform->nwells; well++) {
		if (strcmp(platform->wells[well].name, i915_xd_power_test_5330[well]) == 0)
			same++;
	}
	i915_host_check(platform->nwells == count && same == count, "power: the description's wells are the 5330's, in its order");

	/* Power well 1: index 0, power gate 1, nothing else. */
	desc = &platform->wells[0];
	i915_host_check(desc->control == I915_XD_WELL_CONTROL_MAIN && desc->index == 0U && desc->fuse_gate == 1U &&
			(desc->flags & I915_XD_WELL_VGA) == 0U && desc->irq_pipes == 0U,
			"power: PW_1 is index 0 with power gate 1");

	/* Power well 2: index 1, power gate 2, the VGA plane. */
	desc = &platform->wells[2];
	i915_host_check(desc->index == 1U && desc->fuse_gate == 2U && (desc->flags & I915_XD_WELL_VGA) != 0U &&
			desc->irq_pipes == 0U,
			"power: PW_2 is index 1 with power gate 2 and the VGA plane");

	/* The pipe wells A..D: indexes 5..8, power gates 6..9, each its pipe's interrupts. */
	same = 0U;
	for (well = 3U; well < 7U; well++) {
		desc = &platform->wells[well];
		if (desc->index == well + 2U && desc->fuse_gate == well + 3U && desc->irq_pipes == (1U << (well - 3U)))
			same++;
	}
	i915_host_check(same == 4U, "power: PW_A..PW_D are indexes 5..8, power gates 6..9, pipe interrupts A..D");

	/* The AUX_USBC wells take 500 ms; the Thunderbolt ones are marked. */
	found = i915_xd_power_test_well(platform, "AUX_USBC1");
	i915_host_check(found >= 0 && platform->wells[found].enable_timeout_ms == 500U, "power: AUX_USBC1 may take 500 ms");
	found = i915_xd_power_test_well(platform, "AUX_TBT2");
	i915_host_check(found >= 0 && (platform->wells[found].flags & I915_XD_WELL_TBT_AUX) != 0U &&
			platform->wells[found].index == 10U,
			"power: AUX_TBT2 is the Thunderbolt AUX well at index 10");

	/* Finding by the control and the index. */
	found = drv_i915_xd_power_find(platform, I915_XD_WELL_CONTROL_MAIN, I915_XD_WELL_PW_A);
	i915_host_check(found == 3, "power: PW_A is found by its control and index");
	found = drv_i915_xd_power_find(platform, I915_XD_WELL_CONTROL_MAIN, I915_XD_WELL_PW_5);
	i915_host_check(found == -1, "power: display 13 has no PW_5");
}

/* Power well 1, power well 2 and a pipe well. */
static void
i915_xd_power_test_main_wells(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_power_hooks hooks;
	struct i915_xd_power_test_seen seen;
	struct i915_xd_well_report report;
	uint32_t offsets[8];
	uint32_t values[8];
	unsigned count;
	int owned;
	int error;

	/* Power well 1: the FLR source bit and power gate 0 first, then the request, then power gate 1. */
	platform = i915_xd_power_test_setup();
	if (platform == NULL)
		return;
	i915_xd_power_test_hooks(&hooks, &seen);
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 0U, &hooks, &report);
	count = i915_xd_power_test_writes(offsets, values, 8U);
	i915_host_check(error == 0 && count == 2U &&
			offsets[0] == I915_XD_CHICKEN_DCPR_1 && values[0] == I915_XD_DCPR_1_DISABLE_FLR_SRC &&
			offsets[1] == I915_XD_PWR_WELL_CTL && values[1] == 0x2U &&
			report.state_late == 0 && report.fuses_late == 0U,
			"power: PW_1 sets the FLR source bit, then its request");
	owned = drv_i915_xd_power_well_owned(&i915_xd_power_test_io, platform, 0U);
	i915_host_check(owned == 1, "power: PW_1 is owned and on");

	/* Late fuses of power gates 0 and 1 are reported; the enable goes on. */
	platform = i915_xd_power_test_setup();
	i915_xd_fake_set(I915_XD_FUSE_STATUS, 0U);
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 0U, &hooks, &report);
	i915_host_check(error == 0 && report.fuses_late == 0x3U && report.state_late == 0,
			"power: late fuses of power gates 0 and 1 are reported, the well still comes up");

	/* Power well 2 rewrites the VGA registers. */
	platform = i915_xd_power_test_setup();
	i915_xd_power_test_hooks(&hooks, &seen);
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 2U, &hooks, &report);
	i915_host_check(error == 0 && seen.vga == 1U && seen.pipes_up == 0U, "power: PW_2 rewrites the VGA registers");

	/* Pipe A's well comes up without power well 2 and restores pipe A's interrupts. */
	platform = i915_xd_power_test_setup();
	i915_xd_power_test_hooks(&hooks, &seen);
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 3U, &hooks, &report);
	count = i915_xd_power_test_writes(offsets, values, 8U);
	i915_host_check(error == 0 && count == 1U && offsets[0] == I915_XD_PWR_WELL_CTL &&
			values[0] == I915_XD_PWR_WELL_REQUEST(I915_XD_WELL_PW_A) && seen.pipes_up == 0x1U && seen.vga == 0U,
			"power: PW_A comes up alone and restores pipe A's interrupts");

	/* A well whose state never comes is reported, not unwound. */
	platform = i915_xd_power_test_setup();
	i915_xd_power_test_bare();
	i915_xd_fake_set(I915_XD_FUSE_STATUS, 0xffffffffU);
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 3U, &hooks, &report);
	i915_host_check(error == 0 && report.state_late == 1 &&
			(i915_xd_fake_get(I915_XD_PWR_WELL_CTL) & I915_XD_PWR_WELL_REQUEST(I915_XD_WELL_PW_A)) != 0U,
			"power: a state that does not come is reported and the request stays");

	/* DC off has no request bit: refused. */
	platform = i915_xd_power_test_setup();
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 1U, &hooks, &report);
	i915_host_check(error == EINVAL, "power: DC_off is not switched by a request");
}

/* The AUX wells of a combo port, a Type-C port and a Thunderbolt tunnel. */
static void
i915_xd_power_test_aux_wells(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_power_hooks hooks;
	struct i915_xd_power_test_seen seen;
	struct i915_xd_well_report report;
	uint32_t offsets[8];
	uint32_t values[8];
	unsigned count;
	int well;
	int error;

	/* AUX A: only the request. */
	platform = i915_xd_power_test_setup();
	if (platform == NULL)
		return;
	i915_xd_power_test_hooks(&hooks, &seen);
	well = i915_xd_power_test_well(platform, "AUX_A");
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, (unsigned)well, &hooks, &report);
	count = i915_xd_power_test_writes(offsets, values, 8U);
	i915_host_check(error == 0 && count == 1U && offsets[0] == I915_XD_PWR_WELL_CTL_AUX && values[0] == 0x2U &&
			seen.health_calls == 0U,
			"power: AUX_A is only requested");

	/* AUX USBC2: the channel routed to the PHY, the request, then the PHY's health of TC2. */
	platform = i915_xd_power_test_setup();
	i915_xd_power_test_hooks(&hooks, &seen);
	i915_xd_fake_set(I915_XD_DDI_AUX_CTL(4U), 0x123U | I915_XD_AUX_CTL_TBT_IO);
	well = i915_xd_power_test_well(platform, "AUX_USBC2");
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, (unsigned)well, &hooks, &report);
	count = i915_xd_power_test_writes(offsets, values, 8U);
	i915_host_check(error == 0 && count == 2U &&
			offsets[0] == I915_XD_DDI_AUX_CTL(4U) && values[0] == 0x123U &&
			offsets[1] == I915_XD_PWR_WELL_CTL_AUX && values[1] == I915_XD_PWR_WELL_REQUEST(4U) &&
			seen.health_calls == 1U && seen.health_port == 1U,
			"power: AUX_USBC2 routes its channel to the PHY and waits for TC2's health");

	/* AUX TBT3: the channel routed through the Thunderbolt IO; no health; a missing state is only reported. */
	platform = i915_xd_power_test_setup();
	i915_xd_power_test_bare();
	i915_xd_power_test_hooks(&hooks, &seen);
	well = i915_xd_power_test_well(platform, "AUX_TBT3");
	error = drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, (unsigned)well, &hooks, &report);
	count = i915_xd_power_test_writes(offsets, values, 8U);
	i915_host_check(error == 0 && count == 2U &&
			offsets[0] == I915_XD_DDI_AUX_CTL(5U) && values[0] == I915_XD_AUX_CTL_TBT_IO &&
			offsets[1] == I915_XD_PWR_WELL_CTL_AUX && values[1] == I915_XD_PWR_WELL_REQUEST(11U) &&
			report.state_late == 1 && seen.health_calls == 0U,
			"power: AUX_TBT3 routes TC3's channel through the Thunderbolt IO and may not report");
}

/* Taking wells down, a failed drain, a BIOS request, and taking a BIOS request over. */
static void
i915_xd_power_test_disable(void)
{
	const struct i915_xd_platform *platform;
	struct i915_xd_power_hooks hooks;
	struct i915_xd_power_test_seen seen;
	struct i915_xd_well_report report;
	uint32_t offsets[8];
	uint32_t values[8];
	unsigned count;
	int owned;
	int error;

	/* PW_B down: pipe B drained, the request withdrawn, the state gone. */
	platform = i915_xd_power_test_setup();
	if (platform == NULL)
		return;
	i915_xd_power_test_hooks(&hooks, &seen);
	(void)drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 4U, &hooks, &report);
	error = drv_i915_xd_power_well_disable(&i915_xd_power_test_io, platform, 4U, &hooks, &report);
	owned = drv_i915_xd_power_well_owned(&i915_xd_power_test_io, platform, 4U);
	i915_host_check(error == 0 && seen.pipes_down == 0x2U && owned == 0 && report.state_late == 0 &&
			(i915_xd_fake_get(I915_XD_PWR_WELL_CTL) & I915_XD_PWR_WELL_STATE(I915_XD_WELL_PW_B)) == 0U,
			"power: PW_B goes down after pipe B is drained");

	/* A failed drain keeps PW_C on and its request untouched. */
	platform = i915_xd_power_test_setup();
	i915_xd_power_test_hooks(&hooks, &seen);
	(void)drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 5U, &hooks, &report);
	seen.drain_fails = 1;
	error = drv_i915_xd_power_well_disable(&i915_xd_power_test_io, platform, 5U, &hooks, &report);
	owned = drv_i915_xd_power_well_owned(&i915_xd_power_test_io, platform, 5U);
	i915_host_check(error == EBUSY && owned == 1, "power: a failed drain keeps PW_C on");

	/* A well the BIOS still requests: the driver's request goes, no wait for the state. */
	platform = i915_xd_power_test_setup();
	i915_xd_power_test_hooks(&hooks, &seen);
	(void)drv_i915_xd_power_well_enable(&i915_xd_power_test_io, platform, 2U, &hooks, &report);
	i915_xd_fake_set(I915_XD_PWR_WELL_CTL_BIOS, I915_XD_PWR_WELL_REQUEST(I915_XD_WELL_PW_2));
	i915_xd_fake_set(I915_XD_PWR_WELL_CTL, I915_XD_PWR_WELL_REQUEST(I915_XD_WELL_PW_2) | I915_XD_PWR_WELL_STATE(I915_XD_WELL_PW_2));
	error = drv_i915_xd_power_well_disable(&i915_xd_power_test_io, platform, 2U, &hooks, &report);
	i915_host_check(error == 0 && report.state_late == 0 &&
			(i915_xd_fake_get(I915_XD_PWR_WELL_CTL) & I915_XD_PWR_WELL_REQUEST(I915_XD_WELL_PW_2)) == 0U,
			"power: a well the BIOS requests loses only the driver's request");

	/* Taking power well 2 over from the BIOS: the driver's request first, then the BIOS's withdrawn. */
	platform = i915_xd_power_test_setup();
	i915_xd_fake_set(I915_XD_PWR_WELL_CTL_BIOS, I915_XD_PWR_WELL_REQUEST(I915_XD_WELL_PW_2) | 0x8000U);
	drv_i915_xd_power_well_adopt(&i915_xd_power_test_io, platform, 2U);
	count = i915_xd_power_test_writes(offsets, values, 8U);
	i915_host_check(count == 2U &&
			offsets[0] == I915_XD_PWR_WELL_CTL && values[0] == I915_XD_PWR_WELL_REQUEST(I915_XD_WELL_PW_2) &&
			offsets[1] == I915_XD_PWR_WELL_CTL_BIOS && values[1] == 0x8000U,
			"power: a BIOS request is taken over without a gap");

	/* Nothing to take over when the BIOS does not request the well. */
	platform = i915_xd_power_test_setup();
	drv_i915_xd_power_well_adopt(&i915_xd_power_test_io, platform, 3U);
	count = i915_xd_power_test_writes(offsets, values, 8U);
	i915_host_check(count == 0U, "power: no BIOS request, nothing written");
}

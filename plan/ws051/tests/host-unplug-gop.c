/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws051-p005b: the host test of the firmware's own external output whose
 * display is unplugged while it is lit (display/present.c, display.c).
 * The firmware's Type-C DisplayPort display, lit, is marked to leave the
 * window at its connector's unplug and at its port's partner staying away;
 * the built-in panel, another port, another connector and an output not
 * lit are not.  Giving the output up makes the lease's presentations fail
 * and leaves the node with the panel, or with no output on a machine
 * without the panel's connector (this test's: the hotplug path is not
 * started).
 */

#include "src/drivers/gpu/i915/tests/display/host-test.h"
#include "src/drivers/gpu/i915/display/internal.h"
#include "src/drivers/gpu/i915/display/display.h"
#include "src/drivers/gpu/i915/display/present.h"
#include "src/drivers/gpu/i915/i915.h"

#include <string.h>

/* The worker's wakes. */
static unsigned wakes;

void drv_i915_worker_wake(struct i915_device *device);

/* Counts the worker's wakes (the worker is not built in). */
void
drv_i915_worker_wake(
	struct i915_device *device)
{
	(void)device;
	wakes++;
}

/*
 * Runs the checks.
 */
int
main(
	void)
{
	static struct i915_device device;
	static struct i915_display display;

	/* The firmware lit the DP display of TC2 (connector 4); the driver lit it, not moved. */
	memset(&device, 0, sizeof(device));
	memset(&display, 0, sizeof(display));
	device.display = &display;
	display.device = &device;
	display.output.kind = I915_OUTPUT_KIND_DP_EXT;
	display.output.has_connector = 1;
	display.output.connector = 4U;
	display.output.tc_port = 1U;
	display.gop_output = display.output;
	display.resident_up = 1;

	/* Another connector, and another port's partner: nothing. */
	drv_i915_present_unplugged(&display, 2U);
	drv_i915_present_tc_partner_gone(&display, 0U);
	i915_host_check(display.window.unplugged == 0 && wakes == 0U, "another connector or port does not end the window");

	/* The firmware's display unplugged: marked once, the worker woken. */
	drv_i915_present_unplugged(&display, 4U);
	i915_host_check(display.window.unplugged == 1 && wakes == 1U, "the firmware's external display's unplug marks the window");
	i915_host_check(drv_i915_present_unplug_pending(&device) == 1, "the worker's loop leaves the window");
	drv_i915_present_tc_partner_gone(&display, 1U);
	i915_host_check(wakes == 1U, "the port's partner gone after the unplug does not wake again");

	/* The port's partner gone alone (no disconnection seen): marked as an unplug. */
	display.window.unplugged = 0;
	drv_i915_present_tc_partner_gone(&display, 1U);
	i915_host_check(display.window.unplugged == 1 && wakes == 2U, "the port's partner gone marks the window");

	/* Not lit: not marked. */
	display.window.unplugged = 0;
	display.resident_up = 0;
	drv_i915_present_unplugged(&display, 4U);
	drv_i915_present_tc_partner_gone(&display, 1U);
	i915_host_check(display.window.unplugged == 0 && wakes == 2U, "an output not lit is not marked");

	/* The built-in panel as the firmware's output: never unplugged. */
	display.resident_up = 1;
	display.output.kind = I915_OUTPUT_KIND_PANEL;
	display.output.connector = 0U;
	display.gop_output = display.output;
	drv_i915_present_unplugged(&display, 0U);
	drv_i915_present_tc_partner_gone(&display, 1U);
	i915_host_check(display.window.unplugged == 0 && wakes == 2U, "the built-in panel is never taken as unplugged");

	/* Given up on a machine without the panel's connector: no output, the lease's frames fail. */
	display.output.kind = I915_OUTPUT_KIND_DP_EXT;
	display.output.connector = 4U;
	display.gop_output = display.output;
	drv_i915_display_output_lost(&device);
	i915_host_check(display.output.none == 1 && display.gop_output.none == 1, "given up without a panel: no output");
	i915_host_check(display.window.lease_failed == 1, "the lease's presentations fail");
	i915_host_check(drv_i915_display_output_moved(&display) == 0, "the output given up is the firmware's from here on (nothing moves back)");

	/* With no output, an unplug or a partner gone marks nothing. */
	display.window.unplugged = 0;
	drv_i915_present_unplugged(&display, 4U);
	drv_i915_present_tc_partner_gone(&display, 1U);
	i915_host_check(display.window.unplugged == 0 && wakes == 2U, "no output: nothing to leave");

	/* The result. */
	return i915_host_report("ws051-p005b unplug of the firmware's output");
}

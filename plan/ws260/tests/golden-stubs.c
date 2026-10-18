/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host stand-ins the display 9 golden scenes need beyond
 * src/drivers/gpu/i915/tests/display/host-kernel.c: the same services as
 * plan/ws084/tests/native-decide-stubs.c, except that the boot line is
 * empty, so every boot word takes its default.  Every other stand-in ends
 * the test with the name of the service.
 */

#include <stdio.h>
#include <stdlib.h>

#include <drivers/pci/pci.h>
#include <kern/boot.h>
#include <kern/sched.h>

#include "drivers/gpu/i915/worker.h"

static void stub_unreachable(const char *service) __attribute__((noreturn));

/*
 * Stands in for the scheduler's sleep.
 */
void
sched_sleep(
	uint64_t timeout_tick)
{
	(void)timeout_tick;

	/* The golden scenes do not sleep. */
	stub_unreachable("sched_sleep");
}

/*
 * Stands in for the PCI device's address.
 */
void
drv_pci_device_address(
	const struct drv_pci_device *d,
	struct drv_pci_address *a)
{
	(void)d;
	(void)a;

	/* The golden scenes do not name the device. */
	stub_unreachable("drv_pci_device_address");
}

/*
 * Stands in for the i915 worker's backlight call.
 */
int
drv_i915_worker_sync_backlight(
	struct i915_device *device,
	int op,
	uint32_t *value)
{
	(void)device;
	(void)op;
	(void)value;

	/* The golden scenes do not touch the backlight. */
	stub_unreachable("drv_i915_worker_sync_backlight");
}

/*
 * Stands in for the i915 worker's wake.
 */
void
drv_i915_worker_wake(
	struct i915_device *device)
{
	(void)device;

	/* The golden scenes do not present. */
	stub_unreachable("drv_i915_worker_wake");
}

/*
 * Stands in for the boot parameters: there is no boot line.
 */
const struct kern_boot_parameters *
kern_boot_parameters_current(
	void)
{
	/* Succeeded: no boot line, so every word takes its default. */
	return NULL;
}

/*
 * Stands in for a boot parameter's value.
 */
const char *
kern_boot_parameters_value(
	const struct kern_boot_parameters *parameters,
	enum kern_boot_parameter_key key)
{
	(void)parameters;
	(void)key;

	/* The golden scenes do not read the boot parameters. */
	stub_unreachable("kern_boot_parameters_value");
}

/*
 * Stands in for the display.mode= parser.
 */
int
kern_boot_display_mode_parse(
	const char *text,
	size_t length,
	uint32_t *width,
	uint32_t *height,
	uint32_t *refresh_hz)
{
	(void)text;
	(void)length;
	(void)width;
	(void)height;
	(void)refresh_hz;

	/* The golden scenes do not pick an HDMI mode. */
	stub_unreachable("kern_boot_display_mode_parse");
}

/* Ends the test with the name of a service the tested path must not reach. */
static void
stub_unreachable(
	const char *service)
{
	/* The name, then the end. */
	fprintf(stderr, "golden host test: %s reached\n", service);
	abort();
}

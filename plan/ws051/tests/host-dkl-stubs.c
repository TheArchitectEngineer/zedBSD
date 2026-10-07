/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host stand-ins the Type-C PLL test (host-dkl-test.c) needs beyond
 * src/drivers/gpu/i915/tests/display/host-kernel.c (ws051-p003, as ws084-p004 needs them): kernel
 * services and an i915 worker call that display files added since that
 * file was written reach.  The Type-C PLL text reaches none of them; each
 * stands in only so the production files link, and a call ends the test
 * with the name of the service.
 */

#include <stdio.h>
#include <stdlib.h>

#include <drivers/pci/pci.h>
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

	/* The Type-C PLL text does not sleep. */
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

	/* The Type-C PLL text does not name the device. */
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

	/* The Type-C PLL text does not touch the backlight. */
	stub_unreachable("drv_i915_worker_sync_backlight");
}

/* Ends the test with the name of a service the tested path must not reach. */
static void
stub_unreachable(
	const char *service)
{
	/* The name, then the end. */
	fprintf(stderr, "host-dkl: %s reached\n", service);
	abort();
}

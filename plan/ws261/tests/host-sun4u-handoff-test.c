/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the sun4u HAL's boot handoff: the kernel's common record and
 * the names under which the kernel reads the IDE controller.
 */

#include <stdio.h>
#include <string.h>
#include <hal/hal.h>
#include "bsp.h"

/* How many checks failed; zero when the run passes. */
static unsigned failures;

/* How many checks ran. */
static unsigned checks;

static void check(int condition, const char *what);

/*
 * Stands in for the HAL's copy routine.
 */
void *
hal_memcpy(
	void *destination,
	const void *source,
	size_t size)
{
	/* Copies with the host's routine. */
	memcpy(destination, source, size);

	/* Reports the destination as the HAL's routine does. */
	return destination;
}

/*
 * Runs the checks and reports the number of failures.
 */
int
main(
	void)
{
	struct sun4u_boot_handoff loader;
	const struct kern_boot_handoff *kernel;
	const uint64_t *command;
	const uint64_t *control;

	/* Builds a loader record as the second stage does. */
	memset(&loader, 0, sizeof(loader));
	loader.common.magic = KERN_HANDOFF_MAGIC;
	loader.common.version = SUN4U_HANDOFF_COMMON_VERSION;
	loader.common.size = sizeof(loader);
	loader.common.device_count = 1;
	loader.common.boot_bios_id = 0x80;
	loader.common.boot_partition_scheme = KERN_PARTITION_SCHEME_SUN;
	loader.common.boot_partition_index = 1;
	loader.common.boot_partition_lba = 4096;
	loader.extension_magic = SUN4U_HANDOFF_EXTENSION_MAGIC;
	loader.extension_version = SUN4U_HANDOFF_EXTENSION_VERSION;
	loader.ide_vendor = 0x1095;
	loader.ide_device = 0x0646;
	loader.ide_primary_command = 0x1f0;
	loader.ide_primary_control = 0x3f6;
	sun4u_boot_init(&loader);

	/* The kernel's record is the common one under the common version. */
	kernel = sun4u_kernel_handoff();
	check(kernel->magic == KERN_HANDOFF_MAGIC, "magic");
	check(kernel->version == KERN_HANDOFF_VERSION_MULTIBOOT, "common version");
	check(kernel->size == sizeof(struct kern_boot_handoff), "common size");
	check(kernel->device_count == 1, "device count");
	check(kernel->boot_bios_id == 0x80, "boot BIOS id");
	check(kernel->boot_partition_scheme == KERN_PARTITION_SCHEME_SUN, "Sun scheme");
	check(kernel->boot_partition_index == 1, "slice");
	check(kernel->boot_partition_lba == 4096, "slice LBA");

	/* The HAL keeps its own copy of the loader's record. */
	check(sun4u_boot_handoff() != &loader, "copy kept");
	check(sun4u_boot_handoff()->ide_primary_command == 0x1f0, "copy contents");

	/* The IDE controller's primary channel goes by name. */
	command = hal_get_arch_handoff("sun4u.cmd646.primary.command");
	check(command != NULL && *command == 0x1f0, "command block");
	control = hal_get_arch_handoff("sun4u.cmd646.primary.control");
	check(control != NULL && *control == 0x3f6, "control block");

	/* Unknown, partial, longer and missing names report nothing. */
	check(hal_get_arch_handoff("sun4u.cmd646.primary") == NULL, "prefix");
	check(hal_get_arch_handoff("sun4u.cmd646.primary.commandx") == NULL, "longer");
	check(hal_get_arch_handoff("boot.command-line") == NULL, "command line");
	check(hal_get_arch_handoff(NULL) == NULL, "missing name");

	/* Reports the run. */
	printf("sun4u_handoff_host_test: %u checks, %u failures\n", checks, failures);
	if (failures != 0)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Counts one check and reports a failed one.
 */
static void
check(
	int condition,
	const char *what)
{
	/* Counts the check. */
	checks++;

	/* Reports a failure by name. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

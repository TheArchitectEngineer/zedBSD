/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the ACPI side of the BAR assignment (BUG-210,
 * src/drivers/acpi/acpi-pci-root.c compiled unchanged) on the Latitude
 * 5330's DSDT and SSDTs (plan/ws049/tests/latitude5330/).
 *
 * The interpreter is compiled for the host here, so its error numbers are
 * the host's (<uapi/errno.h> defers to the host C library).
 *
 * The WS049 AML harness (plan/ws049/tests/aml-host.c) loads the tables as
 * the kernel does; it is compiled with its main renamed and its call of
 * drv_acpi_resources_walk() for --resources sent to resources_hook()
 * below, which runs the two walks of acpi-pci-root.c while the namespace
 * is loaded:
 *
 *   - the host bridge of segment 0, bus 0 must be found (\_SB.PC00, _HID
 *     PNP0A08) and its _CRS must give the memory ranges the harness
 *     printed for it: the legacy VGA range 0xa0000 first, and the 32-bit
 *     and 64-bit windows with length 0, because their GNVS fields read as
 *     zero in the harness;
 *   - a bus no bridge has must report ENOENT;
 *   - the motherboard resource devices must include \_SB.PRRE, whose _STA
 *     is 0x08 (functioning, not present): its 0xfe000000 and 0xff000000
 *     ranges must be visited.
 *
 *   plan/ws159/tests/run-host-pci-root.sh
 */

#include <drivers/acpi/acpi.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

/*
 * What one walk saw: the number of memory ranges, the first one, and
 * which of the ranges the test looks for it met.
 */
struct walk_record {
	unsigned memory_count;
	uint64_t first_base;
	unsigned zero_windows;
	unsigned prre_fe000000;
	unsigned prre_ff000000;
};

/* The number of checks that failed. */
static unsigned failures;

int aml_host_main(int argc, char **argv);
int resources_hook(struct drv_acpi_node *device, const char *method, drv_acpi_resource_visitor_t visitor, void *argument);
int main(int argc, char **argv);
static int record_resource(const struct drv_acpi_resource *resource, void *argument);
static void check(const char *name, int passed);

/* Loads the tables through the harness, whose --resources runs the checks. */
int
main(
	int argc,
	char **argv)
{
	int status;

	/* The harness loads the tables and calls resources_hook() once. */
	status = aml_host_main(argc, argv);

	/* Reports the result. */
	if (failures != 0) {
		printf("host-pci-root: %u failed\n", failures);
		return 1;
	}

	/* A harness that failed to load the tables fails the test too. */
	if (status != 0) {
		printf("host-pci-root: the harness failed (%d)\n", status);
		return 1;
	}

	/* Succeeded: every check passed. */
	printf("host-pci-root: all passed\n");
	return 0;
}

/* Runs the walks of acpi-pci-root.c in place of the harness's --resources walk. */
int
resources_hook(
	struct drv_acpi_node *device,
	const char *method,
	drv_acpi_resource_visitor_t visitor,
	void *argument)
{
	struct walk_record record;
	int error;

	/* The device the harness looked up is not used: the walks find their own. */
	(void)device;
	(void)method;
	(void)visitor;
	(void)argument;

	/* The host bridge of segment 0, bus 0 and its windows. */
	memset(&record, 0, sizeof(record));
	error = drv_acpi_pci_root_resources_walk(0, 0, record_resource, &record);
	printf("root 0:0 error %d memory ranges %u first 0x%llx zero-length windows %u\n",
	       error,
	       record.memory_count,
	       (unsigned long long)record.first_base,
	       record.zero_windows);
	check("root bridge found", error == 0);
	check("root gives 16 memory ranges", record.memory_count == 16U);
	check("root's first memory range is the VGA range", record.first_base == 0xa0000ULL);
	check("the GNVS windows read as length 0", record.zero_windows == 2U);

	/* A bus no host bridge has. */
	memset(&record, 0, sizeof(record));
	error = drv_acpi_pci_root_resources_walk(0, 0x80, record_resource, &record);
	printf("root 0:80 error %d\n", error);
	check("no bridge for bus 0x80", error == ENOENT && record.memory_count == 0U);

	/* The motherboard resource devices. */
	memset(&record, 0, sizeof(record));
	error = drv_acpi_system_resources_walk(record_resource, &record);
	printf("system error %d memory ranges %u\n", error, record.memory_count);
	check("system walk succeeds", error == 0);
	check("PRRE's 0xfe000000 range is reserved", record.prre_fe000000 != 0U);
	check("PRRE's 0xff000000 range is reserved", record.prre_ff000000 != 0U);

	/* The harness prints its end line after a successful walk. */
	return 0;
}

/* Counts one resource of a walk and notes the ranges the checks look for. */
static int
record_resource(
	const struct drv_acpi_resource *resource,
	void *argument)
{
	struct walk_record *record;

	/* Only the memory ranges matter. */
	record = argument;
	if (resource->kind != DRV_ACPI_RESOURCE_MEMORY)
		return 0;

	/* Prints and counts the range. */
	printf("  memory 0x%llx length 0x%llx\n",
	       (unsigned long long)resource->base,
	       (unsigned long long)resource->length);
	if (record->memory_count == 0U)
		record->first_base = resource->base;
	record->memory_count++;

	/* Notes the windows the GNVS leaves empty and PRRE's ranges. */
	if (resource->length == 0U)
		record->zero_windows++;
	if (resource->base == 0xfe000000ULL && resource->length == 0x20000ULL)
		record->prre_fe000000++;
	if (resource->base == 0xff000000ULL && resource->length == 0x1000000ULL)
		record->prre_ff000000++;

	/* Goes on with the next resource. */
	return 0;
}

/* Prints one check's outcome and counts a failure. */
static void
check(
	const char *name,
	int passed)
{
	/* A failed check is counted. */
	if (!passed) {
		printf("FAIL %s\n", name);
		failures++;
		return;
	}

	/* Succeeded: the check passed. */
	printf("ok   %s\n", name);
}

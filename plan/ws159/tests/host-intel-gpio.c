/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Intel GPIO pad lookup (ws159-p006,
 * src/drivers/gpio/intel-gpio.c compiled unchanged).
 *
 * The stand-in ACPI namespace holds the Latitude 5330's pad group table
 * \_SB.GPCL (the 18 groups of its DSDT), \SBRG 0xfd000000 and the four
 * 64 KiB memory ranges of \_SB.GPI0's _CRS (0xfd6e0000, 0xfd6d0000,
 * 0xfd6a0000, 0xfd690000, as the 5330's Linux shows them).  The touchpad's
 * GpioInt pin 327 must be found in group 14 (first GPIO number 320) as pad
 * 7, with its DW0 at 0xfd6a0ae0; the pad's level is read from DW0's bit 1
 * (the 5330's DW0 at rest, 0x80800102, reads high).  A pin in no group and
 * a configuration outside the controller's ranges are refused.
 *
 *   plan/ws159/tests/run-host-intel-gpio.sh
 */

#include <drivers/acpi/acpi.h>
#include <drivers/gpio/intel-gpio.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The 5330's sideband base and the address its touchpad pad's DW0 must have. */
#define SIDEBAND_BASE		0xfd000000ULL
#define PAD_DW0			0xfd6a0ae0ULL

/* The groups of the 5330's \_SB.GPCL, and the fields of each. */
#define GROUPS			18U
#define FIELDS			9U

/*
 * An ACPI object of the stand-in namespace: an integer, or a package of
 * objects.
 */
struct drv_acpi_object {
	enum drv_acpi_type type;
	uint64_t integer;
	unsigned count;
	struct drv_acpi_object *elements[GROUPS];
};

/* The number of checks that failed, and of those that ran. */
static int failures;
static int checks;

/* The 5330's \_SB.GPCL: community, pads, pad offset, three registers, GPE, and first GPIO number. */
static const uint64_t gpcl[GROUPS][FIELDS] = {
	{ 0x6e0000, 0x1a, 0x700, 0xb0, 0x20, 0x140, 0x80, 0x84, 0x0 },
	{ 0x6e0000, 0x10, 0x8a0, 0xb4, 0x30, 0x144, 0x88, 0x8c, 0x20 },
	{ 0x6e0000, 0x19, 0x9a0, 0xb8, 0x38, 0x148, 0x90, 0x94, 0x40 },
	{ 0x690000, 0x8, 0x700, 0xb0, 0x20, 0x140, 0x80, 0x84, 0x160 },
	{ 0x690000, 0x9, 0x780, 0xb4, 0x24, 0xffff, 0x88, 0x8c, 0xffff },
	{ 0x6c0000, 0x11, 0x700, 0xb0, 0x20, 0x140, 0x80, 0x84, 0xffff },
	{ 0x6d0000, 0x8, 0x700, 0xb0, 0x20, 0x140, 0x80, 0x84, 0x60 },
	{ 0x6d0000, 0x18, 0x780, 0xb4, 0x24, 0x144, 0x88, 0x8c, 0x80 },
	{ 0x6d0000, 0x15, 0x900, 0xb8, 0x30, 0x148, 0x90, 0x94, 0xa0 },
	{ 0x6d0000, 0x18, 0xa50, 0xbc, 0x3c, 0x14c, 0x98, 0x9c, 0xc0 },
	{ 0x6d0000, 0x1d, 0xbd0, 0xc0, 0x48, 0x150, 0xa0, 0xa4, 0xe0 },
	{ 0x6a0000, 0x18, 0x700, 0xb0, 0x20, 0x140, 0x80, 0x84, 0x100 },
	{ 0x6a0000, 0x19, 0x880, 0xb4, 0x2c, 0x144, 0x88, 0x8c, 0x120 },
	{ 0x6a0000, 0x6, 0xa10, 0xb8, 0x3c, 0xffff, 0x90, 0x94, 0xffff },
	{ 0x6a0000, 0x19, 0xa70, 0xbc, 0x40, 0x14c, 0x98, 0x9c, 0x140 },
	{ 0x6a0000, 0xa, 0xc00, 0xc0, 0x50, 0xffff, 0xa0, 0xa4, 0xffff },
	{ 0x6b0000, 0xf, 0x700, 0xb0, 0x20, 0xffff, 0x80, 0x84, 0xffff },
	{ 0x6b0000, 0x5b, 0x7f0, 0xb4, 0x28, 0xffff, 0x88, 0x8c, 0xffff }
};

/* The controller's memory ranges, the second of which a "narrow" run leaves out. */
static const uint64_t ranges[4] = { 0xfd6e0000ULL, 0xfd6d0000ULL, 0xfd6a0000ULL, 0xfd690000ULL };
static int narrow;

/* The page the driver mapped, and the DW0 value the stand-in hardware gives. */
static uint64_t mapped_page;
static uint8_t page_memory[4096];
static uint32_t dw0_value;

/* The node the stand-in lookup hands out (compared, never followed). */
static int fake_node;

void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);
void kern_logf(const char *format, ...);
int kern_device_map(uint64_t address, size_t size, unsigned attributes, void **mapped);
uint32_t kern_mmio_read32(const volatile void *address);
static void check(int condition, const char *what);
static struct drv_acpi_object *integer_object(uint64_t value);
static struct drv_acpi_object *gpcl_object(void);

/* Allocates zeroed memory for the driver. */
void *
kern_calloc(
	size_t count,
	size_t size)
{
	void *object;

	/* The host's allocator stands in for the kernel's. */
	object = calloc(count, size);
	return object;
}

/* Frees the driver's memory. */
void
kern_free(
	void *pointer)
{
	/* The host's allocator stands in for the kernel's. */
	free(pointer);
}

/* Prints the driver's log lines. */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	/* The log goes to the test's output. */
	va_start(arguments, format);
	vprintf(format, arguments);
	va_end(arguments);
}

/* Maps a device page: the stand-in page memory, its address recorded. */
int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	/* One page, uncached. */
	check(size == 4096U, "one page is mapped");
	check(attributes == 0U, "uncached");
	mapped_page = address;
	*mapped = page_memory;
	return 0;
}

/* Reads the stand-in DW0. */
uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	/* Only the pad's DW0 is read. */
	check((const uint8_t *)address == page_memory + (PAD_DW0 & 0xfffU), "the read is of the pad's DW0");
	return dw0_value;
}

/* Finds the controller. */
int
drv_acpi_lookup(
	struct drv_acpi_node *scope,
	const char *path,
	struct drv_acpi_node **result)
{
	int same;

	/* From the root, the controller GPI0. */
	(void)scope;
	same = strcmp(path, "\\_SB.GPI0");
	check(same == 0, "the controller is \\_SB.GPI0");
	*result = (struct drv_acpi_node *)&fake_node;
	return 0;
}

/* Gives \SBRG. */
int
drv_acpi_evaluate_integer(
	struct drv_acpi_node *scope,
	const char *path,
	uint64_t *value)
{
	int same;

	/* Only \SBRG is asked. */
	(void)scope;
	same = strcmp(path, "\\SBRG");
	check(same == 0, "the integer asked is \\SBRG");
	*value = SIDEBAND_BASE;
	return 0;
}

/* Gives \_SB.GPCL. */
int
drv_acpi_evaluate(
	struct drv_acpi_node *scope,
	const char *path,
	struct drv_acpi_object **arguments,
	unsigned argument_count,
	struct drv_acpi_object **result)
{
	int same;

	/* Only \_SB.GPCL is asked, without arguments. */
	(void)scope;
	(void)arguments;
	same = strcmp(path, "\\_SB.GPCL");
	check(same == 0 && argument_count == 0U, "the object asked is \\_SB.GPCL");
	*result = gpcl_object();
	return 0;
}

/* Walks GPI0's _CRS: its four memory ranges (three in a narrow run). */
int
drv_acpi_resources_walk(
	struct drv_acpi_node *device,
	const char *method,
	drv_acpi_resource_visitor_t visitor,
	void *argument)
{
	struct drv_acpi_resource resource;
	unsigned index;
	int stop;

	/* The controller's _CRS. */
	(void)device;
	(void)method;
	for (index = 0; index < 4U; index++) {
		/* The narrow run leaves out the range of the touchpad's community. */
		if (narrow && ranges[index] == 0xfd6a0000ULL)
			continue;
		memset(&resource, 0, sizeof(resource));
		resource.kind = DRV_ACPI_RESOURCE_MEMORY;
		resource.base = ranges[index];
		resource.length = 0x10000U;
		stop = visitor(&resource, argument);
		if (stop != 0)
			return stop;
	}

	/* Every range was walked. */
	return 0;
}

/* Releases an object and its elements. */
void
drv_acpi_object_release(
	struct drv_acpi_object *object)
{
	unsigned index;

	/* NULL is allowed. */
	if (object == NULL)
		return;
	for (index = 0; index < object->count; index++)
		drv_acpi_object_release(object->elements[index]);
	free(object);
}

/* Gives an object's type. */
enum drv_acpi_type
drv_acpi_object_type(
	const struct drv_acpi_object *object)
{
	/* The type it was made with. */
	return object->type;
}

/* Gives an integer object's value. */
uint64_t
drv_acpi_object_integer(
	const struct drv_acpi_object *object)
{
	/* The value it was made with. */
	return object->integer;
}

/* Gives a package's count. */
unsigned
drv_acpi_object_package_count(
	const struct drv_acpi_object *object)
{
	/* The count it was made with. */
	return object->count;
}

/* Gives a package's element, without a reference. */
struct drv_acpi_object *
drv_acpi_object_package_element(
	const struct drv_acpi_object *object,
	unsigned index)
{
	/* An index past the end names nothing. */
	if (index >= object->count)
		return NULL;
	return object->elements[index];
}

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check ran. */
	checks++;

	/* A failed check is printed and counted. */
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Makes an integer object. */
static struct drv_acpi_object *
integer_object(
	uint64_t value)
{
	struct drv_acpi_object *object;

	/* An integer. */
	object = calloc(1, sizeof(*object));
	object->type = DRV_ACPI_TYPE_INTEGER;
	object->integer = value;
	return object;
}

/* Makes the 5330's \_SB.GPCL. */
static struct drv_acpi_object *
gpcl_object(void)
{
	struct drv_acpi_object *table;
	struct drv_acpi_object *group;
	unsigned index;
	unsigned field;

	/* A package of the groups, each a package of its fields. */
	table = calloc(1, sizeof(*table));
	table->type = DRV_ACPI_TYPE_PACKAGE;
	table->count = GROUPS;
	for (index = 0; index < GROUPS; index++) {
		group = calloc(1, sizeof(*group));
		group->type = DRV_ACPI_TYPE_PACKAGE;
		group->count = FIELDS;
		for (field = 0; field < FIELDS; field++)
			group->elements[field] = integer_object(gpcl[index][field]);
		table->elements[index] = group;
	}

	/* The table. */
	return table;
}

/* Runs the checks. */
int
main(void)
{
	struct drv_intel_gpio_pad *pad;
	int error;
	int level;

	/* 1. The touchpad's pin 327: group 14's pad 7, its DW0 at 0xfd6a0ae0. */
	pad = NULL;
	error = drv_intel_gpio_pad_find("\\_SB.GPI0", 327U, &pad);
	check(error == 0 && pad != NULL, "pin 327 is found");
	check(mapped_page == (PAD_DW0 & ~0xfffULL), "the page of 0xfd6a0ae0 is mapped");

	/* 2. Its level: DW0 0x80800102 (at rest) is high, with bit 1 clear it is low. */
	if (pad != NULL) {
		dw0_value = 0x80800102U;
		level = drv_intel_gpio_pad_level(pad);
		check(level == 1, "the 5330's DW0 at rest reads high");
		dw0_value = 0x80800100U;
		level = drv_intel_gpio_pad_level(pad);
		check(level == 0, "with its input low it reads low");
	}

	/* The pad's state is the driver's allocation; the test lets it go. */
	free(pad);

	/* 3. A pin in no group is refused. */
	error = drv_intel_gpio_pad_find("\\_SB.GPI0", 400U, &pad);
	check(error != 0, "pin 400 is in no group");

	/* 4. A configuration outside the controller's ranges is refused. */
	narrow = 1;
	error = drv_intel_gpio_pad_find("\\_SB.GPI0", 327U, &pad);
	check(error != 0, "outside the controller's ranges it is refused");

	/* The verdict. */
	if (failures != 0) {
		printf("host-intel-gpio: FAIL (%d of %d checks)\n", failures, checks);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("host-intel-gpio: ok (%d checks)\n", checks);
	return 0;
}

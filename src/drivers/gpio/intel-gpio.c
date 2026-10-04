/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The pads of the Intel PCH's GPIO controller that ACPI GpioInt resources
 * name (ws159-p006).
 *
 * A GpioInt resource names its controller (\_SB.GPI0) and a pin in the
 * controller's GPIO numbering, where each pad group starts at a multiple of
 * 32.  Which group a pin is in, and where the group's pads are, is
 * platform data that Intel's reference ACPI code puts in the firmware's own
 * tables: \_SB.GPCL holds one package per group (its community's offset in
 * the sideband space, its pad count, the offset of its first pad's
 * configuration, and, last, its first GPIO number), and \SBRG is the
 * sideband space's base.  So a pad is found without a table of this
 * driver's own: its configuration (DW0) is at SBRG + the community's
 * offset + the group's pad offset + 16 bytes a pad.  The address is checked
 * against the controller's own _CRS memory ranges before it is mapped.
 *
 * DW0's bit 1 is the pad's input as it is on the wire, before the pad's
 * input inversion.  The I2C-HID driver watches it in place of the
 * interrupt (whose controller line needs a HAL change, pending the user's
 * decision: plan/ws159/proposed/hal-irq-trigger.diff).
 */

#include <drivers/acpi/acpi.h>
#include <drivers/gpio/intel-gpio.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#include <stdbool.h>

/* The ACPI names of the pad groups' table and of the sideband space's base. */
#define GPIO_GROUP_TABLE	"\\_SB.GPCL"
#define GPIO_SIDEBAND_BASE	"\\SBRG"

/* The fields of one group's package: the community's offset, the pad count, the first pad's offset, the first GPIO number. */
#define GROUP_COMMUNITY		0U
#define GROUP_PADS		1U
#define GROUP_PAD_OFFSET	2U
#define GROUP_FIRST_NUMBER	8U
#define GROUP_FIELDS		9U

/* The size of one pad's configuration, and the bit of DW0 that is the pad's input. */
#define PAD_CONFIG_SIZE		16U
#define PAD_RX_STATE		0x02U

/* The page the driver maps around a pad's DW0. */
#define PAD_PAGE_SIZE		4096U

/*
 * One pad: its controller's path and pin, the physical address of its DW0,
 * and the mapped page that holds it.  It is allocated by
 * drv_intel_gpio_pad_find() and lives as long as its user keeps it (the
 * I2C-HID device, for the kernel's life).
 */
struct drv_intel_gpio_pad {
	uint32_t pin;
	uint64_t dw0;
	void *page;
	const volatile uint32_t *dw0_mapped;
};

/*
 * What the walk of the controller's _CRS looks for: whether a memory range
 * holds the pad's DW0.  It lives on the stack of the search.
 */
struct range_search {
	uint64_t address;
	bool inside;
};

static int group_field(const struct drv_acpi_object *group, unsigned index, uint64_t *value);
static int range_visitor(const struct drv_acpi_resource *resource, void *argument);

/*
 * Finds the pad an ACPI GpioInt names (its controller's path and its pin)
 * and maps its configuration.
 */
int
drv_intel_gpio_pad_find(
	const char *controller,
	uint32_t pin,
	struct drv_intel_gpio_pad **result)
{
	struct drv_acpi_node *node;
	struct drv_acpi_object *table;
	struct drv_acpi_object *group;
	struct drv_intel_gpio_pad *pad;
	struct range_search search;
	enum drv_acpi_type type;
	uint64_t sideband;
	uint64_t community;
	uint64_t pads;
	uint64_t pad_offset;
	uint64_t first;
	uint64_t page;
	unsigned count;
	unsigned index;
	bool found;
	int error;

	/* The controller's device. */
	error = drv_acpi_lookup(NULL, controller, &node);
	if (error != 0)
		return error;

	/* The sideband space's base. */
	error = drv_acpi_evaluate_integer(NULL, GPIO_SIDEBAND_BASE, &sideband);
	if (error != 0)
		return error;
	if (sideband == 0U)
		return ENODEV;

	/* The pad groups' table. */
	table = NULL;
	error = drv_acpi_evaluate(NULL, GPIO_GROUP_TABLE, NULL, 0U, &table);
	if (error != 0)
		return error;
	type = drv_acpi_object_type(table);
	if (type != DRV_ACPI_TYPE_PACKAGE) {
		drv_acpi_object_release(table);
		return ENODEV;
	}

	/* The group whose GPIO numbers hold the pin. */
	found = false;
	community = 0;
	pad_offset = 0;
	first = 0;
	count = drv_acpi_object_package_count(table);
	for (index = 0; index < count; index++) {
		/* A group's package, with its fields. */
		group = drv_acpi_object_package_element(table, index);
		error = group_field(group, GROUP_COMMUNITY, &community);
		if (error == 0)
			error = group_field(group, GROUP_PADS, &pads);
		if (error == 0)
			error = group_field(group, GROUP_PAD_OFFSET, &pad_offset);
		if (error == 0)
			error = group_field(group, GROUP_FIRST_NUMBER, &first);
		if (error != 0)
			continue;

		/* The pin is one of the group's pads. */
		if (pin >= first && pin < first + pads) {
			found = true;
			break;
		}
	}

	/* The table is no longer needed. */
	drv_acpi_object_release(table);
	if (!found)
		return ENOENT;

	/* The pad's DW0 must lie in one of the controller's memory ranges. */
	search.address = sideband + community + pad_offset + (uint64_t)(pin - first) * PAD_CONFIG_SIZE;
	search.inside = false;
	error = drv_acpi_resources_walk(node, NULL, range_visitor, &search);
	if (error != 0)
		return error;
	if (!search.inside) {
		kern_logf("intel-gpio: pin %u's configuration 0x%llx is outside %s\n", pin, (unsigned long long)search.address, controller);
		return ENODEV;
	}

	/* The pad's state. */
	pad = kern_calloc(1U, sizeof(*pad));
	if (pad == NULL)
		return ENOMEM;
	pad->pin = pin;
	pad->dw0 = search.address;

	/* Maps the page that holds the pad's DW0, uncached. */
	page = pad->dw0 & ~(uint64_t)(PAD_PAGE_SIZE - 1U);
	error = kern_device_map(page, PAD_PAGE_SIZE, KERN_DEVICE_UNCACHED, &pad->page);
	if (error != 0) {
		kern_free(pad);
		return error;
	}

	/* The pad's DW0 within the page. */
	pad->dw0_mapped = (const volatile uint32_t *)((uint8_t *)pad->page + (pad->dw0 - page));

	/* Succeeded: the pad's level can be read. */
	*result = pad;
	return 0;
}

/*
 * Reads a pad's input as it is on the wire: 1 high, 0 low.
 */
int
drv_intel_gpio_pad_level(
	const struct drv_intel_gpio_pad *pad)
{
	uint32_t dw0;

	/* The pad's DW0. */
	dw0 = kern_mmio_read32(pad->dw0_mapped);

	/* Its input's state. */
	if ((dw0 & PAD_RX_STATE) == 0U)
		return 0;

	/* Succeeded: the input is high. */
	return 1;
}

/* Reads one integer field of a group's package. */
static int
group_field(
	const struct drv_acpi_object *group,
	unsigned index,
	uint64_t *value)
{
	struct drv_acpi_object *field;
	enum drv_acpi_type type;
	unsigned count;

	/* A group that is no package of all its fields. */
	if (group == NULL)
		return EINVAL;
	type = drv_acpi_object_type(group);
	if (type != DRV_ACPI_TYPE_PACKAGE)
		return EINVAL;
	count = drv_acpi_object_package_count(group);
	if (count < GROUP_FIELDS)
		return EINVAL;

	/* The field, an integer. */
	field = drv_acpi_object_package_element(group, index);
	if (field == NULL)
		return EINVAL;
	type = drv_acpi_object_type(field);
	if (type != DRV_ACPI_TYPE_INTEGER)
		return EINVAL;

	/* Succeeded: the field's value. */
	*value = drv_acpi_object_integer(field);
	return 0;
}

/* Notes whether a memory range of the controller holds the pad's DW0. */
static int
range_visitor(
	const struct drv_acpi_resource *resource,
	void *argument)
{
	struct range_search *search;

	/* Only memory ranges. */
	search = argument;
	if (resource->kind != DRV_ACPI_RESOURCE_MEMORY)
		return 0;

	/* The range holds the four bytes of DW0. */
	if (search->address >= resource->base && search->address + 4U <= resource->base + resource->length)
		search->inside = true;

	/* Goes on: every range is looked at. */
	return 0;
}

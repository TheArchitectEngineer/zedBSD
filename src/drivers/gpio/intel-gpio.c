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
 * input inversion.
 *
 * A pad's interrupt (the GPI interrupt of a pad the OS owns): the pad sets
 * its bit in its group's GPI_IS register as its RX event configuration
 * says (the 5330's touchpad pad: level, inverted, so while the line is
 * low), and while a bit is set in both GPI_IS and GPI_IE the controller
 * asserts its own interrupt line, the Interrupt of its _CRS (the 5330's
 * \_SB.GPI0: IRQ 14, level, active low; its MADT has no override for 14,
 * so the line is configured with hal_irq_set_mode through
 * kern_irq_set_mode).  The registers are those of the Tiger Lake LP family
 * (INTC1055, INT34C5; Linux's pinctrl-tigerlake): HOSTSW_OWN at 0xb0,
 * GPI_IS at 0x100 and GPI_IE at 0x120 in the community, one 32-bit
 * register a group, the group's index being where its HOSTSW_OWN register
 * is (\_SB.GPCL's fourth field: 0xb0 plus 4 a group).  Another family is
 * refused, and the user watches the level instead.
 *
 * The handler of the controller's line looks at each pad that asked: one
 * whose bit is set in GPI_IS and GPI_IE has its GPI_IE bit cleared and its
 * GPI_IS bit cleared (written as a one), and its user's handler called;
 * the user turns the interrupt on again when it has read the device
 * (drv_intel_gpio_pad_irq_arm), so a level that stays asserted meanwhile
 * does not fire again and again.  A firing that no pad of the driver's
 * explains masks the controller's line for good (a pad the firmware left
 * enabled would otherwise fire without end), and the users go back to
 * watching their pads (drv_intel_gpio_pad_irq_alive).
 */

#include <drivers/acpi/acpi.h>
#include <drivers/gpio/intel-gpio.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#include <stdbool.h>

/* The ACPI names of the pad groups' table and of the sideband space's base. */
#define GPIO_GROUP_TABLE	"\\_SB.GPCL"
#define GPIO_SIDEBAND_BASE	"\\SBRG"

/* The fields of one group's package: the community's offset, the pad count, the first pad's offset, HOSTSW_OWN's offset, the first GPIO number. */
#define GROUP_COMMUNITY		0U
#define GROUP_PADS		1U
#define GROUP_PAD_OFFSET	2U
#define GROUP_HOSTSW_OWN	3U
#define GROUP_FIRST_NUMBER	8U
#define GROUP_FIELDS		9U

/* The size of one pad's configuration, and the bit of DW0 that is the pad's input. */
#define PAD_CONFIG_SIZE		16U
#define PAD_RX_STATE		0x02U

/* The page the driver maps around a pad's DW0, and around its community's interrupt registers. */
#define PAD_PAGE_SIZE		4096U

/* The Tiger Lake LP family's registers in a community: HOSTSW_OWN, GPI_IS and GPI_IE of the first group. */
#define COMMUNITY_HOSTSW_OWN	0xb0U
#define COMMUNITY_GPI_IS	0x100U
#define COMMUNITY_GPI_IE	0x120U

/* The most groups a community has (each one register), the most pads that interrupt, and a group's most pads. */
#define COMMUNITY_GROUPS_MAX	8U
#define IRQ_PADS_MAX		4U
#define GROUP_PADS_MAX		32U

/* The controllers whose registers are the Tiger Lake LP family's. */
static const char *const irq_families[] = { "INTC1055", "INT34C5" };

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
	/* What its interrupt needs: its controller, its community, its group's HOSTSW_OWN offset, its bit in the group. */
	struct drv_acpi_node *controller;
	uint64_t community;
	uint32_t hostsw_offset;
	uint32_t bit;
	/* Its interrupt, once enabled: the community's mapped page, the group's registers in it, the handler and its argument. */
	void *community_page;
	volatile uint32_t *hostsw;
	volatile uint32_t *status;
	volatile uint32_t *enable;
	drv_intel_gpio_handler_t handler;
	void *argument;
};

/*
 * The controller whose line the driver took (one controller, the first a
 * pad asked for): its node, its interrupt, whether the line was given up,
 * the pads that interrupt through it, and the lock (taken with interrupts
 * off) over the pads' GPI_IE registers and the list.  Zero until the first
 * pad's interrupt is enabled; it lives for the kernel's life.
 */
struct irq_controller {
	struct drv_acpi_node *node;
	int irq;
	bool taken;
	bool dead;
	struct drv_intel_gpio_pad *pads[IRQ_PADS_MAX];
	unsigned pad_count;
	struct spinlock lock;
};

/* What the walk of the controller's _CRS found of its interrupt: its number, trigger mode and polarity. */
struct irq_search {
	bool found;
	int irq;
	bool level;
	bool active_low;
};

/*
 * What the walk of the controller's _CRS looks for: whether a memory range
 * holds the pad's DW0.  It lives on the stack of the search.
 */
struct range_search {
	uint64_t address;
	bool inside;
};

static struct irq_controller irq_controller;

static int group_field(const struct drv_acpi_object *group, unsigned index, uint64_t *value);
static int range_visitor(const struct drv_acpi_resource *resource, void *argument);
static int irq_visitor(const struct drv_acpi_resource *resource, void *argument);
static bool irq_family(struct drv_acpi_node *node);
static int irq_take(struct drv_acpi_node *node);
static void irq_interrupt(int irq, kern_irq_ack_t acknowledge, void *argument);

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
	uint64_t hostsw;
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
			error = group_field(group, GROUP_HOSTSW_OWN, &hostsw);
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
	pad->controller = node;
	pad->community = sideband + community;
	pad->hostsw_offset = (uint32_t)hostsw;
	pad->bit = pin - (uint32_t)first;
	if (pads > GROUP_PADS_MAX)
		pad->hostsw_offset = 0U;

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

/*
 * Turns a pad's interrupt on: maps its community's registers, takes the
 * controller's interrupt line (once), and enables the pad's GPI interrupt.
 */
int
drv_intel_gpio_pad_irq_enable(
	struct drv_intel_gpio_pad *pad,
	drv_intel_gpio_handler_t handler,
	void *argument)
{
	struct range_search search;
	uint64_t page;
	uint32_t group;
	uint32_t owned;
	unsigned long state;
	bool family;
	int error;

	/* A controller of the family whose registers the driver knows. */
	family = irq_family(pad->controller);
	if (!family)
		return ENOTSUP;

	/* A group of at most 32 pads, whose HOSTSW_OWN register is one of the community's. */
	if (pad->hostsw_offset < COMMUNITY_HOSTSW_OWN || (pad->hostsw_offset & 3U) != 0U)
		return ENOTSUP;
	group = (pad->hostsw_offset - COMMUNITY_HOSTSW_OWN) / 4U;
	if (group >= COMMUNITY_GROUPS_MAX)
		return ENOTSUP;

	/* The registers must lie in the controller's memory too. */
	search.address = pad->community + COMMUNITY_GPI_IE + 4U * group;
	search.inside = false;
	error = drv_acpi_resources_walk(pad->controller, NULL, range_visitor, &search);
	if (error != 0)
		return error;
	if (!search.inside)
		return ENODEV;

	/* The community's first page, which holds them. */
	page = pad->community & ~(uint64_t)(PAD_PAGE_SIZE - 1U);
	error = kern_device_map(page, PAD_PAGE_SIZE, KERN_DEVICE_UNCACHED, &pad->community_page);
	if (error != 0)
		return error;
	pad->hostsw = (volatile uint32_t *)((uint8_t *)pad->community_page + (pad->community - page) + pad->hostsw_offset);
	pad->status = (volatile uint32_t *)((uint8_t *)pad->community_page + (pad->community - page) + COMMUNITY_GPI_IS + 4U * group);
	pad->enable = (volatile uint32_t *)((uint8_t *)pad->community_page + (pad->community - page) + COMMUNITY_GPI_IE + 4U * group);

	/* The pad must be the OS's (HOSTSW_OWN set): one the firmware keeps interrupts through ACPI. */
	owned = kern_mmio_read32(pad->hostsw);
	if ((owned & (1U << pad->bit)) == 0U)
		return EPERM;

	/* The controller's line, once. */
	if (!irq_controller.taken) {
		error = irq_take(pad->controller);
		if (error != 0)
			return error;
	}

	/* That controller's line, alive, with room for the pad. */
	if (irq_controller.node != pad->controller || irq_controller.dead)
		return ENOTSUP;
	if (irq_controller.pad_count >= IRQ_PADS_MAX)
		return ENOSPC;

	/* The pad joins the list, its status cleared and its interrupt on. */
	pad->handler = handler;
	pad->argument = argument;
	state = spin_lock_irqsave(&irq_controller.lock);

	irq_controller.pads[irq_controller.pad_count] = pad;
	irq_controller.pad_count++;
	kern_mmio_write32(pad->status, 1U << pad->bit);
	kern_mmio_write32(pad->enable, kern_mmio_read32(pad->enable) | (1U << pad->bit));

	spin_unlock_irqrestore(&irq_controller.lock, state);

	/* Succeeded: the pad interrupts through the controller's line. */
	kern_logf("intel-gpio: pin %u interrupts through irq %d\n", pad->pin, irq_controller.irq);
	return 0;
}

/*
 * Turns a pad's interrupt on again after its handler ran: its status
 * cleared first, so only a new assertion (or a level still there) fires.
 */
void
drv_intel_gpio_pad_irq_arm(
	struct drv_intel_gpio_pad *pad)
{
	unsigned long state;

	/* A pad whose interrupt was never enabled, or a line given up. */
	if (pad->enable == NULL || irq_controller.dead)
		return;

	/* The status cleared, the enable set, under the lock of the group's register. */
	state = spin_lock_irqsave(&irq_controller.lock);

	kern_mmio_write32(pad->status, 1U << pad->bit);
	kern_mmio_write32(pad->enable, kern_mmio_read32(pad->enable) | (1U << pad->bit));

	spin_unlock_irqrestore(&irq_controller.lock, state);
}

/* Says whether a pad's interrupt still comes: enabled, and its controller's line not given up. */
int
drv_intel_gpio_pad_irq_alive(
	const struct drv_intel_gpio_pad *pad)
{
	/* Never enabled. */
	if (pad->enable == NULL)
		return 0;

	/* The line given up. */
	if (irq_controller.dead)
		return 0;

	/* Succeeded: it comes. */
	return 1;
}

/* Tells whether a controller's _HID is one of the family whose registers the driver knows. */
static bool
irq_family(
	struct drv_acpi_node *node)
{
	struct drv_acpi_object *hid;
	enum drv_acpi_type type;
	const char *text;
	size_t length;
	unsigned index;
	bool known;
	int same;
	int error;

	/* The controller's _HID, a string. */
	hid = NULL;
	error = drv_acpi_evaluate(node, "_HID", NULL, 0U, &hid);
	if (error != 0 || hid == NULL)
		return false;
	known = false;
	type = drv_acpi_object_type(hid);
	if (type == DRV_ACPI_TYPE_STRING) {
		/* One of the family. */
		text = drv_acpi_object_string(hid, &length);
		for (index = 0; text != NULL && index < sizeof(irq_families) / sizeof(irq_families[0]); index++) {
			same = kern_strcmp(text, irq_families[index]);
			if (same == 0)
				known = true;
		}
	}

	/* The identifier is no longer needed. */
	drv_acpi_object_release(hid);

	/* Succeeded: known or not. */
	return known;
}

/*
 * Takes a controller's interrupt line: the Interrupt of its _CRS,
 * configured as it says (level, active low on the 5330), registered and
 * unmasked.
 */
static int
irq_take(
	struct drv_acpi_node *node)
{
	struct irq_search search;
	unsigned trigger;
	unsigned polarity;
	int error;

	/* The line, from the controller's _CRS. */
	search.found = false;
	search.irq = -1;
	search.level = false;
	search.active_low = false;
	error = drv_acpi_resources_walk(node, NULL, irq_visitor, &search);
	if (error != 0)
		return error;
	if (!search.found)
		return ENOENT;

	/* The controller, its line taken from here on (whatever comes next). */
	spin_init(&irq_controller.lock, LOCK_RANK_DEVICE, "intel gpio");
	irq_controller.node = node;
	irq_controller.irq = search.irq;
	irq_controller.taken = true;
	irq_controller.dead = true;

	/* The handler, while the line is still masked. */
	error = kern_irq_register(search.irq, irq_interrupt, &irq_controller);
	if (error != 0) {
		kern_logf("intel-gpio: irq %d not taken (%d)\n", search.irq, error);
		return error;
	}

	/* The line's trigger mode and polarity as the _CRS says. */
	trigger = KERN_IRQ_TRIGGER_EDGE;
	if (search.level)
		trigger = KERN_IRQ_TRIGGER_LEVEL;
	polarity = KERN_IRQ_POLARITY_HIGH;
	if (search.active_low)
		polarity = KERN_IRQ_POLARITY_LOW;
	error = kern_irq_set_mode(search.irq, trigger, polarity);
	if (error != 0) {
		kern_logf("intel-gpio: irq %d cannot be made level=%d active_low=%d (%d)\n", search.irq, (int)search.level, (int)search.active_low, error);
		(void)kern_irq_unregister(search.irq, irq_interrupt, &irq_controller);
		return error;
	}

	/* Succeeded: the line is live. */
	irq_controller.dead = false;
	kern_irq_unmask(search.irq);
	return 0;
}

/* Notes the controller's interrupt in its _CRS. */
static int
irq_visitor(
	const struct drv_acpi_resource *resource,
	void *argument)
{
	struct irq_search *search;

	/* Only the first interrupt the controller consumes. */
	search = argument;
	if (resource->kind != DRV_ACPI_RESOURCE_IRQ || resource->producer || search->found)
		return 0;

	/* Its number, trigger mode and polarity. */
	search->found = true;
	search->irq = (int)resource->base;
	search->level = resource->level != 0U;
	search->active_low = resource->active_low != 0U;

	/* Goes on (the rest is not looked at). */
	return 0;
}

/*
 * The controller's line fired: each pad of the driver's that fired has its
 * interrupt turned off and acknowledged, and its handler called.  A firing
 * that no pad explains gives the line up.
 */
static void
irq_interrupt(
	int irq,
	kern_irq_ack_t acknowledge,
	void *argument)
{
	struct irq_controller *controller;
	struct drv_intel_gpio_pad *pad;
	uint32_t status;
	uint32_t enable;
	uint32_t bit;
	unsigned index;
	bool explained;

	/* Each pad that asked. */
	controller = argument;
	explained = false;
	spin_lock(&controller->lock);

	for (index = 0; index < controller->pad_count; index++) {
		/* Fired: set in both GPI_IS and GPI_IE. */
		pad = controller->pads[index];
		bit = 1U << pad->bit;
		status = kern_mmio_read32(pad->status);
		enable = kern_mmio_read32(pad->enable);
		if ((status & enable & bit) == 0U)
			continue;

		/* Off until its user arms it, and acknowledged; then the user hears it. */
		kern_mmio_write32(pad->enable, enable & ~bit);
		kern_mmio_write32(pad->status, bit);
		pad->handler(pad->argument);
		explained = true;
	}

	spin_unlock(&controller->lock);

	/* A firing no pad explains: the line goes for good, and the users watch their pads. */
	if (!explained) {
		controller->dead = true;
		kern_irq_mask(irq);
		kern_logf("intel-gpio: irq %d fired for no pad; given up\n", irq);
	}

	/* The interrupt is over (the level is quiet: every pad that fired is off). */
	kern_irq_send_eoi(acknowledge);
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

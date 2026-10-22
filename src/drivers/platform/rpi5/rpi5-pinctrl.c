/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The BCM2712's pin controller, for GPIO 24 to 35.
 *
 * Each pin has a four-bit function selector in one of the mux registers
 * and a two-bit pull in one of the pad registers.  Selector 0 is the GPIO
 * function; the others are the pin's alternate functions, which differ
 * from pin to pin and between the chip's C0 and D0 steppings, and so do
 * the registers' places.  The module knows the functions the onboard
 * radios' pins take: the GPIO function, the second SD controller's (sd2)
 * and the UART's (uart0).
 */

#include <drivers/platform/rpi5/rpi5-pinctrl.h>
#include <kern/device-io.h>
#include <kern/kcrt.h>
#include <kern/klog.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/* The fields' widths and the pulls' codes. */
#define PINCTRL_FUNCTION_MASK	0xfU
#define PINCTRL_PULL_MASK	0x3U
#define PINCTRL_PULL_NONE	0U
#define PINCTRL_PULL_DOWN	1U
#define PINCTRL_PULL_UP		2U

/* No pull asked for: the pad is left as it is. */
#define PINCTRL_PULL_KEEP	0xffU

/* The selector of the GPIO function, the same on every pin. */
#define PINCTRL_FUNCTION_GPIO	0U

/* A selector the pin does not have. */
#define PINCTRL_FUNCTION_NONE	0xffU

/* The bytes of the controller's registers the module maps. */
#define PINCTRL_REGISTER_BYTES	0x30U

/* The most registers the report shows (the C0 controller's twelve). */
#define PINCTRL_REPORT_WORDS	12U

/*
 * One pin's places and the selectors of the functions the module knows.
 */
struct pinctrl_pin {
	uint8_t pin;
	uint8_t mux_offset;
	uint8_t mux_shift;
	uint8_t pad_offset;
	uint8_t pad_shift;
	uint8_t uart0;
	uint8_t sd2;
};

/* One stepping's controller: its compatible string and its pins. */
struct pinctrl_variant {
	const char *compatible;
	const struct pinctrl_pin *pins;
	unsigned pin_count;
};

/*
 * The mapped controller.
 *
 * Filled once by drv_rpi5_pinctrl_init() and constant afterwards; the
 * registers are changed under the lock, taken with interrupts off.
 */
struct pinctrl_state {
	int ready;
	const struct pinctrl_variant *variant;
	volatile uint8_t *registers;
	uint64_t size;
	struct spinlock lock;
};

/*
 * The C0 stepping's pins.
 *
 * Values referenced: Raspberry Pi Linux rpi-6.12.y
 * drivers/pinctrl/bcm/pinctrl-bcm2712.c (bcm2712_c0_gpio_pin_regs, rows
 * 24 to 35, through MUX_BIT() and the pad bit; bcm2712_c0_gpio_pin_funcs,
 * rows 24 to 35: the selector of a function is its place in the row plus
 * one).
 */
static const struct pinctrl_pin pinctrl_c0_pins[] = {
	{ 24U, 0x0cU, 0U, 0x24U, 2U, 3U, PINCTRL_FUNCTION_NONE },
	{ 25U, 0x0cU, 4U, 0x24U, 4U, 4U, PINCTRL_FUNCTION_NONE },
	{ 26U, 0x0cU, 8U, 0x24U, 6U, 4U, PINCTRL_FUNCTION_NONE },
	{ 27U, 0x0cU, 12U, 0x24U, 8U, 4U, PINCTRL_FUNCTION_NONE },
	{ 28U, 0x0cU, 16U, 0x24U, 10U, PINCTRL_FUNCTION_NONE, PINCTRL_FUNCTION_NONE },
	{ 29U, 0x0cU, 20U, 0x24U, 12U, PINCTRL_FUNCTION_NONE, PINCTRL_FUNCTION_NONE },
	{ 30U, 0x0cU, 24U, 0x24U, 14U, PINCTRL_FUNCTION_NONE, 4U },
	{ 31U, 0x0cU, 28U, 0x24U, 16U, PINCTRL_FUNCTION_NONE, 4U },
	{ 32U, 0x10U, 0U, 0x24U, 18U, PINCTRL_FUNCTION_NONE, 4U },
	{ 33U, 0x10U, 4U, 0x24U, 20U, PINCTRL_FUNCTION_NONE, 3U },
	{ 34U, 0x10U, 8U, 0x24U, 22U, PINCTRL_FUNCTION_NONE, 4U },
	{ 35U, 0x10U, 12U, 0x24U, 24U, PINCTRL_FUNCTION_NONE, 3U },
};

/*
 * The D0 stepping's pins.
 *
 * Values referenced: Raspberry Pi Linux rpi-6.12.y
 * drivers/pinctrl/bcm/pinctrl-bcm2712.c (bcm2712_d0_gpio_pin_regs and
 * bcm2712_d0_gpio_pin_funcs, rows 24 to 35, read as for C0).
 */
static const struct pinctrl_pin pinctrl_d0_pins[] = {
	{ 24U, 0x08U, 0U, 0x14U, 12U, 4U, PINCTRL_FUNCTION_NONE },
	{ 25U, 0x08U, 4U, 0x14U, 14U, 4U, PINCTRL_FUNCTION_NONE },
	{ 26U, 0x08U, 8U, 0x14U, 16U, 4U, PINCTRL_FUNCTION_NONE },
	{ 27U, 0x08U, 12U, 0x14U, 18U, 4U, PINCTRL_FUNCTION_NONE },
	{ 28U, 0x08U, 16U, 0x14U, 20U, PINCTRL_FUNCTION_NONE, PINCTRL_FUNCTION_NONE },
	{ 29U, 0x08U, 20U, 0x14U, 22U, PINCTRL_FUNCTION_NONE, PINCTRL_FUNCTION_NONE },
	{ 30U, 0x08U, 24U, 0x14U, 24U, PINCTRL_FUNCTION_NONE, 1U },
	{ 31U, 0x08U, 28U, 0x14U, 26U, PINCTRL_FUNCTION_NONE, 1U },
	{ 32U, 0x0cU, 0U, 0x14U, 28U, PINCTRL_FUNCTION_NONE, 1U },
	{ 33U, 0x0cU, 4U, 0x18U, 0U, PINCTRL_FUNCTION_NONE, 1U },
	{ 34U, 0x0cU, 8U, 0x18U, 2U, PINCTRL_FUNCTION_NONE, 1U },
	{ 35U, 0x0cU, 12U, 0x18U, 4U, PINCTRL_FUNCTION_NONE, 1U },
};

/* The two steppings, by the compatible string of their controller. */
static const struct pinctrl_variant pinctrl_variants[] = {
	{ "brcm,bcm2712c0-pinctrl", pinctrl_c0_pins, sizeof(pinctrl_c0_pins) / sizeof(pinctrl_c0_pins[0]) },
	{ "brcm,bcm2712d0-pinctrl", pinctrl_d0_pins, sizeof(pinctrl_d0_pins) / sizeof(pinctrl_d0_pins[0]) },
};

/* The tree's one pin controller. */
static struct pinctrl_state pinctrl;

static int apply_node(const struct drv_fdt *fdt, uint32_t node);
static unsigned read_pull(const struct drv_fdt *fdt, uint32_t node);
static int parse_gpio_name(const char *name, uint32_t length, unsigned *pin);
static const struct pinctrl_pin *find_pin(unsigned pin);
static unsigned function_selector(const struct pinctrl_pin *entry, const char *function);
static void write_field(unsigned offset, unsigned shift, uint32_t mask, uint32_t value);

/*
 * Finds and maps the pin controller of the tree.
 */
int
drv_rpi5_pinctrl_init(
	const struct drv_fdt *fdt)
{
	uint64_t address;
	uint64_t size;
	uint32_t node;
	unsigned variant;
	void *mapped;
	int error;

	/* Does nothing a second time. */
	if (pinctrl.ready)
		return 0;

	/* Finds the stepping's controller. */
	node = DRV_FDT_NO_NODE;
	error = ENODEV;
	for (variant = 0; variant < sizeof(pinctrl_variants) / sizeof(pinctrl_variants[0]); variant++) {
		error = drv_fdt_find_compatible(fdt, pinctrl_variants[variant].compatible, DRV_FDT_NO_NODE, &node);
		if (error == 0)
			break;
	}

	/* Refuses a tree without a controller the module knows. */
	if (error != 0) {
		kern_logf("rpi5-pinctrl: no known pin controller\n");
		return ENODEV;
	}

	/* Maps the registers, at least as far as the module reaches. */
	error = drv_fdt_reg(fdt, node, 0, &address, &size);
	if (error != 0)
		return error;
	pinctrl.size = size;
	if (size < PINCTRL_REGISTER_BYTES)
		size = PINCTRL_REGISTER_BYTES;
	error = kern_device_map(address, (size_t)size, KERN_DEVICE_UNCACHED, &mapped);
	if (error != 0)
		return error;

	/* Prepares the lock of the registers. */
	spin_init(&pinctrl.lock, LOCK_RANK_DEVICE, "rpi5 pinctrl");

	/* Succeeded: the pins can be set. */
	pinctrl.variant = &pinctrl_variants[variant];
	pinctrl.registers = mapped;
	pinctrl.ready = 1;
	return 0;
}

/*
 * Applies a pin group.
 */
int
drv_rpi5_pinctrl_apply(
	const struct drv_fdt *fdt,
	uint32_t group)
{
	const uint8_t *value;
	uint32_t length;
	uint32_t child;
	int error;

	/* Refuses a request before the controller was found. */
	if (!pinctrl.ready)
		return ENODEV;

	/* A group that names its pins itself is one node. */
	error = drv_fdt_property(fdt, group, "pins", &value, &length);
	if (error == 0) {
		error = apply_node(fdt, group);
		if (error != 0)
			return error;

		/* Succeeded: the group's pins are set. */
		return 0;
	}

	/* Otherwise each subnode names some of the group's pins. */
	child = DRV_FDT_NO_NODE;
	for (;;) {
		error = drv_fdt_child(fdt, group, child, &child);
		if (error != 0)
			break;

		/* Applies the subnode. */
		error = apply_node(fdt, child);
		if (error != 0)
			return error;
	}

	/* Succeeded: every subnode's pins are set. */
	return 0;
}

/*
 * Applies every group a node's pinctrl-0 names.
 */
int
drv_rpi5_pinctrl_apply_default(
	const struct drv_fdt *fdt,
	uint32_t node)
{
	const uint8_t *value;
	uint32_t length;
	uint32_t phandle;
	uint32_t group;
	unsigned index;
	int error;

	/* A node without pinctrl-0 asks for nothing. */
	error = drv_fdt_property(fdt, node, "pinctrl-0", &value, &length);
	if (error != 0)
		return 0;

	/* Applies each group the property names. */
	for (index = 0; (index + 1U) * 4U <= length; index++) {
		phandle = (uint32_t)drv_fdt_cells_value(value, index, 1U);
		error = drv_fdt_find_phandle(fdt, phandle, &group);
		if (error != 0)
			return error;

		/* Applies the group. */
		error = drv_rpi5_pinctrl_apply(fdt, group);
		if (error != 0)
			return error;
	}

	/* Succeeded: the node's pins are set. */
	return 0;
}

/*
 * Logs the controller's stepping and its registers, the function
 * selectors first and the pulls after them.
 */
void
drv_rpi5_pinctrl_report(
	const char *when)
{
	uint32_t words[PINCTRL_REPORT_WORDS];
	unsigned count;
	unsigned index;

	/* Nothing to show before the controller was found. */
	if (!pinctrl.ready)
		return;

	/* Reads the registers the tree gives the controller, as far as the report shows. */
	count = (unsigned)(pinctrl.size / 4U);
	if (count > PINCTRL_REPORT_WORDS)
		count = PINCTRL_REPORT_WORDS;
	for (index = 0; index < PINCTRL_REPORT_WORDS; index++) {
		words[index] = 0;
		if (index < count)
			words[index] = kern_mmio_read32(pinctrl.registers + 4U * index);
	}

	/* Prints them in one line. */
	kern_logf("rpi5-pinctrl: %s %s: %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
		  pinctrl.variant->compatible,
		  when,
		  words[0], words[1], words[2], words[3], words[4], words[5],
		  words[6], words[7], words[8], words[9], words[10], words[11]);
}

/*
 * Sets the function and the pull of each pin a node names.
 */
static int
apply_node(
	const struct drv_fdt *fdt,
	uint32_t node)
{
	const struct pinctrl_pin *entry;
	const uint8_t *function;
	const uint8_t *pins;
	uint32_t function_length;
	uint32_t pins_length;
	uint32_t start;
	uint32_t end;
	unsigned selector;
	unsigned pull;
	unsigned pin;
	int has_function;
	int error;

	/* Reads the pins, the function (which a pull-only group lacks) and the pull. */
	error = drv_fdt_property(fdt, node, "pins", &pins, &pins_length);
	if (error != 0)
		return error;
	error = drv_fdt_property(fdt, node, "function", &function, &function_length);
	has_function = 0;
	if (error == 0 && function_length != 0 && function[function_length - 1U] == 0)
		has_function = 1;
	pull = read_pull(fdt, node);

	/* Walks the pin names, each a string of the list. */
	start = 0;
	while (start < pins_length) {
		/* Finds the end of this name. */
		end = start;
		while (end < pins_length && pins[end] != '\0')
			end++;

		/* Refuses a pin the module does not know. */
		error = parse_gpio_name((const char *)pins + start, end - start, &pin);
		start = end + 1U;
		if (error != 0)
			return ENOTSUP;
		entry = find_pin(pin);
		if (entry == NULL)
			return ENOTSUP;

		/* Sets the function, refusing one the pin does not have. */
		if (has_function) {
			selector = function_selector(entry, (const char *)function);
			if (selector == PINCTRL_FUNCTION_NONE) {
				kern_logf("rpi5-pinctrl: GPIO %u has no function %s\n", pin, (const char *)function);
				return ENOTSUP;
			}

			/* Writes the selector. */
			write_field(entry->mux_offset, entry->mux_shift, PINCTRL_FUNCTION_MASK, selector);
		}

		/* Sets the pull the group asks for. */
		if (pull != PINCTRL_PULL_KEEP)
			write_field(entry->pad_offset, entry->pad_shift, PINCTRL_PULL_MASK, pull);
	}

	/* Succeeded: the node's pins are set. */
	return 0;
}

/* Reads the pull a node asks for, or PINCTRL_PULL_KEEP for none. */
static unsigned
read_pull(
	const struct drv_fdt *fdt,
	uint32_t node)
{
	const uint8_t *value;
	uint32_t length;
	int error;

	/* No pull at all. */
	error = drv_fdt_property(fdt, node, "bias-disable", &value, &length);
	if (error == 0)
		return PINCTRL_PULL_NONE;

	/* A pull-up. */
	error = drv_fdt_property(fdt, node, "bias-pull-up", &value, &length);
	if (error == 0)
		return PINCTRL_PULL_UP;

	/* A pull-down. */
	error = drv_fdt_property(fdt, node, "bias-pull-down", &value, &length);
	if (error == 0)
		return PINCTRL_PULL_DOWN;

	/* Succeeded: nothing asked, the pad stays as it is. */
	return PINCTRL_PULL_KEEP;
}

/* Reads a pin's number from a name of the form gpioN. */
static int
parse_gpio_name(
	const char *name,
	uint32_t length,
	unsigned *pin)
{
	unsigned number;
	uint32_t index;
	int differs;

	/* Refuses a name without the prefix or without a number. */
	if (length <= 4U)
		return EINVAL;
	differs = kern_memcmp(name, "gpio", 4U);
	if (differs != 0)
		return EINVAL;

	/* Reads the decimal digits. */
	number = 0;
	for (index = 4U; index < length; index++) {
		/* Refuses anything that is not a digit. */
		if (name[index] < '0' || name[index] > '9')
			return EINVAL;
		number = number * 10U + (unsigned)(name[index] - '0');
	}

	/* Succeeded: the pin's number. */
	*pin = number;
	return 0;
}

/* Finds a pin's entry in the stepping's table, or NULL. */
static const struct pinctrl_pin *
find_pin(
	unsigned pin)
{
	unsigned index;

	/* Looks at each pin the module knows, up to the pin's entry. */
	for (index = 0; index < pinctrl.variant->pin_count; index++) {
		/* Stops at the pin's entry. */
		if (pinctrl.variant->pins[index].pin == pin)
			break;
	}

	/* The module does not know the pin. */
	if (index == pinctrl.variant->pin_count)
		return NULL;

	/* Succeeded: the pin's entry. */
	return &pinctrl.variant->pins[index];
}

/* Reports the selector of a function on a pin, or PINCTRL_FUNCTION_NONE. */
static unsigned
function_selector(
	const struct pinctrl_pin *entry,
	const char *function)
{
	int differs;

	/* The GPIO function is selector 0 everywhere. */
	differs = kern_strcmp(function, "gpio");
	if (differs == 0)
		return PINCTRL_FUNCTION_GPIO;

	/* The SD controller's function. */
	differs = kern_strcmp(function, "sd2");
	if (differs == 0)
		return entry->sd2;

	/* The UART's function. */
	differs = kern_strcmp(function, "uart0");
	if (differs == 0)
		return entry->uart0;

	/* Succeeded: a function the module does not know. */
	return PINCTRL_FUNCTION_NONE;
}

/* Rewrites one field of a register under the lock. */
static void
write_field(
	unsigned offset,
	unsigned shift,
	uint32_t mask,
	uint32_t value)
{
	unsigned long enabled;
	uint32_t bits;

	/* Reads the register, replaces the field and writes it back. */
	enabled = spin_lock_irqsave(&pinctrl.lock);

	bits = kern_mmio_read32(pinctrl.registers + offset);
	bits &= ~(mask << shift);
	bits |= (value & mask) << shift;
	kern_mmio_write32(pinctrl.registers + offset, bits);

	spin_unlock_irqrestore(&pinctrl.lock, enabled);
}

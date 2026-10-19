/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The receiving side of a memory-mapped 16550-compatible UART.
 *
 * The device tree says where the registers are, how far apart they lie
 * (reg-shift) and how wide an access must be (reg-io-width), and which
 * interrupt the UART raises.  The line's speed and framing are the
 * firmware's: neither the divisor nor the line control register is written,
 * so the console the firmware set up keeps working.
 *
 * Reading is by interrupt.  The handler drains what the receiver holds, a
 * bounded number of bytes per interrupt, and hands each byte to the
 * receiver function its starter gave.
 *
 * The Synopsys DesignWare UART is a 16550 with one more interrupt: a write
 * to the line control register while the UART is busy is ignored and raises
 * a "busy detect" interrupt, which stays raised until the UART status
 * register is read.  Nothing here writes that register, but the firmware
 * may have, so the handler clears the condition when it finds it.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <drivers/serial/ns16550.h>

#include <drivers/generic/fdt.h>
#include <kern/device-io.h>
#include <kern/irq.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

/*
 * The registers, as register numbers before the device tree's reg-shift
 * spaces them out, and the bits of them the driver uses.
 *
 * Values referenced: the 16550 UART register map as the National
 * Semiconductor PC16550D data sheet defines it (receiver buffer 0,
 * interrupt enable 1, interrupt identification 2, line status 5; the
 * received-data-available enable bit, the interrupt identity field, the
 * data-ready status bit).
 */
#define NS16550_RECEIVER_BUFFER		0U
#define NS16550_INTERRUPT_ENABLE	1U
#define NS16550_INTERRUPT_IDENTITY	2U
#define NS16550_LINE_STATUS		5U
#define NS16550_ENABLE_RECEIVED_DATA	0x01U
#define NS16550_IDENTITY_MASK		0x0fU
#define NS16550_STATUS_DATA_READY	0x01U

/*
 * The DesignWare UART's status register and the interrupt identity of its
 * busy-detect condition.
 *
 * Values referenced: Linux 6.8.12 drivers/tty/serial/8250/8250_dw.c (the
 * UART status register's register number and the busy-detect value of the
 * interrupt identification register).
 */
#define DW_UART_STATUS			0x1fU
#define DW_UART_IDENTITY_BUSY		0x07U

/*
 * How many bytes one interrupt takes.  A line that never stops delivering
 * must not keep the processor in the handler for ever; what is left raises
 * the interrupt again.
 */
#define NS16550_DRAIN_LIMIT		64U

/*
 * How much address space the registers are mapped with when the tree gives
 * no size: the sixteen-register window at the widest spacing.
 */
#define NS16550_DEFAULT_WINDOW		0x100U

/*
 * The device-tree compatible strings of the UARTs the driver takes, in the
 * order they are looked for.  The second field says whether the UART is a
 * DesignWare one with the busy-detect interrupt.
 */
struct ns16550_compatible {
	const char *name;
	bool busy_detect;
};

/*
 * The UARTs the driver recognizes, fixed at build time.
 */
static const struct ns16550_compatible ns16550_compatibles[] = {
	{"ns16550a", false},
	{"ns16550", false},
	{"snps,dw-apb-uart", true}
};

static int find_uart(const struct drv_fdt *fdt, uint32_t *node, bool *busy_detect);
static uint32_t read_cell_property(const struct drv_fdt *fdt, uint32_t node, const char *name, uint32_t fallback, int *error);
static uint32_t read_register(const struct drv_ns16550 *port, unsigned number);
static void write_register(const struct drv_ns16550 *port, unsigned number, uint32_t value);
static void drain_receiver(struct drv_ns16550 *port, bool deliver);
static void uart_interrupt(int irq, kern_irq_ack_t acknowledge, void *argument);

/*
 * Finds the first present UART the device tree describes and maps it.
 */
int
drv_ns16550_probe(
	const struct drv_fdt *fdt,
	struct drv_ns16550 *port)
{
	uint32_t node;
	bool busy_detect;
	uint64_t physical;
	uint64_t window;
	uint32_t shift;
	uint32_t width;
	uint32_t irq;
	void *mapped;
	int error;

	/* Refuses a missing tree or result. */
	if (fdt == NULL || port == NULL)
		return EINVAL;

	/* Finds the UART's node and whether it is a DesignWare one. */
	error = find_uart(fdt, &node, &busy_detect);
	if (error != 0)
		return error;

	/* Reads where the registers are; a missing size takes the whole window. */
	error = drv_fdt_reg(fdt, node, 0, &physical, &window);
	if (error != 0)
		return error;
	if (window == 0)
		window = NS16550_DEFAULT_WINDOW;

	/* Reads how far apart the registers are; one byte apart when unstated. */
	shift = read_cell_property(fdt, node, "reg-shift", 0, &error);
	if (error != 0)
		return error;
	if (shift > 4)
		return EINVAL;

	/* Reads how wide an access is; a byte when unstated. */
	width = read_cell_property(fdt, node, "reg-io-width", 1, &error);
	if (error != 0)
		return error;
	if (width != 1 && width != 4)
		return EINVAL;

	/* Reads the interrupt the UART raises at its controller. */
	irq = read_cell_property(fdt, node, "interrupts", 0, &error);
	if (error != 0)
		return error;
	if (irq == 0)
		return ENOENT;

	/* Maps the registers, uncached as every device register is. */
	error = kern_device_map(physical, (size_t)window, KERN_DEVICE_UNCACHED, &mapped);
	if (error != 0)
		return error;

	/* Fills the port; input is started separately. */
	port->physical = physical;
	port->base = (uintptr_t)mapped;
	port->reg_shift = shift;
	port->io_width = width;
	port->irq = (int)irq;
	port->busy_detect = busy_detect;
	port->receive = NULL;
	port->input_started = false;

	/* Succeeded: the UART's registers are reachable. */
	return 0;
}

/*
 * Starts taking received bytes by interrupt.
 */
int
drv_ns16550_start_input(
	struct drv_ns16550 *port,
	drv_ns16550_receive_t receive)
{
	uint32_t enable;
	int error;

	/* Refuses a missing port or receiver, and a second start. */
	if (port == NULL || receive == NULL)
		return EINVAL;
	if (port->input_started)
		return EBUSY;

	/* Installs the handler; the interrupt stays masked until the end. */
	error = kern_irq_register(port->irq, uart_interrupt, port);
	if (error != 0)
		return error;

	/*
	 * Drops what the line carried before anything could read it: a line
	 * connected since power-on may have carried anything, and the first
	 * byte the reader sees must be the first one typed to it.
	 */
	drain_receiver(port, false);

	/*
	 * Publishes the receiver before the interrupt can run: the handler
	 * delivers only once input_started is set.
	 */
	port->receive = receive;
	port->input_started = true;

	/* Has the UART raise its interrupt when a byte arrives. */
	enable = read_register(port, NS16550_INTERRUPT_ENABLE);
	enable |= NS16550_ENABLE_RECEIVED_DATA;
	write_register(port, NS16550_INTERRUPT_ENABLE, enable);

	/* Lets the interrupt through to the handler. */
	kern_irq_unmask(port->irq);

	/* Succeeded: received bytes go to the receiver. */
	return 0;
}

/* Finds the first present node of a recognized UART. */
static int
find_uart(
	const struct drv_fdt *fdt,
	uint32_t *node,
	bool *busy_detect)
{
	uint32_t candidate;
	size_t index;
	bool present;
	int error;

	/* Looks for each recognized kind in turn. */
	for (index = 0;
	     index < sizeof(ns16550_compatibles) / sizeof(ns16550_compatibles[0]);
	     index++) {
		/* Walks the nodes of this kind until a present one is found. */
		candidate = DRV_FDT_NO_NODE;
		for (;;) {
			/* Finds the next node of this kind; none left moves to the next kind. */
			error = drv_fdt_find_compatible(fdt, ns16550_compatibles[index].name, candidate, &candidate);
			if (error == ENOENT)
				break;
			if (error != 0)
				return error;

			/* Skips a node the tree marks as not present. */
			present = drv_fdt_node_enabled(fdt, candidate);
			if (!present)
				continue;

			/* Succeeded: the first present UART of this kind. */
			*node = candidate;
			*busy_detect = ns16550_compatibles[index].busy_detect;
			return 0;
		}
	}

	/* The tree describes no present UART the driver knows. */
	return ENOENT;
}

/* Reads the first cell of a property, or the fallback when it is absent. */
static uint32_t
read_cell_property(
	const struct drv_fdt *fdt,
	uint32_t node,
	const char *name,
	uint32_t fallback,
	int *error)
{
	const uint8_t *value;
	uint32_t length;
	uint32_t number;
	int lookup;

	/* Looks the property up. */
	*error = 0;
	lookup = drv_fdt_property(fdt, node, name, &value, &length);

	/* An absent property takes the fallback. */
	if (lookup == ENOENT)
		return fallback;

	/* Reports a tree the lookup could not read. */
	if (lookup != 0) {
		*error = lookup;
		return 0;
	}

	/* Refuses a value shorter than one cell. */
	if (length < 4) {
		*error = EINVAL;
		return 0;
	}

	/* Reads the first cell. */
	number = (uint32_t)drv_fdt_cells_value(value, 0, 1);

	/* Succeeded: the property's first cell. */
	return number;
}

/* Reads one register at its spacing and with the access width the UART needs. */
static uint32_t
read_register(
	const struct drv_ns16550 *port,
	unsigned number)
{
	uintptr_t address;
	uint32_t value;

	/* Places the register by the tree's spacing. */
	address = port->base + ((uintptr_t)number << port->reg_shift);

	/* Reads it as wide as the UART requires. */
	if (port->io_width == 4) {
		value = kern_mmio_read32((const volatile void *)address);
	} else {
		value = kern_mmio_read8((const volatile void *)address);
	}

	/* Succeeded: the register's value. */
	return value;
}

/* Writes one register at its spacing and with the access width the UART needs. */
static void
write_register(
	const struct drv_ns16550 *port,
	unsigned number,
	uint32_t value)
{
	uintptr_t address;

	/* Places the register by the tree's spacing. */
	address = port->base + ((uintptr_t)number << port->reg_shift);

	/* Writes it as wide as the UART requires. */
	if (port->io_width == 4) {
		kern_mmio_write32((volatile void *)address, value);
	} else {
		kern_mmio_write8((volatile void *)address, (uint8_t)value);
	}
}

/*
 * Takes what the receiver holds, at most one interrupt's worth, and hands
 * each byte to the receiver when deliver is set.
 */
static void
drain_receiver(
	struct drv_ns16550 *port,
	bool deliver)
{
	unsigned drained;
	uint32_t status;
	uint8_t byte;

	/* Takes each byte the receiver holds. */
	for (drained = 0; drained < NS16550_DRAIN_LIMIT; drained++) {
		/* Stops once the receiver holds nothing. */
		status = read_register(port, NS16550_LINE_STATUS);
		if ((status & NS16550_STATUS_DATA_READY) == 0)
			return;

		/* Takes the byte, which also frees its place in the receiver. */
		byte = (uint8_t)read_register(port, NS16550_RECEIVER_BUFFER);

		/* Hands the byte on, unless it is one from before the start. */
		if (deliver)
			port->receive(byte);
	}
}

/*
 * Serves the UART's interrupt: clears a DesignWare busy-detect condition,
 * hands the received bytes on and ends the interrupt.
 */
static void
uart_interrupt(
	int irq,
	kern_irq_ack_t acknowledge,
	void *argument)
{
	struct drv_ns16550 *port;
	uint32_t identity;

	(void)irq;

	/* Finds the port the handler was installed for. */
	port = (struct drv_ns16550 *)argument;

	/*
	 * Reading the status register is what ends a busy-detect interrupt;
	 * left alone, it would be raised again for ever.
	 */
	identity = read_register(port, NS16550_INTERRUPT_IDENTITY) & NS16550_IDENTITY_MASK;
	if (port->busy_detect && identity == DW_UART_IDENTITY_BUSY)
		(void)read_register(port, DW_UART_STATUS);

	/* Hands what arrived to the receiver. */
	drain_receiver(port, port->input_started);

	/* Ends the interrupt at its controller. */
	kern_irq_send_eoi(acknowledge);
}

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the Intel VMD driver's planning half (WS298).
 *
 * A small model of the configuration space behind a VMD holds a few
 * functions; BARs keep only the address bits their size allows, as
 * hardware does.  The plan is run against the Latitude 5330's layout and
 * against the cases it must refuse, and the message table is exercised.
 */

#include <drivers/pci/pci-vmd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The kernel's error numbers (include/uapi/errno.h), which the planning
 * half, compiled freestanding, reports; the host's <errno.h> numbers differ.
 */
#define KERNEL_EINVAL	3
#define KERNEL_ENOSPC	8

/* The functions one model holds, and the writes it remembers per function. */
#define MODEL_FUNCTIONS	16U
#define MODEL_LOG	64U

/*
 * One remembered configuration write: its offset, width and value.
 */
struct model_write {
	unsigned offset;
	unsigned width;
	uint32_t value;
};

/*
 * One function of the model: its address, its 4 KiB of registers, the
 * size and kind of each BAR, and the writes made to it in order.
 */
struct model_function {
	unsigned bus;
	unsigned device;
	unsigned function;
	uint8_t space[4096];
	uint64_t bar_size[6];
	unsigned bar_kind[6];
	uint32_t upper_keep[6];
	struct model_write log[MODEL_LOG];
	unsigned log_count;
};

/*
 * The model of one VMD's configuration space.
 */
struct model {
	struct model_function functions[MODEL_FUNCTIONS];
	unsigned count;
};

/* The kinds of a BAR in the model. */
enum {
	BAR_NONE = 0,
	BAR_MEM32,
	BAR_MEM64,
	BAR_MEM64_PREFETCH,
	BAR_IO,
	BAR_UPPER
};

/* The number of failed checks. */
static unsigned failures;

static void check(int condition, const char *what);
static struct model_function *model_find(struct model *model, unsigned bus, unsigned device, unsigned function);
static struct model_function *model_add(struct model *model, unsigned bus, unsigned device, unsigned function, uint32_t identity, uint32_t class_code, uint8_t header);
static void put16(struct model_function *function, unsigned offset, uint16_t value);
static void put32(struct model_function *function, unsigned offset, uint32_t value);
static uint32_t get(const struct model_function *function, unsigned offset, unsigned width);
static void model_bar(struct model_function *function, unsigned index, unsigned kind, uint64_t size, uint64_t address);
static void model_capability(struct model_function *function, unsigned position, unsigned id, unsigned next);
static int model_read(void *context, unsigned bus, unsigned device, unsigned function, unsigned offset, unsigned width, uint32_t *value);
static int model_write(void *context, unsigned bus, unsigned device, unsigned function, unsigned offset, unsigned width, uint32_t value);
static struct model_function *add_root_port(struct model *model, unsigned device);
static struct model_function *add_nvme(struct model *model, unsigned bus);
static void domain_5330(struct drv_pci_vmd_domain *domain);
static void test_bus_start(void);
static void test_config_offset(void);
static void test_5330(void);
static void test_many_bars(void);
static void test_ports(void);
static void test_no_window(void);
static void test_membar2_head(void);
static void test_messages(void);
static int run_nothing(void *argument);

/*
 * Runs every test and reports the verdict on the last line.
 */
int
main(void)
{
	/* Runs the tests, each counting its own failures. */
	test_bus_start();
	test_config_offset();
	test_5330();
	test_many_bars();
	test_ports();
	test_no_window();
	test_membar2_head();
	test_messages();

	/* Reports a failed run. */
	if (failures != 0) {
		printf("host-vmd-plan: FAIL (%u)\n", failures);
		return 1;
	}

	/* Succeeded: every check held. */
	printf("host-vmd-plan: PASS\n");
	return 0;
}

/* Counts and reports one failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A check that holds says nothing. */
	if (condition)
		return;

	/* Names the failed check and counts it. */
	printf("FAIL: %s\n", what);
	failures++;
}

/* Finds a function of the model by its address. */
static struct model_function *
model_find(
	struct model *model,
	unsigned bus,
	unsigned device,
	unsigned function)
{
	unsigned index;

	/* Looks at each function the model holds. */
	for (index = 0; index < model->count; index++) {
		if (model->functions[index].bus == bus &&
		    model->functions[index].device == device &&
		    model->functions[index].function == function)
			return &model->functions[index];
	}

	/* Nothing answers at the address. */
	return NULL;
}

/* Adds a function with its identity, class and header type. */
static struct model_function *
model_add(
	struct model *model,
	unsigned bus,
	unsigned device,
	unsigned function,
	uint32_t identity,
	uint32_t class_code,
	uint8_t header)
{
	struct model_function *added;

	added = &model->functions[model->count];
	model->count++;
	memset(added, 0, sizeof(*added));
	added->bus = bus;
	added->device = device;
	added->function = function;
	put32(added, 0x00, identity);
	put32(added, 0x08, class_code << 8);
	added->space[0x0e] = header;
	return added;
}

/* Stores a 16-bit register. */
static void
put16(
	struct model_function *function,
	unsigned offset,
	uint16_t value)
{
	function->space[offset] = (uint8_t)value;
	function->space[offset + 1U] = (uint8_t)(value >> 8);
}

/* Stores a 32-bit register. */
static void
put32(
	struct model_function *function,
	unsigned offset,
	uint32_t value)
{
	put16(function, offset, (uint16_t)value);
	put16(function, offset + 2U, (uint16_t)(value >> 16));
}

/* Reads a register of 1, 2 or 4 bytes. */
static uint32_t
get(
	const struct model_function *function,
	unsigned offset,
	unsigned width)
{
	uint32_t value;
	unsigned index;

	/* Gathers the bytes, the lowest first. */
	value = 0;
	for (index = 0; index < width; index++)
		value |= (uint32_t)function->space[offset + index] << (8U * index);

	/* Reports the register. */
	return value;
}

/* Gives a function a BAR of a kind and size, holding an address. */
static void
model_bar(
	struct model_function *function,
	unsigned index,
	unsigned kind,
	uint64_t size,
	uint64_t address)
{
	uint32_t low;

	function->bar_kind[index] = kind;
	function->bar_size[index] = size;
	low = (uint32_t)address;
	if (kind == BAR_IO)
		low |= 1U;
	if (kind == BAR_MEM64 || kind == BAR_MEM64_PREFETCH)
		low |= 4U;
	if (kind == BAR_MEM64_PREFETCH)
		low |= 8U;
	put32(function, 0x10 + index * 4U, low);
	if (kind == BAR_MEM64 || kind == BAR_MEM64_PREFETCH) {
		function->bar_kind[index + 1U] = BAR_UPPER;
		function->bar_size[index + 1U] = size;
		function->upper_keep[index + 1U] = 0xffffffffU;
		put32(function, 0x14 + index * 4U, (uint32_t)(address >> 32));
	}
}

/* Links a capability into a function's list. */
static void
model_capability(
	struct model_function *function,
	unsigned position,
	unsigned id,
	unsigned next)
{
	function->space[0x06] |= 0x10;
	if (function->space[0x34] == 0)
		function->space[0x34] = (uint8_t)position;
	function->space[position] = (uint8_t)id;
	function->space[position + 1U] = (uint8_t)next;
}

/* Reads a register of the model; a missing function reads as all ones. */
static int
model_read(
	void *context,
	unsigned bus,
	unsigned device,
	unsigned function,
	unsigned offset,
	unsigned width,
	uint32_t *value)
{
	struct model_function *found;

	found = model_find(context, bus, device, function);
	if (found == NULL) {
		*value = drv_pci_vmd_absent_value(width);
		return 0;
	}

	*value = get(found, offset, width);
	return 0;
}

/* Writes a register of the model; a BAR keeps only the bits its size allows. */
static int
model_write(
	void *context,
	unsigned bus,
	unsigned device,
	unsigned function,
	unsigned offset,
	unsigned width,
	uint32_t value)
{
	struct model_function *found;
	uint64_t mask;
	uint32_t kept;
	unsigned bars;
	unsigned index;
	unsigned byte;

	found = model_find(context, bus, device, function);
	if (found == NULL)
		return 0;

	if (found->log_count < MODEL_LOG) {
		found->log[found->log_count].offset = offset;
		found->log[found->log_count].width = width;
		found->log[found->log_count].value = value;
		found->log_count++;
	}

	bars = (found->space[0x0e] & 0x7fU) == 1U ? 2U : 6U;
	if (offset >= 0x10 && offset < 0x10 + bars * 4U && width == 4U) {
		index = (offset - 0x10) / 4U;
		kept = get(found, offset, 4U);
		switch (found->bar_kind[index]) {
		case BAR_NONE:
			return 0;
		case BAR_IO:
			mask = ~(found->bar_size[index] - 1U);
			put32(found, offset, ((uint32_t)mask & value & ~3U) | 1U);
			return 0;
		case BAR_UPPER:
			mask = ~(found->bar_size[index] - 1U);
			put32(found, offset, (uint32_t)(mask >> 32) & value & found->upper_keep[index]);
			return 0;
		default:
			mask = ~(found->bar_size[index] - 1U);
			put32(found, offset, ((uint32_t)mask & value & ~15U) | (kept & 15U));
			return 0;
		}
	}

	/* Stores any other register byte by byte. */
	for (byte = 0; byte < width; byte++)
		found->space[offset + byte] = (uint8_t)(value >> (8U * byte));

	/* Succeeded: the register is written. */
	return 0;
}

/*
 * Adds a root port like the 5330's (8086:464d, multi-function header) with
 * MSI enabled at 0x80, PCI Express at 0x40 with its slot and root
 * interrupts enabled, the firmware's bus numbers and window, and decoding on.
 */
static struct model_function *
add_root_port(
	struct model *model,
	unsigned device)
{
	struct model_function *port;

	port = model_add(model, 224, device, 0, 0x464d8086U, 0x060400U, 0x81);
	put16(port, 0x04, 0x0407);
	model_capability(port, 0x40, 0x10, 0x80);
	put16(port, 0x40 + 0x18, 0x1031);
	put16(port, 0x40 + 0x1c, 0x0008);
	model_capability(port, 0x80, 0x05, 0x00);
	put16(port, 0x80 + 0x02, 0x0001);
	put32(port, 0x18, 0x00e1e100U);
	put32(port, 0x20, 0x72007200U);
	return port;
}

/* Adds an NVMe controller like the 5330's: one 64-bit BAR0 of 16 KiB, PM at 0x80 in D0. */
static struct model_function *
add_nvme(
	struct model *model,
	unsigned bus)
{
	struct model_function *nvme;

	nvme = model_add(model, bus, 0, 0, 0x000c1e0fU, 0x010802U, 0x00);
	put16(nvme, 0x04, 0x0406);
	model_bar(nvme, 0, BAR_MEM64, 0x4000, 0x72000000U);
	model_capability(nvme, 0x80, 0x01, 0x00);
	return nvme;
}

/* Fills the 5330's domain: buses 224 to 255, MEMBAR1 and MEMBAR2 past its head. */
static void
domain_5330(
	struct drv_pci_vmd_domain *domain)
{
	domain->bus_start = 224;
	domain->bus_end = 255;
	domain->windows[0].base = 0x72000000U;
	domain->windows[0].length = 0x2000000U;
	domain->windows[1].base = UINT64_C(0x6055100000) + 0x2000U;
	domain->windows[1].length = 0x100000U - 0x2000U;
	domain->window_count = 2;
}

/* The bus start follows the restriction bit and the configuration field. */
static void
test_bus_start(void)
{
	unsigned start;

	check(drv_pci_vmd_bus_start(0x0000, 0xfe01, &start) == 0 && start == 0, "bus start without restriction is 0");
	check(drv_pci_vmd_bus_start(0x0001, 0x0000, &start) == 0 && start == 0, "bus start field 0 is 0");
	check(drv_pci_vmd_bus_start(0x0001, 0x0100, &start) == 0 && start == 128, "bus start field 1 is 128");
	check(drv_pci_vmd_bus_start(0x0001, 0xfe01, &start) == 0 && start == 224, "bus start of the 5330 is 224");
	check(drv_pci_vmd_bus_start(0x0001, 0x0300, &start) == KERNEL_EINVAL, "bus start field 3 is refused");
}

/* Register offsets, and the accesses the CFGBAR does not decode. */
static void
test_config_offset(void)
{
	uint64_t offset;

	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 224, 6, 0, 0x10, 4, &offset) == 0 && offset == (6U << 15) + 0x10U, "offset of the root port's BAR0");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 225, 0, 0, 0x2, 2, &offset) == 0 && offset == (1U << 20) + 2U, "offset on the second bus");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 255, 31, 7, 0xffc, 4, &offset) == 0 && offset == 0x1fffffcU, "last register of the CFGBAR");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 223, 0, 0, 0, 4, &offset) == KERNEL_EINVAL, "bus below the start is refused");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 256, 0, 0, 0, 4, &offset) == KERNEL_EINVAL, "bus past the end is refused");
	check(drv_pci_vmd_config_offset(224, 255, 0x200000U, 226, 0, 0, 0, 4, &offset) == KERNEL_EINVAL, "bus past a small CFGBAR is refused");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 224, 0, 0, 0xffe, 4, &offset) == KERNEL_EINVAL, "access past the function's space is refused");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 224, 0, 0, 0x2, 4, &offset) == KERNEL_EINVAL, "unaligned access is refused");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 224, 32, 0, 0, 4, &offset) == KERNEL_EINVAL, "device 32 is refused");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 224, 0, 8, 0, 4, &offset) == KERNEL_EINVAL, "function 8 is refused");
	check(drv_pci_vmd_config_offset(224, 255, 0x2000000U, 224, 0, 0, 0, 3, &offset) == KERNEL_EINVAL, "width 3 is refused");
	check(drv_pci_vmd_absent_value(1) == 0xffU, "an absent byte reads 0xff");
	check(drv_pci_vmd_absent_value(2) == 0xffffU, "an absent word reads 0xffff");
	check(drv_pci_vmd_absent_value(4) == 0xffffffffU, "an absent double word reads all ones");
}

/* The 5330's layout: one root port at 06.0 with the NVMe behind it. */
static void
test_5330(void)
{
	static struct model model;
	static struct drv_pci_vmd_plan plan;
	struct drv_pci_vmd_config_access access;
	struct drv_pci_vmd_domain domain;
	struct model_function *port;
	struct model_function *nvme;
	static const struct model_write closing[] = {
		{ 0x30, 4, 0x0000ffffU },
		{ 0x1c, 2, 0x00f0U },
		{ 0x30, 4, 0 },
		{ 0x20, 4, 0x0000fff0U },
		{ 0x2c, 4, 0 },
		{ 0x24, 4, 0x0000fff0U },
		{ 0x28, 4, 0xffffffffU },
	};
	unsigned index;
	unsigned found;

	memset(&model, 0, sizeof(model));
	port = add_root_port(&model, 6);
	nvme = add_nvme(&model, 0xe1);
	domain_5330(&domain);
	access.read = model_read;
	access.write = model_write;
	access.context = &model;

	check(drv_pci_vmd_plan_domain(&access, &domain, &plan) == 0, "5330: the plan succeeds");
	check(plan.port_count == 1 && plan.endpoints == 0 && plan.ports_dropped == 0, "5330: one root port");
	check(plan.ports[0].device == 6 && plan.ports[0].function == 0, "5330: the port is 06.0");
	check(plan.ports[0].state == DRV_PCI_VMD_PORT_USED, "5330: the port is used");
	check(plan.ports[0].firmware_buses == 0x00e1e100U && plan.ports[0].firmware_window == 0x72007200U, "5330: the firmware's values are kept for the log");
	check(plan.ports[0].secondary == 225, "5330: the secondary bus is e1");
	check(get(port, 0x18, 1) == 224 && get(port, 0x19, 1) == 225 && get(port, 0x1a, 1) == 225, "5330: bus numbers e0/e1/e1");
	check(plan.ports[0].window_base == 0x72000000U && plan.ports[0].window_length == 0x100000U, "5330: window 0x72000000, 1 MiB");
	check(get(port, 0x20, 4) == 0x72007200U, "5330: memory window register");
	check((get(port, 0x24, 4) & 0xffffU) > (get(port, 0x24, 4) >> 16), "5330: prefetchable window closed");
	check(get(port, 0x1c, 2) == 0x00f0U, "5330: I/O window closed");
	check((get(port, 0x04, 2) & 0x7U) == 0x6U, "5330: the port decodes memory and masters, no I/O");
	check((get(port, 0x82, 2) & 1U) == 0, "5330: the port's MSI is off");
	check((get(port, 0x58, 2) & 0x30U) == 0 && (get(port, 0x58, 2) & 0x1000U) != 0, "5330: slot interrupts off, other slot bits kept");
	check((get(port, 0x5c, 2) & 0x8U) == 0, "5330: root PME interrupt off");
	check(plan.ports[0].bar_count == 1 && plan.ports[0].bars[0].size == 0x4000U && plan.ports[0].bars[0].wide, "5330: the NVMe BAR0 is 64-bit 16 KiB");
	check(get(nvme, 0x10, 4) == 0x72000004U && get(nvme, 0x14, 4) == 0, "5330: the NVMe BAR0 is 0x72000000");
	check((get(nvme, 0x04, 2) & 0x7U) == 0, "5330: the NVMe decodes nothing until its driver enables it");

	/* A function with 42 address bits reads zeros above them; the size must not take them in. */
	memset(&model, 0, sizeof(model));
	(void)add_root_port(&model, 6);
	nvme = add_nvme(&model, 0xe1);
	nvme->upper_keep[1] = 0x3ffU;
	check(drv_pci_vmd_plan_domain(&access, &domain, &plan) == 0, "42-bit: the plan succeeds");
	check(plan.ports[0].bar_count == 1 && plan.ports[0].bars[0].size == 0x4000U, "42-bit: the BAR is still 16 KiB");
	check(get(nvme, 0x10, 4) == 0x72000004U, "42-bit: the BAR is placed");

	/* The windows are closed in the order and with the widths of the procedure. */
	found = 0;
	for (index = 0; index < port->log_count && found < sizeof(closing) / sizeof(closing[0]); index++) {
		if (port->log[index].offset == closing[found].offset &&
		    port->log[index].width == closing[found].width &&
		    port->log[index].value == closing[found].value)
			found++;
	}
	check(found == sizeof(closing) / sizeof(closing[0]), "5330: windows closed in order and width");
	for (index = 0; index < port->log_count; index++) {
		if (port->log[index].offset == 0x1c)
			check(port->log[index].width == 2U, "5330: the I/O window is written 16 bits wide");
	}
}

/* One function with several kinds of BAR behind one port. */
static void
test_many_bars(void)
{
	static struct model model;
	static struct drv_pci_vmd_plan plan;
	struct drv_pci_vmd_config_access access;
	struct drv_pci_vmd_domain domain;
	struct model_function *device;

	memset(&model, 0, sizeof(model));
	(void)add_root_port(&model, 6);
	device = model_add(&model, 0xe1, 0, 0, 0x12348086U, 0x010802U, 0x00);
	model_bar(device, 0, BAR_MEM32, 0x1000, 0x10000000U);
	model_bar(device, 1, BAR_MEM64_PREFETCH, 0x10000, 0x20000000U);
	model_bar(device, 3, BAR_IO, 0x100, 0x3000);
	model_bar(device, 4, BAR_MEM32, 0x100, 0x30000000U);
	domain_5330(&domain);
	access.read = model_read;
	access.write = model_write;
	access.context = &model;

	check(drv_pci_vmd_plan_domain(&access, &domain, &plan) == 0, "bars: the plan succeeds");
	check(plan.ports[0].state == DRV_PCI_VMD_PORT_USED && plan.ports[0].bar_count == 3, "bars: three memory BARs recorded");
	check(get(device, 0x14, 4) == 0x7200000cU && get(device, 0x18, 4) == 0, "bars: the 64 KiB prefetchable BAR goes first");
	check(get(device, 0x10, 4) == 0x72010000U, "bars: the 4 KiB BAR follows");
	check(get(device, 0x20, 4) == 0x72011000U, "bars: the 256-byte BAR takes a page of its own");
	check(get(device, 0x1c, 4) == 0x1U, "bars: the I/O BAR is cleared");
	check(plan.ports[0].window_length == 0x100000U, "bars: the window is 1 MiB");
}

/* Ports that cannot be used: empty, past the buses, with a switch, with a BAR, not in D0. */
static void
test_ports(void)
{
	static struct model model;
	static struct drv_pci_vmd_plan plan;
	struct drv_pci_vmd_config_access access;
	struct drv_pci_vmd_domain domain;
	struct model_function *empty;
	struct model_function *switched;
	struct model_function *barred;
	struct model_function *asleep;
	struct model_function *sleeper;
	struct model_function *beyond;
	struct model_function *nvme;
	struct model_function *below_switch;

	memset(&model, 0, sizeof(model));
	(void)add_root_port(&model, 1);
	nvme = add_nvme(&model, 0xe1);
	empty = add_root_port(&model, 2);
	switched = add_root_port(&model, 3);
	below_switch = model_add(&model, 0xe3, 0, 0, 0x11118086U, 0x060400U, 0x01);
	put32(below_switch, 0x18, 0x00e5e4e3U);
	barred = add_root_port(&model, 4);
	model_bar(barred, 0, BAR_MEM32, 0x1000, 0x50000000U);
	asleep = add_root_port(&model, 5);
	sleeper = add_nvme(&model, 0xe4);
	put16(sleeper, 0x84, 0x0003);
	beyond = add_root_port(&model, 6);
	(void)add_nvme(&model, 0xe5);
	domain_5330(&domain);
	domain.bus_end = 228;
	access.read = model_read;
	access.write = model_write;
	access.context = &model;

	check(drv_pci_vmd_plan_domain(&access, &domain, &plan) == 0, "ports: the plan succeeds");
	check(plan.port_count == 6, "ports: six root ports");
	check(plan.ports[0].state == DRV_PCI_VMD_PORT_USED && plan.ports[0].secondary == 225, "ports: the first port is used on e1");
	check(get(nvme, 0x10, 4) == 0x72000004U, "ports: the NVMe behind the first port is placed");
	check(plan.ports[1].state == DRV_PCI_VMD_PORT_USED && plan.ports[1].secondary == 226 && plan.ports[1].window_length == 0, "ports: the empty port keeps its bus and has no window");
	check(get(empty, 0x19, 1) == 226 && (get(empty, 0x04, 2) & 0x6U) == 0, "ports: the empty port decodes nothing");
	check(get(empty, 0x20, 4) == 0x0000fff0U, "ports: the empty port's window stays closed");
	check(plan.ports[2].state == DRV_PCI_VMD_PORT_SWITCH, "ports: a switch is refused");
	check(get(switched, 0x18, 4) == 0 && get(switched, 0x20, 4) == 0x0000fff0U, "ports: the switch's port has no buses and a closed window");
	check(plan.ports[3].state == DRV_PCI_VMD_PORT_OWN_BAR && get(barred, 0x10, 4) == 0, "ports: a port with its own BAR is refused and the BAR cleared");
	check(get(barred, 0x18, 4) == 0, "ports: the barred port has no buses");
	check(plan.ports[4].state == DRV_PCI_VMD_PORT_NOT_D0 && get(asleep, 0x18, 4) == 0, "ports: a port with a function out of D0 is refused");
	check(plan.ports[5].state == DRV_PCI_VMD_PORT_NO_BUS && get(beyond, 0x18, 4) == 0, "ports: a port past the last bus is refused");
	check((get(beyond, 0x04, 2) & 0x7U) == 0, "ports: a refused port decodes nothing");
}

/* No window below 4 GiB: MEMBAR1 above it and MEMBAR2 too small. */
static void
test_no_window(void)
{
	static struct model model;
	static struct drv_pci_vmd_plan plan;
	struct drv_pci_vmd_config_access access;
	struct drv_pci_vmd_domain domain;
	struct model_function *port;
	struct model_function *nvme;

	memset(&model, 0, sizeof(model));
	port = add_root_port(&model, 6);
	nvme = add_nvme(&model, 0xe1);
	domain_5330(&domain);
	domain.windows[0].base = UINT64_C(0x6000000000);
	access.read = model_read;
	access.write = model_write;
	access.context = &model;

	check(drv_pci_vmd_plan_domain(&access, &domain, &plan) == 0, "no window: the plan succeeds");
	check(plan.ports[0].state == DRV_PCI_VMD_PORT_NO_WINDOW, "no window: the port is refused");
	check(get(port, 0x18, 4) == 0 && get(port, 0x20, 4) == 0x0000fff0U, "no window: no buses, window closed");
	check(get(nvme, 0x10, 4) == 0x4U && get(nvme, 0x14, 4) == 0, "no window: the NVMe BAR is cleared");
}

/* A MEMBAR2 below 4 GiB: nothing is placed in the VMD's own head of it. */
static void
test_membar2_head(void)
{
	static struct model model;
	static struct drv_pci_vmd_plan plan;
	struct drv_pci_vmd_config_access access;
	struct drv_pci_vmd_domain domain;

	memset(&model, 0, sizeof(model));
	(void)add_root_port(&model, 6);
	(void)add_nvme(&model, 0xe1);
	domain.bus_start = 224;
	domain.bus_end = 255;
	domain.windows[0].base = 0x80000000U + 0x2000U;
	domain.windows[0].length = 0x200000U - 0x2000U;
	domain.window_count = 1;
	access.read = model_read;
	access.write = model_write;
	access.context = &model;

	check(drv_pci_vmd_plan_domain(&access, &domain, &plan) == 0, "membar2: the plan succeeds");
	check(plan.ports[0].window_base == 0x80100000U, "membar2: the window starts past the VMD's head");
}

/* The message address and the slot table. */
static void
test_messages(void)
{
	static struct drv_pci_vmd_messages messages;
	struct drv_pci_vmd_pending pending[DRV_PCI_VMD_MESSAGES_MAX];
	drv_pci_vmd_run_t run;
	void *argument;
	uint64_t address;
	uint32_t data;
	unsigned slot;
	unsigned index;
	unsigned count;
	int error;

	drv_pci_vmd_message_address(1, &address, &data);
	check(address == UINT64_C(0xfee01000) && data == 0, "messages: vector 1 is 0xfee01000 with no data");

	drv_pci_vmd_messages_init(&messages);
	for (index = 0; index < DRV_PCI_VMD_MESSAGES_MAX; index++) {
		error = drv_pci_vmd_message_take(&messages, run_nothing, &messages, &slot);
		check(error == 0 && slot == index, "messages: slots are taken in order");
	}
	check(drv_pci_vmd_message_take(&messages, run_nothing, NULL, &slot) == KERNEL_ENOSPC, "messages: a full table refuses");

	count = drv_pci_vmd_messages_collect(&messages, pending);
	check(count == DRV_PCI_VMD_MESSAGES_MAX, "messages: an interrupt takes every held slot");
	check(drv_pci_vmd_message_clear(&messages, 3, &run, &argument) == 0 && run == run_nothing, "messages: a running slot can be cleared");
	check(drv_pci_vmd_message_in_flight(&messages, 3) == 1, "messages: the cleared slot is still running");
	check(drv_pci_vmd_message_take(&messages, run_nothing, NULL, &slot) == KERNEL_ENOSPC, "messages: a slot still running is not reused");
	drv_pci_vmd_messages_release(&messages, pending, count);
	check(drv_pci_vmd_message_in_flight(&messages, 3) == 0, "messages: the slot drains");
	check(drv_pci_vmd_message_take(&messages, run_nothing, NULL, &slot) == 0 && slot == 3, "messages: the drained slot is reused");
	check(drv_pci_vmd_message_clear(&messages, 3, &run, &argument) == 0, "messages: clear again");
	drv_pci_vmd_message_restore(&messages, 3, run, argument);
	check(drv_pci_vmd_message_clear(&messages, 3, &run, &argument) == 0, "messages: a restored slot is held");
	check(drv_pci_vmd_message_clear(&messages, 3, &run, &argument) == KERNEL_EINVAL, "messages: an empty slot cannot be cleared");
	check(drv_pci_vmd_message_clear(&messages, DRV_PCI_VMD_MESSAGES_MAX, &run, &argument) == KERNEL_EINVAL, "messages: a slot past the table is refused");
	count = drv_pci_vmd_messages_collect(&messages, pending);
	check(count == DRV_PCI_VMD_MESSAGES_MAX - 1U, "messages: an interrupt passes over a free slot");
	drv_pci_vmd_messages_release(&messages, pending, count);
}

/* A handler that finds no work. */
static int
run_nothing(
	void *argument)
{
	(void)argument;
	return 0;
}

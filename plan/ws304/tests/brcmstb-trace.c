/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The register trace of a BCM2711 start, to compare two builds of
 * pci-brcmstb.c.
 *
 * The driver is linked against a small model of the Raspberry Pi 4's
 * controller and its VL805, and every register access it makes is printed
 * with the virtual time.  Running the program once against the driver
 * before a change and once after, and comparing the two outputs, shows
 * whether the change left the BCM2711's sequence as it was.
 *
 * Usage: brcmstb-trace > trace.txt
 */

#include <drivers/pci/pci-brcmstb.h>
#include <drivers/generic/dma.h>
#include <kern/clock.h>
#include <kern/device-io.h>
#include <kern/klog.h>
#include <kern/kmem.h>
#include <kern/lock.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Where the model's registers sit, and the windows of the Pi 4's tree. */
#define TRACE_REGISTER_BASE	0xfd500000ULL
#define TRACE_REGISTER_SIZE	0x9310ULL
#define TRACE_WINDOW_CPU	0x600000000ULL
#define TRACE_WINDOW_PCI	0xc0000000ULL
#define TRACE_WINDOW_SIZE	0x40000000ULL

/* The registers the model gives meaning to. */
#define TRACE_STATUS		0x4068U
#define TRACE_EXT_CFG_DATA	0x8000U
#define TRACE_EXT_CFG_INDEX	0x9000U
#define TRACE_SW_INIT		0x9210U
#define TRACE_SW_INIT_PERST	0x1U
#define TRACE_LINK_UP		0x000000b0U

/* The VL805's identity and its one 64-bit BAR of 4 KiB. */
#define TRACE_VL805_ID		0x34831106U
#define TRACE_BAR_SIZE		0x1000U

/* A bus and a device as the fake PCI core hands them to the driver. */
struct drv_pci_bus {
	struct drv_pci_bus *parent;
	struct drv_pci_device *bridge;
};

struct drv_pci_device {
	struct drv_pci_address address;
	struct drv_pci_bus *bus;
};

/* The controller's registers, the root port's configuration space at their start. */
static uint8_t registers[0x10000U];

/* The VL805's configuration space behind the index register. */
static uint8_t endpoint[4096];
static uint32_t endpoint_bar_low;
static uint32_t endpoint_bar_high;

/* The virtual clock the waits advance, in microseconds. */
static unsigned long long now_us;

/* What the fake PCI core recorded. */
static const struct drv_pci_bus_ops *published_ops;
static void *published_host;
static struct drv_pci_bus root_bus;
static int dma_token;
static uint8_t bar_memory[TRACE_BAR_SIZE];

static uint32_t space_read(const uint8_t *space, unsigned offset, unsigned width);
static void space_write(uint8_t *space, unsigned offset, unsigned width, uint32_t value);
static uint32_t model_read(unsigned offset, unsigned width);
static void model_write(unsigned offset, unsigned width, uint32_t value);
static unsigned register_offset(const volatile void *address);

/*
 * Starts the model controller, publishes it, and uses it as the PCI core
 * would, printing every register access.
 */
int
main(
	void)
{
	struct drv_pci_brcmstb_config config;
	struct drv_pci_brcmstb *host;
	struct drv_pci_mapping mapping;
	struct drv_pci_device device;
	struct drv_pci_bar bar;
	struct drv_pci_irq irq;
	unsigned count;
	uint32_t value;
	int error;

	/* Describes the Pi 4's controller as its tree does. */
	memset(&config, 0, sizeof(config));
	config.register_base = TRACE_REGISTER_BASE;
	config.register_size = TRACE_REGISTER_SIZE;
	config.outbound_cpu_base = TRACE_WINDOW_CPU;
	config.outbound_pci_base = TRACE_WINDOW_PCI;
	config.outbound_size = TRACE_WINDOW_SIZE;
	config.inbound_cpu_base = 0;
	config.inbound_pci_base = 0;
	config.inbound_size = 0xc0000000ULL;
	config.intx_irq[0] = 175U;
	config.intx_irq[1] = 176U;
	config.intx_irq[2] = 177U;
	config.intx_irq[3] = 178U;

	/* Gives the VL805 its identity, a 64-bit BAR and INTA. */
	space_write(endpoint, 0x00U, 4U, TRACE_VL805_ID);
	space_write(endpoint, 0x08U, 4U, 0x0c033000U);
	space_write(endpoint, 0x3dU, 1U, 1U);
	endpoint_bar_low = 0x4U;

	/* Starts and publishes the controller. */
	error = drv_pci_brcmstb_start(&config, &host);
	printf("start %d\n", error);
	if (error != 0)
		return EXIT_FAILURE;
	error = drv_pci_brcmstb_publish(host);
	printf("publish %d\n", error);

	/* Reads the endpoint's identity and the root port's command word as the core would. */
	memset(&device, 0, sizeof(device));
	device.address.bus = 1U;
	device.bus = &root_bus;
	error = published_ops->config_read(published_host, &device.address, 0, 4U, &value);
	printf("config read %d %08x\n", error, value);
	device.address.bus = 0;
	error = published_ops->config_read(published_host, &device.address, 4U, 2U, &value);
	printf("config read %d %08x\n", error, value);

	/* Asks for the endpoint's interrupt and maps its BAR. */
	device.address.bus = 1U;
	error = published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_INTX, 1U, 1U, &irq, &count);
	printf("intx %d vector %u\n", error, irq.vector);
	error = published_ops->allocate_irqs(published_host, &device, DRV_PCI_IRQ_MSI, 1U, 1U, &irq, &count);
	printf("msi %d\n", error);
	memset(&bar, 0, sizeof(bar));
	bar.type = DRV_PCI_BAR_MEMORY64;
	bar.bus_address = TRACE_WINDOW_PCI;
	bar.size = TRACE_BAR_SIZE;
	error = published_ops->map_bar(published_host, &device, &bar, 0, &mapping);
	printf("map %d %llx\n", error, (unsigned long long)mapping.physical_address);

	/* Succeeded: the trace is complete. */
	return EXIT_SUCCESS;
}

/* Reads a little-endian value of one width from a byte space. */
static uint32_t
space_read(
	const uint8_t *space,
	unsigned offset,
	unsigned width)
{
	uint32_t value;
	unsigned index;

	/* Gathers the bytes, the lowest first. */
	value = 0;
	for (index = 0; index < width; index++)
		value |= (uint32_t)space[offset + index] << (index * 8U);

	/* Reports the value. */
	return value;
}

/* Writes a little-endian value of one width to a byte space. */
static void
space_write(
	uint8_t *space,
	unsigned offset,
	unsigned width,
	uint32_t value)
{
	unsigned index;

	/* Scatters the bytes, the lowest first. */
	for (index = 0; index < width; index++)
		space[offset + index] = (uint8_t)(value >> (index * 8U));
}

/* Reads a model register: the link, the endpoint's space, or plain storage. */
static uint32_t
model_read(
	unsigned offset,
	unsigned width)
{
	uint32_t value;
	unsigned inner;

	/* The link is up once PERST# is released. */
	if (offset == TRACE_STATUS) {
		value = 0;
		if ((space_read(registers, TRACE_SW_INIT, 4U) & TRACE_SW_INIT_PERST) == 0)
			value = TRACE_LINK_UP;
	} else if (offset >= TRACE_EXT_CFG_DATA && offset < TRACE_EXT_CFG_DATA + 4096U) {
		/* The endpoint's BAR reads back its sizing mask, the rest its space. */
		inner = offset - TRACE_EXT_CFG_DATA;
		if (inner == 0x10U) {
			value = endpoint_bar_low;
		} else if (inner == 0x14U) {
			value = endpoint_bar_high;
		} else if (inner >= 0x18U && inner < 0x28U) {
			/* BAR2 to BAR5 are not implemented and read as zero. */
			value = 0;
		} else {
			value = space_read(endpoint, inner, width);
		}
	} else {
		value = space_read(registers, offset, width);
	}

	/* Prints the access and reports the value. */
	printf("%8llu R %04x/%u %08x\n", now_us, offset, width, value);
	return value;
}

/* Writes a model register: the endpoint's space or plain storage. */
static void
model_write(
	unsigned offset,
	unsigned width,
	uint32_t value)
{
	unsigned inner;

	/* Prints the access first. */
	printf("%8llu W %04x/%u %08x\n", now_us, offset, width, value);

	/* The endpoint's BAR keeps the address bits of a 4 KiB 64-bit BAR. */
	if (offset >= TRACE_EXT_CFG_DATA && offset < TRACE_EXT_CFG_DATA + 4096U) {
		inner = offset - TRACE_EXT_CFG_DATA;
		if (inner == 0x10U) {
			endpoint_bar_low = (value & ~(TRACE_BAR_SIZE - 1U)) | 0x4U;
		} else if (inner == 0x14U) {
			endpoint_bar_high = value;
		} else {
			space_write(endpoint, inner, width, value);
		}
		return;
	}

	/* Anything else is stored. */
	space_write(registers, offset, width, value);
}

/* Converts a mapped address back to the register offset it stands for. */
static unsigned
register_offset(
	const volatile void *address)
{
	return (unsigned)((const volatile uint8_t *)address - (const volatile uint8_t *)registers);
}

uint8_t
kern_mmio_read8(
	const volatile void *address)
{
	return (uint8_t)model_read(register_offset(address), 1U);
}

uint16_t
kern_mmio_read16(
	const volatile void *address)
{
	return (uint16_t)model_read(register_offset(address), 2U);
}

uint32_t
kern_mmio_read32(
	const volatile void *address)
{
	return model_read(register_offset(address), 4U);
}

void
kern_mmio_write8(
	volatile void *address,
	uint8_t value)
{
	model_write(register_offset(address), 1U, value);
}

void
kern_mmio_write16(
	volatile void *address,
	uint16_t value)
{
	model_write(register_offset(address), 2U, value);
}

void
kern_mmio_write32(
	volatile void *address,
	uint32_t value)
{
	model_write(register_offset(address), 4U, value);
}

int
kern_device_map(
	uint64_t address,
	size_t size,
	unsigned attributes,
	void **mapped)
{
	printf("map %llx %zu %u\n", (unsigned long long)address, size, attributes);
	if (address == TRACE_REGISTER_BASE) {
		*mapped = registers;
		return 0;
	}
	*mapped = bar_memory;
	return 0;
}

int
kern_device_unmap(
	void *mapped,
	size_t size)
{
	(void)mapped;
	(void)size;
	return 0;
}

void
kern_usleep_range(
	unsigned min_us,
	unsigned max_us)
{
	(void)max_us;
	now_us += min_us;
}

void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;

	va_start(arguments, format);
	printf("log: ");
	vprintf(format, arguments);
	va_end(arguments);
}

void *
kern_malloc(
	size_t size)
{
	return calloc(1, size);
}

void
kern_free(
	void *pointer)
{
	free(pointer);
}

void
spin_init(
	struct spinlock *lock,
	enum lock_rank rank,
	const char *name)
{
	memset(lock, 0, sizeof(*lock));
	lock->rank = rank;
	lock->name = name;
}

unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	(void)lock;
	return 1;
}

void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	(void)lock;
	(void)enabled;
}

int
drv_dma_device_create_window(
	const struct drv_dma_constraints *constraints,
	uint64_t bus_offset,
	uint64_t physical_limit,
	struct drv_dma_device **result)
{
	printf("dma bits %u segment %zu coherent %d offset %llx limit %llx\n",
	       constraints->address_bits,
	       constraints->max_segment_size,
	       constraints->coherent,
	       (unsigned long long)bus_offset,
	       (unsigned long long)physical_limit);
	*result = (struct drv_dma_device *)&dma_token;
	return 0;
}

int
drv_dma_device_destroy(
	struct drv_dma_device *device)
{
	(void)device;
	return 0;
}

int
drv_pci_bus_create_root(
	uint16_t segment,
	uint8_t number,
	const struct drv_pci_bus_ops *ops,
	void *host,
	struct drv_dma_device *dma,
	struct drv_pci_bus **result)
{
	(void)dma;
	printf("root segment %u bus %u space %u msi %d\n",
	       segment,
	       number,
	       ops->config_space_size,
	       ops->msi_establish != NULL);
	published_ops = ops;
	published_host = host;
	*result = &root_bus;
	return 0;
}

int
drv_pci_bus_scan_tree(
	struct drv_pci_bus *bus)
{
	(void)bus;
	printf("scan\n");
	return 0;
}

int
drv_pci_device_config_read8(
	struct drv_pci_device *device,
	unsigned offset,
	uint8_t *value)
{
	uint32_t read;
	int error;

	error = published_ops->config_read(published_host, &device->address, offset, 1U, &read);
	*value = (uint8_t)read;
	return error;
}

int
drv_pci_device_config_read16(
	struct drv_pci_device *device,
	unsigned offset,
	uint16_t *value)
{
	(void)device;
	(void)offset;
	(void)value;
	return ENODEV;
}

int
drv_pci_device_find_capability(
	struct drv_pci_device *device,
	uint8_t id,
	unsigned *offset)
{
	(void)device;
	(void)id;
	(void)offset;
	return ENOENT;
}

struct drv_pci_bus *
drv_pci_device_bus(
	const struct drv_pci_device *device)
{
	return device->bus;
}

struct drv_pci_bus *
drv_pci_bus_parent(
	const struct drv_pci_bus *bus)
{
	return bus->parent;
}

struct drv_pci_device *
drv_pci_bus_bridge(
	const struct drv_pci_bus *bus)
{
	return bus->bridge;
}

void
drv_pci_device_address(
	const struct drv_pci_device *device,
	struct drv_pci_address *address)
{
	*address = device->address;
}

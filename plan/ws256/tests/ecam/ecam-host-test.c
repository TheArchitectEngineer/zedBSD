/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host check of the generic ECAM PCI host on a fake configuration space:
 * the device tree's windows and interrupt map, the BAR placement of the
 * root bus and the INTx routing, with the PCI core replaced by stubs.
 *
 *   ecam-host-test virt-pcie.dtb
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/generic/dma.h>
#include <drivers/generic/fdt.h>
#include <drivers/pci/pci-ecam-fdt.h>
#include <kern/device-io.h>
#include <kern/kmem.h>
#include <kern/klog.h>
#include <kern/pmem.h>
#include <uapi/errno.h>

#define BUSES		2U
#define SPACE		4096U
#define ECAM_BASE	0x30000000ULL

/* The fake configuration space of two buses. */
static uint8_t ecam[BUSES][32][8][SPACE];

/* The BAR sizes of the fake devices: 00:02.0 and 00:01.0. */
#define NVME_BAR0_SIZE	0x4000U
#define XHCI_BAR0_SIZE	0x4000U
#define IO_BAR_SIZE	0x100U

static uint8_t bar_backing[0x10000];
static uint64_t mapped_bar;
static const struct drv_pci_bus_ops *root_ops;
static void *root_host;
static unsigned root_number;
static int failures;

/* The fake functions the allocation sees. */
struct drv_pci_device {
	struct drv_pci_address address;
};
struct drv_pci_bus {
	int unused;
};
static struct drv_pci_bus root_bus;

static void
check(int condition, const char *what)
{
	printf("%s %s\n", condition ? "PASS" : "FAIL", what);
	if (!condition)
		failures++;
}

static void
put32(unsigned bus, unsigned dev, unsigned fn, unsigned offset, uint32_t value)
{
	memcpy(&ecam[bus][dev][fn][offset], &value, 4);
}

static uint32_t
get32(unsigned bus, unsigned dev, unsigned fn, unsigned offset)
{
	uint32_t value;

	memcpy(&value, &ecam[bus][dev][fn][offset], 4);
	return value;
}

/* Finds which BAR a write lands on and keeps only the bits it decodes. */
static void
bar_write(uint8_t *target, uint32_t value)
{
	size_t position = (size_t)(target - &ecam[0][0][0][0]);
	unsigned offset = (unsigned)(position % SPACE);
	unsigned fn = (unsigned)((position / SPACE) % 8U);
	unsigned dev = (unsigned)((position / SPACE / 8U) % 32U);
	unsigned bus = (unsigned)(position / SPACE / 8U / 32U);

	/* 00:02.0 BAR0/1 is a 64-bit memory BAR, BAR2 an I/O BAR. */
	if (bus == 0 && dev == 2 && fn == 0 && offset == 0x10)
		value = (value & ~(NVME_BAR0_SIZE - 1U)) | 0x4U;
	else if (bus == 0 && dev == 2 && fn == 0 && offset == 0x18)
		value = (value & ~(IO_BAR_SIZE - 1U)) | 0x1U;
	else if (bus == 0 && dev == 1 && fn == 0 && offset == 0x10)
		value = value & ~(XHCI_BAR0_SIZE - 1U);
	else if (offset >= 0x10 && offset < 0x28 && !(bus == 0 && dev == 2 && fn == 0 && offset == 0x14))
		value = 0;
	memcpy(target, &value, 4);
}

uint8_t kern_mmio_read8(const volatile void *address) { return *(const volatile uint8_t *)address; }
uint16_t kern_mmio_read16(const volatile void *address) { uint16_t v; memcpy(&v, (const void *)address, 2); return v; }
uint32_t kern_mmio_read32(const volatile void *address) { uint32_t v; memcpy(&v, (const void *)address, 4); return v; }
void kern_mmio_write8(volatile void *address, uint8_t value) { *(volatile uint8_t *)address = value; }
void kern_mmio_write16(volatile void *address, uint16_t value) { memcpy((void *)address, &value, 2); }
void
kern_mmio_write32(volatile void *address, uint32_t value)
{
	uint8_t *target = (uint8_t *)address;

	if (target >= &ecam[0][0][0][0] && target < &ecam[0][0][0][0] + sizeof(ecam))
		bar_write(target, value);
	else
		memcpy(target, &value, 4);
}

int
kern_device_map(uint64_t address, size_t size, unsigned attributes, void **mapped)
{
	(void)attributes;
	if (address == ECAM_BASE && size == sizeof(ecam)) {
		*mapped = ecam;
		return 0;
	}
	if (size > sizeof(bar_backing))
		return ENOMEM;
	mapped_bar = address;
	*mapped = bar_backing;
	return 0;
}

int kern_device_unmap(void *mapped, size_t size) { (void)mapped; (void)size; return 0; }
void *kern_malloc(size_t size) { return malloc(size); }
void kern_free(void *pointer) { free(pointer); }
void kern_logf(const char *format, ...) { va_list ap; va_start(ap, format); vprintf(format, ap); va_end(ap); }
int drv_dma_device_create(const struct drv_dma_constraints *constraints, struct drv_dma_device **result)
{
	check(constraints->coherent == 1, "DMA is coherent (dma-coherent)");
	check(constraints->address_bits == 32U, "DMA below 4 GiB");
	*result = (struct drv_dma_device *)&root_bus;
	return 0;
}
int drv_dma_device_destroy(struct drv_dma_device *device) { (void)device; return 0; }
int
drv_pci_bus_create_root(uint16_t segment, uint8_t number, const struct drv_pci_bus_ops *ops, void *host, struct drv_dma_device *dma, struct drv_pci_bus **result)
{
	(void)segment;
	(void)dma;
	root_ops = ops;
	root_host = host;
	root_number = number;
	*result = &root_bus;
	return 0;
}
int drv_pci_bus_scan_tree(struct drv_pci_bus *bus) { (void)bus; return 0; }
struct drv_pci_bus *drv_pci_device_bus(const struct drv_pci_device *d) { (void)d; return &root_bus; }
struct drv_pci_bus *drv_pci_bus_parent(const struct drv_pci_bus *b) { (void)b; return NULL; }
struct drv_pci_device *drv_pci_bus_bridge(const struct drv_pci_bus *b) { (void)b; return NULL; }
void drv_pci_device_address(const struct drv_pci_device *d, struct drv_pci_address *a) { *a = d->address; }
int
drv_pci_device_config_read8(struct drv_pci_device *d, unsigned o, uint8_t *v)
{
	*v = ecam[d->address.bus][d->address.device][d->address.function][o];
	return 0;
}

static uint8_t *
read_file(const char *path, size_t *length)
{
	FILE *file = fopen(path, "rb");
	uint8_t *data;
	long size;

	if (file == NULL) {
		perror(path);
		exit(2);
	}
	fseek(file, 0, SEEK_END);
	size = ftell(file);
	fseek(file, 0, SEEK_SET);
	data = malloc((size_t)size);
	if (data == NULL || fread(data, 1, (size_t)size, file) != (size_t)size)
		exit(2);
	fclose(file);
	*length = (size_t)size;
	return data;
}

static unsigned
route(unsigned dev, uint8_t pin, int *error)
{
	struct drv_pci_device device;
	struct drv_pci_irq irq;
	unsigned count = 0;

	memset(&device, 0, sizeof(device));
	device.address.device = (uint8_t)dev;
	ecam[0][dev][0][0x3d] = pin;
	*error = root_ops->allocate_irqs(root_host, &device, DRV_PCI_IRQ_INTX, 1, 1, &irq, &count);
	return *error == 0 && count == 1 ? irq.vector : 0;
}

int
main(int argc, char **argv)
{
	struct drv_pci_ecam_fdt_config config;
	struct drv_pci_ecam_fdt *host;
	struct drv_pci_device device;
	struct drv_pci_address address;
	struct drv_pci_mapping mapping;
	struct drv_pci_bar bar;
	struct drv_pci_irq irq;
	struct drv_fdt fdt;
	uint8_t *blob;
	size_t length;
	uint32_t node, value, bar0, bar0_high, xhci_bar;
	unsigned count;
	int error;

	if (argc != 2)
		return 2;
	blob = read_file(argv[1], &length);
	check(drv_fdt_open(&fdt, blob, length) == 0, "tree opens");
	check(drv_fdt_find_compatible(&fdt, "pci-host-ecam-generic", DRV_FDT_NO_NODE, &node) == 0, "host node found");

	/* The tree's windows and map. */
	error = drv_pci_ecam_fdt_describe(&fdt, node, &config);
	check(error == 0, "describe succeeds");
	check(config.config_base == ECAM_BASE && config.config_size == sizeof(ecam), "ECAM window 0x30000000, 2 MiB");
	check(config.first_bus == 0, "bus-range starts at 0");
	check(config.memory_pci_base == 0x40000000ULL && config.memory_cpu_base == 0x40000000ULL &&
	      config.memory_size == 0x40000000ULL, "32-bit memory window 0x40000000 + 1 GiB (not the I/O or 64-bit one)");
	check(config.coherent, "dma-coherent seen");
	check(config.devfn_mask == 0x1800U && config.pin_mask == 7U, "interrupt-map-mask");
	check(config.route_count == 16U, "16 routes");

	/* The fake devices: a host bridge, xHCI at slot 1, NVMe at slot 2, a bridge at slot 3. */
	memset(ecam, 0xff, sizeof(ecam));
	for (unsigned dev = 0; dev < 4; dev++) {
		memset(ecam[0][dev][0], 0, SPACE);
		put32(0, dev, 0, 0, 0x00081b36U + dev);
	}
	ecam[0][3][0][0x0e] = 0x01;
	put32(0, 2, 0, 0x10, 0x4U);
	put32(0, 2, 0, 0x18, 0x1U);
	put32(0, 2, 0, 0x04, 0x0007U);

	/* The placement. */
	error = drv_pci_ecam_fdt_start(&config, &host);
	check(error == 0, "start succeeds");
	xhci_bar = get32(0, 1, 0, 0x10);
	bar0 = get32(0, 2, 0, 0x10);
	bar0_high = get32(0, 2, 0, 0x14);
	check(xhci_bar == 0x40000000U, "xHCI BAR0 at the window's start");
	check((bar0 & ~0xfU) == 0x40004000U && (bar0 & 0x4U) != 0 && bar0_high == 0, "NVMe 64-bit BAR0 next, aligned, upper half 0");
	check((get32(0, 2, 0, 0x18) & ~0x3U) == 0, "I/O BAR left unassigned");
	check(get32(0, 2, 0, 0x04) == 0x0007U, "command restored after sizing");
	check(get32(0, 3, 0, 0x10) == 0, "bridge left alone");

	/* The configuration access. */
	error = drv_pci_ecam_fdt_publish(host);
	check(error == 0 && root_ops != NULL && root_number == 0, "root bus 0 published");
	memset(&address, 0, sizeof(address));
	address.device = 2;
	root_ops->config_read(root_host, &address, 0, 4, &value);
	check(value == 0x00081b38U, "config read of 00:02.0");
	address.bus = 5;
	root_ops->config_read(root_host, &address, 0, 2, &value);
	check(value == 0xffffU, "bus outside the window reads all-ones");
	check(root_ops->config_read(root_host, &address, 1, 2, &value) == EINVAL, "unaligned access refused");

	/* The BAR mapping. */
	memset(&bar, 0, sizeof(bar));
	bar.type = DRV_PCI_BAR_MEMORY64;
	bar.bus_address = 0x40004000ULL;
	bar.size = NVME_BAR0_SIZE;
	error = root_ops->map_bar(root_host, NULL, &bar, 0, &mapping);
	check(error == 0 && mapped_bar == 0x40004000ULL, "BAR mapped at its CPU address");
	bar.bus_address = 0x80000000ULL;
	check(root_ops->map_bar(root_host, NULL, &bar, 0, &mapping) == EINVAL, "BAR outside the window refused");

	/* The routing: slot s, pin p arrives on 32 + (s + p - 1) mod 4. */
	check(route(0, 1, &error) == 32U, "slot 0 INTA -> 32");
	check(route(1, 1, &error) == 33U, "slot 1 INTA -> 33");
	check(route(2, 1, &error) == 34U, "slot 2 INTA -> 34");
	check(route(3, 4, &error) == 34U, "slot 3 INTD -> 34");
	check(route(6, 2, &error) == 35U, "slot 6 (masked to 2) INTB -> 35");
	route(2, 0, &error);
	check(error == ENODEV, "no pin -> ENODEV");
	memset(&device, 0, sizeof(device));
	count = 0;
	check(root_ops->allocate_irqs(root_host, &device, DRV_PCI_IRQ_MSIX, 1, 1, &irq, &count) == ENOTSUP, "MSI-X not offered");

	printf("%s: %d failure(s)\n", failures == 0 ? "OK" : "NG", failures);
	return failures == 0 ? 0 : 1;
}

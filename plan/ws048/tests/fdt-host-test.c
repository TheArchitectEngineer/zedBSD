/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * WS048 p002: host test of the flattened device tree reader.
 *
 * The test reads the Raspberry Pi 4 device tree the firmware ships, and a
 * copy in which the PCIe node is disabled the way QEMU's raspi4b disables
 * it, and checks the values the PCIe host driver and the mailbox client
 * depend on.  It then damages copies of the blob and checks that the reader
 * refuses them instead of reading outside the blob.
 *
 * Usage: fdt-host-test FIRMWARE.dtb DISABLED.dtb
 */

#include <drivers/generic/fdt.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

#define CHECK(expression)                                                   \
	do {                                                                 \
		checks++;                                                    \
		if (!(expression)) {                                        \
			fprintf(stderr, "ws048-fdt: failed at %s:%d: %s\n",   \
			    __FILE__, __LINE__, #expression);                  \
			exit(EXIT_FAILURE);                                   \
		}                                                            \
	} while (0)

static unsigned char *read_file(const char *path, size_t *size);
static void check_firmware_tree(const unsigned char *blob, size_t size);
static void check_disabled_tree(const unsigned char *blob, size_t size);
static void check_damaged_trees(const unsigned char *blob, size_t size);
static void store_be32(unsigned char *bytes, unsigned value);

int
main(
	int argc,
	char **argv)
{
	unsigned char *firmware;
	unsigned char *disabled;
	size_t firmware_size;
	size_t disabled_size;

	if (argc != 3) {
		fprintf(stderr, "usage: %s FIRMWARE.dtb DISABLED.dtb\n", argv[0]);
		return EXIT_FAILURE;
	}

	firmware = read_file(argv[1], &firmware_size);
	disabled = read_file(argv[2], &disabled_size);

	check_firmware_tree(firmware, firmware_size);
	check_disabled_tree(disabled, disabled_size);
	check_damaged_trees(firmware, firmware_size);

	free(firmware);
	free(disabled);
	printf("ws048-fdt: %u checks passed\n", checks);
	return EXIT_SUCCESS;
}

/* Reads a whole file into memory. */
static unsigned char *
read_file(
	const char *path,
	size_t *size)
{
	unsigned char *bytes;
	FILE *file;
	long length;

	file = fopen(path, "rb");
	if (file == NULL) {
		perror(path);
		exit(EXIT_FAILURE);
	}

	fseek(file, 0, SEEK_END);
	length = ftell(file);
	fseek(file, 0, SEEK_SET);
	bytes = malloc((size_t)length);
	CHECK(bytes != NULL);
	CHECK(fread(bytes, 1, (size_t)length, file) == (size_t)length);
	fclose(file);
	*size = (size_t)length;
	return bytes;
}

/* Checks the values of the firmware's own tree. */
static void
check_firmware_tree(
	const unsigned char *blob,
	size_t size)
{
	struct drv_fdt fdt;
	const uint8_t *value;
	uint32_t length;
	uint32_t pcie;
	uint32_t second;
	uint32_t mailbox;
	uint32_t gic;
	uint32_t parent;
	uint64_t address;
	uint64_t window;

	CHECK(drv_fdt_open(&fdt, blob, size) == 0);

	/* The PCIe controller and its registers behind the scb bus. */
	CHECK(drv_fdt_find_compatible(&fdt, "brcm,bcm2711-pcie", DRV_FDT_NO_NODE, &pcie) == 0);
	CHECK(drv_fdt_node_enabled(&fdt, pcie));
	CHECK(drv_fdt_reg(&fdt, pcie, 0, &address, &window) == 0);
	CHECK(address == 0xfd500000ULL);
	CHECK(window == 0x9310ULL);
	CHECK(drv_fdt_reg(&fdt, pcie, 1, &address, &window) == ENOENT);
	CHECK(drv_fdt_find_compatible(&fdt, "brcm,bcm2711-pcie", pcie, &second) == ENOENT);

	/* The PCI bus the controller presents uses three address cells. */
	CHECK(drv_fdt_node_cells(&fdt, pcie, "#address-cells", 0) == 3U);
	CHECK(drv_fdt_node_cells(&fdt, pcie, "#size-cells", 0) == 2U);
	CHECK(drv_fdt_node_cells(&fdt, pcie, "#interrupt-cells", 0) == 1U);

	/* The outbound window: PCI 0xc0000000 at CPU 0x6_0000_0000, 1 GiB. */
	CHECK(drv_fdt_property(&fdt, pcie, "ranges", &value, &length) == 0);
	CHECK(length == 7U * 4U);
	CHECK(drv_fdt_cells_value(value, 0, 1) == 0x02000000ULL);
	CHECK(drv_fdt_cells_value(value, 1, 2) == 0xc0000000ULL);
	CHECK(drv_fdt_cells_value(value, 3, 2) == 0x600000000ULL);
	CHECK(drv_fdt_cells_value(value, 5, 2) == 0x40000000ULL);
	CHECK(drv_fdt_translate(&fdt, pcie, 0x600000000ULL, &address) == 0);
	CHECK(address == 0x600000000ULL);

	/* The inbound window: PCI 0 is CPU 0, for 3 GiB. */
	CHECK(drv_fdt_property(&fdt, pcie, "dma-ranges", &value, &length) == 0);
	CHECK(length == 7U * 4U);
	CHECK(drv_fdt_cells_value(value, 1, 2) == 0);
	CHECK(drv_fdt_cells_value(value, 3, 2) == 0);
	CHECK(drv_fdt_cells_value(value, 5, 2) == 0xc0000000ULL);

	/* INTA of the interrupt map goes to GIC SPI 143, level high. */
	CHECK(drv_fdt_property(&fdt, pcie, "interrupt-map", &value, &length) == 0);
	CHECK(length == 4U * 8U * 4U);
	CHECK(drv_fdt_cells_value(value, 3, 1) == 1U);
	CHECK(drv_fdt_find_phandle(&fdt, (uint32_t)drv_fdt_cells_value(value, 4, 1), &gic) == 0);
	CHECK(drv_fdt_node_cells(&fdt, gic, "#interrupt-cells", 0) == 3U);
	CHECK(drv_fdt_node_cells(&fdt, gic, "#address-cells", 0) == 0);
	CHECK(drv_fdt_property(&fdt, gic, "interrupt-controller", &value, &length) == 0);
	CHECK(length == 0);
	CHECK(drv_fdt_property(&fdt, pcie, "interrupt-map", &value, &length) == 0);
	CHECK(drv_fdt_cells_value(value, 5, 1) == 0);
	CHECK(drv_fdt_cells_value(value, 6, 1) == 143U);
	CHECK(drv_fdt_cells_value(value, 7, 1) == 4U);
	CHECK(drv_fdt_cells_value(value, 8 + 3, 1) == 2U);
	CHECK(drv_fdt_cells_value(value, 8 + 6, 1) == 144U);

	/* The controller asks for spread spectrum. */
	CHECK(drv_fdt_property(&fdt, pcie, "brcm,enable-ssc", &value, &length) == 0);
	CHECK(drv_fdt_property(&fdt, pcie, "no-such-property", &value, &length) == ENOENT);

	/* The property mailbox, translated through the soc bus. */
	CHECK(drv_fdt_find_compatible(&fdt, "brcm,bcm2835-mbox", DRV_FDT_NO_NODE, &mailbox) == 0);
	CHECK(drv_fdt_node_enabled(&fdt, mailbox));
	CHECK(drv_fdt_reg(&fdt, mailbox, 0, &address, &window) == 0);
	CHECK(address == 0xfe00b880ULL);
	CHECK(window == 0x40ULL);
	CHECK(drv_fdt_parent(&fdt, mailbox, &parent) == 0);
	CHECK(drv_fdt_property(&fdt, parent, "compatible", &value, &length) == 0);
	CHECK(strcmp((const char *)value, "simple-bus") == 0);

	/* The root has no parent, and no node is compatible with nonsense. */
	CHECK(drv_fdt_parent(&fdt, fdt.structure_offset, &parent) == ENOENT);
	CHECK(drv_fdt_find_compatible(&fdt, "no,such-device", DRV_FDT_NO_NODE, &second) == ENOENT);
	CHECK(drv_fdt_find_phandle(&fdt, 0xfffffff0U, &second) == ENOENT);

	/* An address outside every window of the soc bus has no CPU address. */
	CHECK(drv_fdt_translate(&fdt, mailbox, 0x10000000ULL, &address) == ENOENT);
}

/* Checks that a disabled PCIe node reads as absent. */
static void
check_disabled_tree(
	const unsigned char *blob,
	size_t size)
{
	struct drv_fdt fdt;
	uint32_t pcie;
	uint32_t mailbox;

	CHECK(drv_fdt_open(&fdt, blob, size) == 0);
	CHECK(drv_fdt_find_compatible(&fdt, "brcm,bcm2711-pcie", DRV_FDT_NO_NODE, &pcie) == 0);
	CHECK(!drv_fdt_node_enabled(&fdt, pcie));
	CHECK(drv_fdt_find_compatible(&fdt, "brcm,bcm2835-mbox", DRV_FDT_NO_NODE, &mailbox) == 0);
	CHECK(drv_fdt_node_enabled(&fdt, mailbox));
}

/* Checks that damaged copies of the blob are refused. */
static void
check_damaged_trees(
	const unsigned char *blob,
	size_t size)
{
	struct drv_fdt fdt;
	unsigned char *copy;
	uint32_t node;
	unsigned offset;

	copy = malloc(size);
	CHECK(copy != NULL);

	/* A blob shorter than it says. */
	CHECK(drv_fdt_open(&fdt, blob, size - 1U) == EINVAL);
	CHECK(drv_fdt_open(&fdt, blob, 16U) == EINVAL);
	CHECK(drv_fdt_open(NULL, blob, size) == EINVAL);

	/* A wrong magic number. */
	memcpy(copy, blob, size);
	copy[0] ^= 0xffU;
	CHECK(drv_fdt_open(&fdt, copy, size) == EINVAL);

	/* A structure block that leaves the blob. */
	memcpy(copy, blob, size);
	store_be32(copy + 36, (unsigned)size);
	CHECK(drv_fdt_open(&fdt, copy, size) == EINVAL);

	/* A version too old to carry the strings size. */
	memcpy(copy, blob, size);
	store_be32(copy + 20, 16U);
	CHECK(drv_fdt_open(&fdt, copy, size) == ENOTSUP);

	/*
	 * An unknown token in the middle of the structure: every walk must stop
	 * with an error rather than read past it.
	 */
	memcpy(copy, blob, size);
	CHECK(drv_fdt_open(&fdt, copy, size) == 0);
	offset = fdt.structure_offset + fdt.structure_size / 2U;
	offset &= ~3U;
	store_be32(copy + offset, 0x77U);
	CHECK(drv_fdt_find_compatible(&fdt, "no,such-device", DRV_FDT_NO_NODE, &node) == EINVAL);
	CHECK(drv_fdt_find_phandle(&fdt, 0xfffffff0U, &node) == EINVAL);

	/* A node offset that is not a begin-node token. */
	memcpy(copy, blob, size);
	CHECK(drv_fdt_open(&fdt, copy, size) == 0);
	CHECK(drv_fdt_node_cells(&fdt, fdt.structure_offset + 4U, "#address-cells", 7U) == 7U);
	CHECK(drv_fdt_parent(&fdt, 1U, &node) == ENOENT);

	free(copy);
}

/* Stores a big-endian 32-bit number. */
static void
store_be32(
	unsigned char *bytes,
	unsigned value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

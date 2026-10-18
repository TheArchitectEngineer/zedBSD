/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * OpenFirmware to zedBSD sun4u handoff builder.
 */

#include "handoff.h"
#include "ofw.h"

/* The largest property the builder reads. */
#define PROPERTY_BUFFER_SIZE 512U

/*
 * The walk of the device tree that fills one handoff.
 *
 * It lives for one sparcv9_handoff_build() call; each flag says that part
 * of the handoff has been found, so the first device of a kind wins.
 */
struct discovery {
	struct sun4u_boot_handoff *handoff;
	int have_cpu;
	int have_memory;
	int have_pci_io;
	int have_serial;
	int have_ide;
};

static unsigned long be32(const unsigned char *bytes);
static unsigned long long cells64(const unsigned char *bytes, unsigned int cells);
static void clear_bytes(void *destination, unsigned long size);
static int same_string(const char *left, const char *right);
static int property_string(ofw_cell_t node, const char *name, char *buffer, unsigned long size);
static int property_u32(ofw_cell_t node, const char *name, unsigned long *value);
static int address_cells(ofw_cell_t node, const char *name, unsigned int fallback);
static int memory_ranges(ofw_cell_t node, const char *name, struct sun4u_memory_range *ranges, unsigned char *count);
static void discover_pci_ranges(ofw_cell_t node, struct discovery *state);
static void discover_serial(ofw_cell_t node, struct discovery *state);
static void discover_ide(ofw_cell_t node, struct discovery *state, unsigned long vendor, unsigned long device);
static void discover_cpu(ofw_cell_t node, struct discovery *state);
static void discover_memory(ofw_cell_t node, struct discovery *state);
static int walk_node(ofw_cell_t node, struct discovery *state, unsigned int depth);
static void copy_bootpath(char *destination, const char *source);

/*
 * Fills a handoff from OpenFirmware's device tree and the boot path.
 *
 * Returns 0, or -1 when the tree lacks a device the HAL needs.
 */
int
sparcv9_handoff_build(
	struct sun4u_boot_handoff *handoff,
	const char *bootpath)
{
	struct discovery state;
	ofw_scell_t root;
	int walked;

	/* Fills the common record: one SCSI-like boot device and its Sun partition. */
	clear_bytes(handoff, sizeof(*handoff));
	handoff->common.magic = KERN_HANDOFF_MAGIC;
	handoff->common.version = SUN4U_HANDOFF_COMMON_VERSION;
	handoff->common.size = sizeof(*handoff);
	handoff->common.device_count = 1;
	handoff->common.boot_bios_id = 0x80;
	handoff->common.boot_partition_scheme = KERN_PARTITION_SCHEME_SUN;
	handoff->common.boot_partition_index = 1;
	handoff->common.boot_partition_lba = 4096;

	/* Fills the sun4u extension's identity and the boot path. */
	handoff->extension_magic = SUN4U_HANDOFF_EXTENSION_MAGIC;
	handoff->extension_version = SUN4U_HANDOFF_EXTENSION_VERSION;
	handoff->extension_size = sizeof(*handoff) - sizeof(handoff->common);
	copy_bootpath(handoff->bootpath, bootpath);

	/* Starts the walk with nothing found. */
	state.handoff = handoff;
	state.have_cpu = 0;
	state.have_memory = 0;
	state.have_pci_io = 0;
	state.have_serial = 0;
	state.have_ide = 0;

	/* Refuses a firmware without a root node. */
	root = ofw_finddevice("/");
	if (root <= 0)
		return -1;

	/* Walks the whole tree; a tree too deep is refused. */
	walked = walk_node((ofw_cell_t)root, &state, 0);
	if (walked != 0)
		return -1;

	/* Refuses a tree that lacks a device the HAL needs. */
	if (!state.have_cpu ||
	    !state.have_memory ||
	    !state.have_pci_io ||
	    !state.have_serial ||
	    !state.have_ide)
		return -1;

	/* Succeeded: the handoff describes the machine. */
	return 0;
}

/* Reads a big-endian 32-bit cell. */
static unsigned long
be32(
	const unsigned char *bytes)
{
	/* Combines the four bytes, the most significant first. */
	return (unsigned long)bytes[0] << 24 |
		(unsigned long)bytes[1] << 16 |
		(unsigned long)bytes[2] << 8 | bytes[3];
}

/* Reads a value of a number of 32-bit cells, the most significant first. */
static unsigned long long
cells64(
	const unsigned char *bytes,
	unsigned int cells)
{
	unsigned long long value;

	/* Shifts each cell in. */
	value = 0;
	while (cells != 0) {
		value = value << 32 | be32(bytes);
		bytes += 4;
		cells--;
	}

	/* Reports the value. */
	return value;
}

/* Clears bytes without the C library. */
static void
clear_bytes(
	void *destination,
	unsigned long size)
{
	unsigned char *bytes;

	/* Clears the bytes one at a time. */
	bytes = destination;
	while (size != 0) {
		*bytes = 0;
		bytes++;
		size--;
	}
}

/* Reports whether two strings are equal. */
static int
same_string(
	const char *left,
	const char *right)
{
	/* Steps over the common start. */
	while (*left != '\0' && *left == *right) {
		left++;
		right++;
	}

	/* The strings differ where the first one stopped. */
	if (*left != *right)
		return 0;

	/* Succeeded: the strings are equal. */
	return 1;
}

/* Reads a string property into a buffer and terminates it. */
static int
property_string(
	ofw_cell_t node,
	const char *name,
	char *buffer,
	unsigned long size)
{
	int length;

	/* Refuses a buffer too small for a character and the terminator. */
	if (size < 2)
		return -1;

	/* Refuses a missing or empty property, and one that fills the buffer. */
	length = ofw_getprop(node, name, buffer, size - 1U);
	if (length <= 0 || (unsigned long)length >= size)
		return -1;

	/* Terminates the string. */
	buffer[length] = '\0';

	/* Succeeded: the buffer holds the string. */
	return 0;
}

/* Reads a 32-bit integer property. */
static int
property_u32(
	ofw_cell_t node,
	const char *name,
	unsigned long *value)
{
	unsigned char bytes[4];
	int length;

	/* Refuses a property that is not one cell. */
	length = ofw_getprop(node, name, bytes, sizeof(bytes));
	if (length != 4)
		return -1;

	/* Decodes the cell. */
	*value = be32(bytes);

	/* Succeeded: the value is read. */
	return 0;
}

/* Reads a #address-cells or #size-cells property, or the fallback when it is missing or unusable. */
static int
address_cells(
	ofw_cell_t node,
	const char *name,
	unsigned int fallback)
{
	unsigned long value;
	int status;

	/* A missing property, zero cells, or more than four use the fallback. */
	status = property_u32(node, name, &value);
	if (status != 0 || value == 0 || value > 4)
		return (int)fallback;

	/* Reports the cell count. */
	return (int)value;
}

/* Reads a memory node's "reg" or "available" ranges. */
static int
memory_ranges(
	ofw_cell_t node,
	const char *name,
	struct sun4u_memory_range *ranges,
	unsigned char *count)
{
	unsigned char bytes[PROPERTY_BUFFER_SIZE];
	const unsigned char *entry;
	ofw_scell_t parent;
	unsigned int address_count;
	unsigned int size_count;
	unsigned int stride;
	unsigned int entries;
	unsigned int index;
	int length;

	/* The parent's cell counts give the layout of each range. */
	parent = ofw_parent(node);
	if (parent <= 0)
		return -1;

	/* Reads the cell counts and the property. */
	address_count = (unsigned int)address_cells((ofw_cell_t)parent, "#address-cells", 2);
	size_count = (unsigned int)address_cells((ofw_cell_t)parent, "#size-cells", 2);
	stride = (address_count + size_count) * 4U;
	length = ofw_getprop(node, name, bytes, sizeof(bytes));
	if (length <= 0 || stride == 0 || (unsigned int)length % stride != 0)
		return -1;

	/* Refuses no ranges or more than the handoff holds. */
	entries = (unsigned int)length / stride;
	if (entries == 0 || entries > SUN4U_MAX_MEMORY_RANGES)
		return -1;

	/* Decodes each range, refusing an empty one. */
	for (index = 0; index < entries; index++) {
		entry = bytes + index * stride;
		ranges[index].base = cells64(entry, address_count);
		ranges[index].size = cells64(entry + address_count * 4U, size_count);
		if (ranges[index].size == 0)
			return -1;
	}

	/* Publishes the count. */
	*count = (unsigned char)entries;

	/* Succeeded: the ranges are read. */
	return 0;
}

/* Finds the PCI I/O space's physical base from a PCI node's "ranges". */
static void
discover_pci_ranges(
	ofw_cell_t node,
	struct discovery *state)
{
	unsigned char bytes[PROPERTY_BUFFER_SIZE];
	const unsigned char *entry;
	ofw_scell_t parent;
	unsigned int child_address;
	unsigned int parent_address;
	unsigned int size_cells;
	unsigned int stride;
	unsigned int offset;
	unsigned long space;
	unsigned long long child;
	unsigned long long physical;
	int length;

	/* The first PCI node found gives the base; the parent's cells are needed to read it. */
	parent = ofw_parent(node);
	if (parent <= 0 || state->have_pci_io)
		return;

	/* Reads the cell counts and the ranges. */
	child_address = (unsigned int)address_cells(node, "#address-cells", 3);
	size_cells = (unsigned int)address_cells(node, "#size-cells", 2);
	parent_address = (unsigned int)address_cells((ofw_cell_t)parent, "#address-cells", 2);
	stride = (child_address + parent_address + size_cells) * 4U;
	length = ofw_getprop(node, "ranges", bytes, sizeof(bytes));
	if (length <= 0 || stride == 0 || (unsigned int)length % stride != 0)
		return;

	/* Takes the first I/O space range. */
	for (offset = 0; offset < (unsigned int)length; offset += stride) {
		/* Only an I/O space range of the expected cell layout gives the base. */
		entry = bytes + offset;
		space = be32(entry) >> 24 & 3U;
		if (space != 1 || child_address < 3 || parent_address > 2)
			continue;

		/* A range mapped below its own bus address gives no base. */
		child = cells64(entry + 4U, child_address - 1U);
		physical = cells64(entry + child_address * 4U, parent_address);
		if (physical < child)
			continue;

		/* The base is where bus address zero lies. */
		state->handoff->pci_io_base = physical - child;
		state->have_pci_io = 1;
		return;
	}
}

/* Finds the serial port's I/O offset from its "reg". */
static void
discover_serial(
	ofw_cell_t node,
	struct discovery *state)
{
	unsigned char bytes[32];
	ofw_scell_t parent;
	unsigned int address_count;
	int length;

	/* The first serial port found is the console. */
	if (state->have_serial)
		return;

	/* The parent's cells are needed to read the address. */
	parent = ofw_parent(node);
	if (parent <= 0)
		return;

	/* Reads the address, refusing a "reg" shorter than one. */
	address_count = (unsigned int)address_cells((ofw_cell_t)parent, "#address-cells", 2);
	length = ofw_getprop(node, "reg", bytes, sizeof(bytes));
	if (length < (int)(address_count * 4U) || address_count == 0)
		return;

	/* Records the port's offset. */
	state->handoff->serial_io_offset = (uint32_t)cells64(bytes, address_count);
	state->have_serial = 1;
}

/* Finds the CMD646 IDE controller's four I/O port ranges. */
static void
discover_ide(
	ofw_cell_t node,
	struct discovery *state,
	unsigned long vendor,
	unsigned long device)
{
	unsigned char bytes[PROPERTY_BUFFER_SIZE];
	unsigned short ports[4];
	unsigned int found;
	unsigned int offset;
	unsigned long space;
	unsigned long address;
	int length;

	/* Only the first CMD646 is taken. */
	found = 0;
	if (state->have_ide || vendor != 0x1095 || device != 0x0646)
		return;

	/* Refuses assigned addresses that are not whole PCI address entries. */
	length = ofw_getprop(node, "assigned-addresses", bytes, sizeof(bytes));
	if (length <= 0 || (unsigned int)length % 20U != 0)
		return;

	/* Takes the first four I/O space ranges as the controller's ports. */
	for (offset = 0; offset < (unsigned int)length && found < 4; offset += 20U) {
		space = be32(bytes + offset) >> 24 & 3U;
		address = be32(bytes + offset + 8U);
		if (space == 1 && address <= 0xffffU) {
			ports[found] = (unsigned short)address;
			found++;
		}
	}

	/* A controller without all four ports is not used. */
	if (found != 4)
		return;

	/* Records the controller and its ports. */
	state->handoff->ide_vendor = (uint16_t)vendor;
	state->handoff->ide_device = (uint16_t)device;
	state->handoff->ide_primary_command = ports[0];
	state->handoff->ide_primary_control = ports[1];
	state->handoff->ide_secondary_command = ports[2];
	state->handoff->ide_secondary_control = ports[3];
	state->have_ide = 1;
}

/* Takes the tick frequency from the first CPU node with a clock. */
static void
discover_cpu(
	ofw_cell_t node,
	struct discovery *state)
{
	unsigned long clock;
	int status;

	/* A CPU node without a clock, or with a zero one, gives nothing. */
	status = property_u32(node, "clock-frequency", &clock);
	if (status != 0 || clock == 0)
		return;

	/* Records the frequency. */
	state->handoff->tick_frequency = clock;
	state->have_cpu = 1;
}

/* Takes the installed and the available memory from the first memory node that has both. */
static void
discover_memory(
	ofw_cell_t node,
	struct discovery *state)
{
	int status;

	/* Reads the installed memory. */
	status = memory_ranges(node, "reg", state->handoff->installed, &state->handoff->installed_count);
	if (status != 0)
		return;

	/* Reads the available memory. */
	status = memory_ranges(node, "available", state->handoff->available, &state->handoff->available_count);
	if (status != 0)
		return;

	/* The memory is found. */
	state->have_memory = 1;
}

/* Examines a node and walks its children, refusing a tree deeper than 32. */
static int
walk_node(
	ofw_cell_t node,
	struct discovery *state,
	unsigned int depth)
{
	char type[32];
	char name[32];
	unsigned long vendor;
	unsigned long device;
	ofw_scell_t child;
	int is_cpu;
	int is_memory;
	int is_pci;
	int is_serial;
	int walked;

	/* A node without IDs matches no PCI device. */
	vendor = ~0UL;
	device = ~0UL;

	/* Refuses a tree too deep to walk. */
	if (depth > 32)
		return -1;

	/* Reads the properties that tell what the node is; a missing one stays empty. */
	type[0] = '\0';
	name[0] = '\0';
	(void)property_string(node, "device_type", type, sizeof(type));
	(void)property_string(node, "name", name, sizeof(name));
	(void)property_u32(node, "vendor-id", &vendor);
	(void)property_u32(node, "device-id", &device);

	/* The first CPU gives the tick frequency. */
	is_cpu = same_string(type, "cpu");
	if (is_cpu && !state->have_cpu)
		discover_cpu(node, state);

	/* The first memory node gives the memory ranges. */
	is_memory = same_string(type, "memory");
	if (is_memory && !state->have_memory)
		discover_memory(node, state);

	/* A PCI node may give the I/O space base. */
	is_pci = same_string(type, "pci");
	if (is_pci)
		discover_pci_ranges(node, state);

	/* The "su" node is the serial console. */
	is_serial = same_string(name, "su");
	if (is_serial)
		discover_serial(node, state);

	/* Any node may be the IDE controller. */
	discover_ide(node, state, vendor, device);

	/* Walks the children one sibling after another. */
	child = ofw_child(node);
	while (child > 0) {
		/* Refuses a subtree too deep. */
		walked = walk_node((ofw_cell_t)child, state, depth + 1U);
		if (walked != 0)
			return -1;

		child = ofw_peer((ofw_cell_t)child);
	}

	/* Succeeded: the subtree was walked. */
	return 0;
}

/* Copies the boot path, cut to the handoff's field and terminated. */
static void
copy_bootpath(
	char *destination,
	const char *source)
{
	unsigned int index;

	/* Copies up to the terminator or the field's end. */
	for (index = 0;
	     index + 1U < SUN4U_BOOTPATH_SIZE && source[index] != '\0';
	     index++)
		destination[index] = source[index];

	/* Terminates the path. */
	destination[index] = '\0';
}

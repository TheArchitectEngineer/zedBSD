/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A host driver for the sun4u handoff builder and the OpenFirmware client
 * calls it makes.  A fake client interface serves seeded random device
 * trees; the driver prints the handoff each one gives, so two builds of
 * the builder from two source trees can be compared by their output.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bootloader/sparcv9/handoff.h"
#include "bootloader/sparcv9/ofw.h"

#define NODE_MAX 24
#define PROPERTY_MAX 8

struct property {
	const char *name;
	unsigned char value[128];
	int length;
};

struct node {
	int parent;
	int child;
	int peer;
	int count;
	struct property properties[PROPERTY_MAX];
};

static struct node nodes[NODE_MAX];
static int node_count;
static uint32_t random_state;

static uint32_t next_random(void);
static ofw_cell_t fake_client(ofw_cell_t *arguments);
static void build_tree(void);

/* Builds random trees and prints the handoff of each. */
int
main(
	void)
{
	static struct sun4u_boot_handoff handoff;
	unsigned round;
	unsigned index;
	int result;

	/* Runs a fixed number of rounds. */
	random_state = 777U;
	for (round = 0; round < 20000U; round++) {
		build_tree();
		ofw_init(fake_client);
		memset(&handoff, 0x5a, sizeof(handoff));
		result = sparcv9_handoff_build(&handoff, next_random() % 2U == 0 ? "/pci@1f,0/ide@3/disk@0,0:a" : "x");
		printf("R %u %d %llu %u %u %llx %x %x %x %x %x %x %x %s", round, result,
		       (unsigned long long)handoff.tick_frequency, handoff.installed_count, handoff.available_count,
		       (unsigned long long)handoff.pci_io_base, handoff.serial_io_offset, handoff.ide_vendor, handoff.ide_device,
		       handoff.ide_primary_command, handoff.ide_primary_control, handoff.ide_secondary_command, handoff.ide_secondary_control,
		       handoff.bootpath);
		for (index = 0; index < handoff.installed_count && index < SUN4U_MAX_MEMORY_RANGES; index++)
			printf(" i%llx/%llx", (unsigned long long)handoff.installed[index].base, (unsigned long long)handoff.installed[index].size);
		for (index = 0; index < handoff.available_count && index < SUN4U_MAX_MEMORY_RANGES; index++)
			printf(" a%llx/%llx", (unsigned long long)handoff.available[index].base, (unsigned long long)handoff.available[index].size);
		printf(" %u %u %u %u\n", handoff.common.magic, handoff.common.version, handoff.common.size, handoff.extension_size);
	}

	/* Succeeded. */
	return 0;
}

/* Draws the next value of a fixed linear congruential sequence. */
static uint32_t
next_random(
	void)
{
	/* Advances the sequence. */
	random_state = random_state * 1103515245U + 12345U;

	/* Reports the upper bits. */
	return random_state >> 8;
}

/* Adds a property of big-endian cells to a node. */
static void
put_cells(
	int node,
	const char *name,
	const uint32_t *cells,
	int count)
{
	struct property *property;
	int index;

	/* Stores the cells, most significant byte first. */
	if (nodes[node].count == PROPERTY_MAX)
		return;
	property = &nodes[node].properties[nodes[node].count];
	nodes[node].count++;
	property->name = name;
	property->length = count * 4;
	for (index = 0; index < count && index < 32; index++) {
		property->value[index * 4] = (unsigned char)(cells[index] >> 24);
		property->value[index * 4 + 1] = (unsigned char)(cells[index] >> 16);
		property->value[index * 4 + 2] = (unsigned char)(cells[index] >> 8);
		property->value[index * 4 + 3] = (unsigned char)cells[index];
	}
}

/* Adds a string property to a node. */
static void
put_string(
	int node,
	const char *name,
	const char *value)
{
	struct property *property;

	/* Stores the string with its terminator. */
	if (nodes[node].count == PROPERTY_MAX)
		return;
	property = &nodes[node].properties[nodes[node].count];
	nodes[node].count++;
	property->name = name;
	property->length = (int)strlen(value) + 1;
	memcpy(property->value, value, (size_t)property->length);
}

/* Adds a node under a parent. */
static int
add_node(
	int parent)
{
	int node;
	int last;

	/* Links the node as its parent's last child. */
	node = node_count;
	node_count++;
	memset(&nodes[node], 0, sizeof(nodes[node]));
	nodes[node].parent = parent;
	if (parent >= 0) {
		if (nodes[parent].child == 0) {
			nodes[parent].child = node;
		} else {
			last = nodes[parent].child;
			while (nodes[last].peer != 0)
				last = nodes[last].peer;
			nodes[last].peer = node;
		}
	}

	/* Reports the node. */
	return node;
}

/* Builds a random sun4u-like device tree, sometimes broken. */
static void
build_tree(
	void)
{
	uint32_t cells[32];
	int root;
	int node;
	int pci;
	int count;
	int index;

	/* The root, with its cell counts. */
	node_count = 1;
	memset(&nodes[0], 0, sizeof(nodes[0]));
	root = add_node(-1);
	cells[0] = next_random() % 6U == 0 ? 1U : 2U;
	put_cells(root, "#address-cells", cells, 1);
	cells[0] = next_random() % 6U == 0 ? 1U : 2U;
	put_cells(root, "#size-cells", cells, 1);

	/* /chosen with its handles. */
	node = add_node(root);
	put_string(node, "name", "chosen");
	cells[0] = 7;
	put_cells(node, "stdout", cells, 1);
	put_cells(node, "mmu", cells, 1);
	put_cells(node, "memory", cells, 1);

	/* A CPU, sometimes without a clock. */
	if (next_random() % 10U != 0) {
		node = add_node(root);
		put_string(node, "device_type", "cpu");
		cells[0] = next_random() % 8U == 0 ? 0U : 100000000U + next_random() % 1000U;
		if (next_random() % 8U != 0)
			put_cells(node, "clock-frequency", cells, 1);
	}

	/* Memory, with random ranges. */
	if (next_random() % 10U != 0) {
		node = add_node(root);
		put_string(node, "device_type", "memory");
		count = (int)(next_random() % 5U) * 4;
		for (index = 0; index < count; index++)
			cells[index] = next_random() % 3U == 0 ? 0U : next_random();
		put_cells(node, "reg", cells, count);
		count = (int)(next_random() % 5U) * 4 + (next_random() % 15U == 0 ? 1 : 0);
		for (index = 0; index < count; index++)
			cells[index] = next_random() % 3U == 0 ? 0U : next_random();
		put_cells(node, "available", cells, count);
	}

	/* A PCI bus with ranges, a serial port and an IDE controller below it. */
	pci = add_node(root);
	put_string(pci, "device_type", next_random() % 10U == 0 ? "pcie" : "pci");
	cells[0] = 3;
	put_cells(pci, "#address-cells", cells, 1);
	cells[0] = 2;
	put_cells(pci, "#size-cells", cells, 1);
	count = 1 + (int)(next_random() % 2U);
	for (index = 0; index < count * 7 && index < 16; index++)
		cells[index] = next_random() % 4U == 0 ? 0x01000000U : next_random() % 0x100000U;
	put_cells(pci, "ranges", cells, count * 7 > 14 ? 14 : count * 7);

	/* The serial port. */
	node = add_node(pci);
	put_string(node, "name", next_random() % 8U == 0 ? "se" : "su");
	cells[0] = next_random();
	cells[1] = next_random();
	cells[2] = next_random();
	put_cells(node, "reg", cells, (int)(next_random() % 4U));

	/* The IDE controller with its assigned addresses. */
	node = add_node(pci);
	cells[0] = next_random() % 8U == 0 ? 0x1000U : 0x1095U;
	put_cells(node, "vendor-id", cells, 1);
	cells[0] = 0x0646U;
	put_cells(node, "device-id", cells, 1);
	count = (int)(next_random() % 3U) + 4;
	if (next_random() % 6U == 0)
		count = 3;
	for (index = 0; index < count * 5; index++)
		cells[index] = index % 5 == 0 ? (next_random() % 8U == 0 ? 0x02000000U : 0x01000000U) : next_random() % 0x14000U;
	put_cells(node, "assigned-addresses", cells, count * 5);
}

/* Finds a node's property by name. */
static const struct property *
find_property(
	int node,
	const char *name)
{
	int index;

	/* Compares each property's name. */
	for (index = 0; index < nodes[node].count; index++) {
		if (strcmp(nodes[node].properties[index].name, name) == 0)
			return &nodes[node].properties[index];
	}

	/* There is no such property. */
	return NULL;
}

/* Serves the client interface calls the handoff builder makes. */
static ofw_cell_t
fake_client(
	ofw_cell_t *arguments)
{
	const char *service;
	const struct property *property;
	unsigned long length;
	int node;

	/* Answers each service the builder uses. */
	service = (const char *)arguments[0];
	if (strcmp(service, "finddevice") == 0) {
		if (strcmp((const char *)arguments[3], "/") == 0)
			arguments[4] = 1;
		else if (strcmp((const char *)arguments[3], "/chosen") == 0)
			arguments[4] = 2;
		else
			arguments[4] = (ofw_cell_t)-1;
		return 0;
	}
	if (strcmp(service, "child") == 0) {
		arguments[4] = (ofw_cell_t)nodes[arguments[3]].child;
		return 0;
	}
	if (strcmp(service, "peer") == 0) {
		arguments[4] = (ofw_cell_t)nodes[arguments[3]].peer;
		return 0;
	}
	if (strcmp(service, "parent") == 0) {
		arguments[4] = (ofw_cell_t)(nodes[arguments[3]].parent < 0 ? 0 : nodes[arguments[3]].parent);
		return 0;
	}
	if (strcmp(service, "getprop") == 0) {
		node = (int)arguments[3];
		property = find_property(node, (const char *)arguments[4]);
		if (property == NULL) {
			arguments[7] = (ofw_cell_t)-1;
			return 0;
		}
		length = (unsigned long)property->length;
		if (length > arguments[6])
			length = arguments[6];
		memcpy((void *)arguments[5], property->value, length);
		arguments[7] = (ofw_cell_t)property->length;
		return 0;
	}

	/* Any other service fails. */
	return (ofw_cell_t)-1;
}

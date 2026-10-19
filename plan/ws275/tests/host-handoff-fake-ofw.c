/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the sun4u loader's firmware framebuffer and 8042 discovery
 * (WS275 p002): handoff.c runs against a fake OpenFirmware tree shaped like
 * QEMU's sun4u under OpenBIOS.  host-handoff.sh builds it with the
 * physical register read replaced by fake_phys_read32_le().
 */

#include <stdio.h>
#include <string.h>
#include "handoff.h"
#include "ofw.h"

/* One property of a fake node. */
struct fake_property {
	const char *name;
	const unsigned char *value;
	int length;
};

/* One fake node: its parent, first child, next sibling and properties. */
struct fake_node {
	int parent;
	int child;
	int peer;
	struct fake_property properties[12];
};

/* The fake tree, indexed by node handle; zero is no node. */
static struct fake_node nodes[16];

/* What the test controls: the byte order register, the console, failures. */
static unsigned long fake_qext;
static int fake_stdout_display;
static int fake_color_fails;
static int fake_translate_virtual;
static unsigned long long fake_translate_physical;
static unsigned int color_calls;
static unsigned long palette[256][3];
static int failures;

/* Node handles. */
#define N_ROOT 1
#define N_CHOSEN 2
#define N_ALIASES 3
#define N_CPU 4
#define N_MEMORY 5
#define N_PCI 6
#define N_EBUS 7
#define N_SU 8
#define N_I8042 9
#define N_IDE 10
#define N_VGA 11
#define I_SCREEN 0x100
#define I_SERIAL 0x200

static unsigned char be_buffer[64][64];
static int be_used;

static const unsigned char *
cells(int count, const unsigned long *values)
{
	unsigned char *out;
	int index;

	out = be_buffer[be_used++];
	for (index = 0; index < count; index++) {
		out[index * 4] = (unsigned char)(values[index] >> 24);
		out[index * 4 + 1] = (unsigned char)(values[index] >> 16);
		out[index * 4 + 2] = (unsigned char)(values[index] >> 8);
		out[index * 4 + 3] = (unsigned char)values[index];
	}
	return out;
}

static void
prop(int node, const char *name, const void *value, int length)
{
	int index;

	for (index = 0; nodes[node].properties[index].name != NULL; index++)
		;
	nodes[node].properties[index].name = name;
	nodes[node].properties[index].value = value;
	nodes[node].properties[index].length = length;
}

static void
prop_string(int node, const char *name, const char *value)
{
	prop(node, name, value, (int)strlen(value) + 1);
}

static void
prop_cells(int node, const char *name, int count, const unsigned long *values)
{
	prop(node, name, cells(count, values), count * 4);
}

static void
prop_u32(int node, const char *name, unsigned long value)
{
	prop_cells(node, name, 1, &value);
}

static void
link_node(int node, int parent)
{
	int last;

	nodes[node].parent = parent;
	if (nodes[parent].child == 0) {
		nodes[parent].child = node;
		return;
	}
	for (last = nodes[parent].child; nodes[last].peer != 0; last = nodes[last].peer)
		;
	nodes[last].peer = node;
}

/* Builds a tree like QEMU sun4u's under OpenBIOS, the display at a depth. */
static void
build_tree(unsigned long depth, int with_address)
{
	static const unsigned long memory_reg[4] = { 0, 0, 0, 0x8000000 };
	static const unsigned long pci_ranges[14] = {
		0x01000000, 0, 0, 0x1fe, 0x02000000, 0, 0x1000000,
		0x02000000, 0, 0, 0x1ff, 0x00000000, 0, 0x80000000
	};
	static const unsigned long su_reg[3] = { 1, 0x3f8, 8 };
	static const unsigned long i8042_reg[6] = { 1, 0x60, 1, 1, 0x64, 1 };
	static const unsigned long ide_assigned[20] = {
		0x01000810, 0, 0x500, 0, 8, 0x01000814, 0, 0x508, 0, 4,
		0x01000818, 0, 0x510, 0, 8, 0x0100081c, 0, 0x518, 0, 4
	};
	static const unsigned long vga_assigned[10] = {
		0x02001010, 0, 0x80000000, 0, 0x1000000,
		0x02001018, 0, 0x81000000, 0, 0x1000
	};

	memset(nodes, 0, sizeof(nodes));
	be_used = 0;
	link_node(N_CHOSEN, N_ROOT);
	link_node(N_ALIASES, N_ROOT);
	link_node(N_CPU, N_ROOT);
	link_node(N_MEMORY, N_ROOT);
	link_node(N_PCI, N_ROOT);
	link_node(N_EBUS, N_PCI);
	link_node(N_SU, N_EBUS);
	link_node(N_I8042, N_EBUS);
	link_node(N_IDE, N_PCI);
	link_node(N_VGA, N_PCI);
	prop_u32(N_ROOT, "#address-cells", 2);
	prop_u32(N_ROOT, "#size-cells", 2);
	prop_string(N_ALIASES, "screen", "/pci@1fe,0/QEMU,VGA@2");
	prop_string(N_CPU, "device_type", "cpu");
	prop_u32(N_CPU, "clock-frequency", 100000000);
	prop_string(N_MEMORY, "device_type", "memory");
	prop_cells(N_MEMORY, "reg", 4, memory_reg);
	prop_cells(N_MEMORY, "available", 4, memory_reg);
	prop_string(N_PCI, "device_type", "pci");
	prop_u32(N_PCI, "#address-cells", 3);
	prop_u32(N_PCI, "#size-cells", 2);
	prop_cells(N_PCI, "ranges", 14, pci_ranges);
	prop_u32(N_EBUS, "#address-cells", 2);
	prop_u32(N_EBUS, "#size-cells", 1);
	prop_string(N_SU, "name", "su");
	prop_cells(N_SU, "reg", 3, su_reg);
	prop_string(N_I8042, "name", "8042");
	prop_cells(N_I8042, "reg", 6, i8042_reg);
	prop_u32(N_IDE, "vendor-id", 0x1095);
	prop_u32(N_IDE, "device-id", 0x0646);
	prop_cells(N_IDE, "assigned-addresses", 20, ide_assigned);
	prop_string(N_VGA, "device_type", "display");
	prop_u32(N_VGA, "vendor-id", 0x1234);
	prop_u32(N_VGA, "device-id", 0x1111);
	prop_cells(N_VGA, "assigned-addresses", 10, vga_assigned);
	prop_u32(N_VGA, "width", 1024);
	prop_u32(N_VGA, "height", 768);
	prop_u32(N_VGA, "depth", depth);
	prop_u32(N_VGA, "linebytes", 1024 * ((depth + 7) / 8));
	if (with_address)
		prop_u32(N_VGA, "address", 0xfe000000);
}

/* The fake client interface. */

void
ofw_init(ofw_client_t client)
{
	(void)client;
}

ofw_scell_t
ofw_finddevice(const char *path)
{
	if (strcmp(path, "/") == 0)
		return N_ROOT;
	if (strcmp(path, "/chosen") == 0)
		return N_CHOSEN;
	if (strcmp(path, "/aliases") == 0)
		return N_ALIASES;
	if (strcmp(path, "/pci@1fe,0/QEMU,VGA@2") == 0)
		return N_VGA;
	return -1;
}

ofw_scell_t
ofw_child(ofw_cell_t node)
{
	return nodes[node].child;
}

ofw_scell_t
ofw_peer(ofw_cell_t node)
{
	return nodes[node].peer;
}

ofw_scell_t
ofw_parent(ofw_cell_t node)
{
	return nodes[node].parent;
}

int
ofw_getprop(ofw_cell_t node, const char *name, void *buffer, unsigned long size)
{
	struct fake_property *property;
	int index;

	for (index = 0; nodes[node].properties[index].name != NULL; index++) {
		property = &nodes[node].properties[index];
		if (strcmp(property->name, name) != 0)
			continue;
		memcpy(buffer, property->value, (unsigned long)property->length < size ? (unsigned long)property->length : size);
		return property->length;
	}
	return -1;
}

ofw_scell_t
ofw_instance_to_package(ofw_cell_t handle)
{
	if (handle == I_SCREEN)
		return N_VGA;
	if (handle == I_SERIAL)
		return N_SU;
	return -1;
}

int
ofw_call_method(const char *method, ofw_cell_t handle, unsigned int argument_count,
		const ofw_cell_t *arguments, unsigned int result_count, ofw_cell_t *results)
{
	(void)result_count;
	(void)results;
	if (strcmp(method, "color!") != 0 || handle != I_SCREEN || argument_count != 4)
		return -1;
	if (fake_color_fails)
		return -1;
	color_calls++;
	palette[arguments[0] & 0xff][0] = arguments[3];
	palette[arguments[0] & 0xff][1] = arguments[2];
	palette[arguments[0] & 0xff][2] = arguments[1];
	return 0;
}

int
ofw_translate(unsigned long virtual_address, unsigned long long *physical)
{
	if (!fake_translate_virtual || virtual_address != 0xfe000000)
		return -1;
	*physical = fake_translate_physical;
	return 0;
}

ofw_scell_t
ofw_open(const char *path)
{
	if (strcmp(path, "/pci@1fe,0/QEMU,VGA@2") == 0)
		return I_SCREEN;
	return 0;
}

ofw_cell_t
ofw_stdout_handle(void)
{
	return fake_stdout_display ? I_SCREEN : I_SERIAL;
}

unsigned long
fake_phys_read32_le(unsigned long long address)
{
	if (address != 0x1ff00000000ULL + 0x81000000ULL + 0x604)
		return 0xdeadbeef;
	return fake_qext;
}

static void
expect(const char *what, unsigned long long got, unsigned long long want)
{
	if (got == want)
		return;
	printf("FAIL %s: got %#llx, want %#llx\n", what, got, want);
	failures++;
}

static void
run(const char *label, unsigned long depth, unsigned long qext, int stdout_display,
    int color_fails, int translate, unsigned long long translated,
    unsigned long long want_phys, unsigned long want_format)
{
	static struct sun4u_boot_handoff handoff;
	int status;

	build_tree(depth, 1);
	fake_qext = qext;
	fake_stdout_display = stdout_display;
	fake_color_fails = color_fails;
	fake_translate_virtual = translate;
	fake_translate_physical = translated;
	color_calls = 0;
	status = sparcv9_handoff_build(&handoff, "/pci@1fe,0/ide@3/disk@0,0");
	printf("case %s\n", label);
	expect("status", (unsigned long long)status, 0);
	expect("phys", handoff.framebuffer_phys, want_phys);
	expect("format", handoff.framebuffer_format, want_format);
	expect("i8042 data", handoff.i8042_data, 0x60);
	expect("i8042 command", handoff.i8042_command, 0x64);
	expect("version", handoff.extension_version, 3);
	if (want_phys == 0)
		return;
	expect("width", handoff.framebuffer_width, 1024);
	expect("height", handoff.framebuffer_height, 768);
	expect("pitch", handoff.framebuffer_pitch, 1024 * ((depth + 7) / 8));
	expect("vendor", handoff.framebuffer_vendor, 0x1234);
	expect("device", handoff.framebuffer_device, 0x1111);
	if (want_format != KERN_BOOT_FRAMEBUFFER_RGB332)
		return;
	expect("color! calls", color_calls, 256);
	expect("palette 0x25 red", palette[0x25][0], 0x24);
	expect("palette 0x25 green", palette[0x25][1], 0x24);
	expect("palette 0x25 blue", palette[0x25][2], 0x55);
	expect("palette 0xec red", palette[0xec][0], 0xff);
	expect("palette 0xec green", palette[0xec][1], 0x6d);
	expect("palette 0xec blue", palette[0xec][2], 0x00);
	expect("palette 0xff blue", palette[0xff][2], 0xff);
}

int
main(void)
{
	const unsigned long long bar0 = 0x1ff00000000ULL + 0x80000000ULL;

	run("x32 qext LE, console is the screen", 32, 0x1e1e1e1e, 1, 0, 0, 0, bar0, KERN_BOOT_FRAMEBUFFER_BGRX8888);
	run("x32 qext BE, screen alias", 32, 0xbebebebe, 0, 0, 0, 0, bar0, KERN_BOOT_FRAMEBUFFER_XRGB8888);
	run("x32 qext other", 32, 0x12345678, 1, 0, 0, 0, bar0, KERN_BOOT_FRAMEBUFFER_XRGB8888);
	run("x8 palette", 8, 0x1e1e1e1e, 1, 0, 0, 0, bar0, KERN_BOOT_FRAMEBUFFER_RGB332);
	run("x8 color! fails", 8, 0x1e1e1e1e, 1, 1, 0, 0, 0, 0);
	run("x16 refused", 16, 0x1e1e1e1e, 1, 0, 0, 0, 0, 0);
	run("translate inside BAR", 32, 0x1e1e1e1e, 1, 0, 1, bar0 + 0x1000, bar0 + 0x1000, KERN_BOOT_FRAMEBUFFER_BGRX8888);
	run("translate outside BAR", 32, 0x1e1e1e1e, 1, 0, 1, 0x1234000ULL, bar0, KERN_BOOT_FRAMEBUFFER_BGRX8888);
	if (failures != 0) {
		printf("host-handoff: FAIL (%d)\n", failures);
		return 1;
	}
	printf("host-handoff: PASS\n");
	return 0;
}

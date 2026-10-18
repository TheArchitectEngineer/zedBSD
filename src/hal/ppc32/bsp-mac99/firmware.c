/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The board's firmware: the Open Firmware client interface, asked once,
 * before the HAL takes the machine, for the RAM, the time base's rate, the
 * display and the command line.  The answers make the kernel's handoff
 * and the named objects of hal_get_arch_handoff().
 *
 * Values referenced: IEEE Std 1275-1994 (Open Firmware), the client
 * interface's argument array and the finddevice, getprop, child and
 * instance-to-package services; the PowerPC binding (the display's width,
 * height, linebytes, depth and address properties).
 */

#include <hal/hal.h>
#include <kern/boot.h>
#include "../asm.h"
#include "bsp.h"

/* A missing node or property. */
#define FIRMWARE_FAILED		0xffffffffU

/* The most cells one call passes and returns. */
#define FIRMWARE_CELLS		8U

/* One call's argument array: the service's name, the counts, then the cells. */
struct firmware_arguments {
	uint32_t service;
	uint32_t argument_count;
	uint32_t return_count;
	uint32_t cells[FIRMWARE_CELLS];
};

/* What the firmware reported. */
static struct mac99_board_info board;

/* The kernel's handoff and the command line, handed over as "boot.command-line". */
static struct kern_boot_handoff kernel_handoff;
static char boot_command_line[KERN_BOOT_PARAMETERS_STORAGE_SIZE];

static uint32_t firmware(const char *service, unsigned arguments, unsigned returns, const uint32_t *in, uint32_t *out);
static uint32_t finddevice(const char *path);
static uint32_t getprop(uint32_t node, const char *name, void *buffer, uint32_t length);
static uint32_t getprop_word(uint32_t node, const char *name);
static void read_display(void);
static int name_is(const char *name, const char *wanted);

/*
 * Asks the firmware what the HAL needs, and prepares the kernel's handoff.
 */
void
mac99_firmware_read(
	void)
{
	uint32_t node;
	uint32_t cells[2];
	uint32_t length;

	/* The first RAM bank of the memory node. */
	node = finddevice("/memory");
	if (node != FIRMWARE_FAILED && getprop(node, "reg", cells, sizeof(cells)) >= sizeof(cells)) {
		board.memory_base = cells[0];
		board.memory_size = cells[1];
	}

	/* The time base's rate, from the first processor. */
	node = finddevice("/cpus");
	if (node != FIRMWARE_FAILED) {
		node = firmware("child", 1, 1, &node, cells) == 0 ? cells[0] : FIRMWARE_FAILED;
		if (node != FIRMWARE_FAILED && node != 0)
			board.timebase_frequency = getprop_word(node, "timebase-frequency");
	}

	/* The display and the command line. */
	read_display();
	node = finddevice("/chosen");
	if (node != FIRMWARE_FAILED) {
		length = getprop(node, "bootargs", boot_command_line, sizeof(boot_command_line) - 1U);
		if (length == FIRMWARE_FAILED)
			length = 0;
		if (length >= sizeof(boot_command_line))
			length = sizeof(boot_command_line) - 1U;
		boot_command_line[length] = '\0';
	}

	/* The common handoff: one boot device, the first MBR partition, its id 0x80. */
	hal_memset(&kernel_handoff, 0, sizeof(kernel_handoff));
	kernel_handoff.magic = KERN_HANDOFF_MAGIC;
	kernel_handoff.version = KERN_HANDOFF_VERSION_MULTIBOOT;
	kernel_handoff.size = sizeof(kernel_handoff);
	kernel_handoff.device_count = 1;
	kernel_handoff.boot_bios_id = 0x80;
	kernel_handoff.boot_partition_scheme = KERN_PARTITION_SCHEME_MBR;
	kernel_handoff.boot_partition_index = 1;
	kernel_handoff.boot_partition_lba = KERN_BOOT_PARTITION_LBA_UNKNOWN;
}

/*
 * Reports what the firmware said about the board.
 */
const struct mac99_board_info *
mac99_board(
	void)
{
	/* Succeeded: the record filled at boot. */
	return &board;
}

/*
 * Reports the handoff the kernel's entry reads.
 */
const struct kern_boot_handoff *
mac99_kernel_handoff(
	void)
{
	/* Succeeded: the handoff filled at boot. */
	return &kernel_handoff;
}

/*
 * Returns one named architecture handoff object: "boot.command-line" is
 * the firmware's command line, when it had one.
 */
void *
hal_get_arch_handoff(
	const char *name)
{
	/* A missing name names nothing. */
	if (name == NULL)
		return NULL;

	/* The command line, when the firmware passed one. */
	if (name_is(name, "boot.command-line") && boot_command_line[0] != '\0')
		return boot_command_line;

	/* An unknown handoff. */
	return NULL;
}

/* Calls a client interface service; reports zero when the firmware answered. */
static uint32_t
firmware(
	const char *service,
	unsigned arguments,
	unsigned returns,
	const uint32_t *in,
	uint32_t *out)
{
	struct firmware_arguments call;
	unsigned index;

	/* The array: the name, the counts, the arguments, room for the returns. */
	hal_memset(&call, 0, sizeof(call));
	call.service = (uint32_t)(uintptr_t)service;
	call.argument_count = arguments;
	call.return_count = returns;
	for (index = 0; index < arguments; index++)
		call.cells[index] = in[index];

	/* The firmware answers in place. */
	ppc32_firmware_call(&call);
	for (index = 0; index < returns; index++)
		out[index] = call.cells[arguments + index];
	return 0;
}

/* Finds a node by its path or alias. */
static uint32_t
finddevice(
	const char *path)
{
	uint32_t in;
	uint32_t out;

	/* The node's handle, or the failure value. */
	in = (uint32_t)(uintptr_t)path;
	(void)firmware("finddevice", 1, 1, &in, &out);
	return out;
}

/* Reads a property into a buffer; reports its size, or the failure value. */
static uint32_t
getprop(
	uint32_t node,
	const char *name,
	void *buffer,
	uint32_t length)
{
	uint32_t in[4];
	uint32_t out;

	/* The node, the name, the buffer and its length. */
	in[0] = node;
	in[1] = (uint32_t)(uintptr_t)name;
	in[2] = (uint32_t)(uintptr_t)buffer;
	in[3] = length;
	(void)firmware("getprop", 4, 1, in, &out);
	return out;
}

/* Reads a one-cell property, zero when it is missing. */
static uint32_t
getprop_word(
	uint32_t node,
	const char *name)
{
	uint32_t value;

	/* A property shorter than a cell counts as missing. */
	value = 0;
	if (getprop(node, name, &value, sizeof(value)) != sizeof(value))
		return 0;
	return value;
}

/*
 * Reads the display's address and shape: the screen alias's node, or the
 * node behind the firmware's output.
 */
static void
read_display(
	void)
{
	uint32_t node;
	uint32_t chosen;
	uint32_t instance;

	/* The screen alias first. */
	node = finddevice("screen");
	if (node == FIRMWARE_FAILED || node == 0) {
		chosen = finddevice("/chosen");
		if (chosen == FIRMWARE_FAILED)
			return;
		instance = getprop_word(chosen, "stdout");
		if (instance == 0)
			return;
		(void)firmware("instance-to-package", 1, 1, &instance, &node);
		if (node == FIRMWARE_FAILED || node == 0)
			return;
	}

	/* The properties the display's driver published. */
	board.framebuffer_address = getprop_word(node, "address");
	board.framebuffer_width = getprop_word(node, "width");
	board.framebuffer_height = getprop_word(node, "height");
	board.framebuffer_linebytes = getprop_word(node, "linebytes");
	board.framebuffer_depth = getprop_word(node, "depth");
}

/* Tells whether a handoff name is the one wanted. */
static int
name_is(
	const char *name,
	const char *wanted)
{
	size_t index;

	/* Compares character by character, then requires the same end. */
	for (index = 0; wanted[index] != '\0'; index++) {
		if (name[index] != wanted[index])
			return 0;
	}
	if (name[index] != '\0')
		return 0;
	return 1;
}

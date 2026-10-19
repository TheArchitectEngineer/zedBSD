/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The riscv64 boot record and the kernel's handoff.
 *
 * The firmware passes the boot hart's id and the device tree.  The HAL
 * keeps what it read from the tree, copies the command line out of it
 * while the tree is known to be intact, and gives the kernel the common
 * handoff and the named objects of hal_get_arch_handoff(): the command
 * line, the tree, the initial ramdisk and the boot hart.
 */

#include <hal/hal.h>
#include <kern/boot.h>
#include "bootinfo.h"
#include "defs.h"

/*
 * The facts read from the device tree, the tree's physical address and the
 * boot hart's id.  Filled once by riscv64_boot_record() on the boot hart
 * before any other hart starts, and read without a lock afterwards.
 */
static struct riscv64_fdt_info boot_fdt;
static uintptr_t boot_fdt_phys;
static uint64_t boot_hart_id;

/*
 * The named objects of hal_get_arch_handoff(), filled with the record and
 * not changed afterwards.  The ramdisk's size is zero when the firmware
 * loaded none.
 */
static struct riscv64_handoff_range handoff_fdt;
static struct riscv64_handoff_range handoff_initrd;
static uint64_t handoff_boot_hart;

/*
 * The handoff the kernel's entry reads: a single boot device, the first
 * partition of an MBR disk, the way the kernel finds its root on the
 * machines whose loader passes no device table.  Filled with the record.
 */
static struct kern_boot_handoff kernel_handoff;

/*
 * The command line handed to the kernel as "boot.command-line": the
 * firmware's (/chosen/bootargs), copied out of the tree, or the boot
 * parameters the loader passed instead.  The first byte is zero when there
 * is no line.
 */
static char boot_command_line[KERN_BOOT_PARAMETERS_STORAGE_SIZE];

static void copy_command_line(const struct riscv64_fdt_info *info, uintptr_t fdt_phys);
static void keep_command_line(const char *line, size_t available);
static int name_is(const char *name, const char *wanted);

/*
 * Records the boot hart, the device tree and what was read from it, and
 * prepares the kernel's handoff.
 */
void
riscv64_boot_record(
	uint64_t hart_id,
	uintptr_t fdt_phys,
	const struct riscv64_fdt_info *info)
{
	/* Keeps the facts for the rest of the HAL. */
	boot_fdt = *info;
	boot_fdt_phys = fdt_phys;
	boot_hart_id = hart_id;

	/* The named objects: the tree, the ramdisk and the boot hart. */
	handoff_fdt.phys = fdt_phys;
	handoff_fdt.size = info->totalsize;
	handoff_initrd.phys = 0;
	handoff_initrd.size = 0;
	if (info->initrd_end > info->initrd_start) {
		handoff_initrd.phys = info->initrd_start;
		handoff_initrd.size = info->initrd_end - info->initrd_start;
	}
	handoff_boot_hart = hart_id;

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

	/* The command line, out of the tree while it is intact. */
	copy_command_line(info, fdt_phys);
}

/*
 * Replaces the command line with the boot parameters the loader passed.
 *
 * The loader's parameters are the zedbsd.cfg lines; the firmware's own
 * line in the tree is not the kernel's when a loader ran.
 */
void
riscv64_boot_set_command_line(
	const char *text,
	size_t available)
{
	/* Keeps the loader's line in place of the tree's. */
	keep_command_line(text, available);
}

/*
 * Reports the facts read from the device tree.
 */
const struct riscv64_fdt_info *
riscv64_boot_fdt(
	void)
{
	/* Succeeded: the record filled at boot. */
	return &boot_fdt;
}

/*
 * Reports the device tree's physical address.
 */
uintptr_t
riscv64_boot_fdt_phys(
	void)
{
	/* Succeeded: the address the firmware passed. */
	return boot_fdt_phys;
}

/*
 * Reports the boot hart's id.
 */
uint64_t
riscv64_boot_hart(
	void)
{
	/* Succeeded: the id the firmware passed. */
	return boot_hart_id;
}

/*
 * Reports the command line, empty when the firmware passed none.
 */
const char *
riscv64_boot_command_line(
	void)
{
	/* Succeeded: the copy made at boot. */
	return boot_command_line;
}

/*
 * Reads a decimal parameter KEY=N from the command line; 0 when absent.
 *
 * key includes its '='.  The key must start a word.
 */
unsigned
riscv64_boot_parameter_number(
	const char *key)
{
	const char *line;
	unsigned position;
	unsigned matched;
	unsigned value;
	int at_word;

	/* Looks for the key at the start of a word. */
	line = boot_command_line;
	at_word = 1;
	for (position = 0; line[position] != '\0'; position++) {
		/* A space starts the next word. */
		if (line[position] == ' ') {
			at_word = 1;
			continue;
		}

		/* Inside a word the key cannot start. */
		if (!at_word)
			continue;
		at_word = 0;

		/* Compares the key. */
		for (matched = 0; key[matched] != '\0'; matched++) {
			if (line[position + matched] != key[matched])
				break;
		}
		if (key[matched] != '\0')
			continue;

		/* Reads the decimal value after the key, capped so it cannot overflow. */
		value = 0;
		position += matched;
		while (line[position] >= '0' && line[position] <= '9') {
			value = value * 10U + (unsigned)(line[position] - '0');
			if (value > 1000U)
				value = 1000U;
			position++;
		}

		/* Succeeded: the value given. */
		return value;
	}

	/* The parameter is absent. */
	return 0;
}

/*
 * Reports the handoff the kernel's entry reads.
 */
const void *
riscv64_kernel_handoff(
	void)
{
	/* Succeeded: the handoff filled with the boot record. */
	return &kernel_handoff;
}

/*
 * Returns one named architecture handoff object.
 *
 * "boot.command-line" is the firmware's command line; "riscv64.fdt" and
 * "riscv64.initrd" are physical ranges (struct riscv64_handoff_range, two
 * 64-bit words), the second only when the firmware loaded a ramdisk; and
 * "riscv64.boot-hart" is the boot hart's id as a 64-bit word.
 */
void *
hal_get_arch_handoff(
	const char *name)
{
	int match;

	/* A missing name names nothing. */
	if (name == NULL)
		return NULL;

	/* The firmware's command line, when it passed one. */
	match = name_is(name, "boot.command-line");
	if (match) {
		if (boot_command_line[0] == '\0')
			return NULL;
		return boot_command_line;
	}

	/* The device tree. */
	match = name_is(name, "riscv64.fdt");
	if (match)
		return &handoff_fdt;

	/* The initial ramdisk, when there is one. */
	match = name_is(name, "riscv64.initrd");
	if (match) {
		if (handoff_initrd.size == 0)
			return NULL;
		return &handoff_initrd;
	}

	/* The boot hart. */
	match = name_is(name, "riscv64.boot-hart");
	if (match)
		return &handoff_boot_hart;

	/* An unknown handoff. */
	return NULL;
}

/*
 * Copies the firmware's command line out of the device tree, while the
 * tree is known to be intact.
 */
static void
copy_command_line(
	const struct riscv64_fdt_info *info,
	uintptr_t fdt_phys)
{
	const char *line;

	/* Starts with no line, which is what an absent one leaves. */
	boot_command_line[0] = '\0';

	/* Leaves the line empty when the firmware passed none. */
	if (info->bootargs_offset == 0U || info->bootargs_length == 0U)
		return;

	/* Keeps the property's line. */
	line = (const char *)(RISCV64_DIRECT_BASE + fdt_phys + info->bootargs_offset);
	keep_command_line(line, info->bootargs_length);
}

/*
 * Keeps a command line of at most available bytes, ending at its
 * terminator if it has one.
 *
 * A line longer than the kernel keeps is cut at the last separator that
 * fits, so no parameter is handed over in part.
 */
static void
keep_command_line(
	const char *line,
	size_t available)
{
	size_t length;
	size_t limit;

	/* Measures the line up to its terminator, within what is available. */
	length = 0;
	while (length < available && line[length] != '\0')
		length++;

	/* Cuts a line the kernel cannot keep at its last separator. */
	limit = sizeof(boot_command_line) - 1U;
	if (length > limit) {
		length = limit;
		while (length > 0U && line[length] != ' ')
			length--;
	}

	/* Copies the line and terminates it. */
	hal_memcpy(boot_command_line, line, length);
	boot_command_line[length] = '\0';
}

/* Tells whether a handoff name is the one wanted. */
static int
name_is(
	const char *name,
	const char *wanted)
{
	size_t index;

	/* Compares the names character by character. */
	for (index = 0; wanted[index] != '\0'; index++) {
		if (name[index] != wanted[index])
			return 0;
	}

	/* The names are the same only when the name ends there too. */
	if (name[index] != '\0')
		return 0;
	return 1;
}

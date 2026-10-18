/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The sun4u boot handoff: the loader's record kept by the HAL, the common
 * record handed to the kernel, and the named values the kernel reads.
 */

#include <hal/hal.h>
#include "../bsp.h"

/*
 * The HAL's copy of the loader's handoff.
 *
 * Filled once by sun4u_boot_init() before anything reads it, and not changed
 * afterwards; the loader's own record is not referenced again.
 */
static struct sun4u_boot_handoff boot_handoff;

/*
 * The handoff the kernel's entry reads: the loader's common record, under
 * the common version and size.  Filled once with boot_handoff; what is
 * particular to the board goes by name.
 */
static struct kern_boot_handoff kernel_handoff;

/*
 * The named objects of hal_get_arch_handoff(): the I/O port offsets of the
 * CMD646 IDE controller's primary channel, its command block
 * ("sun4u.cmd646.primary.command") and its control block
 * ("sun4u.cmd646.primary.control"), each a 64-bit word.  Filled once with
 * boot_handoff, which the HAL accepts only for a CMD646, and not changed
 * afterwards.
 */
static uint64_t cmd646_primary_command;
static uint64_t cmd646_primary_control;

static int handoff_name_is(const char *name, const char *wanted);

/*
 * Keeps a copy of the loader's handoff and builds the kernel's from it.
 */
void
sun4u_boot_init(
	const struct sun4u_boot_handoff *handoff)
{
	/* Copies the loader's record, which the HAL has already checked. */
	hal_memcpy(&boot_handoff, handoff, sizeof(boot_handoff));

	/*
	 * Gives the kernel the common record alone, under the common version;
	 * the loader's own version tells the HAL only that the sun4u part
	 * follows.
	 */
	kernel_handoff = boot_handoff.common;
	kernel_handoff.version = KERN_HANDOFF_VERSION_MULTIBOOT;
	kernel_handoff.size = sizeof(kernel_handoff);

	/* Keeps the IDE controller's primary channel for its names. */
	cmd646_primary_command = boot_handoff.ide_primary_command;
	cmd646_primary_control = boot_handoff.ide_primary_control;
}

/*
 * Returns the HAL's copy of the loader's handoff.
 */
const struct sun4u_boot_handoff *
sun4u_boot_handoff(
	void)
{
	/* Reports the copy sun4u_boot_init() filled. */
	return &boot_handoff;
}

/*
 * Returns the handoff the kernel's entry reads: the common record alone.
 */
const struct kern_boot_handoff *
sun4u_kernel_handoff(
	void)
{
	/* Reports the record sun4u_boot_init() built. */
	return &kernel_handoff;
}

/*
 * Returns one named architecture handoff object.
 *
 * "sun4u.cmd646.primary.command" and "sun4u.cmd646.primary.control" are the
 * I/O port offsets of the CMD646 IDE controller's primary channel, each a
 * 64-bit word.
 */
void *
hal_get_arch_handoff(
	const char *name)
{
	int match;

	/* Exposes the IDE controller's primary command block. */
	match = handoff_name_is(name, "sun4u.cmd646.primary.command");
	if (match)
		return &cmd646_primary_command;

	/* Exposes the IDE controller's primary control block. */
	match = handoff_name_is(name, "sun4u.cmd646.primary.control");
	if (match)
		return &cmd646_primary_control;

	/* Reports an unavailable or unknown architecture handoff. */
	return NULL;
}

/*
 * Tells whether a handoff name is the one wanted.
 */
static int
handoff_name_is(
	const char *name,
	const char *wanted)
{
	size_t index;

	/* Refuses a missing name. */
	if (name == NULL)
		return 0;

	/* Compares the names character by character, terminator included. */
	for (index = 0; wanted[index] != '\0'; index++) {
		if (name[index] != wanted[index])
			return 0;
	}

	/* Reports whether the name ends where the wanted one does. */
	if (name[index] != '\0')
		return 0;
	return 1;
}

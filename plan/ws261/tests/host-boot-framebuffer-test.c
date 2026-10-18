/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of kern_boot_framebuffer(): the framebuffer is read from
 * six named values, all present or all absent.
 */

#include <stdio.h>
#include <string.h>

#include <uapi/errno.h>
#include <kern/platform.h>

/* The named values the stand-in platform hands over. */
static uint64_t values[6];

/* Whether the stand-in platform has a framebuffer. */
static int present;

/* The number of failed checks. */
static int failures;

static void check(int condition, const char *what);

/*
 * Stands in for the HAL's named handoff lookup.
 */
void *
hal_get_arch_handoff(
	const char *name)
{
	static const char *const names[6] = {
		"test.framebuffer.phys",
		"test.framebuffer.size",
		"test.framebuffer.width",
		"test.framebuffer.height",
		"test.framebuffer.pitch",
		"test.framebuffer.format",
	};
	unsigned index;

	/* Reports nothing without a framebuffer. */
	if (!present)
		return NULL;

	/* Finds the value by its name. */
	for (index = 0; index < 6U; index++) {
		if (strcmp(name, names[index]) == 0)
			return &values[index];
	}

	/* An unknown name. */
	return NULL;
}

/*
 * Runs the checks.
 */
int
main(
	void)
{
	struct kern_boot_framebuffer framebuffer;
	char prefix[80];
	int error;

	/* A framebuffer handed over is read whole. */
	present = 1;
	values[0] = 0x80000000ULL;
	values[1] = 1920ULL * 1080ULL * 4ULL;
	values[2] = 1920;
	values[3] = 1080;
	values[4] = 1920;
	values[5] = 2;
	error = kern_boot_framebuffer("test.framebuffer", &framebuffer);
	check(error == 0, "PRESENT: reads the framebuffer");
	check(framebuffer.physical_base == 0x80000000ULL && framebuffer.size == values[1], "PRESENT: base and size");
	check(framebuffer.width == 1920U && framebuffer.height == 1080U, "PRESENT: width and height");
	check(framebuffer.pitch == 1920U && framebuffer.format == 2U, "PRESENT: pitch and format");

	/* Another prefix names nothing. */
	error = kern_boot_framebuffer("other.framebuffer", &framebuffer);
	check(error == ENOENT && framebuffer.size == 0 && framebuffer.width == 0, "PREFIX: another prefix is absent and clears");

	/* No framebuffer. */
	present = 0;
	error = kern_boot_framebuffer("test.framebuffer", &framebuffer);
	check(error == ENOENT && framebuffer.physical_base == 0, "ABSENT: ENOENT and a cleared record");

	/* A value too wide for the record. */
	present = 1;
	values[2] = 0x100000000ULL;
	error = kern_boot_framebuffer("test.framebuffer", &framebuffer);
	check(error == EINVAL && framebuffer.width == 0, "WIDE: a width beyond 32 bits is refused");
	values[2] = 1920;

	/* A prefix that leaves no room for a name. */
	memset(prefix, 'a', sizeof(prefix) - 1U);
	prefix[sizeof(prefix) - 1U] = '\0';
	error = kern_boot_framebuffer(prefix, &framebuffer);
	check(error == EINVAL, "LONG: a prefix too long is refused");

	/* Missing arguments. */
	error = kern_boot_framebuffer(NULL, &framebuffer);
	check(error == EINVAL, "ARGS: a missing prefix");
	error = kern_boot_framebuffer("test.framebuffer", NULL);
	check(error == EINVAL, "ARGS: a missing record");

	/* Reports the tally. */
	printf("boot_framebuffer_host_test: %d failures\n", failures);
	if (failures != 0)
		return 1;
	return 0;
}

/* Counts and prints one failed check. */
static void
check(
	int condition,
	const char *what)
{
	/* A passed check leaves no trace. */
	if (condition)
		return;

	printf("FAIL: %s\n", what);
	failures++;
}

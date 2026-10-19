/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws277-p003: host test of the boot parameter dc8200.zerocopy= (off or
 * test) with src/kern/boot.c.
 */

#include <kern/boot.h>

#include <uapi/errno.h>

#include <stdio.h>
#include <string.h>

static unsigned checks;
static unsigned failures;

static void
check(
	int ok,
	const char *what)
{
	checks++;
	if (!ok) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

static int
parse(
	struct kern_boot_parameters *parameters,
	const char *text)
{
	char line[256];

	memset(line, 0, sizeof(line));
	strncpy(line, text, sizeof(line) - 1U);
	return kern_boot_parameters_parse(parameters, line, sizeof(line));
}

int
main(void)
{
	struct kern_boot_parameters parameters;
	const char *value;

	check(parse(&parameters, "dc8200.zerocopy=test login=graphical") == 0, "test is taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_DC8200_ZEROCOPY);
	check(value != NULL && strcmp(value, "test") == 0, "the value is test");
	check(kern_boot_parameters_unknown_count(&parameters) == 0U, "not an unknown parameter");
	check(parse(&parameters, "dc8200.zerocopy=off") == 0, "off is taken");
	check(parse(&parameters, "i915.gen9=on") == 0, "other parameters unchanged");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_DC8200_ZEROCOPY);
	check(value == NULL, "absent when not given");
	check(parse(&parameters, "dc8200.zerocopy=on") == EINVAL, "another word is refused");
	printf("boot-zerocopy-host-test: %u checks, %u failures\n", checks, failures);
	return failures != 0U;
}

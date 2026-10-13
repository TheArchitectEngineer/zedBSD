/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws229-p002: host test of the boot parameter i915.gen9=.
 *
 * Links src/kern/boot.c and checks that i915.gen9= takes on and off, is
 * not an unknown parameter, refuses any other word, and leaves i915.debug=
 * as it was.  Built and run by run-host-boot-gen9.sh.
 */

#include <kern/boot.h>

#include <uapi/errno.h>

#include <stdio.h>
#include <string.h>

/* The size of the line buffer a test parses. */
#define TEST_LINE_SIZE	256

/* The checks run and the checks failed; the tally decides the exit status. */
static unsigned checks;
static unsigned failures;

static void check(int ok, const char *what);
static int parse(struct kern_boot_parameters *parameters, const char *text);

/*
 * Runs the checks and reports the tally.
 */
int
main(void)
{
	struct kern_boot_parameters parameters;
	const char *value;
	int error;

	/* The on word, with nothing unknown. */
	error = parse(&parameters, "i915.gen9=on login=graphical");
	check(error == 0, "i915.gen9=on is taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9);
	check(value != NULL && strcmp(value, "on") == 0, "i915.gen9 is on");
	check(kern_boot_parameters_unknown_count(&parameters) == 0U, "i915.gen9 is not an unknown parameter");

	/* The off word. */
	error = parse(&parameters, "i915.gen9=off");
	check(error == 0, "i915.gen9=off is taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9);
	check(value != NULL && strcmp(value, "off") == 0, "i915.gen9 is off");

	/* Without the parameter it has no value. */
	error = parse(&parameters, "i915.debug=display");
	check(error == 0, "i915.debug=display is still taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9);
	check(value == NULL, "i915.gen9 is absent when not given");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_DEBUG);
	check(value != NULL && strcmp(value, "display") == 0, "i915.debug is display");

	/* Other words are refused. */
	error = parse(&parameters, "i915.gen9=yes");
	check(error == EINVAL, "i915.gen9=yes is refused");
	error = parse(&parameters, "i915.gen9=On");
	check(error == EINVAL, "i915.gen9=On is refused");

	printf("host-boot-gen9: %u checks, %u failures\n", checks, failures);

	/* Reports a failed check. */
	if (failures != 0U)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/* Counts one check and names it when it failed. */
static void
check(
	int ok,
	const char *what)
{
	/* Every check counts. */
	checks++;

	/* A failed check is named. */
	if (!ok) {
		failures++;
		printf("FAIL: %s\n", what);
	}
}

/* Parses a copy of the text and reports the parser's answer. */
static int
parse(
	struct kern_boot_parameters *parameters,
	const char *text)
{
	char line[TEST_LINE_SIZE];
	int error;

	/* Copies the text, which the parser terminates in place. */
	memset(line, 0, sizeof(line));
	strncpy(line, text, sizeof(line) - 1U);

	/* Parses the copy. */
	error = kern_boot_parameters_parse(parameters, line, sizeof(line));
	if (error != 0)
		return error;

	/* Succeeded: the record holds the line. */
	return 0;
}

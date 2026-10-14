/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws237-p002, p003: host test of the boot parameter i915.gen9.display=.
 *
 * Links src/kern/boot.c and checks that i915.gen9.display= takes copy,
 * flip, backlight, modeset and full, is not an unknown parameter, refuses
 * any other word, and leaves i915.gen9= apart from it.  Built and run by
 * run-host-boot-gen9-display.sh.
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

	/* The flip word, with nothing unknown, beside i915.gen9=. */
	error = parse(&parameters, "i915.gen9=on i915.gen9.display=flip login=graphical");
	check(error == 0, "i915.gen9.display=flip is taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9_DISPLAY);
	check(value != NULL && strcmp(value, "flip") == 0, "i915.gen9.display is flip");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9);
	check(value != NULL && strcmp(value, "on") == 0, "i915.gen9 is still on");
	check(kern_boot_parameters_unknown_count(&parameters) == 0U, "i915.gen9.display is not an unknown parameter");

	/* The copy word. */
	error = parse(&parameters, "i915.gen9.display=copy");
	check(error == 0, "i915.gen9.display=copy is taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9_DISPLAY);
	check(value != NULL && strcmp(value, "copy") == 0, "i915.gen9.display is copy");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9);
	check(value == NULL, "i915.gen9 is absent when only i915.gen9.display is given");

	/* The later steps' words. */
	error = parse(&parameters, "i915.gen9.display=backlight");
	check(error == 0, "i915.gen9.display=backlight is taken");
	error = parse(&parameters, "i915.gen9.display=modeset");
	check(error == 0, "i915.gen9.display=modeset is taken");
	error = parse(&parameters, "i915.gen9.display=full");
	check(error == 0, "i915.gen9.display=full is taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9_DISPLAY);
	check(value != NULL && strcmp(value, "full") == 0, "i915.gen9.display is full");

	/* Without the parameter it has no value. */
	error = parse(&parameters, "i915.gen9=off");
	check(error == 0, "i915.gen9=off is still taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_GEN9_DISPLAY);
	check(value == NULL, "i915.gen9.display is absent when not given");

	/* Other words are refused. */
	error = parse(&parameters, "i915.gen9.display=on");
	check(error == EINVAL, "i915.gen9.display=on is refused");
	error = parse(&parameters, "i915.gen9.display=Flip");
	check(error == EINVAL, "i915.gen9.display=Flip is refused");

	/* Reports the totals. */
	printf("host-boot-gen9-display: %u checks, %u failures\n", checks, failures);

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

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws083-p003b: host test of the words i915.debug= takes for video decode.
 *
 * Links src/kern/boot.c and checks that i915.debug= takes video and
 * display,video besides off and display, and still refuses another list.
 * Built and run by run-host-boot-video.sh.
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

	/* The video word alone. */
	error = parse(&parameters, "i915.debug=video");
	check(error == 0, "i915.debug=video is taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_DEBUG);
	check(value != NULL && strcmp(value, "video") == 0, "i915.debug is video");

	/* The display and video words together. */
	error = parse(&parameters, "display=edp i915.debug=display,video login=graphical");
	check(error == 0, "i915.debug=display,video is taken");
	value = kern_boot_parameters_value(&parameters, KERN_BOOT_PARAMETER_I915_DEBUG);
	check(value != NULL && strcmp(value, "display,video") == 0, "i915.debug is display,video");

	/* The earlier words are still taken. */
	error = parse(&parameters, "i915.debug=display");
	check(error == 0, "i915.debug=display is still taken");
	error = parse(&parameters, "i915.debug=off");
	check(error == 0, "i915.debug=off is still taken");

	/* Other lists and spellings are refused. */
	error = parse(&parameters, "i915.debug=video,display");
	check(error == EINVAL, "i915.debug=video,display is refused");
	error = parse(&parameters, "i915.debug=display,");
	check(error == EINVAL, "i915.debug=display, is refused");
	error = parse(&parameters, "i915.debug=videos");
	check(error == EINVAL, "i915.debug=videos is refused");
	error = parse(&parameters, "i915.debug=off,video");
	check(error == EINVAL, "i915.debug=off,video is refused");

	printf("host-boot-video: %u checks, %u failures\n", checks, failures);

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

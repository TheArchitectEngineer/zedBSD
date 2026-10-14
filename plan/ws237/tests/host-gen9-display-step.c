/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the step a Gen9 node takes when a step fails
 * (src/drivers/gpu/i915/display/gen9-display.c, WS237).
 *
 * Links src/kern/boot.c and gen9-display.c and checks that a failed step
 * lowers the node's step to the one below it with one line in the log,
 * that a failure of a step already given up changes and logs nothing,
 * that copy is the floor, that the step never rises again (also when the
 * boot line asks for more), and that two threads failing at once leave
 * the lower of their two steps.  Built and run by
 * run-host-gen9-display-step.sh.
 */

#include <kern/boot.h>

#include "src/drivers/gpu/i915/display/gen9-display.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* The size of the line buffer a test parses, and of the log the test keeps. */
#define TEST_LINE_SIZE	256
#define TEST_LOG_SIZE	4096

/* How many times the race of two failing threads is run, each in a child process of its own. */
#define TEST_RACES	200

/* The checks run and the checks failed; the tally decides the exit status. */
static unsigned checks;
static unsigned failures;

/*
 * The kernel log lines the code under test wrote since the last
 * log_reset(), and how many; the race's threads write it under the lock.
 */
static char log_text[TEST_LOG_SIZE];
static unsigned log_lines;
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void check(int ok, const char *what);
static void boot_line(const char *text);
static void log_reset(void);
static int log_has(const char *line);
static void check_race(void);
static void *race_flip(void *argument);
static void *race_modeset(void *argument);

/*
 * Runs the checks and reports the tally.
 */
int
main(void)
{
	/* The boot line asks for the mode set: nothing failed yet. */
	boot_line("i915.gen9.display=modeset");
	check(drv_i915_gen9_display_level() == I915_GEN9_DISPLAY_MODESET, "modeset is asked for");
	check(drv_i915_gen9_display_step() == I915_GEN9_DISPLAY_MODESET, "modeset is taken while nothing failed");

	/* Copy cannot fail. */
	log_reset();
	drv_i915_gen9_display_fail(I915_GEN9_DISPLAY_COPY, "copy", 5);
	check(drv_i915_gen9_display_step() == I915_GEN9_DISPLAY_MODESET, "a copy failure changes nothing");
	check(log_lines == 0U, "a copy failure logs nothing");

	/* The mode set fails: the backlight step, one line with the error. */
	log_reset();
	drv_i915_gen9_display_fail(I915_GEN9_DISPLAY_MODESET, "an external display could not be lit", 5);
	check(drv_i915_gen9_display_step() == I915_GEN9_DISPLAY_BACKLIGHT, "a mode set failure leaves the backlight step");
	check(log_lines == 1U, "a mode set failure logs one line");
	check(log_has("i915: gen9 display: modeset failed (an external display could not be lit: 5); using backlight\n"), "the mode set failure's line");

	/* The same step, or one above, failing again changes and logs nothing. */
	log_reset();
	drv_i915_gen9_display_fail(I915_GEN9_DISPLAY_MODESET, "again", 0);
	drv_i915_gen9_display_fail(I915_GEN9_DISPLAY_FULL, "above", 0);
	check(drv_i915_gen9_display_step() == I915_GEN9_DISPLAY_BACKLIGHT, "a step given up already changes nothing");
	check(log_lines == 0U, "a step given up already logs nothing");

	/* The report names the step taken and the one asked for. */
	log_reset();
	drv_i915_gen9_display_report();
	check(log_has("i915: gen9 display: using backlight (asked for modeset by i915.gen9.display=)\n"), "the report after the failure");

	/* The default asks for full; the failure still caps it. */
	boot_line("login=graphical");
	check(drv_i915_gen9_display_level() == I915_GEN9_DISPLAY_FULL, "the default asks for full");
	check(drv_i915_gen9_display_step() == I915_GEN9_DISPLAY_BACKLIGHT, "the failure caps the default");
	log_reset();
	drv_i915_gen9_display_report();
	check(log_has("i915: gen9 display: using backlight (asked for full by the default)\n"), "the report under the default");

	/* The flips fail: copy, one line without an error. */
	log_reset();
	drv_i915_gen9_display_fail(I915_GEN9_DISPLAY_FLIP, "a flip did not latch within 100 ms", 0);
	check(drv_i915_gen9_display_step() == I915_GEN9_DISPLAY_COPY, "a flip failure leaves copy");
	check(log_has("i915: gen9 display: flip failed (a flip did not latch within 100 ms); using copy\n"), "the flip failure's line");

	/* A later failure of a higher step does not raise it again. */
	log_reset();
	drv_i915_gen9_display_fail(I915_GEN9_DISPLAY_MODESET, "later", 5);
	check(drv_i915_gen9_display_step() == I915_GEN9_DISPLAY_COPY, "a later mode set failure does not raise the step");
	check(log_lines == 0U, "a later mode set failure logs nothing");

	/* The boot line asking for copy is copy. */
	boot_line("i915.gen9.display=copy");
	check(drv_i915_gen9_display_step() == I915_GEN9_DISPLAY_COPY, "copy asked for is copy");

	/* Two threads failing at once. */
	check_race();

	/* Reports the totals. */
	printf("host-gen9-display-step: %u checks, %u failures\n", checks, failures);

	/* Reports a failed check. */
	if (failures != 0U)
		return 1;

	/* Succeeded: every check passed. */
	return 0;
}

/*
 * Stands in for the kernel log: keeps the lines for the checks.
 */
void
kern_logf(
	const char *format,
	...)
{
	va_list arguments;
	size_t used;

	/* Appends the line under the lock, which the race's threads share. */
	pthread_mutex_lock(&log_lock);

	used = strlen(log_text);
	va_start(arguments, format);
	vsnprintf(log_text + used, sizeof(log_text) - used, format, arguments);
	va_end(arguments);
	log_lines++;

	pthread_mutex_unlock(&log_lock);
}

/*
 * Stands in for the kernel's string comparison.
 */
int
kern_strcmp(
	const char *left,
	const char *right)
{
	int order;

	/* The host's comparison. */
	order = strcmp(left, right);

	/* Succeeded: the order of the two strings. */
	return order;
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

/* Makes a copy of the text the system's boot line. */
static void
boot_line(
	const char *text)
{
	static char line[TEST_LINE_SIZE];
	int error;

	/* Copies the text, which the parser terminates in place and the record keeps pointing into. */
	memset(line, 0, sizeof(line));
	strncpy(line, text, sizeof(line) - 1U);

	/* Parses the copy as the system's boot line. */
	error = kern_boot_parameters_initialize(line, sizeof(line));
	check(error == 0, "the boot line is taken");
}

/* Forgets the log lines kept so far. */
static void
log_reset(void)
{
	/* No line kept. */
	log_text[0] = '\0';
	log_lines = 0U;
}

/* Reports nonzero when the kept log holds the line. */
static int
log_has(
	const char *line)
{
	const char *found;

	/* Looks for the line among the kept ones. */
	found = strstr(log_text, line);
	if (found == NULL)
		return 0;

	/* Succeeded: the line was logged. */
	return 1;
}

/*
 * Runs TEST_RACES races of a flip failure and a mode set failure on two
 * threads, each race in a child process that starts at full, and checks
 * that every race leaves copy.
 */
static void
check_race(void)
{
	pthread_t flip_thread;
	pthread_t modeset_thread;
	unsigned race;
	unsigned lost;
	pid_t child;
	int status;
	int step;

	/* Each race in a child of its own, whose step starts as a boot's does. */
	lost = 0U;
	for (race = 0U; race < TEST_RACES; race++) {
		child = fork();
		if (child == 0) {
			boot_line("i915.gen9.display=full");
			pthread_create(&modeset_thread, NULL, race_modeset, NULL);
			pthread_create(&flip_thread, NULL, race_flip, NULL);
			pthread_join(modeset_thread, NULL);
			pthread_join(flip_thread, NULL);
			step = drv_i915_gen9_display_step();
			_exit(step);
		}

		/* The child's step, or a lost race when it did not end as one. */
		status = 0;
		waitpid(child, &status, 0);
		if (!WIFEXITED(status) || WEXITSTATUS(status) != I915_GEN9_DISPLAY_COPY)
			lost++;
	}

	/* Succeeded when no race raised the step back above copy. */
	check(lost == 0U, "two failures at once leave the lower step");
}

/* Fails the flip step, on a thread of the race. */
static void *
race_flip(
	void *argument)
{
	(void)argument;

	/* The flips fail. */
	drv_i915_gen9_display_fail(I915_GEN9_DISPLAY_FLIP, "race", 0);

	/* Succeeded: nothing to hand back. */
	return NULL;
}

/* Fails the mode set step, on a thread of the race. */
static void *
race_modeset(
	void *argument)
{
	(void)argument;

	/* An external display fails. */
	drv_i915_gen9_display_fail(I915_GEN9_DISPLAY_MODESET, "race", 0);

	/* Succeeded: nothing to hand back. */
	return NULL;
}

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the ACPI side of S0 idle (ws052-p003).
 *
 * It is linked into the WS049 harness (aml-host) with the linker's
 * --wrap=drv_acpi_reset: the harness loads the tables, runs _REG and _INI,
 * starts the events and the EC as its options say, and when it frees the
 * namespace at its end, this file first plays the steps WS052_SLEEP names
 * and prints one line for each.  The steps are separated by ';':
 *
 *   lps0-attach | lps0-enter | lps0-exit
 *   power PATH STATE        drv_acpi_device_power_set (STATE 0 to 4, 4 D3cold)
 *   wake-on PATH STATE      drv_acpi_device_wake_enable
 *   wake-off PATH           drv_acpi_device_wake_disable
 *   wake-state PATH         drv_acpi_device_wake_state (_S0W)
 *   sleep-begin | sleep-end drv_acpi_events_sleep_begin / _end
 *   enabled GPE             the GPE's enable bit in the simulated block
 *   raise GPE               sets the GPE's status and takes the SCI
 *   sci                     takes the SCI without raising anything
 *   eval PATH               evaluates PATH and prints an integer result
 *
 * The enable bits are read from the harness's simulated GPE block (16
 * bytes at port 0x620: GPEs 0 to 63), so "enabled" needs --events without
 * firmware.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <drivers/acpi/acpi.h>

#include "drivers/acpi/aml-os.h"

#include "aml-host-hardware.h"

/* The harness's simulated GPE block: its port and how many GPEs it carries. */
#define GPE_BLOCK_PORT		0x0620U
#define GPE_BLOCK_GPES		64U

/* The longest script and the most words one step has. */
#define SCRIPT_MAX		4096U
#define STEP_WORDS		3U

/*
 * One kind of step: its first word, how many words it takes in all, and
 * what plays it with those words.
 */
struct step_kind {
	const char *name;
	unsigned words;
	void (*play)(char **words);
};

void __real_drv_acpi_reset(void);
void __wrap_drv_acpi_reset(void);

static void step_lps0_attach(char **words);
static void step_lps0_enter(char **words);
static void step_lps0_exit(char **words);
static void step_power(char **words);
static void step_wake_on(char **words);
static void step_wake_off(char **words);
static void step_wake_state(char **words);
static void step_sleep_begin(char **words);
static void step_sleep_end(char **words);
static void step_enabled(char **words);
static void step_raise(char **words);
static void step_sci(char **words);
static void step_eval(char **words);

/* The steps a script can name. */
static const struct step_kind step_kinds[] = {
	{ "lps0-attach", 1U, step_lps0_attach },
	{ "lps0-enter", 1U, step_lps0_enter },
	{ "lps0-exit", 1U, step_lps0_exit },
	{ "power", 3U, step_power },
	{ "wake-on", 3U, step_wake_on },
	{ "wake-off", 2U, step_wake_off },
	{ "wake-state", 2U, step_wake_state },
	{ "sleep-begin", 1U, step_sleep_begin },
	{ "sleep-end", 1U, step_sleep_end },
	{ "enabled", 2U, step_enabled },
	{ "raise", 2U, step_raise },
	{ "sci", 1U, step_sci },
	{ "eval", 2U, step_eval }
};

static void run_script(const char *script);
static void run_step(char *step);
static struct drv_acpi_node *find_node(const char *path);
static unsigned parse_number(const char *text);

/*
 * Plays the steps of WS052_SLEEP once, then frees the namespace as the
 * harness asked.
 */
void
__wrap_drv_acpi_reset(void)
{
	static bool played;
	const char *script;

	/* Plays the script at the first reset only, while the namespace is still there. */
	script = getenv("WS052_SLEEP");
	if (!played && script != NULL) {
		played = true;
		run_script(script);
	}

	/* Frees the namespace. */
	__real_drv_acpi_reset();
}

/* Splits the script into its steps and plays each in order. */
static void
run_script(
	const char *script)
{
	char copy[SCRIPT_MAX];
	char *step;
	char *rest;
	size_t length;

	/* Refuses a script longer than the copy. */
	length = strlen(script);
	if (length >= sizeof(copy)) {
		printf("SCRIPT too long\n");
		return;
	}

	/* Plays each step between the separators. */
	memcpy(copy, script, length + 1U);
	rest = copy;
	for (;;) {
		/* Takes the next step; the end of the script ends the loop. */
		step = strsep(&rest, ";");
		if (step == NULL)
			break;

		/* Plays it. */
		run_step(step);
	}
}

/* Splits one step into its words and plays it. */
static void
run_step(
	char *step)
{
	char *words[STEP_WORDS];
	unsigned count;
	unsigned index;
	char *word;
	int compared;

	/* Collects the words; repeated spaces leave empty words, which are skipped. */
	count = 0;
	for (;;) {
		/* Takes the next word; the end of the step ends the loop. */
		word = strsep(&step, " ");
		if (word == NULL)
			break;

		/* Skips an empty word and one beyond the room. */
		if (word[0] == '\0' || count == STEP_WORDS)
			continue;

		/* Keeps the word. */
		words[count] = word;
		count++;
	}

	/* An empty step does nothing. */
	if (count == 0)
		return;

	/* Plays the step its first word names, with the words it takes. */
	for (index = 0; index < sizeof(step_kinds) / sizeof(step_kinds[0]); index++) {
		/* Skips another kind of step. */
		compared = strcmp(words[0], step_kinds[index].name);
		if (compared != 0)
			continue;

		/* Refuses a step with the wrong number of words. */
		if (count != step_kinds[index].words) {
			printf("STEP %s takes %u words\n", words[0], step_kinds[index].words);
			return;
		}

		/* Plays it. */
		step_kinds[index].play(words);
		return;
	}

	/* No kind of step has the name. */
	printf("STEP unknown: %s\n", words[0]);
}

/* Finds the LPS0 device. */
static void
step_lps0_attach(
	char **words)
{
	int error;

	/* The step takes no words but its name. */
	(void)words;

	/* Attaches and prints what it reported. */
	error = drv_acpi_lps0_attach();
	printf("LPS0-ATTACH %d\n", error);
}

/* Makes the notifications into the low-power idle. */
static void
step_lps0_enter(
	char **words)
{
	int error;

	/* The step takes no words but its name. */
	(void)words;

	/* Enters and prints what it reported. */
	error = drv_acpi_lps0_enter();
	printf("LPS0-ENTER %d\n", error);
}

/* Makes the notifications out of the low-power idle. */
static void
step_lps0_exit(
	char **words)
{
	int error;

	/* The step takes no words but its name. */
	(void)words;

	/* Exits and prints what it reported. */
	error = drv_acpi_lps0_exit();
	printf("LPS0-EXIT %d\n", error);
}

/* Puts a device into a D-state. */
static void
step_power(
	char **words)
{
	struct drv_acpi_node *node;
	unsigned state;
	int error;

	/* Finds the device; a path that names nothing is printed as such. */
	node = find_node(words[1]);
	if (node == NULL)
		return;

	/* Changes its state and prints what it reported. */
	state = parse_number(words[2]);
	error = drv_acpi_device_power_set(node, (enum drv_acpi_device_state)state);
	printf("POWER %s %u %d\n", words[1], state, error);
}

/* Enables a device's wake. */
static void
step_wake_on(
	char **words)
{
	struct drv_acpi_node *node;
	unsigned state;
	int error;

	/* Finds the device; a path that names nothing is printed as such. */
	node = find_node(words[1]);
	if (node == NULL)
		return;

	/* Enables its wake and prints what it reported. */
	state = parse_number(words[2]);
	error = drv_acpi_device_wake_enable(node, (enum drv_acpi_device_state)state);
	printf("WAKE-ON %s %d\n", words[1], error);
}

/* Disables a device's wake. */
static void
step_wake_off(
	char **words)
{
	struct drv_acpi_node *node;
	int error;

	/* Finds the device; a path that names nothing is printed as such. */
	node = find_node(words[1]);
	if (node == NULL)
		return;

	/* Disables its wake and prints what it reported. */
	error = drv_acpi_device_wake_disable(node);
	printf("WAKE-OFF %s %d\n", words[1], error);
}

/* Reads the deepest D-state a device wakes from. */
static void
step_wake_state(
	char **words)
{
	struct drv_acpi_node *node;
	enum drv_acpi_device_state state;
	int error;

	/* Finds the device; a path that names nothing is printed as such. */
	node = find_node(words[1]);
	if (node == NULL)
		return;

	/* Reads _S0W and prints the state, or the error. */
	error = drv_acpi_device_wake_state(node, &state);
	if (error != 0) {
		printf("WAKE-STATE %s error %d\n", words[1], error);
		return;
	}

	/* Prints the state. */
	printf("WAKE-STATE %s %u\n", words[1], (unsigned)state);
}

/* Leaves only the armed GPEs enabled. */
static void
step_sleep_begin(
	char **words)
{
	int error;

	/* The step takes no words but its name. */
	(void)words;

	/* Begins the sleep and prints what it reported. */
	error = drv_acpi_events_sleep_begin();
	printf("SLEEP-BEGIN %d\n", error);
}

/* Enables the runtime GPEs again, and prints the GPE that woke the system. */
static void
step_sleep_end(
	char **words)
{
	unsigned woken;
	int error;

	/* The step takes no words but its name. */
	(void)words;

	/* Ends the sleep; a refusal is printed alone. */
	error = drv_acpi_events_sleep_end(&woken);
	if (error != 0) {
		printf("SLEEP-END %d\n", error);
		return;
	}

	/* A sleep that no GPE ended. */
	if (woken == DRV_ACPI_GPE_NONE) {
		printf("SLEEP-END 0 woken none\n");
		return;
	}

	/* Prints the GPE that fired first. */
	printf("SLEEP-END 0 woken 0x%x\n", woken);
}

/* Prints a GPE's enable bit in the simulated block. */
static void
step_enabled(
	char **words)
{
	uint32_t value;
	unsigned gpe;
	unsigned bit;

	/* Refuses a GPE beyond the simulated block. */
	gpe = parse_number(words[1]);
	if (gpe >= GPE_BLOCK_GPES) {
		printf("ENABLED 0x%x beyond the block\n", gpe);
		return;
	}

	/* Reads the enable register in the block's second half. */
	value = 0;
	(void)drv_acpi_os_port_read(GPE_BLOCK_PORT + GPE_BLOCK_GPES / 8U + gpe / 8U, 8, &value);

	/* Prints the bit. */
	bit = 0;
	if ((value & (1U << (gpe % 8U))) != 0)
		bit = 1;

	/* Prints the GPE and its bit. */
	printf("ENABLED 0x%x %u\n", gpe, bit);
}

/* Raises a GPE and takes the SCI as the kernel would. */
static void
step_raise(
	char **words)
{
	unsigned gpe;

	/* Sets the GPE's status bit. */
	gpe = parse_number(words[1]);
	hardware_raise_gpe(gpe);
	printf("RAISE 0x%x\n", gpe);

	/* Takes the SCI. */
	step_sci(words);
}

/* Takes the SCI: the interrupt's part, then the thread's. */
static void
step_sci(
	char **words)
{
	bool pending;

	/* The step takes no words but its name. */
	(void)words;

	/* Records and masks what fired, as the interrupt does. */
	pending = drv_acpi_sci_interrupt();
	if (!pending) {
		printf("SCI none\n");
		return;
	}

	/* Runs the handlers, as the thread does. */
	printf("SCI pending\n");
	drv_acpi_events_process();
}

/* Evaluates a path and prints its integer. */
static void
step_eval(
	char **words)
{
	uint64_t value;
	int error;

	/* Evaluates it; an error is printed as such. */
	error = drv_acpi_evaluate_integer(NULL, words[1], &value);
	if (error != 0) {
		printf("EVAL %s error %d\n", words[1], error);
		return;
	}

	/* Prints the integer. */
	printf("EVAL %s 0x%llx\n", words[1], (unsigned long long)value);
}

/* Finds the node of a path, printing a line when it names nothing. */
static struct drv_acpi_node *
find_node(
	const char *path)
{
	struct drv_acpi_node *node;
	int error;

	/* Looks the path up from the root. */
	error = drv_acpi_lookup(NULL, path, &node);
	if (error != 0) {
		printf("PATH %s error %d\n", path, error);
		return NULL;
	}

	/* Succeeded: reports the node. */
	return node;
}

/* Reads a number in decimal, or in hexadecimal after 0x. */
static unsigned
parse_number(
	const char *text)
{
	unsigned long value;

	/* Reads it in the base its prefix says. */
	value = strtoul(text, NULL, 0);

	/* Reports the number. */
	return (unsigned)value;
}

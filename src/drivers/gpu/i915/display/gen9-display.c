/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The step of i915.gen9.display= a Gen9 node takes (see gen9-display.h).
 */

#include "gen9-display.h"
#include <kern/kcrt.h>

#include <kern/boot.h>

#include <stddef.h>

/* The words of i915.gen9.display=, in the order of their steps. */
static const char *const i915_gen9_display_words[] = {
	"copy",
	"flip",
	"backlight",
	"modeset",
	"full"
};

/*
 * Reports the step a word of i915.gen9.display= names, or -1 for another
 * word.
 */
int
drv_i915_gen9_display_word(
	const char *word)
{
	unsigned index;
	int different;

	/* Compares the word with each step's. */
	for (index = 0U; index < sizeof(i915_gen9_display_words) / sizeof(i915_gen9_display_words[0]); index++) {
		different = kern_strcmp(word, i915_gen9_display_words[index]);
		if (different == 0)
			return (int)index;
	}

	/* Succeeded: the word names no step. */
	return -1;
}

/*
 * Reports the step the boot line, or I915_GEN9_DISPLAY_DEFAULT when the
 * line has no i915.gen9.display=, asks for.
 */
int
drv_i915_gen9_display_level(void)
{
	const struct kern_boot_parameters *parameters;
	const char *value;
	int level;

	/* Without the boot parameters the default chooses. */
	parameters = kern_boot_parameters_current();
	if (parameters == NULL)
		return I915_GEN9_DISPLAY_DEFAULT;

	/* Without i915.gen9.display= the default chooses. */
	value = kern_boot_parameters_value(parameters, KERN_BOOT_PARAMETER_I915_GEN9_DISPLAY);
	if (value == NULL)
		return I915_GEN9_DISPLAY_DEFAULT;

	/* The parser took only the known words; another one would be the default's. */
	level = drv_i915_gen9_display_word(value);
	if (level < 0)
		return I915_GEN9_DISPLAY_DEFAULT;

	/* Succeeded: the step the line names. */
	return level;
}

/* Reports the name of a step, for the log. */
const char *
drv_i915_gen9_display_name(
	int level)
{
	/* A step outside the words has no name. */
	if (level < 0 || (unsigned)level >= sizeof(i915_gen9_display_words) / sizeof(i915_gen9_display_words[0]))
		return "?";

	/* Succeeded: the step's word. */
	return i915_gen9_display_words[level];
}

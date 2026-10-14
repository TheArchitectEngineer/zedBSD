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

#include <kern/atomic.h>
#include <kern/boot.h>
#include <kern/klog.h>

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
 * The highest step the node may take: full until a step fails, then the
 * step below the one that failed.
 *
 * It only ever falls, so the steps a failure gave up stay given up for the
 * rest of the boot.  The binding, the panel's presentations under its
 * lease mutex and the external displays under the node's mutex may fail
 * at the same time, so it is lowered by a compare and exchange that never
 * raises it again.  Read by the starts of the steps and by the hotplug
 * work.  XXX: one device.
 */
static atomic_uint_t i915_gen9_display_ceiling = { I915_GEN9_DISPLAY_FULL };

static int i915_gen9_display_asked(int *explicit);

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
	int explicit;
	int level;

	/* The step the line names, or the default's. */
	level = i915_gen9_display_asked(&explicit);

	/* Succeeded: the step asked for. */
	return level;
}

/*
 * Reports the name of a step, for the log.
 *
 * A step outside the words is "?".
 */
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

/*
 * Reports the step the node takes now: the one the boot line, or the
 * default, asks for, lowered below any step that failed.
 */
int
drv_i915_gen9_display_step(void)
{
	unsigned ceiling;
	int level;

	/* The step asked for. */
	level = drv_i915_gen9_display_level();

	/* A step that failed caps it. */
	ceiling = atomic_load_acquire(&i915_gen9_display_ceiling);
	if ((unsigned)level > ceiling)
		level = (int)ceiling;

	/* Succeeded: the step the node takes. */
	return level;
}

/*
 * Logs that a step failed and lowers the node's step below it.
 *
 * The line is "i915: gen9 display: <step> failed (<reason>); using <step>",
 * with ": <error>" after the reason when error is not zero, and the step
 * used is the one drv_i915_gen9_display_step() reports from then on.  A
 * step at or below copy cannot fail.  A failure of a step a failure before
 * already gave up changes nothing and logs nothing: the caller's own line
 * says what happened.  Any context but an interrupt handler.
 */
void
drv_i915_gen9_display_fail(
	int level,
	const char *reason,
	int error)
{
	unsigned ceiling;
	unsigned below;
	int lowered;
	int used;

	/* Copy is the floor: the firmware's framebuffer is always there. */
	if (level <= I915_GEN9_DISPLAY_COPY)
		return;

	/*
	 * Lowers the ceiling to the step below the failed one, unless a
	 * failure before (here or on another thread) took it as low already;
	 * a lost exchange reloads the ceiling and tries again.
	 */
	below = (unsigned)level - 1U;
	lowered = 0;
	ceiling = atomic_load_acquire(&i915_gen9_display_ceiling);
	while (below < ceiling) {
		lowered = atomic_compare_exchange(&i915_gen9_display_ceiling, &ceiling, below);
		if (lowered)
			break;
	}

	/* A step given up before: nothing changes. */
	if (!lowered)
		return;

	/* The step the node uses from now on. */
	used = drv_i915_gen9_display_step();

	/* The one line that says what failed, why, and what the node uses. */
	if (error != 0) {
		kern_logf("i915: gen9 display: %s failed (%s: %d); using %s\n",
		    drv_i915_gen9_display_name(level),
		    reason,
		    error,
		    drv_i915_gen9_display_name(used));
	} else {
		kern_logf("i915: gen9 display: %s failed (%s); using %s\n",
		    drv_i915_gen9_display_name(level),
		    reason,
		    drv_i915_gen9_display_name(used));
	}
}

/*
 * Logs the step the node takes and the one asked for, once its display is
 * bound: "i915: gen9 display: using <step> (asked for <step> by
 * i915.gen9.display=, or by the default)".
 */
void
drv_i915_gen9_display_report(void)
{
	const char *source;
	int explicit;
	int asked;
	int step;

	/* The step asked for and where it came from, and the step taken. */
	asked = i915_gen9_display_asked(&explicit);
	step = drv_i915_gen9_display_step();
	source = "the default";
	if (explicit)
		source = "i915.gen9.display=";

	/* Succeeded: the one line that tells the step the display runs at. */
	kern_logf("i915: gen9 display: using %s (asked for %s by %s)\n",
	    drv_i915_gen9_display_name(step),
	    drv_i915_gen9_display_name(asked),
	    source);
}

/*
 * Reports the step the boot line, or I915_GEN9_DISPLAY_DEFAULT when the
 * line has no i915.gen9.display=, asks for, and whether the line named it.
 */
static int
i915_gen9_display_asked(
	int *explicit)
{
	const struct kern_boot_parameters *parameters;
	const char *value;
	int level;

	/* The default answers until the line is found to name a step. */
	*explicit = 0;

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
	*explicit = 1;
	return level;
}

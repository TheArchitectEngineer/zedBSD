/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The application switcher's state (ws142-p005; switcher.h says what it
 * is).  The applications are taken when it opens and kept until it
 * closes, so that the order does not move under the user while the
 * selection does.
 */

#include "switcher.h"

#include <stdio.h>
#include <string.h>

/*
 * Opens the switcher on the applications, latest use first, with the one
 * after the current one selected (the only one when there is one).
 * Returns 0, or -1 when there is no application (it stays off).
 */
int
zwl_switcher_open(
	struct zwl_switcher *switcher,
	const struct zwl_apps *apps,
	unsigned via,
	unsigned placement)
{
	unsigned index;

	/* Nothing to switch to. */
	memset(switcher, 0, sizeof(*switcher));
	if (apps->count == 0U)
		return -1;

	/* The keys in the order of their latest use. */
	for (index = 0; index < apps->count; index++)
		(void)snprintf(switcher->keys[index], sizeof(switcher->keys[index]), "%s", apps->apps[apps->recent[index]].key);
	switcher->count = apps->count;

	/* On, with the application used before the current one. */
	switcher->on = 1;
	switcher->via = via;
	switcher->placement = placement;
	switcher->index = 0;
	if (switcher->count > 1U)
		switcher->index = 1;

	/* Succeeded: open. */
	return 0;
}

/* Moves the selection by a number of steps, around at the ends. */
void
zwl_switcher_step(
	struct zwl_switcher *switcher,
	int delta)
{
	int count;
	int index;

	/* Only while on. */
	if (!switcher->on || switcher->count == 0U)
		return;

	/* The new place, in the range. */
	count = (int)switcher->count;
	index = ((int)switcher->index + delta % count + count) % count;
	switcher->index = (unsigned)index;
}

/*
 * Takes the fingers' travel across the pad (micrometres, positive to the
 * right): a step for each ZWL_SWITCHER_STEP_UM, the rest kept for the
 * next.  Returns the steps taken (negative to the left).
 */
int
zwl_switcher_travel(
	struct zwl_switcher *switcher,
	int32_t dx_um)
{
	int steps;

	/* Only while on. */
	if (!switcher->on)
		return 0;

	/* Whole steps; the rest waits. */
	switcher->travel_um += dx_um;
	steps = switcher->travel_um / ZWL_SWITCHER_STEP_UM;
	switcher->travel_um -= steps * ZWL_SWITCHER_STEP_UM;
	zwl_switcher_step(switcher, steps);

	/* Succeeded: the steps. */
	return steps;
}

/* Gives the selected application's key, or NULL while off. */
const char *
zwl_switcher_selected(
	const struct zwl_switcher *switcher)
{
	/* Off. */
	if (!switcher->on || switcher->count == 0U)
		return NULL;

	/* Succeeded: the selection. */
	return switcher->keys[switcher->index];
}

/* Closes the switcher. */
void
zwl_switcher_close(
	struct zwl_switcher *switcher)
{
	/* Off, with nothing kept. */
	memset(switcher, 0, sizeof(*switcher));
}

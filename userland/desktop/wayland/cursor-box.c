/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The cursor's box (ws213-p004): the pointer's place the input thread
 * writes as the reports come and the main thread's warps replace, and the
 * outputs' rectangles it is computed over (cursor-box.h).
 */

#include "cursor-box.h"
#include "kwl.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static void box_write(struct kwl_cursor_box *box, int32_t x, int32_t y, unsigned output, uint64_t event_us, struct kwl_cursor_motion *motion);

/*
 * Makes the box with the pointer at a place of the anchor, whose size is
 * the only output's until the main thread gives the outputs.
 *
 * The change signal uses the monotonic clock, as every timed wait of the
 * compositor's threads does.
 */
int
kwl_cursor_box_init(
	struct kwl_cursor_box *box,
	int32_t x,
	int32_t y,
	uint32_t width,
	uint32_t height)
{
	pthread_condattr_t attributes;
	int error;

	/* The place, on the anchor, and no write yet. */
	memset(box, 0, sizeof(*box));
	box->x = x;
	box->y = y;
	box->output = KWL_PLANE_ANCHOR;
	box->outputs[KWL_PLANE_ANCHOR].width = width;
	box->outputs[KWL_PLANE_ANCHOR].height = height;
	box->output_count = KWL_PLANE_SLOTS;

	/* The cursor lock. */
	error = pthread_mutex_init(&box->lock, NULL);
	if (error != 0)
		return error;

	/* The signal a writer gives the cursor thread, timed by the monotonic clock. */
	error = pthread_condattr_init(&attributes);
	if (error != 0) {
		pthread_mutex_destroy(&box->lock);
		return error;
	}

	/* The clock of the timed waits. */
	error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
	if (error == 0)
		error = pthread_cond_init(&box->changed, &attributes);
	if (error != 0) {
		pthread_condattr_destroy(&attributes);
		pthread_mutex_destroy(&box->lock);
		return error;
	}

	/* The cursor thread's answers to the main thread, on the same clock. */
	error = pthread_cond_init(&box->answered, &attributes);
	pthread_condattr_destroy(&attributes);
	if (error != 0) {
		pthread_cond_destroy(&box->changed);
		pthread_mutex_destroy(&box->lock);
		return error;
	}

	/* Succeeded: the box can be shared. */
	return 0;
}

/*
 * Destroys the box's lock and signal once no thread uses them.
 */
void
kwl_cursor_box_destroy(
	struct kwl_cursor_box *box)
{
	/* The signals, then the lock. */
	pthread_cond_destroy(&box->answered);
	pthread_cond_destroy(&box->changed);
	pthread_mutex_destroy(&box->lock);
}

/*
 * Moves the pointer by a relative motion over the outputs (the input
 * thread's mouse and touch pad): into the output the motion reaches, or
 * stopped at an edge no output shares (plane.c).
 */
void
kwl_cursor_box_move(
	struct kwl_cursor_box *box,
	int64_t dx,
	int64_t dy,
	uint64_t event_us,
	struct kwl_cursor_motion *motion)
{
	int32_t x;
	int32_t y;
	unsigned output;

	/* The new place computed from the box's and written in one section. */
	pthread_mutex_lock(&box->lock);

	output = kwl_plane_move(box->outputs, box->output_count, box->output, box->x, box->y, (int32_t)dx, (int32_t)dy, &x, &y);
	box_write(box, x, y, output, event_us, motion);

	pthread_mutex_unlock(&box->lock);
}

/*
 * Places the pointer where an absolute pointer's report maps on the anchor
 * (QEMU's tablet, design 6.1), the same report's relative motion added and
 * kept on the anchor.  The warp count does not move: the main thread hears
 * of it in the order the input thread wrote it.
 */
void
kwl_cursor_box_place(
	struct kwl_cursor_box *box,
	int32_t value_x,
	int32_t minimum_x,
	int32_t maximum_x,
	int32_t value_y,
	int32_t minimum_y,
	int32_t maximum_y,
	int64_t dx,
	int64_t dy,
	uint64_t event_us,
	struct kwl_cursor_motion *motion)
{
	const struct kwl_plane_rect *anchor;
	int32_t x;
	int32_t y;

	/* The place scaled onto the anchor as it is now, written in one section. */
	pthread_mutex_lock(&box->lock);

	anchor = &box->outputs[KWL_PLANE_ANCHOR];
	x = kwl_cursor_scale_absolute(value_x, minimum_x, maximum_x, anchor->width);
	y = kwl_cursor_scale_absolute(value_y, minimum_y, maximum_y, anchor->height);
	x = kwl_cursor_clamp((int64_t)x + dx, anchor->width);
	y = kwl_cursor_clamp((int64_t)y + dy, anchor->height);
	box_write(box, x, y, KWL_PLANE_ANCHOR, event_us, motion);

	pthread_mutex_unlock(&box->lock);
}

/*
 * Moves the pointer for the main thread (a warp, design 6.1 N-3): the move
 * computes the new place from the box's own, not from the main thread's
 * view that may be behind it, and the outputs' rectangles (when given)
 * change in the same section, so the input thread never computes a motion
 * from a new place over old rectangles or the reverse.  The warp count
 * moves, so the input thread's motions computed before it are not taken
 * as the pointer's place.
 */
void
kwl_cursor_box_warp(
	struct kwl_cursor_box *box,
	const struct kwl_plane_rect *outputs,
	unsigned count,
	kwl_cursor_box_move_fn *move,
	const void *context,
	struct kwl_cursor_motion *motion)
{
	int32_t x;
	int32_t y;
	unsigned output;

	/* The rectangles, the place and the count in one section. */
	pthread_mutex_lock(&box->lock);

	if (outputs != NULL) {
		memcpy(box->outputs, outputs, KWL_PLANE_SLOTS * sizeof(outputs[0]));
		box->output_count = count;
	}

	/* The warp's new place from the box's own. */
	x = box->x;
	y = box->y;
	output = box->output;
	move(context, box->outputs, &x, &y, &output);
	box->warp++;
	box_write(box, x, y, output, 0U, motion);

	pthread_mutex_unlock(&box->lock);
}

/*
 * Gives the box the outputs' rectangles as the main thread has them now
 * (a display opened, closed, moved or resized), for the motions from now
 * on.  A pointer the change takes off its output is carried by a warp.
 */
void
kwl_cursor_box_outputs(
	struct kwl_cursor_box *box,
	const struct kwl_plane_rect *outputs,
	unsigned count)
{
	int same;

	/* The rectangles replaced when they differ. */
	pthread_mutex_lock(&box->lock);

	same = memcmp(box->outputs, outputs, KWL_PLANE_SLOTS * sizeof(outputs[0]));
	if (same != 0 || box->output_count != count) {
		memcpy(box->outputs, outputs, KWL_PLANE_SLOTS * sizeof(outputs[0]));
		box->output_count = count;
	}

	pthread_mutex_unlock(&box->lock);
}

/*
 * Reads the pointer's place and output as the box has them now.
 */
void
kwl_cursor_box_read(
	struct kwl_cursor_box *box,
	int32_t *x,
	int32_t *y,
	unsigned *output)
{
	/* One consistent place. */
	pthread_mutex_lock(&box->lock);

	*x = box->x;
	*y = box->y;
	*output = box->output;

	pthread_mutex_unlock(&box->lock);
}

/*
 * Takes the cursor thread's counts of the window and starts them again.
 */
void
kwl_cursor_box_take_stats(
	struct kwl_cursor_box *box,
	struct kwl_cursor_stats *stats)
{
	/* The counts, and zero for the next window. */
	pthread_mutex_lock(&box->lock);

	*stats = box->stats;
	memset(&box->stats, 0, sizeof(box->stats));

	pthread_mutex_unlock(&box->lock);
}

/*
 * Maps one absolute axis value onto 0 .. size - 1 pixels.
 */
int32_t
kwl_cursor_scale_absolute(
	int32_t value,
	int32_t minimum,
	int32_t maximum,
	uint32_t size)
{
	int64_t offset;
	int64_t range;

	/* The low end of the range, and anything below it, is the first pixel. */
	if (size <= 1U || value <= minimum)
		return 0;

	/* The high end of the range, and anything above it, is the last pixel. */
	if (value >= maximum)
		return (int32_t)size - 1;

	/* Everything between scales linearly, rounding down. */
	offset = (int64_t)value - minimum;
	range = (int64_t)maximum - minimum;

	/* Succeeded: the pixel the value falls on. */
	return (int32_t)((offset * ((int64_t)size - 1)) / range);
}

/*
 * Keeps a pointer coordinate inside 0 .. size - 1.
 */
int32_t
kwl_cursor_clamp(
	int64_t position,
	uint32_t size)
{
	/* Nothing lies left of or above the output. */
	if (position < 0)
		return 0;

	/* Nothing lies right of or below the output. */
	if (position >= (int64_t)size)
		return (int32_t)size - 1;

	/* Succeeded: the position is already on the output. */
	return (int32_t)position;
}

/* Writes a place into the box (its lock held), wakes the cursor thread and reports the write. */
static void
box_write(
	struct kwl_cursor_box *box,
	int32_t x,
	int32_t y,
	unsigned output,
	uint64_t event_us,
	struct kwl_cursor_motion *motion)
{
	uint64_t now;

	/* What changed, before the place is replaced. */
	motion->moved = 0U;
	if (x != box->x || y != box->y || output != box->output)
		motion->moved = 1U;
	motion->crossed = 0U;
	if (output != box->output)
		motion->crossed = 1U;

	/*
	 * The place, and the times of its report and of this write: the box's
	 * lag is measured between them (design 6.4).  A warp has no report.
	 */
	now = kwl_microseconds();
	box->x = x;
	box->y = y;
	box->output = output;
	if (event_us != 0U)
		box->event_us = event_us;
	box->write_us = now;
	box->writes++;
	box->sequence++;

	/* What the writer tells the main thread. */
	motion->x = x;
	motion->y = y;
	motion->output = output;
	motion->warp = box->warp;
	motion->event_us = event_us;
	motion->write_us = now;

	/* The cursor thread wakes for a new place (its wait is under this lock). */
	if (motion->moved)
		pthread_cond_broadcast(&box->changed);
}

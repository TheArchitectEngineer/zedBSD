/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The track of a touch pad's two-finger scrolling (KL_VERSION 40,
 * BUG-211): the last moves of the fingers and their times, from which
 * their velocity is worked out when they lift, so that the content flies
 * on.  kl_scroll keeps one; a program with a scroll of its own (Settings)
 * keeps one too.  Nothing here draws or allocates.
 */

#include <keiland.h>
#include <keiland-ui.h>

#include <string.h>

static unsigned axis_track_slot(const struct kl_axis_track *track, unsigned age);

/*
 * Empties a track of a touch pad's scrolling.
 */
void
kl_axis_track_reset(
	struct kl_axis_track *track)
{
	/* No moves. */
	memset(track, 0, sizeof(*track));
}

/*
 * Adds one move of the fingers (pixels, as a wheel scrolls) at a time to
 * the track; the oldest goes when it is full.
 */
void
kl_axis_track_add(
	struct kl_axis_track *track,
	double dx,
	double dy,
	uint64_t now_us)
{
	/* The move in the next slot. */
	track->dx[track->next] = dx;
	track->dy[track->next] = dy;
	track->us[track->next] = now_us;
	track->next = (track->next + 1U) % KL_AXIS_TRACK_SAMPLES;

	/* One more, up to the slots. */
	if (track->count < KL_AXIS_TRACK_SAMPLES)
		track->count++;
}

/*
 * Works out the fingers' velocity (pixels a second, as a wheel scrolls) at
 * a time: the moves of the last KL_AXIS_TRACK_WINDOW_US after the oldest
 * of them, over the time since it.  Fingers that rested
 * KL_AXIS_TRACK_REST_US or longer, or a single move, give none.
 */
void
kl_axis_track_velocity(
	const struct kl_axis_track *track,
	uint64_t now_us,
	double *vx,
	double *vy)
{
	unsigned age;
	unsigned slot;
	unsigned newer;
	unsigned used;
	uint64_t newest_us;
	uint64_t oldest_us;
	double sum_x;
	double sum_y;
	double seconds;

	/* None until there is something to go by. */
	*vx = 0.0;
	*vy = 0.0;
	if (track->count < 2U)
		return;

	/* Fingers that rested before lifting throw nothing. */
	slot = axis_track_slot(track, 0U);
	newest_us = track->us[slot];
	if (now_us > newest_us && now_us - newest_us >= KL_AXIS_TRACK_REST_US)
		return;

	/* The moves within the window, newest first; the oldest of them starts the time and is not counted. */
	sum_x = 0.0;
	sum_y = 0.0;
	used = 0U;
	oldest_us = newest_us;
	for (age = 0U; age < track->count; age++) {
		slot = axis_track_slot(track, age);
		if (newest_us - track->us[slot] > KL_AXIS_TRACK_WINDOW_US)
			break;
		if (age != 0U) {
			newer = axis_track_slot(track, age - 1U);
			sum_x += track->dx[newer];
			sum_y += track->dy[newer];
			used++;
		}
		oldest_us = track->us[slot];
	}

	/* The distance after the oldest move counted, over the time since it. */
	if (used == 0U || newest_us <= oldest_us)
		return;
	seconds = (double)(newest_us - oldest_us) / 1000000.0;
	*vx = sum_x / seconds;
	*vy = sum_y / seconds;
}

/* Finds the slot of a track's move of an age (0 the newest). */
static unsigned
axis_track_slot(
	const struct kl_axis_track *track,
	unsigned age)
{
	unsigned slot;

	/* Back from the next free slot. */
	slot = (track->next + KL_AXIS_TRACK_SAMPLES - 1U - age) % KL_AXIS_TRACK_SAMPLES;

	/* Reports the slot. */
	return slot;
}

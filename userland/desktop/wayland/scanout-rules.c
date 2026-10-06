/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The rules of the game mode (scanout-rules.h, ws122-p005b): a fullscreen
 * video or game, alone on the output, goes straight to the display.
 */

#include "scanout-rules.h"

#include <stddef.h>

/* The answers' words, by their number. */
static const char *const scanout_reason_names[] = {
	"direct", "no-window", "not-fullscreen", "content", "buffer", "size", "overlay", "shot", "pointer", "refused"
};

/*
 * Decides one pass: the first reason the image is not shown straight, or
 * ZWL_SCANOUT_DIRECT.
 */
unsigned
zwl_scanout_decide(
	const struct zwl_scanout_facts *facts)
{
	/* A fullscreen window on top. */
	if (!facts->window)
		return ZWL_SCANOUT_NO_WINDOW;
	if (!facts->fullscreen)
		return ZWL_SCANOUT_NOT_FULLSCREEN;

	/* That asks for it: a video or a game (the user's decision: only what the application asks for). */
	if (facts->content_type != ZWL_SCANOUT_CONTENT_VIDEO && facts->content_type != ZWL_SCANOUT_CONTENT_GAME)
		return ZWL_SCANOUT_CONTENT;

	/* Its image a GPU buffer of the output's size. */
	if (!facts->gpu_buffer)
		return ZWL_SCANOUT_BUFFER;
	if (!facts->size_matches)
		return ZWL_SCANOUT_SIZE;

	/* Nothing over it, no screenshot waiting for a composed frame. */
	if (facts->overlay)
		return ZWL_SCANOUT_OVERLAY;
	if (facts->shot)
		return ZWL_SCANOUT_SHOT;

	/* The pointer still (a moving one is drawn), and a display that has not refused it. */
	if (facts->pointer_still_ms < ZWL_SCANOUT_POINTER_IDLE_MS)
		return ZWL_SCANOUT_POINTER;
	if (facts->refused)
		return ZWL_SCANOUT_REFUSED;

	/* Succeeded: straight to the display. */
	return ZWL_SCANOUT_DIRECT;
}

/*
 * Names an answer for the log.
 */
const char *
zwl_scanout_reason_name(
	unsigned reason)
{
	/* One of the answers. */
	if (reason < sizeof(scanout_reason_names) / sizeof(scanout_reason_names[0]))
		return scanout_reason_names[reason];

	/* Any other. */
	return "unknown";
}

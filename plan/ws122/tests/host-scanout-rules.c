/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws122-p005b: the host test of the game mode's rules (userland/desktop/
 * wayland/scanout-rules.c): a fullscreen video or game alone on the output
 * with a still pointer is shown straight, and each fact that is missing
 * gives its reason, the first in the rules' order.
 */

#include "userland/desktop/wayland/scanout-rules.h"

#include <stdio.h>
#include <string.h>

static int test_passed;
static int test_failed;

static void check(int ok, const char *what);
static void ready(struct zwl_scanout_facts *facts);

int
main(
	void)
{
	struct zwl_scanout_facts facts;
	unsigned answer;

	/* A fullscreen video alone with a still pointer: straight. */
	ready(&facts);
	answer = zwl_scanout_decide(&facts);
	check(answer == ZWL_SCANOUT_DIRECT, "a fullscreen video alone: direct");
	facts.content_type = ZWL_SCANOUT_CONTENT_GAME;
	answer = zwl_scanout_decide(&facts);
	check(answer == ZWL_SCANOUT_DIRECT, "a fullscreen game alone: direct");

	/* Each missing fact. */
	ready(&facts);
	facts.window = 0U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_NO_WINDOW, "no window: no-window");
	ready(&facts);
	facts.fullscreen = 0U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_NOT_FULLSCREEN, "a window not fullscreen: not-fullscreen");
	ready(&facts);
	facts.content_type = 0U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_CONTENT, "a fullscreen window that does not ask (none): content");
	facts.content_type = 1U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_CONTENT, "a photo: content");
	ready(&facts);
	facts.gpu_buffer = 0U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_BUFFER, "shared memory: buffer");
	ready(&facts);
	facts.size_matches = 0U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_SIZE, "another size: size");
	ready(&facts);
	facts.overlay = 1U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_OVERLAY, "something over it: overlay");
	ready(&facts);
	facts.shot = 1U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_SHOT, "a screenshot waiting: shot");
	ready(&facts);
	facts.pointer_still_ms = ZWL_SCANOUT_POINTER_IDLE_MS - 1U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_POINTER, "the pointer moved 1.999 s ago: pointer");
	facts.pointer_still_ms = ZWL_SCANOUT_POINTER_IDLE_MS;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_DIRECT, "the pointer still for 2 s: direct");
	ready(&facts);
	facts.refused = 1U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_REFUSED, "the display refused it: refused");

	/* The order: the first reason wins. */
	ready(&facts);
	facts.content_type = 0U;
	facts.overlay = 1U;
	facts.pointer_still_ms = 0U;
	check(zwl_scanout_decide(&facts) == ZWL_SCANOUT_CONTENT, "content before overlay before pointer");

	/* The words. */
	check(strcmp(zwl_scanout_reason_name(ZWL_SCANOUT_DIRECT), "direct") == 0, "the word direct");
	check(strcmp(zwl_scanout_reason_name(ZWL_SCANOUT_OVERLAY), "overlay") == 0, "the word overlay");
	check(strcmp(zwl_scanout_reason_name(99U), "unknown") == 0, "an unknown answer");

	/* The summary. */
	printf("host-scanout-rules: %d passed, %d failed\n", test_passed, test_failed);
	if (test_failed != 0)
		return 1;
	return 0;
}

/* The facts of a fullscreen video alone with a still pointer. */
static void
ready(
	struct zwl_scanout_facts *facts)
{
	/* Every fact for the game mode. */
	memset(facts, 0, sizeof(*facts));
	facts->window = 1U;
	facts->fullscreen = 1U;
	facts->content_type = ZWL_SCANOUT_CONTENT_VIDEO;
	facts->gpu_buffer = 1U;
	facts->size_matches = 1U;
	facts->pointer_still_ms = 10000U;
}

/* Counts and prints one check. */
static void
check(
	int ok,
	const char *what)
{
	/* A check that holds. */
	if (ok) {
		test_passed++;
		printf("ok   %s\n", what);
		return;
	}

	/* One that does not. */
	test_failed++;
	printf("FAIL %s\n", what);
}

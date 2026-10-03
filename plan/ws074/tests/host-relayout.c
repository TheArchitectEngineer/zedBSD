/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p071: the host test of laying a page out again.  The style engine
 * keeps the elements' computed styles (and the atoms of their class
 * attributes) between layouts, so a second layout, as when an image or a
 * font arrives, must draw the same page as the first, and a layout at
 * another size the same as a new page laid out at that size.
 *
 *   host-relayout SANS MONO FALLBACK PAGE...
 *
 * For each page: laid out at 1280 by 900, then again after an image
 * "arrived" (the images' generation moved on), then at 800 by 600; the
 * layout and display list dumps of the second layout must equal the
 * first's, and those at 800 by 600 a new page's at that size.  Prints the
 * times of the three layouts, one line per failed check and a summary.
 */

#include "page/page.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int failures;
static int checks;

static void check(int condition, const char *what, const char *page);
static int open_page(const char *path, const struct text_font_paths *paths, struct page **page);
static int laid_out(struct page *page, int width, int height, struct wb_buffer *dump, double *seconds);
static double now(void);

int
main(
	int argc,
	char **argv)
{
	struct text_font_paths paths;
	struct wb_buffer first;
	struct wb_buffer again;
	struct wb_buffer smaller;
	struct wb_buffer fresh;
	struct page *page;
	struct page *other;
	double seconds[4];
	int argument;
	int same;
	int error;

	/* ws074-p080: localStorage goes under XDG_DATA_HOME; the test keeps it under build/, away from ~/.local/share. */
	setenv("XDG_DATA_HOME", "build/ws074-host-data", 1);

	if (argc < 5) {
		fprintf(stderr, "usage: host-relayout SANS MONO FALLBACK PAGE...\n");
		return 2;
	}

	/* The fonts of every page. */
	paths.sans = argv[1];
	paths.mono = argv[2];
	paths.fallback = argv[3];

	/* Each page. */
	for (argument = 4; argument < argc; argument++) {
		wb_buffer_init(&first);
		wb_buffer_init(&again);
		wb_buffer_init(&smaller);
		wb_buffer_init(&fresh);

		/* The first layout, the second after an image arrived, and one at another size. */
		error = open_page(argv[argument], &paths, &page);
		check(error == 0, "page opened", argv[argument]);
		if (error != 0)
			continue;
		error = laid_out(page, 1280, 900, &first, &seconds[0]);
		check(error == 0, "first layout", argv[argument]);
		page->images_generation++;
		if (error == 0)
			error = laid_out(page, 1280, 900, &again, &seconds[1]);
		check(error == 0, "second layout", argv[argument]);
		if (error == 0)
			error = laid_out(page, 800, 600, &smaller, &seconds[2]);
		check(error == 0, "layout at 800x600", argv[argument]);

		/* A new page at the smaller size. */
		if (error == 0)
			error = open_page(argv[argument], &paths, &other);
		if (error == 0) {
			error = laid_out(other, 800, 600, &fresh, &seconds[3]);
			page_destroy(other);
		}
		check(error == 0, "new page at 800x600", argv[argument]);

		/* The second layout is the first again; the smaller one is the new page's. */
		if (error == 0) {
			same = first.length == again.length && memcmp(first.data, again.data, first.length) == 0;
			check(same, "second layout equals the first", argv[argument]);
			same = smaller.length == fresh.length && memcmp(smaller.data, fresh.data, smaller.length) == 0;
			check(same, "layout at another size equals a new page's", argv[argument]);
			printf("host-relayout: %s: first %.3f s, again %.3f s, at 800x600 %.3f s (new page %.3f s)\n", argv[argument],
			    seconds[0], seconds[1], seconds[2], seconds[3]);
		}

		/* The page and the dumps. */
		page_destroy(page);
		wb_buffer_release(&first);
		wb_buffer_release(&again);
		wb_buffer_release(&smaller);
		wb_buffer_release(&fresh);
	}

	/* The summary. */
	printf("host-relayout: %d checks, %d failed\n", checks, failures);
	return failures != 0;
}

/* Counts a check and reports it when it failed. */
static void
check(
	int condition,
	const char *what,
	const char *page)
{
	checks++;
	if (condition)
		return;
	failures++;
	printf("FAIL %s: %s\n", page, what);
}

/* Makes a page from a file with the fonts open. */
static int
open_page(
	const char *path,
	const struct text_font_paths *paths,
	struct page **page)
{
	int error;

	/* The page, its document and its fonts. */
	error = page_create(page, __builtin_frame_address(0));
	if (error != 0)
		return error;
	error = page_load_file(*page, path);
	if (error == 0)
		error = page_open_fonts(*page, paths);
	if (error != 0)
		page_destroy(*page);
	return error;
}

/* Lays a page out at a size, timing it, and dumps its layout and display list. */
static int
laid_out(
	struct page *page,
	int width,
	int height,
	struct wb_buffer *dump,
	double *seconds)
{
	double start;
	int error;

	/* The layout and the display list, timed, then their dumps. */
	start = now();
	error = page_layout(page, width, height);
	if (error == 0)
		error = page_paint(page);
	*seconds = now() - start;
	if (error == 0)
		error = layout_dump(&page->layout, dump);
	if (error == 0)
		error = paint_dump(&page->paint, dump);
	return error;
}

/* The monotonic clock in seconds. */
static double
now(void)
{
	struct timespec time;

	/* The clock's seconds and nanoseconds. */
	clock_gettime(CLOCK_MONOTONIC, &time);
	return (double)time.tv_sec + (double)time.tv_nsec / 1e9;
}

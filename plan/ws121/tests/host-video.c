/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws121-p004: the host test of the browser's <video> through <browser/browser.h>
 * alone: a page with a muted autoplay video (WS122's sample.mp4, 320x240) is
 * loaded, the view's descriptors and time out are polled as a window's loop
 * does, and the view is drawn by the CPU before the picture came, at a
 * second and at two seconds.  The video's place must show a picture, the
 * picture must change, and the layout must take the video's size.
 *
 *     host-video PAGE PREFIX
 *
 * Writes PREFIX-NAME.ppm for each drawing and prints "PASS name" or "FAIL
 * name ..." for each check; exits with 1 when one failed.
 */

#include <browser/browser.h>

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The view's size. */
#define TEST_WIDTH	640U
#define TEST_HEIGHT	400U

/* Where the video is: under the body's margin of 8 pixels, 320 by 240. */
#define TEST_VIDEO_X	8U
#define TEST_VIDEO_Y	8U

/* The checks that failed. */
static int failures;

int main(int argc, char **argv);
static void run_for(struct browser_view *view, int milliseconds);
static void check(const char *name, int passed, const char *detail);
static int save(const char *prefix, const char *name, const uint32_t *pixels);
static unsigned long region_sum(const uint32_t *pixels);
static unsigned long region_changes(const uint32_t *before, const uint32_t *after);

/* Loads the page, plays, draws and checks. */
int
main(
	int argc,
	char **argv)
{
	static uint32_t first[TEST_WIDTH * TEST_HEIGHT];
	static uint32_t second[TEST_WIDTH * TEST_HEIGHT];
	static uint32_t third[TEST_WIDTH * TEST_HEIGHT];
	struct browser_view_options options;
	struct browser_fonts fonts;
	struct browser_view *view;
	char detail[128];
	double height;
	int error;

	/* The page and the prefix. */
	if (argc != 3) {
		fprintf(stderr, "usage: host-video PAGE PREFIX\n");
		return 2;
	}

	/* The view, fetching as a window does. */
	memset(&fonts, 0, sizeof(fonts));
	fonts.sans = "userland/desktop/fonts/Mahora-Regular.ttf";
	fonts.mono = "userland/desktop/fonts/JetBrainsMono-Regular.ttf";
	fonts.fallback = "userland/desktop/fonts/DroidSansFallbackFull.ttf";
	memset(&options, 0, sizeof(options));
	options.version = BROWSER_API_VERSION;
	options.fonts = &fonts;
	options.stack_base = __builtin_frame_address(0);
	options.width = TEST_WIDTH;
	options.height = TEST_HEIGHT;
	error = browser_view_create(&options, &view);
	if (error != 0)
		return 2;
	error = browser_view_load(view, argv[1]);
	check("load", error == 0, "browser_view_load");

	/* Before the video opened: its default box. */
	error = browser_view_draw_pixels(view, first, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	check("draw-before", error == 0, "browser_view_draw_pixels");
	(void)save(argv[2], "before", first);

	/* A second of the loop: the video opened, its size laid out, pictures came. */
	run_for(view, 1000);
	error = browser_view_draw_pixels(view, second, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	(void)save(argv[2], "playing-1", second);
	height = browser_view_document_height(view);
	(void)snprintf(detail, sizeof(detail), "document height %.0f, region sum %lu", height, region_sum(second));
	check("picture", error == 0 && region_sum(second) > 0UL && height >= 248.0, detail);

	/* Another second: the picture moved on. */
	run_for(view, 1000);
	error = browser_view_draw_pixels(view, third, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	(void)save(argv[2], "playing-2", third);
	(void)snprintf(detail, sizeof(detail), "%lu pixels changed", region_changes(second, third));
	check("advances", error == 0 && region_changes(second, third) > 100UL, detail);

	/* The end. */
	browser_view_destroy(view);
	if (failures != 0) {
		printf("host-video: FAIL %d\n", failures);
		return 1;
	}

	/* Every check passed. */
	printf("host-video: PASS\n");
	return 0;
}

/* Runs the view's loop for a while: its descriptors and time out polled, its work done. */
static void
run_for(
	struct browser_view *view,
	int milliseconds)
{
	struct pollfd fds[64];
	struct timespec start;
	struct timespec now;
	size_t count;
	long elapsed;
	int timeout;

	/* Until the time is up. */
	(void)clock_gettime(CLOCK_MONOTONIC, &start);
	for (;;) {
		(void)clock_gettime(CLOCK_MONOTONIC, &now);
		elapsed = (now.tv_sec - start.tv_sec) * 1000L + (now.tv_nsec - start.tv_nsec) / 1000000L;
		if (elapsed >= milliseconds)
			break;
		count = browser_view_poll_fds(view, fds, 64);
		timeout = browser_view_timeout(view);
		if (timeout < 0 || timeout > 20)
			timeout = 20;
		(void)poll(fds, (nfds_t)count, timeout);
		browser_view_process(view, fds, count);
	}
}

/* Reports one check. */
static void
check(
	const char *name,
	int passed,
	const char *detail)
{
	/* PASS or FAIL with what was seen. */
	if (passed) {
		printf("PASS %s\n", name);
		return;
	}

	/* A failure. */
	printf("FAIL %s: %s\n", name, detail);
	failures++;
}

/* Writes a drawing as PREFIX-NAME.ppm. */
static int
save(
	const char *prefix,
	const char *name,
	const uint32_t *pixels)
{
	char path[512];
	FILE *file;
	size_t index;

	/* The file. */
	(void)snprintf(path, sizeof(path), "%s-%s.ppm", prefix, name);
	file = fopen(path, "wb");
	if (file == NULL)
		return -1;

	/* Each pixel's red, green and blue. */
	fprintf(file, "P6\n%u %u\n255\n", TEST_WIDTH, TEST_HEIGHT);
	for (index = 0; index < (size_t)TEST_WIDTH * TEST_HEIGHT; index++) {
		fputc((int)((pixels[index] >> 16) & 0xffU), file);
		fputc((int)((pixels[index] >> 8) & 0xffU), file);
		fputc((int)(pixels[index] & 0xffU), file);
	}

	/* Written. */
	fclose(file);
	return 0;
}

/* Sums how far the video's place is from white (the page's ground). */
static unsigned long
region_sum(
	const uint32_t *pixels)
{
	unsigned long sum;
	unsigned x;
	unsigned y;
	uint32_t pixel;

	/* Each pixel of the place. */
	sum = 0;
	for (y = TEST_VIDEO_Y; y < TEST_VIDEO_Y + 240U; y++) {
		for (x = TEST_VIDEO_X; x < TEST_VIDEO_X + 320U; x++) {
			pixel = pixels[(size_t)y * TEST_WIDTH + x];
			sum += 0xffUL - ((pixel >> 16) & 0xffU);
		}
	}

	/* The sum. */
	return sum;
}

/* Counts the pixels of the video's place that differ between two drawings. */
static unsigned long
region_changes(
	const uint32_t *before,
	const uint32_t *after)
{
	unsigned long changed;
	unsigned x;
	unsigned y;

	/* Each pixel of the place. */
	changed = 0;
	for (y = TEST_VIDEO_Y; y < TEST_VIDEO_Y + 240U; y++) {
		for (x = TEST_VIDEO_X; x < TEST_VIDEO_X + 320U; x++) {
			if (before[(size_t)y * TEST_WIDTH + x] != after[(size_t)y * TEST_WIDTH + x])
				changed++;
		}
	}

	/* The count. */
	return changed;
}

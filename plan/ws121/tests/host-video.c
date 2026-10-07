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
 *     host-video PAGE SCRIPT-PAGE CONTROLS-PAGE PREFIX
 *
 * SCRIPT-PAGE's script (ws121-p005) asks canPlayType, waits for the
 * metadata, seeks to 16 s and plays, and logs the events up to the end;
 * its console lines are checked.  CONTROLS-PAGE's video has controls
 * (ws121-p006): a click on it plays, the bar shows, a click pauses.
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

/* The page's console lines, one after another. */
static char console_log[4096];
static size_t console_length;

int main(int argc, char **argv);
static void run_for(struct browser_view *view, int milliseconds);
static void console_line(void *context, struct browser_view *view, int level, const char *text, size_t length);
static int logged(const char *text);
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
	struct browser_callbacks callbacks;
	struct browser_fonts fonts;
	struct browser_view *view;
	char detail[128];
	double height;
	int error;

	/* The pages and the prefix. */
	if (argc != 5) {
		fprintf(stderr, "usage: host-video PAGE SCRIPT-PAGE CONTROLS-PAGE PREFIX\n");
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
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.console = console_line;
	options.callbacks = &callbacks;
	error = browser_view_create(&options, &view);
	if (error != 0)
		return 2;
	error = browser_view_load(view, argv[1]);
	check("load", error == 0, "browser_view_load");

	/* Before the video opened: its default box. */
	error = browser_view_draw_pixels(view, first, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	check("draw-before", error == 0, "browser_view_draw_pixels");
	(void)save(argv[4], "before", first);

	/* A second of the loop: the video opened, its size laid out, pictures came. */
	run_for(view, 1000);
	error = browser_view_draw_pixels(view, second, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	(void)save(argv[4], "playing-1", second);
	height = browser_view_document_height(view);
	(void)snprintf(detail, sizeof(detail), "document height %.0f, region sum %lu", height, region_sum(second));
	check("picture", error == 0 && region_sum(second) > 0UL && height >= 248.0, detail);

	/* Another second: the picture moved on. */
	run_for(view, 1000);
	error = browser_view_draw_pixels(view, third, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	(void)save(argv[4], "playing-2", third);
	(void)snprintf(detail, sizeof(detail), "%lu pixels changed", region_changes(second, third));
	check("advances", error == 0 && region_changes(second, third) > 100UL, detail);

	/* The video's engine goes with its page. */
	browser_view_destroy(view);

	/* The scripted page: its events up to the end (16 s of 20 s, then 4 s of playing). */
	error = browser_view_create(&options, &view);
	if (error != 0)
		return 2;
	error = browser_view_load(view, argv[2]);
	check("script-load", error == 0, "browser_view_load");
	error = browser_view_draw_pixels(view, first, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	run_for(view, 6500);
	printf("%s", console_log);
	check("script-before", logged("canplaytype maybe| paused true ready 0"), "canPlayType, paused, readyState");
	check("script-metadata", logged("meta 320x240 duration 20"), "loadedmetadata with the size and the length");
	check("script-play", logged("play resolved") && logged("playing paused false"), "play() resolved, playing");
	check("script-ended", logged("ended true paused true time 20"), "ended at the end, paused");
	browser_view_destroy(view);

	/* The controls: the video opens paused with its bar; a click plays, another pauses. */
	error = browser_view_create(&options, &view);
	if (error != 0)
		return 2;
	error = browser_view_load(view, argv[3]);
	check("controls-load", error == 0, "browser_view_load");
	error = browser_view_draw_pixels(view, first, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	run_for(view, 800);
	error = browser_view_draw_pixels(view, second, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	(void)save(argv[4], "controls-paused", second);
	(void)browser_view_pointer_button(view, 168.0f, 100.0f, 0, 1, 0U);
	(void)browser_view_pointer_button(view, 168.0f, 100.0f, 0, 0, 0U);
	run_for(view, 1500);
	(void)browser_view_pointer_button(view, 168.0f, 100.0f, 0, 1, 0U);
	(void)browser_view_pointer_button(view, 168.0f, 100.0f, 0, 0, 0U);
	run_for(view, 300);
	error = browser_view_draw_pixels(view, third, TEST_WIDTH, TEST_HEIGHT, TEST_WIDTH * sizeof(uint32_t));
	(void)save(argv[4], "controls-played", third);
	printf("%s", console_log);
	check("controls-click", logged("controls play") && (logged("controls pause at 1") || logged("controls pause at 2")), "a click played, another paused");
	(void)snprintf(detail, sizeof(detail), "bar %08x video %08x", (unsigned)second[(TEST_VIDEO_Y + 235U) * TEST_WIDTH + 200U],
	    (unsigned)third[(TEST_VIDEO_Y + 235U) * TEST_WIDTH + 200U]);
	check("controls-bar", (second[(TEST_VIDEO_Y + 228U) * TEST_WIDTH + 200U] & 0xffU) < 0x80U, detail);

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

/* Keeps a console line of the page. */
static void
console_line(
	void *context,
	struct browser_view *view,
	int level,
	const char *text,
	size_t length)
{
	/* After the others, while there is room. */
	(void)context;
	(void)view;
	(void)level;
	if (console_length + length + 2U > sizeof(console_log))
		return;
	memcpy(console_log + console_length, text, length);
	console_length += length;
	console_log[console_length] = '\n';
	console_length++;
	console_log[console_length] = '\0';
}

/* Tells whether the page logged a line holding a text. */
static int
logged(
	const char *text)
{
	/* Anywhere in the lines. */
	return strstr(console_log, text) != NULL;
}

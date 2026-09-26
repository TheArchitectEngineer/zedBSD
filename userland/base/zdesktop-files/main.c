/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * zdesktop-files: the file manager of the zedBSD desktop, in a Wayland
 * window drawn on the CPU and shown with Vulkan.
 *
 *   zdesktop-files [--display=NAME] [--font=PATH] [--fallback-font=PATH]
 *                  [--width=N] [--height=N] [--token=NAME] [--timeout-s=N] [FOLDER]
 *
 * It opens on the home dashboard, or on FOLDER.  Its outcome is one line
 * on standard error: ZFILES DONE with the reason, or ZFILES FAILED naming
 * what failed; ZFILES READY says the first frame is shown.
 */

#include "window.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The fonts used unless told otherwise (the fallback is optional). */
#define MAIN_FONT		"/usr/share/fonts/zdesktop.ttf"
#define MAIN_FALLBACK_FONT	"/usr/share/fonts/zdesktop-fallback.ttf"

/* How many frames in a row may find the swapchain out of date before the program gives up. */
#define MAIN_STALE_LIMIT	8U

/* The longest the loop sleeps when nothing is due, in milliseconds (folders are checked for changes). */
#define MAIN_IDLE_MS		500

/*
 * What the command line asked for.
 */
struct main_options {
	const char *display;
	const char *font;
	const char *fallback;
	const char *token;
	const char *start;
	unsigned width;
	unsigned height;
	unsigned timeout;
};

/*
 * The program's parts, for the whole run.  They are file-scope because
 * the window's input queue and the app are too large for the stack.
 */
static struct fm_window main_window;
static struct fm_present main_present;
static struct fm_app main_app;
static struct fm_text main_text;

/*
 * The frame being drawn: ordinary memory the size of the swapchain, and
 * the canvas over it.  They are remade when the window changes size.
 */
static uint32_t *main_pixels;
static struct fm_canvas main_canvas;

static int main_parse(int argc, char **argv, struct main_options *options);
static const char *main_value(const char *argument, const char *name);
static int main_number(const char *text, unsigned maximum, unsigned *value);
static int main_loop(const struct main_options *options);
static int main_frame(void);
static int main_canvas_make(void);
static int main_timeout(uint64_t now);

/*
 * Runs the file manager.
 */
int
main(
	int argc,
	char **argv)
{
	struct main_options options;
	VkResult result;
	int status;
	int error;

	/* The command line. */
	status = main_parse(argc, argv, &options);
	if (status != 0) {
		fprintf(stderr, "usage: zdesktop-files [--display=NAME] [--font=PATH] [--fallback-font=PATH] [--width=N] [--height=N] [--token=NAME] [--timeout-s=N] [FOLDER]\n");
		return 2;
	}

	/* The fonts. */
	error = fm_text_open(&main_text, options.font, options.fallback);
	if (error != 0) {
		fprintf(stderr, "ZFILES FAILED operation=font path=%s error=%d\n", options.font, error);
		return 1;
	}

	/* The window. */
	status = fm_window_open(&main_window, options.display, options.width, options.height, "Files", "zdesktop-files");
	if (status != 0) {
		fprintf(stderr, "ZFILES FAILED operation=window error=%d\n", errno);
		fm_text_close(&main_text);
		return 1;
	}

	/* The presenter. */
	result = fm_present_open(&main_present, &main_window);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "ZFILES FAILED operation=%s result=%d\n", main_present.operation, (int)result);
		fm_present_close(&main_present);
		fm_window_close(&main_window);
		fm_text_close(&main_text);
		return 1;
	}

	/* The file manager itself. */
	main_app.now = fm_clock();
	error = fm_app_init(&main_app, &main_text, options.start);
	if (error != 0) {
		fprintf(stderr, "ZFILES FAILED operation=app error=%d\n", error);
		fm_present_close(&main_present);
		fm_window_close(&main_window);
		fm_text_close(&main_text);
		return 1;
	}

	/* The loop, until the window closes. */
	status = main_loop(&options);

	/* Everything goes, the app before the window it drew into. */
	fm_app_release(&main_app);
	fm_canvas_release(&main_canvas);
	free(main_pixels);
	fm_present_close(&main_present);
	fm_window_close(&main_window);
	fm_text_close(&main_text);

	/* Reports how the run ended. */
	if (status != 0)
		return 1;

	/* Succeeded: the window was closed. */
	return 0;
}

/* Reads the command line into the options; returns nonzero for a malformed one. */
static int
main_parse(
	int argc,
	char **argv,
	struct main_options *options)
{
	const char *value;
	int status;
	int index;

	/* The defaults. */
	memset(options, 0, sizeof(*options));
	options->font = MAIN_FONT;
	options->fallback = MAIN_FALLBACK_FONT;
	options->width = FM_WIDTH;
	options->height = FM_HEIGHT;

	/* Each argument. */
	for (index = 1; index < argc; index++) {
		/* The compositor's display. */
		value = main_value(argv[index], "--display=");
		if (value != NULL) {
			options->display = value;
			continue;
		}

		/* The main font. */
		value = main_value(argv[index], "--font=");
		if (value != NULL) {
			options->font = value;
			continue;
		}

		/* The fallback font. */
		value = main_value(argv[index], "--fallback-font=");
		if (value != NULL) {
			options->fallback = value;
			continue;
		}

		/* A name the log lines carry (a test tells its windows apart by it). */
		value = main_value(argv[index], "--token=");
		if (value != NULL) {
			options->token = value;
			continue;
		}

		/* The window's width. */
		value = main_value(argv[index], "--width=");
		if (value != NULL) {
			status = main_number(value, 8192U, &options->width);
			if (status != 0)
				return status;
			continue;
		}

		/* The window's height. */
		value = main_value(argv[index], "--height=");
		if (value != NULL) {
			status = main_number(value, 8192U, &options->height);
			if (status != 0)
				return status;
			continue;
		}

		/* How long the program runs at most (0 for ever). */
		value = main_value(argv[index], "--timeout-s=");
		if (value != NULL) {
			status = main_number(value, 86400U, &options->timeout);
			if (status != 0)
				return status;
			continue;
		}

		/* An unknown option refuses the command line. */
		if (argv[index][0] == '-')
			return -1;

		/* The folder to open, once. */
		if (options->start != NULL)
			return -1;
		options->start = argv[index];
	}

	/* A window has some size. */
	if (options->width < 320U || options->height < 240U)
		return -1;

	/* Succeeded: the options are read. */
	return 0;
}

/* Returns what follows an option's name in an argument, or NULL when the argument is another option. */
static const char *
main_value(
	const char *argument,
	const char *name)
{
	size_t length;
	int match;

	/* The name must start the argument. */
	length = strlen(name);
	match = strncmp(argument, name, length);
	if (match != 0)
		return NULL;

	/* Reports the value after it. */
	return argument + length;
}

/* Reads a decimal number no larger than a maximum; nonzero for a malformed one. */
static int
main_number(
	const char *text,
	unsigned maximum,
	unsigned *value)
{
	unsigned long number;
	char *end;

	/* The digits, all of them. */
	errno = 0;
	number = strtoul(text, &end, 10);
	if (errno != 0 ||
	    end == text ||
	    *end != '\0' ||
	    number > maximum)
		return -1;

	/* Succeeded: the number. */
	*value = (unsigned)number;
	return 0;
}

/* Runs the window until it closes (or the timeout passes); returns nonzero when something failed. */
static int
main_loop(
	const struct main_options *options)
{
	struct fm_event event;
	const char *token;
	uint64_t started;
	uint64_t now;
	int taken;
	int status;
	int timeout;

	/* The first frame's canvas. */
	status = main_canvas_make();
	if (status != 0) {
		fprintf(stderr, "ZFILES FAILED operation=canvas\n");
		return -1;
	}

	/* The first frame. */
	status = main_frame();
	if (status != 0)
		return -1;

	/* The log line the tests wait for. */
	token = "-";
	if (options->token != NULL)
		token = options->token;
	fm_log("READY width=%u height=%u token=%s", main_present.extent.width, main_present.extent.height, token);

	/* Each round: input, time, and a frame when something changed. */
	started = fm_clock();
	for (;;) {
		/* Waits for the compositor, or until something is due. */
		now = fm_clock();
		timeout = main_timeout(now);
		status = fm_window_dispatch(&main_window, timeout);
		if (status != 0) {
			fm_log("DONE reason=disconnected");
			return 0;
		}

		/* The held key's repeat, and every input queued. */
		now = fm_clock();
		(void)fm_window_repeat(&main_window, now);
		for (;;) {
			taken = fm_window_take(&main_window, &event);
			if (taken == 0)
				break;
			fm_ui_event(&main_app, &event);
		}

		/* Time passes for the file manager. */
		fm_ui_tick(&main_app, now);

		/* The close button ends the run. */
		if (main_window.closed != 0) {
			fm_log("DONE reason=close");
			return 0;
		}

		/* So does the timeout, when one was given. */
		if (options->timeout != 0U && now - started >= (uint64_t)options->timeout * 1000U) {
			fm_log("DONE reason=timeout");
			return 0;
		}

		/* A new size: a new swapchain and canvas, and a frame. */
		if (main_window.resized != 0) {
			main_window.resized = 0;
			status = fm_present_resize(&main_present, main_window.width, main_window.height);
			if (status != VK_SUCCESS) {
				fprintf(stderr, "ZFILES FAILED operation=%s result=%d\n", main_present.operation, status);
				return -1;
			}

			/* The canvas to match. */
			status = main_canvas_make();
			if (status != 0)
				return -1;
			main_app.dirty = 1;
		}

		/* A frame when something changed. */
		if (main_app.dirty != 0) {
			status = main_frame();
			if (status != 0)
				return -1;
		}
	}
}

/* Draws and shows a frame, remaking the swapchain when it is out of date; nonzero when it cannot be shown. */
static int
main_frame(void)
{
	VkResult result;
	unsigned stale;
	int status;

	/* Tries until the frame is shown, remaking a stale swapchain a few times. */
	for (stale = 0; stale < MAIN_STALE_LIMIT; stale++) {
		/* The frame on the CPU. */
		fm_ui_draw(&main_app, &main_canvas);

		/* Shown in the window. */
		result = fm_present_frame(&main_present, main_pixels, (size_t)main_present.extent.width);
		if (result == VK_SUCCESS)
			return 0;

		/* Anything but a stale swapchain is a failure. */
		if (result != VK_ERROR_OUT_OF_DATE_KHR) {
			fprintf(stderr, "ZFILES FAILED operation=%s result=%d\n", main_present.operation, (int)result);
			return -1;
		}

		/* A stale swapchain is remade at the window's size, with a canvas to match. */
		result = fm_present_resize(&main_present, main_window.width, main_window.height);
		if (result != VK_SUCCESS) {
			fprintf(stderr, "ZFILES FAILED operation=%s result=%d\n", main_present.operation, (int)result);
			return -1;
		}

		/* The canvas to match. */
		status = main_canvas_make();
		if (status != 0)
			return -1;
	}

	/* The swapchain stayed out of date. */
	fprintf(stderr, "ZFILES FAILED operation=stale-swapchain\n");
	return -1;
}

/* Makes the frame's memory and canvas at the swapchain's size; nonzero when memory runs out. */
static int
main_canvas_make(void)
{
	size_t count;
	int error;

	/* The old canvas and memory go. */
	fm_canvas_release(&main_canvas);
	free(main_pixels);

	/* Memory for the swapchain's size. */
	count = (size_t)main_present.extent.width * (size_t)main_present.extent.height;
	main_pixels = calloc(count, sizeof(uint32_t));
	if (main_pixels == NULL)
		return -1;

	/* The canvas over it. */
	error = fm_canvas_init(&main_canvas, main_pixels, (size_t)main_present.extent.width, (int)main_present.extent.width, (int)main_present.extent.height);
	if (error != 0)
		return -1;

	/* Succeeded: frames can be drawn. */
	return 0;
}

/* Reports how long the loop may sleep: until a key repeats, or the idle limit. */
static int
main_timeout(
	uint64_t now)
{
	uint64_t wait;

	/* No key is held: the idle limit. */
	if (main_window.repeat_key == 0U)
		return MAIN_IDLE_MS;

	/* A repeat already due is due now. */
	if (main_window.repeat_at <= now)
		return 0;

	/* A held key repeats soon. */
	wait = main_window.repeat_at - now;
	if (wait < (uint64_t)MAIN_IDLE_MS)
		return (int)wait;

	/* Otherwise the idle limit. */
	return MAIN_IDLE_MS;
}

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
 *                  [--width=N] [--height=N] [--wallpaper=PATH] [--token=NAME] [--timeout-s=N] [FOLDER]
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

/* A frame that takes longer than this is logged, in milliseconds. */
#define MAIN_SLOW_FRAME_MS	250U

/* The program a new window runs when this one was not started by its full path. */
#define MAIN_PROGRAM		"/bin/zdesktop-files"

/* How often the menus' state is checked at most while no input arrives, in milliseconds. */
#define MAIN_MENU_CHECK_MS	250U

/* The longest the loop sleeps when nothing is due, in milliseconds (folders are checked for changes). */
#define MAIN_IDLE_MS		500

/*
 * What the command line asked for.
 */
struct main_options {
	const char *program;
	const char *display;
	const char *font;
	const char *fallback;
	const char *token;
	const char *start;
	const char *wallpaper;
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
 * The window's menus in zdesktop, opened with the window and closed before
 * it; its service is NULL when the compositor has no System Menu.
 */
static struct fm_menu main_menu;

/*
 * The window's titlebar in zdesktop (its controls), opened with the window
 * and closed before it; the file manager does not run without it.
 */
static struct fm_titlebar main_titlebar;

/*
 * The window's glass in zdesktop (its panels on the frosted glass), opened
 * with the presenter and closed before the window; without it the window
 * keeps its opaque ground.
 */
static struct fm_glass main_glass;

/* The context menu being opened (too large for the stack's taste). */
static struct fm_context main_context;

/* The titlebar's event being carried out, and its state being made (both too large for the stack's taste). */
static struct fm_titlebar_event main_titlebar_event;
static struct fm_titlebar_state main_titlebar_state;

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
static void main_request(const struct main_options *options);
static void main_new_window(const struct main_options *options);
static void main_menu_update(void);

/*
 * Runs the file manager.
 */
int
main(
	int argc,
	char **argv)
{
	struct main_options options;
	struct fm_menu_state state;
	VkResult result;
	int status;
	int error;

	/* The command line. */
	status = main_parse(argc, argv, &options);
	if (status != 0) {
		fprintf(stderr, "usage: zdesktop-files [--display=NAME] [--font=PATH] [--fallback-font=PATH] [--width=N] [--height=N] [--wallpaper=PATH] [--token=NAME] [--timeout-s=N] [FOLDER]\n");
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

	/* Glass when zdesktop can show the window see-through (the frame's ground is then left clear). */
	main_app.glass = fm_glass_open(&main_glass, &main_window, &main_present);

	/* The dashboard's picture, when another was asked for. */
	if (options.wallpaper != NULL)
		snprintf(main_app.wallpaper, sizeof(main_app.wallpaper), "%s", options.wallpaper);

	/* The menus; a window whose menus cannot be made goes on without them. */
	fm_ui_menu_state(&main_app, &state);
	error = fm_menu_open(&main_menu, &main_window, &state);
	if (error != 0) {
		fm_log("MENU failed errno=%d", error);
		fm_menu_close(&main_menu);
	}

	/* The titlebar's controls; without zdesktop's titlebar the file manager does not start. */
	fm_ui_titlebar_state(&main_app, &main_titlebar_state);
	error = fm_titlebar_open(&main_titlebar, &main_window, &main_titlebar_state);
	if (error != 0) {
		fprintf(stderr, "ZFILES FAILED operation=titlebar errno=%d\n", error);
		fm_titlebar_close(&main_titlebar);
		fm_menu_close(&main_menu);
		fm_glass_close(&main_glass);
		fm_app_release(&main_app);
		fm_present_close(&main_present);
		fm_window_close(&main_window);
		fm_text_close(&main_text);
		return 1;
	}

	/* The loop, until the window closes. */
	status = main_loop(&options);

	/* Everything goes, the titlebar, the menus, the glass and the app before the window they belong to. */
	fm_titlebar_close(&main_titlebar);
	fm_menu_close(&main_menu);
	fm_glass_close(&main_glass);
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

	/* The defaults; a new window runs this program again when it was started by its full path. */
	memset(options, 0, sizeof(*options));
	options->program = MAIN_PROGRAM;
	if (argc > 0 && argv[0] != NULL && argv[0][0] == '/')
		options->program = argv[0];
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

		/* The picture of the dashboard's hero card (the desktop's wallpaper by default). */
		value = main_value(argv[index], "--wallpaper=");
		if (value != NULL) {
			options->wallpaper = value;
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
	uint64_t menu_checked_at;
	int inputs;
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
	menu_checked_at = started;
	for (;;) {
		/* Waits for the compositor, or until something is due. */
		now = fm_clock();
		timeout = main_timeout(now);
		status = fm_window_dispatch(&main_window, timeout);
		if (status != 0) {
			fm_log("DONE reason=disconnected");
			return 0;
		}

		/* The held key's repeat, and every input queued (the menus' choices among them). */
		now = fm_clock();
		(void)fm_window_repeat(&main_window, now);
		inputs = 0;
		for (;;) {
			taken = fm_window_take(&main_window, &event);
			if (taken == 0)
				break;
			fm_ui_event(&main_app, &event);
			inputs++;
		}

		/* What was done with the titlebar, oldest first, at the time now. */
		main_app.now = now;
		for (;;) {
			taken = fm_titlebar_take(&main_titlebar, &main_titlebar_event);
			if (taken == 0)
				break;
			fm_ui_titlebar(&main_app, &main_titlebar_event);
			inputs++;
		}

		/* What the window was asked to do: a new window, minimizing, zooming, closing, a context menu. */
		main_request(options);

		/* Time passes for the file manager. */
		fm_ui_tick(&main_app, now);

		/* The menus show the state after input at once, and otherwise now and then (a task's end changes it). */
		if (inputs != 0 || now - menu_checked_at >= MAIN_MENU_CHECK_MS) {
			main_menu_update();
			menu_checked_at = now;
		}

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
	uint64_t started;
	uint64_t drawn;
	uint64_t shown;
	unsigned stale;
	int status;

	/* Tries until the frame is shown, remaking a stale swapchain a few times. */
	for (stale = 0; stale < MAIN_STALE_LIMIT; stale++) {
		/* The frame on the CPU, laid out for a docked or a floating window. */
		started = fm_clock();
		main_app.docked = main_window.maximized;
		fm_ui_draw(&main_app, &main_canvas);
		drawn = fm_clock();

		/* The frame's glass panels, sent to take effect with it. */
		fm_glass_refresh(&main_glass, &main_app);

		/* Shown in the window. */
		result = fm_present_frame(&main_present, main_pixels, (size_t)main_present.extent.width);
		shown = fm_clock();

		/* A slow frame is logged (a diagnostic: where the time of a frame goes). */
		if (shown - started > MAIN_SLOW_FRAME_MS)
			fm_log("SLOW-FRAME draw=%lu present=%lu copy=%u acquire=%u queue=%u wait=%u", (unsigned long)(drawn - started), (unsigned long)(shown - drawn), main_present.copy_ms, main_present.acquire_ms, main_present.present_ms, main_present.wait_ms);
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

/* Reports how long the loop may sleep: until a key repeats, the file manager's work moves on, or the idle limit. */
static int
main_timeout(
	uint64_t now)
{
	uint64_t wait;
	int limit;
	int busy;

	/* The idle limit, shortened while the file manager has work waiting. */
	limit = MAIN_IDLE_MS;
	busy = fm_ui_wait(&main_app);
	if (busy >= 0 && busy < limit)
		limit = busy;

	/* No key is held: the limit. */
	if (main_window.repeat_key == 0U)
		return limit;

	/* A repeat already due is due now. */
	if (main_window.repeat_at <= now)
		return 0;

	/* A held key repeats soon. */
	wait = main_window.repeat_at - now;
	if (wait < (uint64_t)limit)
		return (int)wait;

	/* Otherwise the limit. */
	return limit;
}

/* Carries out what the window was asked to do by an action, once. */
static void
main_request(
	const struct main_options *options)
{
	unsigned request;

	/* The request, taken. */
	request = main_app.request;
	main_app.request = FM_REQUEST_NONE;

	/* Each request. */
	switch (request) {
	case FM_REQUEST_NEW_WINDOW:
		main_new_window(options);
		break;
	case FM_REQUEST_MINIMIZE:
		fm_window_minimize(&main_window);
		break;
	case FM_REQUEST_ZOOM:
		fm_window_zoom(&main_window);
		break;
	case FM_REQUEST_CLOSE:
		main_window.closed = 1;
		break;
	case FM_REQUEST_CONTEXT:
		/* The context menu of the right press, at its place (ui-context.c, menu.c). */
		fm_ui_context(&main_app, &main_context);
		fm_menu_context(&main_menu, &main_context, main_app.context_x, main_app.context_y);
		break;
	default:
		break;
	}
}

/*
 * Starts another window of the file manager (a process of its own) on the
 * folder shown, with this window's display, fonts, size and picture.
 */
static void
main_new_window(
	const struct main_options *options)
{
	char *arguments[12];
	char display[FM_PATH_MAX + 16];
	char font[FM_PATH_MAX + 16];
	char fallback[FM_PATH_MAX + 32];
	char wallpaper[FM_PATH_MAX + 16];
	char width[32];
	char height[32];
	char token[96];
	char folder[FM_PATH_MAX];
	const char *shown;
	int count;
	int error;

	/* The folder shown, or the home dashboard when none is. */
	shown = fm_current_folder(&main_app);
	folder[0] = '\0';
	if (shown != NULL)
		snprintf(folder, sizeof(folder), "%s", shown);

	/* The program, its fonts and its size. */
	count = 0;
	arguments[count] = (char *)options->program;
	count++;
	snprintf(font, sizeof(font), "--font=%s", options->font);
	arguments[count] = font;
	count++;
	snprintf(fallback, sizeof(fallback), "--fallback-font=%s", options->fallback);
	arguments[count] = fallback;
	count++;
	snprintf(width, sizeof(width), "--width=%d", main_app.width);
	arguments[count] = width;
	count++;
	snprintf(height, sizeof(height), "--height=%d", main_app.height);
	arguments[count] = height;
	count++;

	/* The dashboard's picture. */
	snprintf(wallpaper, sizeof(wallpaper), "--wallpaper=%s", main_app.wallpaper);
	arguments[count] = wallpaper;
	count++;

	/* The same display, when this one was given one. */
	if (options->display != NULL) {
		snprintf(display, sizeof(display), "--display=%s", options->display);
		arguments[count] = display;
		count++;
	}

	/* Its log lines named after this window's. */
	if (options->token != NULL) {
		snprintf(token, sizeof(token), "--token=%s-new", options->token);
		arguments[count] = token;
		count++;
	}

	/* The folder, when one is shown. */
	if (folder[0] != '\0') {
		arguments[count] = folder;
		count++;
	}

	/* The end of the arguments. */
	arguments[count] = NULL;

	/* The new window's process. */
	error = fm_apps_spawn(arguments);
	if (error != 0)
		fm_ui_message(&main_app, "A new window can't be opened.");
}

/* Tells the titlebar and the menus the window's state when it changed. */
static void
main_menu_update(void)
{
	struct fm_menu_state state;

	/* The titlebar's state now, sent when it differs from what it shows. */
	fm_ui_titlebar_state(&main_app, &main_titlebar_state);
	fm_titlebar_refresh(&main_titlebar, &main_titlebar_state);

	/* Without menus there is nothing more to tell. */
	if (main_menu.menu == NULL)
		return;

	/* The menus' state now, sent when it differs from what they show. */
	fm_ui_menu_state(&main_app, &state);
	fm_menu_refresh(&main_menu, &state);
}

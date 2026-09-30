/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * PDF Viewer (ws079-p006): a PDF document in a Wayland window, drawn on
 * the CPU with libpdf and shown with Vulkan.
 *
 *   pdfviewer [--display=NAME] [--font=PATH] [--width=N] [--height=N]
 *             [--mode=scroll|page] [--timeout-s=N] [FILE]
 *
 * The file is opened from the command line (Files' Open With runs this
 * with the file), or with File > Open (Ctrl+O).  "Annotate in Notes"
 * (Ctrl+E) starts /bin/notes on the file.  The outcome is one line on
 * standard error: PDFVIEWER DONE with the reason, or PDFVIEWER FAILED
 * naming what failed; PDFVIEWER READY says the first frame is shown.
 * ws081-p012: the touch screen scrolls with inertia, zooms with two
 * fingers and swipes pages (touch.c).
 */

#include "window.h"

#include <errno.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* The font used unless told otherwise. */
#define MAIN_FONT		"/usr/share/fonts/keiland.ttf"

/* The program "Annotate in Notes" starts. */
#define MAIN_NOTES		"/bin/notes"

/* How many frames in a row may find the swapchain out of date before the program gives up. */
#define MAIN_STALE_LIMIT	8U

/* The longest the loop sleeps when nothing is due, in milliseconds. */
#define MAIN_IDLE_MS		1000

/* The application's identity in the compositor and the recent files. */
#define MAIN_APPLICATION	"pdfviewer"

/*
 * What the command line asked for.
 */
struct main_options {
	const char *display;
	const char *font;
	const char *file;
	unsigned width;
	unsigned height;
	unsigned timeout;
	int page_mode;
};

/*
 * The program's parts, for the whole run.  They are file-scope because
 * the window's input queue and the viewer are too large for the stack.
 *
 * The window: the Wayland connection and surface, and the input queue,
 * from the start of the run to its end.
 */
static struct pv_window main_window;

/* The presenter: Vulkan's swapchain over the window, made after it and closed before it. */
static struct pv_present main_present;

/* The viewer: the document and the view, made once the swapchain's size is known. */
static struct pv_app main_app;

/* The font the frame's words are drawn in, open for the whole run (without it the frame has no words). */
static struct pv_text main_text;

/*
 * The window's menus in the compositor, opened with the window and closed
 * before it; absent with a compositor without them.
 */
static struct pv_menu main_menu;

/* The window's titlebar controls in the compositor, with the same life as the menus. */
static struct pv_titlebar main_titlebar;

/* The touch screen's gestures and scroller, made with the viewer (without them fingers do nothing). */
static struct pv_touch main_touch;

/*
 * The frame being drawn: ordinary memory the size of the swapchain, remade
 * (and the canvas with it) when the window changes size.
 */
static uint32_t *main_pixels;

/* The canvas over main_pixels, which the viewer draws each frame into. */
static struct pv_canvas main_canvas;

/* The environment a started program inherits. */
extern char **environ;

static int main_parse(int argc, char **argv, struct main_options *options);
static const char *main_value(const char *argument, const char *name);
static int main_number(const char *text, unsigned maximum, unsigned *value);
static int main_loop(const struct main_options *options);
static int main_frame(void);
static void main_turn_frame(uint64_t started, uint64_t shown);
static int main_canvas_make(void);
static void main_state(struct pv_state *state);
static void main_opened(void);
static void main_annotate(void);

/*
 * Runs PDF Viewer.
 */
int
main(
	int argc,
	char **argv)
{
	struct main_options options;
	struct pv_state state;
	VkResult result;
	int status;
	int error;

	/* The command line. */
	status = main_parse(argc, argv, &options);
	if (status != 0) {
		fprintf(stderr, "usage: pdfviewer [--display=NAME] [--font=PATH] [--width=N] [--height=N] [--mode=scroll|page] [--timeout-s=N] [FILE]\n");
		return 2;
	}

	/* The font; without it the viewer shows the pages and no text of its own. */
	error = pv_text_open(&main_text, options.font);
	if (error != 0)
		pv_log("FONT missing path=%s error=%d", options.font, error);

	/* The window. */
	status = pv_window_open(&main_window, options.display, options.width, options.height, "PDF Viewer", MAIN_APPLICATION);
	if (status != 0) {
		fprintf(stderr, "PDFVIEWER FAILED operation=window error=%d\n", errno);
		pv_text_close(&main_text);
		return 1;
	}

	/* The presenter. */
	result = pv_present_open(&main_present, &main_window);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "PDFVIEWER FAILED operation=%s result=%d\n", main_present.operation, (int)result);
		pv_present_close(&main_present);
		pv_window_close(&main_window);
		pv_text_close(&main_text);
		return 1;
	}

	/* The viewer at the swapchain's size, in the mode asked for, with the file when one was given. */
	pv_app_init(&main_app, &main_text, (int)main_present.extent.width, (int)main_present.extent.height);
	if (options.page_mode)
		pv_app_action(&main_app, PV_ACTION_MODE_PAGE);
	if (options.file != NULL)
		(void)pv_app_open(&main_app, options.file);

	/* The touch screen; without memory for it the fingers do nothing. */
	error = pv_touch_open(&main_touch);
	if (error != 0)
		pv_log("TOUCH failed errno=%d", error);

	/* The menus and the titlebar; a window without them goes on with its keys. */
	main_state(&state);
	error = pv_menu_open(&main_menu, &main_window, &state);
	if (error != 0) {
		pv_log("MENU failed errno=%d", error);
		pv_menu_close(&main_menu);
	}

	/* The titlebar's controls. */
	error = pv_titlebar_open(&main_titlebar, &main_window, &state);
	if (error != 0) {
		pv_log("TITLEBAR failed errno=%d", error);
		pv_titlebar_close(&main_titlebar);
	}

	/* The loop, until the window closes. */
	status = main_loop(&options);

	/* Everything goes, the titlebar, the menus and the viewer before the window they belong to. */
	pv_titlebar_close(&main_titlebar);
	pv_menu_close(&main_menu);
	pv_touch_close(&main_touch);
	pv_app_release(&main_app);
	free(main_pixels);
	pv_present_close(&main_present);
	pv_window_close(&main_window);
	pv_text_close(&main_text);

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
	int match;

	/* The defaults. */
	memset(options, 0, sizeof(*options));
	options->font = MAIN_FONT;
	options->width = PV_WIDTH;
	options->height = PV_HEIGHT;

	/* Each argument. */
	for (index = 1; index < argc; index++) {
		/* The compositor's display. */
		value = main_value(argv[index], "--display=");
		if (value != NULL) {
			options->display = value;
			continue;
		}

		/* The font of the viewer's own text. */
		value = main_value(argv[index], "--font=");
		if (value != NULL) {
			options->font = value;
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

		/* The mode to start in. */
		value = main_value(argv[index], "--mode=");
		if (value != NULL) {
			match = strcmp(value, "page");
			if (match == 0) {
				options->page_mode = 1;
				continue;
			}

			/* The scroll mode is the default; anything else is a usage error. */
			match = strcmp(value, "scroll");
			if (match != 0)
				return -1;
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

		/* The file to open, once. */
		if (options->file != NULL)
			return -1;
		options->file = argv[index];
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
	struct pv_touch_event touch;
	struct pv_event event;
	struct pv_state state;
	uint64_t started;
	uint64_t now;
	pid_t ended;
	int prefetched;
	int taken;
	int status;
	int timeout;
	int due;

	/* The first frame's canvas and the first frame. */
	status = main_canvas_make();
	if (status != 0) {
		fprintf(stderr, "PDFVIEWER FAILED operation=canvas\n");
		return -1;
	}

	/* The document given on the command line, and the first frame. */
	main_opened();
	status = main_frame();
	if (status != 0)
		return -1;
	pv_log("READY width=%u height=%u pages=%lu", main_present.extent.width, main_present.extent.height, (unsigned long)main_app.document.count);

	/* Each round: input, time, and a frame when something changed. */
	started = pv_clock();
	for (;;) {
		/* Waits for the compositor, or until something is due. */
		now = pv_clock();
		timeout = MAIN_IDLE_MS;
		due = pv_app_tick(&main_app, now);
		if (due >= 0 && due < timeout)
			timeout = due;
		due = pv_window_repeat_wait(&main_window, now);
		if (due >= 0 && due < timeout)
			timeout = due;
		due = pv_touch_tick(&main_touch, &main_app, pv_touch_clock());
		if (due >= 0 && due < timeout)
			timeout = due;
		if (main_app.dirty)
			timeout = 0;

		/* With time to spare, the pages next to the view are drawn ahead, one a round. */
		if (timeout > 0) {
			prefetched = pv_app_prefetch(&main_app);
			if (prefetched)
				timeout = 0;
		}

		/* Waits; a lost connection ends the run. */
		status = pv_window_dispatch(&main_window, timeout);
		if (status != 0) {
			pv_log("DONE reason=disconnected");
			return 0;
		}

		/*
		 * A key held repeats once the compositor's input is in, so that its
		 * release is seen first (BUG-111: a repeat pressed before the wait
		 * made a key act twice when the loop had been busy).
		 */
		now = pv_clock();
		main_app.now = now;
		(void)pv_window_repeat(&main_window, now);

		/* Every input queued (the menus' and the titlebar's choices among them). */
		for (;;) {
			taken = pv_window_take(&main_window, &event);
			if (taken == 0)
				break;
			pv_app_event(&main_app, &event);
		}

		/* Every touch queued. */
		for (;;) {
			taken = pv_window_take_touch(&main_window, &touch);
			if (taken == 0)
				break;
			pv_touch_event(&main_touch, &main_app, &touch);
		}

		/* A document opened: its title and the recent files; an annotation asked for: Notes. */
		main_opened();
		if (main_app.want_annotate) {
			main_app.want_annotate = 0;
			main_annotate();
		}

		/* Time passes for the viewer and the fingers; the menus and the titlebar show its state. */
		(void)pv_app_tick(&main_app, now);
		(void)pv_touch_tick(&main_touch, &main_app, pv_touch_clock());
		main_state(&state);
		pv_menu_refresh(&main_menu, &state);
		pv_titlebar_refresh(&main_titlebar, &state);

		/* Started programs that ended are reaped. */
		for (;;) {
			ended = waitpid(-1, NULL, WNOHANG);
			if (ended <= 0)
				break;
		}

		/* The close button, Quit, or Close on an empty window end the run. */
		if (main_window.closed != 0 || main_app.want_close != 0) {
			pv_log("DONE reason=close");
			return 0;
		}

		/* So does the timeout, when one was given. */
		if (options->timeout != 0U && now - started >= (uint64_t)options->timeout * 1000U) {
			pv_log("DONE reason=timeout");
			return 0;
		}

		/* A new size: a new swapchain and canvas, and a frame. */
		if (main_window.resized != 0) {
			main_window.resized = 0;
			status = pv_present_resize(&main_present, main_window.width, main_window.height);
			if (status != VK_SUCCESS) {
				fprintf(stderr, "PDFVIEWER FAILED operation=%s result=%d\n", main_present.operation, status);
				return -1;
			}

			/* A canvas of the new size. */
			status = main_canvas_make();
			if (status != 0)
				return -1;
			pv_app_resize(&main_app, (int)main_present.extent.width, (int)main_present.extent.height);
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
	uint64_t shown;
	unsigned stale;
	int status;

	/* Tries until the frame is shown, remaking a stale swapchain a few times. */
	for (stale = 0; stale < MAIN_STALE_LIMIT; stale++) {
		/* The frame on the CPU, shown in the window. */
		started = pv_clock();
		pv_draw(&main_app, &main_canvas);
		result = pv_present_frame(&main_present, main_pixels, (size_t)main_present.extent.width);
		if (result == VK_SUCCESS) {
			shown = pv_clock();
			main_turn_frame(started, shown);
			return 0;
		}

		/* Anything but a stale swapchain is a failure. */
		if (result != VK_ERROR_OUT_OF_DATE_KHR) {
			fprintf(stderr, "PDFVIEWER FAILED operation=%s result=%d\n", main_present.operation, (int)result);
			return -1;
		}

		/* A stale swapchain is remade at the window's size, with a canvas to match. */
		result = pv_present_resize(&main_present, main_window.width, main_window.height);
		if (result != VK_SUCCESS) {
			fprintf(stderr, "PDFVIEWER FAILED operation=%s result=%d\n", main_present.operation, (int)result);
			return -1;
		}

		/* A canvas of the swapchain's size. */
		status = main_canvas_make();
		if (status != 0)
			return -1;
		pv_app_resize(&main_app, (int)main_present.extent.width, (int)main_present.extent.height);
	}

	/* The swapchain stayed out of date. */
	fprintf(stderr, "PDFVIEWER FAILED operation=stale-swapchain\n");
	return -1;
}

/*
 * Follows a page turn's frames (ws079-p016): the longest frame while the
 * page changes, and once it rests, the time from the action to its frame
 * shown ("TURN done", which the demo's test reads).
 */
static void
main_turn_frame(
	uint64_t started,
	uint64_t shown)
{
	static uint64_t longest;
	uint64_t took;

	/* No turn going on. */
	if (main_app.turn_at == 0U)
		return;

	/* The longest frame of the turn. */
	took = shown - started;
	if (took > longest)
		longest = took;

	/* Still sliding: more frames come. */
	if (main_app.turning)
		return;

	/* The page rests: the whole turn and its longest frame. */
	pv_log("TURN done page=%lu ms=%lu frame_ms=%lu", (unsigned long)pv_app_current_page(&main_app), (unsigned long)(shown - main_app.turn_at), (unsigned long)longest);
	main_app.turn_at = 0U;
	longest = 0U;
}

/* Makes the frame's memory and canvas at the swapchain's size; nonzero when memory runs out. */
static int
main_canvas_make(void)
{
	uint32_t *pixels;
	size_t count;

	/* The frame's memory. */
	count = (size_t)main_present.extent.width * (size_t)main_present.extent.height;
	pixels = malloc(count * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;
	free(main_pixels);
	main_pixels = pixels;

	/* The canvas over it. */
	main_canvas.pixels = main_pixels;
	main_canvas.stride = main_present.extent.width;
	main_canvas.width = (int)main_present.extent.width;
	main_canvas.height = (int)main_present.extent.height;
	main_app.dirty = 1;

	/* Succeeded: frames can be drawn. */
	return 0;
}

/* Gathers what the menus and the titlebar show. */
static void
main_state(
	struct pv_state *state)
{
	/* A clean state, so that states compare by their bytes. */
	memset(state, 0, sizeof(*state));
	state->has_document = main_app.has_document;
	state->count = main_app.document.count;
	state->page = pv_app_current_page(&main_app);
	state->mode = (int)main_app.mode;
	state->fit = (int)main_app.fit;
	state->thumbnails = main_app.thumbnails;
}

/* After a document opened: the window's title names it, and it joins the recent files. */
static void
main_opened(void)
{
	char resolved[PATH_MAX];
	char title[PV_PATH_MAX + 32];
	const char *name;
	char *absolute;
	int error;

	/* Only once for each document opened. */
	if (!main_app.opened)
		return;
	main_app.opened = 0;

	/* The title: the file's name and the application's. */
	name = strrchr(main_app.document.path, '/');
	if (name == NULL) {
		name = main_app.document.path;
	} else {
		name++;
	}

	/* The window's title. */
	snprintf(title, sizeof(title), "%s \xe2\x80\x94 PDF Viewer", name);
	pv_window_title(&main_window, title);

	/* The recent files, by the absolute path. */
	absolute = realpath(main_app.document.path, resolved);
	if (absolute == NULL)
		return;
	error = keiland_recent_add(resolved, MAIN_APPLICATION);
	if (error != 0)
		pv_log("RECENT failed errno=%d", error);
}

/* Starts Notes on the open document ("Annotate in Notes"). */
static void
main_annotate(void)
{
	char resolved[PATH_MAX];
	char *arguments[3];
	char *absolute;
	pid_t child;
	int usable;
	int error;

	/* Notes must be on the system. */
	usable = access(MAIN_NOTES, X_OK);
	if (usable != 0) {
		pv_app_message(&main_app, "Notes is not installed on this system.", 4000U);
		pv_log("ANNOTATE missing program=%s", MAIN_NOTES);
		return;
	}

	/* The document's absolute path, so that Notes finds it wherever it starts. */
	absolute = realpath(main_app.document.path, resolved);
	if (absolute == NULL)
		snprintf(resolved, sizeof(resolved), "%s", main_app.document.path);

	/* Starts Notes with the path. */
	arguments[0] = "notes";
	arguments[1] = resolved;
	arguments[2] = NULL;
	error = posix_spawn(&child, MAIN_NOTES, NULL, NULL, arguments, environ);
	if (error != 0) {
		pv_app_message(&main_app, "Notes could not be started.", 4000U);
		pv_log("ANNOTATE failed errno=%d", error);
		return;
	}

	/* Succeeded: Notes opens the document. */
	pv_log("ANNOTATE program=%s path=%s pid=%ld", MAIN_NOTES, resolved, (long)child);
}

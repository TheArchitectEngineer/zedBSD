/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Text Editor (WS092): plain UTF-8 text in a Wayland window, drawn on the
 * CPU and shown with Vulkan (plan/ws092/design.md).
 *
 *   textedit [--display=NAME] [--font=PATH] [--fallback-font=PATH]
 *            [--ui-font=PATH] [--width=N] [--height=N] [--timeout-s=N]
 *            [FILE]
 *
 * The file is opened from the command line (a path that does not exist is
 * made by the first save), or with File > Open.  The outcome is one line
 * on standard error: TEXTEDIT DONE with the reason, or TEXTEDIT FAILED
 * naming what failed; TEXTEDIT READY says the first frame is shown, and
 * TEXTEDIT OPEN and TEXTEDIT SAVE name the files read and written.
 */

#include "window.h"

#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The fonts used unless told otherwise: the text's (monospaced), the characters it lacks, and the interface's. */
#define MAIN_FONT		"/usr/share/fonts/keiland-mono.ttf"
#define MAIN_FALLBACK_FONT	"/usr/share/fonts/keiland-fallback.ttf"
#define MAIN_UI_FONT		"/usr/share/fonts/keiland.ttf"

/* How many frames in a row may find the swapchain out of date before the program gives up. */
#define MAIN_STALE_LIMIT	8U

/* The longest the loop sleeps when nothing is due, in milliseconds. */
#define MAIN_IDLE_MS		1000

/* The application's identity in the compositor and the recent files. */
#define MAIN_APPLICATION	"textedit"

/* The longest window title. */
#define MAIN_TITLE_MAX		(TE_PATH_MAX + 64)

/*
 * What the command line asked for.
 */
struct main_options {
	const char *display;
	const char *font;
	const char *fallback;
	const char *ui_font;
	const char *file;
	unsigned width;
	unsigned height;
	unsigned timeout;
};

/*
 * The program's parts, for the whole run.  They are file-scope because
 * the window's input queue and the editor are too large for the stack.
 *
 * The window: the Wayland connection and surface, and the input queue,
 * from the start of the run to its end.
 */
static struct te_window main_window;

/* The presenter: Vulkan's swapchain over the window, made after it and closed before it. */
static struct te_present main_present;

/* The editor: the document and the view, made once the swapchain's size is known. */
static struct te_app main_app;

/* The text's font (monospaced) and its fallback, open for the whole run (without them the text has no words). */
static struct te_text main_body;

/* The interface's font and its fallback, open for the whole run (the chips, dialogs and messages). */
static struct te_text main_ui;

/*
 * The window's menus in the compositor, opened with the window and closed
 * before it; absent with a compositor without them.
 */
static struct te_menu main_menu;

/* The window's titlebar controls in the compositor, with the same life as the menus. */
static struct te_titlebar main_titlebar;

/* The window's glass, when zdesktop has glass and the swapchain is see-through. */
static struct te_glass main_glass;

/* The touch screen's gestures and scroller, made with the editor (without them fingers do nothing). */
static struct te_touch main_touch;

/*
 * The frame being drawn: ordinary memory the size of the swapchain, remade
 * (and the canvas with it) when the window changes size.
 */
static uint32_t *main_pixels;

/* The canvas over main_pixels, which the editor draws each frame into. */
static struct te_canvas main_canvas;

/* The title the window shows now, to set it again only when it changes. */
static char main_title[MAIN_TITLE_MAX];

/*
 * The file chooser open for Open or Save As (libkeiland's), or NULL.  It
 * is destroyed when it answers, and by the main loop when the editor stops
 * waiting for it (Quit while it is open).
 */
static struct keiland_file_chooser *main_chooser;

/* The interface's font, which the chooser draws its words with too. */
static const char *main_ui_font;

/*
 * The filters the chooser offers: the kinds of files that are plain text,
 * and every file.
 */
static const struct keiland_file_filter main_filters[] = {
	{ "Text Files", "txt text md markdown rst c h cc cpp hpp py sh mk conf cfg ini json xml html css js log csv tsv yaml yml toml" },
	{ "All Files", NULL }
};

static int main_parse(int argc, char **argv, struct main_options *options);
static const char *main_value(const char *argument, const char *name);
static int main_number(const char *text, unsigned maximum, unsigned *value);
static int main_loop(const struct main_options *options);
static int main_frame(void);
static int main_canvas_make(void);
static void main_state(struct te_state *state);
static void main_title_refresh(void);
static void main_opened(void);
static void main_host(struct te_app *app);
static void main_copy(void *data, const char *text, size_t length);
static size_t main_paste(void *data, char *text, size_t size);
static void main_select(void *data, const char *text, size_t length);
static size_t main_paste_primary(void *data, char *text, size_t size);
static void main_context_menu(void *data, int x, int y);
static void main_find_focus(void *data);
static int main_choose(void *data, int saving, const char *folder, const char *name);
static void main_chosen(void *data, struct keiland_file_chooser *chooser, unsigned result, const char *path, size_t filter);

/*
 * Runs Text Editor.
 */
int
main(
	int argc,
	char **argv)
{
	struct main_options options;
	struct te_state state;
	VkResult result;
	int status;
	int error;

	/* The command line. */
	status = main_parse(argc, argv, &options);
	if (status != 0) {
		fprintf(stderr, "usage: textedit [--display=NAME] [--font=PATH] [--fallback-font=PATH] [--ui-font=PATH] [--width=N] [--height=N] [--timeout-s=N] [FILE]\n");
		return 2;
	}

	/* The fonts; without them the editor shows no words. */
	error = te_text_open(&main_body, options.font, options.fallback);
	if (error != 0)
		te_log("FONT missing path=%s error=%d", options.font, error);
	error = te_text_open(&main_ui, options.ui_font, options.fallback);
	if (error != 0)
		te_log("FONT missing path=%s error=%d", options.ui_font, error);

	/* The chooser draws with the interface's font. */
	main_ui_font = options.ui_font;

	/* The window. */
	status = te_window_open(&main_window, options.display, options.width, options.height, "Text Editor", MAIN_APPLICATION);
	if (status != 0) {
		fprintf(stderr, "TEXTEDIT FAILED operation=window error=%d\n", errno);
		te_text_close(&main_ui);
		te_text_close(&main_body);
		return 1;
	}

	/* The presenter. */
	result = te_present_open(&main_present, &main_window);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "TEXTEDIT FAILED operation=%s result=%d\n", main_present.operation, (int)result);
		te_present_close(&main_present);
		te_window_close(&main_window);
		te_text_close(&main_ui);
		te_text_close(&main_body);
		return 1;
	}

	/* The editor at the swapchain's size, with the file when one was given, and the window's services. */
	te_app_init(&main_app, &main_body, &main_ui, (int)main_present.extent.width, (int)main_present.extent.height);
	main_host(&main_app);
	main_app.glass = te_glass_open(&main_glass, &main_window, &main_present);
	if (options.file != NULL)
		(void)te_app_open(&main_app, options.file);

	/* The touch screen; without memory for it the fingers do nothing. */
	error = te_touch_open(&main_touch);
	if (error != 0)
		te_log("TOUCH failed errno=%d", error);

	/* The menus and the titlebar; a window without them goes on with its keys. */
	main_state(&state);
	error = te_menu_open(&main_menu, &main_window, &state);
	if (error != 0) {
		te_log("MENU failed errno=%d", error);
		te_menu_close(&main_menu);
	}

	/* The titlebar's controls. */
	error = te_titlebar_open(&main_titlebar, &main_window, &state);
	if (error != 0) {
		te_log("TITLEBAR failed errno=%d", error);
		te_titlebar_close(&main_titlebar);
	}

	/* The loop, until the window closes. */
	status = main_loop(&options);

	/* Everything goes, the chooser, the titlebar, the menus and the editor before the window they belong to. */
	keiland_file_chooser_destroy(main_chooser);
	main_chooser = NULL;
	te_titlebar_close(&main_titlebar);
	te_menu_close(&main_menu);
	te_touch_close(&main_touch);
	te_glass_close(&main_glass);
	te_app_release(&main_app);
	free(main_pixels);
	te_present_close(&main_present);
	te_window_close(&main_window);
	te_text_close(&main_ui);
	te_text_close(&main_body);

	/* Reports how the run ended. */
	if (status != 0)
		return 1;

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Writes one line of the log: "TEXTEDIT " and the words, on standard error.
 */
void
te_log(
	const char *format,
	...)
{
	va_list arguments;

	/* The line. */
	va_start(arguments, format);
	fputs("TEXTEDIT ", stderr);
	vfprintf(stderr, format, arguments);
	fputc('\n', stderr);
	va_end(arguments);
}

/*
 * Reports the monotonic clock in milliseconds.
 */
uint64_t
te_clock(void)
{
	struct timespec now;

	/* The monotonic clock. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);

	/* Reports it in milliseconds. */
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
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
	options->ui_font = MAIN_UI_FONT;
	options->width = TE_WIDTH;
	options->height = TE_HEIGHT;

	/* Each argument. */
	for (index = 1; index < argc; index++) {
		/* The compositor's display. */
		value = main_value(argv[index], "--display=");
		if (value != NULL) {
			options->display = value;
			continue;
		}

		/* The text's font. */
		value = main_value(argv[index], "--font=");
		if (value != NULL) {
			options->font = value;
			continue;
		}

		/* The font of the characters the others lack. */
		value = main_value(argv[index], "--fallback-font=");
		if (value != NULL) {
			options->fallback = value;
			continue;
		}

		/* The interface's font. */
		value = main_value(argv[index], "--ui-font=");
		if (value != NULL) {
			options->ui_font = value;
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
	struct te_touch_event touch;
	struct te_event event;
	struct te_state state;
	uint64_t started;
	uint64_t now;
	int taken;
	int status;
	int timeout;
	int due;

	/* The first frame's canvas and the first frame. */
	status = main_canvas_make();
	if (status != 0) {
		fprintf(stderr, "TEXTEDIT FAILED operation=canvas\n");
		return -1;
	}

	/* The file given on the command line, the title, and the first frame. */
	main_app.now = te_clock();
	main_opened();
	main_title_refresh();
	status = main_frame();
	if (status != 0)
		return -1;
	te_log("READY width=%u height=%u lines=%lu", main_present.extent.width, main_present.extent.height, (unsigned long)main_app.buffer.line_count);

	/* Each round: input, time, and a frame when something changed. */
	started = te_clock();
	for (;;) {
		/* Waits for the compositor, or until something is due. */
		now = te_clock();
		timeout = MAIN_IDLE_MS;
		due = te_app_tick(&main_app, now);
		if (due >= 0 && due < timeout)
			timeout = due;
		due = te_window_repeat_wait(&main_window, now);
		if (due >= 0 && due < timeout)
			timeout = due;
		due = te_touch_tick(&main_touch, &main_app, te_touch_clock());
		if (due >= 0 && due < timeout)
			timeout = due;
		if (main_app.dirty)
			timeout = 0;

		/* Waits; a lost connection ends the run. */
		status = te_window_dispatch(&main_window, timeout);
		if (status != 0) {
			te_log("DONE reason=disconnected");
			return 0;
		}

		/* A key held repeats once the compositor's input is in, so that its release is seen first (BUG-111). */
		now = te_clock();
		main_app.now = now;
		(void)te_window_repeat(&main_window, now);

		/* Every input queued (the menus' and the titlebar's among them). */
		for (;;) {
			taken = te_window_take(&main_window, &event);
			if (taken == 0)
				break;
			te_app_event(&main_app, &event);
		}

		/* Every touch queued. */
		for (;;) {
			taken = te_window_take_touch(&main_window, &touch);
			if (taken == 0)
				break;
			te_touch_event(&main_touch, &main_app, &touch);
		}

		/* A chooser the editor no longer waits for (Quit came meanwhile) closes. */
		if (main_chooser != NULL && !main_app.choosing) {
			keiland_file_chooser_destroy(main_chooser);
			main_chooser = NULL;
		}

		/* The close button asks like File > Close (unsaved changes are asked about). */
		if (main_window.closed != 0) {
			main_window.closed = 0;
			te_app_action(&main_app, TE_ACTION_CLOSE);
		}

		/* A selection made becomes the primary one; a file opened joins the recent files; the title follows. */
		te_app_publish_primary(&main_app);
		main_opened();
		main_title_refresh();

		/* Time passes for the editor and the fingers; the menus and the titlebar show its state. */
		(void)te_app_tick(&main_app, now);
		(void)te_touch_tick(&main_touch, &main_app, te_touch_clock());
		main_state(&state);
		te_menu_refresh(&main_menu, &state);
		te_titlebar_refresh(&main_titlebar, &state);

		/* Close (with nothing unsaved, or dropped) ends the run. */
		if (main_app.want_close != 0) {
			te_log("DONE reason=close");
			return 0;
		}

		/* So does the timeout, when one was given. */
		if (options->timeout != 0U && now - started >= (uint64_t)options->timeout * 1000U) {
			te_log("DONE reason=timeout");
			return 0;
		}

		/* A new size: a new swapchain and canvas, and a frame. */
		if (main_window.resized != 0) {
			main_window.resized = 0;
			status = te_present_resize(&main_present, main_window.width, main_window.height);
			if (status != VK_SUCCESS) {
				fprintf(stderr, "TEXTEDIT FAILED operation=%s result=%d\n", main_present.operation, status);
				return -1;
			}

			/* A canvas of the new size. */
			status = main_canvas_make();
			if (status != 0)
				return -1;
			te_app_resize(&main_app, (int)main_present.extent.width, (int)main_present.extent.height);
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
		/* The frame on the CPU, its glass card, and the frame shown in the window. */
		te_draw(&main_app, &main_canvas);
		te_glass_refresh(&main_glass, &main_app);
		result = te_present_frame(&main_present, main_pixels, (size_t)main_present.extent.width);
		if (result == VK_SUCCESS)
			return 0;

		/* Anything but a stale swapchain is a failure. */
		if (result != VK_ERROR_OUT_OF_DATE_KHR) {
			fprintf(stderr, "TEXTEDIT FAILED operation=%s result=%d\n", main_present.operation, (int)result);
			return -1;
		}

		/* A stale swapchain is remade at the window's size, with a canvas to match. */
		result = te_present_resize(&main_present, main_window.width, main_window.height);
		if (result != VK_SUCCESS) {
			fprintf(stderr, "TEXTEDIT FAILED operation=%s result=%d\n", main_present.operation, (int)result);
			return -1;
		}

		/* A canvas of the swapchain's size. */
		status = main_canvas_make();
		if (status != 0)
			return -1;
		te_app_resize(&main_app, (int)main_present.extent.width, (int)main_present.extent.height);
	}

	/* The swapchain stayed out of date. */
	fprintf(stderr, "TEXTEDIT FAILED operation=stale-swapchain\n");
	return -1;
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
	te_canvas_unclip(&main_canvas);
	main_app.dirty = 1;

	/* Succeeded: frames can be drawn. */
	return 0;
}

/* Gathers what the menus and the titlebar show. */
static void
main_state(
	struct te_state *state)
{
	size_t start;
	size_t end;

	/* A clean state, so that states compare by their bytes. */
	memset(state, 0, sizeof(*state));
	state->can_undo = te_undo_can_undo(&main_app.undo);
	state->can_redo = te_undo_can_redo(&main_app.undo);
	te_edit_selection(&main_app, &start, &end);
	state->selected = 0;
	if (end > start)
		state->selected = 1;
	state->modified = te_app_modified(&main_app);
	state->line_numbers = main_app.line_numbers;
	state->wrap = main_app.wrap;
}

/* Sets the window's title when it changed: "• " for unsaved changes, the document's name and the application's. */
static void
main_title_refresh(void)
{
	char title[MAIN_TITLE_MAX];
	const char *mark;
	int modified;
	int same;

	/* The title as it should be. */
	modified = te_app_modified(&main_app);
	mark = "";
	if (modified)
		mark = "\xe2\x80\xa2 ";
	snprintf(title, sizeof(title), "%s%s \xe2\x80\x94 Text Editor", mark, te_app_name(&main_app));

	/* Sent only when it differs. */
	same = strcmp(title, main_title);
	if (same == 0)
		return;
	snprintf(main_title, sizeof(main_title), "%s", title);
	te_window_title(&main_window, main_title);
	te_log("TITLE %s", main_title);
}

/* After a file was opened or saved: it joins the recent files. */
static void
main_opened(void)
{
	char resolved[PATH_MAX];
	char *absolute;
	int error;

	/* Only once for each file. */
	if (!main_app.opened)
		return;
	main_app.opened = 0;

	/* The recent files, by the absolute path. */
	absolute = realpath(main_app.path, resolved);
	if (absolute == NULL)
		return;
	error = keiland_recent_add(resolved, MAIN_APPLICATION);
	if (error != 0)
		te_log("RECENT failed errno=%d", error);
}

/* Gives the editor the window's services. */
static void
main_host(
	struct te_app *app)
{
	/* The clipboard, the primary selection, the context menu and the find field. */
	memset(&app->host, 0, sizeof(app->host));
	app->host.data = &main_window;
	app->host.copy = main_copy;
	app->host.paste = main_paste;
	app->host.select = main_select;
	app->host.paste_primary = main_paste_primary;
	app->host.context_menu = main_context_menu;
	app->host.find_focus = main_find_focus;
	app->host.choose = main_choose;
}

/* Copies text to the clipboard. */
static void
main_copy(
	void *data,
	const char *text,
	size_t length)
{
	/* The window's clipboard. */
	te_clipboard_set(data, text, length);
}

/* Pastes the clipboard's text into a buffer; reports its length. */
static size_t
main_paste(
	void *data,
	char *text,
	size_t size)
{
	size_t length;

	/* The window's clipboard. */
	length = te_clipboard_receive(data, text, size);

	/* Succeeded: the length received. */
	return length;
}

/* Makes text the primary selection. */
static void
main_select(
	void *data,
	const char *text,
	size_t length)
{
	/* The window's primary selection. */
	te_primary_set(data, text, length);
}

/* Pastes the primary selection's text into a buffer; reports its length. */
static size_t
main_paste_primary(
	void *data,
	char *text,
	size_t size)
{
	size_t length;

	/* The window's primary selection. */
	length = te_primary_receive(data, text, size);

	/* Succeeded: the length received. */
	return length;
}

/* Opens the context menu at a place. */
static void
main_context_menu(
	void *data,
	int x,
	int y)
{
	/* The menus' context menu (the window is the only one). */
	(void)data;
	te_menu_popup(&main_menu, x, y);
}

/* Gives the titlebar's find field the keyboard (with the next refresh). */
static void
main_find_focus(
	void *data)
{
	/* Asked of the titlebar. */
	(void)data;
	main_titlebar.want_focus = 1;
}

/*
 * Opens the file chooser to open a file or to save as a name in a folder;
 * its answer comes back as a TE_EVENT_CHOSEN.  Returns 0 or an errno value.
 */
static int
main_choose(
	void *data,
	int saving,
	const char *folder,
	const char *name)
{
	struct keiland_file_chooser_options options;
	static const struct keiland_file_chooser_listener listener = {
		main_chosen
	};
	struct te_window *window;

	/* A chooser left open goes first (one at a time). */
	window = data;
	keiland_file_chooser_destroy(main_chooser);
	main_chooser = NULL;

	/* Open or Save As, at the document's folder, with the text files shown first. */
	memset(&options, 0, sizeof(options));
	options.mode = KEILAND_FILE_CHOOSER_OPEN;
	if (saving) {
		options.mode = KEILAND_FILE_CHOOSER_SAVE;
		options.name = name;
	}

	/* The editor's mark on the chooser, its folder, its filters and the interface's font. */
	options.application = MAIN_APPLICATION;
	options.folder = folder;
	options.filters = main_filters;
	options.filter_count = sizeof(main_filters) / sizeof(main_filters[0]);
	options.filter = 0;
	options.font = main_ui_font;

	/* The chooser's window over the editor's. */
	main_chooser = keiland_file_chooser_open(window->display, window->toplevel, &options, &listener, window);
	if (main_chooser == NULL)
		return errno;

	/* Succeeded: the answer comes while the loop dispatches. */
	return 0;
}

/* The chooser answered: the path (empty when cancelled) goes to the editor, and the chooser goes. */
static void
main_chosen(
	void *data,
	struct keiland_file_chooser *chooser,
	unsigned result,
	const char *path,
	size_t filter)
{
	struct te_event *event;

	/* The answer as an input of the editor. */
	(void)filter;
	event = te_window_push(data, TE_EVENT_CHOSEN);
	if (event != NULL) {
		event->text[0] = '\0';
		if (result == KEILAND_FILE_CHOOSER_CHOSEN)
			snprintf(event->text, sizeof(event->text), "%s", path);
	}

	/* The log names the answer. */
	te_log("CHOSEN result=%u path=%s", result, path);

	/* The chooser is spent. */
	keiland_file_chooser_destroy(chooser);
	if (chooser == main_chooser)
		main_chooser = NULL;
}

/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window mode of browser: a page loaded from a file, laid out
 * at the window's width, and drawn by the GPU renderer into the window's
 * swapchain.  The wheel and the keys scroll it, a click goes to the page's
 * scripts as a click event and then, unless they cancel it, opens the link
 * under it, and zdesktop's titlebar holds back, forward, reload and the
 * location, whose URL can be edited.  The page's timers run on the real
 * clock between the compositor's events, and a page its scripts changed
 * is laid out and drawn again.  An http or https page (a link, the
 * location, the history) is fetched without blocking: the page shown stays
 * until the new one has arrived, and Esc stops the load; the pages' images
 * come the same way and are laid out as they arrive.
 *
 * The program writes lines to standard output that the guest tests read
 * (ZBROWSER READY, FRAME, LINK, NAVIGATE, TITLEBAR, CONSOLE, LOADING,
 * STOPPED, ERROR); they
 * are its diagnostic interface.
 */

#include "shell/internal.h"
#include "page/page.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* How far a press of an arrow key scrolls, in pixels. */
#define SHELL_LINE_SCROLL	40

/* How much of the window a page scroll keeps in view, in pixels. */
#define SHELL_PAGE_OVERLAP	40

/* The most pages the history remembers (the oldest is forgotten first). */
#define SHELL_HISTORY_MAX	64U

/* How far the pointer may move between a press and its release for a click, in pixels. */
#define SHELL_CLICK_SLOP	4

/* The evdev codes of the keys and the button the shell reads. */
#define SHELL_KEY_ESC		1U
#define SHELL_KEY_Q		16U
#define SHELL_KEY_W		17U
#define SHELL_KEY_L		38U
#define SHELL_KEY_SPACE		57U
#define SHELL_KEY_F5		63U
#define SHELL_KEY_HOME		102U
#define SHELL_KEY_UP		103U
#define SHELL_KEY_PAGEUP	104U
#define SHELL_KEY_LEFT		105U
#define SHELL_KEY_RIGHT		106U
#define SHELL_KEY_END		107U
#define SHELL_KEY_DOWN		108U
#define SHELL_KEY_PAGEDOWN	109U
#define SHELL_BUTTON_LEFT	0x110U

/* How a page is reached: a new step of the history, or a step already in it. */
enum shell_step {
	SHELL_STEP_NEW,
	SHELL_STEP_KEEP
};

/*
 * What the window mode holds while it runs: the page and its file's
 * absolute path, the history of paths (index is the one shown), the
 * window with its titlebar and presenter, how far the page is scrolled (in
 * layout units), where the left button went down, the frame of the run
 * whose stack the pages' heaps scan, the size a page is loaded at, and the
 * clock's time when the page shown (page_epoch) and the page being opened
 * (open_epoch) began, which their timers count from.  loader fetches the
 * http and https pages and images; pending is the page being fetched (its
 * location and how it joins the history), NULL when none is.
 */
struct shell_state {
	struct page *page;
	char *path;
	char *history[SHELL_HISTORY_MAX];
	size_t history_count;
	size_t history_index;
	struct shell_window window;
	struct shell_titlebar titlebar;
	struct shell_present present;
	layout_unit scroll_y;
	int dirty;
	int press_x;
	int press_y;
	int pressed;
	const struct text_font_paths *fonts;
	const void *stack_base;
	unsigned width;
	unsigned height;
	uint64_t page_epoch;
	uint64_t open_epoch;
	struct net_loader *loader;
	struct net_request *pending;
	char *pending_path;
	int pending_step;
};

static int shell_absolute(const char *start, struct wb_buffer *path);
static int shell_open_page(struct shell_state *state, const char *path, struct page **page);
static int shell_navigate(struct shell_state *state, const char *path, int step);
static int shell_make_page(struct shell_state *state, const char *path, struct page **page);
static int shell_show_page(struct shell_state *state, struct page *page, const char *path, int step);
static int shell_start_load(struct shell_state *state, const char *path, int step);
static void shell_document_arrived(void *context, struct net_request *request);
static void shell_stop_load(struct shell_state *state, int report);
static void shell_run_network(struct shell_state *state, struct pollfd *fds, size_t count);
static int shell_lay_out(struct shell_state *state);
static void shell_show_state(struct shell_state *state);
static void shell_input(struct shell_state *state, const struct shell_event *event);
static void shell_click(struct shell_state *state, const struct shell_event *event);
static void shell_titlebar_input(struct shell_state *state, const struct shell_titlebar_event *event);
static void shell_history_go(struct shell_state *state, int direction);
static void shell_follow(struct shell_state *state, const char *target);
static void shell_scroll_by(struct shell_state *state, layout_unit distance);
static int shell_frame(struct shell_state *state);
static void shell_release(struct shell_state *state);
static int shell_wait(struct shell_state *state, uint64_t now);
static void shell_run_page(struct shell_state *state);
static void shell_console(void *context, int level, const char *text, size_t length);

/*
 * Runs the browser in a zdesktop window until it is closed.
 *
 * Returns the program's exit status.
 */
int
shell_run(
	const struct shell_options *options)
{
	struct shell_state state;
	struct pollfd net_fds[SHELL_NET_FDS];
	struct shell_event event;
	struct shell_titlebar_event titlebar_event;
	struct wb_buffer path;
	struct wb_buffer title;
	const char *shown;
	uint64_t now;
	size_t net_count;
	int timeout;
	int status;
	int taken;
	int error;
	VkResult result;

	/* A window needs a page to show. */
	memset(&state, 0, sizeof(state));
	state.fonts = options->fonts;
	state.stack_base = __builtin_frame_address(0);
	state.width = options->width;
	state.height = options->height;
	if (options->start == NULL) {
		fprintf(stderr, "browser: a page to open is needed (a file or a file: URL)\n");
		return 2;
	}

	/* The loader of the http and https pages and images. */
	error = page_net_create(&state.loader);
	if (error != 0) {
		fprintf(stderr, "browser: cannot start the network: %s\n", strerror(error));
		return 1;
	}

	/* The start page's absolute path. */
	wb_buffer_init(&path);
	error = shell_absolute(options->start, &path);
	if (error != 0) {
		fprintf(stderr, "browser: cannot open %s: %s\n", options->start, strerror(error));
		wb_buffer_release(&path);
		return 1;
	}

	/* Loads it as the history's first step. */
	status = shell_open_page(&state, wb_buffer_string(&path), &state.page);
	if (status == 0)
		status = shell_navigate(&state, wb_buffer_string(&path), SHELL_STEP_NEW);
	wb_buffer_release(&path);
	if (status != 0) {
		shell_release(&state);
		return 1;
	}

	/* The window's first title: the page's, or its file's path. */
	wb_buffer_init(&title);
	error = page_title(state.page, &title);
	shown = wb_buffer_string(&title);
	if (error != 0 || title.length == 0)
		shown = state.path;

	/* The window. */
	status = shell_window_open(&state.window, options->display, options->width, options->height, shown);
	wb_buffer_release(&title);
	if (status != 0) {
		fprintf(stderr, "browser: cannot open a window: %s\n", strerror(errno));
		shell_release(&state);
		return 1;
	}

	/* zdesktop's titlebar with the browser's controls (a compositor without it leaves the plain titlebar). */
	error = shell_titlebar_open(&state.titlebar, &state.window);
	if (error != 0) {
		printf("ZBROWSER ERROR titlebar error=%d\n", error);
		shell_titlebar_close(&state.titlebar);
	}

	/* The Vulkan presenter in the window. */
	result = shell_present_open(&state.present, &state.window);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "browser: cannot draw in the window: %s failed (%d)\n", state.present.operation, (int)result);
		shell_release(&state);
		return 1;
	}

	/* The page at the window's size. */
	status = shell_lay_out(&state);
	if (status != 0) {
		shell_release(&state);
		return 1;
	}

	/* The line the tests wait for, and the titlebar's state. */
	printf("ZBROWSER READY width=%u height=%u document=%.0f\n", (unsigned)state.present.extent.width,
	    (unsigned)state.present.extent.height, (double)layout_to_px(state.page->layout.document_height));
	fflush(stdout);
	shell_show_state(&state);

	/* Draws, then waits for the compositor, until the window closes. */
	state.dirty = 1;
	while (!state.window.closed) {
		/* A new size lays the page out again and replaces the swapchain. */
		if (state.window.resized) {
			state.window.resized = 0;
			result = shell_present_resize(&state.present, state.window.width, state.window.height);
			if (result != VK_SUCCESS) {
				fprintf(stderr, "browser: cannot resize: %s failed (%d)\n", state.present.operation, (int)result);
				break;
			}

			/* The page at the new width. */
			status = shell_lay_out(&state);
			if (status != 0)
				break;
			state.dirty = 1;
		}

		/* Draws the page when it changed. */
		if (state.dirty) {
			status = shell_frame(&state);
			if (status != 0)
				break;
			state.dirty = 0;
		}

		/* Waits for the compositor, the network, a held key's next repeat, or the page's next timer. */
		now = shell_clock();
		timeout = shell_wait(&state, now);
		net_count = page_net_poll_fds(state.loader, net_fds, SHELL_NET_FDS);
		status = shell_window_dispatch(&state.window, timeout, net_fds, net_count);
		if (status != 0) {
			fprintf(stderr, "browser: the connection to the compositor was lost\n");
			break;
		}

		/* The network's work: requests that moved on, pages and images that arrived. */
		shell_run_network(&state, net_fds, net_count);

		/* Carries out the inputs that arrived. */
		for (;;) {
			taken = shell_window_take(&state.window, &event);
			if (!taken)
				break;
			shell_input(&state, &event);
		}

		/* And what was done with the titlebar. */
		for (;;) {
			taken = shell_titlebar_take(&state.titlebar, &titlebar_event);
			if (!taken)
				break;
			shell_titlebar_input(&state, &titlebar_event);
		}

		/* The page's timers, and the layout its scripts changed. */
		shell_run_page(&state);
	}

	/* Closes everything. */
	shell_release(&state);

	/* Succeeded: the window was closed. */
	return 0;
}

/*
 * Makes an empty page for a location: its heap scanning the stack up to
 * the run's frame, the window's size for its scripts, its console as
 * CONSOLE lines, its clock starting now, and the loader for its images.
 */
static int
shell_make_page(
	struct shell_state *state,
	const char *path,
	struct page **page)
{
	struct page *made;
	int error;

	/* The page. */
	*page = NULL;
	error = page_create(&made, state->stack_base);
	if (error != 0) {
		printf("ZBROWSER ERROR load path=%s error=%s\n", path, strerror(error));
		fflush(stdout);
		return error;
	}

	/* Its scripts see the window's size, write their console as CONSOLE lines, and count time from now. */
	page_set_viewport(made, (int)state->width, (int)state->height);
	if (state->present.extent.width != 0U)
		page_set_viewport(made, (int)state->present.extent.width, (int)state->present.extent.height);
	page_set_console(made, shell_console, NULL);
	state->open_epoch = shell_clock();

	/* Its http and https images come through the loader. */
	page_set_loader(made, state->loader);

	/* Succeeded: the page is empty. */
	*page = made;
	return 0;
}

/*
 * Starts fetching an http or https page (any load under way is stopped);
 * the page shown stays until it arrives (shell_document_arrived).
 */
static int
shell_start_load(
	struct shell_state *state,
	const char *path,
	int step)
{
	char *copy;
	int error;

	/* One load at a time. */
	shell_stop_load(state, 0);

	/* The location, kept for the arrival. */
	copy = strdup(path);
	if (copy == NULL)
		return ENOMEM;

	/* The request. */
	error = page_net_fetch(state->loader, path, shell_document_arrived, state, &state->pending);
	if (error != 0) {
		printf("ZBROWSER ERROR load path=%s error=%s\n", path, strerror(error));
		fflush(stdout);
		free(copy);
		return error;
	}

	/* Succeeded: the page is loading. */
	state->pending_path = copy;
	state->pending_step = step;
	printf("ZBROWSER LOADING url=%s\n", path);
	fflush(stdout);
	return 0;
}

/*
 * The loader's callback for the page being fetched: a response becomes the
 * page shown (at its final URL, after redirects); a failure is reported and
 * the page shown stays.
 */
static void
shell_document_arrived(
	void *context,
	struct net_request *request)
{
	struct shell_state *state;
	const unsigned char *bytes;
	const char *url;
	const char *reason;
	struct page *page;
	char *path;
	size_t length;
	int step;
	int error;

	/* The load is over. */
	state = context;
	path = state->pending_path;
	step = state->pending_step;
	state->pending = NULL;
	state->pending_path = NULL;

	/* A request that failed leaves the page shown. */
	error = page_net_result(request, &bytes, &length, &url);
	if (error != 0) {
		reason = page_failure_reason();
		printf("ZBROWSER ERROR load path=%s error=%s tls=%s\n", path, strerror(error), reason);
		fflush(stdout);
		free(path);
		return;
	}

	/* The document, in a new page at its final URL. */
	error = shell_make_page(state, url, &page);
	if (error == 0) {
		error = page_load_bytes(page, bytes, length, url);
		if (error == 0)
			error = page_open_fonts(page, state->fonts);
		if (error != 0) {
			printf("ZBROWSER ERROR load path=%s error=%s\n", url, strerror(error));
			fflush(stdout);
			page_destroy(page);
		}
	}

	/* The new page is shown. */
	if (error == 0)
		(void)shell_show_page(state, page, url, step);
	free(path);
}

/* Stops the page being fetched, if any; report writes the STOPPED line. */
static void
shell_stop_load(
	struct shell_state *state,
	int report)
{
	/* Nothing loads. */
	if (state->pending == NULL)
		return;

	/* The request, and the location kept for it. */
	page_net_cancel(state->pending);
	state->pending = NULL;
	if (report) {
		printf("ZBROWSER STOPPED url=%s\n", state->pending_path);
		fflush(stdout);
	}

	/* The location kept for it goes too. */
	free(state->pending_path);
	state->pending_path = NULL;
}

/* Runs the network after the main loop's poll: the requests move on, and their callbacks run. */
static void
shell_run_network(
	struct shell_state *state,
	struct pollfd *fds,
	size_t count)
{
	/* The loader's work. */
	page_net_process(state->loader, fds, count);
}

/* Writes the absolute path of the page the command line names (a path, relative or not, or a file: URL). */
static int
shell_absolute(
	const char *start,
	struct wb_buffer *path)
{
	char directory[1024];
	char *found;
	int error;

	/* The working directory, which a relative path starts from, with a slash to make it a base. */
	found = getcwd(directory, sizeof(directory) - 1U);
	if (found == NULL)
		return errno;
	strcat(directory, "/");

	/* The start resolved against it, as a link would be. */
	error = page_resolve_location(directory, start, path);
	if (error != 0)
		return error;

	/* Succeeded: the path is absolute. */
	return 0;
}

/* Makes a page and loads a file into it with the fonts; reports the error line's cause, or 0. */
static int
shell_open_page(
	struct shell_state *state,
	const char *path,
	struct page **page)
{
	struct page *loaded;
	const char *reason;
	int error;

	/* The page. */
	*page = NULL;
	error = shell_make_page(state, path, &loaded);
	if (error != 0)
		return error;

	/* The file. */
	error = page_load_location(loaded, path);
	if (error == 0)
		error = page_open_fonts(loaded, state->fonts);
	if (error != 0) {
		reason = page_failure_reason();
		printf("ZBROWSER ERROR load path=%s error=%s tls=%s\n", path, strerror(error), reason);
		fflush(stdout);
		page_destroy(loaded);
		return error;
	}

	/* Succeeded: the page is loaded. */
	*page = loaded;
	return 0;
}

/*
 * Makes the page of a path the one shown, as a new step of the history or
 * one already in it; the page of the first call is already loaded.
 * Returns 0, or an errno value when the page could not be opened (the page
 * shown stays).
 */
static int
shell_navigate(
	struct shell_state *state,
	const char *path,
	int step)
{
	struct page *page;
	int remote;
	int error;

	/* The first page is loaded before the window. */
	if (state->path == NULL) {
		error = shell_show_page(state, state->page, path, step);
		return error;
	}

	/* An http or https page is fetched without blocking; the page shown stays until it arrives. */
	remote = page_net_is_remote(path);
	if (remote) {
		error = shell_start_load(state, path, step);
		return error;
	}

	/* Any other page is read at once, and shown. */
	shell_stop_load(state, 0);
	error = shell_open_page(state, path, &page);
	if (error != 0)
		return error;
	error = shell_show_page(state, page, path, step);
	if (error != 0)
		return error;

	/* Succeeded: the page is the one shown. */
	return 0;
}

/*
 * Makes a loaded page the one shown (the first page is already the state's
 * page), as a new step of the history or one already in it.  Returns 0, or
 * an errno value.
 */
static int
shell_show_page(
	struct shell_state *state,
	struct page *page,
	const char *path,
	int step)
{
	char *copy;
	size_t index;
	int error;

	/* The page's own location (a URL's after its redirects) is kept for the history and for resolving links. */
	if (page->base != NULL)
		path = page->base;
	copy = strdup(path);
	if (copy == NULL) {
		if (page != state->page)
			page_destroy(page);
		return ENOMEM;
	}

	/* The new page replaces the old one, from its top, with its own clock. */
	if (page != state->page)
		page_destroy(state->page);
	state->page = page;
	state->page_epoch = state->open_epoch;
	free(state->path);
	state->path = copy;
	state->scroll_y = 0;

	/* A new step drops the steps after the one shown, and the oldest when the history is full. */
	if (step == SHELL_STEP_NEW) {
		for (index = state->history_index + 1U; index < state->history_count; index++)
			free(state->history[index]);
		if (state->history_count != 0)
			state->history_count = state->history_index + 1U;
		if (state->history_count == SHELL_HISTORY_MAX) {
			free(state->history[0]);
			memmove(state->history, state->history + 1, (SHELL_HISTORY_MAX - 1U) * sizeof(state->history[0]));
			state->history_count--;
		}
	}

	/* Records the new step. */
	if (step == SHELL_STEP_NEW) {
		state->history[state->history_count] = strdup(path);
		if (state->history[state->history_count] == NULL)
			return ENOMEM;
		state->history_index = state->history_count;
		state->history_count++;
	}

	/* A page shown in the window is laid out at its size and drawn. */
	if (state->window.display != NULL) {
		error = shell_lay_out(state);
		if (error != 0)
			return EIO;
		shell_show_state(state);
		state->dirty = 1;
	}

	/* Succeeded: the page is the one shown. */
	return 0;
}

/* Lays the page out at the swapchain's size, paints it, and keeps the scroll inside the document; nonzero on failure. */
static int
shell_lay_out(
	struct shell_state *state)
{
	int error;

	/* The layout at the window's size. */
	error = page_layout(state->page, (int)state->present.extent.width, (int)state->present.extent.height);
	if (error != 0) {
		fprintf(stderr, "browser: cannot lay out the page: %s\n", strerror(error));
		return 1;
	}

	/* The display list. */
	error = page_paint(state->page);
	if (error != 0) {
		fprintf(stderr, "browser: cannot paint the page: %s\n", strerror(error));
		return 1;
	}

	/* The scroll stays inside the new document. */
	shell_scroll_by(state, 0);

	/* Succeeded: the page is ready to draw. */
	return 0;
}

/* Shows the page's title in the window and the history and location in the titlebar, and writes the NAVIGATE line. */
static void
shell_show_state(
	struct shell_state *state)
{
	struct wb_buffer title;
	const char *shown;
	int can_back;
	int can_forward;
	int error;

	/* The window's title: the page's, or its file's path. */
	wb_buffer_init(&title);
	error = page_title(state->page, &title);
	shown = wb_buffer_string(&title);
	if (error != 0 || title.length == 0)
		shown = state->path;
	shell_window_title(&state->window, shown);

	/* The line the tests read. */
	printf("ZBROWSER NAVIGATE path=%s title=%s\n", state->path, shown);
	fflush(stdout);
	wb_buffer_release(&title);

	/* The history's steps from the one shown. */
	can_back = 0;
	if (state->history_index > 0)
		can_back = 1;
	can_forward = 0;
	if (state->history_index + 1U < state->history_count)
		can_forward = 1;

	/* The titlebar; a refusal is reported and the titlebar stays as it was. */
	error = shell_titlebar_show(&state->titlebar, can_back, can_forward, state->path);
	if (error != 0) {
		printf("ZBROWSER ERROR titlebar-state error=%d\n", error);
		fflush(stdout);
	}
}

/* Carries out one input: a click, a scroll, a key that scrolls, or a key of the history, the location or the window. */
static void
shell_input(
	struct shell_state *state,
	const struct shell_event *event)
{
	layout_unit page_step;
	int error;

	/* A button: a click on a link opens it. */
	if (event->type == SHELL_EVENT_BUTTON) {
		shell_click(state, event);
		return;
	}

	/* The wheel scrolls by its distance. */
	if (event->type == SHELL_EVENT_SCROLL) {
		shell_scroll_by(state, (layout_unit)event->scroll * LAYOUT_UNIT);
		return;
	}

	/* Ctrl+Q and Ctrl+W close the window; Ctrl+L edits the location. */
	if ((event->modifiers & SHELL_MOD_CTRL) != 0U) {
		if (event->key == SHELL_KEY_Q || event->key == SHELL_KEY_W)
			state->window.closed = 1;
		if (event->key == SHELL_KEY_L) {
			error = shell_titlebar_edit_location(&state->titlebar);
			if (error != 0)
				printf("ZBROWSER ERROR location-edit error=%d\n", error);
		}

		/* Other keys with Ctrl do nothing yet. */
		return;
	}

	/* Alt+Left and Alt+Right step through the history. */
	if ((event->modifiers & SHELL_MOD_ALT) != 0U) {
		if (event->key == SHELL_KEY_LEFT)
			shell_history_go(state, -1);
		if (event->key == SHELL_KEY_RIGHT)
			shell_history_go(state, 1);
		return;
	}

	/* A page's step: the window's height less the overlap kept in view. */
	page_step = ((layout_unit)state->present.extent.height - SHELL_PAGE_OVERLAP) * LAYOUT_UNIT;

	/* The keys that scroll, and F5 that reloads. */
	switch (event->key) {
	case SHELL_KEY_DOWN:
		shell_scroll_by(state, SHELL_LINE_SCROLL * LAYOUT_UNIT);
		break;
	case SHELL_KEY_UP:
		shell_scroll_by(state, -SHELL_LINE_SCROLL * LAYOUT_UNIT);
		break;
	case SHELL_KEY_PAGEDOWN:
	case SHELL_KEY_SPACE:
		shell_scroll_by(state, page_step);
		break;
	case SHELL_KEY_PAGEUP:
		shell_scroll_by(state, -page_step);
		break;
	case SHELL_KEY_HOME:
		shell_scroll_by(state, -state->scroll_y);
		break;
	case SHELL_KEY_END:
		shell_scroll_by(state, state->page->layout.document_height);
		break;
	case SHELL_KEY_F5:
		shell_history_go(state, 0);
		break;
	case SHELL_KEY_ESC:
		shell_stop_load(state, 1);
		break;
	default:
		break;
	}
}

/* Opens the link under a left click: a press and a release at nearly the same place. */
static void
shell_click(
	struct shell_state *state,
	const struct shell_event *event)
{
	struct wb_buffer href;
	int distance_x;
	int distance_y;
	int page_y;
	int canceled;
	int changed;
	int found;
	int error;

	/* Only the left button clicks. */
	if (event->button != SHELL_BUTTON_LEFT)
		return;

	/* A press remembers its place. */
	if (event->pressed) {
		state->press_x = event->x;
		state->press_y = event->y;
		state->pressed = 1;
		return;
	}

	/* A release without its press, or far from it, is not a click. */
	if (!state->pressed)
		return;
	state->pressed = 0;
	distance_x = abs(event->x - state->press_x);
	distance_y = abs(event->y - state->press_y);
	if (distance_x > SHELL_CLICK_SLOP || distance_y > SHELL_CLICK_SLOP)
		return;

	/* The page's scripts get the click first; a canceled click opens no link. */
	page_y = event->y + (int)(state->scroll_y / LAYOUT_UNIT);
	error = page_click(state->page, event->x, page_y, event->x, event->y, &canceled);
	if (error != 0 || canceled)
		return;

	/* A page the listeners changed is laid out again before its link is looked for. */
	changed = page_needs_layout(state->page);
	if (changed) {
		error = shell_lay_out(state);
		if (error != 0)
			return;
		state->dirty = 1;
	}

	/* The link under the release, in the document's coordinates. */
	wb_buffer_init(&href);
	error = page_link_at(state->page, event->x, page_y, &href, &found);
	if (error != 0 || !found) {
		wb_buffer_release(&href);
		return;
	}

	/* Opens it. */
	printf("ZBROWSER LINK href=%s\n", wb_buffer_string(&href));
	fflush(stdout);
	shell_follow(state, wb_buffer_string(&href));
	wb_buffer_release(&href);
}

/* Carries out what was done with the titlebar: back, forward, reload, or the location clicked or edited. */
static void
shell_titlebar_input(
	struct shell_state *state,
	const struct shell_titlebar_event *event)
{
	int error;

	/* The location's editing ended: Enter opens the URL typed; Esc and leaving change nothing. */
	if (event->kind == SHELL_TITLEBAR_DONE) {
		if (event->id != SHELL_CONTROL_LOCATION)
			return;
		if (event->detail != KEILAND_TEXT_SUBMITTED)
			return;
		shell_follow(state, event->text);
		return;
	}

	/* A control chosen. */
	switch (event->id) {
	case SHELL_CONTROL_BACK:
		shell_history_go(state, -1);
		break;
	case SHELL_CONTROL_FORWARD:
		shell_history_go(state, 1);
		break;
	case SHELL_CONTROL_RELOAD:
		shell_history_go(state, 0);
		break;
	case SHELL_CONTROL_LOCATION:
		/* A part of the location turns it into the URL's field. */
		error = shell_titlebar_edit_location(&state->titlebar);
		if (error != 0)
			printf("ZBROWSER ERROR location-edit error=%d\n", error);
		break;
	default:
		break;
	}
}

/* Shows the page a step back (-1), forward (1), or the same page again (0, a reload). */
static void
shell_history_go(
	struct shell_state *state,
	int direction)
{
	size_t index;
	int error;

	/* No step before the first or after the last. */
	if (direction < 0 && state->history_index == 0)
		return;
	if (direction > 0 && state->history_index + 1U >= state->history_count)
		return;

	/* The step to show. */
	index = state->history_index;
	if (direction < 0)
		state->history_index--;
	if (direction > 0)
		state->history_index++;

	/* Its page, loaded again; a page that does not open leaves the history where it was. */
	error = shell_navigate(state, state->history[state->history_index], SHELL_STEP_KEEP);
	if (error != 0)
		state->history_index = index;
}

/* Opens a link's or the location's target, resolved against the page shown, as a new step. */
static void
shell_follow(
	struct shell_state *state,
	const char *target)
{
	struct wb_buffer path;
	int error;

	/* The target's path. */
	wb_buffer_init(&path);
	error = page_resolve_location(state->path, target, &path);
	if (error != 0) {
		printf("ZBROWSER ERROR follow target=%s error=%s\n", target, strerror(error));
		fflush(stdout);
		wb_buffer_release(&path);
		return;
	}

	/* Its page, as a new step of the history. */
	(void)shell_navigate(state, wb_buffer_string(&path), SHELL_STEP_NEW);
	wb_buffer_release(&path);
}

/* Moves the scroll by a distance, kept between the top and the last window's worth of the document. */
static void
shell_scroll_by(
	struct shell_state *state,
	layout_unit distance)
{
	layout_unit limit;
	layout_unit scroll_y;

	/* The furthest the page scrolls: the document's height less the window's. */
	limit = state->page->layout.document_height - (layout_unit)state->present.extent.height * LAYOUT_UNIT;
	if (limit < 0)
		limit = 0;

	/* The new place, within the limits. */
	scroll_y = state->scroll_y + distance;
	if (scroll_y > limit)
		scroll_y = limit;
	if (scroll_y < 0)
		scroll_y = 0;

	/* A change is drawn in the next frame. */
	if (scroll_y != state->scroll_y) {
		state->scroll_y = scroll_y;
		state->dirty = 1;
	}
}

/* Draws the page into the window; a swapchain out of date is replaced and the frame drawn again. Nonzero on failure. */
static int
shell_frame(
	struct shell_state *state)
{
	VkResult result;

	/* The frame. */
	result = shell_present_frame(&state->present, &state->page->paint, &state->page->text, state->scroll_y);

	/* A swapchain that no longer fits the window is replaced, and the frame drawn once more. */
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		result = shell_present_resize(&state->present, state->window.width, state->window.height);
		if (result == VK_SUCCESS)
			result = shell_present_frame(&state->present, &state->page->paint, &state->page->text, state->scroll_y);
	}

	/* Says what failed. */
	if (result != VK_SUCCESS) {
		fprintf(stderr, "browser: cannot draw a frame: %s failed (%d)\n", state->present.operation, (int)result);
		return 1;
	}

	/* The line the tests read. */
	printf("ZBROWSER FRAME scroll=%.0f width=%u height=%u\n", (double)layout_to_px(state->scroll_y),
	    (unsigned)state->present.extent.width, (unsigned)state->present.extent.height);
	fflush(stdout);

	/* Succeeded: the frame is shown. */
	return 0;
}

/* Releases the presenter, the titlebar, the window, the page and the history, in that order. */
static void
shell_release(
	struct shell_state *state)
{
	size_t index;

	/* The presenter before the window whose surface it draws. */
	shell_present_close(&state->present);

	/* The titlebar before the window it belongs to. */
	shell_titlebar_close(&state->titlebar);

	/* The window. */
	if (state->window.display != NULL)
		shell_window_close(&state->window);

	/* A load under way, then the page and its path (its image requests go with it). */
	shell_stop_load(state, 0);
	page_destroy(state->page);
	state->page = NULL;
	free(state->path);
	state->path = NULL;

	/* The history's paths. */
	for (index = 0; index < state->history_count; index++)
		free(state->history[index]);
	state->history_count = 0;

	/* The loader, once no page uses it. */
	page_net_destroy(state->loader);
	state->loader = NULL;
}

/* Reports how long to wait for the compositor: until a held key repeats or the page's next timer is due (-1: no limit). */
static int
shell_wait(
	struct shell_state *state,
	uint64_t now)
{
	double due;
	double page_now;
	double wait;
	int network;
	int timeout;
	int found;

	/* A held key's repeat. */
	timeout = shell_window_repeat(&state->window, now);

	/* The network's earliest time out. */
	network = page_net_timeout(state->loader);
	if (network >= 0 && (timeout < 0 || network < timeout))
		timeout = network;

	/* The page's next timer, in the page's own time. */
	found = page_next_timer(state->page, &due);
	if (!found)
		return timeout;
	page_now = (double)(now - state->page_epoch);
	wait = due - page_now;
	if (wait < 0.0)
		wait = 0.0;
	if (wait > 60000.0)
		wait = 60000.0;

	/* The sooner of the two. */
	if (timeout < 0 || (int)wait < timeout)
		timeout = (int)wait;
	return timeout;
}

/* Runs the page's timers that are due, then lays the page out again and redraws it when its scripts changed it. */
static void
shell_run_page(
	struct shell_state *state)
{
	struct wb_buffer title;
	uint64_t now;
	int changed;
	int error;

	/* The timers due by now, in the page's own time. */
	now = shell_clock();
	error = page_set_time(state->page, (double)(now - state->page_epoch));
	if (error != 0) {
		printf("ZBROWSER ERROR script error=%s\n", strerror(error));
		fflush(stdout);
	}

	/* A page the scripts left as it was needs nothing more. */
	changed = page_needs_layout(state->page);
	if (!changed)
		return;

	/* The layout and the frame, and the title the scripts may have set. */
	error = shell_lay_out(state);
	if (error != 0)
		return;
	state->dirty = 1;
	wb_buffer_init(&title);
	error = page_title(state->page, &title);
	if (error == 0 && title.length != 0)
		shell_window_title(&state->window, wb_buffer_string(&title));
	wb_buffer_release(&title);
}

/* Writes a page's console line as a CONSOLE line of the diagnostic interface. */
static void
shell_console(
	void *context,
	int level,
	const char *text,
	size_t length)
{
	UNUSED_PARAMETER(context);

	/* The level's number and the text. */
	printf("ZBROWSER CONSOLE level=%d %.*s\n", level, (int)length, text);
	fflush(stdout);
}

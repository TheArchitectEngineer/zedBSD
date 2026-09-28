/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window mode of browser: a view (view/view.h) shown in a zdesktop
 * window and drawn by the GPU renderer into the window's swapchain.  The
 * view holds the page, its history, its scroll, its timers and the
 * network; the shell holds the window, zdesktop's titlebar (back, forward,
 * reload and the location, whose URL can be edited) and the presenter, and
 * turns the window's input into the view's calls: the wheel and the keys
 * scroll, a click goes to the view (the page's scripts, then the link under
 * it), Alt+Left and Alt+Right and the titlebar step through the history,
 * F5 reloads, Esc stops a load, and Ctrl+Q or Ctrl+W close the window.
 *
 * The program writes lines to standard output that the guest tests read
 * (ZBROWSER READY, FRAME, LINK, NAVIGATE, TITLEBAR, CONSOLE, LOADING,
 * STOPPED, ERROR); they are its diagnostic interface.
 */

#include "shell/internal.h"
#include "view/view.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* How far a press of an arrow key scrolls, in pixels. */
#define SHELL_LINE_SCROLL	40

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

/*
 * What the window mode holds while it runs: the view, the window with its
 * titlebar and presenter, whether the view must be drawn again, where the
 * left button went down, and whether the window is up (the view's first
 * page is loaded before it).
 */
struct shell_state {
	struct browser_view *view;
	struct shell_window window;
	struct shell_titlebar titlebar;
	struct shell_present present;
	int dirty;
	int press_x;
	int press_y;
	int pressed;
	int ready;
};

static void shell_show_state(struct shell_state *state);
static void shell_input(struct shell_state *state, const struct shell_event *event);
static void shell_click(struct shell_state *state, const struct shell_event *event);
static void shell_titlebar_input(struct shell_state *state, const struct shell_titlebar_event *event);
static void shell_go(struct shell_state *state, int steps);
static void shell_follow(struct shell_state *state, const char *target);
static int shell_frame(struct shell_state *state);
static void shell_release(struct shell_state *state);
static int shell_resize_view(struct shell_state *state);
static void shell_redraw(void *context, struct browser_view *view);
static void shell_title(void *context, struct browser_view *view, const char *title);
static void shell_committed(void *context, struct browser_view *view);
static void shell_load(void *context, struct browser_view *view, enum browser_load_state state, const char *url, int error, const char *reason);
static enum browser_policy shell_link(void *context, struct browser_view *view, const char *href);
static void shell_console(void *context, struct browser_view *view, int level, const char *text, size_t length);
static void shell_script_error(void *context, struct browser_view *view, int error);

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
	struct browser_callbacks callbacks;
	struct browser_view_options view_options;
	struct pollfd net_fds[SHELL_NET_FDS];
	struct shell_event event;
	struct shell_titlebar_event titlebar_event;
	uint64_t now;
	size_t net_count;
	int timeout;
	int network;
	int status;
	int taken;
	int error;
	VkResult result;

	/* A window needs a page to show. */
	memset(&state, 0, sizeof(state));
	if (options->start == NULL) {
		fprintf(stderr, "browser: a page to open is needed (a file or a file: URL)\n");
		return 2;
	}

	/* The view, which tells the shell what happens to it. */
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.redraw = shell_redraw;
	callbacks.title = shell_title;
	callbacks.committed = shell_committed;
	callbacks.load = shell_load;
	callbacks.link = shell_link;
	callbacks.console = shell_console;
	callbacks.script_error = shell_script_error;
	callbacks.context = &state;
	memset(&view_options, 0, sizeof(view_options));
	view_options.fonts = options->fonts;
	view_options.callbacks = &callbacks;
	view_options.stack_base = __builtin_frame_address(0);
	view_options.width = options->width;
	view_options.height = options->height;
	error = browser_view_create(&view_options, &state.view);
	if (error != 0) {
		fprintf(stderr, "browser: cannot start the network: %s\n", strerror(error));
		return 1;
	}

	/* The start page, read before the window opens. */
	error = browser_view_load(state.view, options->start);
	if (error != 0) {
		shell_release(&state);
		return 1;
	}

	/* The window, with the page's title. */
	status = shell_window_open(&state.window, options->display, options->width, options->height,
	    browser_view_title(state.view));
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
	status = shell_resize_view(&state);
	if (status != 0) {
		shell_release(&state);
		return 1;
	}

	/* The line the tests wait for, and the titlebar's state. */
	printf("ZBROWSER READY width=%u height=%u document=%.0f\n", (unsigned)state.present.extent.width,
	    (unsigned)state.present.extent.height, browser_view_document_height(state.view));
	fflush(stdout);
	state.ready = 1;
	shell_show_state(&state);

	/* Draws, then waits for the compositor, until the window closes. */
	state.dirty = 1;
	while (!state.window.closed) {
		/* A new size replaces the swapchain and lays the page out again. */
		if (state.window.resized) {
			state.window.resized = 0;
			result = shell_present_resize(&state.present, state.window.width, state.window.height);
			if (result != VK_SUCCESS) {
				fprintf(stderr, "browser: cannot resize: %s failed (%d)\n", state.present.operation, (int)result);
				break;
			}

			/* The page at the new size. */
			status = shell_resize_view(&state);
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

		/* Waits for the compositor, the view's descriptors, a held key's next repeat, or the view's next work. */
		now = shell_clock();
		timeout = shell_window_repeat(&state.window, now);
		network = browser_view_timeout(state.view);
		if (network >= 0 && (timeout < 0 || network < timeout))
			timeout = network;
		net_count = browser_view_poll_fds(state.view, net_fds, SHELL_NET_FDS);
		status = shell_window_dispatch(&state.window, timeout, net_fds, net_count);
		if (status != 0) {
			fprintf(stderr, "browser: the connection to the compositor was lost\n");
			break;
		}

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

		/* The view's work: the network, the page's timers, and the layout they changed. */
		browser_view_process(state.view, net_fds, net_count);
	}

	/* Closes everything. */
	shell_release(&state);

	/* Succeeded: the window was closed. */
	return 0;
}

/* Gives the view the swapchain's size; nonzero on failure. */
static int
shell_resize_view(
	struct shell_state *state)
{
	int error;

	/* The view at the swapchain's extent. */
	error = browser_view_resize(state->view, state->present.extent.width, state->present.extent.height);
	if (error != 0) {
		fprintf(stderr, "browser: cannot lay out the page: %s\n", strerror(error));
		return 1;
	}

	/* Succeeded: the page fits the window. */
	return 0;
}

/* Shows the page's title in the window and the history and location in the titlebar, and writes the NAVIGATE line. */
static void
shell_show_state(
	struct shell_state *state)
{
	const char *title;
	const char *url;
	int can_back;
	int can_forward;
	int error;

	/* The window's title: the page's, or its location. */
	title = browser_view_title(state->view);
	url = browser_view_url(state->view);
	shell_window_title(&state->window, title);

	/* The line the tests read. */
	printf("ZBROWSER NAVIGATE path=%s title=%s\n", url, title);
	fflush(stdout);

	/* The titlebar; a refusal is reported and the titlebar stays as it was. */
	can_back = browser_view_can_go(state->view, -1);
	can_forward = browser_view_can_go(state->view, 1);
	error = shell_titlebar_show(&state->titlebar, can_back, can_forward, url);
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
	int error;

	/* A button: a click on a link opens it. */
	if (event->type == SHELL_EVENT_BUTTON) {
		shell_click(state, event);
		return;
	}

	/* The wheel scrolls by its distance. */
	if (event->type == SHELL_EVENT_SCROLL) {
		browser_view_scroll_by(state->view, event->scroll);
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
			shell_go(state, -1);
		if (event->key == SHELL_KEY_RIGHT)
			shell_go(state, 1);
		return;
	}

	/* The keys that scroll, F5 that reloads and Esc that stops a load. */
	switch (event->key) {
	case SHELL_KEY_DOWN:
		browser_view_scroll_by(state->view, SHELL_LINE_SCROLL);
		break;
	case SHELL_KEY_UP:
		browser_view_scroll_by(state->view, -SHELL_LINE_SCROLL);
		break;
	case SHELL_KEY_PAGEDOWN:
	case SHELL_KEY_SPACE:
		browser_view_scroll_pages(state->view, 1);
		break;
	case SHELL_KEY_PAGEUP:
		browser_view_scroll_pages(state->view, -1);
		break;
	case SHELL_KEY_HOME:
		browser_view_scroll_to(state->view, BROWSER_SCROLL_TOP);
		break;
	case SHELL_KEY_END:
		browser_view_scroll_to(state->view, BROWSER_SCROLL_BOTTOM);
		break;
	case SHELL_KEY_F5:
		shell_go(state, 0);
		break;
	case SHELL_KEY_ESC:
		browser_view_stop(state->view);
		break;
	default:
		break;
	}
}

/* Gives the view a left click: a press and a release at nearly the same place. */
static void
shell_click(
	struct shell_state *state,
	const struct shell_event *event)
{
	int distance_x;
	int distance_y;
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

	/* The view's click: the page's scripts, then the link under it. */
	error = browser_view_click(state->view, event->x, event->y);
	if (error != 0) {
		printf("ZBROWSER ERROR click error=%s\n", strerror(error));
		fflush(stdout);
	}
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
		shell_go(state, -1);
		break;
	case SHELL_CONTROL_FORWARD:
		shell_go(state, 1);
		break;
	case SHELL_CONTROL_RELOAD:
		shell_go(state, 0);
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

/* Shows the page some steps back or forward, or the same page again (0, a reload). */
static void
shell_go(
	struct shell_state *state,
	int steps)
{
	/* A failure was reported by the load callback; the history stays where it was. */
	(void)browser_view_go(state->view, steps);
}

/* Opens a typed location, resolved against the page shown, as a new step. */
static void
shell_follow(
	struct shell_state *state,
	const char *target)
{
	int error;

	/* The view's navigation; a location that does not resolve is reported here. */
	error = browser_view_follow(state->view, target);
	if (error == EINVAL) {
		printf("ZBROWSER ERROR follow target=%s error=%s\n", target, strerror(error));
		fflush(stdout);
	}
}

/* Draws the view into the window; a swapchain out of date is replaced and the frame drawn again. Nonzero on failure. */
static int
shell_frame(
	struct shell_state *state)
{
	const struct paint_list *list;
	struct text_system *text;
	layout_unit scroll_y;
	VkResult result;

	/* The frame. */
	browser_view_display(state->view, &list, &text, &scroll_y);
	result = shell_present_frame(&state->present, list, text, scroll_y);

	/* A swapchain that no longer fits the window is replaced, and the frame drawn once more. */
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		result = shell_present_resize(&state->present, state->window.width, state->window.height);
		if (result == VK_SUCCESS)
			result = shell_present_frame(&state->present, list, text, scroll_y);
	}

	/* Says what failed. */
	if (result != VK_SUCCESS) {
		fprintf(stderr, "browser: cannot draw a frame: %s failed (%d)\n", state->present.operation, (int)result);
		return 1;
	}

	/* The line the tests read. */
	printf("ZBROWSER FRAME scroll=%.0f width=%u height=%u\n", browser_view_scroll_y(state->view),
	    (unsigned)state->present.extent.width, (unsigned)state->present.extent.height);
	fflush(stdout);

	/* Succeeded: the frame is shown. */
	return 0;
}

/* Releases the presenter, the titlebar, the window and the view, in that order. */
static void
shell_release(
	struct shell_state *state)
{
	/* The presenter before the window whose surface it draws. */
	shell_present_close(&state->present);

	/* The titlebar before the window it belongs to. */
	shell_titlebar_close(&state->titlebar);

	/* The window. */
	if (state->window.display != NULL)
		shell_window_close(&state->window);

	/* The view, with its page, its history and its network. */
	browser_view_destroy(state->view);
	state->view = NULL;
}

/* The view's callback: its content changed and is drawn in the next frame. */
static void
shell_redraw(
	void *context,
	struct browser_view *view)
{
	struct shell_state *state;

	/* The next frame. */
	UNUSED_PARAMETER(view);
	state = context;
	state->dirty = 1;
}

/* The view's callback: a script changed the title, which the window shows. */
static void
shell_title(
	void *context,
	struct browser_view *view,
	const char *title)
{
	struct shell_state *state;

	/* The window's title, once the window is up. */
	UNUSED_PARAMETER(view);
	state = context;
	if (state->ready)
		shell_window_title(&state->window, title);
}

/* The view's callback: a page became the one shown (the first page is shown once the window is up). */
static void
shell_committed(
	void *context,
	struct browser_view *view)
{
	struct shell_state *state;

	/* The title, the NAVIGATE line and the titlebar. */
	UNUSED_PARAMETER(view);
	state = context;
	if (state->ready)
		shell_show_state(state);
}

/* The view's callback: a load started (LOADING), was stopped (STOPPED) or failed (ERROR). */
static void
shell_load(
	void *context,
	struct browser_view *view,
	enum browser_load_state state,
	const char *url,
	int error,
	const char *reason)
{
	/* The line the tests read. */
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(view);
	if (state == BROWSER_LOAD_STARTED)
		printf("ZBROWSER LOADING url=%s\n", url);
	if (state == BROWSER_LOAD_STOPPED)
		printf("ZBROWSER STOPPED url=%s\n", url);
	if (state == BROWSER_LOAD_FAILED)
		printf("ZBROWSER ERROR load path=%s error=%s tls=%s\n", url, strerror(error), reason);
	fflush(stdout);
}

/* The view's callback: a click opens a link, which the shell allows (and writes the LINK line). */
static enum browser_policy
shell_link(
	void *context,
	struct browser_view *view,
	const char *href)
{
	/* The line the tests read. */
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(view);
	printf("ZBROWSER LINK href=%s\n", href);
	fflush(stdout);

	/* The view follows it. */
	return BROWSER_POLICY_ALLOW;
}

/* The view's callback: a page's console line, as a CONSOLE line of the diagnostic interface. */
static void
shell_console(
	void *context,
	struct browser_view *view,
	int level,
	const char *text,
	size_t length)
{
	/* The level's number and the text. */
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(view);
	printf("ZBROWSER CONSOLE level=%d %.*s\n", level, (int)length, text);
	fflush(stdout);
}

/* The view's callback: a timer's script failed. */
static void
shell_script_error(
	void *context,
	struct browser_view *view,
	int error)
{
	/* The line the tests read. */
	UNUSED_PARAMETER(context);
	UNUSED_PARAMETER(view);
	printf("ZBROWSER ERROR script error=%s\n", strerror(error));
	fflush(stdout);
}

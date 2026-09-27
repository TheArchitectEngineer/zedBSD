/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window mode of zdesktop-browser: a page loaded from a file, laid out
 * at the window's width, and drawn by the GPU renderer into the window's
 * swapchain; the wheel and the keys scroll it.
 *
 * The program writes one line to standard output when the window is up
 * (ZBROWSER READY) and one for each frame (ZBROWSER FRAME), which the guest
 * tests read; they are its diagnostic interface.  The URL field, links and
 * the history arrive in ws074-p045.
 */

#include "shell/internal.h"
#include "page/page.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

/* The scheme a file URL starts with. */
#define SHELL_FILE_SCHEME	"file://"

/* How far a press of an arrow key scrolls, in pixels. */
#define SHELL_LINE_SCROLL	40

/* How much of the window a page scroll keeps in view, in pixels. */
#define SHELL_PAGE_OVERLAP	40

/* The evdev codes of the keys the shell reads. */
#define SHELL_KEY_Q		16U
#define SHELL_KEY_W		17U
#define SHELL_KEY_SPACE		57U
#define SHELL_KEY_HOME		102U
#define SHELL_KEY_UP		103U
#define SHELL_KEY_PAGEUP	104U
#define SHELL_KEY_END		107U
#define SHELL_KEY_DOWN		108U
#define SHELL_KEY_PAGEDOWN	109U

/*
 * What the window mode holds while it runs: the page, the window and its
 * presenter, and how far the page is scrolled (in layout units, from 0 to
 * the document's height less the window's).
 */
struct shell_state {
	struct page *page;
	struct shell_window window;
	struct shell_present present;
	layout_unit scroll_y;
	int dirty;
};

static int shell_load(struct shell_state *state, const struct shell_options *options, const void *stack_base);
static int shell_lay_out(struct shell_state *state);
static void shell_input(struct shell_state *state, const struct shell_event *event);
static void shell_scroll_by(struct shell_state *state, layout_unit distance);
static int shell_frame(struct shell_state *state);
static void shell_release(struct shell_state *state);

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
	struct shell_event event;
	struct wb_buffer title;
	const char *shown;
	uint64_t now;
	int timeout;
	int status;
	int taken;
	VkResult result;

	/* A window needs a page to show. */
	memset(&state, 0, sizeof(state));
	if (options->start == NULL) {
		fprintf(stderr, "zdesktop-browser: a page to open is needed (a file or a file: URL)\n");
		return 2;
	}

	/* Loads the page; its heap's stack ends at this frame, which lives for the whole run. */
	status = shell_load(&state, options, __builtin_frame_address(0));
	if (status != 0) {
		shell_release(&state);
		return status;
	}

	/* The window's title: the page's, or the file's name. */
	wb_buffer_init(&title);
	status = page_title(state.page, &title);
	shown = wb_buffer_string(&title);
	if (status != 0 || title.length == 0)
		shown = options->start;

	/* The window. */
	status = shell_window_open(&state.window, options->display, options->width, options->height, shown);
	wb_buffer_release(&title);
	if (status != 0) {
		fprintf(stderr, "zdesktop-browser: cannot open a window: %s\n", strerror(errno));
		shell_release(&state);
		return 1;
	}

	/* The Vulkan presenter in it. */
	result = shell_present_open(&state.present, &state.window);
	if (result != VK_SUCCESS) {
		fprintf(stderr, "zdesktop-browser: cannot draw in the window: %s failed (%d)\n", state.present.operation, (int)result);
		shell_release(&state);
		return 1;
	}

	/* The page at the window's size. */
	status = shell_lay_out(&state);
	if (status != 0) {
		shell_release(&state);
		return 1;
	}

	/* The line the tests wait for. */
	printf("ZBROWSER READY width=%u height=%u document=%.0f\n", (unsigned)state.present.extent.width,
	    (unsigned)state.present.extent.height, (double)layout_to_px(state.page->layout.document_height));
	fflush(stdout);

	/* Draws, then waits for the compositor, until the window closes. */
	state.dirty = 1;
	while (!state.window.closed) {
		/* A new size lays the page out again and replaces the swapchain. */
		if (state.window.resized) {
			state.window.resized = 0;
			result = shell_present_resize(&state.present, state.window.width, state.window.height);
			if (result != VK_SUCCESS) {
				fprintf(stderr, "zdesktop-browser: cannot resize: %s failed (%d)\n", state.present.operation, (int)result);
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

		/* Waits for the compositor, or for a held key's next repeat. */
		now = shell_clock();
		timeout = shell_window_repeat(&state.window, now);
		status = shell_window_dispatch(&state.window, timeout);
		if (status != 0) {
			fprintf(stderr, "zdesktop-browser: the connection to the compositor was lost\n");
			break;
		}

		/* Carries out the inputs that arrived. */
		for (;;) {
			taken = shell_window_take(&state.window, &event);
			if (!taken)
				break;
			shell_input(&state, &event);
		}
	}

	/* Closes everything. */
	shell_release(&state);

	/* Succeeded: the window was closed. */
	return 0;
}

/* Makes the page, loads the file the options name and opens the fonts; reports the exit status of a failure, or 0. */
static int
shell_load(
	struct shell_state *state,
	const struct shell_options *options,
	const void *stack_base)
{
	const char *path;
	size_t scheme_length;
	int differs;
	int error;

	/* A file: URL names the file after its scheme (a host part is not taken yet). */
	path = options->start;
	scheme_length = strlen(SHELL_FILE_SCHEME);
	differs = strncmp(path, SHELL_FILE_SCHEME, scheme_length);
	if (differs == 0)
		path += scheme_length;

	/* The page. */
	error = page_create(&state->page, stack_base);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot make a page: %s\n", strerror(error));
		return 1;
	}

	/* The file. */
	error = page_load_file(state->page, path);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot load %s: %s\n", path, strerror(error));
		return 1;
	}

	/* The fonts. */
	error = page_open_fonts(state->page, options->fonts);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot open the fonts: %s\n", strerror(error));
		return 1;
	}

	/* Succeeded: the page is loaded. */
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
		fprintf(stderr, "zdesktop-browser: cannot lay out the page: %s\n", strerror(error));
		return 1;
	}

	/* The display list. */
	error = page_paint(state->page);
	if (error != 0) {
		fprintf(stderr, "zdesktop-browser: cannot paint the page: %s\n", strerror(error));
		return 1;
	}

	/* The scroll stays inside the new document. */
	shell_scroll_by(state, 0);

	/* Succeeded: the page is ready to draw. */
	return 0;
}

/* Carries out one input: a scroll, a key that scrolls, or the key that closes the window. */
static void
shell_input(
	struct shell_state *state,
	const struct shell_event *event)
{
	layout_unit page_step;

	/* The wheel scrolls by its distance. */
	if (event->type == SHELL_EVENT_SCROLL) {
		shell_scroll_by(state, (layout_unit)event->scroll * LAYOUT_UNIT);
		return;
	}

	/* Ctrl+Q and Ctrl+W close the window. */
	if ((event->modifiers & SHELL_MOD_CTRL) != 0U) {
		if (event->key == SHELL_KEY_Q || event->key == SHELL_KEY_W)
			state->window.closed = 1;
		return;
	}

	/* A page's step: the window's height less the overlap kept in view. */
	page_step = ((layout_unit)state->present.extent.height - SHELL_PAGE_OVERLAP) * LAYOUT_UNIT;

	/* The keys that scroll. */
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
	default:
		break;
	}
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
		fprintf(stderr, "zdesktop-browser: cannot draw a frame: %s failed (%d)\n", state->present.operation, (int)result);
		return 1;
	}

	/* The line the tests read. */
	printf("ZBROWSER FRAME scroll=%.0f width=%u height=%u\n", (double)layout_to_px(state->scroll_y),
	    (unsigned)state->present.extent.width, (unsigned)state->present.extent.height);
	fflush(stdout);

	/* Succeeded: the frame is shown. */
	return 0;
}

/* Releases the presenter, the window and the page, in that order. */
static void
shell_release(
	struct shell_state *state)
{
	/* The presenter before the window whose surface it draws. */
	shell_present_close(&state->present);

	/* The window. */
	if (state->window.display != NULL)
		shell_window_close(&state->window);

	/* The page. */
	page_destroy(state->page);
	state->page = NULL;
}

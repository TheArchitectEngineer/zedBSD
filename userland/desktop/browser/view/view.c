/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The view (view.h): the page shown and its session history, the page
 * being fetched, the scroll, the page's clock and the network, moved out
 * of the window (ws074-p054).  A file or data: page is read at once; an
 * http or https page after the first is fetched without blocking, and the
 * page shown stays until it has arrived (BROWSER_FETCH_DEFAULT; the other
 * ways of fetching read every page at once, or fetch the first one too).
 * The page's timers run on the monotonic clock from the time the page was
 * made, and a page its scripts or its arriving images changed is laid out
 * and painted again the next time its boxes are needed.  The headless
 * modes settle the view on a virtual clock instead, and draw or dump it.
 */

#include "view/view.h"
#include "net/net.h"
#include "page/page.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The most pages the history remembers (the oldest is forgotten first). */
#define VIEW_HISTORY_MAX	64U

/* How much of the view a page scroll keeps in view, in pixels. */
#define VIEW_PAGE_OVERLAP	40

/* The longest a timer is waited for at once, in milliseconds. */
#define VIEW_WAIT_MAX		60000.0

/* How many of the loader's descriptors a headless settle polls at most. */
#define VIEW_POLL_MAX		64U

/* How a page is reached: a new step of the history, or a step already in it. */
enum view_step {
	VIEW_STEP_NEW,
	VIEW_STEP_KEEP
};

/*
 * A view: the page shown and its location, the history of locations
 * (index is the one shown), how far the page is scrolled (layout units),
 * the view's size and whether the page was laid out at another size
 * (resized), the fonts, the stack frame the pages' heaps scan up to, the
 * clock's time when the page shown (page_epoch) and the page being made
 * (open_epoch) began, how pages are fetched, the loader (NULL when every
 * page is read at once), the page being fetched (its location and how it
 * joins the history; NULL when none), the title shown, and the callbacks.
 */
struct browser_view {
	struct page *page;
	char *path;
	char *history[VIEW_HISTORY_MAX];
	size_t history_count;
	size_t history_index;
	layout_unit scroll_y;
	unsigned width;
	unsigned height;
	int resized;
	const struct text_font_paths *fonts;
	const void *stack_base;
	uint64_t page_epoch;
	uint64_t open_epoch;
	enum browser_fetch fetch;
	struct net_loader *loader;
	struct net_request *pending;
	char *pending_path;
	int pending_step;
	struct wb_buffer title;
	struct browser_callbacks callbacks;
};

static uint64_t view_clock(void);
static int view_navigate(struct browser_view *view, const char *path, int step);
static int view_make_page(struct browser_view *view, struct page **page);
static int view_open_page(struct browser_view *view, const char *path, struct page **page);
static int view_show_page(struct browser_view *view, struct page *page, const char *path, int step);
static int view_start_load(struct browser_view *view, const char *path, int step);
static void view_document_arrived(void *context, struct net_request *request);
static void view_stop_load(struct browser_view *view, int report);
static int view_update(struct browser_view *view);
static void view_settle_network(struct browser_view *view);
static void view_clamp_scroll(struct browser_view *view);
static void view_update_title(struct browser_view *view);
static void view_scroll(struct browser_view *view, layout_unit distance);
static void view_redraw(struct browser_view *view);
static void view_failed(struct browser_view *view, const char *url, int error, const char *reason);
static void view_console(void *context, int level, const char *text, size_t length);

/* Makes a view with its loader and no page. */
int
browser_view_create(
	const struct browser_view_options *options,
	struct browser_view **view)
{
	struct browser_view *made;
	int error;

	/* The view, empty. */
	*view = NULL;
	made = calloc(1, sizeof(*made));
	if (made == NULL)
		return ENOMEM;
	made->fonts = options->fonts;
	made->stack_base = options->stack_base;
	made->width = options->width;
	made->height = options->height;
	made->fetch = options->fetch;
	wb_buffer_init(&made->title);
	if (options->callbacks != NULL)
		made->callbacks = *options->callbacks;

	/* A view that reads every page at once needs no loader. */
	if (made->fetch == BROWSER_FETCH_AT_ONCE) {
		*view = made;
		return 0;
	}

	/* The loader of the http and https pages and images. */
	error = page_net_create(&made->loader);
	if (error != 0) {
		free(made);
		return error;
	}

	/* Succeeded: the view has no page yet. */
	*view = made;
	return 0;
}

/* Ends a view: a load under way, the page, the history and the loader, in that order. */
void
browser_view_destroy(
	struct browser_view *view)
{
	size_t index;

	/* No view. */
	if (view == NULL)
		return;

	/* A load under way, then the page and its path (its image requests go with it). */
	view_stop_load(view, 0);
	page_destroy(view->page);
	free(view->path);

	/* The history's paths. */
	for (index = 0; index < view->history_count; index++)
		free(view->history[index]);

	/* The loader, once no page uses it, then the view. */
	if (view->loader != NULL)
		page_net_destroy(view->loader);
	wb_buffer_release(&view->title);
	free(view);
}

/*
 * Loads a location as a new step of the history: a path (relative ones
 * from the working directory) or a URL.  The first page is read at once
 * whatever it is; later http and https pages are fetched without blocking.
 */
int
browser_view_load(
	struct browser_view *view,
	const char *location)
{
	char directory[1024];
	struct wb_buffer path;
	char *found;
	int error;

	/* The working directory, which a relative path starts from, with a slash to make it a base. */
	found = getcwd(directory, sizeof(directory) - 1U);
	if (found == NULL)
		return errno;
	strcat(directory, "/");

	/* The location resolved against it, as a link would be. */
	wb_buffer_init(&path);
	error = page_resolve_location(directory, location, &path);
	if (error == 0)
		error = view_navigate(view, wb_buffer_string(&path), VIEW_STEP_NEW);
	wb_buffer_release(&path);
	if (error != 0)
		return error;

	/* Succeeded: the page is shown, or loading. */
	return 0;
}

/* Opens a link's or a typed location's target, resolved against the page shown, as a new step. */
int
browser_view_follow(
	struct browser_view *view,
	const char *target)
{
	struct wb_buffer path;
	int error;

	/* The target's location. */
	wb_buffer_init(&path);
	error = page_resolve_location(view->path, target, &path);
	if (error != 0) {
		wb_buffer_release(&path);
		return error;
	}

	/* Its page, as a new step of the history. */
	error = view_navigate(view, wb_buffer_string(&path), VIEW_STEP_NEW);
	wb_buffer_release(&path);
	if (error != 0)
		return error;

	/* Succeeded: the page is shown, or loading. */
	return 0;
}

/* Tells whether the history has a step that far from the one shown (0 is the page shown, which can be reloaded). */
int
browser_view_can_go(
	const struct browser_view *view,
	int steps)
{
	/* Back, as far as the first step. */
	if (steps < 0)
		return view->history_index >= (size_t)-steps;

	/* Forward, as far as the last step. */
	if (steps > 0)
		return view->history_index + (size_t)steps < view->history_count;

	/* The page shown. */
	return view->history_count != 0;
}

/*
 * Shows the page some steps back (negative), forward (positive), or the
 * same page again (0, a reload); a step outside the history does nothing.
 */
int
browser_view_go(
	struct browser_view *view,
	int steps)
{
	size_t index;
	int possible;
	int error;

	/* No step before the first or after the last. */
	possible = browser_view_can_go(view, steps);
	if (!possible)
		return 0;

	/* The step to show. */
	index = view->history_index;
	if (steps < 0)
		view->history_index -= (size_t)-steps;
	else
		view->history_index += (size_t)steps;

	/* Its page, loaded again; a page that does not open leaves the history where it was. */
	error = view_navigate(view, view->history[view->history_index], VIEW_STEP_KEEP);
	if (error != 0) {
		view->history_index = index;
		return error;
	}

	/* Succeeded: the page is shown, or loading. */
	return 0;
}

/* Stops the page being fetched, if any (the load callback hears BROWSER_LOAD_STOPPED). */
void
browser_view_stop(
	struct browser_view *view)
{
	/* The load, reported. */
	view_stop_load(view, 1);
}

/* Gives the view a new size: the page is laid out at it again. */
int
browser_view_resize(
	struct browser_view *view,
	unsigned width,
	unsigned height)
{
	/* The size, and the page's scripts see it. */
	view->width = width;
	view->height = height;
	if (view->page == NULL)
		return 0;
	page_set_viewport(view->page, (int)width, (int)height);

	/* The page is laid out at the new size when it is drawn next. */
	view->resized = 1;
	view_redraw(view);

	/* Succeeded: the page will fit the new size. */
	return 0;
}

/* The page shown's location (NULL before the first page). */
const char *
browser_view_url(
	const struct browser_view *view)
{
	/* The location kept for it. */
	return view->path;
}

/* The page shown's title, or its location when it has none. */
const char *
browser_view_title(
	const struct browser_view *view)
{
	/* A page without a title shows its location. */
	if (view->title.length == 0)
		return view->path;

	/* The title. */
	return wb_buffer_string(&view->title);
}

/* The height of the page shown's document in pixels (the page is laid out first when it changed). */
double
browser_view_document_height(
	struct browser_view *view)
{
	int error;

	/* No page is no height. */
	if (view->page == NULL)
		return 0.0;

	/* A page that cannot be laid out has no height either. */
	error = view_update(view);
	if (error != 0)
		return 0.0;

	/* The layout's. */
	return (double)layout_to_px(view->page->layout.document_height);
}

/* Lists the descriptors the view waits on and the events it waits for; returns how many. */
size_t
browser_view_poll_fds(
	const struct browser_view *view,
	struct pollfd *fds,
	size_t capacity)
{
	/* A view without a loader waits on nothing. */
	if (view->loader == NULL)
		return 0;

	/* The loader's. */
	return page_net_poll_fds(view->loader, fds, capacity);
}

/* Reports how long the caller may wait before calling browser_view_process (-1: until a descriptor is ready). */
int
browser_view_timeout(
	const struct browser_view *view)
{
	double due;
	double page_now;
	double wait;
	int timeout;
	int found;

	/* The network's earliest time out (none without a loader). */
	timeout = -1;
	if (view->loader != NULL)
		timeout = page_net_timeout(view->loader);
	if (view->page == NULL)
		return timeout;

	/* The page's next timer, in the page's own time. */
	found = page_next_timer(view->page, &due);
	if (!found)
		return timeout;
	page_now = (double)(view_clock() - view->page_epoch);
	wait = due - page_now;
	if (wait < 0.0)
		wait = 0.0;
	if (wait > VIEW_WAIT_MAX)
		wait = VIEW_WAIT_MAX;

	/* The sooner of the two. */
	if (timeout < 0 || (int)wait < timeout)
		timeout = (int)wait;
	return timeout;
}

/*
 * Does the work due after the caller's poll: the network (pages and
 * images that arrived), the page's timers, and the redraw and the new
 * title when its scripts or its images changed it.
 */
void
browser_view_process(
	struct browser_view *view,
	const struct pollfd *fds,
	size_t count)
{
	int changed;
	int error;

	/* The loader's work: requests that moved on, their callbacks. */
	if (view->loader != NULL)
		page_net_process(view->loader, fds, count);
	if (view->page == NULL)
		return;

	/* The timers due by now, in the page's own time. */
	error = page_set_time(view->page, (double)(view_clock() - view->page_epoch));
	if (error != 0 && view->callbacks.script_error != NULL)
		view->callbacks.script_error(view->callbacks.context, view, error);

	/* A page the scripts and the images left as it was needs nothing more. */
	changed = page_needs_layout(view->page);
	if (!changed)
		return;

	/* The redraw, which lays the page out again, and the title the scripts may have set. */
	view_redraw(view);
	view_update_title(view);
}

/* Scrolls by some pixels (positive is down), within the document. */
void
browser_view_scroll_by(
	struct browser_view *view,
	int pixels)
{
	/* The distance in layout units. */
	view_scroll(view, (layout_unit)pixels * LAYOUT_UNIT);
}

/* Scrolls by pages: the view's height less a little kept in view, each. */
void
browser_view_scroll_pages(
	struct browser_view *view,
	int pages)
{
	layout_unit step;

	/* A page's step. */
	step = ((layout_unit)view->height - VIEW_PAGE_OVERLAP) * LAYOUT_UNIT;
	view_scroll(view, step * pages);
}

/* Scrolls to the document's top or bottom. */
void
browser_view_scroll_to(
	struct browser_view *view,
	enum browser_scroll_place place)
{
	int error;

	/* No page, no scroll. */
	if (view->page == NULL)
		return;

	/* The document's height is the one laid out now. */
	error = view_update(view);
	if (error != 0)
		return;

	/* The top, or the bottom (the scroll stops at the last view's worth). */
	if (place == BROWSER_SCROLL_TOP)
		view_scroll(view, -view->scroll_y);
	else
		view_scroll(view, view->page->layout.document_height);
}

/* How far the page is scrolled, in pixels. */
double
browser_view_scroll_y(
	const struct browser_view *view)
{
	/* The scroll in pixels. */
	return (double)layout_to_px(view->scroll_y);
}

/*
 * A click at a place in the view: the page's scripts get it first; unless
 * they cancel it, the link under it is followed when the link callback
 * allows it.
 */
int
browser_view_click(
	struct browser_view *view,
	int x,
	int y)
{
	struct wb_buffer href;
	enum browser_policy policy;
	int page_y;
	int canceled;
	int changed;
	int found;
	int error;

	/* No page, nothing to click. */
	if (view->page == NULL)
		return 0;

	/* The boxes the click lands on are the page's as it is now. */
	error = view_update(view);
	if (error != 0)
		return error;

	/* The page's scripts get the click first; a canceled click opens no link. */
	page_y = y + (int)(view->scroll_y / LAYOUT_UNIT);
	error = page_click(view->page, x, page_y, x, y, &canceled);
	if (error != 0)
		return error;
	if (canceled)
		return 0;

	/* A page the listeners changed is laid out again before its link is looked for. */
	changed = page_needs_layout(view->page);
	if (changed) {
		error = view_update(view);
		if (error != 0)
			return error;
		view_redraw(view);
	}

	/* The link under the click, in the document's coordinates. */
	wb_buffer_init(&href);
	error = page_link_at(view->page, x, page_y, &href, &found);
	if (error != 0 || !found) {
		wb_buffer_release(&href);
		return error;
	}

	/* The caller decides; an allowed link is followed (a load that fails goes to the load callback). */
	policy = BROWSER_POLICY_ALLOW;
	if (view->callbacks.link != NULL)
		policy = view->callbacks.link(view->callbacks.context, view, wb_buffer_string(&href));
	if (policy == BROWSER_POLICY_ALLOW)
		(void)browser_view_follow(view, wb_buffer_string(&href));
	wb_buffer_release(&href);

	/* Succeeded: the click was carried out. */
	return 0;
}

/*
 * Brings the page to rest for a headless caller: the page being fetched
 * arrives, the page's timers run on a virtual clock up to budget
 * milliseconds, and with BROWSER_SETTLE_LAYOUT the page is laid out, the
 * images its layout asked for arrive, and it is laid out again with them.
 * Returns ENOENT when no page is shown (its load failed, which the load
 * callback heard of), or why the scripts or the layout failed.
 */
int
browser_view_settle(
	struct browser_view *view,
	double budget,
	unsigned flags)
{
	int error;

	/* The page being fetched, if any, arrives or fails. */
	view_settle_network(view);
	if (view->page == NULL)
		return ENOENT;

	/* The page's timers, on the virtual clock. */
	error = page_settle(view->page, budget);
	if (error != 0)
		return error;

	/* The DOM and the style need no layout. */
	if ((flags & BROWSER_SETTLE_LAYOUT) == 0U)
		return 0;

	/* The layout, which asks for the page's images. */
	error = view_update(view);
	if (error != 0)
		return error;

	/* The images that arrive, and the layout again with them. */
	view_settle_network(view);
	error = view_update(view);
	if (error != 0)
		return error;

	/* Succeeded: the page is at rest. */
	return 0;
}

/*
 * Draws the page shown with the CPU renderer into the caller's pixels:
 * width by height pixels of 0xAARRGGBB, rows stride bytes apart, the page
 * scrolled as the view is.
 */
int
browser_view_draw_pixels(
	struct browser_view *view,
	uint32_t *pixels,
	unsigned width,
	unsigned height,
	size_t stride)
{
	struct paint_bitmap bitmap;
	unsigned char *row_start;
	unsigned row;
	int error;

	/* No page has nothing to draw. */
	if (view->page == NULL)
		return ENOENT;

	/* The page as it is now. */
	error = view_update(view);
	if (error != 0)
		return error;

	/* Rows packed one after another are drawn in place. */
	if (stride == (size_t)width * sizeof(uint32_t)) {
		bitmap.pixels = pixels;
		bitmap.width = (int)width;
		bitmap.height = (int)height;
		error = paint_software(&view->page->paint, &view->page->text, view->scroll_y, &bitmap);
		if (error != 0)
			return error;

		/* Succeeded: the pixels hold the page. */
		return 0;
	}

	/* Rows further apart are drawn into a packed bitmap first. */
	error = paint_bitmap_create(&bitmap, (int)width, (int)height);
	if (error != 0)
		return error;
	error = paint_software(&view->page->paint, &view->page->text, view->scroll_y, &bitmap);
	if (error != 0) {
		paint_bitmap_release(&bitmap);
		return error;
	}

	/* Then copied into the caller's rows. */
	for (row = 0; row < height; row++) {
		row_start = (unsigned char *)pixels + (size_t)row * stride;
		memcpy(row_start, bitmap.pixels + (size_t)row * width, (size_t)width * sizeof(uint32_t));
	}

	/* The packed bitmap is no longer needed. */
	paint_bitmap_release(&bitmap);

	/* Succeeded: the pixels hold the page. */
	return 0;
}

/*
 * Writes one of the page shown's text dumps (its DOM, its computed style,
 * its layout or its display list) into memory the caller frees.
 */
int
browser_view_dump(
	struct browser_view *view,
	enum browser_dump kind,
	char **text,
	size_t *length)
{
	struct wb_buffer out;
	int error;

	/* No page has nothing to dump. */
	*text = NULL;
	*length = 0;
	if (view->page == NULL)
		return ENOENT;

	/* The layout and the display list are the page's as it is now. */
	if (kind == BROWSER_DUMP_LAYOUT || kind == BROWSER_DUMP_PAINT) {
		error = view_update(view);
		if (error != 0)
			return error;
	}

	/* The dump, as the kind asks. */
	wb_buffer_init(&out);
	if (kind == BROWSER_DUMP_DOM) {
		error = page_dump_dom(view->page, &out);
	} else if (kind == BROWSER_DUMP_STYLE) {
		error = page_dump_style(view->page, &out);
	} else if (kind == BROWSER_DUMP_LAYOUT) {
		error = layout_dump(&view->page->layout, &out);
	} else {
		error = paint_dump(&view->page->paint, &out);
	}

	/* A dump that could not be written. */
	if (error != 0) {
		wb_buffer_release(&out);
		return error;
	}

	/* An empty dump still gives the caller a string. */
	if (out.data == NULL) {
		error = wb_buffer_append_string(&out, "");
		if (error != 0)
			return error;
	}

	/* Succeeded: the caller owns the text. */
	*text = (char *)out.data;
	*length = out.length;
	return 0;
}

/*
 * Gives what to draw: the page's display list, its text system and the
 * scroll, the page laid out first when it changed.
 */
int
browser_view_display(
	struct browser_view *view,
	const struct paint_list **list,
	struct text_system **text,
	layout_unit *scroll_y)
{
	int error;

	/* No page has nothing to draw. */
	if (view->page == NULL)
		return ENOENT;

	/* The page as it is now. */
	error = view_update(view);
	if (error != 0)
		return error;

	/* Succeeded: the page's. */
	*list = &view->page->paint;
	*text = &view->page->text;
	*scroll_y = view->scroll_y;
	return 0;
}

/* Trusts the CA certificates of a PEM file for https, besides the system's roots, in every view. */
int
browser_add_ca_file(
	const char *path)
{
	int error;

	/* The TLS layer's list. */
	error = net_tls_add_ca_file(path);
	if (error != 0)
		return error;

	/* Succeeded: the file's authorities are trusted. */
	return 0;
}

/* The monotonic clock in milliseconds. */
static uint64_t
view_clock(void)
{
	struct timespec now;
	int status;

	/* The clock. */
	status = clock_gettime(CLOCK_MONOTONIC, &now);
	if (status != 0)
		return 0U;

	/* In milliseconds. */
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/*
 * Makes the page of a location the one shown, as a new step of the history
 * or one already in it: a file or data: page at once, an http or https
 * page as the view fetches (the first at once, or every one at once, or
 * every one without blocking).  Returns 0, or an errno value when the page
 * could not be opened (the page shown stays).
 */
static int
view_navigate(
	struct browser_view *view,
	const char *path,
	int step)
{
	struct page *page;
	int background;
	int remote;
	int error;

	/* Whether an http or https page is fetched without blocking: never without a loader, the first one only when asked. */
	remote = page_net_is_remote(path);
	background = 0;
	if (remote && view->loader != NULL) {
		if (view->page != NULL || view->fetch == BROWSER_FETCH_BACKGROUND)
			background = 1;
	}

	/* Such a page is fetched; the page shown stays until it arrives. */
	if (background) {
		error = view_start_load(view, path, step);
		return error;
	}

	/* Any other page is read at once, and shown. */
	view_stop_load(view, 0);
	error = view_open_page(view, path, &page);
	if (error != 0)
		return error;
	error = view_show_page(view, page, path, step);
	if (error != 0)
		return error;

	/* Succeeded: the page is the one shown. */
	return 0;
}

/*
 * Makes an empty page: its heap scanning the stack up to the view's frame,
 * the view's size for its scripts, its console through the view, its
 * clock starting now, and the loader for its images.
 */
static int
view_make_page(
	struct browser_view *view,
	struct page **page)
{
	struct page *made;
	int error;

	/* The page. */
	*page = NULL;
	error = page_create(&made, view->stack_base);
	if (error != 0)
		return error;

	/* Its scripts see the view's size, write their console through the view, and count time from now. */
	page_set_viewport(made, (int)view->width, (int)view->height);
	page_set_console(made, view_console, view);
	view->open_epoch = view_clock();

	/* Its http and https images come through the loader (without one, they are read at once). */
	page_set_loader(made, view->loader);

	/* Succeeded: the page is empty. */
	*page = made;
	return 0;
}

/* Makes a page and reads a location into it; a failure goes to the load callback. */
static int
view_open_page(
	struct browser_view *view,
	const char *path,
	struct page **page)
{
	struct page *loaded;
	int error;

	/* The page. */
	*page = NULL;
	error = view_make_page(view, &loaded);
	if (error != 0) {
		view_failed(view, path, error, "");
		return error;
	}

	/* The location. */
	error = page_load_location(loaded, path);
	if (error != 0) {
		view_failed(view, path, error, page_failure_reason());
		page_destroy(loaded);
		return error;
	}

	/* Succeeded: the page is loaded. */
	*page = loaded;
	return 0;
}

/*
 * Makes a loaded page the one shown, from its top, as a new step of the
 * history or one already in it; the committed callback hears of it.
 */
static int
view_show_page(
	struct browser_view *view,
	struct page *page,
	const char *path,
	int step)
{
	char *copy;
	size_t index;

	/* The page's own location (a URL's after its redirects) is kept for the history and for resolving links. */
	if (page->base != NULL)
		path = page->base;
	copy = strdup(path);
	if (copy == NULL) {
		page_destroy(page);
		return ENOMEM;
	}

	/* The new page replaces the old one, from its top, with its own clock. */
	page_destroy(view->page);
	view->page = page;
	view->page_epoch = view->open_epoch;
	free(view->path);
	view->path = copy;
	view->scroll_y = 0;

	/* A new step drops the steps after the one shown, and the oldest when the history is full. */
	if (step == VIEW_STEP_NEW) {
		for (index = view->history_index + 1U; index < view->history_count; index++)
			free(view->history[index]);
		if (view->history_count != 0)
			view->history_count = view->history_index + 1U;
		if (view->history_count == VIEW_HISTORY_MAX) {
			free(view->history[0]);
			memmove(view->history, view->history + 1, (VIEW_HISTORY_MAX - 1U) * sizeof(view->history[0]));
			view->history_count--;
		}

		/* Records the new step. */
		view->history[view->history_count] = strdup(path);
		if (view->history[view->history_count] == NULL)
			return ENOMEM;
		view->history_index = view->history_count;
		view->history_count++;
	}

	/* Its title (it is laid out at the view's size when it is drawn). */
	wb_buffer_clear(&view->title);
	(void)page_title(view->page, &view->title);

	/* The caller hears of it and draws it. */
	if (view->callbacks.committed != NULL)
		view->callbacks.committed(view->callbacks.context, view);
	view_redraw(view);

	/* Succeeded: the page is the one shown. */
	return 0;
}

/* Starts fetching an http or https page (any load under way is stopped); the page shown stays until it arrives. */
static int
view_start_load(
	struct browser_view *view,
	const char *path,
	int step)
{
	char *copy;
	int error;

	/* One load at a time. */
	view_stop_load(view, 0);

	/* The location, kept for the arrival. */
	copy = strdup(path);
	if (copy == NULL)
		return ENOMEM;

	/* The request. */
	error = page_net_fetch(view->loader, path, view_document_arrived, view, &view->pending);
	if (error != 0) {
		view_failed(view, path, error, "");
		free(copy);
		return error;
	}

	/* Succeeded: the page is loading. */
	view->pending_path = copy;
	view->pending_step = step;
	if (view->callbacks.load != NULL)
		view->callbacks.load(view->callbacks.context, view, BROWSER_LOAD_STARTED, path, 0, "");
	return 0;
}

/*
 * The loader's callback for the page being fetched: a response becomes the
 * page shown (at its final URL, after redirects); a failure is reported and
 * the page shown stays.
 */
static void
view_document_arrived(
	void *context,
	struct net_request *request)
{
	struct browser_view *view;
	const unsigned char *bytes;
	const char *url;
	struct page *page;
	char *path;
	size_t length;
	int step;
	int error;

	/* The load is over. */
	view = context;
	path = view->pending_path;
	step = view->pending_step;
	view->pending = NULL;
	view->pending_path = NULL;

	/* A request that failed leaves the page shown. */
	error = page_net_result(request, &bytes, &length, &url);
	if (error != 0) {
		view_failed(view, path, error, page_failure_reason());
		free(path);
		return;
	}

	/* The document, in a new page at its final URL. */
	error = view_make_page(view, &page);
	if (error == 0) {
		error = page_load_bytes(page, bytes, length, url);
		if (error != 0)
			page_destroy(page);
	}

	/* The new page is shown; a page that could not be made is reported. */
	if (error == 0)
		(void)view_show_page(view, page, url, step);
	else
		view_failed(view, url, error, "");
	free(path);
}

/* Stops the page being fetched, if any; report tells the load callback. */
static void
view_stop_load(
	struct browser_view *view,
	int report)
{
	/* Nothing loads. */
	if (view->pending == NULL)
		return;

	/* The request. */
	page_net_cancel(view->pending);
	view->pending = NULL;
	if (report && view->callbacks.load != NULL)
		view->callbacks.load(view->callbacks.context, view, BROWSER_LOAD_STOPPED, view->pending_path, 0, "");

	/* The location kept for it goes too. */
	free(view->pending_path);
	view->pending_path = NULL;
}

/*
 * Lays the page out at the view's size and paints it when it changed or
 * the view was resized since it was last laid out (opening the fonts the
 * first time), and keeps the scroll inside the new document.
 */
static int
view_update(
	struct browser_view *view)
{
	int changed;
	int error;

	/* No page has nothing to lay out. */
	if (view->page == NULL)
		return 0;

	/* A page laid out at the view's size and unchanged since is ready. */
	changed = page_needs_layout(view->page);
	if (!changed && !view->resized)
		return 0;

	/* The fonts, opened once. */
	error = page_open_fonts(view->page, view->fonts);
	if (error != 0)
		return error;

	/* The layout at the view's size. */
	error = page_layout(view->page, (int)view->width, (int)view->height);
	if (error != 0)
		return error;

	/* The display list. */
	error = page_paint(view->page);
	if (error != 0)
		return error;

	/* The page fits the view now, and the scroll stays inside the new document. */
	view->resized = 0;
	view_clamp_scroll(view);

	/* Succeeded: the page is ready to draw. */
	return 0;
}

/*
 * Runs the loader until it has no request left (the headless modes): polls
 * its descriptors until one is ready or its earliest time out, then lets
 * it work.
 */
static void
view_settle_network(
	struct browser_view *view)
{
	struct pollfd fds[VIEW_POLL_MAX];
	size_t count;
	int timeout;
	int ready;

	/* A view without a loader read everything at once. */
	if (view->loader == NULL)
		return;

	/* Each round, while a request runs (the loader has no time out when it has none). */
	for (;;) {
		timeout = page_net_timeout(view->loader);
		if (timeout < 0)
			break;

		/* The descriptors, waited for. */
		count = page_net_poll_fds(view->loader, fds, VIEW_POLL_MAX);
		ready = poll(fds, (nfds_t)count, timeout);
		if (ready < 0 && errno != EINTR)
			break;

		/* The loader's work, and the callbacks of the requests that ended. */
		page_net_process(view->loader, fds, count);
	}
}

/* Keeps the scroll between the top and the last view's worth of the document as it is laid out. */
static void
view_clamp_scroll(
	struct browser_view *view)
{
	layout_unit limit;

	/* The furthest the page scrolls: the document's height less the view's. */
	limit = view->page->layout.document_height - (layout_unit)view->height * LAYOUT_UNIT;
	if (limit < 0)
		limit = 0;

	/* The place, within the limits. */
	if (view->scroll_y > limit)
		view->scroll_y = limit;
	if (view->scroll_y < 0)
		view->scroll_y = 0;
}

/* Takes the page's title again, and tells the caller when it changed. */
static void
view_update_title(
	struct browser_view *view)
{
	struct wb_buffer title;
	int differs;
	int error;

	/* The title now. */
	wb_buffer_init(&title);
	error = page_title(view->page, &title);
	if (error != 0 || title.length == 0) {
		wb_buffer_release(&title);
		return;
	}

	/* The same title is no change. */
	differs = 1;
	if (title.length == view->title.length)
		differs = memcmp(title.data, view->title.data, title.length);
	if (differs == 0) {
		wb_buffer_release(&title);
		return;
	}

	/* The new title is kept and told. */
	wb_buffer_release(&view->title);
	view->title = title;
	if (view->callbacks.title != NULL)
		view->callbacks.title(view->callbacks.context, view, wb_buffer_string(&view->title));
}

/* Moves the scroll by a distance, kept between the top and the last view's worth of the document. */
static void
view_scroll(
	struct browser_view *view,
	layout_unit distance)
{
	layout_unit before;
	int error;

	/* No page, no scroll. */
	if (view->page == NULL)
		return;

	/* The document as it is laid out now, which the scroll stays inside. */
	error = view_update(view);
	if (error != 0)
		return;

	/* The new place, within the document. */
	before = view->scroll_y;
	view->scroll_y += distance;
	view_clamp_scroll(view);

	/* A change is drawn again. */
	if (view->scroll_y != before)
		view_redraw(view);
}

/* Asks the caller to draw the view again. */
static void
view_redraw(
	struct browser_view *view)
{
	/* The callback, when there is one. */
	if (view->callbacks.redraw != NULL)
		view->callbacks.redraw(view->callbacks.context, view);
}

/* Tells the caller a load failed. */
static void
view_failed(
	struct browser_view *view,
	const char *url,
	int error,
	const char *reason)
{
	/* The callback, when there is one. */
	if (view->callbacks.load != NULL)
		view->callbacks.load(view->callbacks.context, view, BROWSER_LOAD_FAILED, url, error, reason);
}

/* Passes a page's console line to the caller. */
static void
view_console(
	void *context,
	int level,
	const char *text,
	size_t length)
{
	struct browser_view *view;

	/* The callback, when there is one. */
	view = context;
	if (view->callbacks.console != NULL)
		view->callbacks.console(view->callbacks.context, view, level, text, length);
}

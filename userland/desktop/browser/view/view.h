/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The view: one browsing context (a tab's content) as the engine gives it
 * to the program around it (ws074-p054, the first step of the component
 * of plan/ws074/design.md §19 and plan/ws074/phase053/browser_view.h).
 *
 * A view owns the page shown, the page being fetched, the session history,
 * the scroll, the page's clock and the loader of the network.  The caller
 * (the window of shell/, or a headless mode) makes it, gives it a size,
 * loads a location, polls the descriptors it lists, calls
 * browser_view_process, and hears of what happened through callbacks.  The
 * view knows nothing of Wayland or of windows.
 *
 * The page is laid out when something needs its boxes (a drawing, a dump
 * of the layout, the scroll, a click), not when it changes, so a view that
 * is only asked for its DOM never opens its fonts.  The headless modes and
 * the tests settle a view (browser_view_settle), then draw it with the CPU
 * (browser_view_draw_pixels) or dump it (browser_view_dump).
 *
 * Drawing in a window still reads the view's display list
 * (browser_view_display); the render target of the draft (browser_target)
 * is ws074-p055, and input in the DOM's key names with the default actions
 * in the engine is p056.
 *
 * A view is used from the thread that made it.
 */

#ifndef KEILAND_BROWSER_VIEW_H
#define KEILAND_BROWSER_VIEW_H

#include "layout/layout.h"
#include "paint/paint.h"
#include "text/text.h"

#include <stddef.h>
#include <stdint.h>

/* A view, opaque to the caller. */
struct browser_view;
struct pollfd;

/* How a load went, for the load callback. */
enum browser_load_state {
	BROWSER_LOAD_STARTED,
	BROWSER_LOAD_STOPPED,
	BROWSER_LOAD_FAILED
};

/* What a link callback decides. */
enum browser_policy {
	BROWSER_POLICY_ALLOW,
	BROWSER_POLICY_DENY
};

/*
 * How a view fetches its http and https pages and images: the first page
 * at once and the rest without blocking (the default, a window's), every
 * one at once (the caller waits in each load and each layout), or every
 * one without blocking, the first page too (the caller settles the view).
 */
enum browser_fetch {
	BROWSER_FETCH_DEFAULT,
	BROWSER_FETCH_AT_ONCE,
	BROWSER_FETCH_BACKGROUND
};

/* What browser_view_settle does besides the network and the timers: lay the page out and wait for its images. */
#define BROWSER_SETTLE_LAYOUT	0x01U

/* The text dumps of the page shown (the golden files of the tests are made of them). */
enum browser_dump {
	BROWSER_DUMP_DOM,
	BROWSER_DUMP_STYLE,
	BROWSER_DUMP_LAYOUT,
	BROWSER_DUMP_PAINT
};

/* Where browser_view_scroll_to goes. */
enum browser_scroll_place {
	BROWSER_SCROLL_TOP,
	BROWSER_SCROLL_BOTTOM
};

/*
 * The caller's callbacks (any may be NULL) and the pointer they get back.
 *
 * - redraw: the content or the scroll changed; draw it again.
 * - title: the document's title changed (after a script set it).
 * - committed: a page became the one shown (a navigation, the history or
 *   a reload); browser_view_url, _title and _can_go tell its state.
 * - load: a load of an http or https page started (it is fetched without
 *   blocking), was stopped, or failed (error is an errno value, reason a
 *   text for a TLS failure, or "").  url is the location loaded.
 * - link: a click opens a link (href as the document has it); the caller
 *   allows the view to follow it, or denies it.
 * - console: a script wrote to the console (level 0 log, 1 warn, 2 error).
 * - script_error: a timer's script failed (error is an errno value).
 */
struct browser_callbacks {
	void (*redraw)(void *context, struct browser_view *view);
	void (*title)(void *context, struct browser_view *view, const char *title);
	void (*committed)(void *context, struct browser_view *view);
	void (*load)(void *context, struct browser_view *view, enum browser_load_state state, const char *url, int error,
	    const char *reason);
	enum browser_policy (*link)(void *context, struct browser_view *view, const char *href);
	void (*console)(void *context, struct browser_view *view, int level, const char *text, size_t length);
	void (*script_error)(void *context, struct browser_view *view, int error);
	void *context;
};

/*
 * What a view is made with: the fonts (NULL fields for the defaults), the
 * callbacks, the outermost stack frame of the thread that uses it (the
 * heaps of its pages scan the stack up to it), its size in pixels, and how
 * it fetches (zero is BROWSER_FETCH_DEFAULT).
 */
struct browser_view_options {
	const struct text_font_paths *fonts;
	const struct browser_callbacks *callbacks;
	const void *stack_base;
	unsigned width;
	unsigned height;
	enum browser_fetch fetch;
};

/* Making and ending a view. */
int browser_view_create(const struct browser_view_options *options, struct browser_view **view);
void browser_view_destroy(struct browser_view *view);

/* Loading: a location (a path, file:, data:, http:, https:) as a new step, a link's target, the history, and stopping. */
int browser_view_load(struct browser_view *view, const char *location);
int browser_view_follow(struct browser_view *view, const char *target);
int browser_view_go(struct browser_view *view, int steps);
int browser_view_can_go(const struct browser_view *view, int steps);
void browser_view_stop(struct browser_view *view);

/* The size in pixels. */
int browser_view_resize(struct browser_view *view, unsigned width, unsigned height);

/* The page shown: its location, its title (its location when it has none), and its document's height in pixels. */
const char *browser_view_url(const struct browser_view *view);
const char *browser_view_title(const struct browser_view *view);
double browser_view_document_height(struct browser_view *view);

/* The main loop's side: the descriptors to poll, how long to wait at most (-1: no limit), and the work due. */
size_t browser_view_poll_fds(const struct browser_view *view, struct pollfd *fds, size_t capacity);
int browser_view_timeout(const struct browser_view *view);
void browser_view_process(struct browser_view *view, const struct pollfd *fds, size_t count);

/* Scrolling: by pixels (positive is down), by pages of the view's height, to the top or the bottom; and where it is. */
void browser_view_scroll_by(struct browser_view *view, int pixels);
void browser_view_scroll_pages(struct browser_view *view, int pages);
void browser_view_scroll_to(struct browser_view *view, enum browser_scroll_place place);
double browser_view_scroll_y(const struct browser_view *view);

/* A click at a place in the view's pixels: the page's scripts get it, then its link is followed unless they cancel it. */
int browser_view_click(struct browser_view *view, int x, int y);

/*
 * The headless side: the page brought to rest (the page being fetched has
 * arrived, its timers have run on a virtual clock up to budget
 * milliseconds, and with BROWSER_SETTLE_LAYOUT it is laid out with the
 * images its layout asked for), drawn with the CPU into 0xAARRGGBB pixels
 * whose rows are stride bytes apart, or dumped as text (*text is the
 * caller's to free).
 */
int browser_view_settle(struct browser_view *view, double budget, unsigned flags);
int browser_view_draw_pixels(struct browser_view *view, uint32_t *pixels, unsigned width, unsigned height, size_t stride);
int browser_view_dump(struct browser_view *view, enum browser_dump kind, char **text, size_t *length);

/* What to draw: the display list, the text system its glyphs come from, and the scroll (until p055's render target). */
int browser_view_display(struct browser_view *view, const struct paint_list **list, struct text_system **text,
    layout_unit *scroll_y);

/* The process-wide settings of the engine: the certificate authorities trusted besides the system's (https). */
int browser_add_ca_file(const char *path);

#endif

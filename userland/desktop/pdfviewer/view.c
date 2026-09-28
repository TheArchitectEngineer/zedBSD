/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The view of PDF Viewer: how the pages are laid out, where the view is,
 * and what the keys, the pointer and the actions do to them.
 *
 * In the scroll mode the pages stand in one column, a gap apart, at one
 * scale (the widest page fits the width, or the tallest page fits the
 * window, or the user's zoom); the wheel, a drag and the keys move the
 * view along it.  In the page mode one page is shown at its own fitting
 * scale; a sideways drag moves it with the pointer and, let go far enough
 * or fast enough, turns to the next or the previous page, which slides in
 * (a swipe); the keys and the wheel turn pages too.
 *
 * Nothing here draws or speaks Wayland; draw.c draws what this lays out,
 * and main.c feeds it the window's input.
 */

#include "viewer.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* The zoom steps and bounds, in pixels per point. */
#define VIEW_ZOOM_STEP		1.25
#define VIEW_ZOOM_MIN		0.1
#define VIEW_ZOOM_MAX		6.0

/* How far a key or a wheel notch scrolls, in pixels. */
#define VIEW_KEY_STEP		48.0

/* How far the pointer moves before a press is a drag, in pixels. */
#define VIEW_DRAG_START		6

/* The share of the width, and the speed (pixels a millisecond), that let go of a swipe turn the page. */
#define VIEW_SWIPE_SHARE	0.18
#define VIEW_SWIPE_SPEED	0.6

/* How long a page turn slides, and how long the page indicator stays, in milliseconds. */
#define VIEW_TURN_MS		220U
#define VIEW_INDICATOR_MS	1400U

/* The wheel's travel that turns a page in the page mode, in pixels. */
#define VIEW_WHEEL_TURN		90.0

/* The longest a message stays by default, in milliseconds. */
#define VIEW_MESSAGE_MS		6000U

static size_t current_page(const struct pv_app *app);
static void clamp_view(struct pv_app *app);
static void show_page(struct pv_app *app, size_t index);
static void start_turn(struct pv_app *app, int direction);
static void zoom_by(struct pv_app *app, double factor);
static void keep_anchor(struct pv_app *app, size_t page, double fraction);
static void handle_key(struct pv_app *app, const struct pv_event *event);
static void handle_button(struct pv_app *app, const struct pv_event *event);
static void handle_motion(struct pv_app *app, const struct pv_event *event);
static void handle_axis(struct pv_app *app, const struct pv_event *event);
static void chooser_key(struct pv_app *app, uint32_t key);
static void chooser_click(struct pv_app *app, int x, int y);
static void chooser_choose(struct pv_app *app);
static void open_chooser(struct pv_app *app);
static const char *reason_of(int error);
static double page_mode_top(const struct pv_app *app);

/*
 * Starts the viewer with no document, at a window size.
 */
void
pv_app_init(
	struct pv_app *app,
	struct pv_text *text,
	int width,
	int height)
{
	/* Nothing open, the scroll mode fitting the width. */
	memset(app, 0, sizeof(*app));
	app->text = text;
	app->mode = PV_MODE_SCROLL;
	app->fit = PV_FIT_WIDTH;
	app->zoom = 1.0;
	app->width = width;
	app->height = height;
	app->now = pv_clock();
	app->dirty = 1;
}

/*
 * Frees the document and the chooser.
 */
void
pv_app_release(
	struct pv_app *app)
{
	/* Closes what is open. */
	pv_app_close_document(app);
	pv_chooser_close(&app->chooser);
}

/*
 * Opens a PDF file in place of the one shown; a file that cannot be opened
 * leaves a message and no document.
 *
 * Returns 0, or an errno value.
 */
int
pv_app_open(
	struct pv_app *app,
	const char *path)
{
	char message[sizeof(app->message)];
	const char *name;
	const char *reason;
	int encrypted;
	int checked;
	int error;

	/* Closes the document shown. */
	pv_app_close_document(app);

	/* Opens the new one. */
	error = pv_document_open(&app->document, path);
	if (error != 0) {
		name = strrchr(path, '/');
		if (name == NULL) {
			name = path;
		} else {
			name++;
		}

		/* Why, in words; an encrypted document refused with EACCES needs a password. */
		reason = reason_of(error);
		if (error == PDF_EPASSWORD) {
			encrypted = 0;
			checked = pdf_document_encrypted(path, &encrypted);
			if (checked == 0 && encrypted)
				reason = "it is protected by a password";
		}

		/* Tells it, and logs it for the tests. */
		snprintf(message, sizeof(message), "Cannot open %s: %s.", name, reason);
		pv_app_message(app, message, VIEW_MESSAGE_MS * 2U);
		pv_log("OPEN failed path=%s error=%d", path, error);
		return error;
	}

	/* Starts at its first page, fitting the mode. */
	app->has_document = 1;
	app->opened = 1;
	app->page = 0;
	app->scroll_x = 0.0;
	app->scroll_y = 0.0;
	app->swipe = 0.0;
	app->turning = 0;
	app->fit = PV_FIT_WIDTH;
	if (app->mode == PV_MODE_PAGE)
		app->fit = PV_FIT_PAGE;
	app->message[0] = '\0';
	app->indicator_until = app->now + VIEW_INDICATOR_MS;
	clamp_view(app);
	app->dirty = 1;

	/* Succeeded: the document is shown. */
	pv_log("OPEN path=%s pages=%lu", path, (unsigned long)app->document.count);
	return 0;
}

/*
 * Closes the document shown, if any.
 */
void
pv_app_close_document(
	struct pv_app *app)
{
	/* Nothing to close without a document. */
	if (!app->has_document)
		return;

	/* Closes it and forgets the view. */
	pv_document_close(&app->document);
	app->has_document = 0;
	app->page = 0;
	app->scroll_x = 0.0;
	app->scroll_y = 0.0;
	app->swipe = 0.0;
	app->turning = 0;
	app->pressed = 0;
	app->dragging = 0;
	app->dirty = 1;
}

/*
 * Takes a new window size, keeping the page in view.
 */
void
pv_app_resize(
	struct pv_app *app,
	int width,
	int height)
{
	size_t page;

	/* Remembers the page before the layout changes. */
	page = current_page(app);

	/* The new size, and the same page at its top. */
	app->width = width;
	app->height = height;
	if (app->mode == PV_MODE_SCROLL && app->has_document)
		app->scroll_y = pv_app_page_top(app, page) - PV_MARGIN;
	clamp_view(app);
	app->dirty = 1;
}

/*
 * Handles one input from the window.
 */
void
pv_app_event(
	struct pv_app *app,
	const struct pv_event *event)
{
	/* Handles it by its kind. */
	switch (event->type) {
	case PV_EVENT_KEY:
		if (event->pressed)
			handle_key(app, event);
		break;
	case PV_EVENT_BUTTON:
		handle_button(app, event);
		break;
	case PV_EVENT_MOTION:
		handle_motion(app, event);
		break;
	case PV_EVENT_AXIS:
		handle_axis(app, event);
		break;
	case PV_EVENT_LEAVE:
		break;
	case PV_EVENT_ACTION:
		pv_app_action(app, (enum pv_action)event->action);
		break;
	}
}

/*
 * Carries out an action of the menus, the titlebar or the keys.
 */
void
pv_app_action(
	struct pv_app *app,
	enum pv_action action)
{
	size_t page;

	/* The page the view is on, which most actions keep. */
	page = current_page(app);
	pv_log("ACTION %d page=%lu", (int)action, (unsigned long)page);

	/* Carries out the action. */
	switch (action) {
	case PV_ACTION_OPEN:
		open_chooser(app);
		break;
	case PV_ACTION_CLOSE:
		/* Closes the document, or the window when none is open. */
		if (app->has_document) {
			pv_app_close_document(app);
		} else {
			app->want_close = 1;
		}
		break;
	case PV_ACTION_QUIT:
		app->want_close = 1;
		break;
	case PV_ACTION_ANNOTATE:
		if (app->has_document)
			app->want_annotate = 1;
		break;
	case PV_ACTION_MODE_SCROLL:
		app->mode = PV_MODE_SCROLL;
		if (app->fit == PV_FIT_PAGE)
			app->fit = PV_FIT_WIDTH;
		app->swipe = 0.0;
		app->turning = 0;
		show_page(app, page);
		break;
	case PV_ACTION_MODE_PAGE:
		app->mode = PV_MODE_PAGE;
		if (app->fit == PV_FIT_WIDTH)
			app->fit = PV_FIT_PAGE;
		show_page(app, page);
		break;
	case PV_ACTION_FIT_WIDTH:
		app->fit = PV_FIT_WIDTH;
		show_page(app, page);
		break;
	case PV_ACTION_FIT_PAGE:
		app->fit = PV_FIT_PAGE;
		show_page(app, page);
		break;
	case PV_ACTION_ZOOM_IN:
		zoom_by(app, VIEW_ZOOM_STEP);
		break;
	case PV_ACTION_ZOOM_OUT:
		zoom_by(app, 1.0 / VIEW_ZOOM_STEP);
		break;
	case PV_ACTION_ZOOM_RESET:
		/* The mode's own fit. */
		app->fit = PV_FIT_WIDTH;
		if (app->mode == PV_MODE_PAGE)
			app->fit = PV_FIT_PAGE;
		show_page(app, page);
		break;
	case PV_ACTION_PREVIOUS:
		if (app->mode == PV_MODE_PAGE) {
			start_turn(app, -1);
		} else if (page > 0) {
			show_page(app, page - 1);
		}
		break;
	case PV_ACTION_NEXT:
		if (app->mode == PV_MODE_PAGE) {
			start_turn(app, 1);
		} else {
			show_page(app, page + 1);
		}
		break;
	case PV_ACTION_FIRST:
		show_page(app, 0);
		break;
	case PV_ACTION_LAST:
		if (app->has_document)
			show_page(app, app->document.count - 1);
		break;
	case PV_ACTION_NONE:
		break;
	}
	app->indicator_until = app->now + VIEW_INDICATOR_MS;
	app->dirty = 1;
}

/*
 * Moves time on: the page turn slides, and the indicator and the message
 * go when their time is up.
 *
 * Returns how many milliseconds until something is due (-1 for nothing).
 */
int
pv_app_tick(
	struct pv_app *app,
	uint64_t now)
{
	double progress;
	double eased;
	int due;

	/* The time of the frame. */
	app->now = now;
	due = -1;

	/* Slides the page turn, easing out, and ends it on the new page. */
	if (app->turning) {
		progress = (double)(now - app->turn_start) / (double)VIEW_TURN_MS;
		if (progress >= 1.0) {
			app->turning = 0;
			app->swipe = 0.0;
			if (app->turn_direction > 0)
				app->page++;
			if (app->turn_direction < 0)
				app->page--;
			app->scroll_y = 0.0;
			app->scroll_x = 0.0;
			clamp_view(app);
			pv_log("PAGE shown=%lu", (unsigned long)app->page);
		} else {
			eased = 1.0 - (1.0 - progress) * (1.0 - progress) * (1.0 - progress);
			app->swipe = app->turn_from + (app->turn_to - app->turn_from) * eased;
			due = 16;
		}
		app->dirty = 1;
	}

	/* The indicator goes when its time is up. */
	if (app->indicator_until != 0 && now >= app->indicator_until) {
		app->indicator_until = 0;
		app->dirty = 1;
	}
	if (app->indicator_until != 0) {
		if (due < 0 || (int)(app->indicator_until - now) < due)
			due = (int)(app->indicator_until - now);
	}

	/* So does the message. */
	if (app->message[0] != '\0' && app->message_until != 0 && now >= app->message_until) {
		app->message[0] = '\0';
		app->dirty = 1;
	}
	if (app->message[0] != '\0' && app->message_until != 0) {
		if (due < 0 || (int)(app->message_until - now) < due)
			due = (int)(app->message_until - now);
	}

	/* Reports the wait. */
	return due;
}

/*
 * Rasterizes one page the view will likely show next, while nothing else
 * is to be done: the pages after and before the page in view (and the
 * second after in the scroll mode).
 *
 * Returns 1 when a page was drawn (more may follow), 0 when all are ready.
 */
int
pv_app_prefetch(
	struct pv_app *app)
{
	const struct pv_page *shown;
	const struct pv_page *candidate;
	size_t candidates[3];
	size_t count;
	size_t index;
	size_t page;
	double scale;
	double difference;
	int error;

	/* Nothing while there is no document, or while the view moves. */
	if (!app->has_document || app->turning || app->pressed)
		return 0;

	/* The pages to have ready: after, then before, the page in view. */
	page = current_page(app);
	count = 0;
	if (page + 1 < app->document.count) {
		candidates[count] = page + 1;
		count++;
	}
	if (page > 0) {
		candidates[count] = page - 1;
		count++;
	}
	if (app->mode == PV_MODE_SCROLL && page + 2 < app->document.count) {
		candidates[count] = page + 2;
		count++;
	}

	/* Draws the first one without a raster at its scale. */
	for (index = 0; index < count; index++) {
		scale = pv_app_scale(app, candidates[index]);
		candidate = &app->document.pages[candidates[index]];
		if (candidate->raster != NULL) {
			difference = candidate->raster_scale - scale;
			if (difference < 1e-6 && difference > -1e-6)
				continue;
		}
		error = pv_document_raster(&app->document, candidates[index], scale, &shown);
		if (error != 0)
			return 0;
		return 1;
	}

	/* Every page near the view is ready. */
	return 0;
}

/*
 * Reports the page the view is on (0 without a document).
 */
size_t
pv_app_current_page(
	const struct pv_app *app)
{
	size_t page;

	/* The page mode's page, or the scroll mode's across the middle. */
	page = current_page(app);
	return page;
}

/*
 * Reports the scale a page is shown at, in pixels per point.
 */
double
pv_app_scale(
	const struct pv_app *app,
	size_t index)
{
	const struct pv_page *page;
	double width;
	double height;
	double room_width;
	double room_height;
	double scale;

	/* The user's zoom. */
	if (app->fit == PV_FIT_CUSTOM || !app->has_document)
		return app->zoom;

	/* The page fitted: the scroll mode fits the widest and tallest page, the page mode each page. */
	width = app->document.widest;
	height = app->document.tallest;
	if (app->mode == PV_MODE_PAGE && index < app->document.count) {
		page = &app->document.pages[index];
		width = page->width;
		height = page->height;
	}
	room_width = (double)app->width - 2.0 * PV_MARGIN;
	room_height = (double)app->height - 2.0 * PV_MARGIN;
	if (room_width < 16.0)
		room_width = 16.0;
	if (room_height < 16.0)
		room_height = 16.0;

	/* To the width, or to the whole page. */
	scale = room_width / width;
	if (app->fit == PV_FIT_PAGE && room_height / height < scale)
		scale = room_height / height;

	/* Keeps the scale within the zoom's bounds. */
	if (scale < VIEW_ZOOM_MIN)
		scale = VIEW_ZOOM_MIN;
	if (scale > VIEW_ZOOM_MAX)
		scale = VIEW_ZOOM_MAX;

	/* Reports the scale. */
	return scale;
}

/*
 * Reports how far the page mode's neighbour stands from the page shown,
 * centre to centre, in pixels: half of each page's width and two gaps.
 */
double
pv_app_neighbour_distance(
	const struct pv_app *app,
	size_t neighbour)
{
	double shown;
	double other;

	/* The two pages' widths at their scales. */
	shown = app->document.pages[app->page].width * pv_app_scale(app, app->page);
	other = app->document.pages[neighbour].width * pv_app_scale(app, neighbour);

	/* Reports the distance between their centres. */
	return (shown + other) / 2.0 + 2.0 * PV_GAP;
}

/*
 * Reports where a page's top is in the laid-out document, in pixels (the
 * scroll mode's column; the page mode's page is its only page).
 */
double
pv_app_page_top(
	const struct pv_app *app,
	size_t index)
{
	double top;
	double scale;
	size_t page;

	/* The page mode's one page. */
	if (app->mode == PV_MODE_PAGE)
		return page_mode_top(app);

	/* The pages above it, a gap apart, under the margin. */
	scale = pv_app_scale(app, 0);
	top = PV_MARGIN;
	for (page = 0; page < index && page < app->document.count; page++)
		top += app->document.pages[page].height * scale + PV_GAP;

	/* Reports the top. */
	return top;
}

/*
 * Reports the height of the laid-out document, in pixels.
 */
double
pv_app_content_height(
	const struct pv_app *app)
{
	double height;

	/* Nothing without a document. */
	if (!app->has_document)
		return 0.0;

	/* The page mode's page, with its margins. */
	if (app->mode == PV_MODE_PAGE) {
		height = app->document.pages[app->page].height * pv_app_scale(app, app->page) + 2.0 * PV_MARGIN;
		return height;
	}

	/* The column: the last page's bottom and the margin. */
	height = pv_app_page_top(app, app->document.count - 1);
	height += app->document.pages[app->document.count - 1].height * pv_app_scale(app, 0) + PV_MARGIN;

	/* Reports the height. */
	return height;
}

/*
 * Reports the width of the laid-out document, in pixels.
 */
double
pv_app_content_width(
	const struct pv_app *app)
{
	double width;

	/* Nothing without a document. */
	if (!app->has_document)
		return 0.0;

	/* The page mode's page, or the scroll mode's widest page, with the margins. */
	width = app->document.widest * pv_app_scale(app, 0);
	if (app->mode == PV_MODE_PAGE)
		width = app->document.pages[app->page].width * pv_app_scale(app, app->page);

	/* Reports the width. */
	return width + 2.0 * PV_MARGIN;
}

/*
 * Shows a message over the view for a while (0: until replaced).
 */
void
pv_app_message(
	struct pv_app *app,
	const char *message,
	uint64_t duration)
{
	/* Keeps the text and when it goes. */
	snprintf(app->message, sizeof(app->message), "%s", message);
	app->message_until = 0;
	if (duration != 0)
		app->message_until = app->now + duration;
	app->dirty = 1;
	pv_log("MESSAGE %s", message);
}

/*
 * Places the file chooser's card in the window and reports how many rows
 * it shows.
 */
void
pv_chooser_layout(
	const struct pv_app *app,
	int *x,
	int *y,
	int *width,
	int *height,
	size_t *rows)
{
	/* A card of at most 560 by 480 pixels, in the middle. */
	*width = app->width - 48;
	if (*width > 560)
		*width = 560;
	*height = app->height - 48;
	if (*height > 480)
		*height = 480;
	if (*width < 120)
		*width = 120;
	if (*height < PV_CHOOSER_HEADER + PV_CHOOSER_ROW + 12)
		*height = PV_CHOOSER_HEADER + PV_CHOOSER_ROW + 12;
	*x = (app->width - *width) / 2;
	*y = (app->height - *height) / 2;

	/* The rows under the header. */
	*rows = (size_t)((*height - PV_CHOOSER_HEADER - 12) / PV_CHOOSER_ROW);
}

/*
 * Writes a log line on standard error: PDFVIEWER and the message.  The
 * tests wait for these lines.
 */
void
pv_log(
	const char *format,
	...)
{
	va_list arguments;

	/* The prefix, the message and the end of the line, at once. */
	fputs("PDFVIEWER ", stderr);
	va_start(arguments, format);
	vfprintf(stderr, format, arguments);
	va_end(arguments);
	fputc('\n', stderr);
	fflush(stderr);
}

/*
 * Reports a monotonic time in milliseconds (0 when the clock cannot be read).
 */
uint64_t
pv_clock(void)
{
	struct timespec now;
	int status;

	/* The monotonic clock. */
	status = clock_gettime(CLOCK_MONOTONIC, &now);
	if (status != 0)
		return 0U;

	/* Reports it in milliseconds. */
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Finds the page the view is on: the page mode's page, or the one across the middle of the scroll mode's view. */
static size_t
current_page(
	const struct pv_app *app)
{
	double middle;
	double bottom;
	double scale;
	size_t page;

	/* No document is on page 0. */
	if (!app->has_document)
		return 0;

	/* The page mode's page. */
	if (app->mode == PV_MODE_PAGE)
		return app->page;

	/* The first page whose bottom (with its gap) is below the middle of the view. */
	middle = app->scroll_y + (double)app->height / 2.0;
	scale = pv_app_scale(app, 0);
	bottom = PV_MARGIN;
	for (page = 0; page < app->document.count; page++) {
		bottom += app->document.pages[page].height * scale + PV_GAP;
		if (bottom > middle)
			return page;
	}

	/* Below the last page. */
	return app->document.count - 1;
}

/* Keeps the view within the laid-out document. */
static void
clamp_view(
	struct pv_app *app)
{
	double largest;

	/* The page mode's page stays within the document. */
	if (app->has_document && app->page >= app->document.count)
		app->page = app->document.count - 1;

	/* The top of the view. */
	largest = pv_app_content_height(app) - (double)app->height;
	if (largest < 0.0)
		largest = 0.0;
	if (app->scroll_y > largest)
		app->scroll_y = largest;
	if (app->scroll_y < 0.0)
		app->scroll_y = 0.0;

	/* The left of the view. */
	largest = pv_app_content_width(app) - (double)app->width;
	if (largest < 0.0)
		largest = 0.0;
	if (app->scroll_x > largest)
		app->scroll_x = largest;
	if (app->scroll_x < 0.0)
		app->scroll_x = 0.0;
}

/* Shows a page: the scroll mode scrolls it to the top, the page mode shows it alone. */
static void
show_page(
	struct pv_app *app,
	size_t index)
{
	/* Only a page the document has. */
	if (!app->has_document)
		return;
	if (index >= app->document.count)
		index = app->document.count - 1;

	/* Scrolls or switches to it. */
	if (app->mode == PV_MODE_SCROLL) {
		app->scroll_y = pv_app_page_top(app, index) - PV_MARGIN;
	} else {
		app->page = index;
		app->scroll_y = 0.0;
		app->swipe = 0.0;
		app->turning = 0;
	}
	clamp_view(app);
	app->indicator_until = app->now + VIEW_INDICATOR_MS;
	app->dirty = 1;
	pv_log("PAGE shown=%lu", (unsigned long)index);
}

/*
 * Starts turning the page mode's page: +1 slides the next page in from the
 * right, -1 the previous one from the left, and 0 slides a dragged page
 * back; a turn past the first or the last page slides back.
 */
static void
start_turn(
	struct pv_app *app,
	int direction)
{
	double distance;

	/* Nothing turns without a document. */
	if (!app->has_document)
		return;

	/* A turn past either end slides back. */
	if (direction < 0 && app->page == 0)
		direction = 0;
	if (direction > 0 && app->page + 1 >= app->document.count)
		direction = 0;

	/* Slides from where the page is to where its neighbour stands, or back to rest. */
	distance = 0.0;
	if (direction > 0)
		distance = pv_app_neighbour_distance(app, app->page + 1);
	if (direction < 0)
		distance = pv_app_neighbour_distance(app, app->page - 1);
	app->turning = 1;
	app->turn_from = app->swipe;
	app->turn_to = -(double)direction * distance;
	app->turn_direction = direction;
	app->turn_start = app->now;
	app->indicator_until = app->now + VIEW_INDICATOR_MS;
	app->dirty = 1;
	pv_log("TURN direction=%d from=%.0f", direction, app->turn_from);
}

/* Zooms by a factor, keeping the point in the middle of the view where it is. */
static void
zoom_by(
	struct pv_app *app,
	double factor)
{
	double scale;
	double middle;
	double top;
	double fraction;
	size_t page;

	/* Nothing to zoom without a document. */
	if (!app->has_document)
		return;

	/* Where the middle of the view is: which page, and how far down it. */
	page = current_page(app);
	scale = pv_app_scale(app, page);
	middle = app->scroll_y + (double)app->height / 2.0;
	top = pv_app_page_top(app, page);
	fraction = (middle - top) / (app->document.pages[page].height * scale);

	/* The new zoom, within its bounds. */
	scale *= factor;
	if (scale < VIEW_ZOOM_MIN)
		scale = VIEW_ZOOM_MIN;
	if (scale > VIEW_ZOOM_MAX)
		scale = VIEW_ZOOM_MAX;
	app->zoom = scale;
	app->fit = PV_FIT_CUSTOM;

	/* The same point in the middle again. */
	keep_anchor(app, page, fraction);
	pv_log("ZOOM scale=%.3f", scale);
}

/* Scrolls so that a place (a share of a page's height) is in the middle of the view. */
static void
keep_anchor(
	struct pv_app *app,
	size_t page,
	double fraction)
{
	double scale;
	double top;
	double width;

	/* The page's top and scale in the new layout. */
	scale = pv_app_scale(app, page);
	top = pv_app_page_top(app, page);

	/* The place in the middle, and the view centred across. */
	app->scroll_y = top + fraction * app->document.pages[page].height * scale - (double)app->height / 2.0;
	width = pv_app_content_width(app);
	app->scroll_x = (width - (double)app->width) / 2.0;
	clamp_view(app);
	app->dirty = 1;
}

/* Handles a key press. */
static void
handle_key(
	struct pv_app *app,
	const struct pv_event *event)
{
	double page_height;

	/* The chooser takes the keys while it is open. */
	if (app->choosing) {
		chooser_key(app, event->key);
		return;
	}

	/* The shortcuts with Control (the menus choose them first when zdesktop has menus). */
	if ((event->modifiers & PV_MOD_CTRL) != 0) {
		switch (event->key) {
		case PV_KEY_O:
			pv_app_action(app, PV_ACTION_OPEN);
			break;
		case PV_KEY_W:
			pv_app_action(app, PV_ACTION_CLOSE);
			break;
		case PV_KEY_Q:
			pv_app_action(app, PV_ACTION_QUIT);
			break;
		case PV_KEY_E:
			pv_app_action(app, PV_ACTION_ANNOTATE);
			break;
		case PV_KEY_EQUAL:
		case PV_KEY_KP_PLUS:
			pv_app_action(app, PV_ACTION_ZOOM_IN);
			break;
		case PV_KEY_MINUS:
		case PV_KEY_KP_MINUS:
			pv_app_action(app, PV_ACTION_ZOOM_OUT);
			break;
		case PV_KEY_0:
			pv_app_action(app, PV_ACTION_ZOOM_RESET);
			break;
		default:
			break;
		}
		return;
	}

	/* The movement keys, by the mode. */
	page_height = (double)app->height - VIEW_KEY_STEP;
	switch (event->key) {
	case PV_KEY_PAGE_UP:
		if (app->mode == PV_MODE_PAGE) {
			pv_app_action(app, PV_ACTION_PREVIOUS);
		} else {
			app->scroll_y -= page_height;
		}
		break;
	case PV_KEY_PAGE_DOWN:
	case PV_KEY_SPACE:
		if (app->mode == PV_MODE_PAGE) {
			pv_app_action(app, PV_ACTION_NEXT);
		} else if ((event->modifiers & PV_MOD_SHIFT) != 0) {
			app->scroll_y -= page_height;
		} else {
			app->scroll_y += page_height;
		}
		break;
	case PV_KEY_LEFT:
		if (app->mode == PV_MODE_PAGE || pv_app_content_width(app) <= (double)app->width) {
			pv_app_action(app, PV_ACTION_PREVIOUS);
		} else {
			app->scroll_x -= VIEW_KEY_STEP;
		}
		break;
	case PV_KEY_RIGHT:
		if (app->mode == PV_MODE_PAGE || pv_app_content_width(app) <= (double)app->width) {
			pv_app_action(app, PV_ACTION_NEXT);
		} else {
			app->scroll_x += VIEW_KEY_STEP;
		}
		break;
	case PV_KEY_UP:
		if (app->mode == PV_MODE_PAGE && pv_app_content_height(app) <= (double)app->height) {
			pv_app_action(app, PV_ACTION_PREVIOUS);
		} else {
			app->scroll_y -= VIEW_KEY_STEP;
		}
		break;
	case PV_KEY_DOWN:
		if (app->mode == PV_MODE_PAGE && pv_app_content_height(app) <= (double)app->height) {
			pv_app_action(app, PV_ACTION_NEXT);
		} else {
			app->scroll_y += VIEW_KEY_STEP;
		}
		break;
	case PV_KEY_HOME:
		pv_app_action(app, PV_ACTION_FIRST);
		break;
	case PV_KEY_END:
		pv_app_action(app, PV_ACTION_LAST);
		break;
	default:
		return;
	}

	/* The view stays within the document and is drawn again. */
	clamp_view(app);
	app->indicator_until = app->now + VIEW_INDICATOR_MS;
	app->dirty = 1;
}

/* Handles a pointer button: a press starts a drag or a swipe, a release ends it. */
static void
handle_button(
	struct pv_app *app,
	const struct pv_event *event)
{
	double share;
	int direction;

	/* Only the left button. */
	if (event->button != PV_BUTTON_LEFT)
		return;

	/* A press in the chooser chooses; elsewhere it starts a drag. */
	if (event->pressed) {
		if (app->choosing) {
			chooser_click(app, event->x, event->y);
			return;
		}
		app->pressed = 1;
		app->dragging = 0;
		app->press_x = event->x;
		app->press_y = event->y;
		app->last_x = event->x;
		app->last_y = event->y;
		app->last_time = event->time;
		app->velocity_x = 0.0;
		app->press_scroll_x = app->scroll_x;
		app->press_scroll_y = app->scroll_y;
		return;
	}

	/* A release ends the press; a sideways drag of the page mode turns or slides back. */
	if (!app->pressed)
		return;
	app->pressed = 0;
	if (app->mode == PV_MODE_PAGE && app->dragging == 1) {
		share = app->swipe / (double)app->width;
		direction = 0;
		if (share < -VIEW_SWIPE_SHARE || app->velocity_x < -VIEW_SWIPE_SPEED)
			direction = 1;
		if (share > VIEW_SWIPE_SHARE || app->velocity_x > VIEW_SWIPE_SPEED)
			direction = -1;
		pv_log("SWIPE offset=%.0f velocity=%.2f direction=%d", app->swipe, app->velocity_x, direction);
		start_turn(app, direction);
	}
	app->dragging = 0;
}

/*
 * Handles the pointer's motion: a drag scrolls (the scroll mode, or the
 * page mode's page along its height), a sideways drag of the page mode
 * moves the page with the pointer.
 */
static void
handle_motion(
	struct pv_app *app,
	const struct pv_event *event)
{
	int moved_x;
	int moved_y;
	int distance_x;
	int distance_y;
	uint64_t elapsed;

	/* Only a press drags. */
	if (!app->pressed || app->turning)
		return;
	moved_x = event->x - app->press_x;
	moved_y = event->y - app->press_y;

	/* The drag starts once the pointer has moved far enough; the page mode chooses sideways or along. */
	distance_x = moved_x;
	if (distance_x < 0)
		distance_x = -distance_x;
	distance_y = moved_y;
	if (distance_y < 0)
		distance_y = -distance_y;
	if (app->dragging == 0) {
		if (distance_x < VIEW_DRAG_START && distance_y < VIEW_DRAG_START)
			return;
		app->dragging = 2;
		if (app->mode == PV_MODE_PAGE && distance_x >= distance_y)
			app->dragging = 1;
	}

	/* The speed across, for the swipe's release, smoothed over the last moves. */
	elapsed = event->time - app->last_time;
	if (elapsed > 0) {
		app->velocity_x = app->velocity_x * 0.4 + 0.6 * (double)(event->x - app->last_x) / (double)elapsed;
	}
	app->last_x = event->x;
	app->last_y = event->y;
	app->last_time = event->time;

	/* A swipe follows the pointer across, resisting past the first and the last page. */
	if (app->dragging == 1) {
		app->swipe = (double)moved_x;
		if (app->page == 0 && app->swipe > 0.0)
			app->swipe /= 3.0;
		if (app->page + 1 >= app->document.count && app->swipe < 0.0)
			app->swipe /= 3.0;
		app->dirty = 1;
		return;
	}

	/* A drag moves the view with the pointer. */
	app->scroll_x = app->press_scroll_x - (double)moved_x;
	app->scroll_y = app->press_scroll_y - (double)moved_y;
	clamp_view(app);
	app->indicator_until = app->now + VIEW_INDICATOR_MS;
	app->dirty = 1;
}

/* Handles the wheel: Control zooms, the scroll mode scrolls, the page mode scrolls a tall page or turns. */
static void
handle_axis(
	struct pv_app *app,
	const struct pv_event *event)
{
	double largest;

	/* The chooser scrolls its list. */
	if (app->choosing) {
		if (event->scroll > 0)
			chooser_key(app, PV_KEY_DOWN);
		if (event->scroll < 0)
			chooser_key(app, PV_KEY_UP);
		return;
	}

	/* Control and the wheel zoom. */
	if ((event->modifiers & PV_MOD_CTRL) != 0) {
		if (event->scroll < 0)
			pv_app_action(app, PV_ACTION_ZOOM_IN);
		if (event->scroll > 0)
			pv_app_action(app, PV_ACTION_ZOOM_OUT);
		return;
	}

	/* The page mode turns at the page's ends. */
	if (app->mode == PV_MODE_PAGE && !app->turning) {
		largest = pv_app_content_height(app) - (double)app->height;
		if (largest < 0.0)
			largest = 0.0;
		if ((event->scroll > 0 && app->scroll_y >= largest) || (event->scroll < 0 && app->scroll_y <= 0.0)) {
			app->wheel += (double)event->scroll;
			if (app->wheel >= VIEW_WHEEL_TURN) {
				app->wheel = 0.0;
				pv_app_action(app, PV_ACTION_NEXT);
			} else if (app->wheel <= -VIEW_WHEEL_TURN) {
				app->wheel = 0.0;
				pv_app_action(app, PV_ACTION_PREVIOUS);
			}
			return;
		}
	}

	/* Scrolls the view. */
	app->wheel = 0.0;
	app->scroll_y += (double)event->scroll;
	clamp_view(app);
	app->indicator_until = app->now + VIEW_INDICATOR_MS;
	app->dirty = 1;
}

/* Handles a key in the file chooser: moving, choosing, going up a folder, closing. */
static void
chooser_key(
	struct pv_app *app,
	uint32_t key)
{
	struct pv_chooser *chooser;
	int x;
	int y;
	int width;
	int height;
	int differs;
	size_t rows;

	/* Moves the selection, keeping it in view. */
	chooser = &app->chooser;
	pv_chooser_layout(app, &x, &y, &width, &height, &rows);
	switch (key) {
	case PV_KEY_UP:
		if (chooser->selected > 0)
			chooser->selected--;
		break;
	case PV_KEY_DOWN:
		if (chooser->selected + 1 < chooser->count)
			chooser->selected++;
		break;
	case PV_KEY_ENTER:
		chooser_choose(app);
		return;
	case PV_KEY_BACKSPACE:
		/* The parent folder is the first entry (the root has none). */
		if (chooser->count == 0)
			return;
		differs = strcmp(chooser->entries[0].name, "..");
		if (differs != 0)
			return;
		chooser->selected = 0;
		chooser_choose(app);
		return;
	case PV_KEY_ESCAPE:
		app->choosing = 0;
		pv_log("CHOOSER closed");
		break;
	default:
		break;
	}

	/* The selection stays among the rows shown. */
	if (chooser->selected < chooser->first)
		chooser->first = chooser->selected;
	if (rows > 0 && chooser->selected >= chooser->first + rows)
		chooser->first = chooser->selected - rows + 1;
	app->dirty = 1;
}

/* Handles a click in the file chooser: an entry is chosen, a click outside the card closes it. */
static void
chooser_click(
	struct pv_app *app,
	int click_x,
	int click_y)
{
	int x;
	int y;
	int width;
	int height;
	size_t rows;
	size_t row;

	/* A click outside the card closes the chooser. */
	pv_chooser_layout(app, &x, &y, &width, &height, &rows);
	if (click_x < x || click_x >= x + width || click_y < y || click_y >= y + height) {
		app->choosing = 0;
		app->dirty = 1;
		pv_log("CHOOSER closed");
		return;
	}

	/* A click on a row chooses its entry. */
	if (click_y < y + PV_CHOOSER_HEADER)
		return;
	row = (size_t)((click_y - y - PV_CHOOSER_HEADER) / PV_CHOOSER_ROW);
	if (row >= rows || app->chooser.first + row >= app->chooser.count)
		return;
	app->chooser.selected = app->chooser.first + row;
	chooser_choose(app);
}

/* Chooses the selected entry: a folder is shown, a file is opened. */
static void
chooser_choose(
	struct pv_app *app)
{
	char path[PV_PATH_MAX];
	const struct pv_entry *entry;
	int error;

	/* Nothing to choose in an empty folder. */
	if (app->chooser.count == 0)
		return;
	entry = &app->chooser.entries[app->chooser.selected];

	/* The entry's path. */
	error = pv_chooser_path(&app->chooser, app->chooser.selected, path, sizeof(path));
	if (error != 0)
		return;

	/* A folder replaces the list. */
	if (entry->folder) {
		error = pv_chooser_open(&app->chooser, path);
		if (error != 0)
			pv_app_message(app, "Cannot read that folder.", VIEW_MESSAGE_MS);
		app->dirty = 1;
		return;
	}

	/* A file is opened and the chooser closes. */
	app->choosing = 0;
	pv_log("CHOOSER chose path=%s", path);
	(void)pv_app_open(app, path);
	app->dirty = 1;
}

/* Opens the file chooser at the open document's folder, or the home folder. */
static void
open_chooser(
	struct pv_app *app)
{
	char folder[PV_PATH_MAX];
	const char *home;
	char *slash;
	int error;

	/* The document's folder, the home folder, or the root. */
	folder[0] = '\0';
	if (app->has_document) {
		snprintf(folder, sizeof(folder), "%s", app->document.path);
		slash = strrchr(folder, '/');
		if (slash != NULL && slash != folder)
			*slash = '\0';
		if (slash == NULL)
			snprintf(folder, sizeof(folder), ".");
	}
	if (folder[0] == '\0') {
		home = getenv("HOME");
		if (home == NULL || home[0] == '\0')
			home = "/";
		snprintf(folder, sizeof(folder), "%s", home);
	}

	/* Lists it; a folder that cannot be read falls back to the root. */
	error = pv_chooser_open(&app->chooser, folder);
	if (error != 0)
		error = pv_chooser_open(&app->chooser, "/");
	if (error != 0) {
		pv_app_message(app, "Cannot list any folder.", VIEW_MESSAGE_MS);
		return;
	}
	app->choosing = 1;
	app->dirty = 1;
	pv_log("CHOOSER open folder=%s entries=%lu", app->chooser.folder, (unsigned long)app->chooser.count);
}

/* Says in words why a file could not be opened. */
static const char *
reason_of(
	int error)
{
	/* The reasons libpdf and the system give. */
	if (error == ENOTSUP)
		return "it uses PDF features this version does not read yet";
	if (error == PDF_EFORMAT)
		return "the file is damaged or is not a PDF";
	if (error == ENOENT)
		return "there is no such file";
	if (error == EACCES)
		return "permission denied";
	if (error == ENOMEM)
		return "not enough memory";
	if (error == EFBIG)
		return "the file is too large";
	if (error == EINVAL)
		return "the document has no pages";

	/* Anything else, by the system's words. */
	return strerror(error);
}

/* Reports the page mode's page top: centred when it is shorter than the window. */
static double
page_mode_top(
	const struct pv_app *app)
{
	double height;
	double top;

	/* The page's height at its scale. */
	if (!app->has_document)
		return PV_MARGIN;
	height = app->document.pages[app->page].height * pv_app_scale(app, app->page);

	/* Centred in the window, or under the margin when taller. */
	top = ((double)app->height - height) / 2.0;
	if (top < PV_MARGIN)
		top = PV_MARGIN;

	/* Reports the top. */
	return top;
}

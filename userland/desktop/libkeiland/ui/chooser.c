/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The file chooser (ws090-p006; libkeiland's of ws092-p003 moved here):
 * the Open and Save As window every application shares.
 *
 * The chooser is a toplevel window of its own on the application's
 * connection (a kl_window made by keiui_window_open_shared), drawn with
 * the widgets on the CPU into wl_shm buffers, opaque on Files' pale
 * ground (no glass shows through it, ws090-p014).  Its objects live on the
 * application's default queue, so the chooser's input, configures and
 * frames run while the application dispatches that queue; the application
 * needs no timer for it.  The window wakes the chooser after the events that queued its
 * input; the chooser draws at most once a frame (a frame callback) and
 * keeps asking for frames while the list glides or a finger is down.
 *
 * The answer is told through the listener from a wl_display.sync callback
 * after the window is gone, so that the application may destroy the
 * chooser from its callback without destroying an object whose event is
 * being dispatched.
 */

#include "chooser.h"
#include "window.h"

#include "userland/desktop/paths.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* The fonts used unless the application names others. */
#define CHOOSER_FONT		KEILAND_DATADIR "/fonts/keiland.ttf"
#define CHOOSER_FALLBACK_FONT	KEILAND_DATADIR "/fonts/keiland-fallback.ttf"

/*
 * One file chooser: the model, the text, the window and its input, the
 * frame being drawn, and who is told the answer.  It lives from
 * kl_file_chooser_open to kl_file_chooser_destroy; the window goes when
 * the answer is known.
 */
struct kl_file_chooser {
	struct keiui_chooser model;
	struct kl_text text;
	int text_open;

	/* The application's connection, the window (NULL once gone) and the input of its frames. */
	struct wl_display *display;
	struct kl_window *window;
	struct kl_ui *ui;
	/* Its titlebar in the sheet mode, hung under the parent's title bar (ws090-p014; NULL for a window of its own). */
	struct kl_titlebar *sheet;

	/* The frame: its pixels and size, the canvas over them, the style, the frame asked for, and whether another is due. */
	uint32_t *pixels;
	uint32_t width;
	uint32_t height;
	struct kl_canvas canvas;
	int canvas_made;
	struct kl_style style;
	struct wl_callback *frame;
	int dirty;

	/* The answer's delivery, and who hears it. */
	struct wl_callback *telling;
	const struct kl_file_chooser_listener *listener;
	void *data;
};

static void chooser_woken(void *data);
static void chooser_sheet(struct kl_file_chooser *chooser);
static void chooser_event(struct kl_file_chooser *chooser, const struct kl_window_event *event);
static void chooser_draw(struct kl_file_chooser *chooser);
static int chooser_canvas(struct kl_file_chooser *chooser);
static void chooser_answered(struct kl_file_chooser *chooser);
static void chooser_window_gone(struct kl_file_chooser *chooser);
static void chooser_frame_done(void *data, struct wl_callback *callback, uint32_t time);
static void chooser_tell_done(void *data, struct wl_callback *callback, uint32_t time);

/* The frame asked for is due. */
static const struct wl_callback_listener chooser_frame_listener = {
	chooser_frame_done
};

/* The answer can be told. */
static const struct wl_callback_listener chooser_tell_listener = {
	chooser_tell_done
};

/*
 * Opens a file chooser over an application's window.
 */
struct kl_file_chooser *
kl_file_chooser_open(
	struct wl_display *display,
	struct xdg_toplevel *parent,
	const struct kl_file_chooser_options *options,
	const struct kl_file_chooser_listener *listener,
	void *data)
{
	struct kl_window_options window_options;
	struct kl_file_chooser *chooser;
	const char *font;
	const char *fallback;
	int error;

	/* A display and someone to tell the answer. */
	if (display == NULL || listener == NULL || listener->done == NULL) {
		errno = EINVAL;
		return NULL;
	}

	/* The record. */
	chooser = calloc(1, sizeof(*chooser));
	if (chooser == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	/* The connection and who hears the answer. */
	chooser->display = display;
	chooser->listener = listener;
	chooser->data = data;

	/* The model, which checks the options and lists the first folder. */
	error = keiui_chooser_init(&chooser->model, options);
	if (error != 0) {
		kl_file_chooser_destroy(chooser);
		errno = error;
		return NULL;
	}

	/* The fonts, the application's or the system's. */
	font = CHOOSER_FONT;
	if (options->font != NULL)
		font = options->font;
	fallback = CHOOSER_FALLBACK_FONT;
	if (options->fallback_font != NULL)
		fallback = options->fallback_font;
	error = kl_text_open(&chooser->text, font, fallback);
	if (error != 0) {
		kl_file_chooser_destroy(chooser);
		errno = error;
		return NULL;
	}

	/* Opened. */
	chooser->text_open = 1;

	/* The input of the frames. */
	chooser->ui = kl_ui_create();
	if (chooser->ui == NULL) {
		kl_file_chooser_destroy(chooser);
		errno = ENOMEM;
		return NULL;
	}

	/* The window on the application's connection; its first configure arrives with the application's next dispatch. */
	memset(&window_options, 0, sizeof(window_options));
	window_options.title = chooser->model.title;
	window_options.application = options->application;
	window_options.width = KEIUI_CHOOSER_WIDTH;
	window_options.height = KEIUI_CHOOSER_HEIGHT;
	window_options.present = KL_PRESENT_SHM;
	chooser->window = keiui_window_open_shared(display, parent, &window_options, KEIUI_CHOOSER_MIN_WIDTH, KEIUI_CHOOSER_MIN_HEIGHT);
	if (chooser->window == NULL) {
		error = errno;
		kl_file_chooser_destroy(chooser);
		errno = error;
		return NULL;
	}

	/* It wakes the chooser for its input. */
	keiui_window_set_notify(chooser->window, chooser_woken, chooser);

	/* Over a parent it is a sheet under the parent's title bar, where zdesktop has sheets; otherwise a window of its own (ws090-p014). */
	if (parent != NULL)
		chooser_sheet(chooser);

	/* Opaque, without glass under it: the theme's pale ground and white cards (ws090-p014). */
	chooser->style.text = &chooser->text;
	chooser->style.theme = kl_theme_default();
	chooser->style.glass = 0;

	/* Succeeded: the chooser shows itself once configured. */
	(void)wl_display_flush(display);
	return chooser;
}

/*
 * Closes a chooser; one still open closes without telling.
 */
void
kl_file_chooser_destroy(
	struct kl_file_chooser *chooser)
{
	/* No chooser, nothing to close. */
	if (chooser == NULL)
		return;

	/* The window, and the answer not told any more. */
	chooser_window_gone(chooser);
	if (chooser->telling != NULL)
		wl_callback_destroy(chooser->telling);

	/* The input, the frame, the fonts and the model. */
	kl_ui_destroy(chooser->ui);
	if (chooser->canvas_made)
		kl_canvas_release(&chooser->canvas);
	free(chooser->pixels);
	if (chooser->text_open)
		kl_text_close(&chooser->text);
	keiui_chooser_fini(&chooser->model);
	free(chooser);
}

/* The window's input is queued, its configure came or a buffer came back: carries out the input and draws. */
static void
chooser_woken(
	void *data)
{
	struct kl_file_chooser *chooser;
	struct kl_window_event event;
	int taken;

	/* Each input queued. */
	chooser = data;
	for (;;) {
		taken = kl_window_take(chooser->window, &event);
		if (!taken)
			break;
		chooser_event(chooser, &event);
	}

	/* The close button ends it without a path. */
	if (chooser->model.answered) {
		chooser_answered(chooser);
		return;
	}

	/* A frame now, unless one is asked for already (it draws then). */
	if (chooser->frame == NULL)
		chooser_draw(chooser);
}

/* Gives one input of the window to the widgets. */
static void
chooser_event(
	struct kl_file_chooser *chooser,
	const struct kl_window_event *event)
{
	/* Any input may change the window. */
	chooser->dirty = 1;

	/* What it is. */
	switch (event->kind) {
	case KL_WINDOW_MOTION:
		(void)kl_ui_pointer_motion(chooser->ui, event->x, event->y);
		break;
	case KL_WINDOW_LEAVE:
		(void)kl_ui_pointer_leave(chooser->ui);
		break;
	case KL_WINDOW_BUTTON:
		/* The main button only. */
		(void)kl_ui_pointer_motion(chooser->ui, event->x, event->y);
		if (event->code == KL_BUTTON_LEFT)
			(void)kl_ui_pointer_button(chooser->ui, event->pressed, event->arrival_us);
		break;
	case KL_WINDOW_AXIS:
		(void)kl_ui_wheel(chooser->ui, event->dx, event->dy, event->arrival_us);
		break;
	case KL_WINDOW_KEY:
		(void)kl_ui_key(chooser->ui, event->code, event->pressed, event->modifiers);
		break;
	case KL_WINDOW_TOUCH_DOWN:
		(void)kl_ui_touch_down(chooser->ui, event->id, event->time_us, event->arrival_us, event->x, event->y);
		break;
	case KL_WINDOW_TOUCH_MOTION:
		(void)kl_ui_touch_motion(chooser->ui, event->id, event->time_us, event->arrival_us, event->x, event->y);
		break;
	case KL_WINDOW_TOUCH_UP:
		(void)kl_ui_touch_up(chooser->ui, event->id, event->time_us, event->arrival_us);
		break;
	case KL_WINDOW_TOUCH_CANCEL:
		(void)kl_ui_touch_cancel(chooser->ui, event->arrival_us);
		break;
	case KL_WINDOW_CLOSE:
		keiui_chooser_cancel(&chooser->model);
		break;
	default:
		break;
	}
}

/* Draws a frame when one is due and shows it, asking for the next while something moves. */
static void
chooser_draw(
	struct kl_file_chooser *chooser)
{
	uint32_t width;
	uint32_t height;
	int again;
	int status;

	/* Nothing before the first configure, and nothing once the window is gone. */
	if (chooser->window == NULL || !chooser->window->configured)
		return;

	/* The frame's size: the window's (a new canvas when it changed). */
	(void)kl_window_present_resize(chooser->window, &width, &height);
	if (width != chooser->width || height != chooser->height || !chooser->canvas_made) {
		chooser->width = width;
		chooser->height = height;
		status = chooser_canvas(chooser);
		if (status != 0)
			return;
	}

	/* The frame and its input. */
	again = keiui_chooser_frame(&chooser->model, chooser->ui, &chooser->style, (int)chooser->width, (int)chooser->height, kl_clock_us());
	chooser->dirty = again;

	/* An answer closes the window. */
	if (chooser->model.answered) {
		chooser_answered(chooser);
		return;
	}

	/* The next frame asked for while something moves, and the frame shown. */
	if (again && chooser->frame == NULL) {
		chooser->frame = wl_surface_frame(kl_window_surface(chooser->window));
		if (chooser->frame != NULL)
			(void)wl_callback_add_listener(chooser->frame, &chooser_frame_listener, chooser);
	}

	/* The frame shown (a buffer the compositor still reads: drawn again when it comes back). */
	status = kl_window_present(chooser->window, chooser->pixels, (size_t)chooser->width);
	if (status != 0)
		chooser->dirty = 1;
	(void)wl_display_flush(chooser->display);
}

/* Makes the frame's memory and canvas at its size; nonzero when memory runs out. */
static int
chooser_canvas(
	struct kl_file_chooser *chooser)
{
	uint32_t *pixels;
	size_t count;
	int status;

	/* The frame's memory. */
	count = (size_t)chooser->width * (size_t)chooser->height;
	pixels = malloc(count * sizeof(pixels[0]));
	if (pixels == NULL)
		return -1;

	/* The canvas over it, in place of the old one. */
	if (chooser->canvas_made)
		kl_canvas_release(&chooser->canvas);
	chooser->canvas_made = 0;
	free(chooser->pixels);
	chooser->pixels = pixels;
	status = kl_canvas_init(&chooser->canvas, chooser->pixels, (size_t)chooser->width, (int)chooser->width, (int)chooser->height);
	if (status != 0)
		return -1;
	chooser->canvas_made = 1;
	chooser->style.canvas = &chooser->canvas;

	/* Succeeded. */
	return 0;
}

/* The answer is known: the window goes, and the answer is told after a roundtrip. */
static void
chooser_answered(
	struct kl_file_chooser *chooser)
{
	/* Once. */
	if (chooser->telling != NULL || chooser->window == NULL)
		return;

	/* The window, then a sync whose answer tells. */
	chooser_window_gone(chooser);
	chooser->telling = wl_display_sync(chooser->display);
	if (chooser->telling != NULL)
		(void)wl_callback_add_listener(chooser->telling, &chooser_tell_listener, chooser);
	(void)wl_display_flush(chooser->display);
}

/* Takes the window away (the answer is known, or the chooser is destroyed). */
static void
chooser_window_gone(
	struct kl_file_chooser *chooser)
{
	/* The frame asked for. */
	if (chooser->frame != NULL) {
		wl_callback_destroy(chooser->frame);
		chooser->frame = NULL;
	}

	/* The sheet's titlebar before its window. */
	if (chooser->sheet != NULL) {
		kl_titlebar_destroy(chooser->sheet);
		chooser->sheet = NULL;
	}

	/* The window, with its own shell (zdesktop lets a binding go alone, BUG-112). */
	kl_window_close(chooser->window);
	chooser->window = NULL;
}

/* The frame asked for is due: draws when something changed or moves. */
static void
chooser_frame_done(
	void *data,
	struct wl_callback *callback,
	uint32_t time)
{
	struct kl_file_chooser *chooser;

	/* The callback is used up. */
	(void)time;
	chooser = data;
	wl_callback_destroy(callback);
	chooser->frame = NULL;

	/* The next frame when one is due. */
	if (chooser->dirty)
		chooser_draw(chooser);
}

/* Tells the answer; the application may destroy the chooser from its callback. */
static void
chooser_tell_done(
	void *data,
	struct wl_callback *callback,
	uint32_t time)
{
	struct kl_file_chooser *chooser;

	/* The callback is used up. */
	(void)time;
	chooser = data;
	wl_callback_destroy(callback);
	chooser->telling = NULL;

	/* The answer, last: nothing of the chooser is touched after it. */
	chooser->listener->done(chooser->data, chooser, chooser->model.result, chooser->model.answer, chooser->model.filter);
}

/*
 * Asks zdesktop to hang the chooser under its parent's title bar: its
 * titlebar in the sheet mode (KL_VERSION 20).  A compositor without
 * sheets leaves it a window of its own.
 */
static void
chooser_sheet(
	struct kl_file_chooser *chooser)
{
	int error;

	/* The window's titlebar presentation. */
	chooser->sheet = kl_titlebar_create(chooser->display, chooser->window->toplevel, NULL, NULL);
	if (chooser->sheet == NULL)
		return;

	/* The sheet mode, in one transaction. */
	error = kl_titlebar_begin(chooser->sheet);
	if (error == 0)
		error = kl_titlebar_set_mode(chooser->sheet, KL_TITLEBAR_SHEET);
	if (error == 0)
		error = kl_titlebar_commit(chooser->sheet);

	/* Refused: a window of its own, as before. */
	if (error != 0) {
		kl_titlebar_destroy(chooser->sheet);
		chooser->sheet = NULL;
	}
}

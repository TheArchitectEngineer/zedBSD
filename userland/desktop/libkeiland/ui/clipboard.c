/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The window's clipboard through zdesktop's (ws090-p004, Text Editor's
 * clipboard.c moved here, itself Terminal's; its drag and drop since WS131
 * p018): kl_window_copy makes the application's text the selection (a
 * wl_data_source offering UTF-8 and plain text), and kl_window_paste
 * receives the selection's text through a pipe.  While the window's own
 * text is the selection, a paste takes it directly (asking itself to write
 * into a pipe it reads would wait on itself).  A new selection is told as
 * a KL_WINDOW_SELECTION input.
 *
 * A drag over the window is taken, as a copy, when it has a type the
 * window accepts (kl_window_accept_drops): file names ("text/uri-list")
 * first, then text.  Its enter, leave and drop are the window's inputs;
 * kl_window_take_drop reads what was dropped through a pipe, or the
 * window's own drag's text directly (its source would be asked to send in
 * a dispatch the reader is waiting in).  kl_window_drag_text starts a drag
 * of text out of the window.
 *
 * Without a data device manager (another compositor) the clipboard is the
 * window's own.
 */

#include "window.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The text types the window offers and takes. */
#define CLIPBOARD_TYPE_UTF8	"text/plain;charset=utf-8"
#define CLIPBOARD_TYPE_PLAIN	"text/plain"
#define CLIPBOARD_TYPE_URIS	"text/uri-list"

/* The drag and drop action the window takes and offers: copy. */
#define CLIPBOARD_ACTION_COPY	1U

/* The data device manager version the window uses, and how long a paste waits for the text. */
#define CLIPBOARD_VERSION	3U
#define CLIPBOARD_RECEIVE_MS	2000U

static void clipboard_offer(void *data, struct wl_data_device *device, struct wl_data_offer *offer);
static void clipboard_enter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer);
static void clipboard_leave(void *data, struct wl_data_device *device);
static void clipboard_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void clipboard_drop(void *data, struct wl_data_device *device);
static void clipboard_selection(void *data, struct wl_data_device *device, struct wl_data_offer *offer);
static void clipboard_type(void *data, struct wl_data_offer *offer, const char *mime_type);
static void clipboard_source_actions(void *data, struct wl_data_offer *offer, uint32_t actions);
static void clipboard_action(void *data, struct wl_data_offer *offer, uint32_t action);
static void clipboard_target(void *data, struct wl_data_source *source, const char *mime_type);
static void clipboard_send(void *data, struct wl_data_source *source, const char *mime_type, int32_t fd);
static void clipboard_cancelled(void *data, struct wl_data_source *source);
static void clipboard_dropped(void *data, struct wl_data_source *source);
static void clipboard_finished(void *data, struct wl_data_source *source);
static void clipboard_source_action(void *data, struct wl_data_source *source, uint32_t action);
static size_t clipboard_read(struct kl_window *window, struct wl_data_offer *offer, const char *type, char *text, size_t size);
static void clipboard_drop_input(struct kl_window *window, unsigned kind, unsigned code, double x, double y);

/* The data device's events. */
static const struct wl_data_device_listener device_listener = {
	clipboard_offer,
	clipboard_enter,
	clipboard_leave,
	clipboard_motion,
	clipboard_drop,
	clipboard_selection
};

/* Every offer's events. */
static const struct wl_data_offer_listener offer_listener = {
	clipboard_type,
	clipboard_source_actions,
	clipboard_action
};

/* The window's source's events. */
static const struct wl_data_source_listener source_listener = {
	clipboard_target,
	clipboard_send,
	clipboard_cancelled,
	clipboard_dropped,
	clipboard_finished,
	clipboard_source_action
};

/*
 * Binds the compositor's data device manager (from the registry).
 */
void
keiui_clipboard_bind(
	struct kl_window *window,
	struct wl_registry *registry,
	uint32_t name,
	uint32_t version)
{
	/* Version 3 is enough. */
	if (version > CLIPBOARD_VERSION)
		version = CLIPBOARD_VERSION;
	window->data_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, version);
}

/*
 * Gets the seat's data device, once the globals are bound (without a
 * manager or a seat the clipboard stays the window's own).
 */
void
keiui_clipboard_start(
	struct kl_window *window)
{
	/* Nothing to share through. */
	if (window->data_manager == NULL || window->seat == NULL)
		return;

	/* The device, and its events. */
	window->data_device = wl_data_device_manager_get_data_device(window->data_manager, window->seat);
	if (window->data_device != NULL)
		(void)wl_data_device_add_listener(window->data_device, &device_listener, window);
}

/*
 * Makes a text the selection (Copy, Cut).  The window keeps its own copy,
 * sent from there.
 */
void
kl_window_copy(
	struct kl_window *window,
	const char *text,
	size_t length)
{
	char *copy;

	/* The window's copy of the text; without memory the old one stays. */
	copy = malloc(length + 1U);
	if (copy == NULL)
		return;
	memcpy(copy, text, length);
	free(window->clipboard);
	window->clipboard = copy;
	window->clipboard_length = length;
	if (window->data_device == NULL)
		return;

	/* The last source goes. */
	if (window->data_source != NULL)
		wl_data_source_destroy(window->data_source);

	/* A source with the two text types, as the selection. */
	window->data_source = wl_data_device_manager_create_data_source(window->data_manager);
	if (window->data_source == NULL)
		return;
	(void)wl_data_source_add_listener(window->data_source, &source_listener, window);
	wl_data_source_offer(window->data_source, CLIPBOARD_TYPE_UTF8);
	wl_data_source_offer(window->data_source, CLIPBOARD_TYPE_PLAIN);
	wl_data_device_set_selection(window->data_device, window->data_source, window->serial);
	(void)wl_display_flush(window->display);
}

/*
 * Receives the selection's text into a buffer (Paste): the window's own
 * directly, another client's through a pipe read until its end or
 * CLIPBOARD_RECEIVE_MS.  Returns the bytes (0 for none).
 */
size_t
kl_window_paste(
	struct kl_window *window,
	char *text,
	size_t size)
{
	size_t length;

	/* The window's own text (or the only one, without the compositor's clipboard). */
	if (window->data_device == NULL || window->data_source != NULL) {
		length = window->clipboard_length;
		if (length > size)
			length = size;
		if (length != 0U)
			memcpy(text, window->clipboard, length);
		return length;
	}

	/* No text to receive. */
	if (window->data_offer == NULL || !window->offer_text)
		return 0;

	/* The text, through a pipe. */
	length = clipboard_read(window, window->data_offer, CLIPBOARD_TYPE_UTF8, text, size);

	/* Succeeded: the bytes received. */
	return length;
}

/*
 * Tells whether the selection offers text to paste (the window's own
 * text, or another program's).
 */
int
kl_window_can_paste(
	const struct kl_window *window)
{
	/* The window's own text. */
	if (window->data_source != NULL && window->clipboard_length != 0U)
		return 1;

	/* Another program's offer of text. */
	if (window->data_offer != NULL && window->offer_text)
		return 1;

	/* Nothing to paste. */
	return 0;
}

/*
 * Tells whether the window's own text is a selection (KL_SELECTION_*): a
 * paste then takes it directly.  Without the compositor's selection the
 * window's own is the only one.
 */
int
kl_window_selection_own(
	const struct kl_window *window,
	unsigned which)
{
	/* The primary selection's source. */
	if (which == KL_SELECTION_PRIMARY) {
		if (window->primary_device == NULL || window->primary_source != NULL)
			return 1;
		return 0;
	}

	/* The clipboard's source, while it is the selection (it goes when cancelled). */
	if (window->data_device == NULL || window->data_source != NULL)
		return 1;

	/* Another program's. */
	return 0;
}

/*
 * Takes the drags of some types (KL_DROP_*; 0 refuses every drag).
 * Returns 0, or EINVAL for another type.
 */
int
kl_window_accept_drops(
	struct kl_window *window,
	unsigned types)
{
	/* A window, and only the types known. */
	if (window == NULL || (types & ~(KL_DROP_TEXT | KL_DROP_URIS)) != 0U)
		return EINVAL;

	/* Taken from the next drag that comes over the window. */
	window->drop_types = types;
	return 0;
}

/*
 * Reads what was dropped on the window into a buffer: the file names
 * ("text/uri-list" as it is) or the text, and tells which (KL_DROP_*, 0
 * with nothing).  The drop is then finished as a copy.  Returns the bytes.
 */
size_t
kl_window_take_drop(
	struct kl_window *window,
	char *text,
	size_t size,
	unsigned *type)
{
	size_t length;

	/* Nothing read yet; only a drop waiting, taken once. */
	*type = 0U;
	length = 0;
	if (!window->drop_pending)
		return 0;
	window->drop_pending = 0;
	if (window->drop_offer == NULL)
		return 0;

	/*
	 * The window's own drag's text is taken as it is: reading it through
	 * the pipe would wait for the send the same dispatch has not run yet
	 * (ws035-p093).  Otherwise the file names, or the text.
	 */
	if (window->drag_source != NULL && (window->drop_offered & KL_DROP_TEXT) != 0U) {
		length = window->drag_length;
		if (length > size)
			length = size;
		memcpy(text, window->drag_text, length);
		*type = KL_DROP_TEXT;
	} else if ((window->drop_offered & KL_DROP_URIS) != 0U) {
		length = clipboard_read(window, window->drop_offer, CLIPBOARD_TYPE_URIS, text, size);
		*type = KL_DROP_URIS;
	} else if ((window->drop_offered & KL_DROP_TEXT) != 0U) {
		length = clipboard_read(window, window->drop_offer, CLIPBOARD_TYPE_UTF8, text, size);
		*type = KL_DROP_TEXT;
	}

	/* The drop is finished (as a copy), and its offer goes. */
	wl_data_offer_set_actions(window->drop_offer, CLIPBOARD_ACTION_COPY, CLIPBOARD_ACTION_COPY);
	wl_data_offer_finish(window->drop_offer);
	wl_data_offer_destroy(window->drop_offer);
	window->drop_offer = NULL;
	(void)wl_display_flush(window->display);

	/* Succeeded: what was dropped. */
	return length;
}

/*
 * Starts a drag of text out of the window, from the press with the serial:
 * a source with the two text types that copies.  Returns 0, ENOTSUP
 * without the compositor's drag and drop, EBUSY while a drag goes on, or
 * ENOMEM.
 */
int
kl_window_drag_text(
	struct kl_window *window,
	const char *text,
	size_t length,
	uint32_t serial)
{
	uint32_t version;

	/* Only with the compositor's drag and drop, and one drag at a time. */
	if (window->data_device == NULL)
		return ENOTSUP;
	if (window->drag_source != NULL)
		return EBUSY;

	/* The text the source sends (at most KEIUI_DRAG_TEXT bytes). */
	if (length > sizeof(window->drag_text))
		length = sizeof(window->drag_text);
	memcpy(window->drag_text, text, length);
	window->drag_length = length;

	/* The source, which copies. */
	window->drag_source = wl_data_device_manager_create_data_source(window->data_manager);
	if (window->drag_source == NULL)
		return ENOMEM;
	(void)wl_data_source_add_listener(window->drag_source, &source_listener, window);
	wl_data_source_offer(window->drag_source, CLIPBOARD_TYPE_UTF8);
	wl_data_source_offer(window->drag_source, CLIPBOARD_TYPE_PLAIN);
	version = wl_proxy_get_version((struct wl_proxy *)window->drag_source);
	if (version >= 3U)
		wl_data_source_set_actions(window->drag_source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);

	/* Succeeded: the drag starts from the window. */
	wl_data_device_start_drag(window->data_device, window->drag_source, window->surface, NULL, serial);
	(void)wl_display_flush(window->display);
	return 0;
}

/*
 * Destroys the clipboard's objects (before the seat they belong to).
 */
void
keiui_clipboard_close(
	struct kl_window *window)
{
	/* The offers, the sources, the device and the manager. */
	if (window->drop_offer != NULL)
		wl_data_offer_destroy(window->drop_offer);
	if (window->drag_source != NULL)
		wl_data_source_destroy(window->drag_source);
	window->drop_offer = NULL;
	window->drag_source = NULL;
	if (window->data_offer != NULL)
		wl_data_offer_destroy(window->data_offer);
	if (window->data_source != NULL)
		wl_data_source_destroy(window->data_source);
	if (window->data_device != NULL)
		wl_data_device_release(window->data_device);
	if (window->data_manager != NULL)
		wl_data_device_manager_destroy(window->data_manager);
	window->data_offer = NULL;
	window->data_source = NULL;
	window->data_device = NULL;
	window->data_manager = NULL;
}

/* Takes a new offer: its types are heard next. */
static void
clipboard_offer(
	void *data,
	struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	struct kl_window *window;

	/* The offer being described, with no type yet. */
	(void)device;
	window = data;
	window->pending_text = 0;
	window->pending_uris = 0;
	(void)wl_data_offer_add_listener(offer, &offer_listener, window);
}

/*
 * A drag comes over the window: taken as a copy when it has a type the
 * window accepts (file names first, then text), refused otherwise.
 */
static void
clipboard_enter(
	void *data,
	struct wl_data_device *device,
	uint32_t serial,
	struct wl_surface *surface,
	wl_fixed_t x,
	wl_fixed_t y,
	struct wl_data_offer *offer)
{
	struct kl_window *window;
	unsigned offered;

	/* An offer left from an earlier drag goes. */
	(void)device;
	window = data;
	if (window->drop_offer != NULL && window->drop_offer != offer)
		wl_data_offer_destroy(window->drop_offer);
	window->drop_offer = NULL;
	window->drop_pending = 0;
	if (offer == NULL)
		return;

	/* A drag over another surface of the program, or of nothing the window takes, is refused. */
	offered = 0U;
	if (window->pending_uris)
		offered |= KL_DROP_URIS;
	if (window->pending_text)
		offered |= KL_DROP_TEXT;
	offered &= window->drop_types;
	if (surface != window->surface || offered == 0U) {
		wl_data_offer_accept(offer, serial, NULL);
		wl_data_offer_destroy(offer);
		return;
	}

	/* The drag's offer and place, the type taken, as a copy. */
	window->drop_offer = offer;
	window->drop_serial = serial;
	window->drop_offered = offered;
	window->drop_x = wl_fixed_to_double(x);
	window->drop_y = wl_fixed_to_double(y);
	if ((offered & KL_DROP_URIS) != 0U) {
		wl_data_offer_accept(offer, serial, CLIPBOARD_TYPE_URIS);
	} else {
		wl_data_offer_accept(offer, serial, CLIPBOARD_TYPE_UTF8);
	}
	wl_data_offer_set_actions(offer, CLIPBOARD_ACTION_COPY, CLIPBOARD_ACTION_COPY);

	/* Succeeded: the window hears it came. */
	clipboard_drop_input(window, KL_WINDOW_DROP_ENTER, offered, window->drop_x, window->drop_y);
}

/* The drag left the window: its offer goes (not one that was dropped and waits to be taken). */
static void
clipboard_leave(
	void *data,
	struct wl_data_device *device)
{
	struct kl_window *window;

	/* Nothing over the window, or a drop waiting. */
	(void)device;
	window = data;
	if (window->drop_offer == NULL || window->drop_pending)
		return;

	/* The offer, and the window hears the drag went. */
	wl_data_offer_destroy(window->drop_offer);
	window->drop_offer = NULL;
	clipboard_drop_input(window, KL_WINDOW_DROP_LEAVE, 0U, window->drop_x, window->drop_y);
}

/* A drag moves over the window: its place is kept for the drop. */
static void
clipboard_motion(
	void *data,
	struct wl_data_device *device,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct kl_window *window;

	/* The place, while the window takes the drag. */
	(void)device;
	(void)time;
	window = data;
	if (window->drop_offer == NULL)
		return;
	window->drop_x = wl_fixed_to_double(x);
	window->drop_y = wl_fixed_to_double(y);
}

/* The drag was dropped on the window: it waits for kl_window_take_drop. */
static void
clipboard_drop(
	void *data,
	struct wl_data_device *device)
{
	struct kl_window *window;
	unsigned type;

	/* Only a drag the window took. */
	(void)device;
	window = data;
	if (window->drop_offer == NULL)
		return;

	/* The type it is read as: the file names when it has them. */
	type = KL_DROP_TEXT;
	if ((window->drop_offered & KL_DROP_URIS) != 0U)
		type = KL_DROP_URIS;

	/* Succeeded: it waits, and the window hears it. */
	window->drop_pending = 1;
	clipboard_drop_input(window, KL_WINDOW_DROP, type, window->drop_x, window->drop_y);
}

/* Takes the selection: the offer a paste receives from (the last one goes); the window hears it changed. */
static void
clipboard_selection(
	void *data,
	struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	struct kl_window_event *event;
	struct kl_window *window;

	/* The last offer goes. */
	(void)device;
	window = data;
	if (window->data_offer != NULL && window->data_offer != offer)
		wl_data_offer_destroy(window->data_offer);

	/* The new one, with or without text. */
	window->data_offer = offer;
	window->offer_text = 0;
	if (offer != NULL)
		window->offer_text = window->pending_text;

	/* The window's input. */
	event = keiui_window_push(window, KL_WINDOW_SELECTION);
	if (event != NULL) {
		event->code = KL_SELECTION_CLIPBOARD;
		event->pressed = window->offer_text;
	}
}

/* Notes a type of the offer being described: either text type, or file names. */
static void
clipboard_type(
	void *data,
	struct wl_data_offer *offer,
	const char *mime_type)
{
	struct kl_window *window;
	int utf8;
	int plain;
	int uris;

	/* The UTF-8 text type, or plain text. */
	(void)offer;
	window = data;
	utf8 = strcmp(mime_type, CLIPBOARD_TYPE_UTF8);
	plain = strcmp(mime_type, CLIPBOARD_TYPE_PLAIN);
	if (utf8 == 0 || plain == 0)
		window->pending_text = 1;

	/* File names. */
	uris = strcmp(mime_type, CLIPBOARD_TYPE_URIS);
	if (uris == 0)
		window->pending_uris = 1;
}

/* The actions the drag's source offers: the window copies whatever they are. */
static void
clipboard_source_actions(
	void *data,
	struct wl_data_offer *offer,
	uint32_t actions)
{
	/* Nothing to do. */
	(void)data;
	(void)offer;
	(void)actions;
}

/* The action the compositor chose: always a copy here. */
static void
clipboard_action(
	void *data,
	struct wl_data_offer *offer,
	uint32_t action)
{
	/* Nothing to do. */
	(void)data;
	(void)offer;
	(void)action;
}

/* A drop target took a type of the window's source: nothing to do. */
static void
clipboard_target(
	void *data,
	struct wl_data_source *source,
	const char *mime_type)
{
	/* Nothing to do. */
	(void)data;
	(void)source;
	(void)mime_type;
}

/* Writes the window's text (the drag's, or the copied) into the descriptor another client reads, and closes it. */
static void
clipboard_send(
	void *data,
	struct wl_data_source *source,
	const char *mime_type,
	int32_t fd)
{
	struct kl_window *window;
	const char *text;
	size_t length;
	size_t written;
	ssize_t count;

	/* The drag's text or the copied one, whatever the text type. */
	(void)mime_type;
	window = data;
	text = window->clipboard;
	length = window->clipboard_length;
	if (source == window->drag_source) {
		text = window->drag_text;
		length = window->drag_length;
	}

	/* All of it, written as the reader takes it. */
	written = 0;
	while (written < length) {
		count = write(fd, text + written, length - written);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			break;
		written += (size_t)count;
	}

	/* The end of the text. */
	close(fd);
}

/* A source is not the selection any more, or a drag ended without a drop: the source goes. */
static void
clipboard_cancelled(
	void *data,
	struct wl_data_source *source)
{
	struct kl_window_event *event;
	struct kl_window *window;

	/* The source is destroyed; a paste now takes the other client's selection. */
	window = data;
	wl_data_source_destroy(source);
	if (window->data_source == source)
		window->data_source = NULL;

	/* A drag ends not dropped. */
	if (window->drag_source == source) {
		window->drag_source = NULL;
		event = keiui_window_push(window, KL_WINDOW_DRAG_DONE);
		if (event != NULL)
			event->code = 0U;
	}
}

/* The window's drag was dropped: its end comes with finished. */
static void
clipboard_dropped(
	void *data,
	struct wl_data_source *source)
{
	/* Nothing to do. */
	(void)data;
	(void)source;
}

/* The window's drag ended dropped: its source goes, and the window hears it. */
static void
clipboard_finished(
	void *data,
	struct wl_data_source *source)
{
	struct kl_window_event *event;
	struct kl_window *window;

	/* Only the drag's source. */
	window = data;
	if (window->drag_source != source)
		return;
	wl_data_source_destroy(source);
	window->drag_source = NULL;

	/* The window's input. */
	event = keiui_window_push(window, KL_WINDOW_DRAG_DONE);
	if (event != NULL)
		event->code = 1U;
}

/* The action of the window's drag: always a copy. */
static void
clipboard_source_action(
	void *data,
	struct wl_data_source *source,
	uint32_t action)
{
	/* Nothing to do. */
	(void)data;
	(void)source;
	(void)action;
}

/*
 * Reads a type of an offer through a pipe until its end or
 * CLIPBOARD_RECEIVE_MS.  Returns the bytes read (0 for none).
 */
static size_t
clipboard_read(
	struct kl_window *window,
	struct wl_data_offer *offer,
	const char *type,
	char *text,
	size_t size)
{
	struct pollfd descriptor;
	uint64_t deadline;
	uint64_t now;
	size_t length;
	ssize_t got;
	int pipes[2];
	int error;
	int ready;

	/* The pipe; its writing end goes to the offer's client. */
	error = pipe(pipes);
	if (error != 0)
		return 0;
	wl_data_offer_receive(offer, type, pipes[1]);
	close(pipes[1]);
	(void)wl_display_flush(window->display);

	/* The data, until the writer closes (or the time is up). */
	length = 0;
	deadline = keiui_clock_ms() + CLIPBOARD_RECEIVE_MS;
	while (length < size) {
		/* The time is up. */
		now = keiui_clock_ms();
		if (now >= deadline)
			break;

		/* The pipe becomes readable. */
		descriptor.fd = pipes[0];
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		ready = poll(&descriptor, 1, 100);
		if (ready <= 0)
			continue;

		/* What came; nothing more is the end. */
		got = read(pipes[0], text + length, size - length);
		if (got <= 0)
			break;
		length += (size_t)got;
	}

	/* The reading end goes. */
	close(pipes[0]);

	/* Succeeded: the bytes read. */
	return length;
}

/* Queues a drag's input of the window at a place (surface pixels). */
static void
clipboard_drop_input(
	struct kl_window *window,
	unsigned kind,
	unsigned code,
	double x,
	double y)
{
	struct kl_window_event *event;

	/* The input; a full queue drops it. */
	event = keiui_window_push(window, kind);
	if (event == NULL)
		return;
	event->code = code;
	event->x = x;
	event->y = y;
}

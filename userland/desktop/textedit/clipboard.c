/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Text Editor's clipboard through zdesktop's (Terminal's clipboard.c,
 * without its drag and drop): Copy and Cut make the editor's text the
 * selection (a wl_data_source offering UTF-8 and plain text), and Paste
 * receives the selection's text through a pipe.  While the editor's own
 * text is the selection, a paste takes it directly (asking itself to
 * write into a pipe it reads would wait on itself).
 *
 * Without a data device manager (another compositor) the clipboard is the
 * editor's own.
 */

#include "window.h"

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The text types the editor offers and takes. */
#define CLIPBOARD_TYPE_UTF8	"text/plain;charset=utf-8"
#define CLIPBOARD_TYPE_PLAIN	"text/plain"

/* The data device manager version the editor uses, and how long a paste waits for the text. */
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

/* The editor's source's events. */
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
te_clipboard_bind(
	struct te_window *window,
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
 * manager or a seat the clipboard stays the editor's own).
 */
void
te_clipboard_start(
	struct te_window *window)
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
te_clipboard_set(
	struct te_window *window,
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
	te_log("CLIPBOARD set bytes=%lu", (unsigned long)length);
}

/*
 * Receives the selection's text into a buffer (Paste): the editor's own
 * directly, another client's through a pipe read until its end or
 * CLIPBOARD_RECEIVE_MS.  Returns the bytes (0 for none).
 */
size_t
te_clipboard_receive(
	struct te_window *window,
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

	/* The editor's own text (or the only one, without the compositor's clipboard). */
	if (window->data_device == NULL || window->data_source != NULL) {
		length = window->clipboard_length;
		if (length > size)
			length = size;
		if (length != 0U)
			memcpy(text, window->clipboard, length);
		te_log("CLIPBOARD paste own bytes=%lu", (unsigned long)length);
		return length;
	}

	/* No text to receive. */
	if (window->data_offer == NULL || !window->offer_text)
		return 0;

	/* The pipe; its writing end goes to the offer's client. */
	error = pipe(pipes);
	if (error != 0)
		return 0;
	wl_data_offer_receive(window->data_offer, CLIPBOARD_TYPE_UTF8, pipes[1]);
	close(pipes[1]);
	(void)wl_display_flush(window->display);

	/* The data, until the writer closes (or the time is up). */
	length = 0;
	deadline = te_clock() + CLIPBOARD_RECEIVE_MS;
	while (length < size) {
		/* The time is up. */
		now = te_clock();
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

	/* Succeeded: the bytes received. */
	te_log("CLIPBOARD paste received bytes=%lu", (unsigned long)length);
	return length;
}

/*
 * Destroys the clipboard's objects (before the seat they belong to).
 */
void
te_clipboard_close(
	struct te_window *window)
{
	/* The offer, the source, the device and the manager. */
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
	struct te_window *window;

	/* The offer being described, with no text type yet. */
	(void)device;
	window = data;
	window->pending_text = 0;
	(void)wl_data_offer_add_listener(offer, &offer_listener, window);
}

/* A drag comes over the window: the editor takes no drops (Files' drops come with WS093). */
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
	/* The offer is refused and goes. */
	(void)data;
	(void)device;
	(void)surface;
	(void)x;
	(void)y;
	if (offer == NULL)
		return;
	wl_data_offer_accept(offer, serial, NULL);
	wl_data_offer_destroy(offer);
}

/* Drag and drop is not used. */
static void
clipboard_leave(
	void *data,
	struct wl_data_device *device)
{
	/* Nothing to do. */
	(void)data;
	(void)device;
}

/* Drag and drop is not used. */
static void
clipboard_motion(
	void *data,
	struct wl_data_device *device,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	/* Nothing to do. */
	(void)data;
	(void)device;
	(void)time;
	(void)x;
	(void)y;
}

/* Drag and drop is not used. */
static void
clipboard_drop(
	void *data,
	struct wl_data_device *device)
{
	/* Nothing to do. */
	(void)data;
	(void)device;
}

/* Takes the selection: the offer a paste receives from (the last one goes). */
static void
clipboard_selection(
	void *data,
	struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	struct te_window *window;

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
	te_log("CLIPBOARD selection text=%d", window->offer_text);
}

/* Notes a type of the offer being described: either text type is what the editor takes. */
static void
clipboard_type(
	void *data,
	struct wl_data_offer *offer,
	const char *mime_type)
{
	struct te_window *window;
	int utf8;
	int plain;

	/* The UTF-8 text type, or plain text. */
	(void)offer;
	window = data;
	utf8 = strcmp(mime_type, CLIPBOARD_TYPE_UTF8);
	plain = strcmp(mime_type, CLIPBOARD_TYPE_PLAIN);
	if (utf8 == 0 || plain == 0)
		window->pending_text = 1;
}

/* Drag and drop is not used. */
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

/* Drag and drop is not used. */
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

/* Drag and drop is not used. */
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

/* Writes the editor's copied text into the descriptor another client reads, and closes it. */
static void
clipboard_send(
	void *data,
	struct wl_data_source *source,
	const char *mime_type,
	int32_t fd)
{
	struct te_window *window;
	size_t written;
	ssize_t count;

	/* All of the text, written as the reader takes it. */
	(void)source;
	(void)mime_type;
	window = data;
	written = 0;
	while (written < window->clipboard_length) {
		count = write(fd, window->clipboard + written, window->clipboard_length - written);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			break;
		written += (size_t)count;
	}

	/* The end of the text. */
	close(fd);
	te_log("CLIPBOARD sent bytes=%lu", (unsigned long)written);
}

/* The editor's text is not the selection any more: its source goes. */
static void
clipboard_cancelled(
	void *data,
	struct wl_data_source *source)
{
	struct te_window *window;

	/* The source is destroyed; a paste now takes the other client's selection. */
	window = data;
	wl_data_source_destroy(source);
	if (window->data_source == source)
		window->data_source = NULL;
}

/* Drag and drop is not used. */
static void
clipboard_dropped(
	void *data,
	struct wl_data_source *source)
{
	/* Nothing to do. */
	(void)data;
	(void)source;
}

/* Drag and drop is not used. */
static void
clipboard_finished(
	void *data,
	struct wl_data_source *source)
{
	/* Nothing to do. */
	(void)data;
	(void)source;
}

/* Drag and drop is not used. */
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

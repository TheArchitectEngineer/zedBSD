/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Text Editor's primary selection through zdesktop's (Terminal's
 * primary.c): the text selected becomes the primary selection (a source
 * offering UTF-8 and plain text), and a middle click pastes the primary
 * selection.  While the editor's own text is it, a paste takes it directly
 * (asking itself to write into a pipe it reads would wait on itself).
 *
 * Without the compositor's primary selection manager the primary selection
 * is the editor's own.
 */

#include "window.h"

#include <primary-selection-unstable-v1-client-protocol.h>

#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The text types the editor offers and takes. */
#define PRIMARY_TYPE_UTF8	"text/plain;charset=utf-8"
#define PRIMARY_TYPE_PLAIN	"text/plain"

/* How long a paste waits for the text. */
#define PRIMARY_RECEIVE_MS	2000U

static void primary_offer(void *data, struct zwp_primary_selection_device_v1 *device, struct zwp_primary_selection_offer_v1 *offer);
static void primary_selection(void *data, struct zwp_primary_selection_device_v1 *device, struct zwp_primary_selection_offer_v1 *offer);
static void primary_type(void *data, struct zwp_primary_selection_offer_v1 *offer, const char *mime_type);
static void primary_send(void *data, struct zwp_primary_selection_source_v1 *source, const char *mime_type, int32_t fd);
static void primary_cancelled(void *data, struct zwp_primary_selection_source_v1 *source);

/* The device's events. */
static const struct zwp_primary_selection_device_v1_listener device_listener = {
	primary_offer,
	primary_selection
};

/* Every offer's events. */
static const struct zwp_primary_selection_offer_v1_listener offer_listener = {
	primary_type
};

/* The editor's source's events. */
static const struct zwp_primary_selection_source_v1_listener source_listener = {
	primary_send,
	primary_cancelled
};

/*
 * Binds the compositor's primary selection manager (from the registry).
 */
void
te_primary_bind(
	struct te_window *window,
	struct wl_registry *registry,
	uint32_t name)
{
	/* Version 1 is the only one. */
	window->primary_manager = wl_registry_bind(registry, name, &zwp_primary_selection_device_manager_v1_interface, 1U);
}

/*
 * Gets the seat's primary selection device, once the globals are bound.
 */
void
te_primary_start(
	struct te_window *window)
{
	/* Nothing to share through. */
	if (window->primary_manager == NULL || window->seat == NULL)
		return;

	/* The device, and its events. */
	window->primary_device = zwp_primary_selection_device_manager_v1_get_device(window->primary_manager, window->seat);
	if (window->primary_device != NULL)
		(void)zwp_primary_selection_device_v1_add_listener(window->primary_device, &device_listener, window);
}

/*
 * Makes the selected text the primary selection.  The window keeps its own
 * copy, sent from there.
 */
void
te_primary_set(
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
	free(window->primary_text);
	window->primary_text = copy;
	window->primary_length = length;
	if (window->primary_device == NULL)
		return;

	/* The last source goes. */
	if (window->primary_source != NULL)
		zwp_primary_selection_source_v1_destroy(window->primary_source);

	/* A source with the two text types, as the primary selection. */
	window->primary_source = zwp_primary_selection_device_manager_v1_create_source(window->primary_manager);
	if (window->primary_source == NULL)
		return;
	(void)zwp_primary_selection_source_v1_add_listener(window->primary_source, &source_listener, window);
	zwp_primary_selection_source_v1_offer(window->primary_source, PRIMARY_TYPE_UTF8);
	zwp_primary_selection_source_v1_offer(window->primary_source, PRIMARY_TYPE_PLAIN);
	zwp_primary_selection_device_v1_set_selection(window->primary_device, window->primary_source, window->serial);
	(void)wl_display_flush(window->display);
	te_log("PRIMARY set bytes=%lu", (unsigned long)length);
}

/*
 * Receives the primary selection's text into a buffer (a middle click):
 * the editor's own directly, another client's through a pipe (read until
 * its end or PRIMARY_RECEIVE_MS).  Returns the bytes (0 for none).
 */
size_t
te_primary_receive(
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

	/* The editor's own text (or the only one, without the compositor's). */
	if (window->primary_device == NULL || window->primary_source != NULL) {
		length = window->primary_length;
		if (length > size)
			length = size;
		if (length != 0U)
			memcpy(text, window->primary_text, length);
		te_log("PRIMARY paste own bytes=%lu", (unsigned long)length);
		return length;
	}

	/* No text to receive. */
	if (window->primary_offer == NULL || !window->primary_offer_text)
		return 0;

	/* The pipe; its writing end goes to the offer's client. */
	error = pipe(pipes);
	if (error != 0)
		return 0;
	zwp_primary_selection_offer_v1_receive(window->primary_offer, PRIMARY_TYPE_UTF8, pipes[1]);
	close(pipes[1]);
	(void)wl_display_flush(window->display);

	/* The data, until the writer closes (or the time is up). */
	length = 0;
	deadline = te_clock() + PRIMARY_RECEIVE_MS;
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
	te_log("PRIMARY paste received bytes=%lu", (unsigned long)length);
	return length;
}

/*
 * Destroys the primary selection's objects (before the seat they belong to).
 */
void
te_primary_close(
	struct te_window *window)
{
	/* The offer, the source, the device and the manager. */
	if (window->primary_offer != NULL)
		zwp_primary_selection_offer_v1_destroy(window->primary_offer);
	if (window->primary_source != NULL)
		zwp_primary_selection_source_v1_destroy(window->primary_source);
	if (window->primary_device != NULL)
		zwp_primary_selection_device_v1_destroy(window->primary_device);
	if (window->primary_manager != NULL)
		zwp_primary_selection_device_manager_v1_destroy(window->primary_manager);
	window->primary_offer = NULL;
	window->primary_source = NULL;
	window->primary_device = NULL;
	window->primary_manager = NULL;
}

/* Takes a new offer: its types are heard next. */
static void
primary_offer(
	void *data,
	struct zwp_primary_selection_device_v1 *device,
	struct zwp_primary_selection_offer_v1 *offer)
{
	struct te_window *window;

	/* The offer being described, with no text type yet. */
	(void)device;
	window = data;
	window->primary_pending_text = 0;
	(void)zwp_primary_selection_offer_v1_add_listener(offer, &offer_listener, window);
}

/* The primary selection changed: its offer (the last one described), or none. */
static void
primary_selection(
	void *data,
	struct zwp_primary_selection_device_v1 *device,
	struct zwp_primary_selection_offer_v1 *offer)
{
	struct te_window *window;

	/* The offer before goes. */
	(void)device;
	window = data;
	if (window->primary_offer != NULL && window->primary_offer != offer)
		zwp_primary_selection_offer_v1_destroy(window->primary_offer);

	/* The new one, and whether it has text. */
	window->primary_offer = offer;
	window->primary_offer_text = 0;
	if (offer != NULL)
		window->primary_offer_text = window->primary_pending_text;
	te_log("PRIMARY offer text=%d", window->primary_offer_text);
}

/* Notes an offer's type: text is what the editor takes. */
static void
primary_type(
	void *data,
	struct zwp_primary_selection_offer_v1 *offer,
	const char *mime_type)
{
	struct te_window *window;
	int utf8;
	int plain;

	/* Either text type. */
	(void)offer;
	window = data;
	utf8 = strcmp(mime_type, PRIMARY_TYPE_UTF8);
	plain = strcmp(mime_type, PRIMARY_TYPE_PLAIN);
	if (utf8 == 0 || plain == 0)
		window->primary_pending_text = 1;
}

/* Writes the editor's selected text into another client's descriptor. */
static void
primary_send(
	void *data,
	struct zwp_primary_selection_source_v1 *source,
	const char *mime_type,
	int32_t fd)
{
	struct te_window *window;
	size_t written;
	ssize_t count;

	/* The whole text, whatever the text type. */
	(void)source;
	(void)mime_type;
	window = data;
	written = 0;
	while (written < window->primary_length) {
		count = write(fd, window->primary_text + written, window->primary_length - written);
		if (count < 0 && errno == EINTR)
			continue;
		if (count <= 0)
			break;
		written += (size_t)count;
	}

	/* The reader sees the end. */
	close(fd);
	te_log("PRIMARY send bytes=%lu", (unsigned long)written);
}

/* Another client's text is the primary selection now: the source goes. */
static void
primary_cancelled(
	void *data,
	struct zwp_primary_selection_source_v1 *source)
{
	struct te_window *window;

	/* The source. */
	window = data;
	zwp_primary_selection_source_v1_destroy(source);
	if (window->primary_source == source)
		window->primary_source = NULL;
	te_log("PRIMARY cancelled");
}

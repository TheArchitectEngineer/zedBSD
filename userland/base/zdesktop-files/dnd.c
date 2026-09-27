/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Drag and drop with other windows through zdesktop (ws035-p084): the
 * Wayland side of ui-drag.c.
 *
 * Items dragged out of the window become a wl_data_source offering their
 * file names as "text/uri-list" (file:// URIs, one per line) with the move
 * and copy actions, and zdesktop carries the drag (start_drag, answering
 * the press that started it); the source writes the names to whoever asks,
 * and its end (dropped and finished, or cancelled) is queued for the
 * interface.  A drag coming over the window is queued as the drop events:
 * the interface finds its target and the main loop answers zdesktop
 * (accept the names or not, move preferred); at the drop the names are
 * read from the offer (or taken from the window's own selection when the
 * drag is the window's own: the window cannot write and read them at once)
 * and the offer is finished.
 */

#include "window.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The type that carries file names. */
#define DND_URI_LIST		"text/uri-list"

/* How long a drop's names may take to come, in milliseconds, and the most read. */
#define DND_RECEIVE_MS		3000
#define DND_RECEIVE_MAX		(1024U * 1024U)

/* The version of wl_data_device_manager that has the actions. */
#define DND_ACTIONS_VERSION	3U

static void dnd_device_offer(void *data, struct wl_data_device *device, struct wl_data_offer *offer);
static void dnd_device_enter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer);
static void dnd_device_leave(void *data, struct wl_data_device *device);
static void dnd_device_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y);
static void dnd_device_drop(void *data, struct wl_data_device *device);
static void dnd_device_selection(void *data, struct wl_data_device *device, struct wl_data_offer *offer);
static void dnd_offer_type(void *data, struct wl_data_offer *offer, const char *mime_type);
static void dnd_offer_source_actions(void *data, struct wl_data_offer *offer, uint32_t actions);
static void dnd_offer_action(void *data, struct wl_data_offer *offer, uint32_t action);
static void dnd_source_target(void *data, struct wl_data_source *source, const char *mime_type);
static void dnd_source_send(void *data, struct wl_data_source *source, const char *mime_type, int32_t fd);
static void dnd_source_cancelled(void *data, struct wl_data_source *source);
static void dnd_source_performed(void *data, struct wl_data_source *source);
static void dnd_source_finished(void *data, struct wl_data_source *source);
static void dnd_source_action(void *data, struct wl_data_source *source, uint32_t action);
static void dnd_source_end(struct fm_window *window, int dropped);
static int dnd_uris(char *const *paths, size_t count, char **text, size_t *length);
static int dnd_parse(const char *text, size_t length, char ***paths, size_t *count);
static int dnd_hex(int character);

/* The data device's events. */
static const struct wl_data_device_listener dnd_device_listener = {
	dnd_device_offer, dnd_device_enter, dnd_device_leave, dnd_device_motion, dnd_device_drop, dnd_device_selection
};

/* An offer's events: its types, the source's actions, zdesktop's choice. */
static const struct wl_data_offer_listener dnd_offer_listener = {
	dnd_offer_type, dnd_offer_source_actions, dnd_offer_action
};

/* The window's own drag's events. */
static const struct wl_data_source_listener dnd_source_listener = {
	dnd_source_target, dnd_source_send, dnd_source_cancelled, dnd_source_performed, dnd_source_finished, dnd_source_action
};

/*
 * Makes the seat's data device, when the compositor has a data device
 * manager; without one the window has no drag and drop with others.
 */
void
fm_dnd_open(
	struct fm_window *window)
{
	int status;

	/* Only with a manager and a seat. */
	if (window->data_manager == NULL || window->seat == NULL)
		return;

	/* The device. */
	window->data_device = wl_data_device_manager_get_data_device(window->data_manager, window->seat);
	if (window->data_device == NULL)
		return;

	/* Its events. */
	status = wl_data_device_add_listener(window->data_device, &dnd_device_listener, window);
	if (status != 0) {
		wl_data_device_destroy(window->data_device);
		window->data_device = NULL;
	}
}

/* Destroys drag and drop's objects. */
void
fm_dnd_close(
	struct fm_window *window)
{
	/* A drag of the window's own. */
	if (window->drag_source != NULL)
		wl_data_source_destroy(window->drag_source);
	window->drag_source = NULL;
	free(window->drag_uris);
	window->drag_uris = NULL;

	/* The offers. */
	if (window->drop_offer != NULL)
		wl_data_offer_destroy(window->drop_offer);
	window->drop_offer = NULL;
	if (window->offer_new != NULL)
		wl_data_offer_destroy(window->offer_new);
	window->offer_new = NULL;

	/* The device and the manager. */
	if (window->data_device != NULL)
		wl_data_device_destroy(window->data_device);
	window->data_device = NULL;
	if (window->data_manager != NULL)
		wl_data_device_manager_destroy(window->data_manager);
	window->data_manager = NULL;
}

/*
 * Starts a drag and drop of paths from the window, answering the press
 * that started the drag within it.  Returns 0, or an errno value (the
 * interface then ends its drag).
 */
int
fm_dnd_start(
	struct fm_window *window,
	char *const *paths,
	size_t count)
{
	struct wl_data_source *source;
	uint32_t version;
	int error;

	/* Without a device, or with a drag already, there is none. */
	if (window->data_device == NULL || window->drag_source != NULL)
		return ENOTSUP;

	/* The names it offers. */
	error = dnd_uris(paths, count, &window->drag_uris, &window->drag_uris_length);
	if (error != 0)
		return error;

	/* The source. */
	source = wl_data_device_manager_create_data_source(window->data_manager);
	if (source == NULL) {
		free(window->drag_uris);
		window->drag_uris = NULL;
		return ENOMEM;
	}

	/* Its events, and the one type. */
	(void)wl_data_source_add_listener(source, &dnd_source_listener, window);
	wl_data_source_offer(source, DND_URI_LIST);

	/* Move or copy (version 3). */
	version = wl_proxy_get_version((struct wl_proxy *)source);
	if (version >= DND_ACTIONS_VERSION)
		wl_data_source_set_actions(source, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE);

	/* Succeeded: zdesktop carries it from the window's surface, with its own badge. */
	window->drag_source = source;
	wl_data_device_start_drag(window->data_device, source, window->surface, NULL, window->button_serial);
	(void)wl_display_flush(window->display);
	fm_log("DND start items=%lu bytes=%lu serial=%u", (unsigned long)count, (unsigned long)window->drag_uris_length, window->button_serial);
	return 0;
}

/*
 * Answers zdesktop for the drag over the window: its file names are taken
 * (with move preferred, or copy) or not.
 */
void
fm_dnd_answer(
	struct fm_window *window,
	int accept,
	uint32_t preferred)
{
	uint32_t actions;
	uint32_t taken;
	uint32_t version;

	/* Only a drag over the window. */
	if (window->drop_offer == NULL)
		return;

	/* The type taken, or none. */
	if (accept != 0 && window->drop_files != 0) {
		wl_data_offer_accept(window->drop_offer, window->drop_serial, DND_URI_LIST);
	} else {
		wl_data_offer_accept(window->drop_offer, window->drop_serial, NULL);
	}

	/* The actions taken: move and copy with the one preferred, or none. */
	actions = 0;
	taken = 0;
	if (accept != 0) {
		actions = WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE;
		taken = preferred;
	}

	/* Said to zdesktop (version 3). */
	version = wl_proxy_get_version((struct wl_proxy *)window->drop_offer);
	if (version >= DND_ACTIONS_VERSION)
		wl_data_offer_set_actions(window->drop_offer, actions, taken);
	(void)wl_display_flush(window->display);
}

/*
 * Reads the file names of the drag dropped on the window.  Returns 0 with
 * the paths (fm_paths_free frees them), or an errno value.
 */
int
fm_dnd_receive(
	struct fm_window *window,
	char ***paths,
	size_t *count)
{
	struct pollfd descriptor;
	char *text;
	char *grown;
	size_t length;
	size_t capacity;
	ssize_t got;
	int pipes[2];
	int status;
	int error;

	/* Nothing yet. */
	*paths = NULL;
	*count = 0;
	if (window->drop_offer == NULL || window->drop_files == 0)
		return ENOENT;

	/* The pipe the source writes into. */
	status = pipe(pipes);
	if (status != 0)
		return errno;

	/* The request, sent with the write end, which the window closes (the reader sees the end once the source closes its copy). */
	wl_data_offer_receive(window->drop_offer, DND_URI_LIST, pipes[1]);
	close(pipes[1]);
	(void)wl_display_flush(window->display);

	/* Everything the source writes, up to the end, the limit or the time allowed. */
	text = NULL;
	length = 0;
	capacity = 0;
	error = 0;
	for (;;) {
		/* Waits for more. */
		descriptor.fd = pipes[0];
		descriptor.events = POLLIN;
		descriptor.revents = 0;
		status = poll(&descriptor, 1, DND_RECEIVE_MS);
		if (status <= 0) {
			error = ETIMEDOUT;
			break;
		}

		/* Room for more. */
		if (length + 4096U + 1U > capacity) {
			capacity = length + 4096U + 1U;
			grown = realloc(text, capacity);
			if (grown == NULL) {
				error = ENOMEM;
				break;
			}

			/* The larger buffer. */
			text = grown;
		}

		/* The bytes; the end ends the reading. */
		got = read(pipes[0], text + length, 4096U);
		if (got <= 0)
			break;
		length += (size_t)got;
		if (length > DND_RECEIVE_MAX) {
			error = E2BIG;
			break;
		}
	}

	/* The pipe's read end. */
	close(pipes[0]);

	/* The names from the text. */
	if (error == 0 && text != NULL)
		error = dnd_parse(text, length, paths, count);
	free(text);
	if (error != 0)
		return error;

	/* Succeeded: the paths dropped. */
	fm_log("DND receive bytes=%lu items=%lu", (unsigned long)length, (unsigned long)*count);
	return 0;
}

/* Tells zdesktop that the drop is done, and lets its offer go. */
void
fm_dnd_finish(
	struct fm_window *window)
{
	uint32_t version;

	/* Only a dropped offer. */
	if (window->drop_offer == NULL)
		return;

	/* Finished (version 3), then gone. */
	version = wl_proxy_get_version((struct wl_proxy *)window->drop_offer);
	if (version >= DND_ACTIONS_VERSION)
		wl_data_offer_finish(window->drop_offer);
	wl_data_offer_destroy(window->drop_offer);
	window->drop_offer = NULL;
	(void)wl_display_flush(window->display);
}

/* Notes a new offer introduced by zdesktop; its types follow. */
static void
dnd_device_offer(
	void *data,
	struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	struct fm_window *window;

	/* The last offer introduced, not known to have file names yet. */
	(void)device;
	window = data;
	window->offer_new = offer;
	window->offer_new_files = 0;
	(void)wl_data_offer_add_listener(offer, &dnd_offer_listener, window);
}

/* A drag comes over the window: its offer (none for another's drag without data) and where it is. */
static void
dnd_device_enter(
	void *data,
	struct wl_data_device *device,
	uint32_t serial,
	struct wl_surface *surface,
	wl_fixed_t x,
	wl_fixed_t y,
	struct wl_data_offer *offer)
{
	struct fm_window *window;
	struct fm_event *event;

	/* An offer left from an earlier drag goes. */
	(void)device;
	(void)surface;
	window = data;
	if (window->drop_offer != NULL && window->drop_offer != offer)
		wl_data_offer_destroy(window->drop_offer);

	/* The drag's offer, and whether it has file names. */
	window->drop_offer = offer;
	window->drop_serial = serial;
	window->drop_files = 0;
	if (offer != NULL && offer == window->offer_new)
		window->drop_files = window->offer_new_files;
	if (offer == window->offer_new)
		window->offer_new = NULL;

	/* The interface hears it: whether it is the window's own drag, whether it has names. */
	event = fm_window_push(window, FM_EVENT_DROP_ENTER);
	if (event == NULL)
		return;
	event->x = wl_fixed_to_int(x);
	event->y = wl_fixed_to_int(y);
	event->pressed = 0;
	if (window->drag_source != NULL)
		event->pressed = 1;
	event->focused = window->drop_files;
}

/* The drag left the window: its offer goes. */
static void
dnd_device_leave(
	void *data,
	struct wl_data_device *device)
{
	struct fm_window *window;

	/* The offer. */
	(void)device;
	window = data;
	if (window->drop_offer != NULL)
		wl_data_offer_destroy(window->drop_offer);
	window->drop_offer = NULL;

	/* The interface hears it. */
	(void)fm_window_push(window, FM_EVENT_DROP_LEAVE);
}

/* The drag moves over the window. */
static void
dnd_device_motion(
	void *data,
	struct wl_data_device *device,
	uint32_t time,
	wl_fixed_t x,
	wl_fixed_t y)
{
	struct fm_window *window;
	struct fm_event *event;

	/* The place, for the interface. */
	(void)device;
	(void)time;
	window = data;
	event = fm_window_push(window, FM_EVENT_DROP_MOTION);
	if (event == NULL)
		return;
	event->x = wl_fixed_to_int(x);
	event->y = wl_fixed_to_int(y);
}

/* The drag was dropped on the window (the main loop reads the names and finishes). */
static void
dnd_device_drop(
	void *data,
	struct wl_data_device *device)
{
	struct fm_window *window;

	/* The interface hears it. */
	(void)device;
	window = data;
	(void)fm_window_push(window, FM_EVENT_DROP);
}

/* The clipboard's offer is not used (the file manager keeps its own clipboard): it goes. */
static void
dnd_device_selection(
	void *data,
	struct wl_data_device *device,
	struct wl_data_offer *offer)
{
	struct fm_window *window;

	/* Not the drag's. */
	(void)device;
	window = data;
	if (offer == NULL)
		return;
	if (offer == window->offer_new)
		window->offer_new = NULL;
	wl_data_offer_destroy(offer);
}

/* Notes a type of the last offer introduced: file names or not. */
static void
dnd_offer_type(
	void *data,
	struct wl_data_offer *offer,
	const char *mime_type)
{
	struct fm_window *window;
	int same;

	/* Only the last introduced offer's types matter here. */
	window = data;
	if (offer != window->offer_new)
		return;

	/* The file names' type. */
	same = strcmp(mime_type, DND_URI_LIST);
	if (same == 0)
		window->offer_new_files = 1;
}

/* The source's actions are not needed (zdesktop chooses). */
static void
dnd_offer_source_actions(
	void *data,
	struct wl_data_offer *offer,
	uint32_t actions)
{
	/* Nothing to do. */
	(void)data;
	(void)offer;
	(void)actions;
}

/* zdesktop's choice of action for the drag over the window, for the interface. */
static void
dnd_offer_action(
	void *data,
	struct wl_data_offer *offer,
	uint32_t action)
{
	struct fm_window *window;
	struct fm_event *event;

	/* Only the drag over the window. */
	window = data;
	if (offer != window->drop_offer)
		return;

	/* The interface hears it. */
	event = fm_window_push(window, FM_EVENT_DROP_ACTION);
	if (event != NULL)
		event->action = action;
}

/* The target's accepted type is not needed. */
static void
dnd_source_target(
	void *data,
	struct wl_data_source *source,
	const char *mime_type)
{
	/* Nothing to do. */
	(void)data;
	(void)source;
	(void)mime_type;
}

/* Writes the file names to a target that asks for them, and closes its descriptor. */
static void
dnd_source_send(
	void *data,
	struct wl_data_source *source,
	const char *mime_type,
	int32_t fd)
{
	struct fm_window *window;
	size_t done;
	ssize_t wrote;
	int same;

	/* Only the file names' type is offered. */
	(void)source;
	window = data;
	same = strcmp(mime_type, DND_URI_LIST);
	if (same != 0 || window->drag_uris == NULL) {
		close(fd);
		return;
	}

	/* All of them (a short text; a failure leaves the reader what was written). */
	done = 0;
	while (done < window->drag_uris_length) {
		wrote = write(fd, window->drag_uris + done, window->drag_uris_length - done);
		if (wrote <= 0)
			break;
		done += (size_t)wrote;
	}

	/* The reader sees the end. */
	close(fd);
	fm_log("DND send bytes=%lu", (unsigned long)done);
}

/* The window's drag was cancelled (dropped nowhere, or Esc). */
static void
dnd_source_cancelled(
	void *data,
	struct wl_data_source *source)
{
	/* Over, not dropped. */
	(void)source;
	dnd_source_end(data, 0);
}

/* The window's drag was dropped; its finish comes next. */
static void
dnd_source_performed(
	void *data,
	struct wl_data_source *source)
{
	/* Logged only. */
	(void)data;
	(void)source;
	fm_log("DND dropped");
}

/* The target finished with the window's drag. */
static void
dnd_source_finished(
	void *data,
	struct wl_data_source *source)
{
	/* Over, dropped. */
	(void)source;
	dnd_source_end(data, 1);
}

/* zdesktop's choice of action for the window's drag is logged. */
static void
dnd_source_action(
	void *data,
	struct wl_data_source *source,
	uint32_t action)
{
	/* Logged only (the target carries it out). */
	(void)data;
	(void)source;
	fm_log("DND action=%u", action);
}

/* Ends the window's own drag: the source and its names go, and the interface hears it. */
static void
dnd_source_end(
	struct fm_window *window,
	int dropped)
{
	struct fm_event *event;

	/* The source and its names. */
	if (window->drag_source != NULL)
		wl_data_source_destroy(window->drag_source);
	window->drag_source = NULL;
	free(window->drag_uris);
	window->drag_uris = NULL;
	window->drag_uris_length = 0;
	fm_log("DND end dropped=%d", dropped);

	/* The interface hears it. */
	event = fm_window_push(window, FM_EVENT_DRAG_DONE);
	if (event != NULL)
		event->pressed = dropped;
}

/*
 * Writes paths as a "text/uri-list": a file:// URI a line (CRLF), with the
 * bytes outside the unreserved set and "/" written as %XX.  Returns 0 with
 * the text (the caller frees it), or ENOMEM.
 */
static int
dnd_uris(
	char *const *paths,
	size_t count,
	char **text,
	size_t *length)
{
	static const char hex[] = "0123456789ABCDEF";
	const unsigned char *byte;
	size_t capacity;
	size_t used;
	size_t index;
	char *out;
	int plain;

	/* Room for the worst case: every byte as %XX, the scheme and the line's end. */
	capacity = 1;
	for (index = 0; index < count; index++)
		capacity += strlen(paths[index]) * 3U + 16U;
	out = malloc(capacity);
	if (out == NULL)
		return ENOMEM;

	/* Each path. */
	used = 0;
	for (index = 0; index < count; index++) {
		/* The scheme. */
		memcpy(out + used, "file://", 7U);
		used += 7U;

		/* Each byte, plain or written as %XX. */
		for (byte = (const unsigned char *)paths[index]; *byte != '\0'; byte++) {
			plain = 0;
			if ((*byte >= 'a' && *byte <= 'z') || (*byte >= 'A' && *byte <= 'Z') || (*byte >= '0' && *byte <= '9'))
				plain = 1;
			if (*byte == '/' || *byte == '-' || *byte == '_' || *byte == '.' || *byte == '~')
				plain = 1;
			if (plain) {
				out[used++] = (char)*byte;
			} else {
				out[used++] = '%';
				out[used++] = hex[*byte >> 4];
				out[used++] = hex[*byte & 15U];
			}
		}

		/* The line's end. */
		out[used++] = '\r';
		out[used++] = '\n';
	}

	/* Succeeded: the text. */
	out[used] = '\0';
	*text = out;
	*length = used;
	return 0;
}

/*
 * Reads a "text/uri-list" into paths: each file:// line (comments and other
 * schemes left out), its %XX bytes decoded.  Returns 0 with the paths, or
 * ENOMEM.
 */
static int
dnd_parse(
	const char *text,
	size_t length,
	char ***paths,
	size_t *count)
{
	const char *line;
	const char *end;
	const char *from;
	char **grown;
	char *path;
	size_t used;
	int scheme;
	int high;
	int low;

	/* Each line. */
	line = text;
	while (line < text + length) {
		/* The line's end (LF, with or without CR). */
		end = line;
		while (end < text + length && *end != '\n')
			end++;

		/* Only a file URI with a path ("file://" and an optional host, then "/"). */
		from = NULL;
		scheme = 1;
		if ((size_t)(end - line) > 7U)
			scheme = strncmp(line, "file://", 7U);
		if (scheme == 0) {
			from = line + 7;
			while (from < end && *from != '/')
				from++;
		}

		/* The path, decoded. */
		if (from != NULL && from < end) {
			path = malloc((size_t)(end - from) + 1U);
			if (path == NULL)
				return ENOMEM;
			used = 0;
			while (from < end && *from != '\r') {
				/* A %XX byte's two digits, when there are. */
				high = -1;
				low = -1;
				if (*from == '%' && from + 2 < end) {
					high = dnd_hex(from[1]);
					low = dnd_hex(from[2]);
				}

				/* The byte it stands for, or the character as it is. */
				if (high >= 0 && low >= 0) {
					path[used++] = (char)(high * 16 + low);
					from += 3;
				} else {
					path[used++] = *from;
					from++;
				}
			}

			/* The end of the path. */
			path[used] = '\0';

			/* One more path. */
			grown = realloc(*paths, (*count + 1U) * sizeof(*grown));
			if (grown == NULL) {
				free(path);
				return ENOMEM;
			}

			/* The path joins the others. */
			*paths = grown;
			(*paths)[*count] = path;
			(*count)++;
		}

		/* The next line. */
		line = end + 1;
	}

	/* Succeeded: the paths. */
	return 0;
}

/* Returns a hexadecimal digit's value, or -1. */
static int
dnd_hex(
	int character)
{
	/* The three ranges. */
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	if (character >= 'A' && character <= 'F')
		return character - 'A' + 10;

	/* Not a digit. */
	return -1;
}

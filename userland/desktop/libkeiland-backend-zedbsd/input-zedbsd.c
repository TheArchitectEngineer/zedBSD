/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The input devices on zedBSD (libkeiland-backend since ws131-p007): the
 * evdev nodes opened directly (there is no seat service).
 */
#include "userland/desktop/libkeiland-backend/backend-private.h"
#include "userland/desktop/libkeiland-backend/keiland-backend-evdev.h"
#include <sys/ioctl.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The directory whose eventN nodes are the evdev devices. */
#define INPUT_DIRECTORY "/dev/input"

static int event_node_name(const char *name);
static void refusals_forget(struct kl_backend *backend, uint64_t generation);
static int node_refused(const struct kl_backend *backend, unsigned events_heard, unsigned long node);
static void probe_device(struct kl_backend *backend, const char *path, unsigned long node);
static int open_node(struct kl_backend *backend, unsigned events_heard, const char *path, unsigned long node, int *descriptor, struct kl_backend_input_caps *capabilities);
static int find_known(const struct kl_backend_input_find *find, const char *path);
static int read_capabilities(int descriptor, struct kl_backend_input_caps *capabilities);

/*
 * Opens every evdev pointer and keyboard not already open.
 *
 * A missing directory leaves the seat without devices; it is not an error.
 * The event loop calls this periodically so late devices are picked up.
 * A node whose open failed with the device's own error is passed by until
 * the system's events tell of an input device that came or went
 * (events-zedbsd.c forgets the refusals then).
 */
void
kl_backend_input_scan(
	struct kl_backend *backend)
{
	DIR *directory;
	struct dirent *entry;
	char path[KL_BACKEND_INPUT_PATH_MAX];
	unsigned long node;
	int length;
	int valid;
	int refused;
	int open_already;

	/* The refusals are forgotten once an input device came or went. */
	refusals_forget(backend, backend->input_refused_generation);

	/* Without the directory there is nothing to read. */
	directory = opendir(INPUT_DIRECTORY);
	if (directory == NULL)
		return;

	/* Every eventN entry is a candidate device. */
	while (1) {
		/* The end of the directory ends the scan. */
		entry = readdir(directory);
		if (entry == NULL)
			break;

		/* Only eventN nodes speak evdev. */
		valid = event_node_name(entry->d_name);
		if (!valid)
			continue;

		/* A node that refused to open is not opened again while no device came or went. */
		node = strtoul(entry->d_name + 5, NULL, 10);
		refused = node_refused(backend, backend->events_descriptor >= 0, node);
		if (refused)
			continue;

		/* An overlong name cannot be an ordinary device node. */
		length = snprintf(path, sizeof(path), "%s/%s", INPUT_DIRECTORY, entry->d_name);
		if (length < 0 || (size_t)length >= sizeof(path))
			continue;

		/* A node the compositor already reads is left alone. */
		open_already = 0;
		if (backend->host.input_known != NULL)
			open_already = backend->host.input_known(backend->host.data, path);
		if (open_already)
			continue;

		/* The compositor classifies the node and keeps it if it is a pointer or a keyboard. */
		probe_device(backend, path, node);
	}

	/* The directory stream is no longer needed. */
	closedir(directory);

	/* Succeeded: every present pointer and keyboard is open. */
	return;
}

/*
 * Tells that zedBSD's backend opens the evdev nodes itself (there is no
 * seat service), so a thread of the compositor's may find them.
 */
int
kl_backend_input_find_supported(void)
{
	/* Succeeded: the nodes are opened directly. */
	return 1;
}

/*
 * Copies into a finding what it needs of the backend's state, on the event
 * loop's thread: whether the system's events are heard (without them a
 * refusal is never forgotten, so none is remembered) and how often an
 * input device came or went.
 */
void
kl_backend_input_find_begin(
	struct kl_backend *backend,
	struct kl_backend_input_find *find)
{
	/* Nothing is opened yet. */
	find->count = 0U;

	/* The system's events as the event loop knows them now. */
	find->events_heard = 0U;
	if (backend->events_descriptor >= 0)
		find->events_heard = 1U;
	find->refused_generation = backend->input_refused_generation;
}

/*
 * Opens every evdev node the finding does not name as known, on the
 * thread that runs it, and lists the ones whose capabilities could be
 * read; nothing is offered to the compositor.  A node beyond the list's
 * room is left for the next finding.  Returns 0 (a missing directory is
 * no error).
 */
int
kl_backend_input_find(
	struct kl_backend *backend,
	struct kl_backend_input_find *find)
{
	struct kl_backend_input_node *found;
	DIR *directory;
	struct dirent *entry;
	char path[KL_BACKEND_INPUT_PATH_MAX];
	unsigned long node;
	int length;
	int valid;
	int refused;
	int known;
	int error;

	/* The refusals are forgotten once an input device came or went. */
	refusals_forget(backend, find->refused_generation);

	/* Without the directory there is nothing to open. */
	find->count = 0U;
	directory = opendir(INPUT_DIRECTORY);
	if (directory == NULL)
		return 0;

	/* Every eventN entry is a candidate device, until the list is full. */
	while (find->count < KL_BACKEND_INPUT_FOUND_MAX) {
		/* The end of the directory ends the finding. */
		entry = readdir(directory);
		if (entry == NULL)
			break;

		/* Only eventN nodes speak evdev. */
		valid = event_node_name(entry->d_name);
		if (!valid)
			continue;

		/* A node that refused to open is not opened again while no device came or went. */
		node = strtoul(entry->d_name + 5, NULL, 10);
		refused = node_refused(backend, find->events_heard, node);
		if (refused)
			continue;

		/* An overlong name cannot be an ordinary device node. */
		length = snprintf(path, sizeof(path), "%s/%s", INPUT_DIRECTORY, entry->d_name);
		if (length < 0 || (size_t)length >= sizeof(path))
			continue;

		/* A node the compositor already reads is left alone. */
		known = find_known(find, path);
		if (known)
			continue;

		/* The node and its capabilities, into the list's next place. */
		found = &find->nodes[find->count];
		error = open_node(backend, find->events_heard, path, node, &found->descriptor, &found->caps);
		if (error != 0)
			continue;
		(void)snprintf(found->path, sizeof(found->path), "%s", path);
		find->count++;
	}

	/* The directory stream is no longer needed. */
	closedir(directory);

	/* Succeeded: the list holds the nodes opened. */
	return 0;
}

/*
 * Reads an input axis range.
 *
 * A nonnegative ioctl response is success; failures return an errno value.
 */
int
kl_backend_input_absinfo(
	int descriptor,
	uint32_t axis,
	struct input_absinfo *info)
{
	int error;

	/* Queries the device's metadata. */
	error = ioctl(descriptor, EVIOCGABS(axis), info);
	if (error < 0)
		return errno;

	/* Succeeded: the device supplied the metadata. */
	return 0;
}

/*
 * Reads an input device's name.
 *
 * A nonnegative ioctl response is success; failures return an errno value.
 */
int
kl_backend_input_name(
	int descriptor,
	char *name,
	size_t size)
{
	int error;

	/* Queries the device's metadata. */
	error = ioctl(descriptor, EVIOCGNAME(size), name);
	if (error < 0)
		return errno;

	/* Succeeded: the device supplied the metadata. */
	return 0;
}

/*
 * Reads an input device's identity.
 *
 * A nonnegative ioctl response is success; failures return an errno value.
 */
int
kl_backend_input_id(
	int descriptor,
	struct input_id *id)
{
	int error;

	/* Queries the device's metadata. */
	error = ioctl(descriptor, EVIOCGID, id);
	if (error < 0)
		return errno;

	/* Succeeded: the device supplied the metadata. */
	return 0;
}

/*
 * Reads whole input events without blocking.
 *
 * EOF returns zero; failures preserve read's errno, or use EIO for torn events.
 */
ssize_t
kl_backend_input_read(
	int descriptor,
	struct input_event *events,
	size_t capacity)
{
	ssize_t bytes;

	/* Reads as many whole events as the buffer holds. */
	bytes = read(descriptor, events, capacity * sizeof(events[0]));
	if (bytes < 0)
		return -1;

	/* Reports the end of the device. */
	if (bytes == 0)
		return 0;

	/* Refuses an incomplete evdev event. */
	if (((size_t)bytes % sizeof(events[0])) != 0) {
		errno = EIO;
		return -1;
	}

	/* Succeeded: returns the number of complete events. */
	return bytes / (ssize_t)sizeof(events[0]);
}

/*
 * Closes an input device descriptor.
 *
 * zedBSD does not need a separate seat service to relinquish the node.
 */
void
kl_backend_input_close(
	struct kl_backend *backend,
	int descriptor)
{
	(void)backend;

	/* Releases the node's descriptor. */
	close(descriptor);

	/* Succeeded: the descriptor is no longer owned by the seat. */
	return;
}

/* Reports whether a directory entry is named eventN. */
static int
event_node_name(
	const char *name)
{
	const char *cursor;
	int prefix;

	/* The name starts with "event" and has at least one more character. */
	prefix = strncmp(name, "event", 5);
	if (prefix != 0 || name[5] == '\0')
		return 0;

	/* Everything after the prefix is a decimal digit. */
	for (cursor = name + 5; *cursor != '\0'; cursor++) {
		/* Any other character makes it some other kind of node. */
		if (*cursor < '0' || *cursor > '9')
			return 0;
	}

	/* Succeeded: the entry is an evdev node. */
	return 1;
}

/*
 * Reports whether eventN refused to open since the system's events last
 * told of an input device that came or went.  Without the events nothing
 * would tell when to try again, so no node is passed by.
 */
static int
node_refused(
	const struct kl_backend *backend,
	unsigned events_heard,
	unsigned long node)
{
	uint64_t bit;

	/* Without the system's events, and past the remembered nodes, every node is tried. */
	if (!events_heard)
		return 0;
	if (node >= 64UL * KL_BACKEND_INPUT_REFUSED_WORDS)
		return 0;

	/* The node's bit. */
	bit = (uint64_t)1 << (node % 64UL);
	return (backend->input_refused[node / 64UL] & bit) != 0U;
}

/*
 * Forgets the nodes that refused to open when an input device came or
 * went since they were last forgotten (generation is the count of those
 * events the caller was given).
 */
static void
refusals_forget(
	struct kl_backend *backend,
	uint64_t generation)
{
	/* No device came or went since. */
	if (generation == backend->input_refused_seen)
		return;

	/* Every node is tried again. */
	memset(backend->input_refused, 0, sizeof(backend->input_refused));
	backend->input_refused_seen = generation;
}

/* Opens one node (eventN) and offers it, with its capability bitmaps, to the compositor. */
static void
probe_device(
	struct kl_backend *backend,
	const char *path,
	unsigned long node)
{
	struct kl_backend_input_caps capabilities;
	unsigned events_heard;
	int descriptor;
	int error;
	int kept;

	/* The node and its capabilities. */
	events_heard = 0U;
	if (backend->events_descriptor >= 0)
		events_heard = 1U;
	error = open_node(backend, events_heard, path, node, &descriptor, &capabilities);
	if (error != 0)
		return;

	/* The compositor's classification keeps it, or it is closed here. */
	kept = 0;
	if (backend->host.input_found != NULL)
		kept = backend->host.input_found(backend->host.data, descriptor, path, &capabilities);
	if (!kept)
		close(descriptor);
}

/*
 * Opens one node (eventN) and reads its capability bitmaps.  A failure of
 * the device's own is remembered while the system's events are heard.
 * Returns 0 with the descriptor (the caller's), or an errno value.
 */
static int
open_node(
	struct kl_backend *backend,
	unsigned events_heard,
	const char *path,
	unsigned long node,
	int *descriptor,
	struct kl_backend_input_caps *capabilities)
{
	int opened;
	int error;

	/* Opens a nonblocking descriptor that children cannot inherit. */
	opened = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (opened < 0) {
		/*
		 * The device's own failure is remembered; a node not the
		 * seat's user's yet (EACCES) is tried at every scan.
		 */
		error = errno;
		if (events_heard && (error == EIO || error == ENXIO || error == ENODEV) && node < 64UL * KL_BACKEND_INPUT_REFUSED_WORDS)
			backend->input_refused[node / 64UL] |= (uint64_t)1 << (node % 64UL);
		return error;
	}

	/* Reads the bits that determine the device's role. */
	error = read_capabilities(opened, capabilities);
	if (error != 0) {
		close(opened);
		return error;
	}

	/* Succeeded: the caller owns the open node. */
	*descriptor = opened;
	return 0;
}

/* Tells whether a finding names a path as one the compositor reads already. */
static int
find_known(
	const struct kl_backend_input_find *find,
	const char *path)
{
	unsigned index;
	int same;

	/* Each path the compositor gave. */
	for (index = 0U; index < find->known_count && index < KL_BACKEND_INPUT_KNOWN_MAX; index++) {
		same = strcmp(find->known[index], path);
		if (same == 0)
			return 1;
	}

	/* The node is not read yet. */
	return 0;
}

/* Reads the event, key, relative and absolute capability bitmaps and the properties of one node. */
static int
read_capabilities(
	int descriptor,
	struct kl_backend_input_caps *capabilities)
{
	int error;

	/* Absent bitmaps read as empty. */
	memset(capabilities, 0, sizeof(*capabilities));

	/* The event types the node can produce. */
	error = ioctl(descriptor, EVIOCGBIT(0, sizeof(capabilities->event)), capabilities->event);
	if (error < 0)
		return errno;

	/* The key and button codes the node can produce. */
	error = ioctl(descriptor, EVIOCGBIT(EV_KEY, sizeof(capabilities->key)), capabilities->key);
	if (error < 0)
		return errno;

	/* The relative axes the node can produce. */
	error = ioctl(descriptor, EVIOCGBIT(EV_REL, sizeof(capabilities->relative)), capabilities->relative);
	if (error < 0)
		return errno;

	/* The absolute axes the node can produce. */
	error = ioctl(descriptor, EVIOCGBIT(EV_ABS, sizeof(capabilities->absolute)), capabilities->absolute);
	if (error < 0)
		return errno;

	/* Its properties; a kernel without EVIOCGPROP leaves them empty (ws159-p004). */
	error = ioctl(descriptor, EVIOCGPROP(sizeof(capabilities->properties)), capabilities->properties);
	if (error < 0)
		memset(capabilities->properties, 0, sizeof(capabilities->properties));

	/* Succeeded: the bitmaps describe the node. */
	return 0;
}

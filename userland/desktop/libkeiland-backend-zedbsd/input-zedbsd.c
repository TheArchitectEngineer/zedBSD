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
static int node_refused(const struct kl_backend *backend, unsigned long node);
static void probe_device(struct kl_backend *backend, const char *path, unsigned long node);
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
		refused = node_refused(backend, node);
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
	unsigned long node)
{
	uint64_t bit;

	/* Without the system's events, and past the remembered nodes, every node is tried. */
	if (backend->events_descriptor < 0)
		return 0;
	if (node >= 64UL * KL_BACKEND_INPUT_REFUSED_WORDS)
		return 0;

	/* The node's bit. */
	bit = (uint64_t)1 << (node % 64UL);
	return (backend->input_refused[node / 64UL] & bit) != 0U;
}

/* Opens one node (eventN) and offers it, with its capability bitmaps, to the compositor. */
static void
probe_device(
	struct kl_backend *backend,
	const char *path,
	unsigned long node)
{
	struct kl_backend_input_caps capabilities;
	int descriptor;
	int error;
	int kept;

	/* Opens a nonblocking descriptor that children cannot inherit. */
	descriptor = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (descriptor < 0) {
		/*
		 * The device's own failure is remembered; a node not the
		 * seat's user's yet (EACCES) is tried at every scan.
		 */
		error = errno;
		if ((error == EIO || error == ENXIO || error == ENODEV) && node < 64UL * KL_BACKEND_INPUT_REFUSED_WORDS)
			backend->input_refused[node / 64UL] |= (uint64_t)1 << (node % 64UL);
		return;
	}

	/* Reads the bits that determine the device's role. */
	error = read_capabilities(descriptor, &capabilities);
	if (error != 0) {
		close(descriptor);
		return;
	}

	/* The compositor's classification keeps it, or it is closed here. */
	kept = 0;
	if (backend->host.input_found != NULL)
		kept = backend->host.input_found(backend->host.data, descriptor, path, &capabilities);
	if (!kept)
		close(descriptor);
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

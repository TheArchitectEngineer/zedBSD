/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * libpasskey's operating system layer on zedBSD (ws161-p004; os.h,
 * docs/reference/security-keys.md): the raw HID nodes /dev/input/hidrawN.
 *
 * A node is a security key's when HIDRAW_GET_INFO says FIDO's usage page
 * and the CTAPHID usage.  The reports go through os-posix.c.  A key that
 * numbers its reports is not taken (FIDO keys do not).
 */

#include "os.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uapi/hidraw.h>

/* Where the nodes are, and the start of their names. */
#define OS_DIRECTORY	"/dev/input"
#define OS_PREFIX	"hidraw"

static int os_probe(const char *path, struct pk_os_device *device);

/*
 * Lists the security keys: at most capacity of them in devices, their
 * number in *count.  Returns 0, or an errno value when the nodes cannot
 * be looked at.
 */
int
pk_os_list(
	struct pk_os_device *devices,
	size_t capacity,
	size_t *count)
{
	struct dirent *entry;
	DIR *directory;
	char path[PK_OS_PATH_MAX];
	int compared;
	int written;
	int error;

	/* None yet; the nodes' directory. */
	*count = 0U;
	directory = opendir(OS_DIRECTORY);
	if (directory == NULL)
		return errno;

	/* Each raw node that is a key's, while there is room. */
	for (;;) {
		entry = readdir(directory);
		if (entry == NULL || *count == capacity)
			break;
		compared = strncmp(entry->d_name, OS_PREFIX, sizeof(OS_PREFIX) - 1U);
		if (compared != 0)
			continue;
		written = snprintf(path, sizeof(path), "%s/%s", OS_DIRECTORY, entry->d_name);
		if (written < 0 || (size_t)written >= sizeof(path))
			continue;
		error = os_probe(path, &devices[*count]);
		if (error == 0)
			(*count)++;
	}

	/* Succeeded: the directory closed. */
	(void)closedir(directory);
	return 0;
}

/*
 * Opens a key's node, taking it for this open alone when grab is nonzero,
 * and gives its report functions in *io.  Returns 0, or an errno value.
 */
int
pk_os_open(
	struct pk_os_hid *handle,
	const char *path,
	int grab,
	struct pk_hid_io *io)
{
	struct hidraw_info info;
	int descriptor;
	int on;
	int result;
	int error;

	/* The node, read and written without waiting (poll() waits). */
	handle->descriptor = -1;
	descriptor = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (descriptor < 0)
		return errno;

	/* What it is. */
	result = ioctl(descriptor, HIDRAW_GET_INFO, &info);
	if (result < 0) {
		error = errno;
		(void)close(descriptor);
		return error;
	}

	/* A key's, with reports that are not numbered. */
	if (info.usage_page != HIDRAW_USAGE_PAGE_FIDO || (info.flags & HIDRAW_INFO_NUMBERED) != 0U) {
		(void)close(descriptor);
		return ENODEV;
	}

	/* Taken for this open alone, when asked. */
	if (grab) {
		on = 1;
		result = ioctl(descriptor, HIDRAW_GRAB, &on);
		if (result < 0) {
			error = errno;
			(void)close(descriptor);
			return error;
		}
	}

	/* Succeeded: the report functions on the node. */
	handle->descriptor = descriptor;
	pk_os_posix_io(handle, io);
	return 0;
}

/*
 * Tells whether a node is a key's, filling device when it is.  Returns 0,
 * ENODEV for another device's node, or an errno value.
 */
static int
os_probe(
	const char *path,
	struct pk_os_device *device)
{
	struct hidraw_info info;
	struct hidraw_text name;
	int descriptor;
	int result;
	int error;

	/* The node, only to ask it. */
	descriptor = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (descriptor < 0)
		return errno;

	/* What it is. */
	result = ioctl(descriptor, HIDRAW_GET_INFO, &info);
	if (result < 0) {
		error = errno;
		(void)close(descriptor);
		return error;
	}

	/* FIDO's CTAPHID. */
	if (info.usage_page != HIDRAW_USAGE_PAGE_FIDO || info.usage != HIDRAW_USAGE_CTAPHID) {
		(void)close(descriptor);
		return ENODEV;
	}

	/* Its name (none when the device gave none). */
	memset(&name, 0, sizeof(name));
	result = ioctl(descriptor, HIDRAW_GET_NAME, &name);
	if (result < 0)
		name.value[0] = '\0';
	(void)close(descriptor);

	/* Succeeded: the key. */
	(void)snprintf(device->path, sizeof(device->path), "%s", path);
	(void)snprintf(device->name, sizeof(device->name), "%.*s", (int)(sizeof(name.value) - 1U), name.value);
	device->vendor = info.vendor;
	device->product = info.product;
	return 0;
}

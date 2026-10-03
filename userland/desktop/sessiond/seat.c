/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The seat's devices: the display (/dev/gpu*) and the input devices
 * (/dev/input/event*) belong to the seat's user, 0600, while sessiond runs
 * a greeter or a session, and go back to root when it stops.
 *
 * devfs keeps an owner and a mode given to a name, also for a node made
 * again under that name (a keyboard plugged in again).  A node that appears
 * for the first time comes with devfs's own owner, so the loops that wait
 * for the greeter and the session give the devices again every second.
 *
 * Giving a device away does not take it from a process that opened it
 * before: there is no revoke in the kernel yet, so the graphical login is
 * for a machine with one user (login-manager-design.md §7).
 */

#include "sessiond.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* The display devices sessiond looks for: /dev/gpu0 to /dev/gpu3. */
#define SEAT_GPU_COUNT		4U

/* The input devices' directory. */
#define SEAT_INPUT_DIRECTORY	"/dev/input"

/* devfs's own modes, which the devices go back to. */
#define SEAT_GPU_MODE		0666
#define SEAT_INPUT_MODE		0640

/* The wheel group, which owns the input devices when no one has the seat. */
#define SEAT_WHEEL_GID		0

static void seat_set(const char *path, uid_t uid, gid_t gid, mode_t mode);
static void seat_input(uid_t uid, gid_t gid, mode_t mode);

/*
 * Gives the display and the input devices to a user, for that user only.
 */
void
sessiond_seat_give(
	uid_t uid,
	gid_t gid)
{
	char path[32];
	unsigned index;

	/* Every display. */
	for (index = 0U; index < SEAT_GPU_COUNT; index++) {
		snprintf(path, sizeof(path), "/dev/gpu%u", index);
		seat_set(path, uid, gid, 0600);
	}

	/* Every input device. */
	seat_input(uid, gid, 0600);
}

/*
 * Gives the display and the input devices back to root with devfs's modes.
 */
void
sessiond_seat_restore(
	void)
{
	char path[32];
	unsigned index;

	/* Every display, which anyone may open again. */
	for (index = 0U; index < SEAT_GPU_COUNT; index++) {
		snprintf(path, sizeof(path), "/dev/gpu%u", index);
		seat_set(path, 0, SEAT_WHEEL_GID, SEAT_GPU_MODE);
	}

	/* Every input device, which root and wheel read. */
	seat_input(0, SEAT_WHEEL_GID, SEAT_INPUT_MODE);
}

/* Gives one device node an owner and a mode, when the node is there. */
static void
seat_set(
	const char *path,
	uid_t uid,
	gid_t gid,
	mode_t mode)
{
	struct stat status;
	int error;

	/* A device that is not there is left alone. */
	error = stat(path, &status);
	if (error != 0)
		return;

	/* Nothing to do for a node that already has them (no log line every second). */
	if (status.st_uid == uid && status.st_gid == gid && (status.st_mode & 07777) == mode)
		return;

	/* The owner first, so the new mode never lets the old owner in. */
	error = chown(path, uid, gid);
	if (error != 0) {
		sessiond_log("SESSIOND SEAT chown path=%s failed", path);
		return;
	}

	/* Then the mode. */
	error = chmod(path, mode);
	if (error != 0) {
		sessiond_log("SESSIOND SEAT chmod path=%s failed", path);
		return;
	}

	/* Succeeded: the device is the user's. */
	sessiond_log("SESSIOND SEAT path=%s uid=%u mode=%04o", path, (unsigned)uid, (unsigned)mode);
}

/* Gives every input device an owner and a mode. */
static void
seat_input(
	uid_t uid,
	gid_t gid,
	mode_t mode)
{
	char path[300];
	struct dirent *entry;
	DIR *directory;
	int match;

	/* The input devices there are now. */
	directory = opendir(SEAT_INPUT_DIRECTORY);
	if (directory == NULL)
		return;

	/* Each event device. */
	for (;;) {
		entry = readdir(directory);
		if (entry == NULL)
			break;
		match = strncmp(entry->d_name, "event", 5);
		if (match != 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", SEAT_INPUT_DIRECTORY, entry->d_name);
		seat_set(path, uid, gid, mode);
	}

	/* The listing is done with. */
	(void)closedir(directory);
}

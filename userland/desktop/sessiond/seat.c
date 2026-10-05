/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The seat's devices: the display (/dev/gpu*), the input devices
 * (/dev/input/event*), the panel's light (/dev/backlight/backlight*,
 * ws113-p013), and the security keys' raw HID nodes
 * (/dev/input/hidraw*) and the smart card slots (/dev/smartcard*,
 * ws161-p002, the user's approval U3) belong to the seat's user, 0600,
 * while sessiond runs a greeter or a session, and go back to root when it
 * stops (the keys and the cards to root alone: whoever opens one can ask
 * the key to sign).  The login screen is not given the keys and the cards
 * (ws172: its security key login is passkey's, which opens them as root).
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

/* The backlight devices sessiond looks for: /dev/backlight/backlight0 to backlight3. */
#define SEAT_BACKLIGHT_COUNT	4U

/* The smart card slots sessiond looks for: /dev/smartcard0 to smartcard7. */
#define SEAT_SMARTCARD_COUNT	8U

/* devfs's own modes, which the devices go back to. */
#define SEAT_GPU_MODE		0666
#define SEAT_INPUT_MODE		0640
#define SEAT_BACKLIGHT_MODE	0644
#define SEAT_KEY_MODE		0600

/* The wheel group, which owns the input devices when no one has the seat. */
#define SEAT_WHEEL_GID		0

static void seat_set(const char *path, uid_t uid, gid_t gid, mode_t mode);
static void seat_input(const char *prefix, uid_t uid, gid_t gid, mode_t mode);

/*
 * Gives the display and the input devices to a user, for that user only,
 * and the security keys and the smart card slots too when keys is set (a
 * session's user; not the login screen's account).
 */
void
sessiond_seat_give(
	uid_t uid,
	gid_t gid,
	int keys)
{
	char path[32];
	unsigned index;

	/* Every display. */
	for (index = 0U; index < SEAT_GPU_COUNT; index++) {
		snprintf(path, sizeof(path), "/dev/gpu%u", index);
		seat_set(path, uid, gid, 0600);
	}

	/* Every input device. */
	seat_input("event", uid, gid, 0600);

	/* Every backlight, which the session's compositor sets. */
	for (index = 0U; index < SEAT_BACKLIGHT_COUNT; index++) {
		snprintf(path, sizeof(path), "/dev/backlight/backlight%u", index);
		seat_set(path, uid, gid, 0600);
	}

	/* The login screen's account gets no security key nor smart card slot: they stay root's. */
	if (!keys) {
		seat_input("hidraw", 0, SEAT_WHEEL_GID, SEAT_KEY_MODE);
		for (index = 0U; index < SEAT_SMARTCARD_COUNT; index++) {
			snprintf(path, sizeof(path), "/dev/smartcard%u", index);
			seat_set(path, 0, SEAT_WHEEL_GID, SEAT_KEY_MODE);
		}
		return;
	}

	/* A session's user gets every security key's raw node and every smart card slot. */
	seat_input("hidraw", uid, gid, 0600);
	for (index = 0U; index < SEAT_SMARTCARD_COUNT; index++) {
		snprintf(path, sizeof(path), "/dev/smartcard%u", index);
		seat_set(path, uid, gid, 0600);
	}
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

	/* Every input device, which root and wheel read; every security key's raw node, root's alone. */
	seat_input("event", 0, SEAT_WHEEL_GID, SEAT_INPUT_MODE);
	seat_input("hidraw", 0, SEAT_WHEEL_GID, SEAT_KEY_MODE);

	/* Every backlight, which anyone reads and root sets. */
	for (index = 0U; index < SEAT_BACKLIGHT_COUNT; index++) {
		snprintf(path, sizeof(path), "/dev/backlight/backlight%u", index);
		seat_set(path, 0, SEAT_WHEEL_GID, SEAT_BACKLIGHT_MODE);
	}

	/* Every smart card slot, root's alone. */
	for (index = 0U; index < SEAT_SMARTCARD_COUNT; index++) {
		snprintf(path, sizeof(path), "/dev/smartcard%u", index);
		seat_set(path, 0, SEAT_WHEEL_GID, SEAT_KEY_MODE);
	}
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

/* Gives every node of /dev/input whose name starts with prefix an owner and a mode. */
static void
seat_input(
	const char *prefix,
	uid_t uid,
	gid_t gid,
	mode_t mode)
{
	char path[300];
	struct dirent *entry;
	DIR *directory;
	size_t prefix_length;
	int match;

	/* The input devices there are now. */
	directory = opendir(SEAT_INPUT_DIRECTORY);
	if (directory == NULL)
		return;

	/* Each node of the kind. */
	prefix_length = strlen(prefix);
	for (;;) {
		entry = readdir(directory);
		if (entry == NULL)
			break;
		match = strncmp(entry->d_name, prefix, prefix_length);
		if (match != 0)
			continue;
		snprintf(path, sizeof(path), "%s/%s", SEAT_INPUT_DIRECTORY, entry->d_name);
		seat_set(path, uid, gid, mode);
	}

	/* The listing is done with. */
	(void)closedir(directory);
}

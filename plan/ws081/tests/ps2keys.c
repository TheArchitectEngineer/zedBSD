/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * BUG-191: reads the PS/2 keyboard's input device for a while and counts
 * what it reported for the A key, for the QEMU test of the typematic
 * (ps2-repeat-qemu.sh): a held key's make codes after the first are to
 * come as repeats (value 2), not as new presses.  At the end one line:
 *
 *   PS2KEYS device=PATH press=N repeat=N release=N
 *
 *   ps2keys [--seconds=N]      (default 10)
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <uapi/input.h>

/* The input devices looked at, and the name of the one read. */
#define KEYS_DEVICES	32
#define KEYS_NAME	"PC/AT PS/2 keyboard"

static int keys_find(char *path, size_t size);
static int64_t keys_now_ms(void);

/*
 * Reads the PS/2 keyboard until the time is up and prints the counts.
 */
int
main(
	int argc,
	char **argv)
{
	struct input_event events[64];
	struct pollfd entry;
	char path[64];
	int64_t end;
	int64_t now;
	ssize_t count;
	long seconds;
	long press;
	long repeat;
	long release;
	int descriptor;
	int ready;
	int index;
	int found;

	/* The run's length. */
	seconds = 10;
	for (index = 1; index < argc; index++) {
		found = strncmp(argv[index], "--seconds=", 10);
		if (found == 0) {
			seconds = strtol(argv[index] + 10, NULL, 10);
		} else {
			fprintf(stderr, "usage: ps2keys [--seconds=N]\n");
			return 2;
		}
	}

	/* The PS/2 keyboard's device. */
	found = keys_find(path, sizeof(path));
	if (found != 0) {
		printf("PS2KEYS none\n");
		return 1;
	}

	/* Opened for reading. */
	descriptor = open(path, O_RDONLY);
	if (descriptor < 0) {
		printf("PS2KEYS open errno=%d\n", errno);
		return 1;
	}

	/* Ready for the keys. */
	printf("PS2KEYS ready device=%s\n", path);
	fflush(stdout);

	/* Each event until the time is up. */
	press = 0;
	repeat = 0;
	release = 0;
	end = keys_now_ms() + seconds * 1000;
	for (;;) {
		/* The time left. */
		now = keys_now_ms();
		if (now >= end)
			break;

		/* Waits for events. */
		entry.fd = descriptor;
		entry.events = POLLIN;
		entry.revents = 0;
		ready = poll(&entry, 1, (int)(end - now));
		if (ready <= 0)
			continue;
		count = read(descriptor, events, sizeof(events));
		if (count <= 0)
			break;

		/* The A key's events counted by their value. */
		for (index = 0; index < (int)(count / (ssize_t)sizeof(events[0])); index++) {
			if (events[index].type != EV_KEY || events[index].code != KEY_A)
				continue;
			if (events[index].value == 1)
				press++;
			else if (events[index].value == 2)
				repeat++;
			else if (events[index].value == 0)
				release++;
		}
	}

	/* The counts. */
	(void)close(descriptor);
	printf("PS2KEYS device=%s press=%ld repeat=%ld release=%ld\n", path, press, repeat, release);
	return 0;
}

/* Finds the input device named KEYS_NAME; returns 0 with its path, 1 when there is none. */
static int
keys_find(
	char *path,
	size_t size)
{
	char name[128];
	int descriptor;
	int index;
	int result;

	/* Each event device in turn. */
	for (index = 0; index < KEYS_DEVICES; index++) {
		(void)snprintf(path, size, "/dev/input/event%d", index);
		descriptor = open(path, O_RDONLY | O_NONBLOCK);
		if (descriptor < 0)
			continue;

		/* Its name. */
		memset(name, 0, sizeof(name));
		result = ioctl(descriptor, EVIOCGNAME(sizeof(name) - 1U), name);
		(void)close(descriptor);
		if (result < 0)
			continue;
		result = strcmp(name, KEYS_NAME);
		if (result == 0)
			return 0;
	}

	/* None. */
	return 1;
}

/* Gives the monotonic clock in milliseconds. */
static int64_t
keys_now_ms(void)
{
	struct timespec now;

	/* The clock, in milliseconds. */
	(void)clock_gettime(CLOCK_MONOTONIC, &now);
	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

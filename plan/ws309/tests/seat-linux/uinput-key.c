/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * A keyboard that comes and goes, for the Linux guest's seat test: makes a
 * uinput keyboard, waits SECONDS (the compositor's scan finds it and asks
 * logind for it), types "a" COUNT times a second apart, waits again and
 * destroys it (logind tells the compositor it is gone).  Prints its node's
 * name when it can.
 *
 *   uinput-key SECONDS COUNT
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int emit(int descriptor, unsigned type, unsigned code, int value);
static void pause_seconds(unsigned seconds);

/*
 * Makes the keyboard, types and destroys it.
 */
int
main(
	int argc,
	char **argv)
{
	struct uinput_setup setup;
	char name[64];
	unsigned seconds;
	unsigned count;
	unsigned index;
	int descriptor;
	int error;

	/* The arguments. */
	if (argc != 3) {
		fprintf(stderr, "usage: uinput-key SECONDS COUNT\n");
		return 2;
	}
	seconds = (unsigned)strtoul(argv[1], NULL, 10);
	count = (unsigned)strtoul(argv[2], NULL, 10);

	/* The device: a keyboard with the key A. */
	descriptor = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (descriptor < 0) {
		fprintf(stderr, "uinput-key: open errno=%d\n", errno);
		return 1;
	}
	error = ioctl(descriptor, UI_SET_EVBIT, EV_KEY);
	if (error == 0)
		error = ioctl(descriptor, UI_SET_KEYBIT, KEY_A);
	if (error == 0)
		error = ioctl(descriptor, UI_SET_KEYBIT, KEY_ENTER);
	if (error != 0) {
		fprintf(stderr, "uinput-key: setup errno=%d\n", errno);
		return 1;
	}
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_VIRTUAL;
	setup.id.vendor = 0x1234;
	setup.id.product = 0x5678;
	snprintf(setup.name, sizeof(setup.name), "ws309 uinput keyboard");
	error = ioctl(descriptor, UI_DEV_SETUP, &setup);
	if (error == 0)
		error = ioctl(descriptor, UI_DEV_CREATE);
	if (error != 0) {
		fprintf(stderr, "uinput-key: create errno=%d\n", errno);
		return 1;
	}

	/* Its node's name, when the kernel tells it. */
	memset(name, 0, sizeof(name));
	error = ioctl(descriptor, UI_GET_SYSNAME(sizeof(name)), name);
	if (error >= 0)
		printf("uinput-key: created %s\n", name);
	else
		printf("uinput-key: created\n");
	fflush(stdout);

	/* The compositor finds it, then the keys. */
	pause_seconds(seconds);
	for (index = 0; index < count; index++) {
		(void)emit(descriptor, EV_KEY, KEY_A, 1);
		(void)emit(descriptor, EV_SYN, SYN_REPORT, 0);
		(void)emit(descriptor, EV_KEY, KEY_A, 0);
		(void)emit(descriptor, EV_SYN, SYN_REPORT, 0);
		pause_seconds(1U);
	}

	/* Gone. */
	pause_seconds(seconds);
	(void)ioctl(descriptor, UI_DEV_DESTROY);
	(void)close(descriptor);
	printf("uinput-key: destroyed\n");
	return 0;
}

/* Writes one event. */
static int
emit(
	int descriptor,
	unsigned type,
	unsigned code,
	int value)
{
	struct input_event event;
	ssize_t written;

	/* The kernel stamps the time. */
	memset(&event, 0, sizeof(event));
	event.type = (unsigned short)type;
	event.code = (unsigned short)code;
	event.value = value;
	written = write(descriptor, &event, sizeof(event));
	if (written != (ssize_t)sizeof(event))
		return -1;
	return 0;
}

/* Sleeps whole seconds. */
static void
pause_seconds(
	unsigned seconds)
{
	struct timespec span;

	/* Interrupted sleeps simply end early. */
	span.tv_sec = (time_t)seconds;
	span.tv_nsec = 0;
	(void)nanosleep(&span, NULL);
}

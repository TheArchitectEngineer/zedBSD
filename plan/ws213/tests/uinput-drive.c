/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The input of the Linux guest's ThreadSanitizer run (ws213-p004, the
 * design's 9 節 "入力の自動化"): a virtual mouse and keyboard made with
 * /dev/uinput, driven through the scenes the run needs.  Built and run in
 * the WS131 Debian QEMU+KVM guest over SSH (root, or a member of the input
 * group); the compositor finds the device like any other.
 *
 * Usage: uinput-drive circle SECONDS | home TIMES | drag SECONDS X Y | all
 *   circle  moves the pointer in circles at 125 reports a second;
 *   home    taps the super key (App Home opens and closes) TIMES times, a
 *           second apart;
 *   drag    presses the left button at the window title's place (X, Y,
 *           reached by first moving far to the top left) and moves it back
 *           and forth;
 *   all     circle 60, home 10, drag 30 at (400, 210).
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The reports' spacing, in microseconds (a 125 Hz mouse). */
#define DRIVE_REPORT_US		8000

static int drive_open(void);
static void drive_emit(int descriptor, unsigned short type, unsigned short code, int value);
static void drive_move(int descriptor, int dx, int dy);
static void drive_circle(int descriptor, int seconds);
static void drive_home(int descriptor, int times);
static void drive_drag(int descriptor, int seconds, int x, int y);

int
main(
	int count,
	char **arguments)
{
	int descriptor;

	/* The scene asked for. */
	if (count < 2) {
		fprintf(stderr, "usage: uinput-drive circle SECONDS | home TIMES | drag SECONDS X Y | all\n");
		return 2;
	}

	/* The device, and a moment for the compositor to find it. */
	descriptor = drive_open();
	if (descriptor < 0)
		return 1;
	sleep(2);

	/* By the scene's name. */
	if (strcmp(arguments[1], "circle") == 0 && count >= 3) {
		drive_circle(descriptor, atoi(arguments[2]));
	} else if (strcmp(arguments[1], "home") == 0 && count >= 3) {
		drive_home(descriptor, atoi(arguments[2]));
	} else if (strcmp(arguments[1], "drag") == 0 && count >= 5) {
		drive_drag(descriptor, atoi(arguments[2]), atoi(arguments[3]), atoi(arguments[4]));
	} else if (strcmp(arguments[1], "all") == 0) {
		drive_circle(descriptor, 60);
		drive_home(descriptor, 10);
		drive_drag(descriptor, 30, 400, 210);
	} else {
		fprintf(stderr, "uinput-drive: unknown scene %s\n", arguments[1]);
		return 2;
	}

	/* The device goes. */
	(void)ioctl(descriptor, UI_DEV_DESTROY);
	close(descriptor);
	printf("uinput-drive: done %s\n", arguments[1]);
	return 0;
}

/* Makes the virtual device: relative axes, the left button, the super key. */
static int
drive_open(void)
{
	struct uinput_setup setup;
	int descriptor;

	/* The uinput node. */
	descriptor = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
	if (descriptor < 0) {
		fprintf(stderr, "uinput-drive: /dev/uinput errno=%d\n", errno);
		return -1;
	}

	/* What it reports. */
	(void)ioctl(descriptor, UI_SET_EVBIT, EV_KEY);
	(void)ioctl(descriptor, UI_SET_KEYBIT, BTN_LEFT);
	(void)ioctl(descriptor, UI_SET_KEYBIT, KEY_LEFTMETA);
	(void)ioctl(descriptor, UI_SET_KEYBIT, KEY_A);
	(void)ioctl(descriptor, UI_SET_KEYBIT, KEY_Z);
	(void)ioctl(descriptor, UI_SET_EVBIT, EV_REL);
	(void)ioctl(descriptor, UI_SET_RELBIT, REL_X);
	(void)ioctl(descriptor, UI_SET_RELBIT, REL_Y);

	/* Its name, and the device made. */
	memset(&setup, 0, sizeof(setup));
	setup.id.bustype = BUS_USB;
	setup.id.vendor = 0x1234;
	setup.id.product = 0x5678;
	snprintf(setup.name, sizeof(setup.name), "ws213 uinput drive");
	if (ioctl(descriptor, UI_DEV_SETUP, &setup) != 0 || ioctl(descriptor, UI_DEV_CREATE) != 0) {
		fprintf(stderr, "uinput-drive: create errno=%d\n", errno);
		close(descriptor);
		return -1;
	}
	return descriptor;
}

/* Writes one event. */
static void
drive_emit(
	int descriptor,
	unsigned short type,
	unsigned short code,
	int value)
{
	struct input_event event;

	/* The kernel stamps the time. */
	memset(&event, 0, sizeof(event));
	event.type = type;
	event.code = code;
	event.value = value;
	if (write(descriptor, &event, sizeof(event)) != (ssize_t)sizeof(event))
		fprintf(stderr, "uinput-drive: write errno=%d\n", errno);
}

/* Moves the pointer by one report. */
static void
drive_move(
	int descriptor,
	int dx,
	int dy)
{
	/* Both axes, then the report's end. */
	drive_emit(descriptor, EV_REL, REL_X, dx);
	drive_emit(descriptor, EV_REL, REL_Y, dy);
	drive_emit(descriptor, EV_SYN, SYN_REPORT, 0);
}

/* Moves the pointer in circles for a while. */
static void
drive_circle(
	int descriptor,
	int seconds)
{
	double angle;
	int reports;
	int index;

	/* A circle every two seconds. */
	reports = seconds * (1000000 / DRIVE_REPORT_US);
	angle = 0.0;
	for (index = 0; index < reports; index++) {
		angle += 2.0 * M_PI / 250.0;
		drive_move(descriptor, (int)lround(cos(angle) * 6.0), (int)lround(sin(angle) * 6.0));
		usleep(DRIVE_REPORT_US);
	}
}

/* Taps the super key a number of times, a second apart. */
static void
drive_home(
	int descriptor,
	int times)
{
	int index;

	/* A press and a release, then a second. */
	for (index = 0; index < times; index++) {
		drive_emit(descriptor, EV_KEY, KEY_LEFTMETA, 1);
		drive_emit(descriptor, EV_SYN, SYN_REPORT, 0);
		usleep(80000);
		drive_emit(descriptor, EV_KEY, KEY_LEFTMETA, 0);
		drive_emit(descriptor, EV_SYN, SYN_REPORT, 0);
		sleep(1);
	}
}

/* Drags at a place back and forth for a while. */
static void
drive_drag(
	int descriptor,
	int seconds,
	int x,
	int y)
{
	int reports;
	int index;

	/* To the top left, then to the place (the pointer's speed taken as one to one). */
	for (index = 0; index < 100; index++) {
		drive_move(descriptor, -100, -100);
		usleep(DRIVE_REPORT_US);
	}
	for (index = 0; index < 100; index++) {
		drive_move(descriptor, x / 100, y / 100);
		usleep(DRIVE_REPORT_US);
	}

	/* Pressed, moved back and forth, let go. */
	drive_emit(descriptor, EV_KEY, BTN_LEFT, 1);
	drive_emit(descriptor, EV_SYN, SYN_REPORT, 0);
	reports = seconds * (1000000 / DRIVE_REPORT_US);
	for (index = 0; index < reports; index++) {
		if ((index / 100) % 2 == 0) {
			drive_move(descriptor, 4, 2);
		} else {
			drive_move(descriptor, -4, -2);
		}
		usleep(DRIVE_REPORT_US);
	}
	drive_emit(descriptor, EV_KEY, BTN_LEFT, 0);
	drive_emit(descriptor, EV_SYN, SYN_REPORT, 0);
}

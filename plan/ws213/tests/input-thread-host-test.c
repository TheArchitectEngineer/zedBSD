/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the input thread and the cursor's box (ws213-p004, the
 * design's 9 (k), (k1) to (k4); userland/desktop/wayland/input-thread.c
 * and cursor-box.c).
 *
 * The first part runs the input thread's passes one at a time
 * (kwl_input_thread_step) against pipes that stand for evdev devices, with
 * the main thread's side (commands, warps, the queue taken) between them:
 * a motion reaches the box while the main thread takes nothing, the
 * queue keeps the devices' order, a warp is what the next relative motion
 * adds to, an absolute pointer places without a warp, the settings and the
 * outputs' rectangles count from the next report, a detach is acknowledged
 * only after the device's last event, a descriptor number reused after
 * the acknowledgement is never read, a device that ends detaches itself,
 * and a touch pad's tap and its release at the detach come in order.
 *
 * The second part runs the real thread against a writer thread while the
 * main thread warps, changes the settings and the outputs, and detaches
 * and attaches a device again on a reused descriptor number; built with
 * -fsanitize=thread it is the design's 9 (m) for the input thread.  It
 * checks every report of every device arrives once and in order, and that
 * no detached descriptor number is read.
 *
 * The third part plugs a touch screen and a pen tablet into the running
 * thread, as the Pi's USB touch monitor was on 2026-10-12, and checks
 * that both are read, their reports come whole and in order (a report too
 * large or cut by the kernel is the only one left out), and that both end
 * at the unplug and are read again when plugged in again.
 */

#include "userland/desktop/wayland/input-thread.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The highest descriptor number the test watches reads of. */
#define TEST_FDS		1024

/* The reports each device of the threaded part sends. */
#define TEST_REPORTS		20000U

/* The devices of the threaded part. */
#define TEST_DEVICES		3U

/* The fake clock of the first part (microseconds), and whether it is used. */
static uint64_t fake_us;
static int fake_clock;

/* How many reads each descriptor number had, and whether a number is poisoned (must not be read). */
static atomic_uint reads_of[TEST_FDS];
static atomic_int poisoned[TEST_FDS];
static atomic_uint poisoned_reads;

/* The checks run and failed. */
static unsigned checks;
static unsigned failures;

/* The events the main thread took in the first part, in order. */
static struct kwl_input_event taken[256];
static unsigned taken_count;

static void check(int condition, const char *what);
static ssize_t test_read(int descriptor, struct input_event *events, size_t capacity);
static void device_pipe(int *read_end, int *write_end);
static void send_event(int descriptor, uint16_t type, uint16_t code, int32_t value);
static void take_all(struct kwl_input_thread *input);
static void attach(struct kwl_input_thread *input, unsigned slot, int descriptor, unsigned kind);
static void stepwise(void);
static void threaded(void);
static void *writer_main(void *argument);
static void hotplug(void);
static void *panel_main(void *argument);
static void panel_report(int descriptor, unsigned number, unsigned fingers, unsigned oversize);
static void pen_report(int descriptor, unsigned number);
static int take_until(struct kwl_input_thread *input, int wake, unsigned slot, enum kwl_input_event_kind kind, unsigned wait_ms);
static void warp_set(const void *context, const struct kwl_plane_rect *outputs, int32_t *x, int32_t *y, unsigned *output);
static void warp_outputs(struct kwl_cursor_box *box, const struct kwl_plane_rect *outputs, const int32_t *place, struct kwl_cursor_motion *motion);

/* The device kinds the test attaches. */
enum {
	KIND_MOUSE = 1,
	KIND_ABSOLUTE,
	KIND_TOUCHPAD,
	KIND_TOUCH,
	KIND_TABLET
};

/* Gives the clock in milliseconds: the fake one in the first part. */
uint64_t
kwl_milliseconds(
	void)
{
	struct timespec now;

	/* The fake time. */
	if (fake_clock)
		return fake_us / 1000U;

	/* The monotonic clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Gives the clock in microseconds: the fake one in the first part. */
uint64_t
kwl_microseconds(
	void)
{
	struct timespec now;

	/* The fake time. */
	if (fake_clock)
		return fake_us;

	/* The monotonic clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000000U + (uint64_t)now.tv_nsec / 1000U;
}

int
main(
	void)
{
	/* A pipe whose reader closed gives an error, not a signal. */
	signal(SIGPIPE, SIG_IGN);

	/* The passes one at a time, then the threads. */
	stepwise();
	threaded();
	hotplug();

	/* The verdict. */
	if (failures != 0) {
		printf("input-thread-host-test: FAIL (%u of %u checks)\n", failures, checks);
		return 1;
	}
	printf("input-thread-host-test: PASS (%u checks)\n", checks);
	return 0;
}

/* Counts one check, and reports it when it failed. */
static void
check(
	int condition,
	const char *what)
{
	/* One more check. */
	checks++;
	if (!condition) {
		printf("FAIL: %s\n", what);
		failures++;
	}
}

/* Reads a device's events as the backend does, counting the reads of each descriptor number. */
static ssize_t
test_read(
	int descriptor,
	struct input_event *events,
	size_t capacity)
{
	ssize_t bytes;

	/* A poisoned number must never be read. */
	if (descriptor >= 0 && descriptor < TEST_FDS) {
		atomic_fetch_add(&reads_of[descriptor], 1U);
		if (atomic_load(&poisoned[descriptor]))
			atomic_fetch_add(&poisoned_reads, 1U);
	}

	/* Whole events. */
	bytes = read(descriptor, events, capacity * sizeof(events[0]));
	if (bytes < 0)
		return -1;
	if (bytes == 0)
		return 0;
	return bytes / (ssize_t)sizeof(events[0]);
}

/* Makes a pipe that stands for a device, its read end not blocking. */
static void
device_pipe(
	int *read_end,
	int *write_end)
{
	int ends[2];

	/* The pipe. */
	if (pipe(ends) != 0) {
		perror("pipe");
		exit(2);
	}
	(void)fcntl(ends[0], F_SETFL, O_NONBLOCK);
	*read_end = ends[0];
	*write_end = ends[1];
}

/* Writes one evdev event with the fake or real time. */
static void
send_event(
	int descriptor,
	uint16_t type,
	uint16_t code,
	int32_t value)
{
	struct input_event event;
	uint64_t now;

	/* The event, stamped now. */
	memset(&event, 0, sizeof(event));
	now = kwl_microseconds();
	event.time.tv_sec = (time_t)(now / 1000000U);
	event.time.tv_usec = (suseconds_t)(now % 1000000U);
	event.type = type;
	event.code = code;
	event.value = value;
	if (write(descriptor, &event, sizeof(event)) != (ssize_t)sizeof(event)) {
		perror("write");
		exit(2);
	}
}

/* Takes the queue as the main thread does, keeping copies. */
static void
take_all(
	struct kwl_input_thread *input)
{
	struct kwl_input_event *events;
	struct kwl_input_event *event;

	/* Each event in order. */
	events = kwl_input_thread_take(input);
	while (events != NULL) {
		event = events;
		events = event->next;
		if (taken_count < sizeof(taken) / sizeof(taken[0])) {
			taken[taken_count] = *event;
			taken[taken_count].next = NULL;
			taken_count++;
		}
		free(event);
	}
}

/* Attaches a device of a kind in a slot. */
static void
attach(
	struct kwl_input_thread *input,
	unsigned slot,
	int descriptor,
	unsigned kind)
{
	struct kwl_input_attach device;

	/* Its kind and ranges. */
	memset(&device, 0, sizeof(device));
	device.slot = slot;
	device.fd = descriptor;
	if (kind == KIND_MOUSE) {
		device.pointer = 1U;
		device.keyboard = 1U;
	} else if (kind == KIND_ABSOLUTE) {
		device.pointer = 1U;
		device.absolute = 1U;
		device.abs_x_maximum = 32767;
		device.abs_y_maximum = 32767;
	} else if (kind == KIND_TOUCHPAD) {
		device.pointer = 1U;
		device.touchpad = 1U;
		device.resolution_x = 12;
		device.resolution_y = 12;
		device.pad_x_maximum = 1200;
		device.pad_y_maximum = 800;
	} else if (kind == KIND_TABLET) {
		device.tablet = 1U;
	} else {
		device.touch = 1U;
	}
	if (kwl_input_thread_attach(input, &device) != 0) {
		printf("attach failed\n");
		exit(2);
	}
}

/* Sets the box's place as a warp would (the main thread's). */
static void
warp_set(
	const void *context,
	const struct kwl_plane_rect *outputs,
	int32_t *x,
	int32_t *y,
	unsigned *output)
{
	const int32_t *place;

	/* The place given, on the anchor. */
	(void)outputs;
	place = context;
	*x = place[0];
	*y = place[1];
	*output = KWL_PLANE_ANCHOR;
}

/*
 * Warps with the outputs' rectangles given, as the main thread does: the
 * geometry of WS217 with each output's panel its rectangle, not turned, and
 * the absolute pointer on the anchor.
 */
static void
warp_outputs(
	struct kwl_cursor_box *box,
	const struct kwl_plane_rect *outputs,
	const int32_t *place,
	struct kwl_cursor_motion *motion)
{
	struct kwl_cursor_geometry geometry;
	unsigned slot;

	/* The rectangles, and each panel the same size at 0. */
	memset(&geometry, 0, sizeof(geometry));
	memcpy(geometry.outputs, outputs, sizeof(geometry.outputs));
	geometry.output_count = KWL_PLANE_SLOTS;
	for (slot = 0U; slot < KWL_PLANE_SLOTS; slot++) {
		geometry.physical[slot].width = outputs[slot].width;
		geometry.physical[slot].height = outputs[slot].height;
	}

	/* The absolute pointer on the anchor. */
	geometry.absolute_slot = KWL_PLANE_ANCHOR;

	/* The warp. */
	kwl_cursor_box_warp(box, &geometry, warp_set, place, motion);
}

/* Runs the input thread's passes one at a time against the main thread's side. */
static void
stepwise(void)
{
	struct kwl_input_thread input;
	struct kwl_cursor_box box;
	struct kwl_input_settings settings;
	struct kwl_plane_rect outputs[KWL_PLANE_SLOTS];
	struct kwl_cursor_motion motion;
	int32_t place[2];
	int32_t x;
	int32_t y;
	unsigned output;
	uint64_t sequence;
	int wake[2];
	int mouse_read;
	int mouse_write;
	int tablet_read;
	int tablet_write;
	int pad_read;
	int pad_write;
	int touch_read;
	int touch_write;
	int spare_read;
	int spare_write;
	unsigned reads_before;
	unsigned index;
	unsigned presses;
	unsigned releases;
	int detached_index;
	int error;
	char byte;

	/* The fake clock, the box, the main thread's wake pipe and the input thread's state. */
	fake_clock = 1;
	fake_us = 1000000U;
	if (pipe(wake) != 0)
		exit(2);
	(void)fcntl(wake[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(wake[1], F_SETFL, O_NONBLOCK);
	check(kwl_cursor_box_init(&box, 960, 540, 1920, 1080) == 0, "the box starts");
	memset(&settings, 0, sizeof(settings));
	settings.mouse_speed = 100;
	settings.mouse_acceleration = KWL_ACCEL_NONE;
	settings.touchpad_speed = 100;
	settings.touchpad_acceleration = KWL_ACCEL_NONE;
	check(kwl_input_thread_init(&input, &box, wake[1], test_read, &settings) == 0, "the input thread's state starts");

	/* (k) A mouse's motion reaches the box while the main thread takes nothing. */
	device_pipe(&mouse_read, &mouse_write);
	attach(&input, 0U, mouse_read, KIND_MOUSE);
	check(kwl_input_thread_step(&input, 0) == 1, "a pass takes the attach");
	fake_us += 8000U;
	send_event(mouse_write, EV_REL, REL_X, 10);
	send_event(mouse_write, EV_REL, REL_Y, 5);
	send_event(mouse_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	kwl_cursor_box_read(&box, &x, &y, &output);
	check(x == 970 && y == 545, "the motion is in the box at once (no acceleration, speed 100)");
	check(read(wake[0], &byte, 1) == 1, "the main thread is woken for the queue");

	/* The order motion, button, motion is kept, and the box has the last place. */
	fake_us += 8000U;
	send_event(mouse_write, EV_KEY, BTN_LEFT, 1);
	send_event(mouse_write, EV_SYN, SYN_REPORT, 0);
	fake_us += 8000U;
	send_event(mouse_write, EV_REL, REL_X, -4);
	send_event(mouse_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	check(taken_count == 3U, "three reports queued");
	check(taken[0].kind == KWL_INPUT_EVENT_FRAME && taken[0].u.frame.placed && taken[0].u.frame.motion.x == 970, "first the motion");
	check(taken[1].u.frame.key_count == 1U && taken[1].u.frame.keys[0].code == BTN_LEFT && taken[1].u.frame.keys[0].value == 1, "then the button");
	check(taken[2].u.frame.motion.x == 966 && taken[2].u.frame.motion.y == 545, "then the second motion");
	check(taken[0].frame_time_us < taken[2].frame_time_us, "the reports' evdev times in order");
	check(taken[0].u.frame.motion.write_us >= taken[0].u.frame.motion.event_us, "a write after its report");

	/* (k) A warp: the next relative motion adds to the warp's place; a motion before it keeps the old count. */
	place[0] = 100;
	place[1] = 200;
	kwl_cursor_box_warp(&box, NULL, warp_set, place, &motion);
	check(motion.warp == 1U && motion.x == 100 && motion.y == 200, "the warp moves the box and its count");
	check(taken[2].u.frame.motion.warp == 0U, "the motion before the warp has the old count");
	fake_us += 8000U;
	send_event(mouse_write, EV_REL, REL_X, 3);
	send_event(mouse_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	check(taken_count == 1U && taken[0].u.frame.motion.x == 103 && taken[0].u.frame.motion.y == 200, "the motion adds to the warp's place");
	check(taken[0].u.frame.motion.warp == 1U, "the motion after the warp has the new count");

	/* (k) The settings count from the next report. */
	settings.mouse_speed = 200;
	kwl_input_thread_settings(&input, &settings);
	fake_us += 8000U;
	send_event(mouse_write, EV_REL, REL_X, 3);
	send_event(mouse_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	check(taken_count == 1U && taken[0].u.frame.motion.x > 106, "a faster speed moves further from the next report");

	/* (k) The outputs' rectangles count from the next report: a head to the right is crossed into. */
	memset(outputs, 0, sizeof(outputs));
	outputs[0].width = 1920;
	outputs[0].height = 1080;
	outputs[1].x = 1920;
	outputs[1].width = 1280;
	outputs[1].height = 1024;
	place[0] = 1915;
	place[1] = 500;
	warp_outputs(&box, outputs, place, &motion);
	settings.mouse_speed = 100;
	kwl_input_thread_settings(&input, &settings);
	fake_us += 8000U;
	send_event(mouse_write, EV_REL, REL_X, 20);
	send_event(mouse_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	check(taken_count == 1U && taken[0].u.frame.motion.output == 1U && taken[0].u.frame.motion.crossed, "the motion crosses to the head given in the warp's section");

	/* (k2) An absolute pointer places on the anchor without a warp. */
	device_pipe(&tablet_read, &tablet_write);
	attach(&input, 1U, tablet_read, KIND_ABSOLUTE);
	(void)kwl_input_thread_step(&input, 0);
	fake_us += 8000U;
	send_event(tablet_write, EV_ABS, ABS_X, 16384);
	send_event(tablet_write, EV_ABS, ABS_Y, 16384);
	send_event(tablet_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	kwl_cursor_box_read(&box, &x, &y, &output);
	check(taken_count == 1U && taken[0].u.frame.absolute && taken[0].u.frame.placed, "the absolute report is placed");
	check(output == KWL_PLANE_ANCHOR && x == 959 && y == 539, "on the anchor where it maps");
	check(taken[0].u.frame.motion.warp == 2U, "an absolute place does not move the warp count");

	/* (k1) A detach is acknowledged only after the device's last event; its number reused later is never read. */
	fake_us += 8000U;
	send_event(mouse_write, EV_KEY, BTN_LEFT, 0);
	send_event(mouse_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	error = kwl_input_thread_detach(&input, 0U, &sequence);
	check(error == 0, "the detach is asked");
	check(kwl_input_thread_detach_wait(&input, sequence, 1U) == ETIMEDOUT, "no acknowledgement before the input thread's pass");
	(void)kwl_input_thread_step(&input, 0);
	check(kwl_input_thread_detach_wait(&input, sequence, 1U) == 0, "acknowledged by the pass");
	taken_count = 0;
	take_all(&input);
	check(taken_count == 2U && taken[0].u.frame.key_count == 1U && taken[1].kind == KWL_INPUT_EVENT_DETACHED, "the release first, then the device's last event");
	check(taken[1].u.detached.reason == KWL_INPUT_DETACH_ASKED && taken[1].u.detached.sequence == sequence, "the last event answers the detach");

	/* The number closed and reused for another file (a client's socket in the compositor). */
	reads_before = atomic_load(&reads_of[mouse_read]);
	close(mouse_read);
	device_pipe(&spare_read, &spare_write);
	check(dup2(spare_read, mouse_read) == mouse_read, "the number is reused");
	if (spare_read != mouse_read)
		close(spare_read);
	check(write(spare_write, "x", 1) == 1, "the other file has data");
	(void)kwl_input_thread_step(&input, 0);
	(void)kwl_input_thread_step(&input, 0);
	check(atomic_load(&reads_of[mouse_read]) == reads_before, "the reused number is not read");
	check(read(mouse_read, &byte, 1) == 1, "the other file's data is still there");
	close(mouse_read);
	close(spare_write);
	close(mouse_write);

	/* (k1) A device that ends detaches itself with that reason, as its last event. */
	close(tablet_write);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	check(taken_count == 1U && taken[0].kind == KWL_INPUT_EVENT_DETACHED && taken[0].slot == 1U && taken[0].u.detached.reason == KWL_INPUT_DETACH_ENDED, "the ended device's last event");
	close(tablet_read);

	/* (k4) A touch pad's tap comes as its actions; a button it holds is let go before its last event. */
	device_pipe(&pad_read, &pad_write);
	attach(&input, 2U, pad_read, KIND_TOUCHPAD);
	(void)kwl_input_thread_step(&input, 0);
	fake_us += 10000000U;
	send_event(pad_write, EV_ABS, ABS_MT_SLOT, 0);
	send_event(pad_write, EV_ABS, ABS_MT_TRACKING_ID, 1);
	send_event(pad_write, EV_ABS, ABS_MT_POSITION_X, 500);
	send_event(pad_write, EV_ABS, ABS_MT_POSITION_Y, 300);
	send_event(pad_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	fake_us += 8000U;
	send_event(pad_write, EV_ABS, ABS_MT_SLOT, 0);
	send_event(pad_write, EV_ABS, ABS_MT_TRACKING_ID, -1);
	send_event(pad_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	presses = 0;
	releases = 0;
	for (index = 0; index < taken_count; index++) {
		unsigned action;

		for (action = 0; action < taken[index].u.pad.actions.count; action++) {
			if (taken[index].u.pad.actions.actions[action].kind != KWL_TOUCHPAD_BUTTON)
				continue;
			if (taken[index].u.pad.actions.actions[action].pressed)
				presses++;
			else
				releases++;
		}
	}
	check(presses == 1U && releases == 1U, "the tap's click comes from the input thread");
	fake_us += 1000000U;
	send_event(pad_write, EV_KEY, BTN_LEFT, 1);
	send_event(pad_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	(void)kwl_input_thread_detach(&input, 2U, &sequence);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	detached_index = -1;
	releases = 0;
	for (index = 0; index < taken_count; index++) {
		unsigned action;

		if (taken[index].kind == KWL_INPUT_EVENT_DETACHED) {
			detached_index = (int)index;
			continue;
		}
		for (action = 0; action < taken[index].u.pad.actions.count; action++) {
			if (taken[index].u.pad.actions.actions[action].kind == KWL_TOUCHPAD_BUTTON &&
			    !taken[index].u.pad.actions.actions[action].pressed &&
			    detached_index < 0)
				releases++;
		}
	}
	check(releases == 1U && detached_index == (int)taken_count - 1, "the held button is let go before the pad's last event");
	close(pad_read);
	close(pad_write);

	/* (k4) A touch screen's report comes whole, for the main thread to apply. */
	device_pipe(&touch_read, &touch_write);
	attach(&input, 3U, touch_read, KIND_TOUCH);
	(void)kwl_input_thread_step(&input, 0);
	fake_us += 8000U;
	send_event(touch_write, EV_ABS, ABS_MT_SLOT, 0);
	send_event(touch_write, EV_ABS, ABS_MT_POSITION_X, 77);
	send_event(touch_write, EV_SYN, SYN_REPORT, 0);
	(void)kwl_input_thread_step(&input, 0);
	taken_count = 0;
	take_all(&input);
	check(taken_count == 1U && taken[0].kind == KWL_INPUT_EVENT_REPORT && taken[0].u.report.count == 2U && taken[0].u.report.events[1].value == 77, "the touch screen's report is copied");
	kwl_cursor_box_read(&box, &x, &y, &output);
	check(x == 959 && y == 539, "a touch screen's report does not move the box (the main thread warps)");

	/* The end: the stop is seen at the next pass. */
	kwl_input_thread_destroy(&input);
	kwl_cursor_box_destroy(&box);
	close(touch_read);
	close(touch_write);
	close(wake[0]);
	close(wake[1]);
	fake_clock = 0;
}

/*
 * The writer of the threaded part: each device's reports carry their
 * number in a touch screen's position, so the main thread checks that
 * every report arrives once and in order.
 */
struct writer {
	int write_end[TEST_DEVICES];
	atomic_int stop;
};

/* Writes the devices' reports one after another until each has sent its share. */
static void *
writer_main(
	void *argument)
{
	struct writer *writer;
	unsigned number;
	unsigned device;

	/* Each report on each device in turn; a full pipe waits. */
	writer = argument;
	for (number = 1; number <= TEST_REPORTS; number++) {
		for (device = 0; device < TEST_DEVICES; device++) {
			struct input_event events[3];
			struct pollfd room;
			uint64_t now;

			memset(events, 0, sizeof(events));
			now = kwl_microseconds();
			events[0].time.tv_sec = (time_t)(now / 1000000U);
			events[0].time.tv_usec = (suseconds_t)(now % 1000000U);
			events[1].time = events[0].time;
			events[2].time = events[0].time;
			events[0].type = EV_ABS;
			events[0].code = ABS_MT_SLOT;
			events[1].type = EV_ABS;
			events[1].code = ABS_MT_POSITION_X;
			events[1].value = (int32_t)number;
			events[2].type = EV_SYN;
			events[2].code = SYN_REPORT;
			room.fd = writer->write_end[device];
			room.events = POLLOUT;
			(void)poll(&room, 1, -1);
			if (write(writer->write_end[device], events, sizeof(events)) != (ssize_t)sizeof(events)) {
				perror("write");
				exit(2);
			}
		}
	}
	return NULL;
}

/* Runs the real thread against the writer while the main thread warps, sets and detaches. */
static void
threaded(void)
{
	struct kwl_input_thread input;
	struct kwl_cursor_box box;
	struct kwl_input_settings settings;
	struct kwl_plane_rect outputs[KWL_PLANE_SLOTS];
	struct kwl_cursor_motion motion;
	struct kwl_input_event *events;
	struct kwl_input_event *event;
	struct writer writer;
	pthread_t writer_thread;
	unsigned last[TEST_DEVICES];
	unsigned out_of_order;
	unsigned detaches;
	unsigned round;
	uint64_t sequence;
	int read_end[TEST_DEVICES];
	int mouse_read;
	int mouse_write;
	int spare_read;
	int spare_write;
	int32_t place[2];
	int wake[2];
	unsigned device;
	unsigned complete;
	unsigned char drained[64];

	/* The box, the wake pipe, the thread and the devices. */
	if (pipe(wake) != 0)
		exit(2);
	(void)fcntl(wake[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(wake[1], F_SETFL, O_NONBLOCK);
	check(kwl_cursor_box_init(&box, 960, 540, 1920, 1080) == 0, "the box starts (threads)");
	memset(&settings, 0, sizeof(settings));
	settings.mouse_speed = 150;
	settings.mouse_acceleration = KWL_ACCEL_STRONG;
	settings.touchpad_speed = 100;
	check(kwl_input_thread_init(&input, &box, wake[1], test_read, &settings) == 0, "the thread's state starts");
	for (device = 0; device < TEST_DEVICES; device++) {
		device_pipe(&read_end[device], &writer.write_end[device]);
		attach(&input, device, read_end[device], KIND_TOUCH);
		last[device] = 0;
	}
	device_pipe(&mouse_read, &mouse_write);
	attach(&input, 10U, mouse_read, KIND_MOUSE);
	check(kwl_input_thread_start(&input) == 0, "the thread starts");
	atomic_store(&writer.stop, 0);
	if (pthread_create(&writer_thread, NULL, writer_main, &writer) != 0)
		exit(2);

	/* The main thread's side until every report has come. */
	out_of_order = 0;
	detaches = 0;
	round = 0;
	complete = 0;
	while (!complete) {
		struct pollfd ready;

		/* Woken by the input thread, or after a while to do the main thread's own work. */
		ready.fd = wake[0];
		ready.events = POLLIN;
		(void)poll(&ready, 1, 2);
		while (read(wake[0], drained, sizeof(drained)) > 0)
			;

		/* The queue, each report checked against its device's last. */
		events = kwl_input_thread_take(&input);
		while (events != NULL) {
			event = events;
			events = event->next;
			if (event->kind == KWL_INPUT_EVENT_REPORT && event->slot < TEST_DEVICES) {
				unsigned number;

				number = (unsigned)event->u.report.events[1].value;
				if (number != last[event->slot] + 1U)
					out_of_order++;
				last[event->slot] = number;
			}
			free(event);
		}

		/* A warp, a setting and the outputs now and then. */
		round++;
		if ((round % 7U) == 0U) {
			place[0] = (int32_t)(round % 1900U);
			place[1] = (int32_t)(round % 1000U);
			memset(outputs, 0, sizeof(outputs));
			outputs[0].width = 1920;
			outputs[0].height = 1080;
			outputs[1].x = 1920;
			outputs[1].width = (round % 2U) ? 1280U : 0U;
			outputs[1].height = 1024;
			warp_outputs(&box, outputs, place, &motion);
			settings.mouse_speed = 100 + (int32_t)(round % 100U);
			kwl_input_thread_settings(&input, &settings);
		}

		/* The mouse's motion from the main thread's side of the pipe. */
		send_event(mouse_write, EV_REL, REL_X, 1);
		send_event(mouse_write, EV_SYN, SYN_REPORT, 0);

		/* The mouse detached, its number poisoned and reused, then attached again on a fresh pipe. */
		if ((round % 50U) == 0U && detaches < 40U) {
			check(kwl_input_thread_detach(&input, 10U, &sequence) == 0, "a detach is asked (threads)");
			check(kwl_input_thread_detach_wait(&input, sequence, KWL_INPUT_DETACH_WAIT_MS * 10U) == 0, "the detach is acknowledged in time");
			close(mouse_read);
			close(mouse_write);
			device_pipe(&spare_read, &spare_write);
			if (dup2(spare_read, mouse_read) != mouse_read)
				exit(2);
			if (spare_read != mouse_read)
				close(spare_read);
			if (mouse_read < TEST_FDS)
				atomic_store(&poisoned[mouse_read], 1);
			(void)write(spare_write, "poison", 6);
			usleep(200);
			if (mouse_read < TEST_FDS)
				atomic_store(&poisoned[mouse_read], 0);
			close(mouse_read);
			close(spare_write);
			device_pipe(&mouse_read, &mouse_write);
			attach(&input, 10U, mouse_read, KIND_MOUSE);
			detaches++;
		}

		/* Done when every device's last report came. */
		complete = 1;
		for (device = 0; device < TEST_DEVICES; device++) {
			if (last[device] != TEST_REPORTS)
				complete = 0;
		}
	}

	/* The threads end. */
	(void)pthread_join(writer_thread, NULL);
	kwl_input_thread_stop(&input);
	kwl_input_thread_destroy(&input);
	kwl_cursor_box_destroy(&box);
	check(out_of_order == 0U, "every device's reports came once and in order (threads)");
	check(detaches == 40U, "forty detaches and attaches ran (threads)");
	check(atomic_load(&poisoned_reads) == 0U, "no detached number was read (threads)");
	for (device = 0; device < TEST_DEVICES; device++) {
		close(read_end[device]);
		close(writer.write_end[device]);
	}
	close(mouse_read);
	close(mouse_write);
	close(wake[0]);
	close(wake[1]);
}

/*
 * The writer of the hot-plug part: a touch screen's and a pen's reports,
 * each numbered (the touch screen's in its MSC_TIMESTAMP, the pen's in its
 * ABS_X), one after the other.
 */
struct panel {
	int touch;
	int pen;
};

/*
 * Runs the input thread while a touch screen and a pen tablet are plugged
 * in (2026-10-12 on the Pi: a USB touch monitor's two nodes, event4 and
 * event5, attached while the compositor ran): the attach reaches a thread
 * waiting in its poll without a time limit, the panel's multitouch reports
 * (two fingers at times, a report too large to hold now and then, a
 * dropped one once) and the pen's come whole, once and in order beside a
 * mouse, the unplug ends both with their last event, and a node plugged in
 * again on the same slot is read afresh.
 */
static void
hotplug(void)
{
	struct kwl_input_thread input;
	struct kwl_cursor_box box;
	struct kwl_input_settings settings;
	struct kwl_input_event *events;
	struct kwl_input_event *event;
	struct panel panel;
	pthread_t panel_thread;
	struct pollfd ready;
	unsigned touch_last;
	unsigned pen_last;
	unsigned touch_expected;
	unsigned touch_wrong;
	unsigned pen_wrong;
	unsigned count_wrong;
	unsigned after_end;
	unsigned ended;
	unsigned number;
	unsigned fingers;
	unsigned round;
	uint64_t deadline;
	int touch_read;
	int pen_read;
	int mouse_read;
	int mouse_write;
	int wake[2];
	int seen;
	unsigned char drained[64];

	/* The box, the wake pipe, and the thread with only a mouse, waiting in its poll without a limit. */
	if (pipe(wake) != 0)
		exit(2);
	(void)fcntl(wake[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(wake[1], F_SETFL, O_NONBLOCK);
	check(kwl_cursor_box_init(&box, 400, 240, 800, 480) == 0, "the box starts (hot plug)");
	memset(&settings, 0, sizeof(settings));
	settings.mouse_speed = 150;
	settings.mouse_acceleration = KWL_ACCEL_STRONG;
	settings.touchpad_speed = 100;
	check(kwl_input_thread_init(&input, &box, wake[1], test_read, &settings) == 0, "the thread's state starts (hot plug)");
	device_pipe(&mouse_read, &mouse_write);
	attach(&input, 0U, mouse_read, KIND_MOUSE);
	check(kwl_input_thread_start(&input) == 0, "the thread starts (hot plug)");
	usleep(20000);

	/* The monitor plugged in: its touch screen and its pen, attached while the thread waits. */
	device_pipe(&touch_read, &panel.touch);
	device_pipe(&pen_read, &panel.pen);
	attach(&input, 4U, touch_read, KIND_TOUCH);
	attach(&input, 5U, pen_read, KIND_TABLET);
	panel_report(panel.touch, 0U, 1U, 0U);
	seen = take_until(&input, wake[0], 4U, KWL_INPUT_EVENT_REPORT, 1000U);
	check(seen, "the touch screen attached while the thread waited is read");
	pen_report(panel.pen, 0U);
	seen = take_until(&input, wake[0], 5U, KWL_INPUT_EVENT_REPORT, 1000U);
	check(seen, "the pen attached while the thread waited is read");

	/* The panel's and the pen's reports from their own thread, the mouse's from this one. */
	if (pthread_create(&panel_thread, NULL, panel_main, &panel) != 0)
		exit(2);
	touch_last = 0U;
	pen_last = 0U;
	touch_wrong = 0U;
	pen_wrong = 0U;
	count_wrong = 0U;
	round = 0U;
	deadline = kwl_milliseconds() + 60000U;
	while ((touch_last != TEST_REPORTS || pen_last != TEST_REPORTS) && kwl_milliseconds() < deadline) {
		/* Woken by the input thread, or after a while for the mouse. */
		ready.fd = wake[0];
		ready.events = POLLIN;
		(void)poll(&ready, 1, 2);
		while (read(wake[0], drained, sizeof(drained)) > 0)
			;

		/* Each report against the one before it of its device. */
		events = kwl_input_thread_take(&input);
		while (events != NULL) {
			event = events;
			events = event->next;
			if (event->kind == KWL_INPUT_EVENT_REPORT && event->slot == 4U) {
				/* The next number the panel sent that fits a report (a too large one is not passed on). */
				number = (unsigned)event->u.report.events[0].value;
				touch_expected = touch_last + 1U;
				while ((touch_expected % 97U) == 0U || touch_expected == 1234U)
					touch_expected++;
				if (number != touch_expected)
					touch_wrong++;
				touch_last = number;

				/* The whole report: its time, each finger's four axes, the button and the first finger's place. */
				fingers = 1U;
				if ((number % 13U) == 0U)
					fingers = 2U;
				if (event->u.report.count != 1U + 4U * fingers + 3U)
					count_wrong++;
			} else if (event->kind == KWL_INPUT_EVENT_REPORT && event->slot == 5U) {
				/* The pen's reports, none lost. */
				number = (unsigned)event->u.report.events[0].value;
				if (number != pen_last + 1U)
					pen_wrong++;
				pen_last = number;
			}
			free(event);
		}

		/* The mouse moves meanwhile. */
		round++;
		if ((round % 3U) == 0U) {
			send_event(mouse_write, EV_REL, REL_X, 1);
			send_event(mouse_write, EV_SYN, SYN_REPORT, 0);
		}
	}
	(void)pthread_join(panel_thread, NULL);
	check(touch_last == TEST_REPORTS && touch_wrong == 0U, "every touch report that fits came once and in order");
	check(count_wrong == 0U, "every touch report came whole");
	check(pen_last == TEST_REPORTS && pen_wrong == 0U, "every pen report came once and in order");

	/* The monitor unplugged: both nodes end, each with its last event after its reports. */
	close(panel.touch);
	close(panel.pen);
	ended = 0U;
	after_end = 0U;
	deadline = kwl_milliseconds() + 2000U;
	while (ended != 3U && kwl_milliseconds() < deadline) {
		/* Woken by the input thread. */
		ready.fd = wake[0];
		ready.events = POLLIN;
		(void)poll(&ready, 1, 10);
		while (read(wake[0], drained, sizeof(drained)) > 0)
			;

		/* The ends, and nothing of a node after its end. */
		events = kwl_input_thread_take(&input);
		while (events != NULL) {
			event = events;
			events = event->next;
			if (event->slot == 4U || event->slot == 5U) {
				if ((ended & (1U << (event->slot - 4U))) != 0U)
					after_end++;
				if (event->kind == KWL_INPUT_EVENT_DETACHED &&
				    event->u.detached.reason == KWL_INPUT_DETACH_ENDED &&
				    event->u.detached.sequence == 0U)
					ended |= 1U << (event->slot - 4U);
			}
			free(event);
		}
	}
	check(ended == 3U, "the unplugged touch screen and pen each end");
	check(after_end == 0U, "nothing of an ended node comes after its end");
	close(touch_read);
	close(pen_read);

	/* Plugged in again: the touch screen's new node in the same slot is read afresh. */
	device_pipe(&touch_read, &panel.touch);
	attach(&input, 4U, touch_read, KIND_TOUCH);
	panel_report(panel.touch, 1U, 1U, 0U);
	seen = take_until(&input, wake[0], 4U, KWL_INPUT_EVENT_REPORT, 1000U);
	check(seen, "the touch screen plugged in again is read");

	/* The thread ends. */
	kwl_input_thread_stop(&input);
	kwl_input_thread_destroy(&input);
	kwl_cursor_box_destroy(&box);
	close(touch_read);
	close(panel.touch);
	close(mouse_read);
	close(mouse_write);
	close(wake[0]);
	close(wake[1]);
}

/* Writes the panel's and the pen's numbered reports, a dropped report once among them. */
static void *
panel_main(
	void *argument)
{
	struct panel *panel;
	struct input_event dropped[2];
	unsigned number;
	unsigned fingers;
	unsigned oversize;

	/* Each number on both nodes; a too large touch report every 97th, two fingers every 13th. */
	panel = argument;
	for (number = 1U; number <= TEST_REPORTS; number++) {
		fingers = 1U;
		if ((number % 13U) == 0U)
			fingers = 2U;
		oversize = 0U;
		if ((number % 97U) == 0U)
			oversize = 1U;

		/* The kernel drops events once: the report it cut is not passed on. */
		if (number == 1234U) {
			memset(dropped, 0, sizeof(dropped));
			dropped[0].type = EV_MSC;
			dropped[0].code = MSC_TIMESTAMP;
			dropped[0].value = (int32_t)number;
			dropped[1].type = EV_SYN;
			dropped[1].code = SYN_DROPPED;
			if (write(panel->touch, dropped, sizeof(dropped)) != (ssize_t)sizeof(dropped))
				exit(2);
			panel_report(panel->touch, 0U, 0U, 2U);
		} else {
			panel_report(panel->touch, number, fingers, oversize);
		}
		pen_report(panel->pen, number);
	}

	/* Every report is written. */
	return NULL;
}

/*
 * Writes one touch screen report numbered in its MSC_TIMESTAMP: each
 * finger's slot, number and place, then BTN_TOUCH and the first finger's
 * ABS_X and ABS_Y.  Oversize 1 makes it too large to hold (17 fingers);
 * oversize 2 writes only the end of a report the kernel cut.
 */
static void
panel_report(
	int descriptor,
	unsigned number,
	unsigned fingers,
	unsigned oversize)
{
	struct input_event events[80];
	struct pollfd room;
	unsigned count;
	unsigned finger;
	uint64_t now;

	/* A too large report has 17 fingers. */
	if (oversize == 1U)
		fingers = 17U;

	/* Its time, then each finger. */
	memset(events, 0, sizeof(events));
	count = 0U;
	if (oversize != 2U) {
		events[count].type = EV_MSC;
		events[count].code = MSC_TIMESTAMP;
		events[count].value = (int32_t)number;
		count++;
	}
	for (finger = 0U; finger < fingers; finger++) {
		events[count].type = EV_ABS;
		events[count].code = ABS_MT_SLOT;
		events[count].value = (int32_t)finger;
		count++;
		events[count].type = EV_ABS;
		events[count].code = ABS_MT_TRACKING_ID;
		events[count].value = (int32_t)(finger + 1U);
		count++;
		events[count].type = EV_ABS;
		events[count].code = ABS_MT_POSITION_X;
		events[count].value = (int32_t)(number % 10368U);
		count++;
		events[count].type = EV_ABS;
		events[count].code = ABS_MT_POSITION_Y;
		events[count].value = (int32_t)(number % 6912U);
		count++;
	}

	/* The button and the first finger's place, then the report's end. */
	if (oversize != 2U) {
		events[count].type = EV_KEY;
		events[count].code = BTN_TOUCH;
		events[count].value = 1;
		count++;
		events[count].type = EV_ABS;
		events[count].code = ABS_X;
		events[count].value = (int32_t)(number % 10368U);
		count++;
		events[count].type = EV_ABS;
		events[count].code = ABS_Y;
		events[count].value = (int32_t)(number % 6912U);
		count++;
	}
	events[count].type = EV_SYN;
	events[count].code = SYN_REPORT;
	count++;

	/* Stamped now, written in one piece when the pipe has room. */
	now = kwl_microseconds();
	for (finger = 0U; finger < count; finger++) {
		events[finger].time.tv_sec = (time_t)(now / 1000000U);
		events[finger].time.tv_usec = (suseconds_t)(now % 1000000U);
	}
	room.fd = descriptor;
	room.events = POLLOUT;
	(void)poll(&room, 1, -1);
	if (write(descriptor, events, count * sizeof(events[0])) != (ssize_t)(count * sizeof(events[0]))) {
		perror("write");
		exit(2);
	}
}

/* Writes one pen report numbered in its ABS_X: the place, the pressure and the pen near. */
static void
pen_report(
	int descriptor,
	unsigned number)
{
	struct input_event events[5];
	struct pollfd room;
	unsigned index;
	uint64_t now;

	/* The report. */
	memset(events, 0, sizeof(events));
	events[0].type = EV_ABS;
	events[0].code = ABS_X;
	events[0].value = (int32_t)number;
	events[1].type = EV_ABS;
	events[1].code = ABS_Y;
	events[1].value = (int32_t)(number % 17280U);
	events[2].type = EV_ABS;
	events[2].code = ABS_PRESSURE;
	events[2].value = (int32_t)(number % 4096U);
	events[3].type = EV_KEY;
	events[3].code = BTN_TOOL_PEN;
	events[3].value = 1;
	events[4].type = EV_SYN;
	events[4].code = SYN_REPORT;

	/* Stamped now, written in one piece when the pipe has room. */
	now = kwl_microseconds();
	for (index = 0U; index < 5U; index++) {
		events[index].time.tv_sec = (time_t)(now / 1000000U);
		events[index].time.tv_usec = (suseconds_t)(now % 1000000U);
	}
	room.fd = descriptor;
	room.events = POLLOUT;
	(void)poll(&room, 1, -1);
	if (write(descriptor, events, sizeof(events)) != (ssize_t)sizeof(events)) {
		perror("write");
		exit(2);
	}
}

/*
 * Takes the queue as the main thread does until an event of a kind of a
 * device comes, waiting at most wait_ms; the events are freed.  Reports
 * whether it came.
 */
static int
take_until(
	struct kwl_input_thread *input,
	int wake,
	unsigned slot,
	enum kwl_input_event_kind kind,
	unsigned wait_ms)
{
	struct kwl_input_event *events;
	struct kwl_input_event *event;
	struct pollfd ready;
	uint64_t deadline;
	unsigned char drained[64];
	int seen;

	/* Woken by the input thread until the event or the deadline. */
	seen = 0;
	deadline = kwl_milliseconds() + wait_ms;
	while (!seen && kwl_milliseconds() < deadline) {
		ready.fd = wake;
		ready.events = POLLIN;
		(void)poll(&ready, 1, 10);
		while (read(wake, drained, sizeof(drained)) > 0)
			;

		/* The events taken, the one looked for noted. */
		events = kwl_input_thread_take(input);
		while (events != NULL) {
			event = events;
			events = event->next;
			if (event->slot == slot && event->kind == kind)
				seen = 1;
			free(event);
		}
	}

	/* Succeeded: whether the event came in time. */
	return seen;
}

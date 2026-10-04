/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the compositor backend's system events (ws132-p003):
 * libkeiland-backend's backend.c and events-zedbsd.c on the host, with a
 * pipe standing for /dev/system (the test writes struct system_event
 * records into it).  It checks:
 *   - the descriptor joins the backend's poll after the seat's;
 *   - INPUT, AC and BATTERY records of one poll call input_changed and
 *     power_changed once each, however many records came;
 *   - a POWER PRESS calls power_button with the button its subject names,
 *     and a POWER record that is no press calls nothing;
 *   - LID calls lid_changed with 1 (open) and 0 (closed);
 *   - OVERFLOW calls both input_changed and power_changed;
 *   - more records than one read takes are all read in the one poll;
 *   - a poll that reported nothing reads nothing;
 *   - a part of a record, and the end of the descriptor, close it and leave
 *     the poll (count 0).
 *
 *   plan/ws132/tests/run-host-events.sh
 */

#include "userland/desktop/libkeiland-backend/backend-private.h"

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <uapi/system.h>

/* What the host's callbacks were told. */
struct told {
	int input;
	int power;
	int buttons;
	unsigned button;
	int lids;
	unsigned lid;
};

static void input_changed(void *data);
static void power_changed(void *data);
static void power_button(void *data, unsigned button);
static void lid_changed(void *data, unsigned open);
static void check(int condition, const char *what);
static void post(int descriptor, uint32_t class_bit, uint32_t action, int32_t value, const char *subject);
static void poll_once(struct kl_backend *backend, short revents);

/* The checks that failed, and those that ran. */
static int failures;
static int checks;

/* An input device came or went. */
static void
input_changed(
	void *data)
{
	struct told *told;

	/* Counted. */
	told = data;
	told->input++;
}

/* The power changed. */
static void
power_changed(
	void *data)
{
	struct told *told;

	/* Counted. */
	told = data;
	told->power++;
}

/* A button was pressed. */
static void
power_button(
	void *data,
	unsigned button)
{
	struct told *told;

	/* Counted, with the last button. */
	told = data;
	told->buttons++;
	told->button = button;
}

/* The lid moved. */
static void
lid_changed(
	void *data,
	unsigned open)
{
	struct told *told;

	/* Counted, with the last state. */
	told = data;
	told->lids++;
	told->lid = open;
}

/* Counts one check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	/* Counted. */
	checks++;
	if (condition)
		return;

	/* Failed. */
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

/* Writes one record into the pipe. */
static void
post(
	int descriptor,
	uint32_t class_bit,
	uint32_t action,
	int32_t value,
	const char *subject)
{
	struct system_event event;
	ssize_t written;

	/* The record. */
	memset(&event, 0, sizeof(event));
	event.size = sizeof(event);
	event.class_bit = class_bit;
	event.action = action;
	event.value = value;
	snprintf(event.subject, sizeof(event.subject), "%s", subject);

	/* Into the pipe, whole. */
	written = write(descriptor, &event, sizeof(event));
	check(written == (ssize_t)sizeof(event), "a record written whole");
}

/* Fills the backend's poll and hands it back with revents set on the events' descriptor. */
static void
poll_once(
	struct kl_backend *backend,
	short revents)
{
	struct pollfd descriptors[4];
	size_t count;

	/* The backend's descriptors: the events' one is the only one here (the seat has none). */
	count = kl_backend_poll_count(backend);
	memset(descriptors, 0, sizeof(descriptors));
	kl_backend_poll_fill(backend, descriptors);
	if (count == 1U)
		descriptors[0].revents = revents;

	/* What poll reported. */
	kl_backend_poll_done(backend, descriptors);
}

int
main(void)
{
	struct kl_backend_options options;
	struct kl_backend_host host;
	struct kl_backend *backend;
	struct pollfd descriptors[2];
	struct told told;
	char part[10];
	int pipes[2];
	int error;
	int index;

	/* The backend, with the four callbacks; /dev/system is not there on the host. */
	memset(&options, 0, sizeof(options));
	options.greeter_descriptor = -1;
	options.session_descriptor = -1;
	memset(&host, 0, sizeof(host));
	memset(&told, 0, sizeof(told));
	host.data = &told;
	host.input_changed = input_changed;
	host.power_changed = power_changed;
	host.power_button = power_button;
	host.lid_changed = lid_changed;
	error = kl_backend_open(&options, &host, &backend);
	check(error == 0, "the backend opens");
	check(kl_backend_poll_count(backend) == 0U, "no events' descriptor without /dev/system");

	/* A pipe stands for /dev/system, read without waiting. */
	error = pipe(pipes);
	check(error == 0, "the pipe");
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	backend->events_descriptor = pipes[0];
	check(kl_backend_poll_count(backend) == 1U, "the events' descriptor joins the poll");
	memset(descriptors, 0, sizeof(descriptors));
	kl_backend_poll_fill(backend, descriptors);
	check(descriptors[0].fd == pipes[0] && descriptors[0].events == POLLIN, "filled for reading");

	/* Input devices and the power: one call each for the poll. */
	post(pipes[1], KERN_SYSTEM_EVENT_INPUT, KERN_SYSTEM_EVENT_ADD, 0, "event3");
	post(pipes[1], KERN_SYSTEM_EVENT_INPUT, KERN_SYSTEM_EVENT_REMOVE, 0, "event2");
	post(pipes[1], KERN_SYSTEM_EVENT_AC, KERN_SYSTEM_EVENT_CHANGE, 0, "ac");
	post(pipes[1], KERN_SYSTEM_EVENT_BATTERY, KERN_SYSTEM_EVENT_CHANGE, 40, "battery0");
	poll_once(backend, POLLIN);
	check(told.input == 1 && told.power == 1, "input_changed and power_changed once each");
	check(told.buttons == 0 && told.lids == 0, "no button, no lid");

	/* A poll that reported nothing reads nothing. */
	post(pipes[1], KERN_SYSTEM_EVENT_INPUT, KERN_SYSTEM_EVENT_ADD, 0, "event4");
	poll_once(backend, 0);
	check(told.input == 1, "nothing read without POLLIN");
	poll_once(backend, POLLIN);
	check(told.input == 2, "read at the next POLLIN");

	/* The buttons. */
	post(pipes[1], KERN_SYSTEM_EVENT_POWER, KERN_SYSTEM_EVENT_PRESS, 1, "power-button");
	poll_once(backend, POLLIN);
	check(told.buttons == 1 && told.button == KL_BACKEND_BUTTON_POWER, "the power button");
	post(pipes[1], KERN_SYSTEM_EVENT_POWER, KERN_SYSTEM_EVENT_PRESS, 1, "sleep-button");
	poll_once(backend, POLLIN);
	check(told.buttons == 2 && told.button == KL_BACKEND_BUTTON_SLEEP, "the sleep button");
	post(pipes[1], KERN_SYSTEM_EVENT_POWER, KERN_SYSTEM_EVENT_CHANGE, 1, "power-button");
	poll_once(backend, POLLIN);
	check(told.buttons == 2, "a POWER record that is no press calls nothing");

	/* The lid. */
	post(pipes[1], KERN_SYSTEM_EVENT_LID, KERN_SYSTEM_EVENT_CHANGE, 0, "lid");
	poll_once(backend, POLLIN);
	check(told.lids == 1 && told.lid == 0U, "the lid closed");
	post(pipes[1], KERN_SYSTEM_EVENT_LID, KERN_SYSTEM_EVENT_CHANGE, 1, "lid");
	poll_once(backend, POLLIN);
	check(told.lids == 2 && told.lid == 1U, "the lid open");

	/* Lost records: both looked at again. */
	post(pipes[1], KERN_SYSTEM_EVENT_OVERFLOW, KERN_SYSTEM_EVENT_CHANGE, 5, "overflow");
	poll_once(backend, POLLIN);
	check(told.input == 3 && told.power == 2, "OVERFLOW calls both");

	/* More records than one read takes: all in one poll, one call. */
	for (index = 0; index < 20; index++)
		post(pipes[1], KERN_SYSTEM_EVENT_INPUT, KERN_SYSTEM_EVENT_ADD, 0, "event5");
	poll_once(backend, POLLIN);
	check(told.input == 4, "twenty records, one input_changed");
	poll_once(backend, POLLIN);
	check(told.input == 4, "the pipe was drained");
	check(kl_backend_poll_count(backend) == 1U, "the descriptor stays after EAGAIN");

	/* A part of a record closes the descriptor. */
	memset(part, 0, sizeof(part));
	check(write(pipes[1], part, sizeof(part)) == (ssize_t)sizeof(part), "a part written");
	poll_once(backend, POLLIN);
	check(kl_backend_poll_count(backend) == 0U, "a part of a record closes the descriptor");
	check(backend->events_descriptor == -1, "the descriptor is -1");
	(void)close(pipes[1]);

	/* The end of the descriptor closes it too. */
	error = pipe(pipes);
	check(error == 0, "a second pipe");
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	backend->events_descriptor = pipes[0];
	(void)close(pipes[1]);
	poll_once(backend, POLLHUP);
	check(kl_backend_poll_count(backend) == 0U, "the end closes the descriptor");

	/* The backend closes (nothing left open). */
	kl_backend_close(backend);
	kl_backend_close(NULL);

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-events: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-events: %d checks passed\n", checks);
	return 0;
}

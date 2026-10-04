/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the system's events (ws132-p002): src/kern/system-event.c
 * compiled freestanding like the kernel, with the locks, the wait queue,
 * the poll notice, the clock and the heap supplied here.  It checks the
 * subscription rules, the class filter, the record's fields, the sequence,
 * the overflow record, short reads, the blocking read and its signal, and
 * the poll answer.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uapi/system.h>

/* The kernel's errno values (uapi/errno.h), which differ from the host's. */
#define K_EINVAL	3
#define K_EINTR		7
#define K_EAGAIN	24

struct spinlock;
struct wait_queue;
struct kern_system_subscriber;

void kern_system_event_post(uint32_t class_bit, uint32_t action, int32_t value, const char *subject, const char *detail);
int kern_system_event_open(struct kern_system_subscriber **result);
void kern_system_event_close(struct kern_system_subscriber *subscriber);
int kern_system_event_subscribe(struct kern_system_subscriber *subscriber, uint32_t classes);
long kern_system_event_read(struct kern_system_subscriber *subscriber, struct system_event *events, size_t capacity, int nonblock);
int kern_system_event_readable(struct kern_system_subscriber *subscriber);

void *kern_calloc(size_t count, size_t size);
void kern_free(void *pointer);
void *kern_memset(void *destination, int value, size_t length);
uint64_t clock_milliseconds(void *context);
void poll_notify(void);
void spin_init(struct spinlock *lock, int rank, const char *name);
unsigned long spin_lock_irqsave(struct spinlock *lock);
void spin_unlock_irqrestore(struct spinlock *lock, unsigned long enabled);
void waitq_init(struct wait_queue *queue, const char *name);
uint64_t waitq_sequence(const struct wait_queue *queue);
int waitq_sleep(struct wait_queue *queue, struct spinlock *lock, uint64_t observed, uint64_t deadline, unsigned flags);
void waitq_wake_all(struct wait_queue *queue);

static void check(int condition, const char *what);

/*
 * What the stand-ins saw and do: the lock's depth (which must be back to
 * zero after every call), the poll notices and wakes, and what a sleep
 * does (0 fails the test, 1 posts a DISK event as another thread would,
 * 2 ends with a signal).
 */
static int lock_depth;
static int poll_notices;
static int wakes;
static int sleeps;
static int sleep_action;
static int failures;
static int checks;

void *
kern_calloc(
	size_t count,
	size_t size)
{
	/* The host heap. */
	return calloc(count, size);
}

void
kern_free(
	void *pointer)
{
	/* The host heap. */
	free(pointer);
}

void *
kern_memset(
	void *destination,
	int value,
	size_t length)
{
	/* The host C library. */
	return memset(destination, value, length);
}

uint64_t
clock_milliseconds(
	void *context)
{
	/* A fixed time: 1.5 s. */
	(void)context;
	return 1500U;
}

void
poll_notify(void)
{
	/* Counted; the lock must not be held. */
	check(lock_depth == 0, "poll_notify outside the lock");
	poll_notices++;
}

void
spin_init(
	struct spinlock *lock,
	int rank,
	const char *name)
{
	/* Nothing to make. */
	(void)lock;
	(void)rank;
	(void)name;
}

unsigned long
spin_lock_irqsave(
	struct spinlock *lock)
{
	/* One level, never taken twice. */
	(void)lock;
	check(lock_depth == 0, "the event lock is not taken twice");
	lock_depth++;
	return 1;
}

void
spin_unlock_irqrestore(
	struct spinlock *lock,
	unsigned long enabled)
{
	/* Back to none. */
	(void)lock;
	(void)enabled;
	check(lock_depth == 1, "the event lock is held at its release");
	lock_depth--;
}

void
waitq_init(
	struct wait_queue *queue,
	const char *name)
{
	/* Nothing to make. */
	(void)queue;
	(void)name;
}

uint64_t
waitq_sequence(
	const struct wait_queue *queue)
{
	/* Any value; the sleep below decides. */
	(void)queue;
	return 0;
}

int
waitq_sleep(
	struct wait_queue *queue,
	struct spinlock *lock,
	uint64_t observed,
	uint64_t deadline,
	unsigned flags)
{
	/* The read sleeps holding the lock, without a deadline, interruptibly. */
	(void)queue;
	(void)lock;
	(void)observed;
	check(lock_depth == 1, "the read sleeps holding the lock");
	check(deadline == 0U, "the read sleeps without a deadline");
	check(flags != 0U, "the read sleeps interruptibly");
	sleeps++;

	/* Another thread posts while the reader sleeps (the lock is released meanwhile). */
	if (sleep_action == 1) {
		lock_depth--;
		kern_system_event_post(KERN_SYSTEM_EVENT_DISK, KERN_SYSTEM_EVENT_ADD, 0, "sd1", "late");
		lock_depth++;
		return 0;
	}

	/* A signal. */
	if (sleep_action == 2)
		return K_EINTR;

	/* An unexpected sleep. */
	check(0, "no unexpected sleep");
	return K_EINTR;
}

void
waitq_wake_all(
	struct wait_queue *queue)
{
	/* Counted. */
	(void)queue;
	wakes++;
}

/* Counts one check, and reports it when it fails. */
static void
check(
	int condition,
	const char *what)
{
	checks++;
	if (condition)
		return;
	failures++;
	fprintf(stderr, "FAIL: %s\n", what);
}

int
main(void)
{
	struct kern_system_subscriber *first;
	struct kern_system_subscriber *second;
	struct system_event events[100];
	char long_text[80];
	long taken;
	int error;
	int index;
	int ok;

	/* The record's size is fixed. */
	check(sizeof(struct system_event) == 128U, "a record is 128 bytes");

	/* An open hears nothing and cannot read until it subscribes. */
	error = kern_system_event_open(&first);
	check(error == 0, "open");
	taken = kern_system_event_read(first, events, 1, 1);
	check(taken == -K_EINVAL, "a read before the subscription is K_EINVAL");
	check(kern_system_event_readable(first) == 0, "nothing readable before the subscription");

	/* A subscription to nothing or to an unknown class is refused. */
	error = kern_system_event_subscribe(first, 0U);
	check(error == K_EINVAL, "a subscription to nothing is K_EINVAL");
	error = kern_system_event_subscribe(first, 0x100U);
	check(error == K_EINVAL, "an unknown class is K_EINVAL");
	error = kern_system_event_subscribe(first, KERN_SYSTEM_EVENT_OVERFLOW);
	check(error == K_EINVAL, "OVERFLOW is not a class to subscribe to");

	/* DISK and POWER: a LID event is not heard, a DISK event is. */
	error = kern_system_event_subscribe(first, KERN_SYSTEM_EVENT_DISK | KERN_SYSTEM_EVENT_POWER);
	check(error == 0, "subscribe DISK|POWER");
	poll_notices = 0;
	kern_system_event_post(KERN_SYSTEM_EVENT_LID, KERN_SYSTEM_EVENT_CHANGE, 0, "lid", "");
	check(poll_notices == 0, "an event nobody hears notifies no poll");
	check(kern_system_event_readable(first) == 0, "an unsubscribed class is not heard");
	kern_system_event_post(KERN_SYSTEM_EVENT_DISK, KERN_SYSTEM_EVENT_ADD, 7, "sd0", "removable=1");
	check(poll_notices == 1, "an event heard notifies the polls");
	check(kern_system_event_readable(first) == 1, "the event is readable");
	taken = kern_system_event_read(first, events, 4, 1);
	check(taken == 1, "one record read");
	check(events[0].size == 128U, "the record's size");
	check(events[0].class_bit == KERN_SYSTEM_EVENT_DISK, "the record's class");
	check(events[0].action == KERN_SYSTEM_EVENT_ADD, "the record's action");
	check(events[0].value == 7, "the record's value");
	check(events[0].sequence == 1U, "the sequence counts every post, heard or not");
	check(events[0].time_ns == 1500000000ULL, "the record's time");
	check(strcmp(events[0].subject, "sd0") == 0, "the record's subject");
	check(strcmp(events[0].detail, "removable=1") == 0, "the record's detail");

	/* Nothing more: a nonblocking read is K_EAGAIN. */
	taken = kern_system_event_read(first, events, 4, 1);
	check(taken == -K_EAGAIN, "an empty nonblocking read is K_EAGAIN");
	taken = kern_system_event_read(first, events, 0, 1);
	check(taken == -K_EINVAL, "a read of no record is K_EINVAL");

	/* Texts too long are cut short and ended; no text is empty. */
	memset(long_text, 'x', sizeof(long_text) - 1U);
	long_text[sizeof(long_text) - 1U] = '\0';
	kern_system_event_post(KERN_SYSTEM_EVENT_POWER, KERN_SYSTEM_EVENT_PRESS, 1, long_text, NULL);
	taken = kern_system_event_read(first, events, 4, 1);
	check(taken == 1, "the long record read");
	check(strlen(events[0].subject) == KERN_SYSTEM_EVENT_SUBJECT_MAX - 1U, "the subject is cut to fit");
	check(events[0].detail[0] == '\0', "no detail is empty");

	/* Short reads take the oldest first. */
	kern_system_event_post(KERN_SYSTEM_EVENT_DISK, KERN_SYSTEM_EVENT_ADD, 1, "a", "");
	kern_system_event_post(KERN_SYSTEM_EVENT_DISK, KERN_SYSTEM_EVENT_ADD, 2, "b", "");
	kern_system_event_post(KERN_SYSTEM_EVENT_DISK, KERN_SYSTEM_EVENT_REMOVE, 3, "c", "");
	taken = kern_system_event_read(first, events, 2, 1);
	check(taken == 2 && events[0].value == 1 && events[1].value == 2, "a short read takes the oldest two");
	taken = kern_system_event_read(first, events, 2, 1);
	check(taken == 1 && events[0].value == 3, "the next read takes the rest");

	/* A full ring loses its oldest records and says so first. */
	for (index = 0; index < 70; index++)
		kern_system_event_post(KERN_SYSTEM_EVENT_DISK, KERN_SYSTEM_EVENT_CHANGE, index, "d", "");
	taken = kern_system_event_read(first, events, 100, 1);
	check(taken == 65, "the overflow record and a full ring");
	check(events[0].class_bit == KERN_SYSTEM_EVENT_OVERFLOW, "the overflow record comes first");
	check(events[0].value == 6, "the overflow record counts the records lost");
	ok = 1;
	for (index = 1; index < 65; index++) {
		if (events[index].value != index + 5)
			ok = 0;
		if (index > 1 && events[index].sequence != events[index - 1].sequence + 1U)
			ok = 0;
	}

	/* Every record in order. */
	check(ok, "the newest 64 records stay, in order");

	/* An overflow alone fits a read of one record. */
	for (index = 0; index < 66; index++)
		kern_system_event_post(KERN_SYSTEM_EVENT_DISK, KERN_SYSTEM_EVENT_CHANGE, index, "d", "");
	taken = kern_system_event_read(first, events, 1, 1);
	check(taken == 1 && events[0].class_bit == KERN_SYSTEM_EVENT_OVERFLOW && events[0].value == 2,
	      "a read of one record takes the overflow record alone");
	taken = kern_system_event_read(first, events, 100, 1);
	check(taken == 64 && events[0].value == 2, "then the ring");

	/* Two subscribers hear their own classes; a closed one hears nothing. */
	error = kern_system_event_open(&second);
	check(error == 0, "second open");
	error = kern_system_event_subscribe(second, KERN_SYSTEM_EVENT_INPUT);
	check(error == 0, "subscribe INPUT");
	wakes = 0;
	kern_system_event_post(KERN_SYSTEM_EVENT_INPUT, KERN_SYSTEM_EVENT_ADD, 0, "event3", "");
	check(wakes == 1, "only the subscriber of the class wakes");
	check(kern_system_event_readable(first) == 0, "the first does not hear INPUT");
	check(kern_system_event_readable(second) == 1, "the second hears INPUT");
	kern_system_event_close(second);
	kern_system_event_post(KERN_SYSTEM_EVENT_INPUT, KERN_SYSTEM_EVENT_REMOVE, 0, "event3", "");
	check(wakes == 1, "a closed subscriber hears nothing");

	/* A blocking read sleeps until a post. */
	sleep_action = 1;
	sleeps = 0;
	taken = kern_system_event_read(first, events, 4, 0);
	check(sleeps == 1, "the blocking read slept once");
	check(taken == 1 && strcmp(events[0].detail, "late") == 0, "the blocking read takes the post that woke it");

	/* A signal ends the sleep. */
	sleep_action = 2;
	taken = kern_system_event_read(first, events, 4, 0);
	check(taken == -K_EINTR, "a signal ends the blocking read with K_EINTR");
	sleep_action = 0;

	/* The subscriber goes; the lock was always released. */
	kern_system_event_close(first);
	kern_system_event_close(NULL);
	check(lock_depth == 0, "the lock is released at the end");

	/* The result. */
	if (failures != 0) {
		fprintf(stderr, "host-system-event: %d of %d checks failed\n", failures, checks);
		return 1;
	}

	/* Every check passed. */
	printf("host-system-event: %d checks passed\n", checks);
	return 0;
}

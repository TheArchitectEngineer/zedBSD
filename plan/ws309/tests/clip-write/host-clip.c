/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * Host test of the clipboard's write of the offered text (wayland/clipboard.c):
 * a text larger than a pipe holds is put in the history, made the
 * selection, and written to a reader that does not read at first.  The
 * offer must return at once (no wait for the reader), the rest must follow
 * in the passes (kwl_clipboard_poll) as the reader reads, and every byte
 * must arrive.  A reader that never reads is given up after the write's
 * time; the lock's clear ends a write under way.
 */

#include "userland/desktop/wayland/data.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The test's text: within the history's limit, larger than the reader's pipe (READER_PIPE). */
#define TEXT_SIZE	(60U * 1024U)

/* The reader's pipe size, so the offer cannot write the text at once. */
#define READER_PIPE	4096

/* The text the stubbed source writes, and how many checks failed. */
static char text_source[TEXT_SIZE];
static unsigned failures;

static void check(int condition, const char *what);
static uint64_t now_ms(void);
static void sleep_ms(unsigned milliseconds);
static void fill_history(struct kwl_server *server);
static size_t drain(int descriptor, char *into, size_t capacity);

/* The compositor's clock, as the event loop has it. */
uint64_t
kwl_milliseconds(void)
{
	/* The host's monotonic clock. */
	return now_ms();
}

/* The source's send: the test's text into the pipe, then its end. */
int
kwl_data_send(
	struct kwl_object *source,
	const char *type,
	int descriptor)
{
	size_t written;
	ssize_t count;

	/* All of it (the reader reads alongside, in fill_history). */
	(void)source;
	(void)type;
	written = 0U;
	while (written < sizeof(text_source)) {
		count = write(descriptor, text_source + written, sizeof(text_source) - written);
		if (count <= 0)
			break;
		written += (size_t)count;
	}
	close(descriptor);
	return 0;
}

/* The selection offered: nothing to tell in the test. */
void
kwl_data_select_offered(
	struct kwl_server *server)
{
	/* Nothing. */
	(void)server;
}

/* The paste: nothing to send in the test. */
int
kwl_edit_action(
	struct kwl_server *server,
	unsigned action)
{
	/* Nothing. */
	(void)server;
	(void)action;
	return 0;
}

int
main(void)
{
	static char received[TEXT_SIZE];
	struct kwl_server *server;
	uint64_t started;
	uint64_t took;
	size_t got;
	size_t total;
	unsigned index;
	int pipes[2];
	int error;

	/* The text, and the history holding it. */
	for (index = 0U; index < sizeof(text_source); index++)
		text_source[index] = (char)('a' + (index % 26U));
	server = calloc(1, sizeof(*server));
	check(server != NULL, "server");
	fill_history(server);
	check(kwl_clipboard_history_count(server) == 1U, "history has the text");
	error = kwl_clipboard_history_paste(server, 0U);
	check(error == 0, "paste");

	/* A reader that does not read yet: the offer returns at once. */
	error = pipe(pipes);
	check(error == 0, "pipe");
	(void)fcntl(pipes[1], F_SETPIPE_SZ, READER_PIPE);
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	started = now_ms();
	kwl_clipboard_offer_write(pipes[1]);
	took = now_ms() - started;
	check(took < 20U, "the offer does not wait");

	/* The reader reads; the passes write the rest, then the end. */
	total = drain(pipes[0], received, sizeof(received));
	check(total < sizeof(text_source), "the first offer wrote a part");
	started = now_ms();
	while (total < sizeof(received) && now_ms() - started < 3000U) {
		kwl_clipboard_poll(server);
		got = drain(pipes[0], received + total, sizeof(received) - total);
		total += got;
		sleep_ms(1U);
	}
	kwl_clipboard_poll(server);
	check(total == sizeof(text_source), "every byte arrived");
	check(memcmp(received, text_source, sizeof(text_source)) == 0, "the bytes are the text");
	(void)fcntl(pipes[0], F_SETFL, 0);
	check(read(pipes[0], received, 1) == 0, "the end after the text");
	close(pipes[0]);

	/* A reader that never reads is given up after the write's time. */
	error = pipe(pipes);
	check(error == 0, "pipe 2");
	(void)fcntl(pipes[1], F_SETPIPE_SZ, READER_PIPE);
	kwl_clipboard_offer_write(pipes[1]);
	started = now_ms();
	while (now_ms() - started < 2600U) {
		kwl_clipboard_poll(server);
		sleep_ms(10U);
	}
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	got = drain(pipes[0], received, sizeof(received));
	check(got > 0U && got < sizeof(text_source), "a part fitted");
	(void)fcntl(pipes[0], F_SETFL, 0);
	check(read(pipes[0], received, 1) == 0, "the writer end was closed");
	close(pipes[0]);

	/* The lock's clear ends a write under way. */
	error = pipe(pipes);
	check(error == 0, "pipe 3");
	(void)fcntl(pipes[1], F_SETPIPE_SZ, READER_PIPE);
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	kwl_clipboard_offer_write(pipes[1]);
	kwl_clipboard_history_clear(server, "lock");
	got = drain(pipes[0], received, sizeof(received));
	(void)fcntl(pipes[0], F_SETFL, 0);
	check(read(pipes[0], received, 1) == 0, "the clear closed the write");
	close(pipes[0]);

	/* The verdict. */
	free(server);
	if (failures != 0U) {
		printf("host-clip: FAIL failures=%u\n", failures);
		return 1;
	}
	printf("host-clip: PASS\n");
	return 0;
}

/* Counts a failed check and names it. */
static void
check(
	int condition,
	const char *what)
{
	/* A failure is named. */
	if (!condition) {
		printf("host-clip: failed %s\n", what);
		failures++;
	}
}

/* Reports the monotonic clock in milliseconds. */
static uint64_t
now_ms(void)
{
	struct timespec now;

	/* The clock. */
	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}

/* Sleeps a while. */
static void
sleep_ms(
	unsigned milliseconds)
{
	struct timespec wait;

	/* The sleep. */
	wait.tv_sec = milliseconds / 1000U;
	wait.tv_nsec = (long)(milliseconds % 1000U) * 1000000L;
	(void)nanosleep(&wait, NULL);
}

/* Puts the test's text in the history: a source with a text type, read through the passes. */
static void
fill_history(
	struct kwl_server *server)
{
	static char *types[1] = { "text/plain;charset=utf-8" };
	struct kwl_client *client;
	struct kwl_object *source;
	unsigned pass;

	/* The source. */
	client = calloc(1, sizeof(*client));
	source = calloc(1, sizeof(*source));
	check(client != NULL && source != NULL, "source");
	source->client = client;
	source->mime_types = types;
	source->mime_count = 1U;

	/* The selection; the stub has written all of it before returning, as a pipe would let a reader take. */
	kwl_clipboard_selected(server, source);
	for (pass = 0U; pass < 10U; pass++)
		kwl_clipboard_poll(server);
	free(source);
	free(client);
}

/* Reads what a non-blocking pipe has now, up to capacity; returns how much. */
static size_t
drain(
	int descriptor,
	char *into,
	size_t capacity)
{
	size_t total;
	ssize_t got;

	/* Until it has nothing now. */
	total = 0U;
	while (total < capacity) {
		got = read(descriptor, into + total, capacity - total);
		if (got <= 0)
			break;
		total += (size_t)got;
	}
	return total;
}

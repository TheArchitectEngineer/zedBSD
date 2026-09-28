/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * ws074-p058: the driver of browser's asynchronous loader for
 * run-loader-tests.py.  Fetches URLs through one loader and prints a line
 * for each as it ends: its index, the error (0 on success), the status,
 * the body's length and the body's first bytes (up to 60, spaces and
 * control characters as '.').
 *
 *   host-loader [--ca FILE] [--pause MS] (seq|par) URL...
 *
 * seq fetches the URLs one after another (each after the previous one
 * ended, and --pause milliseconds later); par starts them all at once.
 * The loader is kept across the URLs, so they share its connections and
 * its cache.
 */

#include "net/net.h"

#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* How many URLs the driver takes, and how many descriptors it waits on. */
#define LOADER_URLS	64
#define LOADER_FDS	128

/* What the driver knows of each URL: its index and whether it ended. */
struct fetch {
	int index;
	int ended;
};

static void fetched(void *context, struct net_request *request);
static int run(struct net_loader *loader, struct fetch *fetches, int count);
static void pause_for(long milliseconds);

int
main(
	int argc,
	char **argv)
{
	struct fetch fetches[LOADER_URLS];
	struct net_loader *loader;
	struct net_request *request;
	const char *mode;
	long pause;
	int first;
	int count;
	int index;
	int error;
	int differs;

	/* The options. */
	pause = 0;
	first = 1;
	while (first + 1 < argc && argv[first][0] == '-') {
		/* --ca FILE: the test CA is trusted. */
		differs = strcmp(argv[first], "--ca");
		if (differs == 0) {
			error = net_tls_add_ca_file(argv[first + 1]);
			if (error != 0) {
				fprintf(stderr, "host-loader: cannot trust %s\n", argv[first + 1]);
				return 1;
			}

			/* The option and its file are read. */
			first += 2;
			continue;
		}

		/* --pause MS: the wait between the URLs of seq. */
		differs = strcmp(argv[first], "--pause");
		if (differs == 0) {
			pause = strtol(argv[first + 1], NULL, 10);
			first += 2;
			continue;
		}

		/* Anything else ends the options. */
		break;
	}

	/* A mode and at least one URL, not too many. */
	if (first + 1 >= argc || argc - first - 1 > LOADER_URLS) {
		fprintf(stderr, "usage: host-loader [--ca FILE] [--pause MS] (seq|par) URL...\n");
		return 2;
	}

	/* The mode and the number of URLs. */
	mode = argv[first];
	count = argc - first - 1;

	/* The loader. */
	error = net_loader_create(&loader);
	if (error != 0) {
		fprintf(stderr, "host-loader: no loader (%d)\n", error);
		return 1;
	}

	/* par: every URL at once, then until all have ended. */
	differs = strcmp(mode, "par");
	if (differs == 0) {
		for (index = 0; index < count; index++) {
			fetches[index].index = index;
			fetches[index].ended = 0;
			error = net_loader_fetch(loader, argv[first + 1 + index], fetched, &fetches[index], &request);
			if (error != 0)
				printf("%d start-failed %d\n", index, error);
			if (error != 0)
				fetches[index].ended = 1;
		}

		/* Until they have all ended. */
		error = run(loader, fetches, count);
		net_loader_destroy(loader);
		return error;
	}

	/* seq: each URL after the previous one ended. */
	for (index = 0; index < count; index++) {
		if (index > 0)
			pause_for(pause);
		fetches[index].index = index;
		fetches[index].ended = 0;
		error = net_loader_fetch(loader, argv[first + 1 + index], fetched, &fetches[index], &request);
		if (error != 0) {
			printf("%d start-failed %d\n", index, error);
			continue;
		}

		/* Until it has ended. */
		error = run(loader, fetches + index, 1);
		if (error != 0)
			break;
	}

	/* The loader goes. */
	net_loader_destroy(loader);

	/* Succeeded unless the loader stopped answering. */
	return error;
}

/* Prints a request's outcome when it ends. */
static void
fetched(
	void *context,
	struct net_request *request)
{
	const struct net_response *response;
	struct fetch *fetch;
	size_t index;
	size_t shown;
	int error;
	int c;

	/* The request's error, status and body length. */
	fetch = context;
	fetch->ended = 1;
	error = net_request_error(request);
	response = net_request_response(request);
	if (error != 0 || response == NULL) {
		printf("%d %d 0 0 -\n", fetch->index, error);
		fflush(stdout);
		return;
	}

	/* A response: its status and length. */
	printf("%d 0 %d %zu ", fetch->index, response->status, response->body.length);

	/* The body's first bytes. */
	shown = response->body.length;
	if (shown > 60U)
		shown = 60U;
	for (index = 0; index < shown; index++) {
		c = (unsigned char)response->body.data[index];
		if (c <= ' ' || c >= 0x7f)
			c = '.';
		putchar(c);
	}

	/* The line ends. */
	putchar('\n');
	fflush(stdout);
}

/* Runs the loader until the fetches have ended; 1 when they have not after 30 seconds. */
static int
run(
	struct net_loader *loader,
	struct fetch *fetches,
	int count)
{
	struct pollfd fds[LOADER_FDS];
	size_t used;
	int waiting;
	int timeout;
	int index;
	time_t deadline;
	time_t now;

	/* Until every fetch has ended, each round waits for the loader's descriptors. */
	deadline = time(NULL) + 30;
	now = time(NULL);
	while (now < deadline) {
		waiting = 0;
		for (index = 0; index < count; index++) {
			if (!fetches[index].ended)
				waiting = 1;
		}

		/* All ended, or a wait for the loader's descriptors (at most a second). */
		if (!waiting)
			return 0;
		used = net_loader_poll_fds(loader, fds, LOADER_FDS);
		timeout = net_loader_timeout(loader);
		if (timeout < 0 || timeout > 1000)
			timeout = 1000;
		poll(fds, (nfds_t)used, timeout);
		net_loader_process(loader, fds, used);
		now = time(NULL);
	}

	/* The fetches did not end in time. */
	printf("stuck\n");
	return 1;
}

/* Waits for a number of milliseconds. */
static void
pause_for(
	long milliseconds)
{
	struct timespec time;

	/* Seconds and nanoseconds. */
	time.tv_sec = milliseconds / 1000;
	time.tv_nsec = (milliseconds % 1000) * 1000000L;
	nanosleep(&time, NULL);
}

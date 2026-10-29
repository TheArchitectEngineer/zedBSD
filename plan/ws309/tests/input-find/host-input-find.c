/*
 * zedBSD
 * Copyright (C) 2026 Awe Morris
 *
 * SPDX-License-Identifier: Zlib
 */

/*
 * The host test of the zedBSD backend's finding of the input devices on a
 * thread (libkeiland-backend-zedbsd/input-zedbsd.c kl_backend_input_find)
 * run on a lane of the compositor (wayland/lane.c), with /dev/input faked
 * by the linker's --wrap: event0 opens, event4 waits 80 ms and fails with
 * EIO (a PS/2 mouse port without a mouse), event5 opens.
 */

#include "userland/desktop/libkeiland-backend/backend-private.h"
#include "userland/desktop/libkeiland-backend/keiland-backend-evdev.h"
#include "userland/desktop/wayland/lane.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The fake directory's entries and the opens of each node. */
static const char *fake_entries[] = { ".", "..", "event0", "mice", "event4", "event5" };
static unsigned fake_next;
static int fake_directory;
static pthread_mutex_t fake_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned opens[8];
static pthread_t opener;
static unsigned opener_known;
static unsigned opener_other;
static unsigned failures;

/* One finding as a lane job. */
struct find_job {
	struct kwl_lane_job lane;
	struct kl_backend *backend;
	struct kl_backend_input_find find;
	int error;
};

DIR *__real_opendir(const char *path);
struct dirent *__real_readdir(DIR *directory);
int __real_closedir(DIR *directory);
int __real_open(const char *path, int flags, ...);

static void check(int condition, const char *what);
static void find_run(struct kwl_lane_job *job);
static void find_done(void *context, struct kwl_lane_job *job);
static void find_drop(struct kwl_lane_job *job);
static struct find_job *find_on_lane(struct kwl_lane *lane, int pipe_read, struct kl_backend *backend, const char *known);
static void close_nodes(struct find_job *job);
static int found(const struct find_job *job, const char *path);
static struct find_job *taken_job;

/* The operations of a finding. */
static const struct kwl_lane_ops find_ops = { find_run, find_done, find_drop };

int
main(void)
{
	struct kl_backend *backend;
	struct kwl_lane lane;
	struct find_job *job;
	struct kl_backend_input_find begin;
	int pipes[2];
	int error;
	int index;

	/* The backend's record as kl_backend_open leaves it, with the system's events heard. */
	backend = calloc(1U, sizeof(*backend));
	check(backend != NULL, "backend");
	backend->events_descriptor = 99;
	error = pipe(pipes);
	check(error == 0, "pipe");
	(void)fcntl(pipes[0], F_SETFL, O_NONBLOCK);
	(void)fcntl(pipes[1], F_SETFL, O_NONBLOCK);
	error = kwl_lane_start(&lane, "io-device", 1U, pipes[1]);
	check(error == 0, "lane");
	check(kl_backend_input_find_supported() == 1, "supported");

	/* First finding: event0 and event5 open on the thread, event4 refuses. */
	job = find_on_lane(&lane, pipes[0], backend, NULL);
	check(job->error == 0, "find 1 error");
	check(job->find.count == 2U, "find 1 count");
	check(found(job, "/dev/input/event0"), "find 1 event0");
	check(found(job, "/dev/input/event5"), "find 1 event5");
	check(opens[4] == 1U, "event4 tried once");
	check(opener_other == 3U, "opened on the lane thread");
	check(opener_known == 0U, "never on the main thread");
	close_nodes(job);
	free(job);

	/* Second finding, event0 known: event4 remembered, so only event5 opens. */
	job = find_on_lane(&lane, pipes[0], backend, "/dev/input/event0");
	check(job->find.count == 1U, "find 2 count");
	check(found(job, "/dev/input/event5"), "find 2 event5");
	check(opens[4] == 1U, "event4 passed by");
	close_nodes(job);
	free(job);

	/* An input device came or went: event4 is tried again. */
	backend->input_refused_generation++;
	job = find_on_lane(&lane, pipes[0], backend, NULL);
	check(opens[4] == 2U, "event4 tried after the event");
	close_nodes(job);
	free(job);

	/* Without the system's events nothing is remembered: event4 is tried at every finding. */
	backend->events_descriptor = -1;
	for (index = 0; index < 2; index++) {
		job = find_on_lane(&lane, pipes[0], backend, NULL);
		close_nodes(job);
		free(job);
	}
	check(opens[4] == 4U, "event4 tried each time without events");

	/* The preparation copies the state on this thread. */
	memset(&begin, 0, sizeof(begin));
	backend->events_descriptor = 99;
	kl_backend_input_find_begin(backend, &begin);
	check(begin.events_heard == 1U, "begin events");
	check(begin.refused_generation == backend->input_refused_generation, "begin generation");

	/* The verdict. */
	kwl_lane_stop(&lane);
	free(backend);
	close(pipes[0]);
	close(pipes[1]);
	if (failures != 0U) {
		printf("host-input-find: FAIL failures=%u\n", failures);
		return 1;
	}
	printf("host-input-find: PASS\n");
	return 0;
}

/* Counts a failed check. */
static void
check(
	int condition,
	const char *what)
{
	if (condition)
		return;
	printf("host-input-find: check failed: %s\n", what);
	failures++;
}

/* Runs a finding on the lane's thread. */
static void
find_run(
	struct kwl_lane_job *lane_job)
{
	struct find_job *job;

	job = (struct find_job *)lane_job;
	job->error = kl_backend_input_find(job->backend, &job->find);
}

/* Keeps the finding taken for the test. */
static void
find_done(
	void *context,
	struct kwl_lane_job *lane_job)
{
	(void)context;
	taken_job = (struct find_job *)lane_job;
}

/* Frees a finding not taken. */
static void
find_drop(
	struct kwl_lane_job *lane_job)
{
	close_nodes((struct find_job *)lane_job);
	free(lane_job);
}

/* Prepares a finding on this thread, runs it on the lane and takes it back. */
static struct find_job *
find_on_lane(
	struct kwl_lane *lane,
	int pipe_read,
	struct kl_backend *backend,
	const char *known)
{
	struct find_job *job;
	unsigned char bytes[16];
	struct timespec span;
	unsigned waited;

	job = calloc(1U, sizeof(*job));
	if (job == NULL)
		exit(1);
	job->backend = backend;
	if (known != NULL) {
		(void)snprintf(job->find.known[0], sizeof(job->find.known[0]), "%s", known);
		job->find.known_count = 1U;
	}
	kl_backend_input_find_begin(backend, &job->find);
	opener = pthread_self();
	taken_job = NULL;
	kwl_lane_submit(lane, &job->lane, &find_ops, 0U);
	span.tv_sec = 0;
	span.tv_nsec = 1000000L;
	for (waited = 0U; waited < 5000U && taken_job == NULL; waited++) {
		if (read(pipe_read, bytes, sizeof(bytes)) > 0)
			(void)kwl_lane_take(lane, NULL);
		else
			(void)nanosleep(&span, NULL);
	}
	check(taken_job == job, "finding taken");
	return job;
}

/* Closes the nodes a finding opened. */
static void
close_nodes(
	struct find_job *job)
{
	unsigned index;

	for (index = 0U; index < job->find.count; index++)
		(void)close(job->find.nodes[index].descriptor);
	job->find.count = 0U;
}

/* Tells whether a finding opened a path. */
static int
found(
	const struct find_job *job,
	const char *path)
{
	unsigned index;

	for (index = 0U; index < job->find.count; index++) {
		if (strcmp(job->find.nodes[index].path, path) == 0)
			return 1;
	}
	return 0;
}

/* The fake /dev/input directory. */
DIR *
__wrap_opendir(
	const char *path)
{
	if (strcmp(path, "/dev/input") != 0)
		return __real_opendir(path);
	fake_next = 0U;
	return (DIR *)&fake_directory;
}

/* The fake directory's next entry. */
struct dirent *
__wrap_readdir(
	DIR *directory)
{
	static struct dirent entry;

	if (directory != (DIR *)&fake_directory)
		return __real_readdir(directory);
	if (fake_next >= sizeof(fake_entries) / sizeof(fake_entries[0]))
		return NULL;
	memset(&entry, 0, sizeof(entry));
	(void)snprintf(entry.d_name, sizeof(entry.d_name), "%s", fake_entries[fake_next]);
	fake_next++;
	return &entry;
}

/* The fake directory's end. */
int
__wrap_closedir(
	DIR *directory)
{
	if (directory != (DIR *)&fake_directory)
		return __real_closedir(directory);
	return 0;
}

/* The fake nodes: event0 and event5 open /dev/null, event4 waits 80 ms and fails with EIO. */
int
__wrap_open(
	const char *path,
	int flags,
	...)
{
	struct timespec span;
	va_list arguments;
	unsigned node;
	int mode;

	mode = 0;
	if ((flags & O_CREAT) != 0) {
		va_start(arguments, flags);
		mode = va_arg(arguments, int);
		va_end(arguments);
	}
	if (strncmp(path, "/dev/input/event", 16) != 0)
		return __real_open(path, flags, mode);
	node = (unsigned)strtoul(path + 16, NULL, 10);
	pthread_mutex_lock(&fake_lock);
	if (node < 8U)
		opens[node]++;
	if (pthread_equal(pthread_self(), opener))
		opener_known++;
	else
		opener_other++;
	pthread_mutex_unlock(&fake_lock);
	if (node == 4U) {
		span.tv_sec = 0;
		span.tv_nsec = 80000000L;
		(void)nanosleep(&span, NULL);
		errno = EIO;
		return -1;
	}
	return __real_open("/dev/null", O_RDONLY | O_CLOEXEC);
}

/* The fake nodes' capabilities: every ioctl succeeds with the bitmaps left empty. */
int
__wrap_ioctl(
	int descriptor,
	unsigned long request,
	...)
{
	(void)descriptor;
	(void)request;
	return 0;
}
